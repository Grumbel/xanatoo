// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/font.hpp"
#include <stdexcept>
#include <iostream>

namespace glyphtoo {

Font::~Font() {
  for (auto& [cp, g] : cache_) {
    if (g.texture) {
      SDL_DestroyTexture(g.texture);
    }
  }
  cache_.clear();
  if (face_) {
    FT_Done_Face(face_);
    face_ = nullptr;
  }
  if (library_) {
    FT_Done_FreeType(library_);
    library_ = nullptr;
  }
}

bool Font::load(const std::string& path, int pixel_size, SDL_Renderer* renderer) {
  renderer_ = renderer;
  if (FT_Init_FreeType(&library_)) {
    std::cerr << "FreeType init failed\n";
    return false;
  }
  if (FT_New_Face(library_, path.c_str(), 0, &face_)) {
    std::cerr << "Failed to load font: " << path << "\n";
    return false;
  }
  set_size(pixel_size);
  return true;
}

void Font::set_size(int pixel_size) {
  // Drop cached textures — they are sized for the old pixel size
  for (auto& [cp, g] : cache_) {
    if (g.texture) SDL_DestroyTexture(g.texture);
  }
  cache_.clear();

  pixel_size_ = pixel_size;
  FT_Set_Pixel_Sizes(face_, 0, pixel_size);
  line_height_ = static_cast<int>(face_->size->metrics.height >> 6);
  ascender_ = static_cast<int>(face_->size->metrics.ascender >> 6);
  descender_ = static_cast<int>(face_->size->metrics.descender >> 6);

  // Invalidate cache on size change
  for (auto& [cp, g] : cache_) {
    if (g.texture) SDL_DestroyTexture(g.texture);
  }
  cache_.clear();
}

Glyph Font::load_glyph(uint32_t codepoint) {
  Glyph g;
  if (FT_Load_Char(face_, codepoint, FT_LOAD_RENDER)) {
    return g;
  }
  FT_GlyphSlot slot = face_->glyph;
  g.width = slot->bitmap.width;
  g.height = slot->bitmap.rows;
  g.bearing_x = slot->bitmap_left;
  g.bearing_y = slot->bitmap_top;
  g.advance = static_cast<int>(slot->advance.x);

  if (g.width > 0 && g.height > 0) {
    // Convert grayscale bitmap to RGBA for SDL
    std::vector<uint8_t> rgba(g.width * g.height * 4);
    for (int i = 0; i < g.width * g.height; ++i) {
      uint8_t a = slot->bitmap.buffer[i];
      rgba[i * 4 + 0] = 255;
      rgba[i * 4 + 1] = 255;
      rgba[i * 4 + 2] = 255;
      rgba[i * 4 + 3] = a;
    }
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        rgba.data(), g.width, g.height, 32, g.width * 4,
        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    if (surf) {
      g.texture = SDL_CreateTextureFromSurface(renderer_, surf);
      SDL_SetTextureBlendMode(g.texture, SDL_BLENDMODE_BLEND);
      SDL_FreeSurface(surf);
    }
  }
  return g;
}

const Glyph& Font::glyph(uint32_t codepoint) {
  auto it = cache_.find(codepoint);
  if (it != cache_.end()) {
    return it->second;
  }
  Glyph g = load_glyph(codepoint);
  auto [ins, _] = cache_.emplace(codepoint, std::move(g));
  return ins->second;
}

int Font::measure(const std::string& text) {
  int width = 0;
  size_t i = 0;
  while (i < text.size()) {
    uint32_t cp = 0;
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      cp = c;
      ++i;
    } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
      cp = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f);
      i += 2;
    } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
      cp = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      i += 3;
    } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
      cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
           ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
      i += 4;
    } else {
      ++i;
      continue;
    }
    const Glyph& g = glyph(cp);
    width += g.advance >> 6;
  }
  return width;
}

}  // namespace glyphtoo
