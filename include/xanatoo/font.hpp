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

namespace xanatoo {

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
  // True when the face maps `codepoint` (no .notdef fallback). glyph()
  // still draws unmapped codepoints from a fallback face when one has it.
  bool has_glyph(uint32_t codepoint) const {
    return face_ && FT_Get_Char_Index(face_, codepoint) != 0;
  }
  int line_height() const { return line_height_; }
  int ascender() const { return ascender_; }
  int descender() const { return descender_; }
  int pixel_size() const { return pixel_size_; }
  bool loaded() const { return face_ != nullptr; }
  const std::string& path() const { return path_; }

  // Measure a UTF-8 string width in pixels
  int measure(const std::string& text);

private:
  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  std::string path_;
  int pixel_size_ = 0;
  int line_height_ = 0;
  int ascender_ = 0;
  int descender_ = 0;
  std::unordered_map<uint32_t, Glyph> cache_;
  // Faces opened for codepoints the primary face lacks, same pixel size.
  struct FallbackFace {
    std::string path;
    FT_Face face = nullptr;
  };
  std::vector<FallbackFace> fallbacks_;

  Glyph load_glyph(uint32_t codepoint);
  FT_Face fallback_face(uint32_t codepoint);
  void drop_cache();
};

// Font files that may cover `codepoint` when a face of `family` lacks it,
// best first (FontConfig: sorted by similarity, filtered by coverage;
// without FontConfig: the usual broad-coverage system faces, unfiltered).
std::vector<std::string> fallback_font_paths(uint32_t codepoint,
                                             const std::string& family,
                                             int weight, int slant);

// Which face to use when laying out / drawing a glyph.
enum class FontRole : uint8_t {
  Body = 0,
  BodyBold,
  BodyItalic,
  BodyBoldItalic,
  Heading,
  Code,
};

// Resolve a font file via FontConfig (family + weight + slant).
// weight: 0=regular, 1=bold; slant: 0=roman, 1=italic.
std::string fontconfig_find(const std::string& family, int weight, int slant);

// Infer a family name from a user-supplied TTF/OTF path (best-effort).
std::string fontconfig_family_from_file(const std::string& path);

// Preferred families per face, first installed one wins (generic names such
// as "serif" / "monospace" always resolve). Empty = built-in candidates.
struct FontFamilies {
  std::vector<std::string> body;
  std::vector<std::string> heading;  // empty = body family
  std::vector<std::string> code;
  bool heading_bold = true;
  bool operator==(const FontFamilies&) const = default;
};

// Face for UI chrome (menus, status line, panels): a GNOME 2–era sans
// (DejaVu Sans / Vera / Liberation …), independent of document style sheets.
std::string ui_font_path(bool bold = false);

// First family of `names` that FontConfig has installed, else "".
std::string fontconfig_pick_family(const std::vector<std::string>& names);

// Body regular/bold/italic + heading + monospace for code.
class FontSet {
public:
  Font regular;
  Font bold;
  Font italic;
  Font bold_italic;
  Font heading;
  Font mono;

  // preferred_path may be empty (FontConfig defaults) or a concrete .ttf;
  // it wins over families.body (reader choice beats the style sheet).
  bool load(SDL_Renderer* renderer, const std::string& preferred_path,
            int body_px, int heading_px, const FontFamilies& families = {});

  void set_sizes(int body_px, int heading_px);

  Font& pick(FontRole role);
  const Font& pick(FontRole role) const;
};

}  // namespace xanatoo
