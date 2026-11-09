// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>

#include "xanatoo/app.hpp"
#include "xanatoo/reddit_credentials.hpp"
#include "xanatoo/platform.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/revstore.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/time.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/wikipedia.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

void apply_window_size(App& app, int nw, int nh) {
  if (nw < 64 || nh < 64 || (nw == app.win_w && nh == app.win_h)) return;
  if (nh < app.win_h) app.reveal_caret_after_layout = true;
  app.win_w = nw;
  app.win_h = nh;
  app.needs_layout = true;
  place_windows(app);
}

void sync_window_size(App& app) {
  int nw = 0, nh = 0;
  SDL_GetWindowSize(app.window, &nw, &nh);
  apply_window_size(app, nw, nh);
  // A docked window opened, closed or moved: the pane area changed.
  const Pane& last = app.split ? app.panes[1] : app.panes[0];
  if (app.panes[0].x != dock_column_w(app, Dock::Left) +
                              (app.panes[0].sb_left ? kSbW : 0) ||
      app.panes[0].y !=
          static_cast<int>(app.ui_top) + dock_band_h(app, Dock::Top) ||
      last.x + last.w != app.win_w - dock_column_w(app, Dock::Right))
    app.needs_layout = true;
}

void relayout_if_needed(App& app) {
  if (app.needs_layout && app.win_w >= 64 && app.win_h >= 64) {
    layout_panes(app);
    if (app.reveal_caret_after_layout) {
      app.reveal_caret_after_layout = false;
      scroll_caret_into_view(app, focused_pane(app));
    }
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

// Icons share the image cache and its asynchronous remote fetcher.
static SDL_Texture* address_icon(App& app, const std::string& address) {
  std::string base = xanatoo::parse_locator(address).base;
  std::string web = xanatoo::web_url_for(base);
  if (!web.empty()) base = web;
  if (xanatoo::starts_with_ci(base, "http://") ||
      xanatoo::starts_with_ci(base, "https://")) {
    const size_t start = base.find("://") + 3;
    const size_t end = base.find_first_of("/?#", start);
    if (end == start || start >= base.size()) return nullptr;
    base = base.substr(0, end) + "/favicon.ico";
  } else {
    // thumtoo can render the first page of a local PDF as a thumbnail.
    if (!xanatoo::ends_with(xanatoo::ascii_lower(base), ".pdf")) return nullptr;
  }
  const bool remote = xanatoo::is_remote_image(base);
  const auto& image = remote ? app.images.get(base) : app.images.thumb(base, {}, 128);
  return image.ok ? image.texture : nullptr;
}

static void build_history_menu(App& app, xanatoo::UiState& state) {
  // The toolbar's history lists must describe the very same trail used by
  // nav_step(), including the shared trail while split browsing.
  std::vector<std::string> urls;
  int pos = -1;
  if (split_browsing(app) && app.focus < kCentreViews) {
    int one_based = 0, total = 0;
    if (!split_history_position(app, one_based, total)) return;
    pos = one_based - 1;
    for (const SplitHistoryLevel& level : app.split_history.levels) {
      if (level.opened.empty()) {
        urls.push_back(level.page.url);
      } else {
        for (const SplitHistoryEntry& e : level.opened)
          urls.push_back(e.url);
      }
    }
  } else {
    const NavHistory& h = focused_pane(app).nav;
    pos = h.pos;
    for (const NavHistoryEntry& e : h.entries) urls.push_back(e.url);
  }
  if (pos < 0 || pos >= static_cast<int>(urls.size())) return;
  state.back_history = xanatoo::history_menu_items(urls, pos, -1);
  state.forward_history = xanatoo::history_menu_items(urls, pos, 1);
}

// Fill the chrome-facing snapshot of app state for this frame (also drives the
// software gamepad pointer, which synthesises mouse events).
static void build_ui_state(App& app, xanatoo::UiState& ui_state) {
  ui_state.dirty =
      buf(app, focused_pane(app).buf).dirty;
  const auto& focused_path = path_of(app, focused_pane(app));
  ui_state.can_publish =
      (focused_path.empty() || is_stored_document_ref(focused_path)) &&
      !buf(app, focused_pane(app).buf).placeholder;
  std::error_code source_ec;
  ui_state.can_make_local_copy =
      (is_stored_original_ref(focused_path) ||
       is_in_app_remote_url(focused_path) ||
       fs::is_regular_file(focused_path, source_ec)) &&
      !buf(app, focused_pane(app).buf).placeholder;
  ui_state.has_path = !path_of(app, focused_pane(app)).empty();
  ui_state.split = app.split;
  ui_state.loading = std::any_of(
      app.fetches.begin(), app.fetches.end(), [](const FetchJob& j) {
        return j.purpose == FetchPurpose::Navigate;
      });
  ui_state.outline = app.outline_mode;
  ui_state.section_focus = focused_pane(app).section_focus;
  ui_state.justify = app.view_settings.justify;
  ui_state.footnote_overlay = app.view_settings.footnote_overlay;
  ui_state.bookmark_marks = app.view_settings.bookmark_marks;
  ui_state.hover_preview = app.view_settings.hover_preview;
  ui_state.auto_updates = app.view_settings.auto_updates;
  ui_state.reader_default = app.view_settings.reader_mode;
  ui_state.reader = pane_reader(app, focused_pane(app));
  ui_state.paged = pane_paged(app, focused_pane(app));
  ui_state.paged_default = app.view_settings.paged;
  ui_state.page_columns = app.view_settings.page_columns;
  ui_state.has_chrome = focused_pane(app).chrome_parts > 0;
  ui_state.preferences_open = app.preferences_open;
  ui_state.backlinks = app.backlinks_mode;
  ui_state.sitemap = app.sitemap.shown;
  ui_state.page_panel = app.page.shown;
  ui_state.has_nav = page_has_navigation(app);
#if defined(XANATOO_WEB) && XANATOO_WEB
  {
    const std::string& path = path_of(app, focused_pane(app));
    const std::string web = xanatoo::is_sitemap_ref(path)
                                ? "https://" + xanatoo::sitemap_ref_host(path) + "/"
                                : xanatoo::url_host(path).empty()
                                      ? xanatoo::web_url_for(path) : path;
    ui_state.has_site = !xanatoo::url_host(web).empty();
  }
#endif
  ui_state.searchbar = app.search.open;
  ui_state.search_engine = search_engine_id(app);
  ui_state.search_engines.clear();
  for (const auto& e : app.search.engines)
    ui_state.search_engines.push_back({e.id, e.name, e.keyword, e.badge});
  ui_state.diff = app.diff_mode;
  ui_state.can_diff = !shared_doc(app) &&
                      app.panes[0].buf != app.panes[1].buf;
  ui_state.structure = app.structure_mode;
  ui_state.view_line_spacing = app.view_settings.line_spacing;
  ui_state.view_margin_pct = app.view_settings.margin_pct;
  ui_state.view_measure_chars = app.view_settings.measure_chars;
  ui_state.ui_scale = app.view_settings.ui_scale;
  ui_state.styles.clear();
  for (const auto& e : app.styles) ui_state.styles.emplace_back(e.name, e.title);
  ui_state.style = app.style.name;
  ui_state.browse = browsing(app);
  ui_state.chrome_hidden = app.chrome_hidden;
  ui_state.virtual_keyboard = app.virtual_keyboard;
  ui_state.help = app.help_mode;
  ui_state.selection = !focused_pane(app).sel.empty();
  ui_state.context_comment_native = app.context_comment_native;
  ui_state.context_comment_web = app.context_comment_web;
  ui_state.context_can_reply =
      app.context_reply_buffer != kNoBuffer &&
      app.context_reply_node != xanatoo::INVALID_NODE;
  ui_state.reply_open = app.reply.open;
  ui_state.reply_generation = app.reply.generation;
  ui_state.reply_author = app.reply.author;
  ui_state.reply_page = app.reply.page;
  ui_state.reply_label = app.reply.label;
  ui_state.reply_text = app.reply.text;
  if (app.reply.open) {
    auto [x, y, w, h] = float_client_rect(app, app.reply_win);
    ui_state.reply_x = x;
    ui_state.reply_y = y;
    ui_state.reply_w = w;
    ui_state.reply_h = h;
  }
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
                           : pane_address(app, focused_pane(app));
  ui_state.can_nav_back = can_nav_step(app, -1);
  build_history_menu(app, ui_state);
  {
    const std::string& path = path_of(app, focused_pane(app));
    ui_state.alternate_url = xanatoo::alternate_view_url(path);
    ui_state.native_view = !ui_state.alternate_url.empty() &&
                           xanatoo::native_url_for(path).empty();
    ui_state.up_url = page_up_url(app);
  }
  if (ui_state.services.empty()) {
    // Front pages of the in-app clients; greyed out when not built in.
    struct Entry {
      const char* label;
      const char* target;
      bool example;
      bool sep;
    };
    static const Entry kServices[] = {
        {"Wikipedia", "wikipedia:Main_Page", false, false},
        {"Wikipedia (Deutsch)", "wikipedia:de:Wikipedia:Hauptseite", false, false},
        {"Hacker News", "hn:", false, true},
        {"Reddit (r/popular)", "reddit:r/popular/", false, false},
        {"Lemmy (lemmy.world/c/technology)", "lemmy:lemmy.world/c/technology",
         false, false},
        {"Nostr (recent notes)", "nostr:recent", false, false},
        {"Gemini (geminiprotocol.net)", "gemini://geminiprotocol.net/", false,
         true},
        {"Standard Ebooks", "https://standardebooks.org/ebooks", false, true},
        {"Directory (links and feeds)", "directory.md", true, false},
        {"Books and long reads", "books.md", true, false},
        {"Feeds (RSS / Atom)", "feeds.md", true, false},
    };
    for (const Entry& e : kServices) {
      xanatoo::UiService s;
      s.label = e.label;
      s.target = e.target;
      s.example = e.example;
      s.separator_before = e.sep;
      s.tip = e.target;
      std::string why;
      if (!e.example && !remote_fetch_supported(e.target, &why)) {
        s.enabled = false;
        s.tip = why;
      }
      ui_state.services.push_back(std::move(s));
    }
  }
  if (const auto coll = current_collection(app); coll != xanatoo::INVALID_NODE) {
    ui_state.collection = true;
    auto it = app.collection_views.find(coll);
    const xanatoo::CollectionView v =
        it != app.collection_views.end()
            ? it->second
            : xanatoo::default_collection_view(
                  doc_of(app, focused_pane(app)).get(coll));
    ui_state.collection_layout = static_cast<int>(v.layout);
    ui_state.collection_sort = static_cast<int>(v.sort);
    ui_state.collection_thumb = v.thumb_px;
  }
  ui_state.can_nav_forward = can_nav_step(app, 1);
  ui_state.history_open = app.history_panel_open;
  ui_state.downloads_open = app.downloads_panel_open;
  ui_state.styles_open = app.styles_panel_open;
  ui_state.bookmarks_open = app.bookmarks_panel_open;
  ui_state.bookmarks = app.bookmarks;
  ui_state.annotations_panel_open = app.annotation_panel_open;
  ui_state.revisions_panel_open = app.revision_panel_open;
  {
    const DocBuffer* b = find_buf(app, focused_pane(app).buf);
    ui_state.can_show_revisions = b && !!find_stored_document(b->doc_key);
  }
  if (app.annotation_panel_open) {
    auto [x, y, w, h] = float_client_rect(app, app.annotation_win);
    ui_state.annotation_x = x;
    ui_state.annotation_y = y;
    ui_state.annotation_w = w;
    ui_state.annotation_h = h;
  }
  ui_state.annotation_note_created_gen = app.annotation_note_created_gen;
  ui_state.annotation_layers.clear();
  for (const auto& layer : app.annotation_layers)
    ui_state.annotation_layers.push_back({
        layer.id, layer.name, layer.owner,
        !app.hidden_annotation_layers.contains(layer.id),
        layer.id == app.active_annotation_layer, layer.annotations.size()});
  ui_state.annotations.clear();
  ui_state.annotation_history.clear();
  ui_state.annotation_history_layer.clear();
  if (app.annotation_panel_open) {
    Pane& pane = focused_pane(app);
    update_annotation_marks(app, pane);
    const DocBuffer* source = find_buf(app, pane.buf);
    std::string page = path_of(app, pane);
    if (page.empty()) page = app.gemini_current_url;
    const std::string key = source && !source->doc_key.empty()
                                ? source->doc_key : page;
    if (source && pane.source_buf != pane.buf && !key.empty()) {
      for (const auto& layer : app.annotation_layers) {
        if (app.hidden_annotation_layers.contains(layer.id)) continue;
        for (const auto& annotation : layer.annotations) {
          if (std::none_of(annotation.targets.begin(), annotation.targets.end(),
                           [&](const auto& target) {
                             return target.document_key == key ||
                                    target.document_key == page;
                           })) continue;
          const bool resolved = annotation_marks_fresh(pane) &&
              std::any_of(pane.annotation_marks.items.begin(),
                          pane.annotation_marks.items.end(),
                          [&](const AnnotationMark& mark) {
                            return mark.layer_id == layer.id &&
                                   mark.annotation_id == annotation.id;
                          });
          ui_state.annotations.push_back({
              annotation.id, layer.id, layer.owner, annotation.kind,
              annotation.body, resolved,
              annotation.id == app.selected_annotation_id});
        }
      }
    }
    const auto active = std::find_if(app.annotation_layers.begin(),
                                      app.annotation_layers.end(),
                                      [&](const auto& layer) {
      return layer.id == app.active_annotation_layer && layer.owner == "local";
    });
    if (active != app.annotation_layers.end()) {
      if (app.annotation_history_cached_layer != active->id ||
          app.annotation_history_cached_head != active->revision) {
        app.annotation_history_cached_layer = active->id;
        app.annotation_history_cached_head = active->revision;
        app.annotation_history_cache =
            xanatoo::annotation_layer_history(*active, 24);
      }
      ui_state.annotation_history_layer = active->id;
      for (const auto& revision : app.annotation_history_cache)
        ui_state.annotation_history.push_back({
            revision.id, revision.time, revision.annotation_count});
    }
  }
  ui_state.location_icon = address_icon(app, ui_state.doc_label);
  // Imported EPUBs are stored as content.md beside their extracted media.
  // Use the first image in that document as its cover when present.
  const std::string current = path_of(app, focused_pane(app));
  if (!ui_state.location_icon && current.find("/epub-import/") != std::string::npos) {
    const auto& doc = doc_of(app, focused_pane(app));
    for (const auto& node : doc.nodes) {
      if (node.kind != xanatoo::NodeKind::Image || node.src.empty()) continue;
      const auto& cover = app.images.thumb(node.src, fs::path(current).parent_path().string(), 128);
      if (cover.ok) ui_state.location_icon = cover.texture;
      break;
    }
  }
  ui_state.bookmark_icons.clear();
  for (const auto& bookmark : app.bookmarks)
    ui_state.bookmark_icons.push_back(
        xanatoo::parse_locator(bookmark.address).base == current && ui_state.location_icon
            ? ui_state.location_icon : address_icon(app, bookmark.address));
  {
    // With a selection the star bookmarks the selection.
    ui_state.current_bookmarked =
        focused_pane(app).sel.empty() &&
        is_bookmarked(app, pane_address(app, focused_pane(app)));
    ui_state.page_bookmarked =
        is_bookmarked(app, pane_address(app, focused_pane(app)));
    ui_state.page_bookmarks = bookmarks_in_page(app, focused_pane(app));
    ui_state.bookmark_menu = {};
    if (const xanatoo::Bookmark* b = find_bookmark(app, app.bookmark_menu)) {
      ui_state.bookmark_menu.address = b->address;
      ui_state.bookmark_menu.label = xanatoo::bookmark_label(*b);
      ui_state.bookmark_menu.cut = cut_address(b->address);
    }
  }
  ui_state.tab_labels.clear();
  ui_state.tab_ids.clear();
  ui_state.tab_icons.clear();
  ui_state.active_tab = 0;
  for (int ti = 0; ti < static_cast<int>(app.tab_order.size()); ++ti) {
    const BufferId bi = app.tab_order[static_cast<size_t>(ti)];
    ui_state.tab_labels.push_back(buffer_tab_label(app, bi));
    ui_state.tab_ids.push_back(bi);
    const DocBuffer* tab_buffer = find_buf(app, bi);
    ui_state.tab_icons.push_back(tab_buffer ? address_icon(app, tab_buffer->path) : nullptr);
    if (bi == app.panes[app.centre_focus].buf) ui_state.active_tab = ti;
  }
  ui_state.has_floating = !app.soft_wins.empty();
  // Right-stick software cursor (extra pointer; never touches OS mouse).
  bool stick_active = false;
  if (app.gamepad) {
    auto axis_e = [&](xanatoo::sdl::GamepadAxis a) -> float {
      Sint16 v = xanatoo::sdl::gamepad_axis(app.gamepad, a);
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
    float rx = axis_e(xanatoo::sdl::AxisRightX);
    float ry = axis_e(xanatoo::sdl::AxisRightY);
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
        xanatoo::sdl::push_mouse_motion(app.window, ix, iy, app.stick_lmb,
                                        app.stick_rmb);
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
  if (browsing(app) && cmd != C::None) {
    const C c = cmd;
    const bool allowed =
        c == C::Quit || c == C::OpenDocument || c == C::OpenUrlBar ||
        c == C::NavigateUrl || c == C::NavBack || c == C::NavForward ||
        c == C::NavJump ||
        c == C::GoHome || c == C::GoUp ||
        c == C::ToggleHistory || c == C::ToggleDownloads || c == C::ToggleBookmarks ||
        c == C::ToggleAnnotationsPanel || c == C::ToggleRevisionsPanel ||
        c == C::NewAnnotationLayer ||
        c == C::SelectAnnotationLayer || c == C::ToggleAnnotationLayer ||
        c == C::AddAnnotationHighlight || c == C::AddAnnotationNote ||
        c == C::SelectAnnotation || c == C::UpdateAnnotationNote ||
        c == C::RemoveAnnotation || c == C::RestoreAnnotationLayerVersion ||
        c == C::ToggleStylesPanel ||
        c == C::AddBookmark || c == C::BookmarkPage || c == C::RemoveBookmark ||
        c == C::BookmarkPosition || c == C::NewTab ||
        c == C::CloseTab || c == C::NextTab || c == C::PrevTab ||
        c == C::SelectTab || c == C::PopOut || c == C::PopIn || c == C::Save ||
        c == C::Publish || c == C::EditLocalCopy || c == C::ViewOriginal ||
        c == C::SaveAs || c == C::ExportSexpr || c == C::ExportXml ||
        c == C::ExportMarkdown ||
        c == C::Reload || c == C::ForceReload || c == C::OpenDrafts ||
        c == C::OpenStoredDocuments ||
        c == C::OpenAbout ||
        c == C::Copy ||
        c == C::CopyPageUrl || c == C::SelectAll || c == C::SelectBlock ||
        c == C::OpenReply || c == C::EditReply || c == C::SendReply ||
        c == C::Find || c == C::ToggleSplit || c == C::ToggleOutline ||
        c == C::ToggleBacklinks || c == C::ToggleSitemap || c == C::TogglePage ||
        c == C::ToggleSearchBar || c == C::FocusSearchBar || c == C::Search ||
        c == C::SelectSearchEngine ||
        c == C::SitemapPrev || c == C::SitemapNext ||
        c == C::OpenTranscludeOther ||
        c == C::ToggleDiff || c == C::ToggleStructure || c == C::ToggleBrowse ||
        c == C::ToggleChrome || c == C::ToggleNative ||
        c == C::ToggleReader || c == C::ToggleReaderDefault ||
        c == C::TogglePaged || c == C::TogglePagedDefault ||
        c == C::PageColumns ||
        c == C::CollectionView ||
        c == C::ToggleHelp || c == C::ToggleVirtualKeyboard ||
        c == C::OpenFormatDoc || c == C::OpenSemanticDoc ||
        c == C::OpenInWindow || c == C::OpenExample || c == C::LoadDiffDemo ||
        c == C::ZoomIn || c == C::ZoomOut || c == C::ViewLineSpacingTight ||
        c == C::ViewLineSpacingNormal || c == C::ViewLineSpacingRelaxed ||
        c == C::ViewLineSpacingLoose || c == C::ViewMargins ||
        c == C::ViewMeasure ||
        c == C::ToggleSectionFocus || c == C::ToggleJustify ||
        c == C::ToggleHoverPreview || c == C::ToggleAutoUpdates || c == C::TogglePreferences ||
        c == C::SaveRedditCredentials || c == C::ClearRedditCredentials ||
        c == C::SetUiScale ||
        c == C::SelectStyle || c == C::ReloadStyles;
    if (!allowed) {
      flash_status(app, "Browse mode — F2 to edit");
      cmd = C::None;
    }
  }

  switch (cmd) {
    case C::OpenReply: {
      const xanatoo::NodeId id = app.context_reply_node;
      if (app.context_reply_buffer != p.buf ||
          id == xanatoo::INVALID_NODE || id >= d.next_id ||
          d.get(id).kind != xanatoo::NodeKind::Message) break;
      const auto& message = d.get(id);
      app.reply.open = true;
      app.reply.buffer = p.buf;
      app.reply.node = id;
      app.reply.page = path_of(app, p);
      app.reply.label = message.label;
      app.reply.author = message.src;
      app.reply.text.clear();
      app.reply.anchor_y = app.context_reply_y;
      ++app.reply.generation;
      app.reply_win.scroll = 0.f;
      snap_soft_geom(app, app.reply_win.x, app.reply_win.y,
                     app.reply_win.w, app.reply_win.h);
      place_windows(app);
      break;
    }
    case C::EditReply:
      if (app.reply.open) app.reply.text = arg;
      break;
    case C::SendReply:
      if (app.reply.open)
        flash_status(app, app.reply.text.empty()
                              ? "Enter a reply before sending"
                              : "Reply mockup: not sent (no account connected)");
      break;
    case C::Quit:
      if (stash_all_or_confirm(app)) app.running = false;
      break;
    case C::NewDocument: {
      if (!stash_or_confirm(app, p.buf)) break;
      buf(app, p.buf).doc =
          xanatoo::parse_document(xanatoo::fallback_document(), {});
      path_of(app, p).clear();
      buf(app, p.buf).dirty = false;
      attach_draft(app, p.buf);  // forget the old file's doc key
      const bool saved_locally = save_stored_buffer(app, p.buf);
      xanatoo::ensure_valid_caret(buf(app, p.buf).doc,
                                  p.sel.focus);
      p.sel.anchor = p.sel.focus;
      p.scroll_y = 0.f;
      p.layout = {};
      drop_height_hints_for_buf(app, p.buf);
      buf(app, p.buf).history.reset(
          buf(app, p.buf).doc, p.sel);
      refresh_views_of_buf(app, p.buf, app.focus, true);
      app.needs_layout = true;
      update_title(app);
      flash_status(app, saved_locally ? "New local document" :
                  "Local database unavailable; document remains in memory");
      break;
    }
    case C::OpenDocument:
      open_prompt(app, PromptKind::OpenFile);
      break;
    case C::OpenUrlBar:
      close_prompt(app);
      app.ui.focus_url_bar();
      break;
    case C::CollectionView:
      collection_command(app, arg);
      break;
    case C::ToggleNative: {
      // Explicit only: web pages never switch to the native form on their
      // own, and native refs are never rerouted from https:// URLs.
      const std::string other = xanatoo::alternate_view_url(path_of(app, p));
      if (other.empty())
        flash_status(app, "No native view for this page");
      else
        navigate_to_url(app, other);
      break;
    }
    case C::ToggleReader:
      if (p.chrome_parts == 0 && !pane_reader(app, p))
        flash_status(app, "Reader mode: this page has no page chrome to hide");
      else
        set_reader(app, p, !pane_reader(app, p));
      break;
    case C::TogglePaged:
      set_paged(app, p, !pane_paged(app, p));
      break;
    case C::TogglePagedDefault: {
      keep_views(app);
      app.view_settings.paged = !app.view_settings.paged;
      xanatoo::save_view_settings(app.view_settings);
      for (Pane& v : app.panes)
        if (!v.paged) v.height_cache.clear();
      app.needs_layout = true;
      flash_status(app, app.view_settings.paged
                            ? "Paged reading is the default"
                            : "Scrolling is the default");
      break;
    }
    case C::PageColumns: {
      keep_views(app);
      app.view_settings.page_columns = std::clamp(std::atoi(arg.c_str()), 0, 3);
      xanatoo::save_view_settings(app.view_settings);
      app.needs_layout = true;
      break;
    }
    case C::NavigateUrl:
      xanatoo::platform_release_keyboard(app.window);  // URL typed; page is read-only
      if (!arg.empty()) {
        const std::string kw = search_from_location(app, arg);
        navigate_to_url(app, kw.empty() ? arg : kw);
      }
      break;
    case C::StopLoading:
      if (cancel_fetches(app)) flash_status(app, "Stopped loading");
      break;
    case C::NavBack:
      if (!nav_step(app, -1)) flash_status(app, "No previous page");
      break;
    case C::NavForward:
      if (!nav_step(app, 1)) flash_status(app, "No next page");
      break;
    case C::NavJump: {
      const int steps = std::clamp(std::atoi(arg.c_str()), -kMaxNavHistory,
                                   kMaxNavHistory);
      if (steps == 0) break;
      const bool ok = split_browsing(app) && app.focus < kCentreViews
          ? split_history_jump(app, steps) : nav_step(app, steps);
      if (!ok) flash_status(app, "History destination no longer available");
      break;
    }
    case C::GoHome: {
      const std::string home = home_page(app);
      if (home.empty())
        flash_status(app, "Home page not found (welcome.md)");
      else
        navigate_to_url(app, home);
      break;
    }
    case C::GoUp: {
      const std::string up = page_up_url(app);
      if (up.empty())
        flash_status(app, "Already at the top");
      else
        navigate_to_url(app, up);
      break;
    }
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
              app.panes[app.centre_focus].buf) {
            tab_i = ti;
            break;
          }
        }
      }
      if (tab_i < 0 || app.tab_order.empty()) break;
      const BufferId bi = app.tab_order[static_cast<size_t>(tab_i)];
      if (!stash_or_confirm(app, bi)) break;
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
      focus_view(app, app.centre_focus);  // tabs are the centre's buffers
      Pane& c = focused_pane(app);
      if (c.buf == bi) break;  // already showing
      c.buf = bi;
      // Fresh layout + caret for the newly focused buffer.
      xanatoo::ensure_valid_caret(doc_of(app, c), c.sel.focus);
      c.sel.anchor = c.sel.focus;
      c.layout = {};
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
              app.panes[app.centre_focus].buf) {
            cur = ti;
            break;
          }
        const int ntabs = static_cast<int>(app.tab_order.size());
        if (cmd == C::NextTab)
          cur = (cur + 1) % ntabs;
        else
          cur = (cur - 1 + ntabs) % ntabs;
        focus_view(app, app.centre_focus);
        Pane& c = focused_pane(app);
        c.buf = app.tab_order[static_cast<size_t>(cur)];
        xanatoo::ensure_valid_caret(doc_of(app, c), c.sel.focus);
        c.sel.anchor = c.sel.focus;
        c.layout = {};
        app.needs_layout = true;
        update_title(app);
      }
      break;
    case C::ReorderTabs: {
      std::vector<BufferId> order;
      size_t at = 0;
      while (at <= arg.size()) {
        size_t comma = arg.find(',', at);
        if (comma == std::string::npos) comma = arg.size();
        try {
          order.push_back(static_cast<BufferId>(
              std::stoul(arg.substr(at, comma - at))));
        } catch (...) {
          order.clear();
          break;
        }
        at = comma + 1;
      }
      auto sorted = [](std::vector<BufferId> v) {
        std::sort(v.begin(), v.end());
        return v;
      };
      if (!order.empty() && sorted(order) == sorted(app.tab_order))
        app.tab_order = order;
      break;
    }
    case C::TearTab: {
      unsigned long id = 0;
      int mx = 0, my = 0;
      if (std::sscanf(arg.c_str(), "%lu,%d,%d", &id, &mx, &my) != 3) break;
      app.dock_preview = Dock::None;
      tear_tab(app, static_cast<BufferId>(id), mx, my);
      break;
    }
    case C::PopOut:
      pop_out_focused(app);
      break;
    case C::PopIn:
      // The focused window, else the top one.
      if (FloatWindow* w = window_of_view(app, app.focus))
        pop_in_soft(app, static_cast<size_t>(w - app.soft_wins.data()));
      else if (!app.soft_wins.empty())
        pop_in_soft(app, app.soft_wins.size() - 1);
      else
        flash_status(app, "No floating window to dock");
      break;
    case C::OpenDrafts:
      navigate_to_url(app, kDraftsPage);
      break;
    case C::OpenStoredDocuments:
      navigate_to_url(app, kDocumentsPage);
      break;
    case C::OpenAbout:
      navigate_to_url(app, kAboutPage);
      break;
    case C::ToggleHistory:
      app.history_panel_open = !app.history_panel_open;
      if (app.history_panel_open) {
        app.key_focus = KeyFocus::History;
      } else if (app.key_focus == KeyFocus::History) {
        app.key_focus = KeyFocus::Document;
      }
      break;
    case C::ToggleDownloads:
      app.downloads_panel_open = !app.downloads_panel_open;
      break;
    case C::ToggleStylesPanel:
      app.styles_panel_open = !app.styles_panel_open;
      if (app.styles_panel_open) {
        app.key_focus = KeyFocus::Styles;
        for (int i = 0; i < static_cast<int>(app.styles.size()); ++i)
          if (app.styles[static_cast<size_t>(i)].name == app.style.name) {
            app.styles_sel = i;
            break;
          }
      } else if (app.key_focus == KeyFocus::Styles) {
        app.key_focus = KeyFocus::Document;
      }
      break;
    case C::ToggleBookmarks:
      app.bookmarks_panel_open = !app.bookmarks_panel_open;
      break;
    case C::ToggleAnnotationsPanel:
      app.annotation_panel_open = !app.annotation_panel_open;
      break;
    case C::ToggleRevisionsPanel:
      if (app.revision_panel_open) {
        app.revision_panel_open = false;
      } else {
        const DocBuffer* b = find_buf(app, p.buf);
        if (!b || !find_stored_document(b->doc_key)) {
          flash_status(app, "Open a local database document to view its revisions");
          break;
        }
        app.revision_document_key = b->doc_key;
        app.revision_selected.clear();
        app.revision_working.clear();
        app.revision_published.clear();
        app.revision_win.scroll = 0.f;
        app.revision_panel_open = true;
        refresh_revision_panel(app);
      }
      break;
    case C::ToggleAnnotationLayer:
      if (app.hidden_annotation_layers.contains(arg))
        app.hidden_annotation_layers.erase(arg);
      else
        app.hidden_annotation_layers.insert(arg);
      xanatoo::save_annotation_layer_visibility(
          arg, !app.hidden_annotation_layers.contains(arg));
      ++app.annotation_layers_gen;
      break;
    case C::SelectAnnotationLayer:
      for (const auto& layer : app.annotation_layers)
        if (layer.id == arg && layer.owner == "local")
          app.active_annotation_layer = arg;
      break;
    case C::NewAnnotationLayer: {
      const auto layer = xanatoo::create_annotation_layer(
          "local", "My annotations " + std::to_string(app.annotation_layers.size() + 1));
      if (!layer) {
        flash_status(app, "Could not create annotation layer (revision store unavailable)");
        break;
      }
      app.active_annotation_layer = layer->id;
      app.annotation_layers.push_back(*layer);
      ++app.annotation_layers_gen;
      flash_status(app, "Created a local annotation layer");
      break;
    }
    case C::AddAnnotationHighlight:
    case C::AddAnnotationNote: {
      const bool is_note = cmd == C::AddAnnotationNote;
      if (is_note && arg.empty()) {
        flash_status(app, "Write a note before attaching it");
        break;
      }
      if (p.sel.empty() || p.source_buf == p.buf) {
        flash_status(app, "Select document text to annotate");
        break;
      }
      const std::string address = bookmark_for_view(app, p, false).address;
      if (address.empty()) break;
      const DocBuffer& source = buf(app, p.buf);
      const std::string key = !source.doc_key.empty() ? source.doc_key
                              : !source.path.empty() ? source.path
                                                     : std::string();
      if (key.empty()) {
        flash_status(app, "Document has no persistent identity yet");
        break;
      }
      auto layer = std::find_if(app.annotation_layers.begin(),
                                app.annotation_layers.end(),
                                [&](const auto& l) {
                                  return l.id == app.active_annotation_layer &&
                                         l.owner == "local";
                                });
      if (layer == app.annotation_layers.end()) {
        auto created = xanatoo::create_annotation_layer("local", "My annotations");
        if (!created) {
          flash_status(app, "Cannot save annotations (revision store unavailable)");
          break;
        }
        app.active_annotation_layer = created->id;
        app.annotation_layers.push_back(*created);
        layer = std::prev(app.annotation_layers.end());
      }
      xanatoo::Annotation mark;
      mark.id = xanatoo::new_annotation_id();
      mark.kind = is_note ? "note" : "highlight";
      if (is_note) mark.body = arg;
      mark.targets.push_back({
          key, xanatoo::content_id(xanatoo::serialize_sexpr(d)), address});
      const std::string id = mark.id;
      if (!xanatoo::add_annotation(*layer, std::move(mark))) {
        flash_status(app, "Could not save annotation; no document changes made");
        break;
      }
      app.selected_annotation_id = id;
      if (is_note) ++app.annotation_note_created_gen;
      if (app.hidden_annotation_layers.erase(layer->id))
        xanatoo::save_annotation_layer_visibility(layer->id, true);
      ++app.annotation_layers_gen;
      flash_status(app, is_note ? "Attached note to document passage"
                                : "Highlighted selection in local annotation layer");
      break;
    }
    case C::SelectAnnotation: {
      app.selected_annotation_id = arg;
      select_annotation(app, app.focus, arg);
      break;
    }
    case C::UpdateAnnotationNote:
    case C::RemoveAnnotation: {
      const std::string id = cmd == C::RemoveAnnotation
                                 ? arg : app.selected_annotation_id;
      auto layer = std::find_if(app.annotation_layers.begin(),
                                app.annotation_layers.end(),
                                [&](const auto& candidate) {
        return candidate.owner == "local" &&
            std::any_of(candidate.annotations.begin(),
                        candidate.annotations.end(),
                        [&](const auto& annotation) {
                          return annotation.id == id;
                        });
      });
      if (layer == app.annotation_layers.end()) {
        flash_status(app, "Annotation is not in an editable local layer");
        break;
      }
      const bool removed = cmd == C::RemoveAnnotation;
      const bool saved = removed
          ? xanatoo::remove_annotation(*layer, id)
          : xanatoo::update_annotation_body(*layer, id, arg);
      if (!saved) {
        flash_status(app, "Could not save annotation revision");
        break;
      }
      if (removed && app.selected_annotation_id == id)
        app.selected_annotation_id.clear();
      ++app.annotation_layers_gen;
      flash_status(app, removed ? "Annotation removed (history preserved)"
                                : "Annotation note saved to layer history");
      break;
    }
    case C::RestoreAnnotationLayerVersion: {
      const size_t sep = arg.find('\n');
      if (sep == std::string::npos) break;
      const std::string layer_id = arg.substr(0, sep);
      const std::string revision_id = arg.substr(sep + 1);
      auto layer = std::find_if(app.annotation_layers.begin(),
                                app.annotation_layers.end(),
                                [&](const auto& candidate) {
        return candidate.id == layer_id && candidate.owner == "local";
      });
      if (layer == app.annotation_layers.end() || revision_id.empty() ||
          !xanatoo::restore_annotation_layer_revision(*layer, revision_id)) {
        flash_status(app, "Could not restore layer revision (possibly changed elsewhere)");
        break;
      }
      const bool selected_exists = std::any_of(
          layer->annotations.begin(), layer->annotations.end(),
          [&](const auto& a) { return a.id == app.selected_annotation_id; });
      if (!selected_exists) app.selected_annotation_id.clear();
      ++app.annotation_layers_gen;
      flash_status(app, "Restored annotation layer as a new revision");
      break;
    }
    case C::AddBookmark:
      add_bookmark(app, bookmark_for_view(app, p, false));
      break;
    case C::BookmarkPage: {
      // URL chrome refers to the page, never the document text selection.
      xanatoo::Bookmark bm = bookmark_for_view(app, p, false);
      bm.address = pane_address(app, p);
      bm.title.clear();
      bm.quote.clear();
      add_bookmark(app, std::move(bm));
      break;
    }
    case C::BookmarkPosition:
      add_bookmark(app, bookmark_for_view(app, p, true));
      break;
    case C::RemoveBookmark: {
      std::string url = arg;
      if (url.empty()) url = pane_address(app, p);
      remove_bookmark(app, url);
      break;
    }
    case C::Save: {
      if (p.source_buf == p.buf && p.source_doc) {
        flash_status(app, "Source view is read-only — return to the document to save");
        break;
      }
      const std::string spath = path_of(app, p);
      if (app.invalid_xml_paths.contains(spath)) {
        flash_status(app, "Invalid XML is read-only — fix the file and reload");
      } else if (make_stored_copy(app, p.buf) &&
                 save_stored_buffer(app, focused_pane(app).buf)) {
        app.discard_armed = false;
        flash_status(app, "Saved working document in local database");
      } else {
        flash_status(app, "Could not save to local database; original was not changed");
      }
      break;
    }
    case C::EditLocalCopy:
      if (make_stored_copy(app, p.buf))
        flash_status(app, "Editing local database copy; original preserved");
      else
        flash_status(app, "Could not make local copy; original preserved");
      break;
    case C::ViewOriginal: {
      const std::string path = path_of(app, p);
      if (is_stored_document_ref(path) &&
          find_document_origin(path.substr(3)) &&
          navigate_pane(app, app.focus, "db-original:" + path.substr(3)))
        flash_status(app, "Viewing immutable original; Back returns to working copy");
      else
        flash_status(app, "This document has no imported original");
      break;
    }
    case C::Publish:
      if (path_of(app, p).empty() ||
          is_stored_document_ref(path_of(app, p))) {
        if (publish_stored_buffer(app, p.buf))
          flash_status(app, "Published local snapshot (no remote destination configured)");
        else
          flash_status(app, "Could not publish working document to local database");
      } else {
        flash_status(app, "Only database-native documents can be published locally");
      }
      break;
    case C::SaveAs: {
      const std::string path = path_of(app, p);
      std::string suggested = "untitled.md";
      if (is_in_app_remote_url(path)) {
        suggested = "page.md";
      } else if (!path.empty() && !is_stored_document_ref(path) &&
                 !is_stored_original_ref(path)) {
        const fs::path fp(path);
        suggested = (fp.parent_path() /
                     (fp.stem().string() + "-export.md")).string();
      }
      open_prompt(app, PromptKind::SaveAs, std::move(suggested));
      break;
    }
    case C::ExportSexpr:
    case C::ExportXml:
    case C::ExportMarkdown: {
      const bool as_sexp = (cmd == C::ExportSexpr);
      const bool as_xml = (cmd == C::ExportXml);
      const char* ext = as_sexp ? ".sexp" : as_xml ? ".xml" : ".md";
      std::string cur = path_of(app, p);
      std::string target;
      if (cur.empty() || is_stored_document_ref(cur) ||
          is_stored_original_ref(cur) || is_in_app_remote_url(cur)) {
        target = std::string("untitled") + ext;
      } else {
        std::filesystem::path fp(cur);
        target = fp.stem().string() + "-export" + ext;
        if (fp.has_parent_path() && !fp.parent_path().empty())
          target = (fp.parent_path() / (fp.stem().string() + "-export" + ext)).string();
      }
      open_prompt(app, PromptKind::SaveAs, std::move(target));
      break;
    }
    case C::Reload:
    case C::ForceReload:
    case C::RevertToSaved: {
      std::string& rpath = path_of(app, p);
      if (rpath.empty()) break;
      if (rpath == kDraftsPage) {
        open_drafts_page(app);
        break;
      }
      if (rpath == kDocumentsPage) {
        open_stored_documents_page(app);
        break;
      }
      if (is_stored_document_ref(rpath)) {
        const std::string stable_ref = rpath;
        if (!open_stored_document(app, app.focus, stable_ref))
          flash_status(app, "Could not reload local document");
        else
          flash_status(app, "Reloaded local working revision");
        break;
      }
      if (is_stored_original_ref(rpath)) {
        const std::string stable_ref = rpath;
        if (!open_stored_original(app, app.focus, stable_ref))
          flash_status(app, "Could not reload immutable original");
        break;
      }
      if (rpath == kAboutPage) {
        open_about_page(app);
        break;
      }
      if (cmd == C::RevertToSaved) {
        revert_buffer(app, p.buf);
      } else if (buf(app, p.buf).dirty && !is_foreign_path(rpath)) {
        // Reloading would just restore the draft again.
        stash_draft(app, p.buf);
        flash_status(app, "Unsaved changes are kept as a draft — "
                          "File → Revert to Saved drops them");
        break;
      }
      if (is_in_app_remote_url(rpath)) {
        refetch_page(app, app.focus);
        break;
      }
      xanatoo::clear_external_doc_cache();
      buf(app, p.buf).doc = load_doc(app, rpath);
      buf(app, p.buf).dirty = false;
      attach_draft(app, p.buf);
      buf(app, p.buf).history.reset(
          buf(app, p.buf).doc, p.sel);
      refresh_views_of_buf(app, p.buf, -1, true);
      drop_height_hints_for_buf(app, p.buf);
      reset_blink(app);
      update_title(app);
      app.needs_layout = true;
      flash_status(app, (cmd == C::RevertToSaved ? "Reverted to " : "Reloaded ") +
                            rpath);
      break;
    }
    case C::Undo:
      if (buf(app, p.buf).history.undo(d, p.sel)) {
        xanatoo::ensure_valid_caret(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        refresh_views_of_buf(app, p.buf, app.focus, false);
        reset_blink(app);
        drop_height_hints_for_buf(app, p.buf);
        app.needs_layout = true;
      }
      break;
    case C::Redo:
      if (buf(app, p.buf).history.redo(d, p.sel)) {
        xanatoo::ensure_valid_caret(d, p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        refresh_views_of_buf(app, p.buf, app.focus, false);
        reset_blink(app);
        drop_height_hints_for_buf(app, p.buf);
        app.needs_layout = true;
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
    case C::SelectBookmark:
      if (!arg.empty()) select_bookmark(app, app.focus, arg);
      break;
    case C::CopyText:
      if (!arg.empty()) {
        SDL_SetClipboardText(arg.c_str());
        flash_status(app, "Copied " + arg);
      }
      break;
    case C::CopyPageUrl: {
      const std::string url = pane_address(app, p);
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
        app.key_focus = KeyFocus::Outline;
        refresh_outline(app);
        app.outline_sel = 0;
        app.ol.scroll = 0.f;
      }
      app.needs_layout = true;
      layout_panes(app);
      break;
    case C::ToggleSitemap:
      toggle_sitemap(app);
      break;
    case C::TogglePage:
      toggle_page_panel(app);
      break;
    case C::ToggleSearchBar:
      toggle_search_bar(app);
      break;
    case C::FocusSearchBar:
      show_search_bar(app);
      break;
    case C::Search:
      run_search(app, arg);
      break;
    case C::SelectSearchEngine:
      select_search_engine(app, arg);
      break;
    case C::SitemapPrev:
    case C::SitemapNext:
      sitemap_step(app, cmd == C::SitemapNext ? 1 : -1);
      break;
    case C::ToggleBacklinks:
      app.backlinks_mode = !app.backlinks_mode;
      if (app.backlinks_mode) {
        app.key_focus = KeyFocus::Backlinks;
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
      if (focused_read_only(app)) {
        flash_status(app, kReadOnlyNotice);
        break;
      }
      app.browse_mode = !app.browse_mode;
      if (browsing(app)) {
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
    case C::ToggleChrome:
      toggle_chrome(app);
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
      if (browsing(app)) {
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
      std::string path = find_data_file(app, "FORMAT.md");
      if (path.empty()) path = find_data_file(app, "FORMAT.md");
      open_browse(app, path, "FORMAT.md — format spec");
      break;
    }
    case C::OpenSemanticDoc: {
      std::string path = find_data_file(app, "semantic.sexp");
      if (path.empty()) path = find_data_file(app, "examples/semantic.sexp");
      // find_data_file already tries examples/
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
      // examples/<name> under the data dir (paths.hpp).
      std::string path = find_data_file(app, name.c_str());
      if (path.empty())
        path = find_data_file(app, (std::string("examples/") + name).c_str());
      if (path.empty() || !fs::exists(path)) {
        flash_status(app, "Example not found: " + name);
        std::cerr << "Example not found: " << name
                  << " (see --data-dir)\n";
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
      if (!stash_all_or_confirm(app)) break;
      std::string p0 = find_data_file(app, "diff-v1.md");
      std::string p1 = find_data_file(app, "diff-v2.md");
      if (p0.empty()) p0 = find_data_file(app, "examples/diff-v1.md");
      if (p1.empty()) p1 = find_data_file(app, "examples/diff-v2.md");
      if (p0.empty() || p1.empty() || !fs::exists(p0) || !fs::exists(p1)) {
        flash_status(app,
                     "Diff demo files not found (diff-v1.md / diff-v2.md)");
        std::cerr << "Diff demo: examples/diff-v1.md and diff-v2.md required\n";
        break;
      }
      // Drop floats — their layouts refer to old trees.
      for (const FloatWindow& w : app.soft_wins) release_view(app, w.view);
      app.soft_wins.clear();
      clear_interaction_hover(app);
      app.buffers.clear();
      const BufferId d0 = add_buffer(app, load_doc(app, p0), p0);
      const BufferId d1 = add_buffer(app, load_doc(app, p1), p1);
      app.tab_order = {d0, d1};
      app.panes[0].buf = d0;
      app.panes[1].buf = d1;
      app.split = true;
      focus_view(app, 0);
      for (int pi = 0; pi < kCentreViews; ++pi) {
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
    case C::ViewMargins:
      try {
        app.view_settings.margin_pct = std::stof(arg);
      } catch (...) {
        break;
      }
      apply_view_settings(app);
      flash_status(app, "Margins: " + arg + "%");
      break;
    case C::ViewMeasure:
      try {
        app.view_settings.measure_chars = std::stoi(arg);
      } catch (...) {
        break;
      }
      apply_view_settings(app);
      flash_status(app, app.view_settings.measure_chars > 0
                            ? "Text width: " + arg + " characters"
                            : std::string("Text width: full"));
      break;
    case C::ToggleSectionFocus:
      toggle_section_focus_mode(app);
      break;
    case C::SelectStyle:
      if (!arg.empty()) apply_style(app, arg);
      break;
    case C::ReloadStyles:
      reload_styles(app);
      break;
    case C::ToggleFootnoteOverlay:
      app.view_settings.footnote_overlay = !app.view_settings.footnote_overlay;
      xanatoo::save_view_settings(app.view_settings);
      flash_status(app, app.view_settings.footnote_overlay
                            ? "Footnote overlay on"
                            : "Footnote overlay off");
      break;
    case C::ToggleBookmarkMarks:
      app.view_settings.bookmark_marks = !app.view_settings.bookmark_marks;
      xanatoo::save_view_settings(app.view_settings);
      flash_status(app, app.view_settings.bookmark_marks
                            ? "Bookmarks marked in the page"
                            : "Bookmarks not marked in the page");
      break;
    case C::ToggleAutoUpdates:
      app.view_settings.auto_updates = !app.view_settings.auto_updates;
      xanatoo::save_view_settings(app.view_settings);
      xanatoo::platform_auto_updates(app.view_settings.auto_updates);
      flash_status(app, app.view_settings.auto_updates
                            ? "Automatic update checks on"
                            : "Automatic update checks off");
      break;
    case C::ToggleHoverPreview:
      app.view_settings.hover_preview = !app.view_settings.hover_preview;
      xanatoo::save_view_settings(app.view_settings);
      flash_status(app, app.view_settings.hover_preview
                            ? "Link previews on hover on"
                            : "Link previews on hover off");
      break;
    case C::ToggleReaderDefault: {
      // Views that follow the default switch with it (keep_views first).
      keep_views(app);
      app.view_settings.reader_mode = !app.view_settings.reader_mode;
      xanatoo::save_view_settings(app.view_settings);
      for (Pane& v : app.panes)
        if (!v.reader) v.height_cache.clear();
      app.needs_layout = true;
      if (app.outline_mode) refresh_outline(app);
      flash_status(app, app.view_settings.reader_mode
                            ? "Reader mode is the default"
                            : "Whole pages are the default");
      break;
    }
    case C::TogglePreferences:
      app.preferences_open = !app.preferences_open;
      break;
    case C::SaveRedditCredentials: {
      const size_t split = arg.find('\n');
      const bool saved = split != std::string::npos &&
          xanatoo::save_reddit_credentials(
              {arg.substr(0, split), arg.substr(split + 1)});
      flash_status(app, saved ? "Reddit credentials saved for future requests"
                              : "Could not save Reddit credentials");
      break;
    }
    case C::ClearRedditCredentials:
      flash_status(app, xanatoo::save_reddit_credentials({})
                           ? "Saved Reddit credentials removed"
                           : "Could not remove Reddit credentials");
      break;
    case C::SetUiScale:
      try {
        app.view_settings.ui_scale = std::stof(arg);
        app.ui_scale_pending = true;
      } catch (...) {
      }
      break;
    case C::ToggleJustify:
      app.view_settings.justify = !app.view_settings.justify;
      apply_view_settings(app);
      if (app.view_settings.justify) {
        if (app.view_settings.measure_chars == 0)
          flash_status(
              app, "Justify on — View→Text width 72 for book-like width");
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
  // Content only keeps the status line while a prompt or notice needs it.
  status_bar_h = app.chrome_hidden && app.prompt.kind == PromptKind::None &&
                         xanatoo::sdl::ticks() >= app.status_notice_until
                     ? 0
                     : status_bar_full_h;
  // ImGui controls inside floating panels use absolute screen coordinates.
  // Snap/dock frames *before* reading those coordinates: draw_floats() also
  // places windows, but doing it only there leaves the child controls at the
  // pre-snap position when a dragged/resized panel reaches a screen edge.
  place_windows(app);
  xanatoo::UiState ui_state;
  build_ui_state(app, ui_state);
  auto ui_frame = app.ui.begin_frame(ui_state);
  app.ui_top = ui_frame.top_reserved;
  app.ui_bottom = ui_frame.bottom_reserved;
  place_centre_rows(app);  // the toolbar may have changed height
  // A tab dragged off the strip previews where it would dock.
  if (ui_frame.tab_drag_out) {
    app.dock_preview = compute_dock_zone(app, static_cast<int>(app.mouse_x),
                                         static_cast<int>(app.mouse_y));
    app.tab_drag_preview = true;
  } else if (app.tab_drag_preview) {
    app.tab_drag_preview = false;
    app.dock_preview = Dock::None;
  }
  execute_command(app, ui_frame.cmd, ui_frame.arg);
  // A tab press selects it but may turn into a drag (reorder / tear off),
  // so its mouse capture stays.
  if (ui_frame.cmd != xanatoo::UiCommand::None &&
      ui_frame.cmd != xanatoo::UiCommand::SelectTab &&
      ui_frame.cmd != xanatoo::UiCommand::EditReply)
    app.ui.release_input_capture();
}

void poll_gamepad_axes(App& app) {
  if (app.gamepad) {
    auto axis = [&](xanatoo::sdl::GamepadAxis a) -> float {
      Sint16 v = xanatoo::sdl::gamepad_axis(app.gamepad, a);
      const float dead = 8000.f;
      if (v > -dead && v < dead) return 0.f;
      return static_cast<float>(v) / 32767.f;
    };
    // Left stick Y: scroll focused pane (unchanged).
    float ly = axis(xanatoo::sdl::AxisLeftY);
    if (ly != 0.f) {
      Pane& p = focused_pane(app);
      const float max_s = pane_max_scroll(app, p);
      p.scroll_y = std::max(0.f, std::min(max_s, p.scroll_y + ly * 18.f));
      ensure_virtual_layout(app, p);  // the band may not reach the new view
    }
    // Triggers as page up/down
    static bool lt_held = false, rt_held = false;
    float lt = axis(xanatoo::sdl::AxisLeftTrigger);
    float rt = axis(xanatoo::sdl::AxisRightTrigger);
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

// Calls `fn(x0, x1, top, baseline)` (layout coordinates) for each line-run
// of consecutive glyphs accepted by `match`.
static bool is_reply_target(App& app, Pane& pane) {
  if (!app.reply.open || pane.buf != app.reply.buffer ||
      path_of(app, pane) != app.reply.page) return false;
  const auto& d = doc_of(app, pane);
  const auto matches = [&](xanatoo::NodeId id) {
    if (id == xanatoo::INVALID_NODE || id >= d.next_id) return false;
    const auto& n = d.get(id);
    return n.id == id && n.kind == xanatoo::NodeKind::Message &&
           n.label == app.reply.label;
  };
  if (matches(app.reply.node)) return true;
  // A fetched page may have been reparsed in the same buffer, reassigning
  // numeric node IDs. Service comment labels are the stable identity.
  if (!app.reply.label.empty())
    for (const auto& n : d.nodes)
      if (matches(n.id)) {
        app.reply.node = n.id;
        return true;
      }
  return false;
}

static const xanatoo::LayoutBox* reply_card_box(App& app, Pane& pane) {
  if (!is_reply_target(app, pane)) return nullptr;
  for (const auto& card : pane.layout.message_cards)
    if (card.box.node == app.reply.node) return &card.box;
  return nullptr;
}

template <typename Match, typename Fn>
static void for_each_link_run(App& app, const Pane& pane, Match match,
                              Fn fn) {
  float run_x0 = 0.f, run_x1 = 0.f, run_top = 0.f, run_base = 0.f;
  bool in_run = false;
  auto flush = [&]() {
    if (in_run) fn(run_x0, run_x1, run_top, run_base);
    in_run = false;
  };
  for (const auto& pg : pane.layout.glyphs) {
    if (!match(pg)) {
      flush();
      continue;
    }
    xanatoo::Font& font = app.fonts.pick(pg.font);
    const xanatoo::Glyph& g = font.glyph(pg.codepoint);
    float adv = static_cast<float>((g.advance >> 6) > 0 ? (g.advance >> 6)
                                                        : (g.width + 1));
    float top = pg.y - static_cast<float>(font.ascender());
    if (in_run && std::abs(top - run_top) < 0.5f && pg.x <= run_x1 + 2.f) {
      run_x1 = pg.x + adv;
      continue;
    }
    flush();
    run_x0 = pg.x;
    run_x1 = pg.x + adv;
    run_top = top;
    run_base = pg.y;
    in_run = true;
  }
  flush();
}

// Hover affordances, caret, focus bar, diff marks, structure overlay, drop
// indicators — everything drawn over the text inside the pane clip.
static void draw_pane_overlays(App& app, int i) {
  [[maybe_unused]] const float lh =
      static_cast<float>(app.fonts.regular.line_height());
  [[maybe_unused]] Pane& pane = app.panes[i];
  [[maybe_unused]] const int clip_y = pane.y;
  [[maybe_unused]] const float py = static_cast<float>(pane.y);
  [[maybe_unused]] const float ox = static_cast<float>(pane.x);
  [[maybe_unused]] const float draw_scroll = pane.scroll_y - py;
  // Compose target: persistent amber wash around the precise Message card,
  // independent of the caret/selection and without editing foreign content.
  if (const auto* box = reply_card_box(app, pane)) {
    app.reply.anchor_y = box->y;
    SDL_Rect r{static_cast<int>(box->x + ox),
               static_cast<int>(box->y - draw_scroll),
               static_cast<int>(box->width),
               static_cast<int>(box->height)};
    if (r.w > 0 && r.h > 0) {
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 235, 160, 40, 38);
      xanatoo::sdl::fill_rect(app.sdl, &r);
      SDL_SetRenderDrawColor(app.sdl, 200, 115, 22, 220);
      xanatoo::sdl::draw_rect(app.sdl, &r);
    }
  }
  // Hover affordances for interactive elements in this pane
  {
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    // Split trail: the left link whose target the right pane shows gets a
    // steady wash and a ▸ at the edge towards the right pane.
    if (const std::string& trail = split_trail_href(app, i); !trail.empty()) {
      const auto& tdoc = doc_of(app, pane);
      SDL_Rect last{};
      for_each_link_run(
          app, pane,
          [&](const xanatoo::PositionedGlyph& pg) {
            if (pg.kind != xanatoo::InlineKind::Link) return false;
            const xanatoo::Node& n = tdoc.get(pg.node);
            return pg.span_index < n.spans.size() &&
                   n.spans[pg.span_index].href == trail;
          },
          [&](float x0, float x1, float top, float base) {
            SDL_Rect r{static_cast<int>(x0 + ox) - 2,
                       static_cast<int>(top - draw_scroll) - 2,
                       static_cast<int>(x1 - x0) + 4,
                       app.fonts.regular.line_height() + 4};
            SDL_SetRenderDrawColor(app.sdl, 230, 160, 30, 70);
            xanatoo::sdl::fill_rect(app.sdl, &r);
            const int uy = static_cast<int>(base + 3.f - draw_scroll);
            SDL_SetRenderDrawColor(app.sdl, 200, 120, 10, 255);
            xanatoo::sdl::draw_line(app.sdl, r.x + 1, uy, r.x + r.w - 2, uy);
            last = r;
          });
      if (last.w > 0) {
        // ▸ at the pane's right edge, level with the link's last line.
        const int h = std::max(4, last.h / 3);
        const int x = pane.x + pane.w - sb_reserve(pane) - h - 3;
        const int cy = last.y + last.h / 2;
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 10, 255);
        for (int k = 0; k < h; ++k)
          xanatoo::sdl::draw_line(app.sdl, x + k, cy - (h - k), x + k, cy + (h - k));
      }
    }
    // Ordinary Back/Forward: wash the precise link which led to the next
    // history stop. It is navigation state, not a document style or edit.
    NavLink split_next;
    const NavLink* next = forward_nav_link(app, i);
    if (!next) {
      split_next.href = split_history_forward_href(app, i);
      if (!split_next.href.empty()) next = &split_next;
    }
    if (next) {
      const xanatoo::Document& d = doc_of(app, pane);
      const bool exact = next->node != xanatoo::INVALID_NODE &&
                         next->node < d.next_id &&
                         d.get(next->node).id == next->node &&
                         next->span < d.get(next->node).spans.size() &&
                         d.get(next->node).spans[next->span].href == next->href;
      const auto accent = xanatoo::theme::palette().accent;
      for_each_link_run(
          app, pane,
          [&](const xanatoo::PositionedGlyph& pg) {
            if (pg.kind != xanatoo::InlineKind::Link ||
                pg.node == xanatoo::INVALID_NODE) return false;
            if (exact)
              return pg.node == next->node && pg.span_index == next->span;
            const xanatoo::Node& n = d.get(pg.node);
            return pg.span_index < n.spans.size() &&
                   n.spans[pg.span_index].href == next->href;
          },
          [&](float x0, float x1, float top, float base) {
            SDL_Rect r{static_cast<int>(x0 + ox) - 2,
                       static_cast<int>(top - draw_scroll) - 2,
                       static_cast<int>(x1 - x0) + 4,
                       app.fonts.regular.line_height() + 4};
            SDL_SetRenderDrawColor(app.sdl, accent.r, accent.g, accent.b, 58);
            xanatoo::sdl::fill_rect(app.sdl, &r);
            const int uy = static_cast<int>(base + 3.f - draw_scroll);
            SDL_SetRenderDrawColor(app.sdl, accent.r, accent.g, accent.b, 220);
            xanatoo::sdl::draw_line(app.sdl, r.x + 1, uy, r.x + r.w - 2, uy);
          });
    }
    // Bookmarks inside the page, under the `#` passage of this view's
    // address (a bookmark just opened is that passage, washed once).
    update_bookmark_marks(app, pane);
    update_annotation_marks(app, pane);
    // Not drawn while an edit waits for them to be found again: the old
    // offsets would wash the wrong text.
    const bool bm_fresh = bookmark_marks_fresh(pane);
    for (const BookmarkMark& bm : pane.bookmark_marks.items) {
      if (!bm_fresh) break;
      const xanatoo::Selection& m = bm.range;
      if (m.empty()) continue;
      if (std::find_if(pane.marks.begin(), pane.marks.end(),
                       [&](const xanatoo::Selection& o) {
                         return o.anchor == m.anchor && o.focus == m.focus;
                       }) != pane.marks.end())
        continue;
      const xanatoo::Rgba& c = app.style.bookmark;
      const auto rects = xanatoo::selection_rects(
          pane.layout, m, draw_scroll, app.renderer.glyph_extents());
      app.renderer.draw_selection(rects, ox, c.r, c.g, c.b, c.a);
    }
    if (annotation_marks_fresh(pane)) {
      // Independent layers compose over the source; they never insert spans
      // or otherwise modify the document tree.
      const xanatoo::Rgba& c = app.style.bookmark;
      for (const AnnotationMark& mark : pane.annotation_marks.items) {
        if (mark.range.empty()) continue;
        const auto rects = xanatoo::selection_rects(
            pane.layout, mark.range, draw_scroll,
            app.renderer.glyph_extents());
        app.renderer.draw_selection(rects, ox, c.r, c.g, c.b, c.a);
        if (mark.annotation_id == app.selected_annotation_id)
          app.renderer.draw_selection(rects, ox, c.r, c.g, c.b, c.a);
      }
      // Note tabs are independently hit-tested; their geometry is shared
      // with app_events so clicking the tab opens the note editor.
      for (const AnnotationIcon& icon : annotation_icons(app, pane)) {
        const AnnotationMark& mark = pane.annotation_marks.items[icon.item];
        const bool hot = app.hover.annotation_pane == i &&
                         app.hover.annotation == mark.annotation_id;
        const int x = static_cast<int>(ox + icon.x);
        const int y = static_cast<int>(py + icon.y);
        const SDL_Rect tab{x, y, static_cast<int>(icon.w),
                           static_cast<int>(icon.h - 2.f)};
        SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.sdl, c.r, c.g, c.b, hot ? 255 : 220);
        xanatoo::sdl::fill_rect(app.sdl, &tab);
        xanatoo::sdl::draw_line(app.sdl, x + 5, y + 8, x + 7, y + 10);
      }
    }
    for (const BookmarkMark& bm : pane.bookmark_marks.items) {
      if (!bm_fresh) break;
      if (!bm.range.empty()) continue;
      // A reading position: a bar with a tab at its top, like a ribbon.
      const auto cp = xanatoo::caret_screen_pos(pane.layout, bm.range.focus,
                                                draw_scroll, lh);
      if (!cp.valid) continue;
      const xanatoo::Rgba& c = app.style.bookmark;
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, c.r, c.g, c.b, 220);
      const int x = static_cast<int>(cp.x + ox);
      const int top = static_cast<int>(cp.y - cp.height * 0.8f);
      const SDL_Rect bar{x - 1, top, 3, static_cast<int>(cp.height)};
      const SDL_Rect tab{x - 4, top - 4, 9, 5};
      xanatoo::sdl::fill_rect(app.sdl, &bar);
      xanatoo::sdl::fill_rect(app.sdl, &tab);
    }
    // A ribbon in the margin per bookmark: says what the wash is and takes
    // the clicks (select, right-click menu).
    for (const BookmarkIcon& ic : bookmark_icons(app, pane)) {
      const bool hot = app.hover.bookmark_pane == i &&
                       app.hover.bookmark ==
                           pane.bookmark_marks.items[ic.item].address;
      const xanatoo::Rgba& c = app.style.bookmark;
      const int x = static_cast<int>(ox + ic.x);
      const int y = static_cast<int>(py + ic.y);
      const int w = static_cast<int>(ic.w), h = static_cast<int>(ic.h);
      const auto shade = [&](int v) { return static_cast<Uint8>(v * 3 / 4); };
      SDL_SetRenderDrawColor(app.sdl, hot ? shade(c.r) : c.r,
                             hot ? shade(c.g) : c.g, hot ? shade(c.b) : c.b,
                             hot ? 255 : 210);
      // Body down to the notch, then two legs narrowing to its tip.
      const int notch = std::max(3, w / 2);
      const SDL_Rect body{x, y, w, h - notch};
      xanatoo::sdl::fill_rect(app.sdl, &body);
      for (int k = 0; k < notch; ++k) {
        const int yy = y + h - notch + k;
        const int leg = std::max(1, w / 2 - k);
        xanatoo::sdl::draw_line(app.sdl, x, yy, x + leg - 1, yy);
        xanatoo::sdl::draw_line(app.sdl, x + w - leg, yy, x + w - 1, yy);
      }
    }
    // The `#` passage of this view's address.
    for (const xanatoo::Selection& m : pane.marks) {
      const xanatoo::Rgba& c = app.style.mark;
      const auto rects = xanatoo::selection_rects(
          pane.layout, m, draw_scroll, app.renderer.glyph_extents());
      app.renderer.draw_selection(rects, ox, c.r, c.g, c.b, c.a);
    }
    // ... and its target on the right: the same wash and a ◂ at the edge
    // towards the left pane.
    if (const xanatoo::NodeId target = split_trail_target(app, i);
        target != xanatoo::INVALID_NODE) {
      SDL_Rect first{};
      for_each_link_run(
          app, pane,
          [&](const xanatoo::PositionedGlyph& pg) { return pg.node == target; },
          [&](float x0, float x1, float top, float) {
            SDL_Rect r{static_cast<int>(x0 + ox) - 2,
                       static_cast<int>(top - draw_scroll) - 2,
                       static_cast<int>(x1 - x0) + 4,
                       app.fonts.regular.line_height() + 4};
            SDL_SetRenderDrawColor(app.sdl, 230, 160, 30, 70);
            xanatoo::sdl::fill_rect(app.sdl, &r);
            if (first.w == 0) first = r;
          });
      if (first.w > 0) {
        const int h = std::max(4, first.h / 3);
        const int x = pane.x + 3;
        const int cy = first.y + first.h / 2;
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 10, 255);
        for (int k = 0; k < h; ++k)
          xanatoo::sdl::draw_line(app.sdl, x + h - 1 - k, cy - (h - k),
                             x + h - 1 - k, cy + (h - k));
      }
    }
    // Link / cite run: soft wash + thicker underline
    if (app.hover.link_pane == i &&
        app.hover.link_node != xanatoo::INVALID_NODE &&
        !app.hover.href.empty()) {
      for_each_link_run(
          app, pane,
          [&](const xanatoo::PositionedGlyph& pg) {
            return pg.node == app.hover.link_node &&
                   pg.span_index == app.hover.link_span &&
                   (pg.kind == xanatoo::InlineKind::Link ||
                    pg.kind == xanatoo::InlineKind::Cite ||
                    pg.kind == xanatoo::InlineKind::FootnoteRef);
          },
          [&](float x0, float x1, float top, float base) {
            int x = static_cast<int>(x0 + ox) - 2;
            int y = static_cast<int>(top - draw_scroll) - 2;
            int w = static_cast<int>(x1 - x0) + 4;
            int h = app.fonts.regular.line_height() + 4;
            SDL_SetRenderDrawColor(app.sdl, 40, 110, 220, 45);
            SDL_Rect r{x, y, w, h};
            xanatoo::sdl::fill_rect(app.sdl, &r);
            int uy = static_cast<int>(base + 3.f - draw_scroll);
            SDL_SetRenderDrawColor(app.sdl, 20, 80, 190, 255);
            xanatoo::sdl::draw_line(app.sdl, x + 1, uy, x + w - 2, uy);
            xanatoo::sdl::draw_line(app.sdl, x + 1, uy + 1, x + w - 2, uy + 1);
          });
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
                   static_cast<int>(box.y - pane.scroll_y + py),
                   static_cast<int>(box.width), static_cast<int>(h)};
        xanatoo::sdl::fill_rect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 30, 120, 80, 200);
        xanatoo::sdl::draw_rect(app.sdl, &r);
        break;
      }
    }
    // Image outline + emphasized resize handle
    if (app.hover.image_pane == i && app.hover.image != xanatoo::INVALID_NODE) {
      for (const auto& pi : pane.layout.images) {
        if (pi.node != app.hover.image) continue;
        if (pi.thumb > 0) break;  // collection card: hand cursor only
        int ix = pane.x + static_cast<int>(pi.x);
        int iy = static_cast<int>(pi.y - pane.scroll_y + py);
        int iw = static_cast<int>(pi.width);
        int ih = static_cast<int>(pi.height);
        SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 180);
        SDL_Rect hover_rect{ix, iy, iw, ih};
        xanatoo::sdl::draw_rect(app.sdl, &hover_rect);
        const int hs = app.hover.image_handle ? 14 : 10;
        SDL_Rect handle{ix + iw - hs, iy + ih - hs, hs, hs};
        SDL_SetRenderDrawColor(app.sdl, app.hover.image_handle ? 15 : 25,
                               app.hover.image_handle ? 70 : 90,
                               app.hover.image_handle ? 200 : 170, 255);
        xanatoo::sdl::fill_rect(app.sdl, &handle);
        if (app.hover.image_handle) {
          SDL_SetRenderDrawColor(app.sdl, 255, 255, 255, 220);
          xanatoo::sdl::draw_rect(app.sdl, &handle);
        }
        break;
      }
    }
    // Column gutter highlight
    if (app.hover.col_gap_pane == i && app.hover.col_gap) {
      float mx = app.mouse_x - static_cast<float>(pane.x);
      float my = content_y(app, app.mouse_y, i);
      for (const auto& gap : pane.layout.column_gaps) {
        float gx = gap.x;
        float gy0 = gap.y - pane.scroll_y;
        float gy1 = gy0 + gap.height;
        if (mx >= gx - 5.f && mx <= gx + 5.f && my >= gy0 && my <= gy1) {
          int gx_i = pane.x + static_cast<int>(gx);
          int gy_i = static_cast<int>(gy0 + py);
          int gh = static_cast<int>(gap.height);
          SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 90);
          SDL_Rect wide{gx_i - 4, gy_i, 9, gh};
          xanatoo::sdl::fill_rect(app.sdl, &wide);
          SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
          SDL_Rect bar{gx_i - 1, gy_i, 3, gh};
          xanatoo::sdl::fill_rect(app.sdl, &bar);
          break;
        }
      }
    }
    // Task checkbox zone
    if (app.hover.task_pane == i && app.hover.task != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.task) continue;
        int x = pane.x + static_cast<int>(box.x);
        int y = static_cast<int>(box.y - pane.scroll_y + py);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 40, 140, 80, 55);
        SDL_Rect r{x, y, 26, h};
        xanatoo::sdl::fill_rect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 30, 120, 70, 220);
        xanatoo::sdl::draw_rect(app.sdl, &r);
        break;
      }
    }
    // Block drag grip zone (drawn in the left gutter, not over text)
    if (app.hover.grip_pane == i && app.hover.grip != xanatoo::INVALID_NODE) {
      for (const auto& box : pane.layout.boxes) {
        if (box.node != app.hover.grip) continue;
        int gw = static_cast<int>(kBlockGripW);
        int x = pane.x + static_cast<int>(box.x) - gw;
        int y = static_cast<int>(box.y - pane.scroll_y + py);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 80, 120, 180, 50);
        SDL_Rect r{x, y, gw, h};
        xanatoo::sdl::fill_rect(app.sdl, &r);
        // Stronger grip dots centered in the gutter
        int gx = x + gw / 2 - 2;
        int gy = y + h / 2;
        SDL_SetRenderDrawColor(app.sdl, 40, 90, 160, 255);
        for (int d = -6; d <= 6; d += 6) {
          SDL_Rect dot{gx, gy + d, 4, 4};
          xanatoo::sdl::fill_rect(app.sdl, &dot);
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
        int y = static_cast<int>(box.y - pane.scroll_y + py);
        int w = static_cast<int>(box.width);
        int h = static_cast<int>(box.height);
        SDL_SetRenderDrawColor(app.sdl, 180, 120, 40, 40);
        SDL_Rect r{x, y, w, h};
        xanatoo::sdl::fill_rect(app.sdl, &r);
        SDL_SetRenderDrawColor(app.sdl, 180, 120, 40, 200);
        xanatoo::sdl::draw_rect(app.sdl, &r);
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
        case K::Collection:
          return "collection";
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
      int by = static_cast<int>(box.y - pane.scroll_y + py);
      int bh = static_cast<int>(box.height);
      if (by + bh < clip_y || by > app.win_h - status_bar_h) continue;
      // Leave room on the right for the kind tag.
      const int tag_gutter = 56;
      int wash_w = pane.w - 14 - tag_gutter;
      if (wash_w < 20) wash_w = pane.w - 14;
      SDL_SetRenderDrawColor(app.sdl, r, g, b, a_bar);
      SDL_Rect mark{pane.x + 1, by, 5, bh};
      xanatoo::sdl::fill_rect(app.sdl, &mark);
      SDL_SetRenderDrawColor(app.sdl, r, g, b, a_wash);
      SDL_Rect wash{pane.x + 6, by, wash_w, bh};
      xanatoo::sdl::fill_rect(app.sdl, &wash);

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
        if (chip.h > 4) xanatoo::sdl::fill_rect(app.sdl, &chip);
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
    if (browsing(app) ||
        ((xanatoo::sdl::ticks() - app.caret_blink_start) % 1000) < 600) {
      app.renderer.draw_caret(cpos, ox);
    }
  }

  // Focus indicator: thin bar at top of focused pane content
  if (i == app.focus) {
    SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
    SDL_Rect bar{pane.x, clip_y, pane.w, 3};
    xanatoo::sdl::fill_rect(app.sdl, &bar);
  }

  // Version-diff marks (orange bars on changed text blocks); the diff is
  // between the two centre views.
  if (app.diff_mode && !shared_doc(app) && i < kCentreViews) {
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
        int by = static_cast<int>(box.y - pane.scroll_y + py);
        int bh = static_cast<int>(box.height);
        if (by + bh < clip_y || by > app.win_h - status_bar_h) continue;
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 40, 200);
        SDL_Rect mark{pane.x + 2, by, 4, bh};
        xanatoo::sdl::fill_rect(app.sdl, &mark);
        SDL_SetRenderDrawColor(app.sdl, 200, 120, 40, 40);
        SDL_Rect wash{pane.x, by, pane.w - 6, bh};
        xanatoo::sdl::fill_rect(app.sdl, &wash);
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
            static_cast<int>(g.y - pane.scroll_y + py - lh * 0.85f);
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
        xanatoo::sdl::fill_rect(app.sdl, &r);
      }
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
    }
  }

  // Column gutters (subtle vertical handles)
  for (const auto& gap : pane.layout.column_gaps) {
    int gx = pane.x + static_cast<int>(gap.x);
    int gy = static_cast<int>(gap.y - pane.scroll_y + py);
    int gh = static_cast<int>(gap.height);
    bool active = app.dragging_col_gap && gap.left == app.col_gap_left;
    SDL_SetRenderDrawColor(app.sdl, active ? 25 : 190, active ? 90 : 188,
                           active ? 170 : 180, 255);
    SDL_Rect gbar{gx - 1, gy, 3, gh};
    xanatoo::sdl::fill_rect(app.sdl, &gbar);
  }

  // Block drag-drop: insertion line + source highlight
  if (app.dragging_block && i == app.focus) {
    // Dim the dragged block
    for (const auto& box : pane.layout.boxes) {
      if (box.node != app.drag_block_id) continue;
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 80, 140, 220, 60);
      SDL_Rect r{pane.x + static_cast<int>(box.x),
                 static_cast<int>(box.y - pane.scroll_y + py),
                 static_cast<int>(box.width), static_cast<int>(box.height)};
      xanatoo::sdl::fill_rect(app.sdl, &r);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
    }
    if (app.drop_line_y >= 0.f) {
      int ly = static_cast<int>(app.drop_line_y - pane.scroll_y + py);
      SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 255);
      SDL_Rect line{pane.x + 24, ly - 1, pane.w - 40, 3};
      xanatoo::sdl::fill_rect(app.sdl, &line);
    }
  } else if (i == app.focus && !app.dragging_block &&
             app.hover.bookmark.empty()) {
    // Subtle grip dots in the left gutter (not over the text)
    // mouse_y is window space; boxes are content space.
    float adj_y = content_y(app, app.mouse_y, i) + pane.scroll_y;
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
      int gy = static_cast<int>(box.y - pane.scroll_y + py +
                                box.height * 0.5f);
      SDL_SetRenderDrawColor(app.sdl, 160, 158, 150, 255);
      for (int d = -6; d <= 6; d += 6) {
        SDL_Rect dot{gx, gy + d, 3, 3};
        xanatoo::sdl::fill_rect(app.sdl, &dot);
      }
      break;
    }
  }
}

