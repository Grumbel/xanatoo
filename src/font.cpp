// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/font.hpp"
#include <fontconfig/fontconfig.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <strings.h>

namespace xanatoo {

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
  path_ = path;
  // Clear previous face if reloading
  for (auto& [cp, g] : cache_) {
    if (g.texture) SDL_DestroyTexture(g.texture);
  }
  cache_.clear();
  if (face_) {
    FT_Done_Face(face_);
    face_ = nullptr;
  }
  if (!library_ && FT_Init_FreeType(&library_)) {
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

  if (g.width > 0 && g.height > 0 && renderer_) {
    // Convert grayscale bitmap to RGBA for SDL (skip when metrics-only).
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
      if (g.texture)
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

std::string fontconfig_find(const std::string& family, int weight, int slant) {
  if (!FcInit()) return {};

  FcPattern* pat = FcPatternCreate();
  if (!pat) return {};
  if (!family.empty())
    FcPatternAddString(pat, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
  FcPatternAddInteger(pat, FC_WEIGHT,
                      weight ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
  FcPatternAddInteger(pat, FC_SLANT,
                      slant ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
  FcPatternAddString(pat, FC_STYLE, reinterpret_cast<const FcChar8*>(""));
  FcConfigSubstitute(nullptr, pat, FcMatchPattern);
  FcDefaultSubstitute(pat);

  FcResult result = FcResultNoMatch;
  FcPattern* match = FcFontMatch(nullptr, pat, &result);
  FcPatternDestroy(pat);
  if (!match) return {};

  FcChar8* file = nullptr;
  std::string path;
  if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
    path = reinterpret_cast<const char*>(file);
  }
  FcPatternDestroy(match);
  return path;
}

std::string fontconfig_family_from_file(const std::string& path) {
  if (path.empty() || !FcInit()) return {};
  FcPattern* pat = FcPatternCreate();
  if (!pat) return {};
  FcPatternAddString(pat, FC_FILE, reinterpret_cast<const FcChar8*>(path.c_str()));
  FcConfigSubstitute(nullptr, pat, FcMatchPattern);
  FcDefaultSubstitute(pat);
  FcResult result = FcResultNoMatch;
  FcPattern* match = FcFontMatch(nullptr, pat, &result);
  FcPatternDestroy(pat);
  if (!match) return {};
  FcChar8* family = nullptr;
  std::string name;
  if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch && family) {
    name = reinterpret_cast<const char*>(family);
  }
  FcPatternDestroy(match);
  return name;
}

static bool try_load(Font& font, const std::string& path, int px,
                     SDL_Renderer* r, const char* label) {
  if (path.empty()) return false;
  if (font.load(path, px, r)) {
    std::cout << "Font (" << label << "): " << path << "\n";
    return true;
  }
  return false;
}

// Restored pre-R36S UI font path (commit before d55b74d UI-scale work).
// GNOME 2–era faces: Bitstream Vera → DejaVu Sans.
std::string ui_font_path(bool bold) {
  const char* families[] = {
      "DejaVu Sans",
      "Bitstream Vera Sans",
      "Liberation Sans",
      "FreeSans",
      "Nimbus Sans L",
      "Sans",
      nullptr,
  };
  for (const char** f = families; *f; ++f) {
    std::string path = fontconfig_find(*f, bold ? 1 : 0, 0);
    if (!path.empty()) return path;
  }
  if (bold) return ui_font_path(false);
  const char* files[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/ttf-bitstream-vera/Vera.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      nullptr,
  };
  for (const char** f = files; *f; ++f) {
    if (std::filesystem::exists(*f)) return *f;
  }
  // PortMaster ships DejaVu next to the binary.
  if (const char* data = std::getenv("XANATOO_DATA_DIR")) {
    if (data[0] != '\0') {
      std::filesystem::path p = std::filesystem::path(data) / "fonts" / "DejaVuSans.ttf";
      if (std::filesystem::exists(p)) return p.string();
    }
  }
  if (const char* body = std::getenv("XANATOO_FONT")) {
    if (body[0] != '\0' && std::filesystem::exists(body)) return body;
  }
  return {};
}

static bool is_generic_family(const std::string& name) {
  return name == "serif" || name == "sans-serif" || name == "sans" ||
         name == "monospace" || name == "mono";
}

std::string fontconfig_pick_family(const std::vector<std::string>& names) {
  if (!FcInit()) return {};
  for (const std::string& name : names) {
    if (name.empty()) continue;
    if (is_generic_family(name)) return name;
    // FontConfig always returns *some* match; accept it only when the
    // matched family is the requested one.
    FcPattern* pat = FcPatternCreate();
    if (!pat) continue;
    FcPatternAddString(pat, FC_FAMILY,
                       reinterpret_cast<const FcChar8*>(name.c_str()));
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);
    if (!match) continue;
    FcChar8* fam = nullptr;
    bool same = false;
    if (FcPatternGetString(match, FC_FAMILY, 0, &fam) == FcResultMatch && fam)
      same = strcasecmp(reinterpret_cast<const char*>(fam), name.c_str()) == 0;
    FcPatternDestroy(match);
    if (same) return name;
  }
  return {};
}

bool FontSet::load(SDL_Renderer* renderer, const std::string& preferred_path,
                   int body_px, int heading_px, const FontFamilies& families) {
  std::string family;
  if (!preferred_path.empty()) {
    family = fontconfig_family_from_file(preferred_path);
    if (family.empty()) {
      // Fall back: use the path for regular; FontConfig for variants by guessing
      // common DejaVu/Liberation naming from the basename.
      family = "DejaVu Sans";
    }
  }
  if (family.empty()) family = fontconfig_pick_family(families.body);
  if (family.empty()) {
    // Prefer widely available families
    const char* candidates[] = {
        "DejaVu Sans", "Liberation Sans", "FreeSans", "Noto Sans",
        "Ubuntu", "Cantarell", "Arial", "Helvetica", nullptr};
    for (const char** p = candidates; *p; ++p) {
      std::string path = fontconfig_find(*p, 0, 0);
      if (!path.empty()) {
        family = *p;
        break;
      }
    }
  }
  if (family.empty()) family = "sans-serif";

  std::string reg = preferred_path;
  if (reg.empty() || !try_load(regular, reg, body_px, renderer, "regular")) {
    reg = fontconfig_find(family, 0, 0);
    if (!try_load(regular, reg, body_px, renderer, "regular")) {
      std::cerr << "Failed to load a regular body font (family=" << family << ")\n";
      return false;
    }
  }

  std::string b = fontconfig_find(family, 1, 0);
  if (!try_load(bold, b, body_px, renderer, "bold")) {
    // Soft-fail: share regular face metrics by loading same file
    try_load(bold, regular.path(), body_px, renderer, "bold(fallback)");
  }
  std::string it = fontconfig_find(family, 0, 1);
  if (!try_load(italic, it, body_px, renderer, "italic")) {
    try_load(italic, regular.path(), body_px, renderer, "italic(fallback)");
  }
  std::string bi = fontconfig_find(family, 1, 1);
  if (!try_load(bold_italic, bi, body_px, renderer, "bold-italic")) {
    if (bold.loaded())
      try_load(bold_italic, bold.path(), body_px, renderer, "bold-italic(fallback)");
    else
      try_load(bold_italic, regular.path(), body_px, renderer, "bold-italic(fallback)");
  }

  // Heading: own family when the style names one, else the body family;
  // bold unless the style asks for a regular weight.
  std::string h;
  const std::string heading_family = fontconfig_pick_family(families.heading);
  if (!heading_family.empty())
    h = fontconfig_find(heading_family, families.heading_bold ? 1 : 0, 0);
  else if (families.heading_bold)
    h = bold.loaded() ? bold.path() : regular.path();
  else
    h = regular.path();
  if (!try_load(heading, h, heading_px, renderer, "heading")) {
    try_load(heading, regular.path(), heading_px, renderer, "heading(fallback)");
  }

  // Monospace for code: the style's family first, then common ones.
  std::vector<std::string> mono_families;
  if (std::string f = fontconfig_pick_family(families.code); !f.empty())
    mono_families.push_back(f);
  for (const char* f : {"DejaVu Sans Mono", "Liberation Mono", "FreeMono",
                        "Noto Sans Mono", "Ubuntu Mono", "Courier New",
                        "monospace"})
    mono_families.emplace_back(f);
  bool mono_ok = false;
  for (const std::string& f : mono_families) {
    if (try_load(mono, fontconfig_find(f, 0, 0), body_px, renderer, "mono")) {
      mono_ok = true;
      break;
    }
  }
  if (!mono_ok) {
    try_load(mono, regular.path(), body_px, renderer, "mono(fallback)");
  }

  return regular.loaded();
}

void FontSet::set_sizes(int body_px, int heading_px) {
  if (regular.loaded()) regular.set_size(body_px);
  if (bold.loaded()) bold.set_size(body_px);
  if (italic.loaded()) italic.set_size(body_px);
  if (bold_italic.loaded()) bold_italic.set_size(body_px);
  if (heading.loaded()) heading.set_size(heading_px);
  if (mono.loaded()) mono.set_size(body_px);
}

Font& FontSet::pick(FontRole role) {
  switch (role) {
    case FontRole::BodyBold:
      return bold.loaded() ? bold : regular;
    case FontRole::BodyItalic:
      return italic.loaded() ? italic : regular;
    case FontRole::BodyBoldItalic:
      return bold_italic.loaded() ? bold_italic
             : (bold.loaded() ? bold : regular);
    case FontRole::Heading:
      return heading.loaded() ? heading : regular;
    case FontRole::Code:
      return mono.loaded() ? mono : regular;
    case FontRole::Body:
    default:
      return regular;
  }
}

const Font& FontSet::pick(FontRole role) const {
  return const_cast<FontSet*>(this)->pick(role);
}

}  // namespace xanatoo
