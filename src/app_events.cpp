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
#include "xanatoo/util.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

static void on_quit(App& app, SDL_Event& ev, bool imgui_mouse,
                    bool imgui_keys) {
  app.running = false;
  return;
}

static void on_windowevent(App& app, SDL_Event& ev, bool imgui_mouse,
                           bool imgui_keys) {
  if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
      ev.window.event == SDL_WINDOWEVENT_RESIZED) {
    const int nw = ev.window.data1;
    const int nh = ev.window.data2;
    if (nw >= 64 && nh >= 64) {
      app.win_w = nw;
      app.win_h = nh;
      // Keep tiled/maximized soft windows filling the new client area.
      for (auto& sw : app.soft_wins) {
        if (sw.dock != SoftDock::None || sw.maximized)
          apply_soft_dock(app, sw, sw.maximized ? SoftDock::Max : sw.dock);
      }
      app.needs_layout = true;
    }
  }
  return;
}

static void on_dropfile(App& app, SDL_Event& ev, bool imgui_mouse,
                        bool imgui_keys) {
  // Drag-and-drop: documents open in a soft window; images insert at caret.
  char* dropped = ev.drop.file;
  if (!dropped) return;
  std::string fpath(dropped);
  SDL_free(dropped);

  auto lower_ext = [](std::string p) {
    for (char& c : p)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return p;
  };
  const std::string low = lower_ext(fpath);
  const bool is_doc =
      low.size() >= 3 &&
      (xanatoo::ends_with(low, ".md") || xanatoo::ends_with(low, ".markdown") ||
       xanatoo::ends_with(low, ".sexp") || xanatoo::ends_with(low, ".txt") ||
       xanatoo::ends_with(low, ".org") || xanatoo::ends_with(low, ".gmi") ||
       xanatoo::ends_with(low, ".gemini") || xanatoo::ends_with(low, ".epub"));

  if (is_doc) {
    if (!std::filesystem::exists(fpath)) {
      std::cerr << "Drop: file not found: " << fpath << "\n";
      return;
    }
    std::string title;
    try {
      title = std::filesystem::path(fpath).filename().string();
    } catch (...) {
      title = fpath;
    }
    open_browse(app, fpath, title);
    std::cout << "Opened dropped document: " << fpath << "\n";
    return;
  }

  if (!xanatoo::is_raster_image_path(fpath)) {
    std::cerr << "Drop ignored (not an image or document): " << fpath << "\n";
    return;
  }

  int mx = 0, my_raw = 0;
  SDL_GetMouseState(&mx, &my_raw);
  if (static_cast<float>(my_raw) < app.ui_top) return;
  if (my_raw >= app.win_h - status_bar_h) return;

  app.focus = pane_at(app, static_cast<float>(mx));
  Pane& p = focused_pane(app);
  auto& d = doc_of(app, p);
  float lx = local_x(app, static_cast<float>(mx), app.focus);
  float my = content_y(app, static_cast<float>(my_raw));

  // Place caret at drop location when possible; otherwise keep current.
  if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
    p.sel.focus = *hit;
    p.sel.anchor = p.sel.focus;
  }
  xanatoo::ensure_valid_caret(d, p.sel.focus);
  if (p.sel.focus.node == xanatoo::INVALID_NODE) {
    std::cerr << "Drop: no place to insert image\n";
    return;
  }

  // Prefer a path relative to the document directory for portable markup.
  std::string store = fpath;
  std::string base = parent_dir(path_of(app, p));
  if (!base.empty()) {
    try {
      std::filesystem::path rel = std::filesystem::relative(fpath, base);
      if (!rel.empty() && rel.native().find("..") == std::string::npos)
        store = rel.generic_string();
    } catch (...) {
    }
  }
  std::string alt;
  try {
    alt = std::filesystem::path(fpath).stem().string();
  } catch (...) {
    alt = store;
  }

  edit(app);
  if (xanatoo::insert_image(d, p.sel.focus, store, alt)) {
    p.sel.anchor = p.sel.focus;
    app.needs_layout = true;
    reset_blink(app);
    std::cout << "Dropped image: " << store << "\n";
  } else {
    std::cerr << "Drop: could not insert image at caret\n";
  }
  return;
}

static void on_mousewheel(App& app, SDL_Event& ev, bool imgui_mouse,
                          bool imgui_keys) {
  int mxw = 0, myw = 0;
  SDL_GetMouseState(&mxw, &myw);
  // Soft windows first (match paint z-order: outline → browse → help).
  if (app.outline_mode && mxw >= app.ol.x && mxw < app.ol.x + app.ol.w &&
      myw >= app.ol.y && myw < app.ol.y + app.ol.h) {
    app.ol.scroll -= static_cast<float>(ev.wheel.y) * 40.f;
    if (app.ol.scroll < 0.f) app.ol.scroll = 0.f;
    return;
  }
  {
    bool handled = false;
    for (int i = static_cast<int>(app.soft_wins.size()) - 1; i >= 0; --i) {
      SoftWindow& sw = app.soft_wins[static_cast<size_t>(i)];
      if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
          myw < sw.y + sw.h) {
        sw.scroll -= static_cast<float>(ev.wheel.y) * 40.f;
        if (sw.scroll < 0.f) sw.scroll = 0.f;
        handled = true;
        break;
      }
    }
    if (handled) return;
  }
  if (app.help_mode && mxw >= app.help.x && mxw < app.help.x + app.help.w &&
      myw >= app.help.y && myw < app.help.y + app.help.h) {
    app.help.scroll -= static_cast<float>(ev.wheel.y) * 48.f;
    if (app.help.scroll < 0.f) app.help.scroll = 0.f;
    return;
  }
  if (imgui_mouse) return;
  if (myw >= app.win_h - status_bar_h) return;
  int pi = pane_at(app, static_cast<float>(mxw));
  Pane& p = app.panes[pi];
  p.scroll_y -= static_cast<float>(ev.wheel.y) * 48.f;
  p.scroll_y = std::max(0.f, p.scroll_y);
  {
    float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                   app.ui_bottom;
    float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
    if (p.scroll_y > max_s) p.scroll_y = max_s;
  }
  ensure_virtual_layout(app, p);
  return;
}

