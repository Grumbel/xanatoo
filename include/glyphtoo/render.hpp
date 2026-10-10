// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/layout.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/editor.hpp"
#include <SDL.h>
#include <vector>

namespace glyphtoo {

class Renderer {
public:
  Renderer(SDL_Renderer* sdl, Font& body_font, Font& heading_font);

  void clear(uint8_t r, uint8_t g, uint8_t b);
  void draw(const LayoutResult& layout, float scroll_y = 0.f);
  void draw_selection(const std::vector<SelRect>& rects);
  void draw_caret(const CaretScreenPos& pos);
  void present();

  SDL_Renderer* sdl() { return sdl_; }

private:
  SDL_Renderer* sdl_;
  Font& body_;
  Font& heading_;
};

}  // namespace glyphtoo
