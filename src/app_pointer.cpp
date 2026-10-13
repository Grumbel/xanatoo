// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/app.hpp"

#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

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

namespace fs = std::filesystem;

namespace xanatoo::app {

void open_first_gamepad(App& app) {if (app.gamepad) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      if (SDL_IsGameController(i)) {
        app.gamepad = SDL_GameControllerOpen(i);
        if (app.gamepad) {
          const char* name = SDL_GameControllerName(app.gamepad);
          std::cout << "Gamepad: " << (name ? name : "(unknown)") << "\n";
          break;
        }
      }
    }
}

void clear_interaction_hover(App& app) {app.hover.href.clear();
    app.hover.rel.clear();
    app.hover.preview.clear();
    app.hover.is_cite = false;
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
}

void draw_stick_cursor(App& app, SDL_Renderer* r, int cx, int cy, bool pressed) {const int ox[7] = {0, 0, 4, 6, 9, 7, 11};
    const int oy[7] = {0, 14, 11, 17, 15, 10, 8};
    auto fill_tri = [&](int x0, int y0, int x1, int y1, int x2, int y2,
                        Uint8 R, Uint8 G, Uint8 B) {
      // Scanline fill for one triangle (no RenderGeometry needed).
      auto edge = [](int ax, int ay, int bx, int by, int y, int& xL, int& xR) {
        if (ay == by) return;
        if (ay > by) {
          std::swap(ax, bx);
          std::swap(ay, by);
        }
        if (y < ay || y > by) return;
        float t = (by == ay) ? 0.f : float(y - ay) / float(by - ay);
        int x = ax + int((bx - ax) * t);
        if (x < xL) xL = x;
        if (x > xR) xR = x;
      };
      int miny = std::min(y0, std::min(y1, y2));
      int maxy = std::max(y0, std::max(y1, y2));
      SDL_SetRenderDrawColor(r, R, G, B, 255);
      for (int y = miny; y <= maxy; ++y) {
        int xL = 100000, xR = -100000;
        edge(x0, y0, x1, y1, y, xL, xR);
        edge(x1, y1, x2, y2, y, xL, xR);
        edge(x2, y2, x0, y0, y, xL, xR);
        if (xL <= xR) SDL_RenderDrawLine(r, cx + xL, cy + y, cx + xR, cy + y);
      }
    };
    // Two triangles form the arrow head + stem-ish shape.
    const Uint8 fr = pressed ? 180 : 255;
    const Uint8 fg = pressed ? 200 : 255;
    const Uint8 fb = pressed ? 255 : 255;
    fill_tri(ox[0], oy[0], ox[1], oy[1], ox[2], oy[2], fr, fg, fb);
    fill_tri(ox[0], oy[0], ox[2], oy[2], ox[6], oy[6], fr, fg, fb);
    fill_tri(ox[2], oy[2], ox[5], oy[5], ox[6], oy[6], fr, fg, fb);
    fill_tri(ox[2], oy[2], ox[3], oy[3], ox[4], oy[4], fr, fg, fb);
    // Black outline
    SDL_SetRenderDrawColor(r, 20, 24, 32, 255);
    for (int i = 0; i < 7; ++i) {
      int j = (i + 1) % 7;
      // Outline only along silhouette edges (approximate)
      SDL_RenderDrawLine(r, cx + ox[i], cy + oy[i], cx + ox[j], cy + oy[j]);
    }
    // Explicit tip + left edge + bottom for a clearer outline
    SDL_RenderDrawLine(r, cx + 0, cy + 0, cx + 0, cy + 14);
    SDL_RenderDrawLine(r, cx + 0, cy + 0, cx + 11, cy + 8);
    SDL_RenderDrawLine(r, cx + 0, cy + 14, cx + 4, cy + 11);
}

void set_cursor(App& app, SDL_Cursor* c) {if (!c) c = app.cursor_arrow;
    if (c != app.cursor_current) {
      SDL_SetCursor(c);
      app.cursor_current = c;
    }
}

void reset_blink(App& app) {app.caret_blink_start = SDL_GetTicks();
}

}  // namespace xanatoo::app
