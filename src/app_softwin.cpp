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

std::tuple<int, int, int, int> soft_client_rect(App& app,
                                                const FloatWindow& sw) {
  return std::tuple{sw.x + help_pad, sw.y + help_title_h + 4,
                    sw.w - help_pad * 2, sw.h - help_title_h - help_pad - 4};
}

void snap_soft_geom(App& app, int& x, int& y, int w, int h) {
  const int top = static_cast<int>(app.ui_top);
  const int bottom = app.win_h - status_bar_h;
  // Title bar (help_title_h) must remain below the chrome.
  if (y < top) y = top;
  if (x < 0) x = 0;
  if (w > app.win_w)
    w = app.win_w;  // caller may not use w out-param; size clamped elsewhere
  if (x + w > app.win_w) x = std::max(0, app.win_w - w);
  // Prefer keeping the title bar on-screen even if the body is tall.
  if (y + help_title_h > bottom)
    y = std::max(top, bottom - help_title_h);
  else if (y + h > bottom)
    y = std::max(top, bottom - h);
  // Edge snap (optional stickiness near edges)
  if (x < soft_snap_px) x = 0;
  if (y < top + soft_snap_px) y = top;
  if (x + w > app.win_w - soft_snap_px) x = std::max(0, app.win_w - w);
  if (y + h > bottom - soft_snap_px) y = std::max(top, bottom - h);
}

std::tuple<int, int, int> soft_client_bounds(App& app) {
  const int top = static_cast<int>(app.ui_top);
  const int bottom = app.win_h - status_bar_h;
  return std::tuple{top, bottom, std::max(160, bottom - top)};
}

namespace {

int side_index(Dock side) { return side == Dock::Right ? 1 : 0; }

bool has_column(App& app, Dock side) {
  for (FloatWindow* w : float_stack(app))
    if (w->dock == side) return true;
  return false;
}

// Columns keep at least kMinPaneArea px for the panes between them.
constexpr int kMinPaneArea = 320;

int column_w(App& app, Dock side) {
  const Dock other = side == Dock::Left ? Dock::Right : Dock::Left;
  int limit = app.win_w * 2 / 5;
  if (has_column(app, other))
    limit = std::min(limit, (app.win_w - kMinPaneArea) / 2);
  return std::clamp(app.dock_w[side_index(side)], 160, std::max(160, limit));
}

// Visible windows docked on `side`, top to bottom.
std::vector<FloatWindow*> dock_column(App& app, Dock side) {
  std::vector<FloatWindow*> col;
  for (FloatWindow* w : float_stack(app))
    if (w->dock == side) col.push_back(w);
  std::stable_sort(col.begin(), col.end(),
                   [](const FloatWindow* a, const FloatWindow* b) {
                     return a->dock_seq < b->dock_seq;
                   });
  return col;
}

// Move / resize; a new width lays the window's content out again.
void set_geom(App& app, FloatWindow& w, int x, int y, int ww, int hh) {
  if (w.w != ww && w.role == WindowRole::Help) app.help_needs_layout = true;
  w.x = x;
  w.y = y;
  w.w = ww;
  w.h = hh;
}

void save_free_geom(FloatWindow& w) {
  if (w.dock != Dock::None) return;
  w.restore_x = w.x;
  w.restore_y = w.y;
  w.restore_w = w.w;
  w.restore_h = w.h;
}

}  // namespace

Dock compute_dock_zone(App& app, int mx, int my) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  (void)ch;
  if (my < top || my > bottom) return Dock::None;
  // Window edge, or anywhere over an existing column (stacks into it).
  if (mx < std::max(soft_dock_zone, dock_column_w(app, Dock::Left)))
    return Dock::Left;
  if (mx > app.win_w - std::max(soft_dock_zone, dock_column_w(app, Dock::Right)))
    return Dock::Right;
  if (my < top + soft_dock_zone && mx > app.win_w / 4 &&
      mx < (app.win_w * 3) / 4)
    return Dock::Max;
  return Dock::None;
}

std::tuple<int, int, int, int> dock_rect(App& app, Dock d) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  (void)bottom;
  switch (d) {
    case Dock::Left:
      return {0, top, column_w(app, Dock::Left), ch};
    case Dock::Right: {
      const int cw = column_w(app, Dock::Right);
      return {app.win_w - cw, top, cw, ch};
    }
    case Dock::Max:
      return {0, top, app.win_w, ch};
    case Dock::None:
      break;
  }
  return {0, top, 0, 0};
}

