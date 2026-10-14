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

SoftDock compute_dock_zone(App& app, int mx, int my) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  (void)ch;
  if (my < top || my > bottom) return SoftDock::None;
  // Corners prefer left/right over top/bottom for split layouts.
  if (mx < soft_dock_zone) return SoftDock::Left;
  if (mx > app.win_w - soft_dock_zone) return SoftDock::Right;
  // Top-center → maximize; top sides already handled as L/R
  if (my < top + soft_dock_zone) {
    if (mx > app.win_w / 4 && mx < (app.win_w * 3) / 4) return SoftDock::Max;
    return SoftDock::Top;
  }
  if (my > bottom - soft_dock_zone) return SoftDock::Bottom;
  return SoftDock::None;
}

std::tuple<int, int, int, int> dock_rect(App& app, SoftDock d) {
  auto [top, bottom, ch] = soft_client_bounds(app);
  switch (d) {
    case SoftDock::Left:
      return {0, top, std::max(200, app.win_w / 2), ch};
    case SoftDock::Right: {
      int x = app.win_w / 2;
      return {x, top, app.win_w - x, ch};
    }
    case SoftDock::Top:
      return {0, top, app.win_w, std::max(120, ch / 2)};
    case SoftDock::Bottom: {
      int h = std::max(120, ch / 2);
      return {0, bottom - h, app.win_w, h};
    }
    case SoftDock::Max:
      return {0, top, app.win_w, ch};
    default:
      return {0, top, 0, 0};
  }
}

void apply_soft_dock(App& app, FloatWindow& sw, SoftDock d) {
  if (d == SoftDock::None) return;
  // Save floating geometry once when leaving free placement
  if (sw.dock == SoftDock::None && !sw.maximized) {
    sw.restore_x = sw.x;
    sw.restore_y = sw.y;
    sw.restore_w = sw.w;
    sw.restore_h = sw.h;
  }
  auto [x, y, w, h] = dock_rect(app, d);
  sw.x = x;
  sw.y = y;
  sw.w = w;
  sw.h = h;
  sw.dock = d;
  sw.maximized = (d == SoftDock::Max);
  sw.needs_layout = true;
}

void restore_soft_float(App& app, FloatWindow& sw, int mx, int my) {
  if (sw.dock == SoftDock::None && !sw.maximized) return;
  sw.w = sw.restore_w;
  sw.h = sw.restore_h;
  sw.x = mx - sw.w / 2;
  sw.y = my - help_title_h / 2;
  if (sw.x < 0) sw.x = 0;
  if (sw.y < static_cast<int>(app.ui_top)) sw.y = static_cast<int>(app.ui_top);
  sw.dock = SoftDock::None;
  sw.maximized = false;
  sw.needs_layout = true;
}

void maximize_soft(App& app, FloatWindow& sw) {
  if (!sw.maximized && sw.dock == SoftDock::None) {
    sw.restore_x = sw.x;
    sw.restore_y = sw.y;
    sw.restore_w = sw.w;
    sw.restore_h = sw.h;
    apply_soft_dock(app, sw, SoftDock::Max);
  } else if (sw.maximized || sw.dock != SoftDock::None) {
    sw.x = sw.restore_x;
    sw.y = sw.restore_y;
    sw.w = sw.restore_w;
    sw.h = sw.restore_h;
    sw.maximized = false;
    sw.dock = SoftDock::None;
    sw.needs_layout = true;
    snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
  }
}

void clamp_all_floating(App& app) {
  const int top = static_cast<int>(app.ui_top);
  for (auto& sw : app.soft_wins) {
    if (sw.dock != SoftDock::None || sw.maximized) {
      SoftDock d = sw.maximized ? SoftDock::Max : sw.dock;
      auto [x, y, w, h] = dock_rect(app, d);
      // Only rewrite geometry when chrome/size changed (avoids needs_layout
      // spam).
      if (sw.x != x || sw.y != y || sw.w != w || sw.h != h) {
        sw.x = x;
        sw.y = y;
        sw.w = w;
        sw.h = h;
        sw.needs_layout = true;
      }
      continue;
    }
    snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
    if (sw.y < top) sw.y = top;
  }
  snap_soft_geom(app, app.help.x, app.help.y, app.help.w, app.help.h);
  if (app.help.y < top) app.help.y = top;
  snap_soft_geom(app, app.ol.x, app.ol.y, app.ol.w, app.ol.h);
  if (app.ol.y < top) app.ol.y = top;
}

xanatoo::Document& soft_doc(App& app, FloatWindow& sw) {
  return buf(app, sw.buf).doc;
}

const std::string& soft_path(App& app, const FloatWindow& sw) {
  const DocBuffer* b = find_buf(app, sw.buf);
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
  try {
    return fs::path(path).filename().string();
  } catch (...) {
    return path;
  }
}

void layout_soft(App& app, FloatWindow& sw) {
  float mw = static_cast<float>(std::max(120, sw.w - help_pad * 2));
  std::string base = parent_dir(soft_path(app, sw));
  try {
    xanatoo::Document& d = soft_doc(app, sw);
    xanatoo::ensure_valid_caret(d, sw.sel.focus);
    sw.sel.anchor = sw.sel.focus;
    sw.layout = app.layout_engine.layout(d, mw, base);
  } catch (const std::exception& e) {
    std::cerr << "soft layout failed: " << e.what() << "\n";
    sw.layout = {};
  }
  sw.needs_layout = false;
}

void raise_soft(App& app, size_t i) {
  if (i + 1 >= app.soft_wins.size()) return;
  FloatWindow sw = std::move(app.soft_wins[i]);
  app.soft_wins.erase(app.soft_wins.begin() + static_cast<std::ptrdiff_t>(i));
  app.soft_wins.push_back(std::move(sw));
}

}  // namespace xanatoo::app