static void on_mousebuttondown(App& app, SDL_Event& ev, bool imgui_mouse,
                               bool imgui_keys) {
  if (static_cast<float>(ev.button.y) < app.ui_top) return;
  if (ev.button.y >= app.win_h - status_bar_h) return;

  if (ev.button.button == SDL_BUTTON_LEFT) {
    const int mxw = ev.button.x, myw = ev.button.y;

    // Software windows are custom-drawn (not ImGui). Hit-test them
    // before the ImGui capture check and before the document panes.
    // Z-order matches paint order: outline → browse → help.

    // ---- Backlinks software window ----
    if (app.backlinks_mode && mxw >= app.bl.x - help_edge &&
        mxw <= app.bl.x + app.bl.w + help_edge && myw >= app.bl.y - help_edge &&
        myw <= app.bl.y + app.bl.h + help_edge) {
      const bool inside = mxw >= app.bl.x && mxw < app.bl.x + app.bl.w &&
                          myw >= app.bl.y && myw < app.bl.y + app.bl.h;
      if (inside && myw < app.bl.y + help_title_h) {
        if (mxw >= app.bl.x + app.bl.w - 28) {
          app.backlinks_mode = false;
          app.bl.dragging = false;
        } else {
          app.bl.dragging = true;
          app.bl.drag_dx = mxw - app.bl.x;
          app.bl.drag_dy = myw - app.bl.y;
        }
        return;
      }
      if (inside) {
        const int title_h = help_title_h;
        const int cy = app.bl.y + title_h + 4;
        const float step = 22.f;
        int row = static_cast<int>(
            (static_cast<float>(myw - cy) + app.bl.scroll) / step);
        if (row >= 0 && row < static_cast<int>(app.backlinks.size())) {
          app.backlinks_sel = row;
          auto& pp = focused_pane(app);
          pp.sel.focus = {app.backlinks[static_cast<size_t>(row)].source, 0, 0};
          pp.sel.anchor = pp.sel.focus;
          expand_ancestors_for_node(app, pp.sel.focus.node);
          scroll_caret_into_view(app, pp);
          reset_blink(app);
        }
        return;
      }
    }

    // ---- Outline software window ----
    if (app.outline_mode && mxw >= app.ol.x - help_edge &&
        mxw <= app.ol.x + app.ol.w + help_edge && myw >= app.ol.y - help_edge &&
        myw <= app.ol.y + app.ol.h + help_edge) {
      const bool inside = mxw >= app.ol.x && mxw < app.ol.x + app.ol.w &&
                          myw >= app.ol.y && myw < app.ol.y + app.ol.h;
      {
        const bool on_r = mxw >= app.ol.x + app.ol.w - help_edge &&
                          myw >= app.ol.y + help_title_h &&
                          myw <= app.ol.y + app.ol.h + help_edge;
        const bool on_b = myw >= app.ol.y + app.ol.h - help_edge &&
                          mxw >= app.ol.x &&
                          mxw <= app.ol.x + app.ol.w + help_edge;
        if (on_r || on_b) {
          if (app.outline_docked) {
            // Docked: only horizontal resize (dock width).
            app.ol.resize = 1;
          } else {
            app.ol.resize = (on_r ? 1 : 0) | (on_b ? 2 : 0);
          }
          app.ol.resize_start_x = mxw;
          app.ol.resize_start_y = myw;
          app.ol.resize_start_w = app.ol.w;
          app.ol.resize_start_h = app.ol.h;
          app.ol.dragging = false;
          return;
        }
      }
      if (inside && myw < app.ol.y + help_title_h) {
        if (mxw >= app.ol.x + app.ol.w - 28) {
          app.outline_mode = false;
          app.ol.dragging = false;
          app.ol.resize = 0;
        } else {
          // Focus chip sits just left of ×.
          const char* flab = app.section_focus_mode ? "Focus●" : "Focus";
          int fw = app.fonts.regular.measure(flab);
          int fx = app.ol.x + app.ol.w - 28 - fw - 10;
          if (mxw >= fx - 4 && mxw < app.ol.x + app.ol.w - 28) {
            toggle_section_focus_mode(app);
            app.ol.dragging = false;
          } else {
            app.ol.dragging = true;
            app.ol.drag_dx = mxw - app.ol.x;
            app.ol.drag_dy = myw - app.ol.y;
          }
        }
        return;
      }
      if (inside) {
        auto [cx, cy, cw, ch] = outline_client_rect(app);
        const float step = outline_row_step(app);
        const int sb_w = 10;
        if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 && myw >= cy &&
            myw <= cy + ch) {
          float content_h = static_cast<float>(app.outline.size()) * step + 8.f;
          float max_s = std::max(0.f, content_h - static_cast<float>(ch));
          float view_h = static_cast<float>(std::max(1, ch));
          float ratio = (static_cast<float>(myw - cy) / view_h);
          ratio = std::max(0.f, std::min(1.f, ratio));
          app.ol.scroll = ratio * max_s;
          app.ol.scroll_dragging = true;
          return;
        }
        if (mxw >= cx && mxw < cx + cw && myw >= cy && myw < cy + ch) {
          int row = static_cast<int>(
              (static_cast<float>(myw - cy) + app.ol.scroll) / step);
          if (row >= 0 && row < static_cast<int>(app.outline.size())) {
            app.outline_sel = row;
            const auto& er = app.outline[static_cast<size_t>(row)];
            // Disclosure hit only on the ▸/▾ glyph, not the heading
            // text (old 48+level*12 zone stole clicks into fold).
            float local_x = static_cast<float>(mxw - cx);
            const float indent_px =
                static_cast<float>(std::max(0, er.level - 1)) * 12.f;
            // Paint draws at cx+8 with level spaces then the twisty.
            const float twist_x0 = 8.f + indent_px;
            const float twist_x1 = twist_x0 + 22.f;
            const bool on_twist = er.has_children &&
                                  local_x >= twist_x0 - 2.f &&
                                  local_x < twist_x1;
            if (on_twist && er.section != xanatoo::INVALID_NODE) {
              toggle_section_fold(app, er.section);
            } else {
              jump_outline_sel(app);
            }
          }
        }
        return;
      }
    }

    // ---- Document software windows (topmost first) ----
    {
      bool soft_hit = false;
      for (int ii = static_cast<int>(app.soft_wins.size()) - 1; ii >= 0; --ii) {
        const size_t i = static_cast<size_t>(ii);
        SoftWindow& sw = app.soft_wins[i];
        if (mxw < sw.x - help_edge || mxw > sw.x + sw.w + help_edge ||
            myw < sw.y - help_edge || myw > sw.y + sw.h + help_edge)
          continue;
        const bool inside = mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
                            myw < sw.y + sw.h;
        const bool on_r = mxw >= sw.x + sw.w - help_edge &&
                          myw >= sw.y + help_title_h &&
                          myw <= sw.y + sw.h + help_edge;
        const bool on_b = myw >= sw.y + sw.h - help_edge && mxw >= sw.x &&
                          mxw <= sw.x + sw.w + help_edge;
        if (on_r || on_b) {
          raise_soft(app, i);
          SoftWindow& top = app.soft_wins.back();
          top.resize = (on_r ? 1 : 0) | (on_b ? 2 : 0);
          top.resize_start_x = mxw;
          top.resize_start_y = myw;
          top.resize_start_w = top.w;
          top.resize_start_h = top.h;
          top.dragging = false;
          soft_hit = true;
          break;
        }
        if (inside && myw < sw.y + help_title_h) {
          if (mxw >= sw.x + sw.w - 28) {
            app.soft_wins.erase(app.soft_wins.begin() +
                                static_cast<std::ptrdiff_t>(i));
          } else {
            raise_soft(app, i);
            SoftWindow& top = app.soft_wins.back();
            const Uint32 now = SDL_GetTicks();
            if (top.last_title_click != 0 && now - top.last_title_click < 400) {
              maximize_soft(app, top);
              top.last_title_click = 0;
              top.dragging = false;
            } else {
              top.last_title_click = now;
              // Dragging a tiled/maximized window restores float first.
              if (top.maximized || top.dock != SoftDock::None)
                restore_soft_float(app, top, mxw, myw);
              top.dragging = true;
              top.drag_dx = mxw - top.x;
              top.drag_dy = myw - top.y;
              app.dock_preview = SoftDock::None;
            }
          }
          soft_hit = true;
          break;
        }
        if (inside) {
          raise_soft(app, i);
          SoftWindow& top = app.soft_wins.back();
          auto [cx, cy, cw, ch] = soft_client_rect(app, top);
          const int sb_w = 10;
          if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 && myw >= cy &&
              myw <= cy + ch) {
            float max_s = std::max(
                0.f, top.layout.content_height - static_cast<float>(ch) + 40.f);
            float view_h = static_cast<float>(std::max(1, ch));
            float ratio = (static_cast<float>(myw - cy) / view_h);
            ratio = std::max(0.f, std::min(1.f, ratio));
            top.scroll = ratio * max_s;
            top.scroll_dragging = true;
            soft_hit = true;
            break;
          }
          float lx = static_cast<float>(mxw - cx);
          float ly = static_cast<float>(myw - cy);
          if (lx >= 0 && ly >= 0 && lx <= cw && ly <= ch) {
            auto& sdoc = soft_doc(app, top);
            if (auto sid =
                    xanatoo::hit_test_stretch(top.layout, lx, ly, top.scroll)) {
              if (sdoc.get(*sid).kind == xanatoo::NodeKind::Stretch) {
                // Shared buffer: stretch state is document-owned.
                int bi = top.buf;
                if (bi >= 0 && bi < static_cast<int>(app.buffers.size())) {
                  // Snapshot for undo (view-independent history).
                  app.buffers[static_cast<size_t>(bi)].history.push(sdoc,
                                                                    top.sel);
                  sdoc.get(*sid).expanded = !sdoc.get(*sid).expanded;
                  app.buffers[static_cast<size_t>(bi)].dirty = true;
                  update_title(app);
                } else {
                  sdoc.get(*sid).expanded = !sdoc.get(*sid).expanded;
                }
                top.needs_layout = true;
                app.needs_layout = true;
                for (auto& osw : app.soft_wins)
                  if (osw.buf == top.buf) osw.needs_layout = true;
              }
            }
            if (auto href = xanatoo::hit_test_link(sdoc, top.layout, lx, ly,
                                                   top.scroll)) {
              if (!href->empty() && (*href)[0] == '#') {
                if (auto dest = xanatoo::jump_to_href(sdoc, *href)) {
                  auto cpos = xanatoo::caret_screen_pos(
                      top.layout, *dest, 0.f,
                      static_cast<float>(app.fonts.regular.line_height()));
                  if (cpos.valid) top.scroll = std::max(0.f, cpos.y - 24.f);
                }
              } else if (!href->empty()) {
                // Follow file / gemini / external from soft window.
                // Always load into the focused pane (same as main-pane
                // clicks). open_browse here left stale soft layouts and
                // carets that later threw "invalid node id".
                std::string abs = *href;
                if (is_in_app_remote_url(abs)) {
                  navigate_to_url(app, abs);
                } else if (is_inapp_doc_href(app, abs)) {
                  std::string resolved =
                      resolve_doc_href(app, abs, soft_path(app, top));
                  navigate_to_url(app, resolved.empty() ? abs : resolved);
                } else if (abs[0] != '#') {
                  xanatoo::open_external_url(abs);
                  flash_status(app, "Opening " + abs);
                }
              }
            }
          }
          soft_hit = true;
          break;
        }
      }
      if (soft_hit) return;
    }

    // ---- Help software window ----
    if (app.help_mode && mxw >= app.help.x - help_edge &&
        mxw <= app.help.x + app.help.w + help_edge &&
        myw >= app.help.y - help_edge &&
        myw <= app.help.y + app.help.h + help_edge) {
      const bool inside = mxw >= app.help.x && mxw < app.help.x + app.help.w &&
                          myw >= app.help.y && myw < app.help.y + app.help.h;
      {
        const bool on_right = mxw >= app.help.x + app.help.w - help_edge &&
                              myw >= app.help.y + help_title_h &&
                              myw <= app.help.y + app.help.h + help_edge;
        const bool on_bottom = myw >= app.help.y + app.help.h - help_edge &&
                               mxw >= app.help.x &&
                               mxw <= app.help.x + app.help.w + help_edge;
        if (on_right || on_bottom) {
          app.help.resize = (on_right ? 1 : 0) | (on_bottom ? 2 : 0);
          app.help.resize_start_x = mxw;
          app.help.resize_start_y = myw;
          app.help.resize_start_w = app.help.w;
          app.help.resize_start_h = app.help.h;
          app.help.dragging = false;
          return;
        }
      }
      if (inside && myw < app.help.y + help_title_h) {
        if (mxw >= app.help.x + app.help.w - 28) {
          app.help_mode = false;
          app.help.dragging = false;
          app.help.resize = 0;
        } else {
          app.help.dragging = true;
          app.help.drag_dx = mxw - app.help.x;
          app.help.drag_dy = myw - app.help.y;
        }
        return;
      }
      if (inside) {
        auto [cx, cy, cw, ch] = help_client_rect(app);
        const int sb_w = 10;
        if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 && myw >= cy &&
            myw <= cy + ch) {
          float max_s = std::max(0.f, app.help_layout.content_height -
                                          static_cast<float>(ch) + 40.f);
          float view_h = static_cast<float>(std::max(1, ch));
          float ratio = (static_cast<float>(myw - cy) / view_h);
          ratio = std::max(0.f, std::min(1.f, ratio));
          app.help.scroll = ratio * max_s;
          app.help.scroll_dragging = true;
          return;
        }
        float lx = static_cast<float>(mxw - cx);
        float ly = static_cast<float>(myw - cy);
        if (lx >= 0 && ly >= 0 && lx <= cw && ly <= ch) {
          if (auto sid = xanatoo::hit_test_stretch(app.help_layout, lx, ly,
                                                   app.help.scroll)) {
            if (app.help_doc.get(*sid).kind == xanatoo::NodeKind::Stretch) {
              app.help_doc.get(*sid).expanded =
                  !app.help_doc.get(*sid).expanded;
              app.help_needs_layout = true;
              return;
            }
          }
          if (auto href = xanatoo::hit_test_link(app.help_doc, app.help_layout,
                                                 lx, ly, app.help.scroll)) {
            if (!href->empty() && (*href)[0] == '#') {
              if (auto dest = xanatoo::jump_to_href(app.help_doc, *href)) {
                auto cpos = xanatoo::caret_screen_pos(
                    app.help_layout, *dest, 0.f,
                    static_cast<float>(app.fonts.regular.line_height()));
                if (cpos.valid) app.help.scroll = std::max(0.f, cpos.y - 24.f);
              }
            }
          }
        }
        return;  // absorb click even if no hit target
      }
    }

    // Below soft windows: let ImGui keep the event if it wants it.
    if (imgui_mouse) return;

    // Clicking the document dismisses status-bar prompts (Esc also works).
    if (app.save_as_mode || app.path_in_window_mode || app.open_file_mode ||
        app.url_bar_mode || app.image_insert_mode || app.include_insert_mode ||
        app.link_edit_mode || app.summary_edit_mode) {
      app.save_as_mode = false;
      app.save_as_path.clear();
      app.path_in_window_mode = false;
      app.path_in_window_buf.clear();
      app.open_file_mode = false;
      app.open_file_buf.clear();
      app.url_bar_mode = false;
      app.url_bar_buf.clear();
      app.image_insert_mode = false;
      app.image_insert_path.clear();
      app.image_insert_as_figure = false;
      app.include_insert_mode = false;
      app.include_insert_src.clear();
      app.link_edit_mode = false;
      app.link_edit_href.clear();
      app.summary_edit_mode = false;
      app.summary_edit_text.clear();
    }

    float mx = static_cast<float>(ev.button.x);
    float my = content_y(app, static_cast<float>(ev.button.y));

    // Classic scrollbar (right edge): arrows page, track jumps/drags.
    {
      int pi = pane_at(app, mx);
      int sx = app.panes[pi].x + app.panes[pi].w - kSbW;
      // `my` is content Y (0 at top of viewport). Usable height excludes
      // the menu/toolbar and the status bar.
      float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                     app.ui_bottom;
      if (mx >= sx && my >= 0.f && my < view_h) {
        app.focus = pi;
        Pane& sp = app.panes[pi];
        float content_h = std::max(view_h, sp.layout.content_height);
        float max_s = std::max(0.f, content_h - view_h + 40.f);
        const float step =
            static_cast<float>(app.fonts.regular.line_height()) * 3.f;
        if (my < static_cast<float>(kSbArrow)) {
          sp.scroll_y = std::max(0.f, sp.scroll_y - step);
          ensure_virtual_layout(app, sp);
          return;
        }
        if (my >= view_h - static_cast<float>(kSbArrow)) {
          sp.scroll_y = std::min(max_s, sp.scroll_y + step);
          ensure_virtual_layout(app, sp);
          return;
        }
        // Track between arrows: page toward click, or grab the thumb.
        float track_h =
            std::max(1.f, view_h - 2.f * static_cast<float>(kSbArrow));
        float track_my = my - static_cast<float>(kSbArrow);
        float thumb_h =
            std::max(20.f, track_h * (view_h / std::max(view_h, content_h)));
        if (thumb_h > track_h) thumb_h = track_h;
        float travel = std::max(0.f, track_h - thumb_h);
        float ratio = (max_s > 0.f) ? (sp.scroll_y / max_s) : 0.f;
        float thumb_top = ratio * travel;
        float thumb_bot = thumb_top + thumb_h;
        if (track_my < thumb_top || track_my > thumb_bot) {
          // Shift+click jumps; plain click pages toward the click.
          if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
            float tr = track_my / std::max(1.f, track_h);
            tr = std::max(0.f, std::min(1.f, tr));
            sp.scroll_y = tr * max_s;
          } else if (track_my < thumb_top) {
            sp.scroll_y = std::max(0.f, sp.scroll_y - view_h * 0.85f);
          } else {
            sp.scroll_y = std::min(max_s, sp.scroll_y + view_h * 0.85f);
          }
          ensure_virtual_layout(app, sp);
          return;
        }
        // On thumb — drag without jumping.
        app.dragging_scroll = true;
        app.scroll_drag_pane = pi;
        app.scroll_drag_grab = track_my - thumb_top;
        return;
      }
    }

    // Drag the split gutter?
    if (app.split) {
      int gutter_x = app.panes[1].x - 3;
      if (mx >= gutter_x && mx <= gutter_x + 6) {
        app.dragging_split = true;
        return;
      }
    }

    app.focus = pane_at(app, mx);
    Pane& p = focused_pane(app);
    float lx = local_x(app, mx, app.focus);

    // Image resize handle (bottom-right 12px square)?
    {
      const float hs = 12.f;
      for (const auto& pi : p.layout.images) {
        float hx = pi.x + pi.width - hs;
        float hy = pi.y + pi.height - hs - p.scroll_y;
        if (lx >= hx && lx <= hx + hs && my >= hy && my <= hy + hs) {
          if (app.browse_mode) break;
          edit(app);
          app.dragging_image = true;
          app.resize_image = pi.node;
          app.resize_start_x = mx;
          app.resize_start_scale = doc_of(app, p).get(pi.node).display_scale;
          if (app.resize_start_scale < 0.05f) app.resize_start_scale = 1.f;
          break;
        }
      }
      if (app.dragging_image) return;
    }

    // Column gutter drag (between side-by-side columns)
    {
      float adj_y = my + p.scroll_y;
      for (const auto& gap : p.layout.column_gaps) {
        if (adj_y < gap.y || adj_y > gap.y + gap.height) continue;
        if (std::abs(lx - gap.x) > 8.f) continue;
        if (app.browse_mode) break;
        edit(app);
        app.dragging_col_gap = true;
        app.col_gap_left = gap.left;
        app.col_gap_right = gap.right;
        app.col_gap_start_x = mx;
        float lw = doc_of(app, p).get(gap.left).display_scale;
        float rw = doc_of(app, p).get(gap.right).display_scale;
        if (lw < 0.1f) lw = 1.f;
        if (rw < 0.1f) rw = 1.f;
        app.col_gap_left_w = lw;
        app.col_gap_right_w = rw;
        break;
      }
      if (app.dragging_col_gap) return;
    }

    // Block drag: grab gutter immediately left of the block text
    {
      float adj_y = my + p.scroll_y;
      for (const auto& box : p.layout.boxes) {
        if (adj_y < box.y || adj_y > box.y + box.height) continue;
        if (lx < box.x - kBlockGripW || lx >= box.x) continue;
        const auto& bn = doc_of(app, p).get(box.node);
        // Only reorder movable siblings (not structural containers)
        if (bn.kind == xanatoo::NodeKind::Document ||
            bn.kind == xanatoo::NodeKind::Columns ||
            bn.kind == xanatoo::NodeKind::Column ||
            bn.kind == xanatoo::NodeKind::List)
          continue;
        if (bn.parent == xanatoo::INVALID_NODE) continue;
        if (app.browse_mode) break;
        app.dragging_block = true;
        app.drag_block_id = box.node;
        app.drop_target_id = xanatoo::INVALID_NODE;
        app.drop_line_y = -1.f;
        break;
      }
      if (app.dragging_block) return;
    }

    if (auto sid = xanatoo::hit_test_stretch(p.layout, lx, my, p.scroll_y)) {
      xanatoo::NodeId id = *sid;
      if (doc_of(app, p).get(id).kind == xanatoo::NodeKind::Stretch) {
        edit(app);
        doc_of(app, p).get(id).expanded = !doc_of(app, p).get(id).expanded;
        app.needs_layout = true;
        return;
      }
    }

    // Follow links: internal #anchors jump in-document; http(s)/mailto open
    // externally
    if (auto href = xanatoo::hit_test_link(doc_of(app, p), p.layout, lx, my,
                                           p.scroll_y)) {
      if (auto dest = xanatoo::jump_to_href(doc_of(app, p), *href)) {
        const bool ctrl_click = (SDL_GetModState() & KMOD_CTRL) != 0;
        expand_ancestors_for_node(app, dest->node);
        // Ctrl+click: open target in the other pane (enable split)
        if (ctrl_click) {
          if (!app.split) {
            app.split = true;
            app.needs_layout = true;
          }
          int other = 1 - app.focus;
          app.panes[other].sel.focus = *dest;
          app.panes[other].sel.anchor = *dest;
          scroll_caret_into_view(app, app.panes[other]);
          flash_status(app, "Link in other pane: " + *href);
          std::cout << "Opened link in other pane: " << *href << "\n";
        } else {
          p.sel.focus = *dest;
          p.sel.anchor = p.sel.focus;
          scroll_caret_into_view(app, p);
          // Dual-pane shared doc: mirror target in the other view
          if (app.split && shared_doc(app)) {
            int other = 1 - app.focus;
            app.panes[other].sel.focus = *dest;
            app.panes[other].sel.anchor = *dest;
            scroll_caret_into_view(app, app.panes[other]);
          }
          flash_status(app, "Followed " + *href);
          std::cout << "Followed link: " << *href << "\n";
        }
        reset_blink(app);
        return;
      } else if (!href->empty() && (*href)[0] != '#') {
        std::string abs = *href;
        if (!xanatoo::is_gemini_url(abs) && !app.gemini_current_url.empty()) {
          std::string resolved =
              xanatoo::resolve_gemini_url(app.gemini_current_url, abs);
          if (!resolved.empty()) abs = resolved;
        }
        if (is_in_app_remote_url(abs)) {
          navigate_to_url(app, std::move(abs));
          return;
        }
        if (is_inapp_doc_href(app, abs)) {
          navigate_to_url(app, resolve_doc_href(app, abs));
          return;
        }
        flash_status(app, "Opening " + *href);
        std::cout << "Opening external link: " << *href << "\n";
        xanatoo::open_external_url(*href);
        return;
      }
    }

    // Click list-item marker column → toggle GFM task checkbox
    if (!app.browse_mode) {
      bool toggled = false;
      float adj_y = my + p.scroll_y;
      for (const auto& box : p.layout.boxes) {
        if (adj_y < box.y || adj_y > box.y + box.height) continue;
        if (lx < box.x || lx > box.x + 26.f) continue;
        auto& bn = doc_of(app, p).get(box.node);
        if (bn.kind != xanatoo::NodeKind::ListItem) continue;
        // Ordered items keep numbers unless already a task.
        if (bn.level > 0 && bn.label != "task" && bn.label != "task-done")
          continue;
        edit(app);
        if (bn.label == "task")
          bn.label = "task-done";
        else if (bn.label == "task-done")
          bn.label = "task";
        else
          bn.label = "task";  // bullet → unchecked task
        app.needs_layout = true;
        reset_blink(app);
        toggled = true;
        break;
      }
      if (toggled) return;
    }

    // Click a transclusion → jump to local source, or open external file.
    // Alt+click opens the target in the other pane.
    {
      bool followed = false;
      const bool alt_click = (SDL_GetModState() & KMOD_ALT) != 0;
      float adj_y = my + p.scroll_y;
      for (const auto& box : p.layout.boxes) {
        if (adj_y < box.y || adj_y > box.y + box.height) continue;
        if (lx < box.x || lx > box.x + box.width) continue;
        const auto& bn = doc_of(app, p).get(box.node);
        if (bn.kind != xanatoo::NodeKind::Transclude) continue;
        if (alt_click) {
          p.sel.focus = {box.node, 0, 0};
          p.sel.anchor = p.sel.focus;
          open_transclude_in_other_pane(app);
          followed = true;
          break;
        }
        if (auto dest = xanatoo::jump_to_href(doc_of(app, p), bn.src)) {
          expand_ancestors_for_node(app, dest->node);
          p.sel.focus = *dest;
          p.sel.anchor = p.sel.focus;
          scroll_caret_into_view(app, p);
          if (app.split && shared_doc(app)) {
            int other = 1 - app.focus;
            app.panes[other].sel.focus = *dest;
            app.panes[other].sel.anchor = *dest;
            scroll_caret_into_view(app, app.panes[other]);
          }
          reset_blink(app);
          flash_status(app, "Followed include " + bn.src);
          followed = true;
        } else {
          std::string path_part = bn.src;
          size_t hash = path_part.find('#');
          if (hash != std::string::npos) path_part = path_part.substr(0, hash);
          if (!path_part.empty() &&
              (path_part.find('.') != std::string::npos ||
               path_part.find('/') != std::string::npos)) {
            std::filesystem::path resolved = path_part;
            std::string base = path_of(app, p);
            if (!base.empty() && resolved.is_relative()) {
              resolved = std::filesystem::path(base).parent_path() / path_part;
            }
            if (std::filesystem::exists(resolved)) {
              navigate_to_url(app, resolved.string());
              followed = true;
            } else {
              flash_status(app, "Broken include: " + bn.src);
            }
          } else {
            flash_status(app, "Broken include: " + bn.src);
          }
        }
        break;
      }
      if (followed) return;
    }

    if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
      Uint32 now = SDL_GetTicks();
      bool near = (now - app.last_click_ticks) < 400 &&
                  std::abs(mx - app.last_click_x) < 4.f &&
                  std::abs(my - app.last_click_y) < 4.f;
      app.click_count = near ? app.click_count + 1 : 1;
      app.last_click_ticks = now;
      app.last_click_x = mx;
      app.last_click_y = my;

      p.sel.focus = *hit;
      xanatoo::Document& dd = doc_of(app, p);
      if (app.click_count >= 3) {
        // Triple-click: select whole block
        xanatoo::select_node(dd, p.sel, hit->node);
        app.dragging_sel = false;
      } else if (app.click_count == 2) {
        // Double-click: select word
        xanatoo::select_word_at(dd, p.sel);
        app.dragging_sel = false;
      } else if (!(SDL_GetModState() & KMOD_SHIFT)) {
        p.sel.anchor = p.sel.focus;
        app.dragging_sel = true;
      } else {
        app.dragging_sel = true;
      }
      p.preferred_x = lx;
      reset_blink(app);
    }
  } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
    // Middle-click paste at caret (common on Linux).
    if (app.browse_mode) return;
    if (imgui_mouse) return;
    const int mxw = ev.button.x, myw = ev.button.y;
    bool over_soft = false;
    if (app.outline_mode && mxw >= app.ol.x && mxw < app.ol.x + app.ol.w &&
        myw >= app.ol.y && myw < app.ol.y + app.ol.h)
      over_soft = true;
    if (!over_soft && app.help_mode && mxw >= app.help.x &&
        mxw < app.help.x + app.help.w && myw >= app.help.y &&
        myw < app.help.y + app.help.h)
      over_soft = true;
    if (!over_soft) {
      for (int i = static_cast<int>(app.soft_wins.size()) - 1; i >= 0; --i) {
        const SoftWindow& sw = app.soft_wins[static_cast<size_t>(i)];
        if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
            myw < sw.y + sw.h) {
          over_soft = true;
          break;
        }
      }
    }
    if (!over_soft && SDL_HasClipboardText()) {
      float mx = static_cast<float>(mxw);
      float my = content_y(app, static_cast<float>(myw));
      app.focus = pane_at(app, mx);
      Pane& pp = focused_pane(app);
      float lx = local_x(app, mx, app.focus);
      if (auto hit = xanatoo::hit_test(pp.layout, lx, my, pp.scroll_y)) {
        pp.sel.focus = *hit;
        pp.sel.anchor = pp.sel.focus;
      }
      char* clip = SDL_GetClipboardText();
      if (clip && clip[0]) {
        edit(app);
        xanatoo::paste_plain_text(doc_of(app, pp), pp.sel.focus, clip);
        pp.sel.anchor = pp.sel.focus;
        app.needs_layout = true;
        reset_blink(app);
      }
      if (clip) SDL_free(clip);
    }
  } else if (ev.button.button == SDL_BUTTON_RIGHT) {
    // Soft windows / outline / help absorb right-clicks (no doc menu under
    // them).
    const int rx = ev.button.x, ry = ev.button.y;
    bool over_soft = false;
    if (app.outline_mode && rx >= app.ol.x && rx < app.ol.x + app.ol.w &&
        ry >= app.ol.y && ry < app.ol.y + app.ol.h)
      over_soft = true;
    if (!over_soft && app.help_mode && rx >= app.help.x &&
        rx < app.help.x + app.help.w && ry >= app.help.y &&
        ry < app.help.y + app.help.h)
      over_soft = true;
    if (!over_soft) {
      for (int i = static_cast<int>(app.soft_wins.size()) - 1; i >= 0; --i) {
        const SoftWindow& sw = app.soft_wins[static_cast<size_t>(i)];
        if (rx >= sw.x && rx < sw.x + sw.w && ry >= sw.y && ry < sw.y + sw.h) {
          over_soft = true;
          break;
        }
      }
    }
    // Document context menu (ImGui popup).
    if (!over_soft && !imgui_mouse) {
      float mx = static_cast<float>(ev.button.x);
      float my = content_y(app, static_cast<float>(ev.button.y));
      app.focus = pane_at(app, mx);
      Pane& p = focused_pane(app);
      float lx = local_x(app, mx, app.focus);
      // Keep an existing selection when the click lands inside it
      // (Cut/Copy/Bold from the menu need the range). Only move the
      // caret when there is no selection, or the click is outside.
      bool keep_selection = false;
      if (!p.sel.empty()) {
        const float lh = static_cast<float>(app.fonts.regular.line_height());
        auto rects = xanatoo::selection_rects(p.layout, p.sel, p.scroll_y, lh);
        for (const auto& r : rects) {
          if (lx >= r.x && lx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            keep_selection = true;
            break;
          }
        }
      }
      if (!keep_selection) {
        if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
          p.sel.focus = *hit;
          if (!(SDL_GetModState() & KMOD_SHIFT)) p.sel.anchor = p.sel.focus;
        }
      }
      app.ui.open_context_menu(static_cast<float>(ev.button.x),
                               static_cast<float>(ev.button.y));
    }
  }
  return;
}

