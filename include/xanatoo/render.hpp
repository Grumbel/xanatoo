// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/layout.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/style.hpp"
#include <SDL.h>
#include <vector>

namespace xanatoo {

class Renderer {
public:
  Renderer(SDL_Renderer* sdl, FontSet& fonts, ImageCache* images = nullptr);

  // Decoration colours (bars, rules, code fills, selection); glyph colours
  // are already resolved by layout. Must outlive the renderer.
  void set_style(const DocStyle* style) { style_ = style; }

  void clear(uint8_t r, uint8_t g, uint8_t b);
  // origin_x shifts all content horizontally (for multi-pane views).
  void draw(const LayoutResult& layout, float scroll_y = 0.f,
            float origin_x = 0.f);
  void draw_selection(const std::vector<SelRect>& rects, float origin_x = 0.f);
  // Custom-colour selection (e.g. leap-search match highlight).
  void draw_selection(const std::vector<SelRect>& rects, float origin_x,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  void draw_caret(const CaretScreenPos& pos, float origin_x = 0.f);
  // Ascent / descent per FontRole for selection_rects.
  GlyphExtents glyph_extents() const;
  // Simple left-to-right UTF-8 run (status bars, overlays).
  void draw_text(Font& font, float x, float y_baseline, const std::string& text,
                 uint8_t r, uint8_t g, uint8_t b);
  void present();

  SDL_Renderer* sdl() { return sdl_; }

private:
  SDL_Renderer* sdl_;
  FontSet& fonts_;
  ImageCache* images_;
  const DocStyle* style_ = nullptr;
};

}  // namespace xanatoo
