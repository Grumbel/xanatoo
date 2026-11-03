// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// An OpenType math font (FreeType): the MATH table behind `MathFace` and
// glyph rasters by glyph id for drawing a `MathBox`.
#pragma once

#include "xanatoo/math.hpp"
#include "xanatoo/style.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xanatoo {

struct MathRaster {
  SDL_Texture* texture = nullptr;
  int w = 0, h = 0;
  int left = 0;  // bitmap_left
  int top = 0;   // bitmap_top
};

class MathFont : public MathFace {
public:
  MathFont() = default;
  ~MathFont() override;
  MathFont(const MathFont&) = delete;
  MathFont& operator=(const MathFont&) = delete;

  // `renderer` may be null (metrics only).
  bool load(const std::string& path, SDL_Renderer* renderer);
  bool loaded() const { return face_ != nullptr; }
  bool has_math_table() const { return has_math_; }
  const std::string& path() const { return path_; }

  uint32_t glyph(uint32_t cp) const override;
  MathGlyph metrics(uint32_t gid) const override;
  const MathConstants& constants() const override { return k_; }
  std::vector<MathVariantGlyph> variants(uint32_t gid,
                                         bool vertical) const override;
  bool assembly(uint32_t gid, bool vertical,
                std::vector<MathPart>& parts) const override;

  // The glyph drawn at `px` pixels per em (cached).
  const MathRaster& raster(uint32_t gid, int px);
  void drop_rasters();

private:
  struct Construction {
    std::vector<MathVariantGlyph> variants;  // without the glyph itself
    std::vector<MathPart> parts;
  };

  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  std::string path_;
  bool has_math_ = false;
  float upem_ = 1000.f;
  MathConstants k_;
  std::unordered_map<uint32_t, float> italic_;
  std::unordered_map<uint32_t, float> accent_;
  std::unordered_map<uint32_t, Construction> vert_, horiz_;
  mutable std::unordered_map<uint32_t, MathGlyph> metrics_;
  std::unordered_map<uint64_t, MathRaster> rasters_;

  void parse_math_table(const std::vector<uint8_t>& t);
};

// Finds a math font: data_dir()/fonts/latinmodern-math.otf, else the first
// installed of the well-known families. "" when there is none.
std::string find_math_font_path();

// Draws `box` with its origin (left end of the baseline) at (x, baseline).
void draw_math(SDL_Renderer* r, MathFont& font, const MathBox& box, float x,
               float baseline, float px, const Rgba& color);

}  // namespace xanatoo
