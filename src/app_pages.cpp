// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Paged reading (docs/PAGED.md): a view shows spreads of ebook style pages
// instead of scrolling. The cutting is `paginate` (paginate.hpp); this file
// is the view side: state, page turning, the page bar and the edge zones.
#include "xanatoo/app.hpp"
#include "xanatoo/theme.hpp"

#include <algorithm>
#include <cmath>

namespace xanatoo::app {

namespace th = xanatoo::theme;

namespace {

// Preferred width of one text column, in body-font ems; the auto column
// count is what fits at that width.
constexpr float kColumnEms = 34.f;

struct BarGeom {
  SDL_Rect bar{}, prev{}, next{}, slider{}, left_text{}, right_text{};
};

BarGeom bar_geom(App& app, const Pane& p) {
  BarGeom g;
  const int h = page_bar_h(app);
  g.bar = SDL_Rect{p.x, p.y + p.h, p.w, h};
  const int bw = h + 8;
  g.prev = SDL_Rect{g.bar.x + 2, g.bar.y + 2, bw, h - 3};
  g.next = SDL_Rect{g.bar.x + g.bar.w - bw - 2, g.bar.y + 2, bw, h - 3};
  const PageStatus st = page_status(p);
  Font& f = app.ui_font;
  const std::string lt = (st.first_page == st.last_page)
                             ? "Page " + std::to_string(st.first_page) + " of " +
                                   std::to_string(st.pages)
                             : "Pages " + std::to_string(st.first_page) + "–" +
                                   std::to_string(st.last_page) + " of " +
                                   std::to_string(st.pages);
  const std::string rt = "Loc " + std::to_string(st.location) + " of " +
                         std::to_string(st.locations) + " · " +
                         std::to_string(st.percent) + "%";
  const int pad = 10;
  int x0 = g.prev.x + g.prev.w + 6;
  int x1 = g.next.x - 6;
  const int min_slider = 90;
  int lw = f.measure(lt) + pad;
  int rw = f.measure(rt) + pad;
  if (x1 - x0 - lw - rw < min_slider) rw = 0;
  if (x1 - x0 - lw - rw < min_slider) lw = 0;
  g.left_text = SDL_Rect{x0, g.bar.y, lw, h};
  g.right_text = SDL_Rect{x1 - rw, g.bar.y, rw, h};
  g.slider = SDL_Rect{x0 + lw, g.bar.y + 4, std::max(1, x1 - rw - x0 - lw),
                      h - 8};
  return g;
}

bool inside(const SDL_Rect& r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Zone at the left / right edge of the page area that turns the page.
int edge_w(const App& app, const Pane& p) {
  const int lo = static_cast<int>(28.f * app.ui_scale);
  const int hi = static_cast<int>(72.f * app.ui_scale);
  return std::clamp(p.w / 20, lo, hi);
}
SDL_Rect edge_rect(const App& app, const Pane& p, bool next) {
  const int w = edge_w(app, p);
  return SDL_Rect{next ? p.x + p.w - w : p.x, p.y, w, p.h};
}

size_t cur_spread(const Pane& p) {
  if (p.pages.stride <= 0.f) return 0;
  return static_cast<size_t>(
      std::max(0.f, std::round(p.scroll_y / p.pages.stride)));
}

void arrow_glyph(SDL_Renderer* r, const SDL_Rect& rc, bool right,
                 th::Rgb ink, uint8_t alpha) {
  const int cx = rc.x + rc.w / 2, cy = rc.y + rc.h / 2;
  const int s = std::max(4, std::min(rc.w, rc.h) / 4);
  const int dir = right ? 1 : -1;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, alpha);
  for (int t = 0; t < 2; ++t) {
    SDL_RenderDrawLine(r, cx - dir * s / 2 + t, cy - s, cx + dir * s / 2 + t,
                       cy);
    SDL_RenderDrawLine(r, cx + dir * s / 2 + t, cy, cx - dir * s / 2 + t,
                       cy + s);
  }
}

}  // namespace

bool pane_paged(const App& app, const Pane& p) {
  return p.paged.value_or(app.view_settings.paged);
}

int page_bar_h(const App& app) {
  return std::max(18, static_cast<int>(std::lround(24.f * app.ui_scale)));
}

int pane_page_columns(const App& app, const Pane& p) {
  if (app.view_settings.page_columns > 0) return app.view_settings.page_columns;
  const float em = static_cast<float>(app.fonts.regular.pixel_size());
  const int fit = static_cast<int>(static_cast<float>(p.w) / (kColumnEms * em));
  return std::clamp(fit, 1, 3);
}

void set_paged(App& app, Pane& p, bool on) {
  if (pane_paged(app, p) == on) {
    p.paged = on;
    return;
  }
  if (!p.view_anchor && p.scroll_y > 0.f)  // the page in view stays in view
    p.view_anchor = view_anchor_at(app, p, 0.f, 0.f);
  p.paged = on;
  p.height_cache.clear();
  app.needs_layout = true;
  flash_status(app, on ? "Paged reading: PgDn / PgUp or click the page edges"
                       : "Scrolling");
}

void paginate_pane(App& app, Pane& p, int columns, float column_w) {
  xanatoo::PageSpec s;
  const float my = std::max(8.f, app.layout_engine.options().margin_y);
  s.page_h = static_cast<float>(std::max(1, p.h));
  s.pad_top = my;
  s.pad_bottom = my;
  s.columns = columns;
  s.pitch = column_w;
  s.first_y = app.layout_engine.options().margin_y;
  s.last_y = p.layout.content_height - app.layout_engine.options().margin_y -
             24.f;
  s.extents = app.renderer.glyph_extents();
  s.px_scale = app.layout_engine.options().px_scale;
  p.layout = xanatoo::paginate(p.layout, s, p.pages.map);
  p.pages.on = true;
  p.pages.columns = columns;
  p.pages.pitch = column_w;
  p.pages.stride = s.page_h;
}

float spread_scroll(const Pane& p, size_t s) {
  return static_cast<float>(s) * p.pages.stride;
}

size_t spread_at(const Pane& p, float content_y) {
  if (!p.pages.on || p.pages.stride <= 0.f) return 0;
  const size_t last = p.pages.map.spread_count();
  const size_t s = static_cast<size_t>(std::max(0.f, content_y / p.pages.stride));
  return std::min(s, last > 0 ? last - 1 : 0);
}

void page_goto_spread(App& app, Pane& p, size_t spread) {
  (void)app;
  if (!p.pages.on) return;
  const size_t n = p.pages.map.spread_count();
  spread = std::min(spread, n > 0 ? n - 1 : 0);
  p.scroll_y = spread_scroll(p, spread);
}

bool page_turn(App& app, Pane& p, int spreads) {
  if (!p.pages.on) return false;
  const long cur = static_cast<long>(cur_spread(p));
  const long n = static_cast<long>(p.pages.map.spread_count());
  const long to = std::clamp(cur + spreads, 0L, std::max(0L, n - 1));
  if (to == cur) return false;
  page_goto_spread(app, p, static_cast<size_t>(to));
  return true;
}

PageStatus page_status(const Pane& p) {
  PageStatus st;
  const xanatoo::PageMap& m = p.pages.map;
  if (!p.pages.on || m.pages.empty()) return st;
  const size_t cols = static_cast<size_t>(std::max(1, p.pages.columns));
  const size_t spread = cur_spread(p);
  st.pages = m.pages.size();
  st.first_page = std::min(spread * cols, st.pages - 1) + 1;
  st.last_page = std::min(st.first_page + cols - 1, st.pages);
  st.location = xanatoo::glyph_location(m.pages[st.first_page - 1].first_glyph);
  st.locations = xanatoo::glyph_location(m.glyph_count);
  st.percent = static_cast<int>(st.last_page * 100 / st.pages);
  return st;
}

int page_view_at(App& app, int x, int y) {
  for (int pi : live_views(app)) {
    const Pane& p = app.panes[pi];
    if (!p.pages.on) continue;
    if (x < p.x || x >= p.x + p.w || y < p.y || y >= p.y + p.h + page_bar_h(app))
      continue;
    if (pi < kCentreViews && over_float_chrome(app, x, y)) continue;
    return pi;
  }
  return -1;
}

bool page_pointer_down(App& app, int pi, int x, int y) {
  Pane& p = app.panes[pi];
  if (!p.pages.on) return false;
  const BarGeom g = bar_geom(app, p);
  if (inside(g.prev, x, y)) {
    page_turn(app, p, -1);
    return true;
  }
  if (inside(g.next, x, y)) {
    page_turn(app, p, +1);
    return true;
  }
  if (inside(g.slider, x, y) || inside(g.bar, x, y)) {
    if (inside(g.slider, x, y)) {
      app.page_slider_pane = pi;
      page_pointer_drag(app, x, y);
    }
    return true;
  }
  if (inside(edge_rect(app, p, false), x, y)) {
    page_turn(app, p, -1);
    return true;
  }
  if (inside(edge_rect(app, p, true), x, y)) {
    page_turn(app, p, +1);
    return true;
  }
  return false;
}

bool page_pointer_drag(App& app, int x, int y) {
  (void)y;
  if (app.page_slider_pane < 0) return false;
  Pane& p = app.panes[app.page_slider_pane];
  if (!p.pages.on) return false;
  const BarGeom g = bar_geom(app, p);
  const size_t n = p.pages.map.spread_count();
  const float f =
      std::clamp(static_cast<float>(x - g.slider.x) /
                     static_cast<float>(std::max(1, g.slider.w)),
                 0.f, 1.f);
  page_goto_spread(app, p,
                   static_cast<size_t>(std::lround(f * static_cast<float>(
                                                           n > 0 ? n - 1 : 0))));
  return true;
}

void page_pointer_up(App& app) { app.page_slider_pane = -1; }

void draw_page_chrome(App& app, int pi) {
  Pane& p = app.panes[pi];
  if (!p.pages.on) return;
  SDL_Renderer* r = app.sdl;
  const th::Palette& pal = th::palette();
  SDL_RenderSetClipRect(r, nullptr);

  // Rules between the columns of a spread.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, pal.shadow.r, pal.shadow.g, pal.shadow.b, 110);
  for (int k = 1; k < p.pages.columns; ++k) {
    const int cx = p.x + static_cast<int>(std::lround(k * p.pages.pitch));
    const int pad = static_cast<int>(
        std::max(8.f, app.layout_engine.options().margin_y));
    SDL_RenderDrawLine(r, cx, p.y + pad, cx, p.y + p.h - pad);
  }

  // Edge zones: an arrow while the pointer is over one.
  const int mx = static_cast<int>(app.mouse_x), my = static_cast<int>(app.mouse_y);
  const size_t spread = cur_spread(p);
  const size_t spreads = p.pages.map.spread_count();
  for (int side = 0; side < 2; ++side) {
    const bool next = side == 1;
    const SDL_Rect e = edge_rect(app, p, next);
    if (!inside(e, mx, my)) continue;
    if (next ? spread + 1 >= spreads : spread == 0) continue;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, pal.accent.r, pal.accent.g, pal.accent.b, 34);
    SDL_RenderFillRect(r, &e);
    arrow_glyph(r, e, next, pal.accent_dark, 220);
  }