struct SectionMark {
  float y = 0.f;  // document space
  int level = 1;
};

// Section / heading positions of a pane's document for the scrollbar.
// Every block the layout skipped still has a stub box, so only the inside
// of a stub is estimated (from height hints); nodes with no box outside a
// stub were not laid out at all (section focus) and get no mark.
// `block_y`, when given, gets the (estimated) top of every block on the way.
static std::vector<SectionMark> section_marks(
    App& app, Pane& pane,
    std::unordered_map<xanatoo::NodeId, float>* block_y = nullptr) {
  std::vector<SectionMark> marks;
  const auto& d = doc_of(app, pane);
  if (d.root == xanatoo::INVALID_NODE || d.root >= d.next_id) return marks;
  std::unordered_map<xanatoo::NodeId, const xanatoo::LayoutBox*> box_of;
  box_of.reserve(pane.layout.boxes.size());
  for (const auto& b : pane.layout.boxes)
    if (b.node != xanatoo::INVALID_NODE) box_of.emplace(b.node, &b);
  const float lh = static_cast<float>(app.fonts.regular.line_height());
  auto heading_level = [&](const xanatoo::Node& sec, int depth) {
    for (xanatoo::NodeId c : sec.children) {
      const auto& h = d.get(c);
      if (h.kind == xanatoo::NodeKind::Header ||
          h.kind == xanatoo::NodeKind::Heading)
        return h.level > 0 ? std::min(8, static_cast<int>(h.level)) : depth;
    }
    return depth;
  };
  // y: running position, only meaningful while `est` (inside a stub).
  std::function<void(xanatoo::NodeId, int, bool, float&)> visit =
      [&](xanatoo::NodeId id, int depth, bool est, float& y) {
        if (id == xanatoo::INVALID_NODE || id >= d.next_id) return;
        const auto& n = d.get(id);
        if (n.id != id) return;
        auto it = box_of.find(id);
        const xanatoo::LayoutBox* box = it != box_of.end() ? it->second : nullptr;
        if (!box && !est) {
          // Box-less container (Document, List, …) or a block the layout
          // left out: only materialised descendants count.
          float unused = 0.f;
          for (xanatoo::NodeId c : n.children) visit(c, depth, false, unused);
          return;
        }
        const float y0 = box ? box->y : y;
        if (block_y) block_y->emplace(id, y0);
        const bool sec = n.kind == xanatoo::NodeKind::Section ||
                         n.kind == xanatoo::NodeKind::Article;
        const bool parent_sec =
            n.parent != xanatoo::INVALID_NODE &&
            (d.get(n.parent).kind == xanatoo::NodeKind::Section ||
             d.get(n.parent).kind == xanatoo::NodeKind::Article);
        if (sec) {
          marks.push_back(SectionMark{y0, heading_level(n, depth + 1)});
        } else if ((n.kind == xanatoo::NodeKind::Header ||
                    n.kind == xanatoo::NodeKind::Heading) &&
                   !parent_sec) {
          const int lv = static_cast<int>(n.level);
          marks.push_back(SectionMark{y0, lv > 0 ? std::min(8, lv)
                                                 : std::max(1, depth)});
        }
        const bool folded = sec && app.folded_sections.count(id);
        float end = y0;
        if (!folded && !n.children.empty()) {
          bool stub = false;
          if (box) {
            stub = true;
            for (xanatoo::NodeId c : n.children)
              if (box_of.count(c)) {
                stub = false;
                break;
              }
          }
          float cy = y0;
          for (xanatoo::NodeId c : n.children)
            visit(c, sec ? depth + 1 : depth, est || stub, cy);
          end = cy;
        }
        if (box) {
          y = box->y + box->height;
        } else if (auto hit = pane.height_cache.find(id);
                   hit != pane.height_cache.end()) {
          y = y0 + hit->second;
        } else {
          y = end > y0 ? end : y0 + lh;
        }
      };
  float y = 0.f;
  visit(d.root, 0, false, y);
  return marks;
}

