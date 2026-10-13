// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// App lifecycle: build the initial world state / tear down platform handles.

#include "xanatoo/app.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

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
    DocBuffer b0;
    {
      std::string r0 = resolve_open_path(app, p0);
      b0.path = r0.empty() ? p0 : r0;
      b0.doc = load_doc_or_placeholder(app, b0.path.empty() ? p0 : b0.path);
      // If EPUB resolve failed, still attempt load_doc on original for error page.
      if (xanatoo::is_epub_path(p0) && r0.empty())
        b0.doc = load_doc(app, p0);
    }
    app.buffers.push_back(std::move(b0));
    if (!p1.empty()) {
      DocBuffer b1;
      {
        std::string r1 = resolve_open_path(app, p1);
        b1.path = r1.empty() ? p1 : r1;
        b1.doc = load_doc_or_placeholder(app, b1.path);
        if (xanatoo::is_epub_path(p1) && r1.empty())
          b1.doc = load_doc(app, p1);
      }
      app.buffers.push_back(std::move(b1));
    }
  }
for (int i = 0; i < static_cast<int>(app.buffers.size()); ++i)
    app.tab_order.push_back(i);
update_title(app);
xanatoo::load_view_settings(app.view_settings);
app.layout_engine.set_options(xanatoo::LayoutOptions::from_view(app.view_settings));
if (!app.ui.init(app.window, app.sdl, app.opt.ui_scale)) {
    std::cerr << "Warning: ImGui UI failed to init; continuing without menus\n";
  }
app.panes[0].buf = 0;
app.panes[1].buf = (app.buffers.size() > 1) ? 1 : 0;
xanatoo::ensure_valid_caret(app.buffers[0].doc, app.panes[0].sel.focus);
app.panes[0].sel.anchor = app.panes[0].sel.focus;
xanatoo::ensure_valid_caret(app.buffers[static_cast<size_t>(app.panes[1].buf)].doc,
                              app.panes[1].sel.focus);
app.panes[1].sel.anchor = app.panes[1].sel.focus;
app.buffers[0].history.reset(app.buffers[0].doc, app.panes[0].sel);
if (app.buffers.size() > 1)
    app.buffers[1].history.reset(app.buffers[1].doc, app.panes[1].sel);
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
if (!app.buffers.empty() && is_in_app_remote_url(app.buffers[0].path)) {
    std::string u = app.buffers[0].path;
    app.buffers[0].path.clear();
    navigate_to_url(app, std::move(u));
  }
else if (app.buffers.size() > 1 && is_in_app_remote_url(app.buffers[1].path)) {
    std::string u = app.buffers[1].path;
    app.buffers[1].path.clear();
    app.focus = 1;
    navigate_to_url(app, std::move(u));
    app.focus = 0;
  }
  app.split = app.buffers.size() > 1;  // auto-split when two files given
  return true;
}

void app_shutdown(App& app) {
  SDL_StopTextInput();
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