  // The bar: ‹  page numbers  ====o====  location · percent  ›
  const BarGeom g = bar_geom(app, p);
  th::fill(r, g.bar, pal.face);
  th::hline(r, g.bar.x, g.bar.x + g.bar.w - 1, g.bar.y, pal.highlight);
  th::hline(r, g.bar.x, g.bar.x + g.bar.w - 1, g.bar.y - 1, pal.shadow);
  const bool first = spread == 0, last = spread + 1 >= spreads;
  for (int side = 0; side < 2; ++side) {
    const bool next = side == 1;
    const SDL_Rect& b = next ? g.next : g.prev;
    const bool dead = next ? last : first;
    th::draw_button_face(r, b, false, !dead && inside(b, mx, my));
    arrow_glyph(r, b, next, dead ? pal.shadow : pal.text, 255);
  }
  Font& f = app.ui_font;
  const PageStatus st = page_status(p);
  const int ty = g.bar.y + (g.bar.h - f.line_height()) / 2 + f.ascender();
  if (g.left_text.w > 0) {
    const std::string t = (st.first_page == st.last_page)
                              ? "Page " + std::to_string(st.first_page) +
                                    " of " + std::to_string(st.pages)
                              : "Pages " + std::to_string(st.first_page) + "–" +
                                    std::to_string(st.last_page) + " of " +
                                    std::to_string(st.pages);
    app.renderer.draw_text(f, static_cast<float>(g.left_text.x + 4),
                           static_cast<float>(ty), t, pal.text.r, pal.text.g,
                           pal.text.b);
  }
  if (g.right_text.w > 0) {
    const std::string t = "Loc " + std::to_string(st.location) + " of " +
                          std::to_string(st.locations) + " · " +
                          std::to_string(st.percent) + "%";
    app.renderer.draw_text(f, static_cast<float>(g.right_text.x + 4),
                           static_cast<float>(ty), t, pal.text_dim.r,
                           pal.text_dim.g, pal.text_dim.b);
  }
  // Slider: a sunken trough filled up to the thumb.
  SDL_Rect trough{g.slider.x, g.slider.y + g.slider.h / 2 - 3, g.slider.w, 6};
  th::fill(r, trough, pal.face_dark);
  th::bevel(r, trough, th::Bevel::Sunken);
  const float frac = spreads > 1 ? static_cast<float>(spread) /
                                       static_cast<float>(spreads - 1)
                                 : 0.f;
  const int tw = 10;
  const int tx = g.slider.x + static_cast<int>(frac * (g.slider.w - tw));
  SDL_Rect fillr{trough.x + 1, trough.y + 1, std::max(0, tx - trough.x), 4};
  th::fill(r, fillr, pal.accent);
  SDL_Rect thumb{tx, g.slider.y, tw, g.slider.h};
  th::draw_button_face(r, thumb, app.page_slider_pane == pi, false);
}

}  // namespace xanatoo::app
