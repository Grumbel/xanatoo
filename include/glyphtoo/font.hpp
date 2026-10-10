// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace glyphtoo {

struct Glyph {
  SDL_Texture* texture = nullptr;
  int width = 0;
  int height = 0;
  int bearing_x = 0;
  int bearing_y = 0;
  int advance = 0;  // in 1/64th pixels
};

class Font {
public:
  Font() = default;
  ~Font();

  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  bool load(const std::string& path, int pixel_size, SDL_Renderer* renderer);
  void set_size(int pixel_size);

  const Glyph& glyph(uint32_t codepoint);
  int line_height() const { return line_height_; }
  int ascender() const { return ascender_; }
  int descender() const { return descender_; }
  int pixel_size() const { return pixel_size_; }

  // Measure a UTF-8 string width in pixels
  int measure(const std::string& text);

private:
  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  int pixel_size_ = 0;
  int line_height_ = 0;
  int ascender_ = 0;
  int descender_ = 0;
  std::unordered_map<uint32_t, Glyph> cache_;

  Glyph load_glyph(uint32_t codepoint);
};

}  // namespace glyphtoo