int dock_column_w(App& app, Dock side) {
  return has_column(app, side) ? column_w(app, side) : 0;
}

void dock_window(App& app, FloatWindow& w, Dock d, int my) {
  if (d == Dock::None) {
    undock_window(app, w);
    return;
  }
  save_free_geom(w);
  if (d == Dock::Max) {
    if (w.dock != Dock::Max) w.restore_dock = w.dock;
  } else {
    // Slot: below every docked window whose middle is above the drop point.
    auto col = dock_column(app, d);
    col.erase(std::remove(col.begin(), col.end(), &w), col.end());
    size_t at = 0;
    while (at < col.size() && col[at]->y + col[at]->h / 2 < my) ++at;
    col.insert(col.begin() + static_cast<std::ptrdiff_t>(at), &w);
    for (size_t i = 0; i < col.size(); ++i)
      col[i]->dock_seq = static_cast<int>(i);
    w.restore_dock = Dock::None;
  }
  w.dock = d;
  app.needs_layout = true;
  place_windows(app);
}

void undock_window(App& app, FloatWindow& w) {
  if (w.dock == Dock::None) return;
  w.dock = Dock::None;
  w.restore_dock = Dock::None;
  set_geom(app, w, w.restore_x, w.restore_y, w.restore_w, w.restore_h);
  snap_soft_geom(app, w.x, w.y, w.w, w.h);
  app.needs_layout = true;
}

void toggle_maximize(App& app, FloatWindow& w) {
  if (w.dock != Dock::Max) {
    dock_window(app, w, Dock::Max, 0);
  } else if (w.restore_dock == Dock::None) {
    undock_window(app, w);
  } else {
    w.dock = w.restore_dock;
    w.restore_dock = Dock::None;
    app.needs_layout = true;
    place_windows(app);
  }
}

void place_windows(App& app) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  (void)bottom;
  for (Dock side : {Dock::Left, Dock::Right}) {
    auto col = dock_column(app, side);
    auto [x, y, cw, h] = dock_rect(app, side);
    (void)y;
    (void)h;
    const int n = static_cast<int>(col.size());
    for (int i = 0; i < n; ++i) {
      const int y0 = top + ch * i / n;
      const int y1 = top + ch * (i + 1) / n;
      set_geom(app, *col[static_cast<size_t>(i)], x, y0, cw, y1 - y0);
    }
  }
  for (FloatWindow* w : float_stack(app)) {
    if (w->dock == Dock::Max) {
      auto [x, y, mw, mh] = dock_rect(app, Dock::Max);
      set_geom(app, *w, x, y, mw, mh);
    } else if (w->dock == Dock::None) {
      snap_soft_geom(app, w->x, w->y, w->w, w->h);
    }
  }
  // Document windows: the view fills the client area (a new size lays it
  // out again).
  for (FloatWindow& w : app.soft_wins) {
    if (!view_live(app, w.view)) continue;
    Pane& p = app.panes[w.view];
    auto [cx, cy, cw, ch] = soft_client_rect(app, w);
    if (p.w != cw || p.h != ch) app.needs_layout = true;
    p.x = cx;
    p.y = cy;
    p.w = cw;
    p.h = ch;
  }
}

const std::string& soft_path(App& app, const FloatWindow& sw) {
  const DocBuffer* b =
      view_live(app, sw.view) ? find_buf(app, app.panes[sw.view].buf) : nullptr;
  if (!b) {
    static const std::string empty;
    return empty;
  }
  return b->path;
}

std::string soft_title(App& app, const FloatWindow& sw) {
  if (!sw.title.empty()) return sw.title;
  const auto& path = soft_path(app, sw);
  if (path.empty()) return "untitled";
  return path_title(path);
}

void raise_soft(App& app, size_t i) {
  if (i + 1 >= app.soft_wins.size()) return;
  FloatWindow sw = std::move(app.soft_wins[i]);
  app.soft_wins.erase(app.soft_wins.begin() + static_cast<std::ptrdiff_t>(i));
  app.soft_wins.push_back(std::move(sw));
}

void close_doc_window(App& app, size_t i) {
  if (i >= app.soft_wins.size()) return;
  release_view(app, app.soft_wins[i].view);
  app.soft_wins.erase(app.soft_wins.begin() + static_cast<std::ptrdiff_t>(i));
  app.needs_layout = true;
}

FloatWindow* window_of_view(App& app, int pi) {
  if (pi < kCentreViews) return nullptr;
  for (FloatWindow& w : app.soft_wins)
    if (w.view == pi) return &w;
  return nullptr;
}

}  // namespace xanatoo::app
