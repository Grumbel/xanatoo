// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/layout.hpp"
#include "glyphtoo/font.hpp"
#include <SDL.h>

namespace glyphtoo {

class Renderer {
public:
  Renderer(SDL_Renderer* sdl, Font& font);

  void clear(uint8_t r, uint8_t g, uint8_t b);
  void draw(const LayoutResult& layout, float scroll_y = 0.f);
  void present();

  SDL_Renderer* sdl() { return sdl_; }

private:
  SDL_Renderer* sdl_;
  Font& font_;
};

}  // namespace glyphtoo
