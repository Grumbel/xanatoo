// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/math_font.hpp"

#include "xanatoo/font.hpp"
#include "xanatoo/paths.hpp"

#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#include FT_SFNT_NAMES_H

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace xanatoo {

namespace {

// Big-endian reads with bounds checks (a bad table reads as zeros).
struct Reader {
  const std::vector<uint8_t>& d;
  uint32_t u16(size_t o) const {
    return o + 2 <= d.size() ? (d[o] << 8) | d[o + 1] : 0;
  }
  int32_t i16(size_t o) const { return static_cast<int16_t>(u16(o)); }
};

// Glyph ids of a Coverage table, in coverage-index order.
std::vector<uint16_t> read_coverage(const Reader& r, size_t off) {
  std::vector<uint16_t> out;
  if (off == 0 || off + 4 > r.d.size()) return out;
  const uint32_t fmt = r.u16(off);
  const uint32_t n = r.u16(off + 2);
  if (fmt == 1) {
    for (uint32_t i = 0; i < n && i < 65536; ++i)
      out.push_back(static_cast<uint16_t>(r.u16(off + 4 + 2 * i)));
  } else if (fmt == 2) {
    for (uint32_t i = 0; i < n && i < 65536; ++i) {
      const size_t e = off + 4 + 6 * i;
      const uint32_t a = r.u16(e), b = r.u16(e + 2), idx = r.u16(e + 4);
      if (b < a) continue;
      if (out.size() < idx + (b - a) + 1) out.resize(idx + (b - a) + 1, 0);
      for (uint32_t g = a; g <= b; ++g) out[idx + (g - a)] = static_cast<uint16_t>(g);
    }
  }
  return out;
}

}  // namespace

MathFont::~MathFont() {
  drop_rasters();
  if (face_) FT_Done_Face(face_);
  if (library_) FT_Done_FreeType(library_);
}

void MathFont::drop_rasters() {
  for (auto& [key, r] : rasters_)
    if (r.texture) SDL_DestroyTexture(r.texture);
  rasters_.clear();
}

bool MathFont::load(const std::string& path, SDL_Renderer* renderer) {
  renderer_ = renderer;
  path_ = path;
  drop_rasters();
  metrics_.clear();
  if (face_) {
    FT_Done_Face(face_);
    face_ = nullptr;
  }
  if (!library_ && FT_Init_FreeType(&library_)) return false;
  if (FT_New_Face(library_, path.c_str(), 0, &face_)) {
    face_ = nullptr;
    return false;
  }
  upem_ = face_->units_per_EM > 0 ? static_cast<float>(face_->units_per_EM)
                                  : 1000.f;
  has_math_ = false;
  k_ = MathConstants{};
  italic_.clear();
  accent_.clear();
  vert_.clear();
  horiz_.clear();

  FT_ULong len = 0;
  if (FT_Load_Sfnt_Table(face_, FT_MAKE_TAG('M', 'A', 'T', 'H'), 0, nullptr,
                         &len) == 0 &&
      len > 12) {
    std::vector<uint8_t> t(len);
    if (FT_Load_Sfnt_Table(face_, FT_MAKE_TAG('M', 'A', 'T', 'H'), 0, t.data(),
                           &len) == 0) {
      parse_math_table(t);
      has_math_ = true;
    }
  }
  return true;
}

void MathFont::parse_math_table(const std::vector<uint8_t>& t) {
  const Reader r{t};
  const size_t consts = r.u16(4), info = r.u16(6), vars = r.u16(8);

  if (consts && consts + 8 < t.size()) {
    // Two percentages, two plain heights, then MathValueRecords (4 bytes).
    k_.script_scale = static_cast<float>(r.i16(consts)) / 100.f;
    k_.script_script_scale = static_cast<float>(r.i16(consts + 2)) / 100.f;
    k_.display_op_min_height = static_cast<float>(r.u16(consts + 6)) / upem_;
    auto mv = [&](int k) {  // k-th MathValueRecord, 1-based
      return static_cast<float>(r.i16(consts + 8 + 4 * static_cast<size_t>(k - 1))) /
             upem_;
    };
    k_.axis_height = mv(2);
    k_.accent_base_height = mv(3);
    k_.subscript_shift_down = mv(5);
    k_.subscript_top_max = mv(6);
    k_.subscript_baseline_drop_min = mv(7);
    k_.superscript_shift_up = mv(8);
    k_.superscript_shift_up_cramped = mv(9);
    k_.superscript_bottom_min = mv(10);
    k_.superscript_baseline_drop_max = mv(11);
    k_.sub_superscript_gap_min = mv(12);
    k_.superscript_bottom_max_with_subscript = mv(13);
    k_.space_after_script = mv(14);
    k_.upper_limit_gap_min = mv(15);
    k_.upper_limit_baseline_rise_min = mv(16);
    k_.lower_limit_gap_min = mv(17);
    k_.lower_limit_baseline_drop_min = mv(18);
    k_.stack_top_shift_up = mv(19);
    k_.stack_top_display_shift_up = mv(20);
    k_.stack_bottom_shift_down = mv(21);
    k_.stack_bottom_display_shift_down = mv(22);
    k_.stack_gap_min = mv(23);
    k_.stack_display_gap_min = mv(24);
    k_.fraction_numerator_shift_up = mv(29);
    k_.fraction_numerator_display_shift_up = mv(30);
    k_.fraction_denominator_shift_down = mv(31);
    k_.fraction_denominator_display_shift_down = mv(32);
    k_.fraction_numerator_gap_min = mv(33);
    k_.fraction_numerator_display_gap_min = mv(34);
    k_.fraction_rule_thickness = mv(35);
    k_.fraction_denominator_gap_min = mv(36);
    k_.fraction_denominator_display_gap_min = mv(37);
    k_.overbar_vertical_gap = mv(40);
    k_.overbar_rule_thickness = mv(41);
    k_.overbar_extra_ascender = mv(42);
    k_.underbar_vertical_gap = mv(43);
    k_.underbar_rule_thickness = mv(44);
    k_.underbar_extra_descender = mv(45);
    k_.radical_vertical_gap = mv(46);
    k_.radical_display_vertical_gap = mv(47);
    k_.radical_rule_thickness = mv(48);
    k_.radical_extra_ascender = mv(49);
    k_.radical_kern_before_degree = mv(50);
    k_.radical_kern_after_degree = mv(51);
    k_.radical_degree_bottom_raise =
        static_cast<float>(r.i16(consts + 8 + 4 * 51)) / 100.f;
  }

  if (info && info + 8 <= t.size()) {
    auto values = [&](size_t off, std::unordered_map<uint32_t, float>& out) {
      if (!off) return;
      const size_t base = info + off;
      const std::vector<uint16_t> cov = read_coverage(r, base + r.u16(base));
      const uint32_t n = r.u16(base + 2);
      for (uint32_t i = 0; i < n && i < cov.size(); ++i)
        out[cov[i]] = static_cast<float>(r.i16(base + 4 + 4 * i)) / upem_;
    };
    values(r.u16(info), italic_);
    values(r.u16(info + 2), accent_);
  }

  if (vars && vars + 10 <= t.size()) {
    k_.min_connector_overlap = static_cast<float>(r.u16(vars)) / upem_;
    const uint32_t nv = r.u16(vars + 6), nh = r.u16(vars + 8);
    auto constructions = [&](size_t cov_off, uint32_t n, size_t first,
                             std::unordered_map<uint32_t, Construction>& out) {
      const std::vector<uint16_t> cov = read_coverage(r, vars + cov_off);
      for (uint32_t i = 0; i < n && i < cov.size(); ++i) {
        const size_t c = vars + r.u16(first + 2 * i);
        Construction con;
        const size_t asm_off = r.u16(c);
        const uint32_t nvar = r.u16(c + 2);
        for (uint32_t k = 0; k < nvar && k < 64; ++k)
          con.variants.push_back({r.u16(c + 4 + 4 * k),
                                  static_cast<float>(r.u16(c + 6 + 4 * k)) / upem_});
        if (asm_off) {
          const size_t a = c + asm_off;
          const uint32_t np = r.u16(a + 4);
          for (uint32_t k = 0; k < np && k < 32; ++k) {
            const size_t p = a + 6 + 10 * k;
            MathPart part;
            part.gid = r.u16(p);
            part.start_overlap = static_cast<float>(r.u16(p + 2)) / upem_;
            part.end_overlap = static_cast<float>(r.u16(p + 4)) / upem_;
            part.advance = static_cast<float>(r.u16(p + 6)) / upem_;
            part.extender = (r.u16(p + 8) & 1) != 0;
            con.parts.push_back(part);
          }
        }
        out[cov[i]] = std::move(con);
      }
    };
    const size_t vcov = r.u16(vars + 2), hcov = r.u16(vars + 4);
    constructions(vcov, nv, vars + 10, vert_);
    constructions(hcov, nh, vars + 10 + 2 * nv, horiz_);
  }
}

uint32_t MathFont::glyph(uint32_t cp) const {
  return face_ ? FT_Get_Char_Index(face_, cp) : 0;
}

MathGlyph MathFont::metrics(uint32_t gid) const {
  auto it = metrics_.find(gid);
  if (it != metrics_.end()) return it->second;
  MathGlyph g;
  if (face_ && FT_Load_Glyph(face_, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) == 0) {
    const FT_Glyph_Metrics& m = face_->glyph->metrics;
    g.advance = static_cast<float>(m.horiAdvance) / upem_;
    g.ascent = static_cast<float>(m.horiBearingY) / upem_;
    g.descent = static_cast<float>(m.height - m.horiBearingY) / upem_;
    g.lsb = static_cast<float>(m.horiBearingX) / upem_;
    g.ink_w = static_cast<float>(m.width) / upem_;
    if (auto i = italic_.find(gid); i != italic_.end()) g.italic = i->second;
    if (auto a = accent_.find(gid); a != accent_.end()) g.accent = a->second;
  }
  metrics_[gid] = g;
  return g;
}

std::vector<MathVariantGlyph> MathFont::variants(uint32_t gid,
                                                 bool vertical) const {
  const MathGlyph m = metrics(gid);
  std::vector<MathVariantGlyph> out;
  out.push_back({gid, vertical ? m.ascent + m.descent : m.advance});
  const auto& table = vertical ? vert_ : horiz_;
  auto it = table.find(gid);
  if (it != table.end())
    for (const MathVariantGlyph& v : it->second.variants)
      if (v.gid != gid) out.push_back(v);
  return out;
}

bool MathFont::assembly(uint32_t gid, bool vertical,
                        std::vector<MathPart>& parts) const {
  const auto& table = vertical ? vert_ : horiz_;
  auto it = table.find(gid);
  if (it == table.end() || it->second.parts.empty()) return false;
  parts = it->second.parts;
  return true;
}

const MathRaster& MathFont::raster(uint32_t gid, int px) {
  const uint64_t key = (static_cast<uint64_t>(gid) << 16) | static_cast<uint32_t>(px & 0xFFFF);
  auto it = rasters_.find(key);
  if (it != rasters_.end()) return it->second;
  if (rasters_.size() > 4000) drop_rasters();
  MathRaster out;
  if (face_ && px > 0) {
    FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(px));
    if (FT_Load_Glyph(face_, gid, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP) == 0) {
      FT_GlyphSlot slot = face_->glyph;
      out.w = static_cast<int>(slot->bitmap.width);
      out.h = static_cast<int>(slot->bitmap.rows);
      out.left = slot->bitmap_left;
      out.top = slot->bitmap_top;
      if (out.w > 0 && out.h > 0 && renderer_) {
        std::vector<uint8_t> rgba(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 4);
        for (int y = 0; y < out.h; ++y)
          for (int x = 0; x < out.w; ++x) {
            const size_t i = static_cast<size_t>(y * out.w + x);
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = slot->bitmap.buffer[static_cast<size_t>(y) *
                                                      static_cast<size_t>(slot->bitmap.pitch) +
                                                  static_cast<size_t>(x)];
          }
        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
            rgba.data(), out.w, out.h, 32, out.w * 4, 0x000000ff, 0x0000ff00,
            0x00ff0000, 0xff000000);
        if (surf) {
          out.texture = SDL_CreateTextureFromSurface(renderer_, surf);
          if (out.texture) SDL_SetTextureBlendMode(out.texture, SDL_BLENDMODE_BLEND);
          SDL_FreeSurface(surf);
        }
      }
    }
  }
  return rasters_.emplace(key, out).first->second;
}

