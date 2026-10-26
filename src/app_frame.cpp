// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>

#include "xanatoo/app.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

void sync_window_size(App& app) {
  {
    int nw = 0, nh = 0;
    SDL_GetWindowSize(app.window, &nw, &nh);
    if (nw >= 64 && nh >= 64 && (nw != app.win_w || nh != app.win_h)) {
      app.win_w = nw;
      app.win_h = nh;
      app.needs_layout = true;
      for (auto& sw : app.soft_wins) {
        if (sw.dock != SoftDock::None || sw.maximized)
          apply_soft_dock(app, sw, sw.maximized ? SoftDock::Max : sw.dock);
      }
    }
  }
}

void relayout_if_needed(App& app) {
  if (app.needs_layout && app.win_w >= 64 && app.win_h >= 64) {
    layout_panes(app);
    {
      std::string dk = path_of(app, focused_pane(app));
      if (dk != app.last_fold_doc_key) {
        app.last_fold_doc_key = dk;
        reload_folds_for_focused(app);
      }
    }
    if (app.outline_mode) refresh_outline(app);
    if (app.backlinks_mode) refresh_backlinks(app);
  }
}

// Fill the chrome-facing snapshot of app state for this frame (also drives the
// software gamepad pointer, which synthesises mouse events).
static void build_ui_state(App& app, xanatoo::UiState& ui_state) {
  ui_state.dirty =
      buf(app, focused_pane(app).buf).dirty;
  ui_state.has_path = !path_of(app, focused_pane(app)).empty();
  ui_state.split = app.split;
  ui_state.outline = app.outline_mode;
  ui_state.section_focus = app.section_focus_mode;
  ui_state.justify = app.view_settings.justify;
  ui_state.backlinks = app.backlinks_mode;
  ui_state.diff = app.diff_mode;
  ui_state.can_diff = !shared_doc(app) &&
                      app.panes[0].buf != app.panes[1].buf;
  ui_state.structure = app.structure_mode;
  ui_state.view_line_spacing = app.view_settings.line_spacing;
  ui_state.view_margin_x = app.view_settings.margin_x;
  ui_state.view_max_content_width = app.view_settings.max_content_width;
  ui_state.browse = app.browse_mode;
  ui_state.virtual_keyboard = app.virtual_keyboard;
  ui_state.help = app.help_mode;
  ui_state.selection = !focused_pane(app).sel.empty();
  {
    auto& fd = doc_of(app, focused_pane(app));
    auto nid = focused_pane(app).sel.focus.node;
    // Guard: caret can briefly point at a node from a replaced tree.
    bool nid_ok = false;
    if (nid != xanatoo::INVALID_NODE) {
      try {
        (void)fd.get(nid);
        nid_ok = true;
      } catch (...) {
        xanatoo::ensure_valid_caret(fd, focused_pane(app).sel.focus);
        focused_pane(app).sel.anchor = focused_pane(app).sel.focus;
        nid = focused_pane(app).sel.focus.node;
        try {
          (void)fd.get(nid);
          nid_ok = nid != xanatoo::INVALID_NODE;
        } catch (...) {
          nid_ok = false;
        }
      }
    }
    ui_state.on_list_item =
        nid_ok && fd.get(nid).kind == xanatoo::NodeKind::ListItem;
    ui_state.in_stretch =
        nid_ok && xanatoo::enclosing_stretch(fd, nid) != xanatoo::INVALID_NODE;
  }
  ui_state.doc_label = path_of(app, focused_pane(app)).empty()
                           ? std::string("untitled")
                           : path_of(app, focused_pane(app));
  ui_state.can_nav_back = app.nav_hist_pos > 0;
  ui_state.can_nav_forward =
      app.nav_hist_pos + 1 < static_cast<int>(app.nav_history.size());
  ui_state.history_open = app.history_panel_open;
  ui_state.bookmarks_open = app.bookmarks_panel_open;
  ui_state.history_entries = app.nav_history;
  ui_state.history_pos = app.nav_hist_pos;
  ui_state.bookmarks = app.bookmarks;
  {
    const std::string& cur = path_of(app, focused_pane(app));
    ui_state.current_bookmarked = is_bookmarked(app, cur);
  }
  ui_state.tab_labels.clear();
  ui_state.active_tab = 0;
  for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti) {
    const BufferId bi = app.tab_order[static_cast<size_t>(ti)];
    ui_state.tab_labels.push_back(buffer_tab_label(app, bi));
    if (bi == app.panes[app.focus].buf) ui_state.active_tab = ti;
  }
  ui_state.has_floating = !app.soft_wins.empty();
  // Right-stick software cursor (extra pointer; never touches OS mouse).
  bool stick_active = false;
  if (app.gamepad) {
    auto axis_e = [&](SDL_GameControllerAxis a) -> float {
      Sint16 v = SDL_GameControllerGetAxis(app.gamepad, a);
      const float dead = 8000.f;
      if (v > -dead && v < dead) return 0.f;
      return static_cast<float>(v) / 32767.f;
    };
    if (!app.stick_cursor_init) {
      app.stick_cx = static_cast<float>(app.win_w) * 0.5f;
      app.stick_cy = static_cast<float>(app.win_h) * 0.5f;
      app.stick_cursor_init = true;
    }
    app.stick_cursor_on = true;
    float rx = axis_e(SDL_CONTROLLER_AXIS_RIGHTX);
    float ry = axis_e(SDL_CONTROLLER_AXIS_RIGHTY);
    if (rx != 0.f || ry != 0.f) {
      // Non-linear response: fine aim near centre, faster at full tilt.
      auto curve = [](float v) {
        float s = (v < 0.f) ? -1.f : 1.f;
        float a = std::fabs(v);
        return s * a * a;  // quadratic
      };
      rx = curve(rx);
      ry = curve(ry);
      const float speed = 11.f + 0.014f * static_cast<float>(app.win_w);
      app.stick_cx += rx * speed;
      app.stick_cy += ry * speed;
      if (app.stick_cx < 0.f) app.stick_cx = 0.f;
      if (app.stick_cy < 0.f) app.stick_cy = 0.f;
      if (app.stick_cx > static_cast<float>(app.win_w - 1))
        app.stick_cx = static_cast<float>(app.win_w - 1);
      if (app.stick_cy > static_cast<float>(app.win_h - 1))
        app.stick_cy = static_cast<float>(app.win_h - 1);
      stick_active = true;
    }
    if (app.stick_lmb || app.stick_rmb) stick_active = true;
    if (stick_active) {
      const int ix = static_cast<int>(app.stick_cx);
      const int iy = static_cast<int>(app.stick_cy);
      static int last_ix = -1, last_iy = -1;
      if (ix != last_ix || iy != last_iy || app.stick_lmb || app.stick_rmb) {
        last_ix = ix;
        last_iy = iy;
        // Synthetic events at stick position — OS cursor stays put.
        SDL_Event me{};
        me.type = SDL_MOUSEMOTION;
        me.motion.x = ix;
        me.motion.y = iy;
        me.motion.xrel = 0;
        me.motion.yrel = 0;
        me.motion.windowID = SDL_GetWindowID(app.window);
        me.motion.state = 0;
        if (app.stick_lmb) me.motion.state |= SDL_BUTTON(SDL_BUTTON_LEFT);
        if (app.stick_rmb) me.motion.state |= SDL_BUTTON(SDL_BUTTON_RIGHT);
        SDL_PushEvent(&me);
      }
    }
  }
  ui_state.stick_cursor = app.stick_cursor_on;
  ui_state.stick_active = stick_active;
  ui_state.stick_cx = app.stick_cx;
  ui_state.stick_cy = app.stick_cy;
  ui_state.stick_lmb = app.stick_lmb;
  ui_state.stick_rmb = app.stick_rmb;
}