static void on_mousebuttonup(App& app, SDL_Event& ev, bool imgui_mouse,
                             bool imgui_keys) {
  if (ev.button.button == SDL_BUTTON_LEFT) {
    for (auto& sw : app.soft_wins) {
      if (sw.dragging) {
        SoftDock zone = compute_dock_zone(app, ev.button.x, ev.button.y);
        if (zone != SoftDock::None) {
          apply_soft_dock(app, sw, zone);
        } else {
          snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
        }
      } else if (sw.resize) {
        snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
        sw.needs_layout = true;
      }
      sw.dragging = false;
      sw.resize = 0;
      sw.scroll_dragging = false;
    }
    app.dock_preview = SoftDock::None;
    if (app.help.dragging || app.help.resize) {
      snap_soft_geom(app, app.help.x, app.help.y, app.help.w, app.help.h);
      if (app.help.resize) app.help_needs_layout = true;
    }
    app.help.dragging = false;
    app.help.resize = 0;
    app.help.scroll_dragging = false;
    if (app.ol.dragging || app.ol.resize)
      snap_soft_geom(app, app.ol.x, app.ol.y, app.ol.w, app.ol.h);
    app.ol.dragging = false;
    app.ol.resize = 0;
    app.ol.scroll_dragging = false;
    app.bl.dragging = false;
    if (app.dragging_block && app.drag_block_id != xanatoo::INVALID_NODE &&
        app.drop_target_id != xanatoo::INVALID_NODE &&
        app.drop_target_id != app.drag_block_id) {
      edit(app);
      if (xanatoo::relocate_block(doc_of(app, focused_pane(app)),
                                  app.drag_block_id, app.drop_target_id,
                                  app.drop_before)) {
        app.needs_layout = true;
        focused_pane(app).sel.focus.node = app.drag_block_id;
        focused_pane(app).sel.focus.span_index = 0;
        focused_pane(app).sel.focus.char_index = 0;
        focused_pane(app).sel.anchor = focused_pane(app).sel.focus;
      }
    }
    app.dragging_sel = false;
    app.dragging_split = false;
    app.dragging_scroll = false;
    app.dragging_image = false;
    app.dragging_block = false;
    app.drag_block_id = xanatoo::INVALID_NODE;
    app.drop_target_id = xanatoo::INVALID_NODE;
    app.drop_line_y = -1.f;
    app.dragging_col_gap = false;
    app.col_gap_left = xanatoo::INVALID_NODE;
    app.col_gap_right = xanatoo::INVALID_NODE;
    app.resize_image = xanatoo::INVALID_NODE;
  }
  return;
}