// Classic scrollbar in the strip excluded from the content clip.
static void draw_pane_scrollbar(App& app, int i) {
  [[maybe_unused]] const float lh =
      static_cast<float>(app.fonts.regular.line_height());
  [[maybe_unused]] Pane& pane = app.panes[i];
  const float max_scroll = pane_max_scroll(app, pane);
  // Classic scrollbar: track, ↑/↓, chapter/search markers, thumb.
  {
    const float view_h = std::max(1.f, static_cast<float>(pane.h));
    float content_h = std::max(view_h, pane.layout.content_height);
    const float arrow = static_cast<float>(kSbArrow);
    float track_h = std::max(1.f, view_h - 2.f * arrow);
    // Thumb sized against full view; position on track between arrows.
    float thumb_h = std::max(20.f, track_h * (view_h / content_h));
    if (thumb_h > track_h) thumb_h = track_h;
    float track_travel = std::max(0.f, track_h - thumb_h);
    float ratio = (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
    float thumb_y = static_cast<float>(pane.y) + arrow + ratio * track_travel;
    int sx = sb_x(pane);
    int top = pane.y;
    int vh = static_cast<int>(view_h);

    // Track (sunken trough)
    namespace th = xanatoo::theme;
    const th::Palette& pal = th::palette();
    SDL_Rect track_r{sx, top, kSbW, vh};
    th::fill(app.sdl, track_r, pal.scroll_trough);
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
      xanatoo::sdl::fill_rect(app.sdl, &tick);
    };
    // Chapter / section breaks, from this pane's own document and layout.
    for (const SectionMark& m : section_marks(app, pane)) {
      if (m.level <= 1)
        mark_at(m.y, 25, 90, 170);
      else
        mark_at(m.y, 140, 160, 190);
    }
    // Bookmarks inside the page: from the layout where it holds the text,
    // else from the estimated position of the block.
    if (bookmark_marks_fresh(pane) && !pane.bookmark_marks.items.empty()) {
      std::unordered_map<xanatoo::NodeId, float> block_y;
      section_marks(app, pane, &block_y);
      const xanatoo::Document& bd = doc_of(app, pane);
      const xanatoo::Rgba& c = app.style.bookmark;
      for (const BookmarkMark& bm : pane.bookmark_marks.items) {
        const auto cpos = xanatoo::caret_screen_pos(
            pane.layout, bm.range.anchor, 0.f,
            static_cast<float>(app.fonts.regular.line_height()));
        float y = cpos.valid ? cpos.y : -1.f;
        for (xanatoo::NodeId n = bm.range.anchor.node;
             y < 0.f && n != xanatoo::INVALID_NODE; n = bd.get(n).parent)
          if (auto it = block_y.find(n); it != block_y.end()) y = it->second;
        if (y >= 0.f) mark_at(y, c.r, c.g, c.b);
      }
    }
    if (annotation_marks_fresh(pane) && !pane.annotation_marks.items.empty()) {
      std::unordered_map<xanatoo::NodeId, float> block_y;
      section_marks(app, pane, &block_y);
      const xanatoo::Document& ad = doc_of(app, pane);
      const xanatoo::Rgba& c = app.style.bookmark;
      for (const AnnotationMark& mark : pane.annotation_marks.items) {
        auto cp = xanatoo::caret_screen_pos(
            pane.layout, mark.range.anchor, 0.f,
            static_cast<float>(app.fonts.regular.line_height()));
        float y = cp.valid ? cp.y : -1.f;
        for (xanatoo::NodeId n = mark.range.anchor.node;
             y < 0.f && n != xanatoo::INVALID_NODE; n = ad.get(n).parent)
          if (auto it = block_y.find(n); it != block_y.end()) y = it->second;
        if (y >= 0.f) mark_at(y, c.r, c.g, c.b);
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
        xanatoo::sdl::draw_line(app.sdl, cx, cy - 3, cx - 4, cy + 2);
        xanatoo::sdl::draw_line(app.sdl, cx, cy - 3, cx + 4, cy + 2);
        xanatoo::sdl::draw_line(app.sdl, cx - 4, cy + 2, cx + 4, cy + 2);
      } else {
        xanatoo::sdl::draw_line(app.sdl, cx, cy + 3, cx - 4, cy - 2);
        xanatoo::sdl::draw_line(app.sdl, cx, cy + 3, cx + 4, cy - 2);
        xanatoo::sdl::draw_line(app.sdl, cx - 4, cy - 2, cx + 4, cy - 2);
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
    // The target's tick is drawn *over* the thumb so it stays visible even
    // when the comment is at the current scroll position. The block map also
    // approximates positions outside the virtualised layout band.
    if (is_reply_target(app, pane)) {
      float y = app.reply.anchor_y;
      if (const auto* box = reply_card_box(app, pane))
        y = app.reply.anchor_y = box->y;
      if (y < 0.f) {
        std::unordered_map<xanatoo::NodeId, float> block_y;
        section_marks(app, pane, &block_y);
        if (auto it = block_y.find(app.reply.node); it != block_y.end())
          y = it->second;
      }
      if (y >= 0.f) {
        mark_at(y - 2.f, 255, 178, 48);
        mark_at(y, 204, 94, 12);
        mark_at(y + 2.f, 255, 178, 48);
      }
    }
  }
}

// Document layer of pane i — paper, selection, text — into the current render
// target, clipped to `area`. Content y maps to target y - `scroll`.
static void draw_pane_layer(App& app, int i, const SDL_Rect& area, float ox,
                            float scroll) {
  Pane& pane = app.panes[i];
  xanatoo::sdl::set_clip_rect(app.sdl, &area);
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  app.renderer.draw_backdrop(pane.layout, area, scroll, ox);
  if (!pane.sel.empty()) {
    auto rects = xanatoo::selection_rects(pane.layout, pane.sel, scroll,
                                          app.renderer.glyph_extents());
    // Leap-search match uses amber so it is distinct from a user selection.
    if (prompt_is_search(app) && i == app.focus)
      app.renderer.draw_selection(rects, ox, 230, 190, 70, 150);
    else
      app.renderer.draw_selection(rects, ox);
  }
  app.renderer.draw(pane.layout, scroll, ox);
}

void release_raster(PaneRaster& r) {
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
  if (!xanatoo::sdl::target_supported(app.sdl)) return false;
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
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
    const int keep = r.h - std::abs(dy);
    const SDL_Rect from{0, std::max(dy, 0), r.w, keep};
    const SDL_Rect to{0, std::max(-dy, 0), r.w, keep};
    xanatoo::sdl::copy(app.sdl, src, &from, &to);
    const SDL_Rect strip = dy > 0 ? SDL_Rect{0, keep, r.w, dy}
                                  : SDL_Rect{0, 0, r.w, -dy};
    draw_pane_layer(app, i, strip, 0.f, scroll);
  } else {
    SDL_SetRenderTarget(app.sdl, r.tex[r.cur]);
    draw_pane_layer(app, i, SDL_Rect{0, 0, r.w, r.h}, 0.f, scroll);
  }
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
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
  pane.scroll_y = std::min(pane.scroll_y, pane_max_scroll(app, pane));
  // Paged: always at the top of a spread.
  if (pane.pages.on)
    pane.scroll_y = spread_scroll(
        pane, spread_at(pane, pane.scroll_y + pane.pages.stride * 0.5f));

  const int clip_y = pane.y;
  SDL_Rect clip{pane.x, clip_y, std::max(1, pane.w - sb_reserve(pane)),
                std::max(1, pane.h)};

  // Draw at a whole-pixel scroll so the cached raster can be blitted; the
  // fractional part (touch / fling / scrollbar drag) stays in pane.scroll_y.
  const float exact_scroll = pane.scroll_y;
  pane.scroll_y = std::round(exact_scroll);

  // screen_y = content_y - scroll_y + pane.y
  const float draw_scroll = pane.scroll_y - static_cast<float>(pane.y);
  // Same mapping relative to the clip top (texture space).
  const float tex_scroll = draw_scroll + static_cast<float>(clip_y);
  if (update_pane_raster(app, i, clip.w, clip.h, tex_scroll)) {
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
    xanatoo::sdl::copy(app.sdl, pane.raster.tex[pane.raster.cur], nullptr, &clip);
    xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  } else {
    draw_pane_layer(app, i, clip, static_cast<float>(pane.x), draw_scroll);
  }

  draw_pane_overlays(app, i);
  draw_footnote_overlay(app, i, clip);

  // Scrollbar lives in the strip excluded from the content clip.
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);

  if (pane.pages.on)
    draw_page_chrome(app, i);
  else
    draw_pane_scrollbar(app, i);
  draw_preview_badge(app, i);
  draw_load_indicator(app, i);
  pane.scroll_y = exact_scroll;
}