// Apply one toolbar / menu command to the world. `arg` is the optional
// payload (URL, tab index, virtual key, example name).
void execute_command(App& app, xanatoo::UiCommand cmd, const std::string& arg) {
  using C = xanatoo::UiCommand;
  auto& p = focused_pane(app);
  auto& d = doc_of(app, p);

  // Browse mode: ignore mutating commands from menus/toolbar.
  if (app.browse_mode && cmd != C::None) {
    const C c = cmd;
    const bool allowed =
        c == C::Quit || c == C::OpenDocument || c == C::OpenUrlBar ||
        c == C::NavigateUrl || c == C::NavBack || c == C::NavForward ||
        c == C::ToggleHistory || c == C::ToggleBookmarks ||
        c == C::AddBookmark || c == C::RemoveBookmark || c == C::NewTab ||
        c == C::CloseTab || c == C::NextTab || c == C::PrevTab ||
        c == C::SelectTab || c == C::PopOut || c == C::PopIn || c == C::Save ||
        c == C::SaveAs || c == C::ExportSexpr || c == C::ExportMarkdown ||
        c == C::Reload || c == C::ForceReload || c == C::Copy ||
        c == C::CopyPageUrl || c == C::SelectAll || c == C::SelectBlock ||
        c == C::Find || c == C::ToggleSplit || c == C::ToggleOutline ||
        c == C::ToggleBacklinks || c == C::OpenTranscludeOther ||
        c == C::ToggleDiff || c == C::ToggleStructure || c == C::ToggleBrowse ||
        c == C::ToggleHelp || c == C::ToggleVirtualKeyboard ||
        c == C::OpenFormatDoc || c == C::OpenSemanticDoc ||
        c == C::OpenInWindow || c == C::OpenExample || c == C::LoadDiffDemo ||
        c == C::ZoomIn || c == C::ZoomOut || c == C::ViewLineSpacingTight ||
        c == C::ViewLineSpacingNormal || c == C::ViewLineSpacingRelaxed ||
        c == C::ViewLineSpacingLoose || c == C::ViewMarginsNarrow ||
        c == C::ViewMarginsNormal || c == C::ViewMarginsWide ||
        c == C::ViewMarginsExtra || c == C::ViewMeasureFull ||
        c == C::ViewMeasureComfortable || c == C::ViewMeasureWide ||
        c == C::ToggleSectionFocus || c == C::ToggleJustify;
    if (!allowed) {
      flash_status(app, "Browse mode — F2 to edit");
      cmd = C::None;
    }
  }

  switch (cmd) {
    case C::Quit: {
      if (any_dirty(app) && !app.discard_armed) {
        app.discard_armed = true;
        flash_status(app,
                     "Unsaved changes — Ctrl+S to save, or Quit again to exit");
        break;
      }
      app.discard_armed = false;
      app.running = false;
      break;
    }
    case C::NewDocument: {
      if (!allow_discard_dirty(app, p.buf)) break;
      buf(app, p.buf).doc =
          xanatoo::parse_document(xanatoo::fallback_document(), {});
      path_of(app, p).clear();
      buf(app, p.buf).dirty = false;
      xanatoo::ensure_valid_caret(buf(app, p.buf).doc,
                                  p.sel.focus);
      p.sel.anchor = p.sel.focus;
      p.scroll_y = 0.f;
      p.layout = {};
      drop_height_hints_for_buf(app, p.buf);
      buf(app, p.buf).history.reset(
          buf(app, p.buf).doc, p.sel);
      if (app.panes[0].buf == app.panes[1].buf) {
        app.panes[1 - app.focus].sel = p.sel;
        app.panes[1 - app.focus].scroll_y = 0.f;
        app.panes[1 - app.focus].layout = {};
      }
      for (auto& sw : app.soft_wins)
        if (sw.buf == p.buf) {
          sw.needs_layout = true;
          sw.scroll = 0.f;
          sw.sel = p.sel;
        }
      app.needs_layout = true;
      update_title(app);
      flash_status(app, "New document");
      break;
    }
    case C::OpenDocument:
      open_prompt(app, PromptKind::OpenFile);
      break;
    case C::OpenUrlBar:
      close_prompt(app);
      app.ui.focus_url_bar();
      break;
    case C::NavigateUrl:
      if (!arg.empty()) navigate_to_url(app, arg);
      break;
    case C::NavBack:
      if (app.nav_hist_pos > 0) {
        --app.nav_hist_pos;
        navigate_to_url(app,
                        app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
                        /*record_history=*/false);
      } else {
        flash_status(app, "No previous page");
      }
      break;
    case C::NavForward:
      if (app.nav_hist_pos + 1 < static_cast<int>(app.nav_history.size())) {
        ++app.nav_hist_pos;
        navigate_to_url(app,
                        app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
                        /*record_history=*/false);
      } else {
        flash_status(app, "No next page");
      }
      break;
    case C::NewTab: {
      const BufferId bi = add_buffer(
          app, xanatoo::parse_document(xanatoo::fallback_document(), {}), {});
      app.tab_order.push_back(bi);
      app.panes[app.focus].buf = bi;
      app.panes[app.focus].sel = {};
      xanatoo::ensure_valid_caret(buf(app, bi).doc,
                                  app.panes[app.focus].sel.focus);
      app.panes[app.focus].sel.anchor = app.panes[app.focus].sel.focus;
      app.panes[app.focus].scroll_y = 0.f;
      buf(app, bi).history.reset(
          buf(app, bi).doc, app.panes[app.focus].sel);
      app.needs_layout = true;
      update_title(app);
      flash_status(app, "New tab");
      break;
    }
    case C::CloseTab: {
      int tab_i = -1;
      if (!arg.empty()) {
        try {
          tab_i = std::stoi(arg);
        } catch (...) {
          tab_i = -1;
        }
      }
      if (tab_i < 0 || tab_i >= static_cast<int>(app.tab_order.size())) {
        // Close focused pane's buffer tab
        for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti) {
          if (app.tab_order[static_cast<size_t>(ti)] ==
              app.panes[app.focus].buf) {
            tab_i = ti;
            break;
          }
        }
      }
      if (tab_i < 0 || app.tab_order.empty()) break;
      const BufferId bi = app.tab_order[static_cast<size_t>(tab_i)];
      if (!allow_discard_dirty(app, bi)) break;
      close_tab(app, bi);
      flash_status(app, "Tab closed");
      break;
    }
    case C::SelectTab: {
      int tab_i = 0;
      try {
        tab_i = std::stoi(arg);
      } catch (...) {
        break;
      }
      if (tab_i < 0 || tab_i >= static_cast<int>(app.tab_order.size())) break;
      const BufferId bi = app.tab_order[static_cast<size_t>(tab_i)];
      if (app.panes[app.focus].buf == bi) break;  // already showing
      app.panes[app.focus].buf = bi;
      // Fresh layout + caret for the newly focused buffer.
      xanatoo::ensure_valid_caret(doc_of(app, app.panes[app.focus]),
                                  app.panes[app.focus].sel.focus);
      app.panes[app.focus].sel.anchor = app.panes[app.focus].sel.focus;
      app.panes[app.focus].layout = {};
      app.needs_layout = true;
      update_title(app);
      break;
    }
    case C::NextTab:
    case C::PrevTab:
      if (!app.tab_order.empty()) {
        int cur = 0;
        for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti)
          if (app.tab_order[static_cast<size_t>(ti)] ==
              app.panes[app.focus].buf) {
            cur = ti;
            break;
          }
        const int ntabs = static_cast<int>(app.tab_order.size());
        if (cmd == C::NextTab)
          cur = (cur + 1) % ntabs;
        else
          cur = (cur - 1 + ntabs) % ntabs;
        app.panes[app.focus].buf = app.tab_order[static_cast<size_t>(cur)];
        xanatoo::ensure_valid_caret(doc_of(app, app.panes[app.focus]),
                                    app.panes[app.focus].sel.focus);
        app.panes[app.focus].sel.anchor = app.panes[app.focus].sel.focus;
        app.panes[app.focus].layout = {};
        app.needs_layout = true;
        update_title(app);
      }
      break;
    case C::PopOut:
      pop_out_focused(app);
      break;
    case C::PopIn:
      if (!app.soft_wins.empty())
        pop_in_soft(app, app.soft_wins.size() - 1);
      else
        flash_status(app, "No floating window to dock");
      break;
    case C::ToggleHistory:
      app.history_panel_open = !app.history_panel_open;
      break;
    case C::ToggleBookmarks:
      app.bookmarks_panel_open = !app.bookmarks_panel_open;
      break;
    case C::AddBookmark: {
      std::string url = path_of(app, p);
      if (url.empty()) url = app.gemini_current_url;
      add_bookmark(app, url);
      break;
    }
    case C::RemoveBookmark: {
      std::string url = arg;
      if (url.empty()) {
        url = path_of(app, p);
        if (url.empty()) url = app.gemini_current_url;
      }
      remove_bookmark(app, url);
      break;
    }
    case C::Save: {
      std::string& spath = path_of(app, p);
      if (spath.empty()) {
        open_prompt(app, PromptKind::SaveAs, "untitled.md");
      } else {
        std::string text = xanatoo::serialize_document(d, spath);
        std::ofstream out(spath);
        if (out) {
          out << text;
          buf(app, p.buf).dirty = false;
          app.discard_armed = false;
          update_title(app);
          flash_status(app, "Saved " + spath);
          std::cout << "Saved: " << spath << "\n";
        }
      }
      break;
    }
    case C::SaveAs:
      open_prompt(app, PromptKind::SaveAs,
                  path_of(app, p).empty() ? "untitled.md" : path_of(app, p));
      break;
    case C::ExportSexpr:
    case C::ExportMarkdown: {
      const bool as_sexp = (cmd == C::ExportSexpr);
      const char* ext = as_sexp ? ".sexp" : ".md";
      std::string cur = path_of(app, p);
      std::string target;
      if (cur.empty()) {
        target = as_sexp ? "untitled.sexp" : "untitled.md";
      } else {
        std::filesystem::path fp(cur);
        target = fp.stem().string() + ext;
        if (fp.has_parent_path() && !fp.parent_path().empty())
          target = (fp.parent_path() / (fp.stem().string() + ext)).string();
      }
      open_prompt(app, PromptKind::SaveAs, std::move(target));
      break;
    }
    case C::Reload:
    case C::ForceReload: {
      std::string& rpath = path_of(app, p);
      if (rpath.empty()) break;
      if (buf(app, p.buf).dirty && cmd == C::Reload) {
        flash_status(app, "Unsaved changes — use Force Reload");
        break;
      }
      if (is_in_app_remote_url(rpath)) {
        navigate_to_url(app, rpath, /*record_history=*/false);
        break;
      }
      xanatoo::clear_external_doc_cache();
      buf(app, p.buf).doc = load_doc(app, rpath);
      buf(app, p.buf).dirty = false;
      buf(app, p.buf).history.reset(
          buf(app, p.buf).doc, p.sel);
      for (int pi = 0; pi < 2; ++pi) {
        Pane& rp = app.panes[pi];
        if (rp.buf != p.buf) continue;
        xanatoo::ensure_valid_caret(buf(app, p.buf).doc,
                                    rp.sel.focus);
        rp.sel.anchor = rp.sel.focus;
        rp.scroll_y = 0.f;
        rp.layout = {};
      }
      drop_height_hints_for_buf(app, p.buf);
      reset_blink(app);
      for (auto& sw : app.soft_wins)
        if (sw.buf == p.buf) {
          sw.needs_layout = true;
          sw.scroll = 0.f;
          sw.layout = {};
          sw.sel = p.sel;
        }
      update_title(app);
      app.needs_layout = true;
      flash_status(app, "Reloaded " + rpath);
      break;
    }
    case C::Undo:
      if (buf(app, p.buf).history.undo(d, p.sel)) {
        xanatoo::ensure_valid_caret(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        if (app.panes[0].buf == app.panes[1].buf) {
          Pane& other = app.panes[1 - app.focus];
          xanatoo::ensure_valid_caret(d, other.sel.focus);
          other.sel.anchor = other.sel.focus;
        }
        reset_blink(app);
        drop_height_hints_for_buf(app, p.buf);
        app.needs_layout = true;
        for (auto& sw : app.soft_wins)
          if (sw.buf == p.buf) {
            sw.needs_layout = true;
            sw.layout = {};
            sw.sel = p.sel;
          }
      }
      break;
    case C::Redo:
      if (buf(app, p.buf).history.redo(d, p.sel)) {
        xanatoo::ensure_valid_caret(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        if (app.panes[0].buf == app.panes[1].buf) {
          Pane& other = app.panes[1 - app.focus];
          xanatoo::ensure_valid_caret(d, other.sel.focus);
          other.sel.anchor = other.sel.focus;
        }
        reset_blink(app);
        drop_height_hints_for_buf(app, p.buf);
        app.needs_layout = true;
        for (auto& sw : app.soft_wins)
          if (sw.buf == p.buf) {
            sw.needs_layout = true;
            sw.layout = {};
            sw.sel = p.sel;
          }
      }
      break;
    case C::Cut: {
      std::string text = xanatoo::selected_text(d, p.sel);
      if (!text.empty()) {
        SDL_SetClipboardText(text.c_str());
        edit(app);
        xanatoo::delete_selection(d, p.sel);
        app.needs_layout = true;
        reset_blink(app);
      }
      break;
    }
    case C::CopyPageUrl: {
      std::string url = path_of(app, p);
      if (url.empty()) url = app.gemini_current_url;
      if (url.empty()) {
        flash_status(app, "No page URL");
        break;
      }
      SDL_SetClipboardText(url.c_str());
      flash_status(app, "Copied " + url);
      break;
    }
    case C::Copy: {
      std::string text = xanatoo::selected_text(d, p.sel);
      if (!text.empty()) SDL_SetClipboardText(text.c_str());
      break;
    }
    case C::Paste:
      if (SDL_HasClipboardText()) {
        char* clip = SDL_GetClipboardText();
        if (clip && clip[0]) {
          edit(app);
          if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
          xanatoo::paste_plain_text(d, p.sel.focus, clip);
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
        }
        if (clip) SDL_free(clip);
      }
      break;
    case C::SelectAll:
      xanatoo::select_all(d, p.sel);
      break;
    case C::SelectBlock:
      if (p.sel.focus.node != xanatoo::INVALID_NODE)
        xanatoo::select_node(d, p.sel, p.sel.focus.node);
      break;
    case C::DuplicateBlock:
      edit(app);
      if (auto nid = xanatoo::duplicate_block(d, p.sel.focus.node)) {
        p.sel.focus.node = *nid;
        p.sel.focus.span_index = 0;
        p.sel.focus.char_index = 0;
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
        reset_blink(app);
      }
      break;
    case C::MoveBlockUp:
      edit(app);
      if (xanatoo::move_block_up(d, p.sel.focus.node)) {
        app.needs_layout = true;
        reset_blink(app);
      }
      break;
    case C::MoveBlockDown:
      edit(app);
      if (xanatoo::move_block_down(d, p.sel.focus.node)) {
        app.needs_layout = true;
        reset_blink(app);
      }
      break;
    case C::Find:
      open_search(app, false);
      break;
    case C::Replace:
      open_search(app, true);
      break;
    case C::ReplaceAll: {
      const std::string& query =
          prompt_is_search(app) ? app.prompt.text : app.last_search;
      const std::string& repl =
          prompt_is_search(app) ? app.prompt.text2 : app.last_replace;
      if (!query.empty()) {
        edit(app);
        size_t n = xanatoo::replace_all(d, query, repl);
        app.needs_layout = true;
        std::cout << "Replace all: " << n << " substitution(s)\n";
      } else {
        open_search(app, true);
      }
      break;
    }
    case C::Bold:
      if (!p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel,
                                          xanatoo::InlineKind::Strong);
        app.needs_layout = true;
      }
      break;
    case C::Italic:
      if (!p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel,
                                          xanatoo::InlineKind::Emphasis);
        app.needs_layout = true;
      }
      break;
    case C::Strike:
      if (!p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel,
                                          xanatoo::InlineKind::Strike);
        app.needs_layout = true;
      }
      break;
    case C::Code:
      if (!p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Code);
        app.needs_layout = true;
      }
      break;
    case C::Link:
      if (!p.sel.empty()) {
        std::string text = xanatoo::selected_text(d, p.sel);
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Link,
                                          "#" + text);
        open_prompt(app, PromptKind::EditLink, "#" + text);
        app.prompt.node = p.sel.focus.node;
        app.prompt.span = p.sel.focus.span_index;
        app.needs_layout = true;
      }
      break;
    case C::Cite:
      if (!p.sel.empty()) {
        std::string text = xanatoo::selected_text(d, p.sel);
        std::string key = text;
        for (char& ch : key) {
          if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
          else if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')))
            ch = '-';
        }
        while (!key.empty() && key.front() == '-') key.erase(key.begin());
        while (!key.empty() && key.back() == '-') key.pop_back();
        if (key.empty()) key = "key";
        if (key.size() > 32) key.resize(32);
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Cite,
                                          key);
        open_prompt(app, PromptKind::EditCite, key);
        app.prompt.node = p.sel.focus.node;
        app.prompt.span = p.sel.focus.span_index;
        app.needs_layout = true;
      }
      break;
    case C::InsertBiblioEntry: {
      edit(app);
      std::string key = "key";
      if (xanatoo::insert_biblio_entry(d, p.sel.focus, key)) {
        p.sel.anchor = p.sel.focus;
        // Open key-edit on the entry label via status: reuse link_edit for
        // label (label is not a span; set it when Enter confirms after
        // typing).
        open_prompt(app, PromptKind::EditCite, key);
        app.prompt.node = p.sel.focus.node;
        app.prompt.span = 0;
        app.needs_layout = true;
      }
      break;
    }
    case C::InsertParagraph:
      edit(app);
      if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                        xanatoo::NodeKind::Paragraph)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertHr:
      edit(app);
      if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                        xanatoo::NodeKind::HorizontalRule)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertNote:
      edit(app);
      if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                        xanatoo::NodeKind::Note)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertCodeBlock:
      edit(app);
      if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                        xanatoo::NodeKind::CodeBlock)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertTable:
      edit(app);
      if (xanatoo::insert_table(d, p.sel.focus)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertDefList:
      edit(app);
      if (xanatoo::insert_deflist(d, p.sel.focus)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertColumns:
      edit(app);
      if (xanatoo::insert_columns(d, p.sel.focus, 2)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::InsertTransclude: {
      std::string t;
      if (!p.sel.empty()) {
        t = xanatoo::selected_text(d, p.sel);
        if (!t.empty() && t[0] != '#') t = "#" + t;
      }
      open_prompt(app, PromptKind::InsertInclude, std::move(t));
      break;
    }
    case C::InsertImage:
      open_prompt(app, PromptKind::InsertImage);
      break;
    case C::InsertFigure:
      open_prompt(app, PromptKind::InsertFigure);
      break;
    case C::Heading1:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 1))
        app.needs_layout = true;
      break;
    case C::Heading2:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 2))
        app.needs_layout = true;
      break;
    case C::Heading3:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 3))
        app.needs_layout = true;
      break;
    case C::Heading4:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 4))
        app.needs_layout = true;
      break;
    case C::Heading5:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 5))
        app.needs_layout = true;
      break;
    case C::Heading6:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 6))
        app.needs_layout = true;
      break;
    case C::MakeHeader:
      edit(app);
      if (xanatoo::make_header(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Header");
      }
      break;
    case C::WrapSection:
      edit(app);
      if (xanatoo::wrap_in_section(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Wrapped in section");
      }
      break;
    case C::WrapArticle:
      edit(app);
      if (xanatoo::wrap_in_article(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Wrapped in article");
      }
      break;
    case C::UnwrapSectioning:
      edit(app);
      if (xanatoo::unwrap_sectioning(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Unwrapped section/article");
      }
      break;
    case C::PromoteSection:
      edit(app);
      if (xanatoo::promote_section(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Section promoted");
      } else {
        flash_status(app, "Cannot promote section");
      }
      break;
    case C::DemoteSection:
      edit(app);
      if (xanatoo::demote_section(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Section demoted");
      } else {
        flash_status(app,
                     "Cannot demote section (need previous section sibling)");
      }
      break;
    case C::MoveSectionUp:
      edit(app);
      if (xanatoo::move_section_up(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Section moved up");
      }
      break;
    case C::MoveSectionDown:
      edit(app);
      if (xanatoo::move_section_down(d, p.sel.focus)) {
        app.needs_layout = true;
        flash_status(app, "Section moved down");
      }
      break;
    case C::ParagraphLevel:
      edit(app);
      if (xanatoo::set_heading_level(d, p.sel.focus, 0))
        app.needs_layout = true;
      break;
    case C::ToggleUnorderedList:
      edit(app);
      if (xanatoo::toggle_list(d, p.sel.focus, false)) app.needs_layout = true;
      break;
    case C::ToggleOrderedList:
      edit(app);
      if (xanatoo::toggle_list(d, p.sel.focus, true)) app.needs_layout = true;
      break;
    case C::ToggleTask:
      edit(app);
      if (xanatoo::toggle_task_item(d, p.sel.focus)) app.needs_layout = true;
      break;
    case C::ToggleBlockQuote: {
      edit(app);
      auto& node = d.get(p.sel.focus.node);
      if (node.kind == xanatoo::NodeKind::BlockQuote) {
        node.kind = xanatoo::NodeKind::Paragraph;
      } else if (node.kind == xanatoo::NodeKind::Paragraph ||
                 node.kind == xanatoo::NodeKind::Heading ||
                 node.kind == xanatoo::NodeKind::ListItem ||
                 node.kind == xanatoo::NodeKind::Note) {
        node.kind = xanatoo::NodeKind::BlockQuote;
        node.level = 0;
      }
      app.needs_layout = true;
      break;
    }
    case C::IndentList:
      edit(app);
      if (xanatoo::indent_list_item(d, p.sel.focus)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::OutdentList:
      edit(app);
      if (xanatoo::outdent_list_item(d, p.sel.focus)) {
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
      }
      break;
    case C::WrapStretch: {
      edit(app);
      std::string summary = xanatoo::selected_text(d, p.sel);
      if (xanatoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
        app.needs_layout = true;
      }
      break;
    }
    case C::EditStretchSummary: {
      xanatoo::NodeId sid = xanatoo::enclosing_stretch(d, p.sel.focus.node);
      if (sid != xanatoo::INVALID_NODE) {
        open_prompt(app, PromptKind::EditSummary, d.get(sid).summary);
        app.prompt.node = sid;
      }
      break;
    }
    case C::UnwrapStretch: {
      edit(app);
      if (auto first = xanatoo::unwrap_stretch(d, p.sel.focus.node)) {
        p.sel.focus.node = *first;
        p.sel.focus.span_index = 0;
        p.sel.focus.char_index = 0;
        p.sel.anchor = p.sel.focus;
        app.needs_layout = true;
        reset_blink(app);
      }
      break;
    }
    case C::ToggleSplit:
      app.split = !app.split;
      app.needs_layout = true;
      break;
    case C::ToggleOutline:
      app.outline_mode = !app.outline_mode;
      if (app.outline_mode) {
        refresh_outline(app);
        app.outline_sel = 0;
        app.ol.scroll = 0.f;
      }
      app.needs_layout = true;
      layout_panes(app);
      break;
    case C::ToggleBacklinks:
      app.backlinks_mode = !app.backlinks_mode;
      if (app.backlinks_mode) {
        refresh_backlinks(app);
        app.backlinks_sel = 0;
        app.bl.scroll = 0.f;
        if (app.backlinks_label.empty())
          flash_status(app, "Backlinks: place caret on a labelled node");
        else
          flash_status(app, "Backlinks to #" + app.backlinks_label + " (" +
                                std::to_string(app.backlinks.size()) + ")");
      }
      break;
    case C::OpenTranscludeOther:
      open_transclude_in_other_pane(app);
      break;
    case C::ToggleDiff:
      if (shared_doc(app) || app.buffers.size() < 2) {
        flash_status(app,
                     "Diff needs two files — Examples → Diff demo, or "
                     "xanatoo a.md b.md");
      } else {
        app.diff_mode = !app.diff_mode;
        refresh_diff(app);
        flash_status(app, app.diff_mode ? "Diff marks on" : "Diff marks off");
      }
      break;
    case C::ToggleStructure:
      app.structure_mode = !app.structure_mode;
      flash_status(app, app.structure_mode ? "Structure highlight on"
                                           : "Structure highlight off");
      break;
    case C::ToggleBrowse:
      app.browse_mode = !app.browse_mode;
      if (app.browse_mode) {
        // Clear any status prompts so they do not trap keys
        close_prompt(app);
        flash_status(
            app,
            "Browse mode — read-only · F2 exits · gamepad: D-pad move, "
            "A open, B collapse, LB/RB headers, LT/RT page");
      } else {
        flash_status(app, "Edit mode");
      }
      break;
    case C::ToggleHelp:
      app.help_mode = !app.help_mode;
      if (app.help_mode) {
        ensure_help_doc(app);
        app.help_needs_layout = true;
      }
      break;
    case C::ToggleVirtualKeyboard:
      app.virtual_keyboard = !app.virtual_keyboard;
      flash_status(app, app.virtual_keyboard ? "Virtual keyboard on (F3)"
                                             : "Virtual keyboard off");
      break;
    case C::VirtualKey: {
      if (app.browse_mode) {
        flash_status(app, "Browse mode — F2 to edit");
        break;
      }
      if (arg.empty()) break;
      Pane& vp = focused_pane(app);
      xanatoo::Document& vd = doc_of(app, vp);
      if (arg == "\b") {
        edit(app);
        if (!vp.sel.empty())
          xanatoo::delete_selection(vd, vp.sel);
        else
          xanatoo::delete_backward(vd, vp.sel.focus);
        vp.sel.anchor = vp.sel.focus;
        app.needs_layout = true;
        scroll_caret_into_view(app, vp);
        reset_blink(app);
      } else if (arg == "\n") {
        edit(app);
        if (!vp.sel.empty()) xanatoo::delete_selection(vd, vp.sel);
        xanatoo::split_block(vd, vp.sel.focus);
        vp.sel.anchor = vp.sel.focus;
        app.needs_layout = true;
        scroll_caret_into_view(app, vp);
        reset_blink(app);
      } else {
        edit(app);
        if (!vp.sel.empty()) xanatoo::delete_selection(vd, vp.sel);
        xanatoo::insert_text(vd, vp.sel.focus, arg);
        vp.sel.anchor = vp.sel.focus;
        app.needs_layout = true;
        scroll_caret_into_view(app, vp);
        reset_blink(app);
      }
      break;
    }
    case C::OpenFormatDoc: {
      std::string path = find_repo_file(app, "FORMAT.md");
      if (path.empty()) path = find_repo_file(app, "FORMAT.md");
      open_browse(app, path, "FORMAT.md — format spec");
      break;
    }
    case C::OpenSemanticDoc: {
      std::string path = find_repo_file(app, "semantic.sexp");
      if (path.empty()) path = find_repo_file(app, "examples/semantic.sexp");
      // find_repo_file already tries examples/
      if (path.empty()) {
        for (const char* c :
             {"examples/semantic.sexp", "../examples/semantic.sexp"})
          if (fs::exists(c)) {
            path = fs::absolute(c).string();
            break;
          }
      }
      open_browse(app, path, "semantic.sexp — demo");
      break;
    }
    case C::OpenInWindow:
      open_prompt(app, PromptKind::OpenInWindow);
      break;
    case C::OpenExample: {
      std::string name = arg;
      if (name.empty()) break;
      // Prefer examples/<name> under data dir / cwd (PortMaster layout).
      std::string path = find_repo_file(app, name.c_str());
      if (path.empty())
        path = find_repo_file(app, (std::string("examples/") + name).c_str());
      if (path.empty() || !fs::exists(path)) {
        flash_status(app, "Example not found: " + name);
        std::cerr << "Example not found: " << name
                  << " (set XANATOO_DATA_DIR to the port folder)\n";
        break;
      }
      // Load into the focused pane (not a soft window). On R36S 640×480
      // floating windows are too tight for org tables / gemtext samples.
      if (!navigate_to_url(app, path, /*record_history=*/true)) {
        flash_status(app, "Failed to open: " + name);
        break;
      }
      {
        std::string title = name;
        try {
          title = fs::path(path).filename().string();
        } catch (...) {
        }
        flash_status(app, "Opened example: " + title);
      }
      break;
    }
    case C::LoadDiffDemo: {
      if (any_dirty(app) && !app.discard_armed) {
        app.discard_armed = true;
        flash_status(
            app,
            "Unsaved changes — save first, or repeat Diff demo to discard");
        break;
      }
      app.discard_armed = false;
      std::string p0 = find_repo_file(app, "diff-v1.md");
      std::string p1 = find_repo_file(app, "diff-v2.md");
      if (p0.empty()) p0 = find_repo_file(app, "examples/diff-v1.md");
      if (p1.empty()) p1 = find_repo_file(app, "examples/diff-v2.md");
      if (p0.empty() || p1.empty() || !fs::exists(p0) || !fs::exists(p1)) {
        flash_status(app,
                     "Diff demo files not found (diff-v1.md / diff-v2.md)");
        std::cerr << "Diff demo: examples/diff-v1.md and diff-v2.md required\n";
        break;
      }
      // Drop floats — their layouts refer to old trees.
      app.soft_wins.clear();
      clear_interaction_hover(app);
      app.buffers.clear();
      const BufferId d0 = add_buffer(app, load_doc(app, p0), p0);
      const BufferId d1 = add_buffer(app, load_doc(app, p1), p1);
      app.tab_order = {d0, d1};
      app.panes[0].buf = d0;
      app.panes[1].buf = d1;
      app.split = true;
      app.focus = 0;
      for (int pi = 0; pi < 2; ++pi) {
        Pane& dp = app.panes[pi];
        DocBuffer& db = buf(app, dp.buf);
        xanatoo::ensure_valid_caret(db.doc, dp.sel.focus);
        dp.sel.anchor = dp.sel.focus;
        dp.scroll_y = 0.f;
        db.history.reset(db.doc, dp.sel);
      }
      app.diff_mode = true;
      refresh_diff(app);
      app.needs_layout = true;
      update_title(app);
      flash_status(app, "Diff demo — orange bars mark changed blocks");
      std::cout << "Diff demo: " << p0 << " | " << p1 << "\n";
      break;
    }
    case C::ZoomIn:
      zoom_by(app, 1);
      break;
    case C::ZoomOut:
      zoom_by(app, -1);
      break;
    case C::ViewLineSpacingTight:
      app.view_settings.line_spacing = xanatoo::ViewSettings::kLineTight;
      apply_view_settings(app);
      flash_status(app, "Line spacing: tight");
      break;
    case C::ViewLineSpacingNormal:
      app.view_settings.line_spacing = xanatoo::ViewSettings::kLineNormal;
      apply_view_settings(app);
      flash_status(app, "Line spacing: normal");
      break;
    case C::ViewLineSpacingRelaxed:
      app.view_settings.line_spacing = xanatoo::ViewSettings::kLineRelaxed;
      apply_view_settings(app);
      flash_status(app, "Line spacing: relaxed");
      break;
    case C::ViewLineSpacingLoose:
      app.view_settings.line_spacing = xanatoo::ViewSettings::kLineLoose;
      apply_view_settings(app);
      flash_status(app, "Line spacing: loose");
      break;
    case C::ViewMarginsNarrow:
      app.view_settings.margin_x = xanatoo::ViewSettings::kMarginNarrow;
      apply_view_settings(app);
      flash_status(app, "Margins: narrow");
      break;
    case C::ViewMarginsNormal:
      app.view_settings.margin_x = xanatoo::ViewSettings::kMarginNormal;
      apply_view_settings(app);
      flash_status(app, "Margins: normal");
      break;
    case C::ViewMarginsWide:
      app.view_settings.margin_x = xanatoo::ViewSettings::kMarginWide;
      apply_view_settings(app);
      flash_status(app, "Margins: wide");
      break;
    case C::ViewMarginsExtra:
      app.view_settings.margin_x = xanatoo::ViewSettings::kMarginExtra;
      apply_view_settings(app);
      flash_status(app, "Margins: extra wide");
      break;
    case C::ViewMeasureFull:
      app.view_settings.max_content_width = 0.f;
      apply_view_settings(app);
      flash_status(app, "Measure: full width");
      break;
    case C::ViewMeasureComfortable:
      app.view_settings.max_content_width =
          xanatoo::ViewSettings::kMeasureComfortable;
      apply_view_settings(app);
      flash_status(app, "Measure: comfortable (~720px)");
      break;
    case C::ViewMeasureWide:
      app.view_settings.max_content_width = xanatoo::ViewSettings::kMeasureWide;
      apply_view_settings(app);
      flash_status(app, "Measure: wide (~960px)");
      break;
    case C::ToggleSectionFocus:
      toggle_section_focus_mode(app);
      break;
    case C::ToggleJustify:
      app.view_settings.justify = !app.view_settings.justify;
      apply_view_settings(app);
      if (app.view_settings.justify) {
        if (app.view_settings.max_content_width < 1.f)
          flash_status(
              app, "Justify on — View→Measure Comfortable for book-like width");
        else
          flash_status(app, "Justify on");
      } else {
        flash_status(app, "Justify off");
      }
      break;
    default:
      break;
  }
}

void run_chrome(App& app) {
  xanatoo::UiState ui_state;
  build_ui_state(app, ui_state);
  auto ui_frame = app.ui.begin_frame(ui_state);
  app.ui_top = ui_frame.top_reserved;
  app.ui_bottom = ui_frame.bottom_reserved;
  execute_command(app, ui_frame.cmd, ui_frame.arg);
  if (ui_frame.cmd != xanatoo::UiCommand::None) app.ui.release_input_capture();
}

void poll_gamepad_axes(App& app) {
  if (app.gamepad) {
    auto axis = [&](SDL_GameControllerAxis a) -> float {
      Sint16 v = SDL_GameControllerGetAxis(app.gamepad, a);
      const float dead = 8000.f;
      if (v > -dead && v < dead) return 0.f;
      return static_cast<float>(v) / 32767.f;
    };
    // Left stick Y: scroll focused pane (unchanged).
    float ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    if (ly != 0.f) {
      Pane& p = focused_pane(app);
      float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                     app.ui_bottom;
      float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
      p.scroll_y = std::max(0.f, std::min(max_s, p.scroll_y + ly * 18.f));
    }
    // Triggers as page up/down
    static bool lt_held = false, rt_held = false;
    float lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    float rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (lt > 0.5f) {
      if (!lt_held) {
        nav_page(app, true);
        lt_held = true;
      }
    } else {
      lt_held = false;
    }
    if (rt > 0.5f) {
      if (!rt_held) {
        nav_page(app, false);
        rt_held = true;
      }
    } else {
      rt_held = false;
    }
  } else {
    app.stick_cursor_on = false;
    app.stick_cursor_init = false;
  }
}

// Hover affordances, caret, focus bar, diff marks, structure overlay, drop
// indicators — everything drawn over the text inside the pane clip.
static void draw_pane_overlays(App& app, int i) {
  [[maybe_unused]] const float lh =
      static_cast<float>(app.fonts.regular.line_height());
  [[maybe_unused]] Pane& pane = app.panes[i];
  [[maybe_unused]] const int clip_y = static_cast<int>(app.ui_top);
  [[maybe_unused]] const float ox = static_cast<float>(pane.x);
  [[maybe_unused]] const float draw_scroll = pane.scroll_y - app.ui_top;
  // Hover affordances for interactive elements in this pane
  {
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    // Link / cite run: soft wash + thicker underline
    if (app.hover.link_pane == i &&
        app.hover.link_node != xanatoo::INVALID_NODE &&
        !app.hover.href.empty()) {
      float run_x0 = 0.f, run_x1 = 0.f, run_top = 0.f, run_base = 0.f;
      bool in_run = false;
      auto flush = [&]() {
        if (!in_run) return;
        int x = static_cast<int>(run_x0 + ox) - 2;
        int y = static_cast<int>(run_top - draw_scroll) - 2;
        int w = static_cast<int>(run_x1 - run_x0) + 4;
        int h = app.fonts.regular.line_height() + 4;
        SDL_SetRenderDrawColor(app.sdl, 40, 110, 220, 45);
        SDL_Rect r{x, y, w, h};
        SDL_RenderFillRect(app.sdl, &r);
        int uy = static_cast<int>(run_base + 3.f - draw_scroll);
        SDL_SetRenderDrawColor(app.sdl, 20, 80, 190, 255);
        SDL_RenderDrawLine(app.sdl, x + 1, uy, x + w - 2, uy);
        SDL_RenderDrawLine(app.sdl, x + 1, uy + 1, x + w - 2, uy + 1);
        in_run = false;
      };
      for (const auto& pg : pane.layout.glyphs) {
        if (pg.node != app.hover.link_node ||
            pg.span_index != app.hover.link_span ||
            (pg.kind != xanatoo::InlineKind::Link &&
             pg.kind != xanatoo::InlineKind::Cite)) {
          flush();
          continue;
        }
        xanatoo::Font& font = app.fonts.pick(pg.font);
        const xanatoo::Glyph& g = font.glyph(pg.codepoint);
        float adv = static_cast<float>((g.advance >> 6) > 0 ? (g.advance >> 6)
                                                            : (g.width + 1));
        float top = pg.y - static_cast<float>(font.ascender());
        if (!in_run) {
          run_x0 = pg.x;
          run_x1 = pg.x + adv;
          run_top = top;
          run_base = pg.y;
          in_run = true;
        } else if (std::abs(top - run_top) < 0.5f && pg.x <= run_x1 + 2.f) {
          run_x1 = pg.x + adv;
        } else {
          flush();
          run_x0 = pg.x;
          run_x1 = pg.x + adv;
          run_top = top;
          run_base = pg.y;
          in_run = true;
        }
      }
      flush();
    }
    // Stretch marker wash
    if (app.hover.stretch_pane == i &&
        app.hover.stretch != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.stretch) continue;
        // Only the header line for expanded stretches is interactive;
        // wash the first line-height of the box when expanded.
        float h = box.height;
        auto& hdoc = buf(app, pane.buf).doc;
        if (app.hover.stretch == xanatoo::INVALID_NODE ||
            app.hover.stretch >= hdoc.next_id ||
            static_cast<size_t>(app.hover.stretch - 1) >= hdoc.nodes.size())
          break;
        auto& sn = hdoc.get(app.hover.stretch);
        if (sn.expanded &&
            h > static_cast<float>(app.fonts.regular.line_height()) + 8.f)
          h = static_cast<float>(app.fonts.regular.line_height()) + 6.f;
        SDL_SetRenderDrawColor(app.sdl, 40, 130, 90, 50);
        SDL_Rect r{pane.x + static_cast<int>(box.x),
                   static_cast<int>(box.y - pane.scroll_y + app.ui_top),
                   static_cast<int>(box.width), static_cast<int>(h)};
        SDL_RenderFillRect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 30, 120, 80, 200);
        SDL_RenderDrawRect(app.sdl, &r);
        break;
      }
    }
    // Image outline + emphasized resize handle
    if (app.hover.image_pane == i && app.hover.image != xanatoo::INVALID_NODE) {
      for (const auto& pi : pane.layout.images) {
        if (pi.node != app.hover.image) continue;
        int ix = pane.x + static_cast<int>(pi.x);
        int iy = static_cast<int>(pi.y - pane.scroll_y + app.ui_top);
        int iw = static_cast<int>(pi.width);
        int ih = static_cast<int>(pi.height);
        SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 180);
        SDL_Rect hover_rect{ix, iy, iw, ih};
        SDL_RenderDrawRect(app.sdl, &hover_rect);
        const int hs = app.hover.image_handle ? 14 : 10;
        SDL_Rect handle{ix + iw - hs, iy + ih - hs, hs, hs};
        SDL_SetRenderDrawColor(app.sdl, app.hover.image_handle ? 15 : 25,
                               app.hover.image_handle ? 70 : 90,
                               app.hover.image_handle ? 200 : 170, 255);
        SDL_RenderFillRect(app.sdl, &handle);
        if (app.hover.image_handle) {
          SDL_SetRenderDrawColor(app.sdl, 255, 255, 255, 220);
          SDL_RenderDrawRect(app.sdl, &handle);
        }
        break;
      }
    }
    // Column gutter highlight
    if (app.hover.col_gap_pane == i && app.hover.col_gap) {
      float mx = app.mouse_x - static_cast<float>(pane.x);
      float my = content_y(app, app.mouse_y);
      for (const auto& gap : pane.layout.column_gaps) {
        float gx = gap.x;
        float gy0 = gap.y - pane.scroll_y;
        float gy1 = gy0 + gap.height;
        if (mx >= gx - 5.f && mx <= gx + 5.f && my >= gy0 && my <= gy1) {
          int gx_i = pane.x + static_cast<int>(gx);
          int gy_i = static_cast<int>(gy0 + app.ui_top);
          int gh = static_cast<int>(gap.height);
          SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 90);
          SDL_Rect wide{gx_i - 4, gy_i, 9, gh};
          SDL_RenderFillRect(app.sdl, &wide);
          SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
          SDL_Rect bar{gx_i - 1, gy_i, 3, gh};
          SDL_RenderFillRect(app.sdl, &bar);
          break;
        }
      }
    }
    // Task checkbox zone
    if (app.hover.task_pane == i && app.hover.task != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.task) continue;
        int x = pane.x + static_cast<int>(box.x);
        int y = static_cast<int>(box.y - pane.scroll_y + app.ui_top);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 40, 140, 80, 55);
        SDL_Rect r{x, y, 26, h};
        SDL_RenderFillRect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 30, 120, 70, 220);
        SDL_RenderDrawRect(app.sdl, &r);
        break;
      }
    }
    // Block drag grip zone (drawn in the left gutter, not over text)
    if (app.hover.grip_pane == i && app.hover.grip != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.grip) continue;
        int gw = static_cast<int>(kBlockGripW);
        int x = pane.x + static_cast<int>(box.x) - gw;
        int y = static_cast<int>(box.y - pane.scroll_y + app.ui_top);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 80, 120, 180, 50);
        SDL_Rect r{x, y, gw, h};
        SDL_RenderFillRect(app.sdl, &r);
        // Stronger grip dots centered in the gutter
        int gx = x + gw / 2 - 2;
        int gy = y + h / 2;
        SDL_SetRenderDrawColor(app.sdl, 40, 90, 160, 255);
        for (int d = -6; d <= 6; d += 6) {
          SDL_Rect dot{gx, gy + d, 4, 4};
          SDL_RenderFillRect(app.sdl, &dot);
        }
        break;
      }
    }
    // Transclude region
    if (app.hover.transclude_pane == i &&
        app.hover.transclude != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.transclude) continue;
        int x = pane.x + static_cast<int>(box.x);
        int y = static_cast<int>(box.y - pane.scroll_y + app.ui_top);
        int w = static_cast<int>(box.width);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 180, 120, 40, 40);
        SDL_Rect r{x, y, w, h};
        SDL_RenderFillRect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 180, 120, 40, 200);
        SDL_RenderDrawRect(app.sdl, &r);
        break;
      }
    }
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  }

  // Logical structure overlay: colour bars + wash by NodeKind + right labels
  if (app.structure_mode) {
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    const auto& ddoc = buf(app, pane.buf).doc;
    auto kind_label = [&ddoc](const xanatoo::Node& n) -> const char* {
      using K = xanatoo::NodeKind;
      switch (n.kind) {
        case K::Heading: {
          static thread_local char buf[4];
          int lv = n.level;
          if (lv < 1) lv = 1;
          if (lv > 6) lv = 6;
          buf[0] = 'h';
          buf[1] = static_cast<char>('0' + lv);
          buf[2] = 0;
          return buf;
        }
        case K::Header: {
          // Show interop depth (org stars / ATX) or section nesting.
          static thread_local char buf[12];
          int lv = n.level;
          if (lv < 1) {
            xanatoo::NodeId pid = n.parent;
            while (pid != xanatoo::INVALID_NODE && pid < ddoc.next_id &&
                   static_cast<size_t>(pid - 1) < ddoc.nodes.size()) {
              const auto& pn = ddoc.get(pid);
              if (pn.kind == K::Section || pn.kind == K::Article) ++lv;
              pid = pn.parent;
            }
          }
          if (lv < 1) lv = 1;
          if (lv > 9) lv = 9;
          buf[0] = 'h';
          buf[1] = 'e';
          buf[2] = 'a';
          buf[3] = 'd';
          buf[4] = 'e';
          buf[5] = 'r';
          buf[6] = static_cast<char>('0' + lv);
          buf[7] = 0;
          return buf;
        }
        case K::Article:
          return "article";
        case K::Section:
          return "section";
        case K::Paragraph:
          return "para";
        case K::List:
          return n.level == 1 ? "ol" : "ul";
        case K::ListItem:
          if (n.label == "task") return "task";
          if (n.label == "task-done") return "done";
          return "li";
        case K::BlockQuote:
          return "quote";
        case K::Note:
          return "note";
        case K::CodeBlock:
          return "code";
        case K::Table:
          return "table";
        case K::TableRow:
          return "row";
        case K::TableCell:
          return n.level > 0 ? "th" : "td";
        case K::DefList:
          return "deflist";
        case K::Term:
          return "term";
        case K::Definition:
          return "def";
        case K::Stretch:
          return "stretch";
        case K::Columns:
          return "columns";
        case K::Column:
          return "column";
        case K::Image:
          return "image";
        case K::Figure:
          return "figure";
        case K::Caption:
          return "caption";
        case K::Biblio:
          return "biblio";
        case K::BiblioEntry:
          return "ref";
        case K::HorizontalRule:
          return "hr";
        case K::Transclude:
          return "include";
        case K::Thread:
          return "thread";
        case K::Message:
          return "message";
        default:
          return nullptr;
      }
    };
    // Prefer mono for compact gutter tags; fall back to body.
    xanatoo::Font& tag_font =
        app.fonts.mono.loaded() ? app.fonts.mono : app.fonts.regular;
    const int tag_lh = tag_font.line_height();
    for (const auto& box : pane.layout.boxes) {
      if (box.node == xanatoo::INVALID_NODE) continue;
      const auto& node = ddoc.get(box.node);
      const auto kind = node.kind;
      // Skip pure container shells that span the whole doc
      if (kind == xanatoo::NodeKind::Document) continue;
      uint8_t r = 120, g = 120, b = 130, a_bar = 200, a_wash = 28;
      switch (kind) {
        case xanatoo::NodeKind::Heading:
        case xanatoo::NodeKind::Header:
          r = 40;
          g = 90;
          b = 180;
          a_wash = 36;
          break;
        case xanatoo::NodeKind::Article:
          r = 60;
          g = 100;
          b = 160;
          a_wash = 30;
          break;
        case xanatoo::NodeKind::Section:
          r = 70;
          g = 110;
          b = 170;
          a_wash = 26;
          break;
        case xanatoo::NodeKind::Paragraph:
          r = 100;
          g = 110;
          b = 120;
          a_wash = 18;
          break;
        case xanatoo::NodeKind::List:
        case xanatoo::NodeKind::ListItem:
          r = 50;
          g = 140;
          b = 90;
          break;
        case xanatoo::NodeKind::BlockQuote:
          r = 140;
          g = 155;
          b = 180;
          break;
        case xanatoo::NodeKind::Note:
          r = 200;
          g = 140;
          b = 40;
          break;
        case xanatoo::NodeKind::CodeBlock:
          r = 160;
          g = 100;
          b = 50;
          a_wash = 40;
          break;
        case xanatoo::NodeKind::Table:
        case xanatoo::NodeKind::TableRow:
        case xanatoo::NodeKind::TableCell:
          r = 80;
          g = 120;
          b = 160;
          break;
        case xanatoo::NodeKind::DefList:
        case xanatoo::NodeKind::Term:
        case xanatoo::NodeKind::Definition:
          r = 120;
          g = 80;
          b = 160;
          break;
        case xanatoo::NodeKind::Stretch:
          r = 180;
          g = 80;
          b = 120;
          break;
        case xanatoo::NodeKind::Columns:
        case xanatoo::NodeKind::Column:
          r = 70;
          g = 130;
          b = 150;
          break;
        case xanatoo::NodeKind::Image:
        case xanatoo::NodeKind::Figure:
        case xanatoo::NodeKind::Caption:
          r = 150;
          g = 70;
          b = 100;
          break;
        case xanatoo::NodeKind::Biblio:
        case xanatoo::NodeKind::BiblioEntry:
          r = 90;
          g = 50;
          b = 140;
          break;
        case xanatoo::NodeKind::HorizontalRule:
          r = 160;
          g = 160;
          b = 160;
          break;
        case xanatoo::NodeKind::Transclude:
          r = 180;
          g = 120;
          b = 40;
          break;
        case xanatoo::NodeKind::Thread:
        case xanatoo::NodeKind::Message:
          r = 40;
          g = 130;
          b = 140;
          a_wash = 32;
          break;
        default:
          break;
      }
      int by = static_cast<int>(box.y - pane.scroll_y + app.ui_top);
      int bh = static_cast<int>(box.height);
      if (by + bh < clip_y || by > app.win_h - status_bar_h) continue;
      // Leave room on the right for the kind tag.
      const int tag_gutter = 56;
      int wash_w = pane.w - 14 - tag_gutter;
      if (wash_w < 20) wash_w = pane.w - 14;
      SDL_SetRenderDrawColor(app.sdl, r, g, b, a_bar);
      SDL_Rect mark{pane.x + 1, by, 5, bh};
      SDL_RenderFillRect(app.sdl, &mark);
      SDL_SetRenderDrawColor(app.sdl, r, g, b, a_wash);
      SDL_Rect wash{pane.x + 6, by, wash_w, bh};
      SDL_RenderFillRect(app.sdl, &wash);

      // Small kind label on the right edge of the region.
      // Skip pure table-row shells and plain paragraphs — the wash is
      // enough for para; tags stay for structural kinds.
      const char* tag = kind_label(node);
      if (tag && kind != xanatoo::NodeKind::TableRow &&
          kind != xanatoo::NodeKind::Paragraph && bh >= 10 &&
          tag_font.loaded()) {
        int tw = tag_font.measure(tag);
        int tx = pane.x + pane.w - tw - 8;
        if (tx < pane.x + 10) tx = pane.x + 10;
        // Baseline near the top of the box so stacked regions stay readable.
        float baseline = static_cast<float>(by) +
                         static_cast<float>(std::min(bh, tag_lh + 2)) - 3.f;
        if (baseline < static_cast<float>(clip_y) + 8.f)
          baseline = static_cast<float>(clip_y) + 8.f;
        // Soft chip behind the tag for contrast on wash.
        SDL_SetRenderDrawColor(app.sdl, 248, 246, 240, 200);
        SDL_Rect chip{tx - 3, by + 1, tw + 6, std::min(bh - 2, tag_lh + 2)};
        if (chip.h > 4) SDL_RenderFillRect(app.sdl, &chip);
        app.renderer.draw_text(tag_font, static_cast<float>(tx), baseline, tag,
                               r, g, b);
      }
    }
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  }

  if (i == app.focus) {
    auto cpos =
        xanatoo::caret_screen_pos(pane.layout, pane.sel.focus, draw_scroll, lh);
    if (cpos.valid) pane.preferred_x = cpos.x;
    // Browse mode: steady caret (reading focus). Edit mode: blink.
    if (app.browse_mode ||
        ((SDL_GetTicks() - app.caret_blink_start) % 1000) < 600) {
      app.renderer.draw_caret(cpos, ox);
    }
  }

  // Focus indicator: thin bar at top of focused pane content
  if (i == app.focus) {
    SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
    SDL_Rect bar{pane.x, clip_y, pane.w, 3};
    SDL_RenderFillRect(app.sdl, &bar);
  }

  // Version-diff marks (orange bars on changed text blocks)
  if (app.diff_mode && !shared_doc(app)) {
    const auto& marks = app.diff_marks[i];
    if (!marks.empty()) {
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      for (const auto& box : pane.layout.boxes) {
        bool hit = false;
        for (xanatoo::NodeId id : marks) {
          if (id == box.node) {
            hit = true;
            break;
          }
        }
        if (!hit) continue;
        // Content → screen: same convention as structure overlay (+ ui_top).
        int by = static_cast<int>(box.y - pane.scroll_y + app.ui_top);
        int bh = static_cast<int>(box.height);
        if (by + bh < clip_y || by > app.win_h - status_bar_h) continue;
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 40, 200);
        SDL_Rect mark{pane.x + 2, by, 4, bh};
        SDL_RenderFillRect(app.sdl, &mark);
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 40, 40);
        SDL_Rect wash{pane.x, by, pane.w - 6, bh};
        SDL_RenderFillRect(app.sdl, &wash);
      }
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
    }
    // Intra-block word-level highlights for modified (similar) blocks
    const auto& words = app.diff_words[i];
    if (!words.empty()) {
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      const auto& ddoc = buf(app, pane.buf).doc;
      for (const auto& g : pane.layout.glyphs) {
        if (g.node == xanatoo::INVALID_NODE) continue;
        // Map glyph to plain-text character index within its node
        size_t gch = 0;
        {
          const auto& gn = ddoc.get(g.node);
          for (size_t si = 0; si < g.span_index && si < gn.spans.size(); ++si) {
            for (size_t ci = 0; ci < gn.spans[si].text.size();) {
              unsigned char c =
                  static_cast<unsigned char>(gn.spans[si].text[ci]);
              if (c < 0x80)
                ++ci;
              else if ((c & 0xe0) == 0xc0)
                ci += 2;
              else if ((c & 0xf0) == 0xe0)
                ci += 3;
              else if ((c & 0xf8) == 0xf0)
                ci += 4;
              else
                ++ci;
              ++gch;
            }
          }
          gch += g.char_index;
        }
        bool hit = false;
        for (const auto& wm : words) {
          if (wm.node == g.node && gch >= wm.char_begin && gch < wm.char_end) {
            hit = true;
            break;
          }
        }
        if (!hit) continue;
        int gx = pane.x + static_cast<int>(g.x);
        int gy =
            static_cast<int>(g.y - pane.scroll_y + app.ui_top - lh * 0.85f);
        // Width: advance to next glyph on the same baseline/node when possible.
        int gw = 8;
        {
          const float baseline = g.y;
          float next_x = g.x + 8.f;
          for (const auto& ng : pane.layout.glyphs) {
            if (ng.node != g.node) continue;
            if (std::abs(ng.y - baseline) > 1.f) continue;
            if (ng.x > g.x + 0.5f && ng.x < next_x) next_x = ng.x;
          }
          int adv = static_cast<int>(next_x - g.x);
          if (adv > 2 && adv < 48) gw = adv;
        }
        if (gy + static_cast<int>(lh) < clip_y || gy > app.win_h - status_bar_h)
          continue;
        SDL_SetRenderDrawColor(app.sdl, 220, 160, 40, 110);
        SDL_Rect r{gx, gy, gw, static_cast<int>(lh)};
        SDL_RenderFillRect(app.sdl, &r);
      }
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
    }
  }

  // Column gutters (subtle vertical handles)
  for (const auto& gap : pane.layout.column_gaps) {
    int gx = pane.x + static_cast<int>(gap.x);
    int gy = static_cast<int>(gap.y - pane.scroll_y + app.ui_top);
    int gh = static_cast<int>(gap.height);
    bool active = app.dragging_col_gap && gap.left == app.col_gap_left;
    SDL_SetRenderDrawColor(app.sdl, active ? 25 : 190, active ? 90 : 188,
                           active ? 170 : 180, 255);
    SDL_Rect gbar{gx - 1, gy, 3, gh};
    SDL_RenderFillRect(app.sdl, &gbar);
  }

  // Block drag-drop: insertion line + source highlight
  if (app.dragging_block && i == app.focus) {
    // Dim the dragged block
    for (const auto& box : pane.layout.boxes) {
      if (box.node != app.drag_block_id) continue;
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 80, 140, 220, 60);
      SDL_Rect r{pane.x + static_cast<int>(box.x),
                 static_cast<int>(box.y - pane.scroll_y + app.ui_top),
                 static_cast<int>(box.width), static_cast<int>(box.height)};
      SDL_RenderFillRect(app.sdl, &r);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
    }
    if (app.drop_line_y >= 0.f) {
      int ly = static_cast<int>(app.drop_line_y - pane.scroll_y + app.ui_top);
      SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
      SDL_Rect line{pane.x + 24, ly - 1, pane.w - 40, 3};
      SDL_RenderFillRect(app.sdl, &line);
    }
  } else if (i == app.focus && !app.dragging_block) {
    // Subtle grip dots in the left gutter (not over the text)
    // mouse_y is window space; boxes are content space.
    float adj_y = content_y(app, app.mouse_y) + pane.scroll_y;
    float lx = app.mouse_x - static_cast<float>(pane.x);
    for (const auto& box : pane.layout.boxes) {
      if (adj_y < box.y || adj_y > box.y + box.height) continue;
      if (lx < box.x - kBlockGripW || lx >= box.x) continue;
      const auto& bn = doc_of(app, pane).get(box.node);
      if (bn.kind == xanatoo::NodeKind::Document ||
          bn.kind == xanatoo::NodeKind::Columns ||
          bn.kind == xanatoo::NodeKind::Column ||
          bn.kind == xanatoo::NodeKind::List)
        continue;
      int gx = pane.x + static_cast<int>(box.x) -
               static_cast<int>(kBlockGripW) / 2 - 1;
      int gy = static_cast<int>(box.y - pane.scroll_y + app.ui_top +
                                box.height * 0.5f);
      SDL_SetRenderDrawColor(app.sdl, 160, 158, 150, 255);
      for (int d = -6; d <= 6; d += 6) {
        SDL_Rect dot{gx, gy + d, 3, 3};
        SDL_RenderFillRect(app.sdl, &dot);
      }
      break;
    }
  }
}

