// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Classic desktop look (BeOS R5 / GNOME 2 Clearlooks): one palette (plain
// data) and stateless drawing primitives used by every window / bar that is
// drawn on the SDL canvas. The ImGui chrome (menus, toolbar) reads the same
// palette so the two never drift apart.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

namespace xanatoo::theme {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
};

constexpr Rgb rgb(int r, int g, int b) {
  return Rgb{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
             static_cast<uint8_t>(b)};
}

struct Palette {
  // Surfaces
  Rgb face = rgb(0xEC, 0xE9, 0xE4);        // window / bar background
  Rgb face_light = rgb(0xF6, 0xF5, 0xF2);  // gradient top, menu
  Rgb face_dark = rgb(0xDA, 0xD6, 0xCF);   // gradient bottom, pressed
  Rgb field = rgb(0xFF, 0xFF, 0xFF);       // text entry / sunken well
  Rgb paper = rgb(0xF8, 0xF6, 0xF0);       // document canvas
  // Bevel edges
  Rgb highlight = rgb(0xFF, 0xFF, 0xFF);
  Rgb shadow = rgb(0xA8, 0xA4, 0x9C);
  Rgb dark = rgb(0x6C, 0x69, 0x62);
  Rgb frame = rgb(0x7E, 0x7B, 0x74);
  // Ink
  Rgb text = rgb(0x2E, 0x34, 0x36);
  Rgb text_dim = rgb(0x6E, 0x6C, 0x66);
  // Window title tab (BeOS yellow when active)
  Rgb tab_top = rgb(0xFF, 0xE0, 0x5A);
  Rgb tab_bottom = rgb(0xF2, 0xBE, 0x1A);
  Rgb tab_idle_top = rgb(0xEE, 0xEC, 0xE8);
  Rgb tab_idle_bottom = rgb(0xCF, 0xCB, 0xC4);
  Rgb tab_text = rgb(0x2A, 0x24, 0x08);
  // Selection / accent (GNOME 2 blue)
  Rgb accent = rgb(0x4A, 0x90, 0xD9);
  Rgb accent_dark = rgb(0x2A, 0x62, 0xA8);
  Rgb accent_soft = rgb(0xC8, 0xDC, 0xF2);
  // Tooltip / notice (BeOS-style pale yellow)
  Rgb notice = rgb(0xFF, 0xFA, 0xD2);
  Rgb notice_text = rgb(0x3A, 0x30, 0x10);
};

const Palette& palette();

enum class Bevel { Raised, Sunken, Etched };

void fill(SDL_Renderer* r, const SDL_Rect& rc, Rgb c, uint8_t a = 255);
void hline(SDL_Renderer* r, int x0, int x1, int y, Rgb c);
void vline(SDL_Renderer* r, int x, int y0, int y1, Rgb c);
void vgradient(SDL_Renderer* r, const SDL_Rect& rc, Rgb top, Rgb bottom);
// 1px frame (inside `rc`); Raised = light top-left / dark bottom-right.
void bevel(SDL_Renderer* r, const SDL_Rect& rc, Bevel style);
// Soft drop shadow for floating windows.
void drop_shadow(SDL_Renderer* r, const SDL_Rect& rc, int offset = 3);

struct WindowChrome {
  SDL_Rect outer{};       // whole window incl. frame
  int title_h = 28;       // height of the title strip
  bool active = true;     // yellow tab vs. grey
  bool close_box = true;  // right-aligned close widget (hit zone: last 28px)
  bool resizable = true;  // SE grip
};

// Frame, title strip (gradient), close box and resize grip. The window
// client area is `outer` minus title strip minus 1px frame; the title text
// is drawn by the caller (needs the font) at title_text_x/baseline.
void draw_window_chrome(SDL_Renderer* r, const WindowChrome& w);

// Classic vertical scrollbar (trough + bevelled thumb). `value`/`range` are
// content-space; returns thumb rect through `thumb_out` when not null.
void draw_vscrollbar(SDL_Renderer* r, const SDL_Rect& trough, float scroll,
                     float max_scroll, float view_h,
                     SDL_Rect* thumb_out = nullptr);

// Raised push-button face; `pressed` flips to sunken + darker face.
void draw_button_face(SDL_Renderer* r, const SDL_Rect& rc, bool pressed = false,
                      bool hot = false);

}  // namespace xanatoo::theme
