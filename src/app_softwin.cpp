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
#include "xanatoo/dock_slices.hpp"
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
  return window_client_rect(sw.x, sw.y, sw.w, sw.h);
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

// Slot in App::dock_w: Left, Right, Top, Bottom.
int side_index(Dock side) {
  switch (side) {
    case Dock::Right: return 1;
    case Dock::Top: return 2;
    case Dock::Bottom: return 3;
    default: return 0;
  }
}

bool is_band(Dock d) { return d == Dock::Top || d == Dock::Bottom; }

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

// Bands keep at least kMinPaneRows px for the panes between them.
constexpr int kMinPaneRows = 160;

// Height of a shaded panel: the title strip and the frame line below it.
int shaded_h() { return help_title_h + 1; }

// Width of a shaded panel in a band: the three title controls plus a
// readable piece of the title.
int shaded_band_w() { return 8 * float_close_w; }

bool all_shaded(App& app, Dock side) {
  bool any = false;
  for (FloatWindow* w : float_stack(app)) {
    if (w->dock != side) continue;
    if (!float_shaded(*w)) return false;
    any = true;
  }
  return any;
}

int band_h(App& app, Dock side) {
  // A band whose panels are all rolled up gives the rest to the panes.
  if (all_shaded(app, side)) return shaded_h();
  const Dock other = side == Dock::Top ? Dock::Bottom : Dock::Top;
  auto [top, bottom, ch] = soft_client_bounds(app);
  (void)top;
  (void)bottom;
  int limit = ch * 2 / 5;
  if (has_column(app, other)) limit = std::min(limit, (ch - kMinPaneRows) / 2);
  return std::clamp(app.dock_w[side_index(side)], 80, std::max(80, limit));
}

// Visible windows docked on `side`: a column top to bottom, a band left to
// right.
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
  const int bh = dock_band_h(app, Dock::Bottom);
  const int th = dock_band_h(app, Dock::Top);
  // Window edge, or anywhere over an existing column (stacks into it).
  if (mx < std::max(soft_dock_zone, dock_column_w(app, Dock::Left)))
    return Dock::Left;
  if (mx > app.win_w - std::max(soft_dock_zone, dock_column_w(app, Dock::Right)))
    return Dock::Right;
  if (my > bottom - std::max(soft_dock_zone, bh)) return Dock::Bottom;
  if (my < top + std::max(soft_dock_zone, th)) {
    // The middle of the top edge maximizes, unless a band is there already.
    if (th == 0 && mx > app.win_w / 4 && mx < (app.win_w * 3) / 4)
      return Dock::Max;
    return Dock::Top;
  }
  return Dock::None;
}

