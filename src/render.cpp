// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/render.hpp"

namespace glyphtoo {

Renderer::Renderer(SDL_Renderer* sdl, Font& body, Font& heading, ImageCache* images)
    : sdl_(sdl), body_(body), heading_(heading), images_(images) {}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b) {
  SDL_SetRenderDrawColor(sdl_, r, g, b, 255);
  SDL_RenderClear(sdl_);
}

void Renderer::draw(const LayoutResult& layout, float scroll_y) {
  // Images first (behind text of the same region)
  if (images_) {
    for (const auto& pi : layout.images) {
      const Image& img = images_->get(pi.src);
      SDL_Rect dst{
          static_cast<int>(pi.x),
          static_cast<int>(pi.y - scroll_y),
          static_cast<int>(pi.width),
          static_cast<int>(pi.height)};
      if (img.ok && img.texture) {
        SDL_RenderCopy(sdl_, img.texture, nullptr, &dst);
      } else {
        // Placeholder frame
        SDL_SetRenderDrawColor(sdl_, 200, 200, 210, 255);
        SDL_RenderDrawRect(sdl_, &dst);
        SDL_SetRenderDrawColor(sdl_, 230, 230, 235, 255);
        SDL_RenderFillRect(sdl_, &dst);
      }
    }
  }

  for (const auto& pg : layout.glyphs) {
    Font& font = (pg.font == FontRole::Heading) ? heading_ : body_;
    const Glyph& g = font.glyph(pg.codepoint);
    if (!g.texture) continue;

    SDL_Rect dst;
    dst.x = static_cast<int>(pg.x);
    dst.y = static_cast<int>(pg.y - g.bearing_y - scroll_y);
    dst.w = g.width;
    dst.h = g.height;

    switch (pg.kind) {
      case InlineKind::Link:
        SDL_SetTextureColorMod(g.texture, 30, 90, 180);
        break;
      case InlineKind::Strong:
        SDL_SetTextureColorMod(g.texture, 15, 15, 20);
        break;
      case InlineKind::Emphasis:
        SDL_SetTextureColorMod(g.texture, 45, 45, 55);
        break;
      case InlineKind::Code:
        SDL_SetTextureColorMod(g.texture, 130, 55, 30);
        break;
      default:
        SDL_SetTextureColorMod(g.texture, 28, 28, 32);
        break;
    }
    SDL_RenderCopy(sdl_, g.texture, nullptr, &dst);
  }
}

void Renderer::draw_selection(const std::vector<SelRect>& rects) {
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(sdl_, 160, 190, 230, 120);
  for (const auto& r : rects) {
    SDL_Rect sr{
        static_cast<int>(r.x), static_cast<int>(r.y),
        static_cast<int>(r.w), static_cast<int>(r.h)};
    SDL_RenderFillRect(sdl_, &sr);
  }
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
}

void Renderer::draw_caret(const CaretScreenPos& pos) {
  if (!pos.valid) return;
  SDL_SetRenderDrawColor(sdl_, 25, 90, 170, 255);
  SDL_Rect r;
  r.x = static_cast<int>(pos.x);
  r.y = static_cast<int>(pos.y - pos.height * 0.85f);
  r.w = 2;
  r.h = static_cast<int>(pos.height * 0.95f);
  SDL_RenderFillRect(sdl_, &r);
}

void Renderer::present() {
  SDL_RenderPresent(sdl_);
}

}  // namespace glyphtoo