// Classic scrollbar in the strip excluded from the content clip.
static void draw_pane_scrollbar(App& app, int i) {
  [[maybe_unused]] const float lh =
      static_cast<float>(app.fonts.regular.line_height());
  [[maybe_unused]] Pane& pane = app.panes[i];
  const float view_h_pane =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  const float max_scroll =
      std::max(0.f, pane.layout.content_height - view_h_pane + 40.f);
  // Classic scrollbar: track, ↑/↓, chapter/search markers, thumb.
  {
    float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                   app.ui_bottom;
    if (view_h < 1.f) view_h = 1.f;
    float content_h = std::max(view_h, pane.layout.content_height);
    const float arrow = static_cast<float>(kSbArrow);
    float track_h = std::max(1.f, view_h - 2.f * arrow);
    // Thumb sized against full view; position on track between arrows.
    float thumb_h = std::max(20.f, track_h * (view_h / content_h));
    if (thumb_h > track_h) thumb_h = track_h;
    float track_travel = std::max(0.f, track_h - thumb_h);
    float ratio = (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
    float thumb_y = app.ui_top + arrow + ratio * track_travel;
    int sx = pane.x + pane.w - kSbW;
    int top = static_cast<int>(app.ui_top);
    int vh = static_cast<int>(view_h);

    // Track (sunken trough)
    namespace th = xanatoo::theme;
    const th::Palette& pal = th::palette();
    SDL_Rect track_r{sx, top, kSbW, vh};
    th::fill(app.sdl, track_r, pal.face_dark);
    th::bevel(app.sdl, track_r, th::Bevel::Sunken);

    // Metadata marks on the track (doc Y → track fraction).
    auto mark_at = [&](float doc_y, int r, int g, int b) {
      if (content_h < 1.f) return;
      float frac = std::max(0.f, std::min(1.f, doc_y / content_h));
      int my = top + static_cast<int>(arrow + frac * track_h);
      if (my < top + static_cast<int>(arrow) ||
          my > top + vh - static_cast<int>(arrow))
        return;
      SDL_SetRenderDrawColor(app.sdl, r, g, b, 255);
      SDL_Rect tick{sx + 1, my - 1, kSbW - 2, 2};
      SDL_RenderFillRect(app.sdl, &tick);
    };
    // Chapter / section breaks. Virtualization often stubs a whole Section
    // (no Header child boxes) — prefer Section id, and synthesise Y from
    // height_cache when a node is outside the materialised band.
    if (app.outline.empty()) {
      xanatoo::collect_outline(doc_of(app, pane), app.outline,
                               &app.folded_sections);
    }
    if (!app.outline.empty()) {
      std::unordered_map<xanatoo::NodeId, float> mark_y;
      mark_y.reserve(pane.layout.boxes.size() + 16);
      for (const auto& box : pane.layout.boxes) {
        if (box.node != xanatoo::INVALID_NODE) mark_y[box.node] = box.y;
      }
      // Fill missing Section/Article positions by walking with height hints.
      const auto& dmark = doc_of(app, pane);
      std::function<float(xanatoo::NodeId, float)> walk_y =
          [&](xanatoo::NodeId id, float y) -> float {
        if (id == xanatoo::INVALID_NODE || id >= dmark.next_id) return y;
        const auto& n = dmark.get(id);
        if (!mark_y.count(id)) mark_y[id] = y;
        if (n.kind == xanatoo::NodeKind::Document) {
          for (xanatoo::NodeId c : n.children) y = walk_y(c, y);
          return y;
        }
        if (n.kind == xanatoo::NodeKind::Section ||
            n.kind == xanatoo::NodeKind::Article) {
          // If layout already has a stub/full box height, advance by it
          // when children were not materialised (no child entries in mark_y
          // beyond what we just set for id).
          auto hit = pane.height_cache.find(id);
          bool child_known = false;
          for (xanatoo::NodeId c : n.children) {
            if (mark_y.count(c)) {
              child_known = true;
              break;
            }
          }
          if (hit != pane.height_cache.end() && !child_known) {
            // Parent was virtualized as one stub — still place nested
            // outline targets using child height hints so marks remain.
            float cy = y;
            for (xanatoo::NodeId c : n.children) {
              if (!mark_y.count(c)) mark_y[c] = cy;
              auto ch = pane.height_cache.find(c);
              cy += ch != pane.height_cache.end()
                        ? ch->second
                        : static_cast<float>(app.fonts.regular.line_height());
            }
            return y + hit->second;
          }
          float cy = y;
          for (xanatoo::NodeId c : n.children) cy = walk_y(c, cy);
          if (hit != pane.height_cache.end() && cy <= y + 0.5f)
            return y + hit->second;
          return cy > y ? cy
                        : y + (hit != pane.height_cache.end() ? hit->second
                                                              : 24.f);
        }
        auto hit = pane.height_cache.find(id);
        if (hit != pane.height_cache.end()) return y + hit->second;
        return y + static_cast<float>(app.fonts.regular.line_height());
      };
      if (dmark.root != xanatoo::INVALID_NODE) {
        // Prefer content_height-consistent origin: layout boxes already
        // used absolute y; walk only fills gaps (does not overwrite).
        float y0 = app.view_settings.margin_y;
        // If root box exists, start from it.
        auto root_it = mark_y.find(dmark.root);
        if (root_it != mark_y.end()) y0 = root_it->second;
        walk_y(dmark.root, y0);
      }
      for (const auto& e : app.outline) {
        float y = -1.f;
        // Prefer Section (stable under virtualization stubs), then Header.
        for (xanatoo::NodeId id : {e.section, e.node}) {
          if (id == xanatoo::INVALID_NODE) continue;
          auto it = mark_y.find(id);
          if (it != mark_y.end()) {
            y = it->second;
            break;
          }
        }
        if (y < 0.f) continue;
        if (e.level <= 1)
          mark_at(y, 25, 90, 170);
        else
          mark_at(y, 140, 160, 190);
      }
    }
    // Search hits (cheap capped scan while leap search is active).
    if (prompt_is_search(app) && !app.prompt.text.empty() &&
        &pane == &app.panes[app.focus]) {
      xanatoo::Caret from{};
      size_t mlen = 0;
      for (int n = 0; n < 48; ++n) {
        auto hit =
            xanatoo::find_next(doc_of(app, pane), app.prompt.text, from, mlen);
        if (!hit) break;
        auto cpos = xanatoo::caret_screen_pos(
            pane.layout, *hit, 0.f,
            static_cast<float>(app.fonts.regular.line_height()));
        if (cpos.valid) mark_at(cpos.y, 200, 120, 40);
        from = *hit;
        from.char_index += (mlen > 0 ? mlen : 1);
      }
    }
    // Caret position on the focused pane.
    if (&pane == &app.panes[app.focus]) {
      auto cpos = xanatoo::caret_screen_pos(
          pane.layout, pane.sel.focus, 0.f,
          static_cast<float>(app.fonts.regular.line_height()));
      if (cpos.valid) mark_at(cpos.y, 40, 140, 70);
    }

    // ↑ / ↓ buttons (hover highlight when sb_hover matches)
    auto draw_arrow_btn = [&](int by, bool up) {
      const bool hot =
          (&pane == &app.panes[app.focus]) &&
          ((up && app.sb_hover == 1) || (!up && app.sb_hover == 2));
      SDL_Rect btn{sx, by, kSbW, kSbArrow};
      th::draw_button_face(app.sdl, btn, false, hot);
      int cx = sx + kSbW / 2;
      int cy = by + kSbArrow / 2;
      SDL_SetRenderDrawColor(app.sdl, 60, 58, 50, 255);
      if (up) {
        SDL_RenderDrawLine(app.sdl, cx, cy - 3, cx - 4, cy + 2);
        SDL_RenderDrawLine(app.sdl, cx, cy - 3, cx + 4, cy + 2);
        SDL_RenderDrawLine(app.sdl, cx - 4, cy + 2, cx + 4, cy + 2);
      } else {
        SDL_RenderDrawLine(app.sdl, cx, cy + 3, cx - 4, cy - 2);
        SDL_RenderDrawLine(app.sdl, cx, cy + 3, cx + 4, cy - 2);
        SDL_RenderDrawLine(app.sdl, cx - 4, cy - 2, cx + 4, cy - 2);
      }
    };
    draw_arrow_btn(top, true);
    draw_arrow_btn(top + vh - kSbArrow, false);

    // Thumb
    SDL_Rect thumb{sx + 1, static_cast<int>(thumb_y), kSbW - 2,
                   std::max(8, static_cast<int>(thumb_h))};
    th::draw_button_face(app.sdl, thumb,
                         app.dragging_scroll && app.scroll_drag_pane == i,
                         false);
    // Grip ridges (BeOS-style) when the thumb is tall enough.
    if (thumb.h >= 24) {
      const int gy = thumb.y + thumb.h / 2;
      for (int k = -1; k <= 1; ++k) {
        th::hline(app.sdl, thumb.x + 3, thumb.x + thumb.w - 4, gy + k * 3,
                  pal.shadow);
        th::hline(app.sdl, thumb.x + 3, thumb.x + thumb.w - 4, gy + k * 3 + 1,
                  pal.highlight);
      }
    }
  }
}

// Pane paper colour (also the window clear colour behind the panes).
constexpr uint8_t kPaperR = 248, kPaperG = 246, kPaperB = 240;

// Document layer of pane i — paper, selection, text — into the current render
// target, clipped to `area`. Content y maps to target y - `scroll`.
static void draw_pane_layer(App& app, int i, const SDL_Rect& area, float ox,
                            float scroll) {
  const float lh = static_cast<float>(app.fonts.regular.line_height());
  Pane& pane = app.panes[i];
  SDL_RenderSetClipRect(app.sdl, &area);
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(app.sdl, kPaperR, kPaperG, kPaperB, 255);
  SDL_RenderFillRect(app.sdl, &area);
  if (!pane.sel.empty()) {
    auto rects = xanatoo::selection_rects(pane.layout, pane.sel, scroll, lh);
    // Leap-search match uses amber so it is distinct from a user selection.
    if (prompt_is_search(app) && i == app.focus)
      app.renderer.draw_selection(rects, ox, 230, 190, 70, 150);
    else
      app.renderer.draw_selection(rects, ox);
  }
  app.renderer.draw(pane.layout, scroll, ox);
}

static void release_raster(PaneRaster& r) {
  for (SDL_Texture*& t : r.tex) {
    if (t) SDL_DestroyTexture(t);
    t = nullptr;
  }
  r.w = r.h = 0;
  r.valid = false;
}

void release_pane_rasters(App& app) {
  for (Pane& p : app.panes) release_raster(p.raster);
}

static bool ensure_raster_textures(App& app, PaneRaster& r, int w, int h) {
  if (r.tex[0] && r.tex[1] && r.w == w && r.h == h) return true;
  release_raster(r);
  for (SDL_Texture*& t : r.tex) {
    t = SDL_CreateTexture(app.sdl, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) {
      release_raster(r);
      return false;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
  }
  r.w = w;
  r.h = h;
  return true;
}

// Bring pane i's raster up to date for texture-space `scroll` (content y −
// scroll = texture y). Unchanged content scrolled by whole pixels is shifted
// into the other texture and only the exposed strip is drawn; anything else
// redraws the whole texture. False → render targets unavailable.
static bool update_pane_raster(App& app, int i, int w, int h, float scroll) {
  if (!SDL_RenderTargetSupported(app.sdl)) return false;
  Pane& pane = app.panes[i];
  PaneRaster& r = pane.raster;
  if (!ensure_raster_textures(app, r, w, h)) return false;

  const xanatoo::Selection sel = pane.sel.empty() ? xanatoo::Selection{}
                                                  : pane.sel;
  const bool tint = !sel.empty() && prompt_is_search(app) && i == app.focus;
  const bool same_content = r.valid &&
                            r.layout_gen == pane.layout.generation &&
                            r.sel.anchor == sel.anchor &&
                            r.sel.focus == sel.focus && r.search_tint == tint;
  if (same_content && r.scroll == scroll) return true;

  SDL_Texture* prev_target = SDL_GetRenderTarget(app.sdl);
  const float d = scroll - r.scroll;
  const int dy = static_cast<int>(d);
  if (same_content && d == static_cast<float>(dy) && std::abs(dy) < r.h) {
    // dy > 0: content moved up, keep the lower part and expose the bottom.
    SDL_Texture* src = r.tex[r.cur];
    r.cur ^= 1;
    SDL_SetRenderTarget(app.sdl, r.tex[r.cur]);
    SDL_RenderSetClipRect(app.sdl, nullptr);
    const int keep = r.h - std::abs(dy);
    const SDL_Rect from{0, std::max(dy, 0), r.w, keep};
    const SDL_Rect to{0, std::max(-dy, 0), r.w, keep};
    SDL_RenderCopy(app.sdl, src, &from, &to);
    const SDL_Rect strip = dy > 0 ? SDL_Rect{0, keep, r.w, dy}
                                  : SDL_Rect{0, 0, r.w, -dy};
    draw_pane_layer(app, i, strip, 0.f, scroll);
  } else {
    SDL_SetRenderTarget(app.sdl, r.tex[r.cur]);
    draw_pane_layer(app, i, SDL_Rect{0, 0, r.w, r.h}, 0.f, scroll);
  }
  SDL_RenderSetClipRect(app.sdl, nullptr);
  SDL_SetRenderTarget(app.sdl, prev_target);

  r.valid = true;
  r.layout_gen = pane.layout.generation;
  r.scroll = scroll;
  r.sel = sel;
  r.search_tint = tint;
  return true;
}

// Text, selection and the three layers above, then the scrollbar.
void draw_pane(App& app, int i) {
  Pane& pane = app.panes[i];
  float view_h_pane =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  float max_scroll =
      std::max(0.f, pane.layout.content_height - view_h_pane + 40.f);
  if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

  const int clip_y = static_cast<int>(app.ui_top);
  SDL_Rect clip{
      pane.x, clip_y, std::max(1, pane.w - kSbW),
      std::max(1, app.win_h - status_bar_h - clip_y -
                      static_cast<int>(app.ui_bottom))};

  // Draw at a whole-pixel scroll so the cached raster can be blitted; the
  // fractional part (touch / fling / scrollbar drag) stays in pane.scroll_y.
  const float exact_scroll = pane.scroll_y;
  pane.scroll_y = std::round(exact_scroll);

  // screen_y = content_y - scroll_y + ui_top = content_y - (scroll_y - ui_top)
  const float draw_scroll = pane.scroll_y - app.ui_top;
  // Same mapping relative to the clip top (texture space).
  const float tex_scroll = draw_scroll + static_cast<float>(clip_y);
  if (update_pane_raster(app, i, clip.w, clip.h, tex_scroll)) {
    SDL_RenderSetClipRect(app.sdl, nullptr);
    SDL_RenderCopy(app.sdl, pane.raster.tex[pane.raster.cur], nullptr, &clip);
    SDL_RenderSetClipRect(app.sdl, &clip);
  } else {
    draw_pane_layer(app, i, clip, static_cast<float>(pane.x), draw_scroll);
  }

  draw_pane_overlays(app, i);

  // Scrollbar lives in the strip excluded from the content clip.
  SDL_RenderSetClipRect(app.sdl, nullptr);

  draw_pane_scrollbar(app, i);
  pane.scroll_y = exact_scroll;
}

void draw_panes(App& app) {
  app.renderer.clear(kPaperR, kPaperG, kPaperB);
  int pane_count = app.split ? 2 : 1;
  for (int i = 0; i < pane_count; ++i) draw_pane(app, i);
}

void draw_split_chrome(App& app) {
  SDL_RenderSetClipRect(app.sdl, nullptr);
  if (app.split) {
    int mid = app.panes[1].x - 3;
    SDL_Rect div{mid, static_cast<int>(app.ui_top), 6,
                 app.win_h - status_bar_h - static_cast<int>(app.ui_top) -
                     static_cast<int>(app.ui_bottom)};
    xanatoo::theme::fill(app.sdl, div, xanatoo::theme::palette().face);
    xanatoo::theme::vline(app.sdl, div.x, div.y, div.y + div.h - 1,
                          xanatoo::theme::palette().highlight);
    xanatoo::theme::vline(app.sdl, div.x + div.w - 1, div.y, div.y + div.h - 1,
                          xanatoo::theme::palette().shadow);
  }
  if (app.split && shared_doc(app) &&
      app.panes[0].sel.focus.node != xanatoo::INVALID_NODE &&
      app.panes[0].sel.focus.node == app.panes[1].sel.focus.node) {
    float lh = static_cast<float>(app.fonts.regular.line_height());
    // draw_scroll matches pane rendering (content → screen under the toolbar).
    const float ds0 = app.panes[0].scroll_y - app.ui_top;
    const float ds1 = app.panes[1].scroll_y - app.ui_top;
    auto c0 = xanatoo::caret_screen_pos(app.panes[0].layout,
                                        app.panes[0].sel.focus, ds0, lh);
    auto c1 = xanatoo::caret_screen_pos(app.panes[1].layout,
                                        app.panes[1].sel.focus, ds1, lh);
    if (c0.valid && c1.valid) {
      int x0 = app.panes[0].x + static_cast<int>(c0.x);
      int y0 = static_cast<int>(c0.y);
      int x1 = app.panes[1].x + static_cast<int>(c1.x);
      int y1 = static_cast<int>(c1.y);
      // Clamp into content area below toolbar / above status bar
      int min_y = static_cast<int>(app.ui_top);
      int max_y = app.win_h - status_bar_h;
      if (y0 >= min_y && y0 < max_y && y1 >= min_y && y1 < max_y) {
        SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 160);
        // Straight connector + soft endpoints
        SDL_RenderDrawLine(app.sdl, x0, y0, x1, y1);
        SDL_RenderDrawLine(app.sdl, x0, y0 + 1, x1, y1 + 1);
        auto dot = [&](int x, int y) {
          SDL_Rect r{x - 3, y - 3, 7, 7};
          SDL_RenderFillRect(app.sdl, &r);
        };
        dot(x0, y0);
        dot(x1, y1);
        SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
      }
    }
  }
}

void draw_outline_panel(App& app) {
  if (app.outline_mode) {
    if (app.outline_docked) {
      app.ol.x = 0;
      app.ol.y = static_cast<int>(app.ui_top);
      app.ol.w = std::max(160, std::min(app.outline_dock_w, app.win_w / 2));
      app.ol.h = app.win_h - status_bar_h - app.ol.y;
      app.outline_dock_w = app.ol.w;
    } else {
      if (app.ol.w < 200) app.ol.w = 200;
      if (app.ol.h < 140) app.ol.h = 140;
    }
    const float step = outline_row_step(app);
    auto [cx, cy, cw, ch] = outline_client_rect(app);
    {
      float content_h = static_cast<float>(app.outline.size()) * step + 8.f;
      float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      if (app.ol.scroll > max_s) app.ol.scroll = max_s;
    }

    std::string ol_title = app.outline_docked ? "Outline · body →" : "Outline";
    if (app.section_focus_mode) {
      ol_title = "Outline · Focus";
      if (!app.outline.empty() && app.outline_sel >= 0 &&
          app.outline_sel < static_cast<int>(app.outline.size())) {
        const std::string& tx =
            app.outline[static_cast<size_t>(app.outline_sel)].text;
        if (!tx.empty()) {
          ol_title += ": ";
          ol_title += tx.size() > 18 ? tx.substr(0, 17) + "…" : tx;
        }
      }
    }
    draw_floating_frame(app, SDL_Rect{app.ol.x, app.ol.y, app.ol.w, app.ol.h},
                        ol_title, true, !app.outline_docked);
    // Focus toggle (section isolation) — a small push button left of close.
    {
      namespace th = xanatoo::theme;
      const char* flab = app.section_focus_mode ? "Focus ●" : "Focus";
      const int fw = app.fonts.regular.measure(flab);
      SDL_Rect fb{app.ol.x + app.ol.w - 28 - fw - 18, app.ol.y + 5, fw + 12,
                  help_title_h - 10};
      th::draw_button_face(app.sdl, fb, app.section_focus_mode, false);
      const th::Palette& pal = th::palette();
      app.renderer.draw_text(
          app.fonts.regular, static_cast<float>(fb.x + 6),
          static_cast<float>(fb.y +
                             (fb.h - app.fonts.regular.line_height()) / 2 +
                             app.fonts.regular.ascender()),
          flab, pal.text.r, pal.text.g, pal.text.b);
    }

    SDL_Rect clip{cx, cy, cw, ch};
    SDL_RenderSetClipRect(app.sdl, &clip);
    float ty =
        static_cast<float>(cy) + app.fonts.regular.ascender() - app.ol.scroll;
    for (int i = 0; i < static_cast<int>(app.outline.size()); ++i) {
      if (ty > static_cast<float>(cy + ch) + step) break;
      if (ty + step < static_cast<float>(cy)) {
        ty += step;
        continue;
      }
      const auto& e = app.outline[static_cast<size_t>(i)];
      std::string line;
      if (i < 9) {
        line += static_cast<char>('1' + i);
        line += " ";
      } else {
        line += "  ";
      }
      for (int ind = 1; ind < e.level; ++ind) line += "  ";
      if (e.has_children)
        line += e.folded ? "▸ " : "▾ ";
      else
        line += "  ";
      line += e.text;
      if (e.folded && e.has_children) line += " …";
      if (i == app.outline_sel) {
        const auto& pal = xanatoo::theme::palette();
        SDL_Rect hi{cx + 2,
                    static_cast<int>(ty - app.fonts.regular.ascender() - 2),
                    cw - 4, static_cast<int>(step)};
        xanatoo::theme::fill(app.sdl, hi, pal.accent);
        app.renderer.draw_text(app.fonts.regular, static_cast<float>(cx + 8),
                               ty, line, 255, 255, 255);
      } else {
        app.renderer.draw_text(app.fonts.regular, static_cast<float>(cx + 8),
                               ty, line, 50, 48, 42);
      }
      ty += step;
    }
    if (app.outline.empty()) {
      app.renderer.draw_text(
          app.fonts.regular, static_cast<float>(cx + 8),
          static_cast<float>(cy) + app.fonts.regular.ascender(),
          "(no headings in this document)", 120, 118, 110);
    }
    SDL_RenderSetClipRect(app.sdl, nullptr);

    {
      const float content_h =
          static_cast<float>(app.outline.size()) * step + 8.f;
      const float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      draw_client_scrollbar(app, cx, cy, cw, ch, app.ol.scroll, max_s);
    }
  }
}

void draw_backlinks_panel(App& app) {
  if (app.backlinks_mode) {
    if (app.bl.w < 200) app.bl.w = 200;
    if (app.bl.h < 120) app.bl.h = 120;
    const float step = 22.f;
    const int title_h = help_title_h;
    const int pad = 8;
    int cx = app.bl.x + pad, cy = app.bl.y + title_h + 4;
    int cw = app.bl.w - pad * 2, ch = app.bl.h - title_h - pad - 4;
    {
      float content_h = static_cast<float>(app.backlinks.size()) * step + 8.f;
      float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      if (app.bl.scroll > max_s) app.bl.scroll = max_s;
    }
    draw_floating_frame(app, SDL_Rect{app.bl.x, app.bl.y, app.bl.w, app.bl.h},
                        app.backlinks_label.empty()
                            ? "Backlinks (no label under caret)"
                            : ("Backlinks → #" + app.backlinks_label),
                        true);
    SDL_Rect clip{cx, cy, cw, ch};
    SDL_RenderSetClipRect(app.sdl, &clip);
    float ty =
        static_cast<float>(cy) + app.fonts.regular.ascender() - app.bl.scroll;
    if (app.backlinks.empty()) {
      app.renderer.draw_text(app.fonts.regular, static_cast<float>(cx + 8), ty,
                             "(none in this document)", 120, 118, 110);
    }
    for (int i = 0; i < static_cast<int>(app.backlinks.size()); ++i) {
      if (ty > static_cast<float>(cy + ch) + step) break;
      if (ty + step >= static_cast<float>(cy)) {
        const auto& e = app.backlinks[static_cast<size_t>(i)];
        std::string line = e.kind;
        if (!e.rel.empty()) line += ":" + e.rel;
        line += "  ";
        line += e.preview;
        const bool sel = i == app.backlinks_sel;
        if (sel) {
          SDL_Rect hi{cx + 2,
                      static_cast<int>(ty - app.fonts.regular.ascender() - 2),
                      cw - 4, static_cast<int>(step)};
          xanatoo::theme::fill(app.sdl, hi, xanatoo::theme::palette().accent);
        }
        app.renderer.draw_text(app.fonts.regular, static_cast<float>(cx + 8),
                               ty, line, sel ? 255 : 40, sel ? 255 : 40,
                               sel ? 255 : 45);
      }
      ty += step;
    }
    SDL_RenderSetClipRect(app.sdl, nullptr);
    {
      const float content_h =
          static_cast<float>(app.backlinks.size()) * step + 8.f;
      const float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      draw_client_scrollbar(app, cx, cy, cw, ch, app.bl.scroll, max_s);
    }
  }
}

void draw_help_window(App& app) {
  if (app.help_mode) {
    ensure_help_doc(app);
    if (app.help.w < 280) app.help.w = 280;
    if (app.help.h < 200) app.help.h = 200;
    if (app.help_needs_layout) layout_help(app);

    // Clamp scroll
    {
      auto [cx, cy, cw, ch] = help_client_rect(app);
      float max_s = std::max(
          0.f, app.help_layout.content_height - static_cast<float>(ch) + 40.f);
      if (app.help.scroll > max_s) app.help.scroll = max_s;
    }

    draw_floating_frame(
        app, SDL_Rect{app.help.x, app.help.y, app.help.w, app.help.h},
        "Help — xanatoo", true);

    auto [cx, cy, cw, ch] = help_client_rect(app);
    SDL_Rect clip{cx, cy, cw, ch};
    SDL_RenderSetClipRect(app.sdl, &clip);
    // screen_y = content_y - scroll + client_y  ⇒  draw_scroll = scroll -
    // client_y (mirrors pane path: draw_scroll = scroll_y - ui_top)
    const float help_draw_scroll = app.help.scroll - static_cast<float>(cy);
    app.renderer.draw(app.help_layout, help_draw_scroll,
                      static_cast<float>(cx));
    SDL_RenderSetClipRect(app.sdl, nullptr);

    {
      const float max_s = std::max(
          0.f, app.help_layout.content_height - static_cast<float>(ch) + 40.f);
      draw_client_scrollbar(app, cx, cy, cw, ch, app.help.scroll, max_s);
    }
  }
}

void draw_dock_preview(App& app) {
  if (app.dock_preview != SoftDock::None) {
    auto [dx, dy, dw, dh] = dock_rect(app, app.dock_preview);
    if (dw > 0 && dh > 0) {
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 70);
      SDL_Rect zone{dx, dy, dw, dh};
      SDL_RenderFillRect(app.sdl, &zone);
      SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 180);
      SDL_RenderDrawRect(app.sdl, &zone);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
      const char* label = "";
      switch (app.dock_preview) {
        case SoftDock::Left:
          label = "Tile left";
          break;
        case SoftDock::Right:
          label = "Tile right";
          break;
        case SoftDock::Top:
          label = "Tile top";
          break;
        case SoftDock::Bottom:
          label = "Tile bottom";
          break;
        case SoftDock::Max:
          label = "Maximize";
          break;
        default:
          break;
      }
      if (label[0]) {
        int tw = app.fonts.regular.measure(label);
        float lx = static_cast<float>(dx + (dw - tw) / 2);
        float ly = static_cast<float>(dy + dh / 2);
        app.renderer.draw_text(app.fonts.regular, lx, ly, label, 30, 60, 120);
      }
    }
  }
}

