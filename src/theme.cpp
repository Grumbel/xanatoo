// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/theme.hpp"

#include <algorithm>

namespace xanatoo::theme {

const Palette& palette() {
  static const Palette p;
  return p;
}

namespace {

void set(SDL_Renderer* r, Rgb c, uint8_t a = 255) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
}

Rgb lerp(Rgb a, Rgb b, float t) {
  auto m = [t](uint8_t x, uint8_t y) {
    return static_cast<uint8_t>(
        static_cast<float>(x) +
        (static_cast<float>(y) - static_cast<float>(x)) * t + 0.5f);
  };
  return Rgb{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

}  // namespace

void fill(SDL_Renderer* r, const SDL_Rect& rc, Rgb c, uint8_t a) {
  SDL_BlendMode old;
  SDL_GetRenderDrawBlendMode(r, &old);
  SDL_SetRenderDrawBlendMode(
      r, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  set(r, c, a);
  SDL_RenderFillRect(r, &rc);
  SDL_SetRenderDrawBlendMode(r, old);
}

void hline(SDL_Renderer* r, int x0, int x1, int y, Rgb c) {
  set(r, c);
  SDL_RenderDrawLine(r, x0, y, x1, y);
}

void vline(SDL_Renderer* r, int x, int y0, int y1, Rgb c) {
  set(r, c);
  SDL_RenderDrawLine(r, x, y0, x, y1);
}

void vgradient(SDL_Renderer* r, const SDL_Rect& rc, Rgb top, Rgb bottom) {
  if (rc.h <= 0 || rc.w <= 0) return;
  for (int i = 0; i < rc.h; ++i) {
    const float t = rc.h > 1 ? static_cast<float>(i) / (rc.h - 1) : 0.f;
    hline(r, rc.x, rc.x + rc.w - 1, rc.y + i, lerp(top, bottom, t));
  }
}

void bevel(SDL_Renderer* r, const SDL_Rect& rc, Bevel style) {
  const Palette& p = palette();
  const int x0 = rc.x, y0 = rc.y, x1 = rc.x + rc.w - 1, y1 = rc.y + rc.h - 1;
  Rgb tl = p.highlight, br = p.shadow;
  if (style == Bevel::Sunken) std::swap(tl, br);
  if (style == Bevel::Etched) {
    // Two-tone groove: dark line with a light line right below/after it.
    hline(r, x0, x1, y0, p.shadow);
    vline(r, x0, y0, y1, p.shadow);
    hline(r, x0 + 1, x1, y1, p.highlight);
    vline(r, x1, y0 + 1, y1, p.highlight);
    return;
  }
  hline(r, x0, x1 - 1, y0, tl);
  vline(r, x0, y0, y1 - 1, tl);
  hline(r, x0, x1, y1, br);
  vline(r, x1, y0, y1, br);
}

void drop_shadow(SDL_Renderer* r, const SDL_Rect& rc, int offset) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < offset; ++i) {
    SDL_SetRenderDrawColor(r, 0, 0, 0, static_cast<Uint8>(46 - i * 12));
    SDL_Rect s{rc.x + 2 + i, rc.y + 2 + i, rc.w, rc.h};
    SDL_RenderFillRect(r, &s);
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void draw_button_face(SDL_Renderer* r, const SDL_Rect& rc, bool pressed,
                      bool hot) {
  const Palette& p = palette();
  Rgb top = hot ? p.accent_soft : p.face_light;
  Rgb bot = hot ? lerp(p.accent_soft, p.face_dark, 0.35f) : p.face_dark;
  if (pressed) std::swap(top, bot);
  vgradient(r, rc, top, bot);
  bevel(r, rc, pressed ? Bevel::Sunken : Bevel::Raised);
}

void draw_window_chrome(SDL_Renderer* r, const WindowChrome& w) {
  const Palette& p = palette();
  const SDL_Rect& o = w.outer;
  drop_shadow(r, o);
  // Frame + client face
  fill(r, o, p.face);
  // Title strip
  SDL_Rect title{o.x + 1, o.y + 1, o.w - 2, w.title_h - 1};
  if (w.active)
    vgradient(r, title, p.tab_top, p.tab_bottom);
  else
    vgradient(r, title, p.tab_idle_top, p.tab_idle_bottom);
  // Light edge under the top line, dark line below the strip
  hline(r, title.x, title.x + title.w - 1, title.y, p.highlight);
  hline(r, o.x + 1, o.x + o.w - 2, o.y + w.title_h,
        w.active ? Rgb{0x9A, 0x74, 0x08} : p.shadow);
  // Outer frame
  SDL_SetRenderDrawColor(r, p.frame.r, p.frame.g, p.frame.b, 255);
  SDL_RenderDrawRect(r, &o);
  // Close box (right), 16x14 inside the 28px hit zone
  if (w.close_box) {
    SDL_Rect cb{o.x + o.w - 24, o.y + (w.title_h - 15) / 2, 18, 15};
    draw_button_face(r, cb, false, false);
    set(r, p.text);
    const int cx = cb.x + cb.w / 2, cy = cb.y + cb.h / 2;
    for (int d = -3; d <= 3; ++d) {
      SDL_RenderDrawPoint(r, cx + d, cy + d);
      SDL_RenderDrawPoint(r, cx + d + 1, cy + d);
      SDL_RenderDrawPoint(r, cx + d, cy - d);
      SDL_RenderDrawPoint(r, cx + d + 1, cy - d);
    }
  }
  // Resize grip (SE): diagonal ridges in light/dark pairs
  if (w.resizable) {
    const int gx = o.x + o.w - 3, gy = o.y + o.h - 3;
    for (int i = 0; i < 3; ++i) {
      set(r, p.shadow);
      SDL_RenderDrawLine(r, gx - i * 4 - 1, gy, gx, gy - i * 4 - 1);
      set(r, p.highlight);
      SDL_RenderDrawLine(r, gx - i * 4, gy, gx, gy - i * 4);
    }
  }
}

void draw_vscrollbar(SDL_Renderer* r, const SDL_Rect& trough, float scroll,
                     float max_scroll, float view_h, SDL_Rect* thumb_out) {
  const Palette& p = palette();
  fill(r, trough, p.face_dark);
  bevel(r, trough, Bevel::Sunken);
  if (max_scroll <= 1.f) return;
  const float th = static_cast<float>(trough.h - 2);
  const float thumb_h = std::max(18.f, th * (view_h / (view_h + max_scroll)));
  const float track = std::max(0.f, th - thumb_h);
  const float ratio = std::clamp(scroll / max_scroll, 0.f, 1.f);
  SDL_Rect thumb{trough.x + 1, trough.y + 1 + static_cast<int>(ratio * track),
                 trough.w - 2, static_cast<int>(thumb_h)};
  draw_button_face(r, thumb, false, false);
  if (thumb_out) *thumb_out = thumb;
}

}  // namespace xanatoo::theme
