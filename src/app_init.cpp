// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// App lifecycle: build the initial world state / tear down platform handles.

#include <cstdlib>
#include <filesystem>
#include <iostream>

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

bool app_init(App& app) {
  std::string font_path = app.opt.font_path;
  if (font_path.empty()) {
    if (const char* env = std::getenv("XANATOO_FONT")) {
      if (env[0] != '\0') font_path = env;
    }
  }
  // Preferred path is optional — FontConfig resolves a full family set.
  if (!font_path.empty() && !fs::exists(font_path)) {
    std::cerr << "Font file does not exist: " << font_path
              << " (falling back to FontConfig)\n";
    font_path.clear();
  }
  if (!app.fonts.load(app.sdl, font_path, app.body_px, app.heading_px)) {
    std::cerr << "Failed to load fonts via FontConfig. "
                 "Pass a TTF with --font or install DejaVu/Liberation.\n";
    return false;
  }
  app.cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  app.cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
  app.cursor_sizewe = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
  app.cursor_sizenwse = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
  app.cursor_sizeall = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
  app.cursor_current = app.cursor_arrow;
  app.caret_blink_start = SDL_GetTicks();
  app.minibuf_hist_path = load_minibuffer_history("path");
  app.minibuf_hist_url = load_minibuffer_history("url");
  app.bookmarks = load_bookmarks();

  {
    std::string p0 = app.opt.file_path;
    std::string p1 = app.opt.file_path2;
    if (p0.empty()) {
      p0 = xanatoo::find_default_document();
      if (p0.empty())
        std::cerr << "Note: examples/welcome.md not found; starting empty.\n";
    }
    std::string b0_path;
    xanatoo::Document b0_doc;
    {
      std::string r0 = resolve_open_path(app, p0);
      b0_path = r0.empty() ? p0 : r0;
      b0_doc = load_doc_or_placeholder(app, b0_path.empty() ? p0 : b0_path);
      // If EPUB resolve failed, still attempt load_doc on original for error
      // page.
      if (xanatoo::is_epub_path(p0) && r0.empty()) b0_doc = load_doc(app, p0);
    }
    add_buffer(app, std::move(b0_doc), std::move(b0_path));
    if (!p1.empty()) {
      std::string b1_path;
      xanatoo::Document b1_doc;
      {
        std::string r1 = resolve_open_path(app, p1);
        b1_path = r1.empty() ? p1 : r1;
        b1_doc = load_doc_or_placeholder(app, b1_path);
        if (xanatoo::is_epub_path(p1) && r1.empty()) b1_doc = load_doc(app, p1);
      }
      add_buffer(app, std::move(b1_doc), std::move(b1_path));
    }
  }
  for (const auto& b : app.buffers) app.tab_order.push_back(b.id);
  update_title(app);
  xanatoo::load_view_settings(app.view_settings);
  app.layout_engine.set_options(
      xanatoo::LayoutOptions::from_view(app.view_settings));
  if (!app.ui.init(app.window, app.sdl, app.opt.ui_scale)) {
    std::cerr << "Warning: ImGui UI failed to init; continuing without menus\n";
  }
  app.panes[0].buf = app.buffers.front().id;
  app.panes[1].buf = app.buffers.size() > 1 ? app.buffers.back().id
                                            : app.buffers.front().id;
  for (int pi = 0; pi < 2; ++pi) {
    Pane& p = app.panes[pi];
    xanatoo::ensure_valid_caret(buf(app, p.buf).doc, p.sel.focus);
    p.sel.anchor = p.sel.focus;
    buf(app, p.buf).history.reset(buf(app, p.buf).doc, p.sel);
  }
  {
    int nw = 0, nh = 0;
    SDL_GetWindowSize(app.window, &nw, &nh);
    if (nw >= 64 && nh >= 64) {
      app.win_w = nw;
      app.win_h = nh;
    }
  }
  if (app.win_w > 0 && app.win_w <= 720 && app.body_px < 20) {
    app.body_px = 20;
    app.heading_px = 28;
    app.fonts.set_sizes(app.body_px, app.heading_px);
  }
  open_first_gamepad(app);
  if (app.gamepad) app.stick_cursor_on = true;
  for (const auto& b : app.buffers) {
    if (!b.path.empty()) push_nav_history(app, b.path);
  }
  // A remote URL given on the command line loads through the navigator
  // (the pane's buffer starts as a "Loading…" placeholder).
  for (int pi = 0; pi < (app.buffers.size() > 1 ? 2 : 1); ++pi) {
    DocBuffer& b = buf(app, app.panes[pi].buf);
    if (!is_in_app_remote_url(b.path)) continue;
    std::string u = std::move(b.path);
    b.path.clear();
    app.focus = pi;
    navigate_to_url(app, std::move(u));
    break;
  }
  app.focus = 0;
  app.split = app.buffers.size() > 1;  // auto-split when two files given
  return true;
}

void app_shutdown(App& app) {
  SDL_StopTextInput();
  release_pane_rasters(app);
  app.ui.shutdown();
  if (app.gamepad) {
    SDL_GameControllerClose(app.gamepad);
    app.gamepad = nullptr;
  }
  SDL_Cursor** cursors[] = {&app.cursor_arrow, &app.cursor_hand,
                            &app.cursor_sizewe, &app.cursor_sizenwse,
                            &app.cursor_sizeall};
  for (SDL_Cursor** c : cursors) {
    if (*c) SDL_FreeCursor(*c);
    *c = nullptr;
  }
}

}  // namespace xanatoo::app