std::tuple<int, int, int, int> dock_rect(App& app, Dock d) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  switch (d) {
    case Dock::Left:
      return {0, top, column_w(app, Dock::Left), ch};
    case Dock::Right: {
      const int cw = column_w(app, Dock::Right);
      return {app.win_w - cw, top, cw, ch};
    }
    case Dock::Top:
    case Dock::Bottom: {
      const int lw = dock_column_w(app, Dock::Left);
      const int bw = std::max(
          64, app.win_w - lw - dock_column_w(app, Dock::Right));
      const int bh = band_h(app, d);
      return {lw, d == Dock::Top ? top : bottom - bh, bw, bh};
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

int dock_band_h(App& app, Dock side) {
  return has_column(app, side) ? band_h(app, side) : 0;
}

void dock_window(App& app, FloatWindow& w, Dock d, int mx, int my) {
  if (d == Dock::None) {
    undock_window(app, w);
    return;
  }
  save_free_geom(w);
  if (d != w.dock) w.shaded = false;
  if (d != w.dock && d != Dock::Max)
    w.dock_share = 1.f;
  if (d == Dock::Max) {
    if (w.dock != Dock::Max) w.restore_dock = w.dock;
  } else {
    // Slot: below every docked window whose middle is above the drop point.
    auto col = dock_column(app, d);
    col.erase(std::remove(col.begin(), col.end(), &w), col.end());
    size_t at = 0;
    while (at < col.size() &&
           (is_band(d) ? col[at]->x + col[at]->w / 2 < mx
                       : col[at]->y + col[at]->h / 2 < my))
      ++at;
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
  w.shaded = false;
  w.restore_dock = Dock::None;
  set_geom(app, w, w.restore_x, w.restore_y, w.restore_w, w.restore_h);
  snap_soft_geom(app, w.x, w.y, w.w, w.h);
  app.needs_layout = true;
}

void toggle_shade(App& app, FloatWindow& w) {
  if (!float_docked(w)) return;
  w.shaded = !w.shaded;
  w.dragging = false;
  w.resize = 0;
  w.scroll_dragging = false;
  // A rolled-up panel has nothing to type into.
  if (w.shaded && app.key_focus == float_key_focus(w))
    app.key_focus = KeyFocus::Document;
  app.needs_layout = true;
  place_windows(app);
}

void toggle_maximize(App& app, FloatWindow& w) {
  if (w.dock != Dock::Max) {
    dock_window(app, w, Dock::Max, 0, 0);
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
  for (Dock side : {Dock::Top, Dock::Bottom}) {
    auto row = dock_column(app, side);
    auto [x, y, bw, bh] = dock_rect(app, side);
    const int n = static_cast<int>(row.size());
    std::vector<float> shares;
    std::vector<bool> shaded;
    for (const FloatWindow* w : row) {
      shares.push_back(w->dock_share);
      shaded.push_back(float_shaded(*w));
    }
    const auto edges =
        xanatoo::dock_slices_shaded(shares, shaded, bw, shaded_band_w());
    // A shaded panel keeps a narrow title strip at the band's top, in line
    // with its neighbours' titles.
    const bool all = all_shaded(app, side);
    for (int i = 0; i < n; ++i) {
      const size_t k = static_cast<size_t>(i);
      int x0 = x + edges[k];
      int x1 = x + edges[k + 1];
      // All rolled up: the strips share the band's width again.
      if (all) {
        const auto even = xanatoo::dock_slices(shares, bw);
        x0 = x + even[k];
        x1 = x + even[k + 1];
      }
      const int h = shaded[k] ? std::min(bh, shaded_h()) : bh;
      set_geom(app, *row[k], x0, y, x1 - x0, h);
    }
  }
  for (Dock side : {Dock::Left, Dock::Right}) {
    auto col = dock_column(app, side);
    auto [x, y, cw, h] = dock_rect(app, side);
    (void)y;
    (void)h;
    const int n = static_cast<int>(col.size());
    std::vector<float> shares;
    std::vector<bool> shaded;
    for (const FloatWindow* w : col) {
      shares.push_back(w->dock_share);
      shaded.push_back(float_shaded(*w));
    }
    const auto edges =
        xanatoo::dock_slices_shaded(shares, shaded, ch, shaded_h());
    for (int i = 0; i < n; ++i) {
      const int y0 = top + edges[static_cast<size_t>(i)];
      const int y1 = top + edges[static_cast<size_t>(i + 1)];
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
    const int ph = std::max(1, ch - pane_bar_h(app, p));  // page bar below
    if (p.w != cw || p.h != ph) app.needs_layout = true;
    p.x = cx;
    p.y = cy;
    p.w = cw;
    p.h = ph;
  }
}

namespace {

struct DockSplitHit {
  Dock side = Dock::None;
  size_t first = 0;
};

DockSplitHit find_dock_split(App& app, int mx, int my) {
  // A free/maximized window in front covers the separator behind it.
  FloatWindow* front = float_at(app, mx, my);
  for (Dock side : {Dock::Left, Dock::Right, Dock::Top, Dock::Bottom}) {
    if (front && front->dock != side) continue;
    const auto col = dock_column(app, side);
    const bool horizontal = is_band(side);  // left/right neighbours
    for (size_t i = 0; i + 1 < col.size(); ++i) {
      const FloatWindow& a = *col[i];
      if (float_shaded(a) || float_shaded(*col[i + 1])) continue;
      const int edge = horizontal ? a.x + a.w : a.y + a.h;
      const int along = horizontal ? mx : my;
      const int cross = horizontal ? my : mx;
      const int first_cross = horizontal ? a.y : a.x;
      const int length = horizontal ? a.h : a.w;
      if (std::abs(along - edge) <= help_edge &&
          cross >= first_cross + help_edge &&
          cross < first_cross + length - help_edge)
        return {side, i};
    }
  }
  return {};
}

}  // namespace

DockSplitAxis dock_split_at(App& app, int mx, int my) {
  const Dock side = find_dock_split(app, mx, my).side;
  if (side == Dock::Top || side == Dock::Bottom)
    return DockSplitAxis::Horizontal;
  if (side == Dock::Left || side == Dock::Right)
    return DockSplitAxis::Vertical;
  return DockSplitAxis::None;
}

bool dock_split_press(App& app, int mx, int my) {
  const DockSplitHit hit = find_dock_split(app, mx, my);
  if (hit.side == Dock::None) return false;
  const auto col = dock_column(app, hit.side);
  const bool horizontal = is_band(hit.side);
  const FloatWindow& a = *col[hit.first];
  const FloatWindow& b = *col[hit.first + 1];
  app.dock_split_drag = {hit.side, hit.first, horizontal ? mx : my,
                         horizontal ? a.w : a.h,
                         horizontal ? b.w : b.h,
                         a.dock_share, b.dock_share};
  return true;
}

bool dock_split_motion(App& app, int mx, int my) {
  const auto& drag = app.dock_split_drag;
  if (drag.side == Dock::None) return false;
  const auto col = dock_column(app, drag.side);
  if (drag.first + 1 >= col.size()) {
    dock_split_release(app);
    return false;
  }
  const int point = is_band(drag.side) ? mx : my;
  auto [a, b] = xanatoo::dock_resize_pair(
      drag.first_share, drag.second_share, drag.first_px, drag.second_px,
      point - drag.start);
  col[drag.first]->dock_share = a;
  col[drag.first + 1]->dock_share = b;
  app.needs_layout = true;
  place_windows(app);
  return true;
}

void dock_split_release(App& app) {
  app.dock_split_drag = {};
}

bool over_tab_strip(App& app, int x, int y) {
  if (app.tab_order.empty() || x < 0 || x >= app.win_w) return false;
  const int h = static_cast<int>(28.f * app.ui_scale);
  const int bottom = static_cast<int>(app.ui_top);
  return y >= bottom - h && y < bottom;
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