void draw_panes(App& app) {
  // Style paper is also the window clear colour behind the panes.
  app.renderer.clear(app.style.paper.r, app.style.paper.g, app.style.paper.b);
  int pane_count = app.split ? 2 : 1;
  for (int i = 0; i < pane_count; ++i) draw_pane(app, i);
}

void draw_split_chrome(App& app) {
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  if (app.split) {
    int mid = app.panes[1].x - 3;
    SDL_Rect div{mid, app.panes[1].y, 6, app.panes[1].h};
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
    const float ds0 = app.panes[0].scroll_y - static_cast<float>(app.panes[0].y);
    const float ds1 = app.panes[1].scroll_y - static_cast<float>(app.panes[1].y);
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
      int min_y = app.panes[0].y;
      int max_y = app.panes[0].y + app.panes[0].h;
      if (y0 >= min_y && y0 < max_y && y1 >= min_y && y1 < max_y) {
        SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.sdl, 25, 90, 170, 160);
        // Straight connector + soft endpoints
        xanatoo::sdl::draw_line(app.sdl, x0, y0, x1, y1);
        xanatoo::sdl::draw_line(app.sdl, x0, y0 + 1, x1, y1 + 1);
        auto dot = [&](int x, int y) {
          SDL_Rect r{x - 3, y - 3, 7, 7};
          xanatoo::sdl::fill_rect(app.sdl, &r);
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
    if (app.ol.dock == Dock::None) {
      app.ol.w = std::max(app.ol.w, 200);
      app.ol.h = std::max(app.ol.h, 140);
    }
    const float step = outline_row_step(app);
    auto [cx, cy, cw, ch] = outline_client_rect(app);
    {
      float content_h = static_cast<float>(app.outline.size()) * step + 8.f;
      float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      if (app.ol.scroll > max_s) app.ol.scroll = max_s;
    }

    namespace th = xanatoo::theme;
    const th::Palette& pal = th::palette();
    xanatoo::Font& font = app.ui_font;
    xanatoo::Font& bold =
        app.ui_font_bold.loaded() ? app.ui_font_bold : app.ui_font;

    std::string ol_title = "Outline";
    const bool section_focus = focused_pane(app).section_focus;
    if (section_focus && app.outline_sel >= 0 &&
        app.outline_sel < static_cast<int>(app.outline.size())) {
      const std::string& tx =
          app.outline[static_cast<size_t>(app.outline_sel)].text;
      if (!tx.empty()) ol_title += " — " + tx;
    }
    const SDL_Rect outer{app.ol.x, app.ol.y, app.ol.w, app.ol.h};
    const SDL_Rect fb = outline_focus_rect(app, outer);
    // Title tab + selection are lit only while the outline has the keyboard.
    const bool keys = panel_has_keys(app, KeyFocus::Outline);
    draw_floating_frame(app, outer, ol_title, keys, app.ol.dock == Dock::None,
                        outer.x + outer.w - 2 * float_close_w - fb.x,
                        app.ol.dock == Dock::Max);
    // Focus toggle (section isolation): latched push button left of close.
    draw_title_button(app, fb, "Focus", section_focus);

    SDL_Rect clip{cx, cy, cw, ch};
    xanatoo::sdl::set_clip_rect(app.sdl, &clip);
    const bool scrolls =
        static_cast<float>(app.outline.size()) * step + 8.f >
        static_cast<float>(ch);
    const int text_right = cw - (scrolls ? 16 : 4);
    // Row i spans [cy + i·step − scroll, +step) — click_outline relies on it.
    float row_y = static_cast<float>(cy) - app.ol.scroll;
    for (int i = 0; i < static_cast<int>(app.outline.size()); ++i, row_y += step) {
      if (row_y > static_cast<float>(cy + ch)) break;
      if (row_y + step < static_cast<float>(cy)) continue;
      const auto& e = app.outline[static_cast<size_t>(i)];
      const bool sel = i == app.outline_sel;
      const int ry = static_cast<int>(row_y);
      const int rh = static_cast<int>(step);
      const OutlineRowGeom g = outline_row_geom(app, e.level);
      const bool lit = sel && keys;
      if (sel)
        th::fill(app.sdl, SDL_Rect{cx + 1, ry, text_right - 1, rh},
                 keys ? pal.accent : pal.face_dark);
      const th::Rgb ink = lit ? pal.highlight : pal.text;
      const th::Rgb dim = lit ? pal.accent_soft : pal.text_dim;

      xanatoo::Font& f = e.level <= 1 ? bold : font;
      const float baseline = static_cast<float>(
          ry + (rh - f.line_height()) / 2 + f.ascender());
      // Alt+1…9 / 1…9 shortcut column.
      if (i < 9) {
        const std::string num(1, static_cast<char>('1' + i));
        app.renderer.draw_text(font,
                               static_cast<float>(cx + g.num_right -
                                                  font.measure(num)),
                               baseline, num, dim.r, dim.g, dim.b);
      }
      // Disclosure triangle (drawn, so it never depends on font coverage).
      if (e.has_children) {
        const int tcx = cx + g.twist_x + g.twist_w / 2;
        const int tcy = ry + rh / 2;
        SDL_SetRenderDrawColor(app.sdl, dim.r, dim.g, dim.b, 255);
        const int t = std::max(4, g.twist_w * 2 / 7);  // half extent
        for (int k = 0; k <= t; ++k) {
          if (e.folded)  // ▸ pointing right
            xanatoo::sdl::draw_line(app.sdl, tcx - t / 2 + k, tcy - t + k,
                               tcx - t / 2 + k, tcy + t - k);
          else  // ▾ pointing down
            xanatoo::sdl::draw_line(app.sdl, tcx - t + k, tcy - t / 2 + k,
                               tcx + t - k, tcy - t / 2 + k);
        }
      }
      const int text_w = text_right - g.text_x - 4;
      const th::Rgb tink = e.folded && !lit ? pal.text_dim : ink;
      app.renderer.draw_text(f, static_cast<float>(cx + g.text_x), baseline,
                             fit_text(f, e.text, text_w), tink.r, tink.g,
                             tink.b);
    }
    if (app.outline.empty()) {
      app.renderer.draw_text(
          font, static_cast<float>(cx + 8),
          static_cast<float>(cy + 6 + font.ascender()),
          "No headings in this document", pal.text_dim.r, pal.text_dim.g,
          pal.text_dim.b);
    }
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);

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
    if (app.bl.dock == Dock::None) {
      app.bl.w = std::max(app.bl.w, 200);
      app.bl.h = std::max(app.bl.h, 120);
    }
    const float step = backlinks_row_step(app);
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
                        panel_has_keys(app, KeyFocus::Backlinks),
                        app.bl.dock == Dock::None, 0, app.bl.dock == Dock::Max);
    SDL_Rect clip{cx, cy, cw, ch};
    xanatoo::sdl::set_clip_rect(app.sdl, &clip);
    xanatoo::Font& font = app.ui_font;
    const auto& pal = xanatoo::theme::palette();
    const int text_off =
        (static_cast<int>(step) - font.line_height()) / 2 + font.ascender();
    float ty = static_cast<float>(cy + text_off) - app.bl.scroll;
    if (app.backlinks.empty()) {
      app.renderer.draw_text(font, static_cast<float>(cx + 8), ty,
                             "No links to this label", pal.text_dim.r,
                             pal.text_dim.g, pal.text_dim.b);
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
        const bool lit = sel && panel_has_keys(app, KeyFocus::Backlinks);
        if (sel) {
          SDL_Rect hi{cx + 1, static_cast<int>(ty) - text_off, cw - 2,
                      static_cast<int>(step)};
          xanatoo::theme::fill(app.sdl, hi, lit ? pal.accent : pal.face_dark);
        }
        const auto ink = lit ? pal.highlight : pal.text;
        app.renderer.draw_text(font, static_cast<float>(cx + 8), ty,
                               fit_text(font, line, cw - 28), ink.r, ink.g,
                               ink.b);
      }
      ty += step;
    }
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
    {
      const float content_h =
          static_cast<float>(app.backlinks.size()) * step + 8.f;
      const float max_s = std::max(0.f, content_h - static_cast<float>(ch));
      draw_client_scrollbar(app, cx, cy, cw, ch, app.bl.scroll, max_s);
    }
  }
}