void draw_hover_tooltip(App& app) {
  {
    const bool prompt = prompt_active(app) || app.help_mode;
    std::string tip;
    if (!prompt) {
      if (app.hover.stretch != xanatoo::INVALID_NODE) {
        tip = "Click to expand / collapse";
      } else if (!app.hover.href.empty()) {
        tip = app.hover.href;
        if (!app.hover.preview.empty() && app.hover.href[0] == '#') {
          tip += "  —  ";
          tip += app.hover.preview;
        }
        if (!app.hover.rel.empty()) {
          tip += "  ·  rel=";
          tip += app.hover.rel;
        }
      } else if (app.hover.image_handle) {
        tip = "Drag to resize image";
      } else if (app.hover.col_gap) {
        tip = "Drag to resize columns";
      } else if (app.hover.task != xanatoo::INVALID_NODE) {
        tip = "Click to toggle task";
      } else if (app.hover.grip != xanatoo::INVALID_NODE) {
        tip = "Drag to move block";
      } else if (app.hover.transclude != xanatoo::INVALID_NODE) {
        tip = app.hover.transclude_src.empty()
                  ? "Include"
                  : ("Include: " + app.hover.transclude_src);
      }
    }
    if (!tip.empty()) {
      if (tip.size() > 96) tip = tip.substr(0, 93) + "…";
      int tw = app.fonts.regular.measure(tip);
      const int pad_x = 10, pad_y = 6;
      int box_w = tw + pad_x * 2;
      int box_h = app.fonts.regular.line_height() + pad_y * 2;
      int bx = static_cast<int>(app.mouse_x) + 14;
      int by = static_cast<int>(app.mouse_y) - box_h - 10;
      if (bx + box_w > app.win_w - 4) bx = app.win_w - box_w - 4;
      if (bx < 4) bx = 4;
      if (by < static_cast<int>(app.ui_top) + 4)
        by = static_cast<int>(app.mouse_y) + 18;
      if (by + box_h > app.win_h - 32) by = app.win_h - 32 - box_h;

      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 28, 36, 50, 235);
      SDL_Rect box{bx, by, box_w, box_h};
      SDL_RenderFillRect(app.sdl, &box);
      // Accent by kind
      if (app.hover.stretch != xanatoo::INVALID_NODE)
        SDL_SetRenderDrawColor(app.sdl, 40, 140, 90, 255);
      else if (app.hover.col_gap || app.hover.image_handle)
        SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 255);
      else
        SDL_SetRenderDrawColor(app.sdl, 90, 150, 220, 255);
      SDL_RenderDrawRect(app.sdl, &box);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
      float tip_baseline =
          static_cast<float>(by + pad_y + app.fonts.regular.ascender());
      app.renderer.draw_text(app.fonts.regular, static_cast<float>(bx + pad_x),
                             tip_baseline, tip, 235, 240, 250);
    }
  }
}

