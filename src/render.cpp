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

void Renderer::draw(const LayoutResult& layout, float scroll_y, float origin_x) {
  if (images_) {
    for (const auto& pi : layout.images) {
      const Image& img = images_->get(pi.src, pi.base);
      SDL_Rect dst{
          static_cast<int>(pi.x + origin_x),
          static_cast<int>(pi.y - scroll_y),
          static_cast<int>(pi.width),
          static_cast<int>(pi.height)};
      if (img.ok && img.texture) {
        SDL_RenderCopy(sdl_, img.texture, nullptr, &dst);
      } else {
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
    dst.x = static_cast<int>(pg.x + origin_x);
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

void Renderer::draw_selection(const std::vector<SelRect>& rects, float origin_x) {
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(sdl_, 160, 190, 230, 120);
  for (const auto& r : rects) {
    SDL_Rect sr{
        static_cast<int>(r.x + origin_x), static_cast<int>(r.y),
        static_cast<int>(r.w), static_cast<int>(r.h)};
    SDL_RenderFillRect(sdl_, &sr);
  }
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
}

void Renderer::draw_caret(const CaretScreenPos& pos, float origin_x) {
  if (!pos.valid) return;
  SDL_SetRenderDrawColor(sdl_, 25, 90, 170, 255);
  SDL_Rect r;
  r.x = static_cast<int>(pos.x + origin_x);
  r.y = static_cast<int>(pos.y - pos.height * 0.85f);
  r.w = 2;
  r.h = static_cast<int>(pos.height * 0.95f);
  SDL_RenderFillRect(sdl_, &r);
}

void Renderer::draw_text(Font& font, float x, float y_baseline,
                         const std::string& text,
                         uint8_t r, uint8_t g, uint8_t b) {
  size_t i = 0;
  float pen = x;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp = 0;
    size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
      cp = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f);
      len = 2;
    } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
      cp = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      len = 3;
    } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
      cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
           ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
      len = 4;
    } else {
      ++i;
      continue;
    }
    i += len;
    const Glyph& gl = font.glyph(cp);
    if (gl.texture) {
      SDL_SetTextureColorMod(gl.texture, r, g, b);
      SDL_Rect dst{
          static_cast<int>(pen + gl.bearing_x),
          static_cast<int>(y_baseline - gl.bearing_y),
          gl.width, gl.height};
      SDL_RenderCopy(sdl_, gl.texture, nullptr, &dst);
    }
    pen += static_cast<float>(gl.advance >> 6);
  }
}

void Renderer::present() {
  SDL_RenderPresent(sdl_);
}

}  // namespace glyphtoo