void draw_history_panel(App& app) {
  if (!app.history_panel_open) return;
  FloatWindow& w = app.history_win;
  if (w.dock == Dock::None) {
    w.w = std::max(w.w, 200);
    w.h = std::max(w.h, 140);
  }
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const float step = static_cast<float>(
      std::max(18, app.ui_font.line_height() + 7));
  const float content_h = static_cast<float>(app.visited.size()) * step + 8.f;
  const float max_s = std::max(0.f, content_h - static_cast<float>(ch));
  w.scroll = std::clamp(w.scroll, 0.f, max_s);

  const SDL_Rect outer{w.x, w.y, w.w, w.h};
  draw_floating_frame(app, outer, "History",
                      panel_has_keys(app, KeyFocus::History),
                      w.dock == Dock::None, 0,
                      w.dock == Dock::Max);
  SDL_Rect clip{cx, cy, cw, ch};
  xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  const auto& pal = xanatoo::theme::palette();
  const std::string here = path_of(app, focused_pane(app));
  float row_y = static_cast<float>(cy) - w.scroll;
  for (int row = 0; row < static_cast<int>(app.visited.size()); ++row) {
    if (row_y > static_cast<float>(cy + ch)) break;
    if (row_y + step >= static_cast<float>(cy)) {
      const std::string& url =
          app.visited[app.visited.size() - 1 - static_cast<size_t>(row)];
      const bool current = url == here;
      const int ry = static_cast<int>(row_y);
      if (current)
        xanatoo::theme::fill(app.sdl, SDL_Rect{cx + 1, ry, cw - 2,
                                               static_cast<int>(step)},
                             pal.face_dark);
      const int baseline = ry + (static_cast<int>(step) -
                                 app.ui_font.line_height()) / 2 +
                           app.ui_font.ascender();
      app.renderer.draw_text(
          app.ui_font, static_cast<float>(cx + 8), static_cast<float>(baseline),
          fit_text(app.ui_font, url, cw - 28), pal.text.r, pal.text.g,
          pal.text.b);
    }
    row_y += step;
  }
  if (app.visited.empty())
    app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 8),
                           static_cast<float>(cy + 8 + app.ui_font.ascender()),
                           "No pages visited this session", pal.text_dim.r,
                           pal.text_dim.g, pal.text_dim.b);
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll, max_s);
}