static void on_mousemotion(App& app, SDL_Event& ev, bool imgui_mouse,
                           bool imgui_keys) {
  app.mouse_x = static_cast<float>(ev.motion.x);
  app.mouse_y = static_cast<float>(ev.motion.y);
  {
    const int mx_i = static_cast<int>(app.mouse_x);
    const int my_i = static_cast<int>(app.mouse_y);

    // Soft-window drag/resize continue even if ImGui wants the mouse
    // (capture flag can lag one frame after a title-bar grab).
    if (app.ol.dragging) {
      app.ol.x = mx_i - app.ol.drag_dx;
      app.ol.y = my_i - app.ol.drag_dy;
      if (app.ol.x < 0) app.ol.x = 0;
      if (app.ol.y < static_cast<int>(app.ui_top))
        app.ol.y = static_cast<int>(app.ui_top);
      return;
    }
    if (app.ol.scroll_dragging) {
      auto [cx, cy, cw, ch] = outline_client_rect(app);
      float step = outline_row_step(app);
      float content_h = static_cast<float>(app.outline.size()) * step + 8.f;
      float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      float view_h = static_cast<float>(std::max(1, ch));
      float ratio = (app.mouse_y - static_cast<float>(cy)) / view_h;
      ratio = std::max(0.f, std::min(1.f, ratio));
      app.ol.scroll = ratio * max_s;
      return;
    }
    if (app.ol.resize) {
      int dx = mx_i - app.ol.resize_start_x;
      int dy = my_i - app.ol.resize_start_y;
      if (app.ol.resize & 1) {
        app.ol.w = app.ol.resize_start_w + dx;
        if (app.outline_docked) {
          if (app.ol.w < 160) app.ol.w = 160;
          if (app.ol.w > app.win_w / 2) app.ol.w = app.win_w / 2;
          if (app.ol.w != app.outline_dock_w) {
            app.outline_dock_w = app.ol.w;
            app.needs_layout = true;
            layout_panes(app);
          }
        } else {
          if (app.ol.w < 200) app.ol.w = 200;
          if (app.ol.w > app.win_w - 20) app.ol.w = app.win_w - 20;
        }
      }
      if (app.ol.resize & 2 && !app.outline_docked) {
        app.ol.h = app.ol.resize_start_h + dy;
        if (app.ol.h < 140) app.ol.h = 140;
        if (app.ol.h > app.win_h - 40) app.ol.h = app.win_h - 40;
      }
      return;
    }
    if (app.bl.dragging) {
      app.bl.x = mx_i - app.bl.drag_dx;
      app.bl.y = my_i - app.bl.drag_dy;
      if (app.bl.x < 0) app.bl.x = 0;
      if (app.bl.y < static_cast<int>(app.ui_top))
        app.bl.y = static_cast<int>(app.ui_top);
      return;
    }
    {
      bool soft_motion = false;
      for (auto& sw : app.soft_wins) {
        if (sw.dragging) {
          sw.x = mx_i - sw.drag_dx;
          sw.y = my_i - sw.drag_dy;
          if (sw.x < 0) sw.x = 0;
          if (sw.y < static_cast<int>(app.ui_top))
            sw.y = static_cast<int>(app.ui_top);
          app.dock_preview = compute_dock_zone(app, mx_i, my_i);
          soft_motion = true;
          break;
        }
        if (sw.scroll_dragging) {
          auto [cx, cy, cw, ch] = soft_client_rect(app, sw);
          float max_s = std::max(
              0.f, sw.layout.content_height - static_cast<float>(ch) + 40.f);
          float view_h = static_cast<float>(std::max(1, ch));
          float ratio = (app.mouse_y - static_cast<float>(cy)) / view_h;
          ratio = std::max(0.f, std::min(1.f, ratio));
          sw.scroll = ratio * max_s;
          soft_motion = true;
          break;
        }
        if (sw.resize) {
          int dx = mx_i - sw.resize_start_x;
          int dy = my_i - sw.resize_start_y;
          if (sw.resize & 1) {
            sw.w = sw.resize_start_w + dx;
            if (sw.w < 280) sw.w = 280;
            if (sw.w > app.win_w - 20) sw.w = app.win_w - 20;
            sw.needs_layout = true;
          }
          if (sw.resize & 2) {
            sw.h = sw.resize_start_h + dy;
            if (sw.h < 160) sw.h = 160;
            if (sw.h > app.win_h - 40) sw.h = app.win_h - 40;
          }
          soft_motion = true;
          break;
        }
      }
      if (soft_motion) return;
    }
    if (app.help.dragging) {
      app.help.x = mx_i - app.help.drag_dx;
      app.help.y = my_i - app.help.drag_dy;
      if (app.help.x < 0) app.help.x = 0;
      if (app.help.y < static_cast<int>(app.ui_top))
        app.help.y = static_cast<int>(app.ui_top);
      return;
    }
    if (app.help.scroll_dragging) {
      auto [cx, cy, cw, ch] = help_client_rect(app);
      float max_s = std::max(
          0.f, app.help_layout.content_height - static_cast<float>(ch) + 40.f);
      float view_h = static_cast<float>(std::max(1, ch));
      float ratio = (app.mouse_y - static_cast<float>(cy)) / view_h;
      ratio = std::max(0.f, std::min(1.f, ratio));
      app.help.scroll = ratio * max_s;
      return;
    }
    if (app.help.resize) {
      int dx = mx_i - app.help.resize_start_x;
      int dy = my_i - app.help.resize_start_y;
      if (app.help.resize & 1) {
        app.help.w = app.help.resize_start_w + dx;
        if (app.help.w < 280) app.help.w = 280;
        if (app.help.w > app.win_w - 20) app.help.w = app.win_w - 20;
        app.help_needs_layout = true;
      }
      if (app.help.resize & 2) {
        app.help.h = app.help.resize_start_h + dy;
        if (app.help.h < 160) app.help.h = 160;
        if (app.help.h > app.win_h - 40) app.help.h = app.win_h - 40;
      }
      return;
    }
    // Soft-window interactions handled; rest of motion is for panes.
    if (imgui_mouse) return;

    // Status bar is not part of the document surface.
    if (my_i >= app.win_h - status_bar_h && !app.dragging_sel &&
        !app.dragging_scroll && !app.dragging_split && !app.dragging_image &&
        !app.dragging_block && !app.dragging_col_gap) {
      return;
    }

    // Hovering a soft window (without an active document drag) should not
    // drive pane selection, scroll-hover, or gutter feedback underneath.
    if (!app.dragging_sel && !app.dragging_scroll && !app.dragging_split &&
        !app.dragging_image && !app.dragging_block && !app.dragging_col_gap) {
      const int mxw = mx_i, myw = my_i;
      bool over_soft = false;
      if (app.outline_mode && mxw >= app.ol.x && mxw < app.ol.x + app.ol.w &&
          myw >= app.ol.y && myw < app.ol.y + app.ol.h)
        over_soft = true;
      if (!over_soft && app.help_mode && mxw >= app.help.x &&
          mxw < app.help.x + app.help.w && myw >= app.help.y &&
          myw < app.help.y + app.help.h)
        over_soft = true;
      if (!over_soft) {
        for (const auto& sw : app.soft_wins) {
          if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
              myw < sw.y + sw.h) {
            over_soft = true;
            break;
          }
        }
      }
      if (over_soft) return;
    }

    // Scrollbar hover (focused pane) for arrow highlight.
    if (!app.dragging_scroll) {
      app.sb_hover = 0;
      float myh = app.mouse_y - app.ui_top;
      float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                     app.ui_bottom;
      int sx = app.panes[app.focus].x + app.panes[app.focus].w - kSbW;
      if (app.mouse_x >= sx &&
          app.mouse_x < app.panes[app.focus].x + app.panes[app.focus].w &&
          myh >= 0.f && myh < view_h) {
        if (myh < static_cast<float>(kSbArrow))
          app.sb_hover = 1;
        else if (myh >= view_h - static_cast<float>(kSbArrow))
          app.sb_hover = 2;
        else
          app.sb_hover = 3;
      }
    }

    if (app.dragging_scroll) {
      float my = app.mouse_y - app.ui_top;
      Pane& sp = app.panes[app.scroll_drag_pane];
      float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                     app.ui_bottom;
      float content_h = std::max(view_h, sp.layout.content_height);
      float max_s = std::max(0.f, content_h - view_h + 40.f);
      float track_h =
          std::max(1.f, view_h - 2.f * static_cast<float>(kSbArrow));
      float track_my = my - static_cast<float>(kSbArrow);
      float thumb_h =
          std::max(20.f, track_h * (view_h / std::max(view_h, content_h)));
      if (thumb_h > track_h) thumb_h = track_h;
      float travel = std::max(1.f, track_h - thumb_h);
      float thumb_top = track_my - app.scroll_drag_grab;
      float ratio = thumb_top / travel;
      ratio = std::max(0.f, std::min(1.f, ratio));
      sp.scroll_y = ratio * max_s;
      ensure_virtual_layout(app, sp);
      return;
    }
    if (app.dragging_split && app.split) {
      float mx = app.mouse_x;
      app.split_ratio = mx / static_cast<float>(app.win_w);
      app.split_ratio = std::max(0.15f, std::min(0.85f, app.split_ratio));
      app.needs_layout = true;
      return;
    }
    if (app.dragging_image && app.resize_image != xanatoo::INVALID_NODE) {
      float mx = app.mouse_x;
      float dx = mx - app.resize_start_x;
      // 200px drag ≈ 1.0 scale change
      float scale = app.resize_start_scale + dx / 200.f;
      scale = std::max(0.15f, std::min(3.0f, scale));
      doc_of(app, focused_pane(app)).get(app.resize_image).display_scale =
          scale;
      app.needs_layout = true;
      return;
    }
    if (app.dragging_col_gap && app.col_gap_left != xanatoo::INVALID_NODE &&
        app.col_gap_right != xanatoo::INVALID_NODE) {
      float mx = app.mouse_x;
      float dx = mx - app.col_gap_start_x;
      // Shift weight from one column to the other; keep total constant
      float total = app.col_gap_left_w + app.col_gap_right_w;
      float delta = dx / 200.f * total;
      float nl = app.col_gap_left_w + delta;
      float nr = app.col_gap_right_w - delta;
      if (nl < 0.2f) {
        nl = 0.2f;
        nr = total - nl;
      }
      if (nr < 0.2f) {
        nr = 0.2f;
        nl = total - nr;
      }
      doc_of(app, focused_pane(app)).get(app.col_gap_left).display_scale = nl;
      doc_of(app, focused_pane(app)).get(app.col_gap_right).display_scale = nr;
      app.needs_layout = true;
      return;
    }
    if (app.dragging_block && app.drag_block_id != xanatoo::INVALID_NODE) {
      Pane& p = focused_pane(app);
      float adj_y = content_y(app, app.mouse_y) + p.scroll_y;
      app.drop_target_id = xanatoo::INVALID_NODE;
      app.drop_line_y = -1.f;
      for (const auto& box : p.layout.boxes) {
        if (box.node == app.drag_block_id) continue;
        if (adj_y < box.y || adj_y > box.y + box.height) continue;
        const auto& bn = doc_of(app, p).get(box.node);
        // Skip structural containers as drop targets
        if (bn.kind == xanatoo::NodeKind::Document ||
            bn.kind == xanatoo::NodeKind::Columns ||
            bn.kind == xanatoo::NodeKind::List)
          continue;
        // Top half → before, bottom half → after
        app.drop_before = (adj_y < box.y + box.height * 0.5f);
        app.drop_target_id = box.node;
        app.drop_line_y = app.drop_before ? box.y : (box.y + box.height);
        break;
      }
      return;
    }
    if (app.dragging_sel) {
      float mx = app.mouse_x;
      float my = content_y(app, app.mouse_y);
      // Keep focus on the pane where drag started
      Pane& p = focused_pane(app);
      float lx = local_x(app, mx, app.focus);
      if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
        p.sel.focus = *hit;
        p.preferred_x = lx;
      }
    } else {
      // Track interactive elements under the cursor for status, tooltip,
      // highlight, and pointer shape.
      float mx = app.mouse_x;
      float my = content_y(app, app.mouse_y);
      // mouse_x/y already set above (stick or hardware); do not clobber.
      int hover_pane = pane_at(app, mx);
      Pane& hp = app.panes[hover_pane];
      float lx = local_x(app, mx, hover_pane);
      auto& hd = doc_of(app, hp);

      app.hover.href.clear();
      app.hover.rel.clear();
      app.hover.is_cite = false;
      app.hover.preview.clear();
      app.hover.link_node = xanatoo::INVALID_NODE;
      app.hover.link_span = 0;
      app.hover.link_pane = -1;
      app.hover.stretch = xanatoo::INVALID_NODE;
      app.hover.stretch_pane = -1;
      app.hover.image = xanatoo::INVALID_NODE;
      app.hover.image_pane = -1;
      app.hover.image_handle = false;
      app.hover.col_gap = false;
      app.hover.col_gap_pane = -1;
      app.hover.task = xanatoo::INVALID_NODE;
      app.hover.task_pane = -1;
      app.hover.grip = xanatoo::INVALID_NODE;
      app.hover.grip_pane = -1;
      app.hover.transclude = xanatoo::INVALID_NODE;
      app.hover.transclude_pane = -1;
      app.hover.transclude_src.clear();

      // Column gutters
      for (const auto& gap : hp.layout.column_gaps) {
        float gx = gap.x;
        float gy0 = gap.y - hp.scroll_y;
        float gy1 = gy0 + gap.height;
        if (lx >= gx - 5.f && lx <= gx + 5.f && my >= gy0 && my <= gy1) {
          app.hover.col_gap = true;
          app.hover.col_gap_pane = hover_pane;
          break;
        }
      }

      // Image body / resize handle (bottom-right 12px)
      if (!app.hover.col_gap) {
        for (const auto& pi : hp.layout.images) {
          float ix0 = pi.x, iy0 = pi.y - hp.scroll_y;
          float ix1 = ix0 + pi.width, iy1 = iy0 + pi.height;
          if (lx >= ix0 && lx <= ix1 && my >= iy0 && my <= iy1) {
            app.hover.image = pi.node;
            app.hover.image_pane = hover_pane;
            const float hs = 12.f;
            if (lx >= ix1 - hs && my >= iy1 - hs) app.hover.image_handle = true;
            break;
          }
        }
      }

      // Stretch marker (collapsed line or expanded header)
      if (!app.hover.col_gap && app.hover.image == xanatoo::INVALID_NODE) {
        if (auto sid =
                xanatoo::hit_test_stretch(hp.layout, lx, my, hp.scroll_y)) {
          if (hd.get(*sid).kind == xanatoo::NodeKind::Stretch) {
            app.hover.stretch = *sid;
            app.hover.stretch_pane = hover_pane;
          }
        }
      }

      // Links / cites (skip stretch-marker glyphs — those are stretch)
      if (!app.hover.col_gap && app.hover.stretch == xanatoo::INVALID_NODE &&
          app.hover.image == xanatoo::INVALID_NODE) {
        if (auto hit =
                xanatoo::hit_test_link_ex(hd, hp.layout, lx, my, hp.scroll_y)) {
          // Stretch summaries are laid out as Link-styled glyphs on a
          // Stretch node; treat them as stretch, not as hyperlinks.
          if (hd.get(hit->node).kind == xanatoo::NodeKind::Stretch) {
            app.hover.stretch = hit->node;
            app.hover.stretch_pane = hover_pane;
          } else if (!hit->href.empty()) {
            app.hover.href = hit->href;
            app.hover.rel = hit->rel;
            app.hover.is_cite = (hit->kind == xanatoo::InlineKind::Cite);
            app.hover.link_node = hit->node;
            app.hover.link_span = hit->span_index;
            app.hover.link_pane = hover_pane;
            if (auto prev = xanatoo::link_preview_text(hd, hit->href)) {
              app.hover.preview = *prev;
            }
          }
        }
      }

      // Task checkbox column (left ~26px of a ListItem)
      if (!app.hover.col_gap && app.hover.stretch == xanatoo::INVALID_NODE &&
          app.hover.image == xanatoo::INVALID_NODE &&
          app.hover.link_node == xanatoo::INVALID_NODE) {
        float adj_y = my + hp.scroll_y;
        for (const auto& box : hp.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x || lx > box.x + 26.f) continue;
          const auto& bn = hd.get(box.node);
          if (bn.kind != xanatoo::NodeKind::ListItem) continue;
          if (bn.level > 0 && bn.label != "task" && bn.label != "task-done")
            continue;
          app.hover.task = box.node;
          app.hover.task_pane = hover_pane;
          break;
        }
      }

      // Block drag grip (gutter left of the block text)
      if (!app.hover.col_gap && app.hover.task == xanatoo::INVALID_NODE &&
          app.hover.stretch == xanatoo::INVALID_NODE &&
          app.hover.link_node == xanatoo::INVALID_NODE &&
          app.hover.image == xanatoo::INVALID_NODE) {
        float adj_y = my + hp.scroll_y;
        for (const auto& box : hp.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x - kBlockGripW || lx >= box.x) continue;
          const auto& bn = hd.get(box.node);
          if (bn.kind == xanatoo::NodeKind::Document ||
              bn.kind == xanatoo::NodeKind::Columns ||
              bn.kind == xanatoo::NodeKind::Column ||
              bn.kind == xanatoo::NodeKind::List)
            continue;
          app.hover.grip = box.node;
          app.hover.grip_pane = hover_pane;
          break;
        }
      }

      // Transclude / include region
      if (!app.hover.col_gap && app.hover.task == xanatoo::INVALID_NODE &&
          app.hover.grip == xanatoo::INVALID_NODE &&
          app.hover.stretch == xanatoo::INVALID_NODE &&
          app.hover.link_node == xanatoo::INVALID_NODE &&
          app.hover.image == xanatoo::INVALID_NODE) {
        float adj_y = my + hp.scroll_y;
        for (const auto& box : hp.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x || lx > box.x + box.width) continue;
          const auto& bn = hd.get(box.node);
          if (bn.kind != xanatoo::NodeKind::Transclude) continue;
          app.hover.transclude = box.node;
          app.hover.transclude_pane = hover_pane;
          app.hover.transclude_src = bn.src;
          break;
        }
      }
    }
  }  // mx_i / my_i scope
  return;
}

static void on_textinput(App& app, SDL_Event& ev, bool imgui_mouse,
                         bool imgui_keys) {
  // Status-bar prompts own typing even when ImGui still reports capture
  // (menu bar / toolbar focus must not steal path or search input).
  if (minibuf_active(app)) {
    app.minibuf.insert_utf8(ev.text.text);
    minibuf_sync_out(app);
    return;
  }

  if (app.link_edit_mode) {
    app.link_edit_href += ev.text.text;
    return;
  }
  if (app.summary_edit_mode) {
    app.summary_edit_text += ev.text.text;
    return;
  }
  if (app.search_mode) {
    if (app.replace_mode && !app.search_query.empty() && app.replace_focus) {
      app.replace_text += ev.text.text;
      return;
    }
    app.search_query += ev.text.text;
    Pane& p = focused_pane(app);
    size_t mlen = 0;
    xanatoo::Caret from = p.sel.focus;
    if (from.char_index > 0) from.char_index -= 1;
    if (auto hit =
            xanatoo::find_next(doc_of(app, p), app.search_query, from, mlen)) {
      p.sel.anchor = *hit;
      p.sel.focus = *hit;
      xanatoo::advance_caret(doc_of(app, p), p.sel.focus, mlen);
      scroll_caret_into_view(app, p);
    }
    reset_blink(app);
    return;
  }
  if (app.browse_mode) return;  // read-only: no typing into the document
  if (imgui_keys) return;
  edit(app);
  Pane& p = focused_pane(app);
  xanatoo::Document& td = doc_of(app, p);
  if (!p.sel.empty()) {
    xanatoo::delete_selection(td, p.sel);
  }
  xanatoo::ensure_valid_caret(td, p.sel.focus);
  xanatoo::insert_text(td, p.sel.focus, ev.text.text);
  p.sel.anchor = p.sel.focus;
  app.needs_layout = true;
  reset_blink(app);
  return;
}

static void on_controllerdeviceadded(App& app, SDL_Event& ev, bool imgui_mouse,
                                     bool imgui_keys) {
  if (!app.gamepad) {
    open_first_gamepad(app);
    if (app.gamepad) {
      app.stick_cursor_on = true;
      app.stick_cursor_init = false;
      flash_status(app,
                   "Pad: R-stick cursor · A click · B rclick · "
                   "D-pad nav · L-stick scroll · Start help");
    }
  }
  return;
}

static void on_controllerdeviceremoved(App& app, SDL_Event& ev,
                                       bool imgui_mouse, bool imgui_keys) {
  if (app.gamepad &&
      ev.cdevice.which ==
          SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app.gamepad))) {
    if (app.stick_lmb || app.stick_rmb) {
      SDL_Event me{};
      me.type = SDL_MOUSEBUTTONUP;
      me.button.state = SDL_RELEASED;
      me.button.clicks = 1;
      me.button.x = static_cast<Sint32>(app.stick_cx);
      me.button.y = static_cast<Sint32>(app.stick_cy);
      me.button.windowID = SDL_GetWindowID(app.window);
      if (app.stick_lmb) {
        me.button.button = SDL_BUTTON_LEFT;
        SDL_PushEvent(&me);
        app.stick_lmb = false;
      }
      if (app.stick_rmb) {
        me.button.button = SDL_BUTTON_RIGHT;
        SDL_PushEvent(&me);
        app.stick_rmb = false;
      }
    }
    SDL_GameControllerClose(app.gamepad);
    app.gamepad = nullptr;
    app.stick_cursor_on = false;
    app.stick_cursor_init = false;
  }
  return;
}

