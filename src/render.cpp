// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/render.hpp"

namespace glyphtoo {

Renderer::Renderer(SDL_Renderer* sdl, Font& font)
    : sdl_(sdl), font_(font) {}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b) {
  SDL_SetRenderDrawColor(sdl_, r, g, b, 255);
  SDL_RenderClear(sdl_);
}

void Renderer::draw(const LayoutResult& layout, float scroll_y) {
  // Soft off-white background already cleared
  for (const auto& pg : layout.glyphs) {
    const Glyph& g = font_.glyph(pg.codepoint);
    if (!g.texture) continue;

    SDL_Rect dst;
    dst.x = static_cast<int>(pg.x);
    dst.y = static_cast<int>(pg.y - g.bearing_y - scroll_y);
    dst.w = g.width;
    dst.h = g.height;

    // Dark text on light paper
    SDL_SetTextureColorMod(g.texture, 30, 30, 35);
    SDL_RenderCopy(sdl_, g.texture, nullptr, &dst);
  }

  // Optional: draw layout boxes for debugging (very subtle)
  // SDL_SetRenderDrawColor(sdl_, 200, 200, 210, 40);
  // for (const auto& box : layout.boxes) {
  //   SDL_Rect r{static_cast<int>(box.x), static_cast<int>(box.y - scroll_y),
  //              static_cast<int>(box.width), static_cast<int>(box.height)};
  //   SDL_RenderDrawRect(sdl_, &r);
  // }
}

void Renderer::present() {
  SDL_RenderPresent(sdl_);
}

}  // namespace glyphtoo