void draw_styles_panel(App& app) {
  if (!app.styles_panel_open) return;
  FloatWindow& w = app.styles_win;
  if (w.dock == Dock::None) {
    w.w = std::max(w.w, 200);
    w.h = std::max(w.h, 140);
  }
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const float step = static_cast<float>(
      std::max(18, app.ui_font.line_height() + 7));
  if (app.styles.empty())
    app.styles_sel = 0;
  else
    app.styles_sel = std::clamp(app.styles_sel, 0,
                                static_cast<int>(app.styles.size()) - 1);
  const float content_h = static_cast<float>(app.styles.size()) * step + 8.f;
  const float max_s = std::max(0.f, content_h - static_cast<float>(ch));
  w.scroll = std::clamp(w.scroll, 0.f, max_s);

  const SDL_Rect outer{w.x, w.y, w.w, w.h};
  const bool focused = panel_has_keys(app, KeyFocus::Styles);
  draw_floating_frame(app, outer, "Style Sheets", focused,
                      w.dock == Dock::None, 0, w.dock == Dock::Max);
  SDL_Rect clip{cx, cy, cw, ch};
  xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  const auto& pal = xanatoo::theme::palette();
  float row_y = static_cast<float>(cy) - w.scroll;
  for (int row = 0; row < static_cast<int>(app.styles.size()); ++row) {
    if (row_y > static_cast<float>(cy + ch)) break;
    if (row_y + step >= static_cast<float>(cy)) {
      const xanatoo::StyleEntry& style =
          app.styles[static_cast<size_t>(row)];
      const bool active = style.name == app.style.name;
      const bool selected = row == app.styles_sel;
      const int ry = static_cast<int>(row_y);
      if (selected || active)
        xanatoo::theme::fill(app.sdl, SDL_Rect{cx + 1, ry, cw - 2,
                                               static_cast<int>(step)},
                             selected && focused ? pal.accent : pal.face_dark);
      const int baseline = ry + (static_cast<int>(step) -
                                 app.ui_font.line_height()) / 2 +
                           app.ui_font.ascender();
      const std::string& label = style.title.empty() ? style.name : style.title;
      const xanatoo::theme::Rgb ink =
          selected && focused ? pal.highlight : pal.text;
      app.renderer.draw_text(
          app.ui_font, static_cast<float>(cx + 8), static_cast<float>(baseline),
          fit_text(app.ui_font, label, cw - 28), ink.r, ink.g, ink.b);
    }
    row_y += step;
  }
  if (app.styles.empty())
    app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 8),
                           static_cast<float>(cy + 8 + app.ui_font.ascender()),
                           "No style sheets found", pal.text_dim.r,
                           pal.text_dim.g, pal.text_dim.b);
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll, max_s);
}