static void on_controllerbuttondown(App& app, SDL_Event& ev, bool imgui_mouse,
                                    bool imgui_keys) {
  // Gamepad is always for navigation (never text entry).
  switch (ev.cbutton.button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
      nav_move_line(app, true);
      break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
      nav_move_line(app, false);
      break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
      // Collapse stretch under caret, else previous block
      if (!toggle_stretch_at_caret(app)) nav_move_block(app, true);
      break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
      if (!toggle_stretch_at_caret(app)) nav_move_block(app, false);
      break;
    case SDL_CONTROLLER_BUTTON_A: {
      // With a pad, A is left-click at the stick pointer (menus + hits).
      // Hold LB to fall back to caret activate (stretch / link under caret).
      const bool lb = SDL_GameControllerGetButton(
                          app.gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
      if (!lb) {
        app.stick_cursor_on = true;
        app.stick_lmb = true;
        SDL_Event me{};
        me.type = SDL_MOUSEBUTTONDOWN;
        me.button.button = SDL_BUTTON_LEFT;
        me.button.state = SDL_PRESSED;
        me.button.clicks = 1;
        me.button.x = static_cast<Sint32>(app.stick_cx);
        me.button.y = static_cast<Sint32>(app.stick_cy);
        me.button.windowID = SDL_GetWindowID(app.window);
        SDL_PushEvent(&me);
      } else {
        activate_at_caret(app);
      }
      break;
    }
    case SDL_CONTROLLER_BUTTON_B: {
      // Right-click at pointer; LB+B = collapse stretch under caret.
      const bool lb = SDL_GameControllerGetButton(
                          app.gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
      if (!lb) {
        app.stick_cursor_on = true;
        app.stick_rmb = true;
        SDL_Event me{};
        me.type = SDL_MOUSEBUTTONDOWN;
        me.button.button = SDL_BUTTON_RIGHT;
        me.button.state = SDL_PRESSED;
        me.button.clicks = 1;
        me.button.x = static_cast<Sint32>(app.stick_cx);
        me.button.y = static_cast<Sint32>(app.stick_cy);
        me.button.windowID = SDL_GetWindowID(app.window);
        SDL_PushEvent(&me);
      } else {
        if (!toggle_stretch_at_caret(app)) collapse_or_expand_all(app, false);
      }
      break;
    }
    case SDL_CONTROLLER_BUTTON_X:
      app.outline_mode = !app.outline_mode;
      if (app.outline_mode) {
        refresh_outline(app);
        app.outline_sel = 0;
      }
      break;
    case SDL_CONTROLLER_BUTTON_Y:
      // Forward in history when possible; else structure overlay.
      if (app.nav_hist_pos + 1 < static_cast<int>(app.nav_history.size())) {
        ++app.nav_hist_pos;
        navigate_to_url(app,
                        app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
                        /*record_history=*/false);
      } else {
        app.structure_mode = !app.structure_mode;
        flash_status(app,
                     app.structure_mode ? "Structure on" : "Structure off");
      }
      break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
      nav_header(app, true);
      break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
      nav_header(app, false);
      break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
      collapse_or_expand_all(app, false);
      break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
      collapse_or_expand_all(app, true);
      break;
    case SDL_CONTROLLER_BUTTON_BACK:
      // Prefer capsule history when available; else toggle browse.
      if (app.nav_hist_pos > 0) {
        --app.nav_hist_pos;
        navigate_to_url(app,
                        app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
                        /*record_history=*/false);
      } else {
        app.browse_mode = !app.browse_mode;
        flash_status(app,
                     app.browse_mode ? "Browse mode (read-only)" : "Edit mode");
      }
      break;
    case SDL_CONTROLLER_BUTTON_START:
      app.help_mode = !app.help_mode;
      if (app.help_mode) {
        ensure_help_doc(app);
        app.help_needs_layout = true;
      }
      break;
    case SDL_CONTROLLER_BUTTON_GUIDE:
      app.virtual_keyboard = !app.virtual_keyboard;
      if (app.virtual_keyboard && app.browse_mode) {
        app.browse_mode = false;
        flash_status(app, "Virtual keyboard — edit mode");
      } else {
        flash_status(app, app.virtual_keyboard ? "Virtual keyboard on"
                                               : "Virtual keyboard off");
      }
      break;
    default:
      break;
  }
  return;
}

static void on_controllerbuttonup(App& app, SDL_Event& ev, bool imgui_mouse,
                                  bool imgui_keys) {
  if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A && app.stick_lmb) {
    app.stick_lmb = false;
    SDL_Event me{};
    me.type = SDL_MOUSEBUTTONUP;
    me.button.button = SDL_BUTTON_LEFT;
    me.button.state = SDL_RELEASED;
    me.button.clicks = 1;
    me.button.x = static_cast<Sint32>(app.stick_cx);
    me.button.y = static_cast<Sint32>(app.stick_cy);
    me.button.windowID = SDL_GetWindowID(app.window);
    SDL_PushEvent(&me);
  } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B && app.stick_rmb) {
    app.stick_rmb = false;
    SDL_Event me{};
    me.type = SDL_MOUSEBUTTONUP;
    me.button.button = SDL_BUTTON_RIGHT;
    me.button.state = SDL_RELEASED;
    me.button.clicks = 1;
    me.button.x = static_cast<Sint32>(app.stick_cx);
    me.button.y = static_cast<Sint32>(app.stick_cy);
    me.button.windowID = SDL_GetWindowID(app.window);
    SDL_PushEvent(&me);
  }
  return;
}