std::string find_math_font_path() {
  if (std::string p = data_file("fonts/latinmodern-math.otf"); !p.empty())
    return p;
  static const char* const kFamilies[] = {
      "Latin Modern Math", "STIX Two Math", "Libertinus Math", "XITS Math",
      "TeX Gyre Pagella Math", "TeX Gyre Termes Math", "Asana Math",
      "Cambria Math", "Fira Math", "DejaVu Math TeX Gyre", "IBM Plex Math",
      "Noto Sans Math",
  };
  for (const char* family : kFamilies) {
    if (fontconfig_pick_family({family}).empty()) continue;
    std::string path = fontconfig_find(family, 0, 0);
    if (!path.empty()) return path;
  }
  return {};
}

void draw_math(SDL_Renderer* r, MathFont& font, const MathBox& box, float x,
               float baseline, float px, const Rgba& color) {
  for (const MathItem& it : box.items) {
    if (it.rule) {
      const int x0 = static_cast<int>(std::lround(x + it.x * px));
      const int x1 = static_cast<int>(std::lround(x + (it.x + it.w) * px));
      const int y0 = static_cast<int>(std::lround(baseline - (it.y + it.h) * px));
      int y1 = static_cast<int>(std::lround(baseline - it.y * px));
      if (y1 <= y0) y1 = y0 + 1;
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
      SDL_Rect rc{x0, y0, std::max(1, x1 - x0), y1 - y0};
      SDL_RenderFillRect(r, &rc);
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      continue;
    }
    const int gpx = std::max(1, static_cast<int>(std::lround(px * it.size)));
    const MathRaster& g = font.raster(it.gid, gpx);
    if (!g.texture) continue;
    SDL_Rect dst{static_cast<int>(std::lround(x + it.x * px)) + g.left,
                 static_cast<int>(std::lround(baseline - it.y * px)) - g.top, g.w,
                 g.h};
    SDL_SetTextureColorMod(g.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(g.texture, color.a);
    SDL_RenderCopy(r, g.texture, nullptr, &dst);
  }
}

}  // namespace xanatoo