void draw_help_window(App& app) {
  if (app.help_mode) {
    ensure_help_doc(app);
    if (app.help.dock == Dock::None) {
      app.help.w = std::max(app.help.w, 280);
      app.help.h = std::max(app.help.h, 200);
    }
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
        "Help — xanatoo", true, app.help.dock == Dock::None, 0,
        app.help.dock == Dock::Max);

    auto [cx, cy, cw, ch] = help_client_rect(app);
    SDL_Rect clip{cx, cy, cw, ch};
    xanatoo::sdl::set_clip_rect(app.sdl, &clip);
    // screen_y = content_y - scroll + client_y  ⇒  draw_scroll = scroll -
    // client_y (mirrors pane path: draw_scroll = scroll_y - ui_top)
    const float help_draw_scroll = app.help.scroll - static_cast<float>(cy);
    app.renderer.draw_backdrop(app.help_layout, clip, help_draw_scroll,
                               static_cast<float>(cx));
    app.renderer.draw(app.help_layout, help_draw_scroll,
                      static_cast<float>(cx));
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);

    {
      const float max_s = std::max(
          0.f, app.help_layout.content_height - static_cast<float>(ch) + 40.f);
      draw_client_scrollbar(app, cx, cy, cw, ch, app.help.scroll, max_s);
    }
  }
}