void present_frame(App& app) {
  if (app.hover.col_gap)
    set_cursor(app, app.cursor_sizewe);
  else if (app.hover.image_handle)
    set_cursor(app, app.cursor_sizenwse);
  else if (app.hover.grip != xanatoo::INVALID_NODE)
    set_cursor(app, app.cursor_sizeall);
  else if (!app.hover.href.empty() ||
           app.hover.stretch != xanatoo::INVALID_NODE ||
           app.hover.image != xanatoo::INVALID_NODE ||
           app.hover.task != xanatoo::INVALID_NODE ||
           app.hover.transclude != xanatoo::INVALID_NODE)
    set_cursor(app, app.cursor_hand);
  else
    set_cursor(app, app.cursor_arrow);
  app.ui.end_frame(app.sdl);
  if (app.stick_cursor_on) {
    draw_stick_cursor(app, app.sdl, static_cast<int>(app.stick_cx),
                      static_cast<int>(app.stick_cy),
                      app.stick_lmb || app.stick_rmb);
  }
  app.renderer.present();
}

void run_frame(App& app) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) handle_event(app, ev);
  update_touch(app);
  sync_window_size(app);
  relayout_if_needed(app);
  run_chrome(app);
  // UI commands (Open, Diff demo, New tab, …) often replace documents after
  // the first layout pass. Relayout before draw so glyphs/carets never
  // reference node ids from the previous document tree.
  relayout_if_needed(app);
  poll_gamepad_axes(app);
  draw_panes(app);
  draw_split_chrome(app);
  draw_status_bar(app);
  draw_floats(app);
  draw_dock_preview(app);
  draw_hover_tooltip(app);
  present_frame(app);
}

}  // namespace xanatoo::app
