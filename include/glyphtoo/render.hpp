// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/layout.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/editor.hpp"
#include "glyphtoo/images.hpp"
#include <SDL.h>
#include <vector>

namespace glyphtoo {

class Renderer {
public:
  Renderer(SDL_Renderer* sdl, Font& body_font, Font& heading_font,
           ImageCache* images = nullptr);

  void clear(uint8_t r, uint8_t g, uint8_t b);
  // origin_x shifts all content horizontally (for multi-pane views).
  void draw(const LayoutResult& layout, float scroll_y = 0.f,
            float origin_x = 0.f);
  void draw_selection(const std::vector<SelRect>& rects, float origin_x = 0.f);
  void draw_caret(const CaretScreenPos& pos, float origin_x = 0.f);
  void present();

  SDL_Renderer* sdl() { return sdl_; }

private:
  SDL_Renderer* sdl_;
  Font& body_;
  Font& heading_;
  ImageCache* images_;
};

}  // namespace glyphtoo