void draw_dock_preview(App& app) {
  if (app.tab_drop_preview) {
    const int h = static_cast<int>(28.f * app.ui_scale);
    SDL_Rect strip{0, static_cast<int>(app.ui_top) - h, app.win_w, h};
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 90);
    xanatoo::sdl::fill_rect(app.sdl, &strip);
    SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 200);
    xanatoo::sdl::draw_rect(app.sdl, &strip);
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  }
  if (app.dock_preview != Dock::None) {
    auto [dx, dy, dw, dh] = dock_rect(app, app.dock_preview);
    if (dw > 0 && dh > 0) {
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 70);
      SDL_Rect zone{dx, dy, dw, dh};
      xanatoo::sdl::fill_rect(app.sdl, &zone);
      SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 180);
      xanatoo::sdl::draw_rect(app.sdl, &zone);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
      const char* label = "";
      switch (app.dock_preview) {
        case Dock::Left:
          label = "Dock left";
          break;
        case Dock::Right:
          label = "Dock right";
          break;
        case Dock::Top:
          label = "Dock top";
          break;
        case Dock::Bottom:
          label = "Dock bottom";
          break;
        case Dock::Max:
          label = "Maximize";
          break;
        case Dock::None:
          break;
      }
      if (label[0]) {
        int tw = app.ui_font.measure(label);
        float lx = static_cast<float>(dx + (dw - tw) / 2);
        float ly = static_cast<float>(dy + dh / 2);
        app.renderer.draw_text(app.ui_font, lx, ly, label, 30, 60, 120);
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
      } else if (!app.hover.datetime.empty()) {
        tip = xanatoo::timestamp_hover_text(
            app.hover.datetime, static_cast<int64_t>(std::time(nullptr)));
        if (!app.hover.href.empty()) tip += "\nClick: " + app.hover.href;
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
      } else if (!app.hover.bookmark.empty()) {
        const xanatoo::Bookmark* b = find_bookmark(app, app.hover.bookmark);
        tip = "Bookmark: " + (b ? xanatoo::bookmark_label(*b)
                                : app.hover.bookmark) +
              "  ·  right-click for more";
      } else if (app.hover.grip != xanatoo::INVALID_NODE) {
        tip = "Drag to move block";
      } else if (app.hover.transclude != xanatoo::INVALID_NODE) {
        tip = app.hover.transclude_src.empty()
                  ? "Include"
                  : ("Include: " + app.hover.transclude_src);
      }
    }
    if (!tip.empty()) {
      std::vector<std::string> lines;
      size_t pos = 0;
      do {
        const size_t end = tip.find('\n', pos);
        std::string line = tip.substr(pos, end == std::string::npos
                                              ? std::string::npos : end - pos);
        if (line.size() > 96) line = line.substr(0, 93) + "…";
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        pos = end + 1;
      } while (pos < tip.size());
      int tw = 0;
      for (const auto& line : lines)
        tw = std::max(tw, app.ui_font.measure(line));
      const int pad_x = 10, pad_y = 6;
      int box_w = tw + pad_x * 2;
      int box_h = app.ui_font.line_height() * static_cast<int>(lines.size()) +
                  pad_y * 2;
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
      xanatoo::sdl::fill_rect(app.sdl, &box);
      // Accent by kind
      if (app.hover.stretch != xanatoo::INVALID_NODE)
        SDL_SetRenderDrawColor(app.sdl, 40, 140, 90, 255);
      else if (app.hover.col_gap || app.hover.image_handle)
        SDL_SetRenderDrawColor(app.sdl, 40, 100, 180, 255);
      else
        SDL_SetRenderDrawColor(app.sdl, 90, 150, 220, 255);
      xanatoo::sdl::draw_rect(app.sdl, &box);
      SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
      float tip_baseline =
          static_cast<float>(by + pad_y + app.ui_font.ascender());
      for (const auto& line : lines) {
        app.renderer.draw_text(app.ui_font, static_cast<float>(bx + pad_x),
                               tip_baseline, line, 235, 240, 250);
        tip_baseline += app.ui_font.line_height();
      }
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
  else if (!app.hover.href.empty() || !app.hover.bookmark.empty() ||
           !app.hover.annotation.empty() ||
           app.hover.stretch != xanatoo::INVALID_NODE ||
           app.hover.image != xanatoo::INVALID_NODE ||
           app.hover.task != xanatoo::INVALID_NODE || app.hover.chrome_notice ||
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
  apply_ui_scale(app);
}

void run_frame(App& app) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) handle_event(app, ev);
  update_touch(app);
  sync_nav_history(app);  // capture scroll before a completed fetch swaps pages
  poll_fetches(app);
  sync_nav_history(app);  // apply Back/Forward restore after remote loads
  autosave_drafts(app);
  autosave_session(app);
  update_split_preview(app);
  update_sitemap(app);
  update_page_panel(app);
  sync_window_size(app);
  relayout_if_needed(app);
  run_chrome(app);
  // UI commands (Open, Diff demo, New tab, …) often replace documents after
  // the first layout pass. Relayout before draw so glyphs/carets never
  // reference node ids from the previous document tree.
  // Stick scrolling may leave the materialised band: scroll before the
  // relayout so the exposed raster strip is drawn from a complete layout.
  poll_gamepad_axes(app);
  relayout_if_needed(app);
  apply_pending_scroll(app);
  poll_images(app);
  draw_panes(app);
  draw_split_chrome(app);
  draw_status_bar(app);
  draw_floats(app);
  draw_dock_preview(app);
  draw_hover_tooltip(app);
  // The gallery covers document chrome, but ImGui menus/windows must remain
  // on top so they can be seen and interacted with while the lightbox is open.
  draw_lightbox(app);
  present_frame(app);
}

}  // namespace xanatoo::app
