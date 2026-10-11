// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/render.hpp"
#include <cmath>

namespace xanatoo {

Renderer::Renderer(SDL_Renderer* sdl, FontSet& fonts, ImageCache* images)
    : sdl_(sdl), fonts_(fonts), images_(images) {}

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
      // Resize handle (bottom-right corner)
      const int hs = 10;
      SDL_Rect handle{dst.x + dst.w - hs, dst.y + dst.h - hs, hs, hs};
      SDL_SetRenderDrawColor(sdl_, 25, 90, 170, 255);
      SDL_RenderFillRect(sdl_, &handle);
    }
  }

  // Table header cell fills (drawn under text)
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    for (const auto& fill : layout.table_header_fills) {
      SDL_SetRenderDrawColor(sdl_, 230, 235, 245, 220);
      SDL_Rect r{
          static_cast<int>(fill.x + origin_x),
          static_cast<int>(fill.y - scroll_y),
          static_cast<int>(fill.width),
          static_cast<int>(fill.height)};
      SDL_RenderFillRect(sdl_, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  // Fenced code-block soft background
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    for (const auto& cb : layout.code_blocks) {
      SDL_SetRenderDrawColor(sdl_, 245, 240, 230, 210);
      SDL_Rect r{
          static_cast<int>(cb.x + origin_x),
          static_cast<int>(cb.y - scroll_y),
          static_cast<int>(cb.width),
          static_cast<int>(cb.height)};
      SDL_RenderFillRect(sdl_, &r);
      SDL_SetRenderDrawColor(sdl_, 210, 200, 180, 255);
      SDL_RenderDrawRect(sdl_, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  // Block-quote left bars
  for (const auto& qb : layout.quote_bars) {
    SDL_SetRenderDrawColor(sdl_, 140, 155, 180, 255);
    int bw = static_cast<int>(qb.width);
    if (bw < 2) bw = 2;
    SDL_Rect bar{
        static_cast<int>(qb.x + origin_x),
        static_cast<int>(qb.y - scroll_y),
        bw,
        static_cast<int>(qb.height)};
    SDL_RenderFillRect(sdl_, &bar);
  }

  // Note left bars (amber, distinct from quotes)
  for (const auto& nb : layout.note_bars) {
    SDL_SetRenderDrawColor(sdl_, 200, 140, 40, 255);
    int bw = static_cast<int>(nb.width);
    if (bw < 2) bw = 2;
    SDL_Rect bar{
        static_cast<int>(nb.x + origin_x),
        static_cast<int>(nb.y - scroll_y),
        bw,
        static_cast<int>(nb.height)};
    SDL_RenderFillRect(sdl_, &bar);
  }

  // Table grid lines
  for (const auto& tl : layout.table_lines) {
    SDL_SetRenderDrawColor(sdl_, 160, 165, 175, 255);
    int w = static_cast<int>(tl.width);
    int h = static_cast<int>(tl.height);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_Rect line{
        static_cast<int>(tl.x + origin_x),
        static_cast<int>(tl.y - scroll_y),
        w, h};
    SDL_RenderFillRect(sdl_, &line);
  }

  // Horizontal rules
  for (const auto& hr : layout.hr_lines) {
    SDL_SetRenderDrawColor(sdl_, 170, 168, 160, 255);
    int th = static_cast<int>(hr.height);
    if (th < 1) th = 1;
    SDL_Rect line{
        static_cast<int>(hr.x + origin_x),
        static_cast<int>(hr.y - scroll_y),
        static_cast<int>(hr.width),
        th};
    SDL_RenderFillRect(sdl_, &line);
  }

  // Soft background runs under inline code
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    float run_x0 = 0.f, run_y = 0.f, run_x1 = 0.f;
    bool in_run = false;
    auto flush_run = [&]() {
      if (!in_run) return;
      int x = static_cast<int>(run_x0 + origin_x) - 2;
      int y = static_cast<int>(run_y - scroll_y) - 2;
      int w = static_cast<int>(run_x1 - run_x0) + 4;
      int h = fonts_.pick(FontRole::Code).line_height() + 2;
      SDL_SetRenderDrawColor(sdl_, 245, 235, 220, 200);
      SDL_Rect r{x, y, w, h};
      SDL_RenderFillRect(sdl_, &r);
      in_run = false;
    };
    for (const auto& pg : layout.glyphs) {
      if (pg.kind != InlineKind::Code) {
        flush_run();
        continue;
      }
      Font& font = fonts_.pick(pg.font);
      const Glyph& g = font.glyph(pg.codepoint);
      float adv = static_cast<float>((g.advance >> 6) > 0 ? (g.advance >> 6)
                                                          : (g.width + 1));
      float top = pg.y - static_cast<float>(font.ascender());
      if (!in_run) {
        run_x0 = pg.x;
        run_x1 = pg.x + adv;
        run_y = top;
        in_run = true;
      } else if (std::abs(top - run_y) < 0.5f && pg.x <= run_x1 + 2.f) {
        run_x1 = pg.x + adv;
      } else {
        flush_run();
        run_x0 = pg.x;
        run_x1 = pg.x + adv;
        run_y = top;
        in_run = true;
      }
    }
    flush_run();
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  for (const auto& pg : layout.glyphs) {
    Font& font = fonts_.pick(pg.font);
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
      case InlineKind::Cite:
        SDL_SetTextureColorMod(g.texture, 90, 50, 140);  // violet for citations
        break;
      case InlineKind::Code:
        SDL_SetTextureColorMod(g.texture, 130, 55, 30);
        break;
      case InlineKind::Strong:
      case InlineKind::Emphasis:
      default:
        SDL_SetTextureColorMod(g.texture, 28, 28, 32);
        break;
    }
    SDL_RenderCopy(sdl_, g.texture, nullptr, &dst);
    if (pg.kind == InlineKind::Link || pg.kind == InlineKind::Cite) {
      if (pg.kind == InlineKind::Cite)
        SDL_SetRenderDrawColor(sdl_, 90, 50, 140, 255);
      else
        SDL_SetRenderDrawColor(sdl_, 30, 90, 180, 255);
      int uy = static_cast<int>(pg.y + 2.f - scroll_y);
      int ux = static_cast<int>(pg.x + origin_x);
      int uw = static_cast<int>((g.advance >> 6) > 0 ? (g.advance >> 6) : (g.width + 1));
      SDL_RenderDrawLine(sdl_, ux, uy, ux + uw, uy);
    }
  }
}

void Renderer::draw_selection(const std::vector<SelRect>& rects, float origin_x) {
  draw_selection(rects, origin_x, 160, 190, 230, 120);
}

void Renderer::draw_selection(const std::vector<SelRect>& rects, float origin_x,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(sdl_, r, g, b, a);
  for (const auto& rect : rects) {
    SDL_Rect sr{
        static_cast<int>(rect.x + origin_x), static_cast<int>(rect.y),
        static_cast<int>(rect.w), static_cast<int>(rect.h)};
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

}  // namespace xanatoo
