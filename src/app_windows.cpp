// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Floating windows (help / outline / backlinks / document): one z-ordered
// stack with shared hit-testing, drag, resize, scroll and draw dispatch.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "xanatoo/app.hpp"
#include "xanatoo/cli.hpp"
#include "xanatoo/layout.hpp"

namespace xanatoo::app {

namespace {


// Smallest free-floating size per role.
std::pair<int, int> min_size(const FloatWindow& w) {
  switch (w.role) {
    case WindowRole::Outline:
    case WindowRole::Sitemap:
      return {200, 140};
    case WindowRole::Backlinks:
      return {200, 120};
    case WindowRole::Help:
    case WindowRole::Document:
      break;
  }
  return {280, 160};
}

bool docked(const FloatWindow& w) {
  return w.dock == Dock::Left || w.dock == Dock::Right;
}

// Pointer travel before a title press on a docked window tears it off.
constexpr int kTearOffPx = 8;

bool contains(const FloatWindow& w, int mx, int my) {
  return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h;
}

// Backlinks rows live in a padded client area just below the title.
std::tuple<int, int, int, int> backlinks_client_rect(const FloatWindow& w) {
  constexpr int pad = 8;
  return {w.x + pad, w.y + help_title_h + 4, w.w - pad * 2,
          w.h - help_title_h - pad - 4};
}

float scroll_wheel_step(const FloatWindow& w) {
  return w.role == WindowRole::Help ? 48.f : 40.f;
}

void clamp_scroll_min(FloatWindow& w) {
  if (w.scroll < 0.f) w.scroll = 0.f;
}

void close_window(App& app, FloatWindow& w) {
  w.dragging = false;
  w.resize = 0;
  w.scroll_dragging = false;
  switch (w.role) {
    case WindowRole::Help:
      app.help_mode = false;
      break;
    case WindowRole::Outline:
      app.outline_mode = false;
      break;
    case WindowRole::Backlinks:
      app.backlinks_mode = false;
      break;
    case WindowRole::Sitemap:
      toggle_sitemap(app);
      break;
    case WindowRole::Document:
      close_doc_window(app, static_cast<size_t>(&w - app.soft_wins.data()));
      break;
  }
}

void begin_resize(FloatWindow& w, int bits, int mx, int my) {
  w.resize = bits;
  w.resize_start_x = mx;
  w.resize_start_y = my;
  w.resize_start_w = w.w;
  w.resize_start_h = w.h;
  w.dragging = false;
}

void begin_drag(FloatWindow& w, int mx, int my) {
  w.dragging = true;
  w.drag_dx = mx - w.x;
  w.drag_dy = my - w.y;
  w.press_x = mx;
  w.press_y = my;
}

// Jump the scroll offset to the click position on the scrollbar strip.
void begin_scroll_drag(App& app, FloatWindow& w, int my) {
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const float max_s = float_max_scroll(app, w);
  const float view_h = static_cast<float>(std::max(1, ch));
  float ratio = static_cast<float>(my - cy) / view_h;
  ratio = std::max(0.f, std::min(1.f, ratio));
  w.scroll = ratio * max_s;
  w.scroll_dragging = true;
}

// ---- role-specific client actions ----

void click_backlinks(App& app, FloatWindow& w, int my) {
  const float step = backlinks_row_step(app);
  auto [cx, cy, cw, ch] = backlinks_client_rect(w);
  const int row =
      static_cast<int>((static_cast<float>(my - cy) + w.scroll) / step);
  if (row < 0 || row >= static_cast<int>(app.backlinks.size())) return;
  app.backlinks_sel = row;
  auto& pp = focused_pane(app);
  pp.sel.focus = {app.backlinks[static_cast<size_t>(row)].source, 0, 0};
  pp.sel.anchor = pp.sel.focus;
  expand_ancestors_for_node(app, pp.sel.focus.node);
  scroll_caret_into_view(app, pp);
  reset_blink(app);
  app.key_focus = KeyFocus::Document;
}

void click_outline(App& app, FloatWindow& w, int mx, int my) {
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;
  const float step = outline_row_step(app);
  const int row =
      static_cast<int>((static_cast<float>(my - cy) + w.scroll) / step);
  if (row < 0 || row >= static_cast<int>(app.outline.size())) return;
  app.outline_sel = row;
  const auto& er = app.outline[static_cast<size_t>(row)];
  // Disclosure hit only on the triangle (a little slack), not the text.
  const int local_x = mx - cx;
  const OutlineRowGeom g = outline_row_geom(app, er.level);
  const bool on_twist = er.has_children && local_x >= g.twist_x - 4 &&
                        local_x < g.twist_x + g.twist_w + 2;
  if (on_twist && er.section != xanatoo::INVALID_NODE) {
    toggle_section_fold(app, er.section);
  } else {
    jump_outline_sel(app);
    app.key_focus = KeyFocus::Document;  // clicked a heading: edit there
  }
}

// Stretch toggle / link follow inside the Help client area.
void click_help(App& app, FloatWindow& w, int mx, int my) {
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const float lx = static_cast<float>(mx - cx);
  const float ly = static_cast<float>(my - cy);
  if (lx < 0 || ly < 0 || lx > cw || ly > ch) return;
  if (auto sid = xanatoo::hit_test_stretch(app.help_layout, lx, ly, w.scroll)) {
    if (app.help_doc.get(*sid).kind == xanatoo::NodeKind::Stretch) {
      app.help_doc.get(*sid).expanded = !app.help_doc.get(*sid).expanded;
      app.help_needs_layout = true;
      return;
    }
  }
  if (auto href = xanatoo::hit_test_link(app.help_doc, app.help_layout, lx, ly,
                                         w.scroll)) {
    if (!href->empty() && (*href)[0] == '#') {
      if (auto dest = xanatoo::jump_to_href(app.help_doc, *href)) {
        auto cpos = xanatoo::caret_screen_pos(
            app.help_layout, *dest, 0.f,
            static_cast<float>(app.fonts.regular.line_height()));
        if (cpos.valid) w.scroll = std::max(0.f, cpos.y - 24.f);
      }
    }
  }
}

void press_title(App& app, FloatWindow& w, int mx, int my) {
  if (mx >= w.x + w.w - float_close_w) {
    close_window(app, w);
    return;
  }
  if (w.role == WindowRole::Sitemap) {
    // "Page" button just left of ×.
    const SDL_Rect pb =
        title_button_rect(app, SDL_Rect{w.x, w.y, w.w, w.h}, "Page");
    if (mx >= pb.x - 2 && mx < pb.x + pb.w + 2) {
      click_sitemap(app, mx, my);
      return;
    }
  }
  if (w.role == WindowRole::Outline) {
    // Focus toggle sits just left of ×.
    const SDL_Rect fb = outline_focus_rect(app, SDL_Rect{w.x, w.y, w.w, w.h});
    if (mx >= fb.x - 2 && mx < fb.x + fb.w + 2) {
      toggle_section_focus_mode(app);
      return;
    }
  }
  // Double-click maximizes / restores; a drag moves the window (a docked
  // or maximized one tears off first, see float_motion).
  const Uint32 now = SDL_GetTicks();
  if (w.last_title_click != 0 && now - w.last_title_click < 400) {
    toggle_maximize(app, w);
    w.last_title_click = 0;
    return;
  }
  w.last_title_click = now;
  begin_drag(w, mx, my);
  app.dock_preview = Dock::None;
}

// Docked / maximized window dragged by its title: float it again at its
// free size, under the pointer.
void tear_off(App& app, FloatWindow& w, int mx, int my) {
  undock_window(app, w);
  w.x = std::max(0, mx - w.w / 2);
  w.y = std::max(static_cast<int>(app.ui_top), my - help_title_h / 2);
  w.drag_dx = mx - w.x;
  w.drag_dy = my - w.y;
}

void press_client(App& app, FloatWindow& w, int mx, int my) {
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  constexpr int sb_w = 10;
  // Scrollbar strip: Outline and Backlinks rows span it, so only the
  // content-scroll roles reserve it for click-free jump/drag.
  const bool in_sb =
      mx >= cx + cw - sb_w && mx <= cx + cw + 2 && my >= cy && my <= cy + ch;
  if (in_sb) {
    begin_scroll_drag(app, w, my);
    return;
  }
  switch (w.role) {
    case WindowRole::Backlinks:
      click_backlinks(app, w, my);
      break;
    case WindowRole::Outline:
      click_outline(app, w, mx, my);
      break;
    case WindowRole::Sitemap:
      click_sitemap(app, mx, my);
      break;
    case WindowRole::Help:
      click_help(app, w, mx, my);
      break;
    case WindowRole::Document:
      break;  // the view takes its clicks through the pane path
  }
}

}  // namespace

std::vector<FloatWindow*> float_stack(App& app) {
  std::vector<FloatWindow*> st;
  if (app.outline_mode) st.push_back(&app.ol);
  if (app.backlinks_mode) st.push_back(&app.bl);
  if (app.sitemap.shown) st.push_back(&app.sm);
  if (app.help_mode) st.push_back(&app.help);
  for (auto& sw : app.soft_wins) st.push_back(&sw);
  // Docked columns under floating windows, maximized ones on top.
  auto layer = [](const FloatWindow* w) {
    return w->dock == Dock::None ? 1 : w->dock == Dock::Max ? 2 : 0;
  };
  std::stable_sort(st.begin(), st.end(),
                   [&](const FloatWindow* a, const FloatWindow* b) {
                     return layer(a) < layer(b);
                   });
  return st;
}

FloatWindow* float_at(App& app, int x, int y) {
  auto st = float_stack(app);
  for (auto it = st.rbegin(); it != st.rend(); ++it)
    if (contains(**it, x, y)) return *it;
  return nullptr;
}

std::tuple<int, int, int, int> float_client_rect(App& app,
                                                 const FloatWindow& w) {
  switch (w.role) {
    case WindowRole::Help:
      return help_client_rect(app);
    case WindowRole::Outline:
      return outline_client_rect(app);
    case WindowRole::Backlinks:
      return backlinks_client_rect(w);
    case WindowRole::Sitemap:
      return sitemap_client_rect(app);
    case WindowRole::Document:
      return soft_client_rect(app, w);
  }
  return {0, 0, 0, 0};
}

float float_max_scroll(App& app, const FloatWindow& w) {
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const float view_h = static_cast<float>(ch);
  switch (w.role) {
    case WindowRole::Help:
      return std::max(0.f, app.help_layout.content_height - view_h + 40.f);
    case WindowRole::Document:
      return view_live(app, w.view) ? pane_max_scroll(app, app.panes[w.view])
                                    : 0.f;
    case WindowRole::Outline:
      return std::max(
          0.f, static_cast<float>(app.outline.size()) * outline_row_step(app) +
                   8.f - view_h);
    case WindowRole::Backlinks:
      return std::max(0.f, static_cast<float>(app.backlinks.size()) *
                                   backlinks_row_step(app) +
                               8.f - view_h);
    case WindowRole::Sitemap:
      return sitemap_max_scroll(app);
  }
  return 0.f;
}

bool float_press(App& app, int mx, int my) {
  auto st = float_stack(app);
  for (auto it = st.rbegin(); it != st.rend(); ++it) {
    FloatWindow* w = *it;
    if (mx < w->x - help_edge || mx > w->x + w->w + help_edge ||
        my < w->y - help_edge || my > w->y + w->h + help_edge)
      continue;
    const bool inside = contains(*w, mx, my);
    // Free windows resize at right / bottom; a docked one only at the
    // edge facing the panes (the column width); maximized ones not at all.
    int bits = 0;
    const bool on_r = mx >= w->x + w->w - help_edge &&
                      my >= w->y + help_title_h &&
                      my <= w->y + w->h + help_edge;
    const bool on_l = mx <= w->x + help_edge && my >= w->y + help_title_h &&
                      my <= w->y + w->h;
    const bool on_b = my >= w->y + w->h - help_edge && mx >= w->x &&
                      mx <= w->x + w->w + help_edge;
    switch (w->dock) {
      case Dock::None:
        bits = (on_r ? 1 : 0) | (on_b ? 2 : 0);
        break;
      case Dock::Left:
        bits = on_r ? 1 : 0;
        break;
      case Dock::Right:
        bits = on_l ? 4 : 0;
        break;
      case Dock::Max:
        break;
    }
    if (!bits && !inside) continue;
    // Clicking a panel gives it the keyboard; any other window, the document.
    app.key_focus = w->role == WindowRole::Outline     ? KeyFocus::Outline
                    : w->role == WindowRole::Backlinks ? KeyFocus::Backlinks
                    : w->role == WindowRole::Sitemap   ? KeyFocus::Sitemap
                                                       : KeyFocus::Document;
    if (w->role == WindowRole::Document) {
      // Raise to top of the document stack; `w` is stale afterwards.
      const size_t idx = static_cast<size_t>(w - app.soft_wins.data());
      raise_soft(app, idx);
      w = &app.soft_wins.back();
      // Content clicks belong to the view (caret, links, selection, its
      // scrollbar): focus it and let the pane path handle the press.
      if (!bits && my >= w->y + help_title_h &&
          pane_at(app, static_cast<float>(mx), static_cast<float>(my)) ==
              w->view) {
        focus_view(app, w->view);
        return false;
      }
    }
    if (bits) {
      begin_resize(*w, bits, mx, my);
      return true;
    }
    if (my < w->y + help_title_h)
      press_title(app, *w, mx, my);
    else
      press_client(app, *w, mx, my);
    return true;
  }
  return false;
}

bool float_motion(App& app, int mx, int my) {
  for (FloatWindow* w : float_stack(app)) {
    if (w->dragging) {
      if (w->dock != Dock::None) {
        if (std::abs(mx - w->press_x) + std::abs(my - w->press_y) < kTearOffPx)
          return true;
        tear_off(app, *w, mx, my);
      }
      w->x = std::max(0, mx - w->drag_dx);
      w->y = std::max(static_cast<int>(app.ui_top), my - w->drag_dy);
      app.dock_preview = compute_dock_zone(app, mx, my);
      return true;
    }
    if (w->scroll_dragging) {
      auto [cx, cy, cw, ch] = float_client_rect(app, *w);
      const float view_h = static_cast<float>(std::max(1, ch));
      float ratio = (static_cast<float>(my) - static_cast<float>(cy)) / view_h;
      ratio = std::max(0.f, std::min(1.f, ratio));
      w->scroll = ratio * float_max_scroll(app, *w);
      return true;
    }
    if (w->resize) {
      const int dx = mx - w->resize_start_x;
      const int dy = my - w->resize_start_y;
      if (docked(*w)) {
        // Column width, shared by every window docked on that side.
        int& cw = app.dock_w[w->dock == Dock::Right ? 1 : 0];
        const int nw = w->resize_start_w + (w->resize & 4 ? -dx : dx);
        cw = std::clamp(nw, 160, std::max(160, app.win_w * 2 / 5));
        app.needs_layout = true;
        place_windows(app);
        return true;
      }
      auto [min_w, min_h] = min_size(*w);
      if (w->resize & 1) {
        w->w = std::clamp(w->resize_start_w + dx, min_w,
                          std::max(min_w, app.win_w - 20));
        if (w->role == WindowRole::Help) app.help_needs_layout = true;
      }
      if (w->resize & 2)
        w->h = std::clamp(w->resize_start_h + dy, min_h,
                          std::max(min_h, app.win_h - 40));
      return true;
    }
  }
  return false;
}

void float_release(App& app, int mx, int my) {
  for (FloatWindow* w : float_stack(app)) {
    // A press on a docked title that never moved keeps it docked.
    if (w->dragging && w->dock == Dock::None) {
      const Dock zone = compute_dock_zone(app, mx, my);
      if (zone != Dock::None)
        dock_window(app, *w, zone, my);
      else
        snap_soft_geom(app, w->x, w->y, w->w, w->h);
    } else if (w->resize && w->dock == Dock::None) {
      snap_soft_geom(app, w->x, w->y, w->w, w->h);
    }
    w->dragging = false;
    w->resize = 0;
    w->scroll_dragging = false;
  }
  app.dock_preview = Dock::None;
}

bool float_wheel(App& app, int mx, int my, int wheel_y) {
  FloatWindow* w = float_at(app, mx, my);
  if (!w || w->role == WindowRole::Document) return false;  // pane scrolls
  w->scroll -= static_cast<float>(wheel_y) * scroll_wheel_step(*w);
  clamp_scroll_min(*w);
  return true;
}

bool float_page_scroll(App& app, int mx, int my, bool up) {
  FloatWindow* w = float_at(app, mx, my);
  if (!w || w->role == WindowRole::Document) return false;
  auto [cx, cy, cw, ch] = float_client_rect(app, *w);
  const float view_h = static_cast<float>(std::max(1, ch));
  const float max_s = float_max_scroll(app, *w);
  const float step = std::max(1.f, view_h) * 0.85f;
  w->scroll =
      up ? std::max(0.f, w->scroll - step) : std::min(max_s, w->scroll + step);
  return true;
}

bool over_float_chrome(App& app, int x, int y) {
  FloatWindow* w = float_at(app, x, y);
  if (!w) return false;
  return w->role != WindowRole::Document ||
         pane_at(app, static_cast<float>(x), static_cast<float>(y)) != w->view;
}

bool close_top_float(App& app) {
  auto st = float_stack(app);
  if (st.empty()) return false;
  close_window(app, *st.back());
  return true;
}

// Frame, then the view through the ordinary pane drawing (caret, overlays,
// raster, footnotes, scrollbar).
static void draw_soft_window(App& app, FloatWindow& sw) {
  if (sw.dock == Dock::None) {
    sw.w = std::max(sw.w, 280);
    sw.h = std::max(sw.h, 200);
  }
  draw_floating_frame(app, SDL_Rect{sw.x, sw.y, sw.w, sw.h},
                      soft_title(app, sw), app.focus == sw.view,
                      sw.dock == Dock::None);
  if (!view_live(app, sw.view)) return;
  const Pane& p = app.panes[sw.view];
  SDL_Rect clip{p.x, p.y, p.w, p.h};
  SDL_RenderSetClipRect(app.sdl, &clip);
  fill_doc_paper(app, clip);
  draw_pane(app, sw.view);
  SDL_RenderSetClipRect(app.sdl, nullptr);
}

void draw_floats(App& app) {
  place_windows(app);
  for (size_t i = app.soft_wins.size(); i-- > 0;)
    if (!find_buf(app, app.panes[app.soft_wins[i].view].buf))
      close_doc_window(app, i);
  for (FloatWindow* w : float_stack(app)) {
    switch (w->role) {
      case WindowRole::Outline:
        draw_outline_panel(app);
        break;
      case WindowRole::Backlinks:
        draw_backlinks_panel(app);
        break;
      case WindowRole::Sitemap:
        draw_sitemap_panel(app);
        break;
      case WindowRole::Help:
        draw_help_window(app);
        break;
      case WindowRole::Document:
        draw_soft_window(app, *w);
        break;
    }
  }
}

}  // namespace xanatoo::app