static void on_keydown(App& app, SDL_Event& ev, bool imgui_mouse,
                       bool imgui_keys) {
  bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
  bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
  bool alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
  Pane& p = focused_pane(app);
  xanatoo::Document& d = doc_of(app, p);

  // Alt+letter opens the matching top-level menu (Windows/GNOME style).
  // Handled before status-prompt isolation so chrome stays reachable.
  if (alt && !ctrl && !shift) {
    const SDL_Keycode k = ev.key.keysym.sym;
    const char* menu = nullptr;
    switch (k) {
      case SDLK_f:
        menu = "File";
        break;
      case SDLK_e:
        menu = "Edit";
        break;
      case SDLK_o:
        menu = "Format";
        break;  // classic "fOrmat"
      case SDLK_i:
        menu = "Insert";
        break;
      case SDLK_v:
        menu = "View";
        break;
      case SDLK_b:
        menu = "Bookmarks";
        break;
      case SDLK_h:
        menu = "Help";
        break;
      default:
        break;
    }
    if (menu) {
      app.ui.request_menu(menu);
      return;
    }
  }

  // Status prompts own Esc / Backspace / Enter; block document keys so
  // arrows do not move the caret while typing a path or query.
  // Path/URL minibuffer also needs Tab (complete), arrows (cursor /
  // history), Home/End, Delete, and C-a/e/k/u/w.
  const bool status_prompt =
      app.save_as_mode || app.path_in_window_mode || app.open_file_mode ||
      app.url_bar_mode || app.image_insert_mode || app.include_insert_mode ||
      app.link_edit_mode || app.summary_edit_mode || app.search_mode;
  if (status_prompt) {
    const SDL_Keycode k = ev.key.keysym.sym;
    const bool minibuf_path = app.open_file_mode || app.save_as_mode ||
                              app.path_in_window_mode || app.url_bar_mode ||
                              app.image_insert_mode || app.include_insert_mode;
    const bool prompt_key =
        k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_RETURN ||
        k == SDLK_KP_ENTER || k == SDLK_DELETE ||
        // Paste into the status-bar buffer
        (ctrl && k == SDLK_v) ||
        // Search: F3 / Shift+F3 still jump matches while the query is open
        (app.search_mode && (k == SDLK_F3 || k == SDLK_g)) ||
        // Allow Tab to switch replace-field focus when replace is open
        (app.search_mode && app.replace_mode && k == SDLK_TAB) ||
        // Path minibuffer: completion + cursor + history
        (minibuf_path &&
         (k == SDLK_TAB || k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT ||
          k == SDLK_RIGHT || k == SDLK_HOME || k == SDLK_END ||
          (ctrl && (k == SDLK_a || k == SDLK_e || k == SDLK_k || k == SDLK_u ||
                    k == SDLK_w))));
    if (!prompt_key) return;
  } else if (imgui_keys && ev.key.keysym.sym != SDLK_ESCAPE &&
             ev.key.keysym.sym != SDLK_F1 && ev.key.keysym.sym != SDLK_F2) {
    return;
  }

  // Browse mode: navigation and view toggles only (no document edits).
  if (app.browse_mode) {
    const SDL_Keycode k = ev.key.keysym.sym;
    const bool browse_ok =
        k == SDLK_ESCAPE || k == SDLK_F1 || k == SDLK_F2 || k == SDLK_LEFT ||
        k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN || k == SDLK_PAGEUP ||
        k == SDLK_PAGEDOWN || k == SDLK_HOME || k == SDLK_END ||
        k == SDLK_TAB || k == SDLK_RETURN || k == SDLK_KP_ENTER ||
        k == SDLK_SPACE || k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET ||
        (ctrl && (k == SDLK_q || k == SDLK_o || k == SDLK_f || k == SDLK_g ||
                  k == SDLK_BACKSLASH || k == SDLK_l || k == SDLK_l ||
                  k == SDLK_EQUALS || k == SDLK_MINUS || k == SDLK_PLUS ||
                  k == SDLK_KP_PLUS || k == SDLK_KP_MINUS)) ||
        (ctrl && shift &&
         (k == SDLK_o || k == SDLK_l || k == SDLK_d || k == SDLK_f ||
          k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET));
    if (!browse_ok) return;
    // Space / Enter activate stretch or link
    if (k == SDLK_SPACE ||
        ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !ctrl)) {
      activate_at_caret(app);
      return;
    }
  }

  // Help window is non-modal: Esc/F1 close it; other keys edit the document.
  if (app.help_mode &&
      (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_F1)) {
    app.help_mode = false;
    app.help.dragging = false;
    return;
  }

  // ---- Minibuffer (path/URL prompts): Tab, history, cursor ----
  if (minibuf_active(app)) {
    const SDL_Keycode k = ev.key.keysym.sym;
    bool used = true;
    if (k == SDLK_ESCAPE) {
      app.open_file_mode = false;
      app.save_as_mode = false;
      app.path_in_window_mode = false;
      app.url_bar_mode = false;
      app.image_insert_mode = false;
      app.include_insert_mode = false;
      app.open_file_buf.clear();
      app.save_as_path.clear();
      app.path_in_window_buf.clear();
      app.url_bar_buf.clear();
      app.image_insert_path.clear();
      app.include_insert_src.clear();
      app.minibuf.clear();
    } else if (k == SDLK_TAB) {
      if (app.url_bar_mode &&
          (app.minibuf.text.find("://") != std::string::npos)) {
        // no filesystem completion for remote schemes
        used = false;
      } else {
        if (!app.minibuf.tab_complete_path())
          flash_status(app, "No path completions");
        minibuf_sync_out(app);
      }
    } else if (k == SDLK_UP) {
      if (app.url_bar_mode)
        app.minibuf.history_older(app.minibuf_hist_url);
      else
        app.minibuf.history_older(app.minibuf_hist_path);
      minibuf_sync_out(app);
    } else if (k == SDLK_DOWN) {
      if (app.url_bar_mode)
        app.minibuf.history_newer(app.minibuf_hist_url);
      else
        app.minibuf.history_newer(app.minibuf_hist_path);
      minibuf_sync_out(app);
    } else if (k == SDLK_LEFT && !ctrl) {
      app.minibuf.move_left();
    } else if (k == SDLK_RIGHT && !ctrl) {
      app.minibuf.move_right();
    } else if (k == SDLK_HOME || (ctrl && k == SDLK_a)) {
      app.minibuf.move_home();
    } else if (k == SDLK_END || (ctrl && k == SDLK_e)) {
      app.minibuf.move_end();
    } else if (ctrl && k == SDLK_k) {
      app.minibuf.kill_to_end();
      minibuf_sync_out(app);
    } else if (ctrl && k == SDLK_u) {
      app.minibuf.kill_line();
      minibuf_sync_out(app);
    } else if (ctrl && k == SDLK_w) {
      app.minibuf.kill_word_back();
      minibuf_sync_out(app);
    } else if (k == SDLK_BACKSPACE) {
      if (ctrl)
        app.minibuf.kill_word_back();
      else
        app.minibuf.backspace();
      minibuf_sync_out(app);
    } else if (k == SDLK_DELETE) {
      app.minibuf.delete_forward();
      minibuf_sync_out(app);
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      used = false;  // fall through to existing Enter handlers
    } else {
      used = false;
    }
    if (used) return;
  }

  if (app.backlinks_mode) {
    if (ev.key.keysym.sym == SDLK_ESCAPE ||
        (ev.key.keysym.sym == SDLK_b && ctrl && alt)) {
      app.backlinks_mode = false;
      return;
    }
    if (ev.key.keysym.sym == SDLK_UP) {
      if (app.backlinks_sel > 0) --app.backlinks_sel;
      return;
    }
    if (ev.key.keysym.sym == SDLK_DOWN) {
      if (app.backlinks_sel + 1 < static_cast<int>(app.backlinks.size()))
        ++app.backlinks_sel;
      return;
    }
    if (ev.key.keysym.sym == SDLK_RETURN ||
        ev.key.keysym.sym == SDLK_KP_ENTER) {
      if (app.backlinks_sel >= 0 &&
          app.backlinks_sel < static_cast<int>(app.backlinks.size())) {
        auto& p = focused_pane(app);
        p.sel.focus = {
            app.backlinks[static_cast<size_t>(app.backlinks_sel)].source, 0, 0};
        p.sel.anchor = p.sel.focus;
        scroll_caret_into_view(app, p);
        reset_blink(app);
      }
      return;
    }
  }

  // Outline software window: Esc/toggle close; arrows + 1-9 + Enter
  // navigate without eating other document keys.
  if (app.outline_mode) {
    if (ev.key.keysym.sym == SDLK_ESCAPE ||
        (ev.key.keysym.sym == SDLK_o && ctrl && shift)) {
      app.outline_mode = false;
      app.ol.dragging = false;
      app.ol.resize = 0;
      return;
    }
    bool outline_consumed = false;
    switch (ev.key.keysym.sym) {
      case SDLK_UP:
        if (app.outline_sel > 0) --app.outline_sel;
        outline_consumed = true;
        break;
      case SDLK_DOWN:
        if (app.outline_sel + 1 < static_cast<int>(app.outline.size()))
          ++app.outline_sel;
        outline_consumed = true;
        break;
      case SDLK_HOME:
        app.outline_sel = 0;
        outline_consumed = true;
        break;
      case SDLK_END:
        if (!app.outline.empty())
          app.outline_sel = static_cast<int>(app.outline.size()) - 1;
        outline_consumed = true;
        break;
      case SDLK_LEFT: {
        if (app.outline_sel >= 0 &&
            app.outline_sel < static_cast<int>(app.outline.size())) {
          const auto& er = app.outline[static_cast<size_t>(app.outline_sel)];
          if (er.section != xanatoo::INVALID_NODE && !er.folded)
            toggle_section_fold(app, er.section);
        }
        outline_consumed = true;
        break;
      }
      case SDLK_RIGHT: {
        if (app.outline_sel >= 0 &&
            app.outline_sel < static_cast<int>(app.outline.size())) {
          const auto& er = app.outline[static_cast<size_t>(app.outline_sel)];
          if (er.section != xanatoo::INVALID_NODE && er.folded)
            toggle_section_fold(app, er.section);
        }
        outline_consumed = true;
        break;
      }
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        jump_outline_sel(app);
        outline_consumed = true;
        break;
      default:
        if (ev.key.keysym.sym >= SDLK_1 && ev.key.keysym.sym <= SDLK_9) {
          int idx = static_cast<int>(ev.key.keysym.sym - SDLK_1);
          if (idx < static_cast<int>(app.outline.size())) {
            app.outline_sel = idx;
            jump_outline_sel(app);
            outline_consumed = true;
          }
        }
        break;
    }
    if (outline_consumed) return;
  }

  auto clear_sel = [&]() { p.sel.anchor = p.sel.focus; };
  auto move_and_sel = [&](auto mover) {
    if (!shift) clear_sel();
    mover();
    if (!shift) p.sel.anchor = p.sel.focus;
  };

  switch (ev.key.keysym.sym) {
    case SDLK_ESCAPE:
      if (app.outline_mode) {
        app.outline_mode = false;
      } else if (app.save_as_mode) {
        app.save_as_mode = false;
        app.save_as_path.clear();
      } else if (app.path_in_window_mode) {
        app.path_in_window_mode = false;
        app.path_in_window_buf.clear();
      } else if (app.open_file_mode) {
        app.open_file_mode = false;
        app.open_file_buf.clear();
      } else if (app.url_bar_mode) {
        app.url_bar_mode = false;
        app.url_bar_buf.clear();
      } else if (app.image_insert_mode) {
        app.image_insert_mode = false;
        app.image_insert_as_figure = false;
        app.image_insert_path.clear();
      } else if (app.include_insert_mode) {
        app.include_insert_mode = false;
        app.include_insert_src.clear();
      } else if (app.link_edit_mode) {
        app.link_edit_mode = false;
        app.link_edit_is_cite = false;
        app.link_edit_href.clear();
      } else if (app.summary_edit_mode) {
        app.summary_edit_mode = false;
        app.summary_edit_text.clear();
      } else if (app.search_mode) {
        app.search_mode = false;
        app.replace_mode = false;
        app.replace_focus = false;
        app.search_query.clear();
        app.replace_text.clear();
        reset_blink(app);
      } else if (app.help_mode) {
        app.help_mode = false;
      } else if (!app.soft_wins.empty()) {
        app.soft_wins.pop_back();
      } else if (!p.sel.empty()) {
        p.sel.anchor = p.sel.focus;
        reset_blink(app);
      }
      // Esc no longer quits — use Ctrl+Q
      break;

    case SDLK_F1:
      app.help_mode = !app.help_mode;
      if (app.help_mode) {
        ensure_help_doc(app);
        app.help_needs_layout = true;
      }
      break;

    case SDLK_F2:
      app.browse_mode = !app.browse_mode;
      if (app.browse_mode) {
        app.search_mode = false;
        app.save_as_mode = false;
        app.open_file_mode = false;
        app.link_edit_mode = false;
        flash_status(app,
                     "Browse mode — read-only · gamepad navigate · F2 edits");
      } else {
        flash_status(app, "Edit mode");
      }
      break;

    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
      if (ctrl) {
        app.body_px = std::min(48, app.body_px + 1);
        app.heading_px = std::min(64, app.heading_px + 2);
        app.fonts.set_sizes(app.body_px, app.heading_px);
        app.needs_layout = true;
      }
      break;

    case SDLK_MINUS:
    case SDLK_KP_MINUS:
      if (ctrl && shift) {
        // Insert horizontal rule after current block
        edit(app);
        if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                          xanatoo::NodeKind::HorizontalRule)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
        break;
      }
      if (ctrl) {
        app.body_px = std::max(10, app.body_px - 1);
        app.heading_px = std::max(14, app.heading_px - 2);
        app.fonts.set_sizes(app.body_px, app.heading_px);
        app.needs_layout = true;
      }
      break;

    case SDLK_q:
      if (ctrl && shift) {
        // Toggle block quote on the current text block
        edit(app);
        auto& node = d.get(p.sel.focus.node);
        if (node.kind == xanatoo::NodeKind::BlockQuote) {
          node.kind = xanatoo::NodeKind::Paragraph;
        } else if (node.kind == xanatoo::NodeKind::Paragraph ||
                   node.kind == xanatoo::NodeKind::Heading ||
                   node.kind == xanatoo::NodeKind::Header ||
                   node.kind == xanatoo::NodeKind::ListItem) {
          node.kind = xanatoo::NodeKind::BlockQuote;
          node.level = 0;
        }
        app.needs_layout = true;
        reset_blink(app);
      } else if (ctrl) {
        if (any_dirty(app) && !app.discard_armed) {
          app.discard_armed = true;
          flash_status(
              app, "Unsaved changes — Ctrl+S to save, or Ctrl+Q again to quit");
          std::cerr
              << "Unsaved changes — save first, or Ctrl+Q again to quit.\n";
          break;
        }
        app.discard_armed = false;
        app.running = false;
      }
      break;

    case SDLK_w:
      if (ctrl) {
        // Soft window → help → outline → document tab
        if (!app.soft_wins.empty()) {
          app.soft_wins.pop_back();
          flash_status(app, "Closed window");
        } else if (app.help_mode) {
          app.help_mode = false;
          app.help.dragging = false;
        } else if (app.outline_mode) {
          app.outline_mode = false;
          app.ol.dragging = false;
          app.ol.resize = 0;
        } else {
          // Close focused tab
          int tab_i = -1;
          for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti)
            if (app.tab_order[static_cast<size_t>(ti)] == p.buf) {
              tab_i = ti;
              break;
            }
          if (tab_i >= 0) {
            // Reuse CloseTab logic via command path is heavy; inline.
            int bi = app.tab_order[static_cast<size_t>(tab_i)];
            if (allow_discard_dirty(app, bi)) {
              app.tab_order.erase(app.tab_order.begin() + tab_i);
              if (app.tab_order.empty()) {
                DocBuffer b;
                b.doc =
                    xanatoo::parse_document(xanatoo::fallback_document(), {});
                app.buffers.push_back(std::move(b));
                int nbi = static_cast<int>(app.buffers.size()) - 1;
                app.tab_order.push_back(nbi);
                app.panes[0].buf = app.panes[1].buf = nbi;
              } else {
                int next = app.tab_order[static_cast<size_t>(std::min(
                    tab_i, static_cast<int>(app.tab_order.size()) - 1))];
                if (app.panes[0].buf == bi) app.panes[0].buf = next;
                if (app.panes[1].buf == bi) app.panes[1].buf = next;
              }
              app.needs_layout = true;
              update_title(app);
              flash_status(app, "Tab closed");
            }
          }
        }
      }
      break;

    case SDLK_f:
      if (ctrl && shift) {
        // View-local section fold (not stored in the document).
        toggle_fold_at_caret(app);
        break;
      }
      if (ctrl && alt) {
        // Body shows only the focused outline section.
        toggle_section_focus_mode(app);
        break;
      }
      if (ctrl) {
        app.search_mode = true;
        app.replace_mode = false;
        app.search_query.clear();
        std::cout << "Leap search: type to find, F3 next, Esc cancel\n";
        reset_blink(app);
      }
      break;

    case SDLK_h:
      if (ctrl && shift) {
        app.history_panel_open = !app.history_panel_open;
        break;
      }
      if (ctrl) {
        // Leap replace: find with current query (or enter search), then type
        // replacement
        app.search_mode = true;
        app.replace_mode = true;
        app.replace_text.clear();
        if (app.search_query.empty()) {
          std::cout << "Leap replace: type find text, Tab to edit replacement, "
                       "Enter replaces\n";
        } else {
          std::cout << "Leap replace: type replacement for \""
                    << app.search_query << "\"\n";
        }
        reset_blink(app);
      }
      break;

    case SDLK_F3:
    case SDLK_g:
      if (ev.key.keysym.sym == SDLK_F3 || ctrl) {
        if (app.search_query.empty() && !app.search_mode) break;
        app.search_mode = true;
        Pane& pp = focused_pane(app);
        size_t mlen = 0;
        const bool backward = shift || (ev.key.keysym.mod & KMOD_SHIFT) != 0;
        auto hit = backward
                       ? xanatoo::find_prev(doc_of(app, pp), app.search_query,
                                            pp.sel.focus, mlen)
                       : xanatoo::find_next(doc_of(app, pp), app.search_query,
                                            pp.sel.focus, mlen);
        if (hit) {
          pp.sel.anchor = *hit;
          pp.sel.focus = *hit;
          xanatoo::advance_caret(doc_of(app, pp), pp.sel.focus, mlen);
          scroll_caret_into_view(app, pp);
          reset_blink(app);
        }
      }
      break;

    case SDLK_TAB:
      if (app.replace_mode) {
        app.replace_focus = !app.replace_focus;
        break;
      }
      // Ctrl+Tab / Ctrl+Shift+Tab — cycle document tabs
      if (ctrl && !app.tab_order.empty()) {
        auto& p = focused_pane(app);
        int cur = 0;
        for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti)
          if (app.tab_order[static_cast<size_t>(ti)] == p.buf) {
            cur = ti;
            break;
          }
        int n = static_cast<int>(app.tab_order.size());
        cur = shift ? (cur - 1 + n) % n : (cur + 1) % n;
        p.buf = app.tab_order[static_cast<size_t>(cur)];
        xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        app.needs_layout = true;
        update_title(app);
        break;
      }
      // Tab / Shift+Tab: nest or promote list items; move table cells
      {
        auto& d = doc_of(app, focused_pane(app));
        auto& p = focused_pane(app);
        if (p.sel.focus.node != xanatoo::INVALID_NODE &&
            d.get(p.sel.focus.node).kind == xanatoo::NodeKind::ListItem) {
          edit(app);
          bool ok = shift ? xanatoo::outdent_list_item(d, p.sel.focus)
                          : xanatoo::indent_list_item(d, p.sel.focus);
          if (ok) {
            p.sel.anchor = p.sel.focus;
            app.needs_layout = true;
            reset_blink(app);
          }
          break;
        }
        if (p.sel.focus.node != xanatoo::INVALID_NODE &&
            d.get(p.sel.focus.node).kind == xanatoo::NodeKind::TableCell) {
          if (xanatoo::table_move_cell(d, p.sel.focus, !shift)) {
            p.sel.anchor = p.sel.focus;
            app.needs_layout = true;
            reset_blink(app);
          }
          break;
        }
      }
      if (app.split) {
        app.focus = 1 - app.focus;
        reset_blink(app);
      }
      break;

    case SDLK_BACKSLASH:
      if (ctrl) {
        app.split = !app.split;
        if (app.split) {
          // Second pane starts on the same buffer as the first.
          app.panes[1].buf = app.panes[0].buf;
          app.panes[1].sel = app.panes[0].sel;
          app.panes[1].scroll_y = app.panes[0].scroll_y;
        } else {
          app.focus = 0;
        }
        app.needs_layout = true;
      }
      break;

    case SDLK_BACKSPACE:
      if (app.path_in_window_mode) {
        xanatoo::utf8_pop_back(app.path_in_window_buf);
        break;
      }
      if (app.open_file_mode) {
        xanatoo::utf8_pop_back(app.open_file_buf);
        break;
      }
      if (app.url_bar_mode) {
        xanatoo::utf8_pop_back(app.url_bar_buf);
        break;
      }
      if (app.save_as_mode) {
        minibuf_sync_out(app);
        if (!app.save_as_path.empty()) {
          app.save_as_path = xanatoo::expand_user_path(app.save_as_path);
          xanatoo::utf8_pop_back(app.save_as_path);
        }
        break;
      }
      if (app.image_insert_mode) {
        xanatoo::utf8_pop_back(app.image_insert_path);
        break;
      }
      if (app.include_insert_mode) {
        xanatoo::utf8_pop_back(app.include_insert_src);
        break;
      }
      if (app.link_edit_mode) {
        xanatoo::utf8_pop_back(app.link_edit_href);
        break;
      }
      if (app.summary_edit_mode) {
        xanatoo::utf8_pop_back(app.summary_edit_text);
        break;
      }
      if (app.search_mode) {
        std::string& target = (app.replace_mode && app.replace_focus)
                                  ? app.replace_text
                                  : app.search_query;
        xanatoo::utf8_pop_back(target);
        reset_blink(app);
        break;
      }
      edit(app);
      if (!p.sel.empty()) {
        xanatoo::delete_selection(d, p.sel);
      } else {
        xanatoo::delete_backward(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
      }
      app.needs_layout = true;
      reset_blink(app);
      break;

    case SDLK_DELETE:
      edit(app);
      if (!p.sel.empty()) {
        xanatoo::delete_selection(d, p.sel);
      } else {
        xanatoo::delete_forward(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
      }
      app.needs_layout = true;
      reset_blink(app);
      break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (app.replace_mode && app.search_mode && !app.search_query.empty()) {
        Pane& pp = focused_pane(app);
        xanatoo::Document& dd = doc_of(app, pp);
        if (ctrl && shift) {
          // Replace all occurrences
          edit(app);
          size_t n =
              xanatoo::replace_all(dd, app.search_query, app.replace_text);
          app.needs_layout = true;
          std::cout << "Replace all: " << n << " substitution(s)\n";
          reset_blink(app);
          break;
        }
        if (!pp.sel.empty()) {
          edit(app);
          xanatoo::delete_selection(dd, pp.sel);
          if (!app.replace_text.empty()) {
            xanatoo::insert_text(dd, pp.sel.focus, app.replace_text);
            pp.sel.anchor = pp.sel.focus;
          }
          app.needs_layout = true;
        }
        // Find next
        size_t mlen = 0;
        if (auto hit =
                xanatoo::find_next(dd, app.search_query, pp.sel.focus, mlen)) {
          pp.sel.anchor = *hit;
          pp.sel.focus = *hit;
          xanatoo::advance_caret(dd, pp.sel.focus, mlen);
          scroll_caret_into_view(app, pp);
        }
        reset_blink(app);
        break;
      }
      if (app.link_edit_mode) {
        if (app.link_edit_node != xanatoo::INVALID_NODE) {
          auto& node = d.get(app.link_edit_node);
          // BiblioEntry: edit the entry's label (cite target key)
          if (node.kind == xanatoo::NodeKind::BiblioEntry &&
              app.link_edit_is_cite) {
            std::string key = app.link_edit_href;
            if (key.rfind("cite:", 0) == 0) key = key.substr(5);
            if (!key.empty() && key[0] == '#') key = key.substr(1);
            if (key.empty()) key = "key";
            node.label = key;
            mark_dirty(app);
            app.needs_layout = true;
            std::cout << "Bibliography key: " << key << "\n";
          } else if (app.link_edit_span < node.spans.size()) {
            auto& sp = node.spans[app.link_edit_span];
            if (app.link_edit_is_cite) {
              // Strip optional cite: / # prefix from the typed key
              std::string key = app.link_edit_href;
              if (key.rfind("cite:", 0) == 0) key = key.substr(5);
              if (!key.empty() && key[0] == '#') key = key.substr(1);
              sp.href = key;
              sp.kind = xanatoo::InlineKind::Cite;
              std::cout << "Citation key: " << key << "\n";
            } else {
              // Accept "href rel=glossary" or "href {rel=glossary}"
              std::string raw = app.link_edit_href;
              std::string rel;
              auto strip_rel = [&](std::string& s) {
                auto rpos = s.find("rel=");
                if (rpos == std::string::npos) return;
                size_t after = rpos + 4;
                while (after < s.size() &&
                       (s[after] == ' ' || s[after] == '\t'))
                  ++after;
                size_t end = after;
                while (end < s.size() && s[end] != ' ' && s[end] != '\t' &&
                       s[end] != '}')
                  ++end;
                rel = s.substr(after, end - after);
                // Remove the rel clause (and optional braces) from href
                size_t cut = rpos;
                while (cut > 0 && (s[cut - 1] == ' ' || s[cut - 1] == '\t' ||
                                   s[cut - 1] == '{'))
                  --cut;
                s = s.substr(0, cut);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                  s.pop_back();
              };
              strip_rel(raw);
              sp.href = raw;
              if (!rel.empty()) sp.rel = rel;
              sp.kind = xanatoo::InlineKind::Link;
              std::cout << "Link href: " << sp.href;
              if (!sp.rel.empty()) std::cout << " rel=" << sp.rel;
              std::cout << "\n";
            }
            mark_dirty(app);
            app.needs_layout = true;
          }
        }
        app.link_edit_mode = false;
        app.link_edit_is_cite = false;
        app.link_edit_href.clear();
        break;
      }
      if (app.summary_edit_mode) {
        if (app.summary_edit_node != xanatoo::INVALID_NODE) {
          d.get(app.summary_edit_node).summary = app.summary_edit_text;
          mark_dirty(app);
          app.needs_layout = true;
        }
        app.summary_edit_mode = false;
        app.summary_edit_text.clear();
        break;
      }
      if (app.path_in_window_mode) {
        minibuf_sync_out(app);
        if (!app.path_in_window_buf.empty()) {
          std::string path = xanatoo::expand_user_path(app.path_in_window_buf);
          if (!fs::exists(path)) {
            std::string found = find_repo_file(app, path.c_str());
            if (!found.empty()) path = found;
          }
          open_browse(app, path, path);
          minibuf_commit_path_hist(app);
        }
        app.path_in_window_mode = false;
        app.path_in_window_buf.clear();
        app.minibuf.clear();
        break;
      }
      if (app.url_bar_mode) {
        minibuf_sync_out(app);
        std::string target = app.url_bar_buf;
        app.url_bar_mode = false;
        app.url_bar_buf.clear();
        if (!target.empty()) minibuf_commit_url_hist(app);
        app.minibuf.clear();
        navigate_to_url(app, std::move(target));
        break;
      }
      if (app.open_file_mode) {
        minibuf_sync_out(app);
        if (!app.open_file_buf.empty()) {
          std::string path = xanatoo::expand_user_path(app.open_file_buf);
          if (!fs::exists(path)) {
            std::string found = find_repo_file(app, path.c_str());
            if (!found.empty()) path = found;
          }
          if (!fs::exists(path)) {
            flash_status(app, "Open: file not found");
            std::cerr << "Open: file not found: " << path << "\n";
          } else {
            int existing = find_buffer(app, path);
            if (existing >= 0) {
              // Already open — switch the focused pane to that tab.
              p.buf = existing;
              app.needs_layout = true;
              update_title(app);
              push_nav_history(app, path);
              flash_status(app, "Switched to " + path);
              minibuf_commit_path_hist(app);
              app.open_file_mode = false;
              app.open_file_buf.clear();
              app.minibuf.clear();
            } else if (!allow_discard_dirty(app, p.buf)) {
              flash_status(app,
                           "Unsaved changes — Enter again to discard and open");
              break;
            } else {
              app.buffers[static_cast<size_t>(p.buf)].doc = load_doc(app, path);
              path_of(app, p) = path;
              app.buffers[static_cast<size_t>(p.buf)].dirty = false;
              xanatoo::ensure_valid_caret(
                  app.buffers[static_cast<size_t>(p.buf)].doc, p.sel.focus);
              p.sel.anchor = p.sel.focus;
              p.scroll_y = 0.f;
              app.buffers[static_cast<size_t>(p.buf)].history.reset(
                  app.buffers[static_cast<size_t>(p.buf)].doc, p.sel);
              if (app.panes[0].buf == app.panes[1].buf) {
                app.panes[1 - app.focus].sel = p.sel;
                app.panes[1 - app.focus].scroll_y = 0.f;
              }
              // Ensure path is on the tab strip
              if (std::find(app.tab_order.begin(), app.tab_order.end(),
                            p.buf) == app.tab_order.end())
                app.tab_order.push_back(p.buf);
              app.needs_layout = true;
              update_title(app);
              push_nav_history(app, path);
              flash_status(app, "Opened " + path);
              std::cout << "Opened: " << path << "\n";
              minibuf_commit_path_hist(app);
              app.open_file_mode = false;
              app.open_file_buf.clear();
            }
          }
        } else {
          app.open_file_mode = false;
          app.open_file_buf.clear();
        }
        break;
      }
      if (app.save_as_mode) {
        minibuf_sync_out(app);
        if (!app.save_as_path.empty()) {
          app.save_as_path = xanatoo::expand_user_path(app.save_as_path);
          std::string text = xanatoo::serialize_document(d, app.save_as_path);
          std::ofstream out(app.save_as_path);
          if (!out) {
            std::cerr << "Failed to save: " << app.save_as_path << "\n";
          } else {
            out << text;
            path_of(app, p) = app.save_as_path;
            app.buffers[static_cast<size_t>(p.buf)].dirty = false;
            app.discard_armed = false;
            update_title(app);
            flash_status(app, "Saved as " + app.save_as_path);
            std::cout << "Saved as: " << app.save_as_path << "\n";
            minibuf_commit_path_hist(app);
          }
        }
        app.save_as_mode = false;
        app.save_as_path.clear();
        app.minibuf.clear();
        break;
      }
      if (app.image_insert_mode) {
        minibuf_sync_out(app);
        if (!app.image_insert_path.empty()) {
          app.image_insert_path =
              xanatoo::expand_user_path(app.image_insert_path);
          edit(app);
          bool ok = false;
          if (app.image_insert_as_figure) {
            ok = static_cast<bool>(
                xanatoo::insert_figure(d, p.sel.focus, app.image_insert_path));
            if (ok)
              std::cout << "Inserted figure: " << app.image_insert_path << "\n";
          } else {
            ok = static_cast<bool>(
                xanatoo::insert_image(d, p.sel.focus, app.image_insert_path));
            if (ok)
              std::cout << "Inserted image: " << app.image_insert_path << "\n";
          }
          if (ok) {
            p.sel.anchor = p.sel.focus;
            app.needs_layout = true;
            reset_blink(app);
            minibuf_commit_path_hist(app);
          }
        }
        app.image_insert_mode = false;
        app.image_insert_as_figure = false;
        app.image_insert_path.clear();
        app.minibuf.clear();
        break;
      }
      if (app.include_insert_mode) {
        minibuf_sync_out(app);
        if (!app.include_insert_src.empty()) {
          edit(app);
          if (xanatoo::insert_transclude(d, p.sel.focus,
                                         app.include_insert_src)) {
            p.sel.anchor = p.sel.focus;
            app.needs_layout = true;
            reset_blink(app);
            std::cout << "Inserted include: " << app.include_insert_src << "\n";
            minibuf_commit_path_hist(app);
          }
        }
        app.include_insert_mode = false;
        app.include_insert_src.clear();
        app.minibuf.clear();
        break;
      }
      // Ctrl+Enter: new empty paragraph after current block
      if (ctrl) {
        edit(app);
        if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                          xanatoo::NodeKind::Paragraph)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
        break;
      }
      edit(app);
      if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
      xanatoo::split_block(d, p.sel.focus);
      p.sel.anchor = p.sel.focus;
      app.needs_layout = true;
      reset_blink(app);
      break;

    case SDLK_LEFT:
      if (alt && !ctrl) {
        // Capsule history back
        if (app.nav_hist_pos > 0) {
          --app.nav_hist_pos;
          navigate_to_url(
              app, app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
              /*record_history=*/false);
        } else {
          flash_status(app, "No previous page");
        }
        break;
      }
      move_and_sel([&] {
        if (ctrl) {
          xanatoo::move_word_left(d, p.sel.focus);
        } else if (!xanatoo::move_left(d, p.sel.focus)) {
          xanatoo::move_to_prev_node(d, p.sel.focus);
        }
      });
      scroll_caret_into_view(app, p);
      reset_blink(app);
      break;

    case SDLK_RIGHT:
      if (alt && !ctrl) {
        // Capsule history forward
        if (app.nav_hist_pos + 1 < static_cast<int>(app.nav_history.size())) {
          ++app.nav_hist_pos;
          navigate_to_url(
              app, app.nav_history[static_cast<size_t>(app.nav_hist_pos)],
              /*record_history=*/false);
        } else {
          flash_status(app, "No next page");
        }
        break;
      }
      move_and_sel([&] {
        if (ctrl) {
          xanatoo::move_word_right(d, p.sel.focus);
        } else if (!xanatoo::move_right(d, p.sel.focus)) {
          xanatoo::move_to_next_node(d, p.sel.focus);
        }
      });
      scroll_caret_into_view(app, p);
      reset_blink(app);
      break;

    case SDLK_UP:
      if ((ev.key.keysym.mod & KMOD_ALT) != 0 &&
          (ev.key.keysym.mod & KMOD_SHIFT) != 0) {
        edit(app);
        if (xanatoo::move_section_up(d, p.sel.focus)) {
          app.needs_layout = true;
          reset_blink(app);
          flash_status(app, "Section moved up");
        }
      } else if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
        edit(app);
        if (xanatoo::move_block_up(d, p.sel.focus.node)) {
          app.needs_layout = true;
          reset_blink(app);
        }
      } else {
        move_and_sel([&] {
          if (!xanatoo::move_up(p.layout, p.sel.focus, p.preferred_x))
            xanatoo::move_to_prev_node(d, p.sel.focus);
        });
        scroll_caret_into_view(app, p);
        reset_blink(app);
      }
      break;

    case SDLK_DOWN:
      if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
        edit(app);
        if (xanatoo::move_block_down(d, p.sel.focus.node)) {
          app.needs_layout = true;
          reset_blink(app);
        }
      } else {
        move_and_sel([&] {
          if (!xanatoo::move_down(p.layout, p.sel.focus, p.preferred_x))
            xanatoo::move_to_next_node(d, p.sel.focus);
        });
        scroll_caret_into_view(app, p);
        reset_blink(app);
      }
      break;

    case SDLK_PAGEUP:
    case SDLK_PAGEDOWN: {
      const bool up = (ev.key.keysym.sym == SDLK_PAGEUP);
      // When the pointer is over soft chrome, page that surface instead
      // of the document (matches wheel routing).
      int mxk = 0, myk = 0;
      SDL_GetMouseState(&mxk, &myk);
      auto page_scroll = [&](float& scroll, float content_h, float view_h) {
        if (view_h < 1.f) view_h = 1.f;
        const float step = view_h * 0.85f;
        float max_s = std::max(0.f, content_h - view_h + 40.f);
        if (up)
          scroll = std::max(0.f, scroll - step);
        else
          scroll = std::min(max_s, scroll + step);
      };
      if (app.outline_mode && mxk >= app.ol.x && mxk < app.ol.x + app.ol.w &&
          myk >= app.ol.y && myk < app.ol.y + app.ol.h) {
        page_scroll(app.ol.scroll,
                    static_cast<float>(app.outline.size()) * 22.f,
                    static_cast<float>(app.ol.h - help_title_h - 8));
        break;
      }
      {
        bool soft_paged = false;
        for (int i = static_cast<int>(app.soft_wins.size()) - 1; i >= 0; --i) {
          SoftWindow& sw = app.soft_wins[static_cast<size_t>(i)];
          if (mxk >= sw.x && mxk < sw.x + sw.w && myk >= sw.y &&
              myk < sw.y + sw.h) {
            auto [cx, cy, cw, ch] = soft_client_rect(app, sw);
            page_scroll(sw.scroll, sw.layout.content_height,
                        static_cast<float>(std::max(1, ch)));
            soft_paged = true;
            break;
          }
        }
        if (soft_paged) break;
      }
      if (app.help_mode && mxk >= app.help.x && mxk < app.help.x + app.help.w &&
          myk >= app.help.y && myk < app.help.y + app.help.h) {
        auto [cx, cy, cw, ch] = help_client_rect(app);
        page_scroll(app.help.scroll, app.help_layout.content_height,
                    static_cast<float>(std::max(1, ch)));
        break;
      }
      const float view_h = static_cast<float>(app.win_h - status_bar_h) -
                           app.ui_top - app.ui_bottom;
      const float step = view_h * 0.85f;
      float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
      if (up)
        p.scroll_y = std::max(0.f, p.scroll_y - step);
      else
        p.scroll_y = std::min(max_s, p.scroll_y + step);
      ensure_virtual_layout(app, p);
      // Move caret to a glyph near the top of the new viewport
      move_and_sel([&] {
        if (p.layout.glyphs.empty()) return;
        float target_y = p.scroll_y + view_h * 0.15f;
        float best = 1e9f;
        xanatoo::Caret best_c = p.sel.focus;
        for (const auto& g : p.layout.glyphs) {
          float dy = std::abs(g.y - target_y);
          if (dy < best) {
            best = dy;
            best_c = {g.node, g.span_index, g.char_index};
          }
        }
        p.sel.focus = best_c;
      });
      reset_blink(app);
      break;
    }

    case SDLK_HOME:
      move_and_sel([&] {
        if (ctrl) {
          xanatoo::move_to_document_start(d, p.sel.focus);
        } else {
          // Line start via layout: find leftmost glyph on same baseline
          auto cpos = xanatoo::caret_screen_pos(
              p.layout, p.sel.focus, p.scroll_y,
              static_cast<float>(app.fonts.regular.line_height()));
          if (cpos.valid) {
            float best_x = 1e9f;
            xanatoo::Caret best = p.sel.focus;
            for (const auto& g : p.layout.glyphs) {
              if (g.node != p.sel.focus.node) continue;
              if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
              if (g.x < best_x) {
                best_x = g.x;
                best = {g.node, g.span_index, g.char_index};
              }
            }
            p.sel.focus = best;
          } else {
            p.sel.focus.span_index = 0;
            p.sel.focus.char_index = 0;
          }
        }
      });
      scroll_caret_into_view(app, p);
      reset_blink(app);
      break;

    case SDLK_END:
      move_and_sel([&] {
        if (ctrl) {
          xanatoo::move_to_document_end(d, p.sel.focus);
        } else {
          auto cpos = xanatoo::caret_screen_pos(
              p.layout, p.sel.focus, p.scroll_y,
              static_cast<float>(app.fonts.regular.line_height()));
          if (cpos.valid) {
            float best_x = -1e9f;
            xanatoo::Caret best = p.sel.focus;
            for (const auto& g : p.layout.glyphs) {
              if (g.node != p.sel.focus.node) continue;
              if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
              if (g.x >= best_x) {
                best_x = g.x;
                best = {g.node, g.span_index, g.char_index + 1};
              }
            }
            p.sel.focus = best;
          }
        }
      });
      scroll_caret_into_view(app, p);
      reset_blink(app);
      break;

    case SDLK_c:
      if (ctrl && shift && !p.sel.empty()) {
        // Ctrl+Shift+C — wrap selection as a citation; type the key
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
        app.link_edit_mode = true;
        app.link_edit_is_cite = true;
        app.search_mode = false;
        app.save_as_mode = false;
        app.link_edit_node = p.sel.focus.node;
        app.link_edit_span = p.sel.focus.span_index;
        app.link_edit_href = key;
        app.needs_layout = true;
        reset_blink(app);
      } else if (ctrl) {
        std::string text = xanatoo::selected_text(d, p.sel);
        if (!text.empty()) SDL_SetClipboardText(text.c_str());
      }
      break;

    case SDLK_x:
      if (ctrl && shift) {
        if (!p.sel.empty()) {
          edit(app);
          xanatoo::apply_style_to_selection(d, p.sel,
                                            xanatoo::InlineKind::Strike);
          app.needs_layout = true;
          reset_blink(app);
        }
      } else if (ctrl) {
        std::string text = xanatoo::selected_text(d, p.sel);
        if (!text.empty()) {
          SDL_SetClipboardText(text.c_str());
          edit(app);
          xanatoo::delete_selection(d, p.sel);
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_v:
      if (ctrl) {
        if (SDL_HasClipboardText()) {
          char* clip = SDL_GetClipboardText();
          if (clip && clip[0]) {
            // Status-bar prompts: paste into the active buffer, not the doc.
            auto append_clip = [&](std::string& buf) {
              // Single-line prompts: strip newlines.
              for (const char* s = clip; *s; ++s) {
                if (*s == '\n' || *s == '\r') continue;
                buf.push_back(*s);
              }
            };
            if (app.save_as_mode) {
              append_clip(app.save_as_path);
            } else if (app.path_in_window_mode) {
              append_clip(app.path_in_window_buf);
            } else if (app.open_file_mode) {
              append_clip(app.open_file_buf);
            } else if (app.url_bar_mode) {
              append_clip(app.url_bar_buf);
            } else if (app.image_insert_mode) {
              append_clip(app.image_insert_path);
            } else if (app.include_insert_mode) {
              append_clip(app.include_insert_src);
            } else if (app.link_edit_mode) {
              append_clip(app.link_edit_href);
            } else if (app.summary_edit_mode) {
              append_clip(app.summary_edit_text);
            } else if (app.search_mode) {
              if (app.replace_mode && app.replace_focus)
                append_clip(app.replace_text);
              else
                append_clip(app.search_query);
            } else {
              edit(app);
              if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
              xanatoo::paste_plain_text(d, p.sel.focus, clip);
              p.sel.anchor = p.sel.focus;
              app.needs_layout = true;
              reset_blink(app);
            }
          }
          if (clip) SDL_free(clip);
        }
      }
      break;

    case SDLK_z:
      if (ctrl) {
        if (app.buffers[static_cast<size_t>(p.buf)].history.undo(d, p.sel)) {
          if (app.panes[0].buf == app.panes[1].buf) {
            xanatoo::ensure_valid_caret(doc_of(app, app.panes[1 - app.focus]),
                                        app.panes[1 - app.focus].sel.focus);
            app.panes[1 - app.focus].sel.anchor =
                app.panes[1 - app.focus].sel.focus;
          }
          drop_height_hints_for_buf(app, p.buf);
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_y:
      if (ctrl) {
        if (app.buffers[static_cast<size_t>(p.buf)].history.redo(d, p.sel)) {
          if (app.panes[0].buf == app.panes[1].buf) {
            xanatoo::ensure_valid_caret(doc_of(app, app.panes[1 - app.focus]),
                                        app.panes[1 - app.focus].sel.focus);
            app.panes[1 - app.focus].sel.anchor =
                app.panes[1 - app.focus].sel.focus;
          }
          drop_height_hints_for_buf(app, p.buf);
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_n:
      if (ctrl) {
        if (!allow_discard_dirty(app, p.buf)) break;
        app.buffers[static_cast<size_t>(p.buf)].doc =
            xanatoo::parse_document(xanatoo::fallback_document(), {});
        path_of(app, p).clear();
        app.buffers[static_cast<size_t>(p.buf)].dirty = false;
        xanatoo::ensure_valid_caret(app.buffers[static_cast<size_t>(p.buf)].doc,
                                    p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.scroll_y = 0.f;
        p.layout = {};
        drop_height_hints_for_buf(app, p.buf);
        app.buffers[static_cast<size_t>(p.buf)].history.reset(
            app.buffers[static_cast<size_t>(p.buf)].doc, p.sel);
        if (app.panes[0].buf == app.panes[1].buf) {
          app.panes[1 - app.focus].sel = p.sel;
          app.panes[1 - app.focus].scroll_y = 0.f;
          app.panes[1 - app.focus].layout = {};
        }
        app.needs_layout = true;
        update_title(app);
        flash_status(app, "New document");
      }
      break;

    case SDLK_s:
      if (ctrl && shift) {
        app.save_as_mode = true;
        minibuf_begin(app, app.save_as_path);
        app.search_mode = false;
        app.save_as_path =
            path_of(app, p).empty() ? "untitled.md" : path_of(app, p);
        break;
      }
      if (ctrl) {
        std::string& spath = path_of(app, p);
        if (spath.empty()) {
          std::string candidate = "untitled.md";
          for (int n = 1; n < 1000; ++n) {
            if (!std::filesystem::exists(candidate)) break;
            candidate = "untitled-" + std::to_string(n) + ".md";
          }
          spath = candidate;
          std::cout << "Saving untitled buffer as: " << spath << "\n";
        }
        {
          std::string text = xanatoo::serialize_document(d, spath);
          std::ofstream out(spath);
          if (!out) {
            std::cerr << "Failed to save: " << spath << "\n";
          } else {
            out << text;
            std::cout << "Saved: " << spath << "\n";
            app.buffers[static_cast<size_t>(p.buf)].dirty = false;
            app.discard_armed = false;
            update_title(app);
            flash_status(app, "Saved " + spath);
          }
        }
      }
      break;

    case SDLK_o:
      if (ctrl && alt) {
        app.outline_docked = !app.outline_docked;
        if (app.outline_mode) {
          app.needs_layout = true;
          layout_panes(app);
        }
        flash_status(app, app.outline_docked ? "Outline docked (left)"
                                             : "Outline floating");
        break;
      }
      if (ctrl && shift) {
        app.outline_mode = !app.outline_mode;
        if (app.outline_mode) {
          refresh_outline(app);
          app.outline_sel = 0;
          app.ol.scroll = 0.f;
          // Prefer selecting the heading nearest the caret
          for (int i = 0; i < static_cast<int>(app.outline.size()); ++i) {
            if (app.outline[static_cast<size_t>(i)].node == p.sel.focus.node) {
              app.outline_sel = i;
              break;
            }
          }
        }
        app.needs_layout = true;
        layout_panes(app);
        break;
      }
      if (ctrl) {
        app.open_file_mode = true;
        minibuf_begin(app);
        app.open_file_buf.clear();
        app.path_in_window_mode = false;
        app.save_as_mode = false;
        app.search_mode = false;
      }
      break;

    case SDLK_r:
      if (ctrl) {
        // Reload focused document from disk or re-fetch gemini://
        std::string& rpath = path_of(app, p);
        if (rpath.empty()) {
          flash_status(app, "Reload: no path (Save As or Open URL first)");
          break;
        }
        if (app.buffers[static_cast<size_t>(p.buf)].dirty && !shift) {
          flash_status(app,
                       "Unsaved changes — Ctrl+Shift+R to discard and reload");
          break;
        }
        if (is_in_app_remote_url(rpath)) {
          navigate_to_url(app, rpath, /*record_history=*/false);
          break;
        }
        xanatoo::clear_external_doc_cache();
        app.buffers[static_cast<size_t>(p.buf)].doc = load_doc(app, rpath);
        app.buffers[static_cast<size_t>(p.buf)].dirty = false;
        app.buffers[static_cast<size_t>(p.buf)].history.reset(
            app.buffers[static_cast<size_t>(p.buf)].doc, p.sel);
        for (int pi = 0; pi < 2; ++pi) {
          if (app.panes[pi].buf != p.buf) continue;
          xanatoo::ensure_valid_caret(
              app.buffers[static_cast<size_t>(p.buf)].doc,
              app.panes[pi].sel.focus);
          app.panes[pi].sel.anchor = app.panes[pi].sel.focus;
          app.panes[pi].scroll_y = 0.f;
          app.panes[pi].layout = {};
        }
        drop_height_hints_for_buf(app, p.buf);
        update_title(app);
        app.needs_layout = true;
        reset_blink(app);
        flash_status(app, "Reloaded " + rpath);
        std::cout << "Reloaded: " << rpath << "\n";
      }
      break;

    case SDLK_d:
      if (ctrl && shift) {
        // Side-by-side version marks (needs two independent docs)
        if (!app.split) {
          app.split = true;
          app.needs_layout = true;
        }
        if (shared_doc(app) || app.buffers.size() < 2) {
          flash_status(app,
                       "Diff needs two files — Examples → Diff demo, or "
                       "xanatoo a.md b.md");
          std::cout << "Diff marks need two open files "
                       "(xanatoo a.md b.md).\n";
        } else {
          app.diff_mode = !app.diff_mode;
          refresh_diff(app);
          flash_status(app, app.diff_mode ? "Diff marks on" : "Diff marks off");
          std::cout << (app.diff_mode ? "Diff marks on\n" : "Diff marks off\n");
        }
        break;
      }
      if (ctrl) {
        edit(app);
        if (auto nid = xanatoo::duplicate_block(d, p.sel.focus.node)) {
          p.sel.focus.node = *nid;
          p.sel.focus.span_index = 0;
          p.sel.focus.char_index = 0;
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_l:
      if (ctrl && shift) {
        app.structure_mode = !app.structure_mode;
        flash_status(app, app.structure_mode ? "Structure highlight on"
                                             : "Structure highlight off");
        std::cout << (app.structure_mode ? "Structure highlight on\n"
                                         : "Structure highlight off\n");
      } else if (ctrl && !shift) {
        // Focus interactive toolbar URL bar
        app.search_mode = false;
        app.save_as_mode = false;
        app.open_file_mode = false;
        app.path_in_window_mode = false;
        app.url_bar_mode = false;
        app.ui.focus_url_bar();
      }
      break;

    case SDLK_8:
      // Ctrl+Shift+8 — toggle unordered list (common editor binding)
      if (ctrl && shift) {
        edit(app);
        if (xanatoo::toggle_list(d, p.sel.focus, false)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_7:
      // Ctrl+Shift+7 — toggle ordered list
      if (ctrl && shift) {
        edit(app);
        if (xanatoo::toggle_list(d, p.sel.focus, true)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_t:
      if (ctrl && shift) {
        // Cycle task item state
        edit(app);
        if (xanatoo::toggle_task_item(d, p.sel.focus)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
        }
      } else if (ctrl) {
        // New tab
        DocBuffer b;
        b.doc = xanatoo::parse_document(xanatoo::fallback_document(), {});
        int bi = static_cast<int>(app.buffers.size());
        app.buffers.push_back(std::move(b));
        app.tab_order.push_back(bi);
        p.buf = bi;
        p.sel = {};
        xanatoo::ensure_valid_caret(app.buffers[static_cast<size_t>(bi)].doc,
                                    p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.scroll_y = 0.f;
        app.buffers[static_cast<size_t>(bi)].history.reset(
            app.buffers[static_cast<size_t>(bi)].doc, p.sel);
        app.needs_layout = true;
        update_title(app);
        flash_status(app, "New tab");
      }
      break;

    case SDLK_p:
      if (ctrl && shift) {
        pop_out_focused(app);
      }
      break;

    case SDLK_b:
      if (ctrl && alt) {
        app.backlinks_mode = !app.backlinks_mode;
        if (app.backlinks_mode) {
          refresh_backlinks(app);
          app.backlinks_sel = 0;
          app.bl.scroll = 0.f;
        }
        break;
      }
      if (ctrl && shift) {
        std::string url = path_of(app, p);
        if (url.empty()) url = app.gemini_current_url;
        if (is_bookmarked(app, url))
          remove_bookmark(app, url);
        else
          add_bookmark(app, url);
        break;
      }
      if (ctrl && !p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel,
                                          xanatoo::InlineKind::Strong);
        app.needs_layout = true;
        reset_blink(app);
      }
      break;

    case SDLK_i:
      if (ctrl && shift) {
        // Insert image — prompt for path in status bar
        app.image_insert_mode = true;
        minibuf_begin(app);
        app.image_insert_path.clear();
        app.save_as_mode = false;
        app.search_mode = false;
        app.link_edit_mode = false;
        app.summary_edit_mode = false;
        break;
      }
      if (ctrl && !p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel,
                                          xanatoo::InlineKind::Emphasis);
        app.needs_layout = true;
        reset_blink(app);
      }
      break;

    case SDLK_BACKQUOTE:
      if (ctrl && !p.sel.empty()) {
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Code);
        app.needs_layout = true;
        reset_blink(app);
      }
      break;

    case SDLK_k:
      if (ctrl && !p.sel.empty()) {
        std::string text = xanatoo::selected_text(d, p.sel);
        std::string href = "#" + text;
        edit(app);
        xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Link,
                                          href);
        app.link_edit_mode = true;
        app.link_edit_is_cite = false;
        app.search_mode = false;
        app.save_as_mode = false;
        app.link_edit_node = p.sel.focus.node;
        app.link_edit_span = p.sel.focus.span_index;
        app.link_edit_href = href;
        app.needs_layout = true;
        reset_blink(app);
      }
      break;

    case SDLK_e:
      if (ctrl && shift) {
        // Edit summary of enclosing stretch
        xanatoo::NodeId sid = xanatoo::enclosing_stretch(d, p.sel.focus.node);
        if (sid != xanatoo::INVALID_NODE) {
          app.summary_edit_mode = true;
          app.summary_edit_node = sid;
          app.summary_edit_text = d.get(sid).summary;
          app.search_mode = false;
          app.save_as_mode = false;
          app.link_edit_mode = false;
        }
      } else if (ctrl) {
        edit(app);
        std::string summary = xanatoo::selected_text(d, p.sel);
        if (xanatoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_u:
      if (ctrl && shift) {
        // Browse / capsule: copy page URL; edit: unwrap stretch
        std::string page = path_of(app, p);
        if (page.empty()) page = app.gemini_current_url;
        if (app.browse_mode || xanatoo::is_gemini_url(page)) {
          if (page.empty()) {
            flash_status(app, "No page URL");
          } else {
            SDL_SetClipboardText(page.c_str());
            flash_status(app, "Copied " + page);
          }
        } else {
          edit(app);
          if (auto first = xanatoo::unwrap_stretch(d, p.sel.focus.node)) {
            p.sel.focus.node = *first;
            p.sel.focus.span_index = 0;
            p.sel.focus.char_index = 0;
            p.sel.anchor = p.sel.focus;
            app.needs_layout = true;
            reset_blink(app);
          }
        }
      }
      break;

    case SDLK_0:
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
      if (ctrl && shift) {
        // Ctrl+Shift+1/2 — wrap in Section / Article
        int n = static_cast<int>(ev.key.keysym.sym - SDLK_0);
        if (n == 1) {
          edit(app);
          if (xanatoo::wrap_in_section(d, p.sel.focus)) {
            app.needs_layout = true;
            reset_blink(app);
          }
        } else if (n == 2) {
          edit(app);
          if (xanatoo::wrap_in_article(d, p.sel.focus)) {
            app.needs_layout = true;
            reset_blink(app);
          }
        }
      } else if (ctrl && !shift) {
        int level = static_cast<int>(ev.key.keysym.sym - SDLK_0);
        edit(app);
        bool ok = false;
        if (level == 1)
          ok = xanatoo::make_header(d, p.sel.focus);
        else
          ok = xanatoo::set_heading_level(d, p.sel.focus, level);
        if (ok) {
          app.needs_layout = true;
          reset_blink(app);
        }
      }
      break;

    case SDLK_LEFTBRACKET:
      if (ctrl && alt) {
        edit(app);
        if (xanatoo::demote_section(d, p.sel.focus)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          flash_status(app, "Section demoted");
        } else {
          flash_status(app, "Cannot demote section");
        }
        break;
      }
      if (ctrl && shift) {
        // Collapse all stretch nodes
        edit(app);
        for (auto& node : d.nodes) {
          if (node.kind == xanatoo::NodeKind::Stretch) node.expanded = false;
        }
        app.needs_layout = true;
      }
      break;

    case SDLK_RIGHTBRACKET:
      if (ctrl && alt) {
        edit(app);
        if (xanatoo::promote_section(d, p.sel.focus)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          flash_status(app, "Section promoted");
        } else {
          flash_status(app, "Cannot promote section");
        }
        break;
      }
      if (ctrl && shift) {
        // Expand all stretch nodes
        edit(app);
        for (auto& node : d.nodes) {
          if (node.kind == xanatoo::NodeKind::Stretch) node.expanded = true;
        }
        app.needs_layout = true;
      }
      break;

    case SDLK_a:
      if (ctrl && shift) {
        xanatoo::select_all(d, p.sel);
      } else if (ctrl) {
        xanatoo::select_node(d, p.sel, p.sel.focus.node);
      }
      break;

    default:
      break;
  }
  return;
}

void handle_event(App& app, SDL_Event& ev) {
  app.ui.process_event(ev);
  // Let ImGui consume mouse/keyboard when interacting with menus
  const bool imgui_mouse = app.ui.want_capture_mouse();
  const bool imgui_keys = app.ui.want_capture_keyboard();
  switch (ev.type) {
    case SDL_QUIT:
      on_quit(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_WINDOWEVENT:
      on_windowevent(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_DROPFILE:
      on_dropfile(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_MOUSEWHEEL:
      on_mousewheel(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_MOUSEBUTTONDOWN:
      on_mousebuttondown(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_MOUSEBUTTONUP:
      on_mousebuttonup(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_MOUSEMOTION:
      on_mousemotion(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_TEXTINPUT:
      on_textinput(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_CONTROLLERDEVICEADDED:
      on_controllerdeviceadded(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      on_controllerdeviceremoved(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      on_controllerbuttondown(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_CONTROLLERBUTTONUP:
      on_controllerbuttonup(app, ev, imgui_mouse, imgui_keys);
      break;
    case SDL_KEYDOWN:
      on_keydown(app, ev, imgui_mouse, imgui_keys);
      break;
    default:
      break;
  }
}

}  // namespace xanatoo::app
