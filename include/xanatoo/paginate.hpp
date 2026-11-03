// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/editor.hpp"
#include "xanatoo/layout.hpp"
#include <cstdint>
#include <vector>

namespace xanatoo {

// Paged reading (docs/PAGED.md): a continuous layout, made as wide as one
// column, is cut into pages at line boundaries. Pages are stacked into
// "spreads" of `columns` pages side by side, spread s at content y
// s * page_h, so the result is an ordinary LayoutResult: hit-testing,
// carets, selections and overlays work on it unchanged, and a view just
// scrolls in whole spreads.
struct PageSpec {
  float page_h = 600.f;      // one spread: the height of the page area
  float pad_top = 24.f;      // blank above / below the text of a page
  float pad_bottom = 24.f;
  int columns = 1;
  float pitch = 600.f;       // x distance of neighbouring columns
  float first_y = 24.f;      // where the text of the layout starts / ends
  float last_y = 0.f;        // (0: content_height - first_y)
  GlyphExtents extents;      // ascent / descent per FontRole
  float px_scale = 1.f;
};

// Where the pages lie in the continuous layout.
struct PageMap {
  struct Page {
    float start = 0.f;  // source y range [start, end)
    float end = 0.f;
    uint32_t first_glyph = 0;  // index into the glyphs of the first one
  };
  std::vector<Page> pages;
  int columns = 1;
  float page_h = 0.f;
  size_t glyph_count = 0;

  size_t page_count() const { return pages.size(); }
  size_t spread_count() const {
    return (pages.size() + static_cast<size_t>(columns) - 1) /
           static_cast<size_t>(columns);
  }
};

// Cut `in` into pages and stack them. `map` receives the page ranges.
LayoutResult paginate(const LayoutResult& in, const PageSpec& spec,
                      PageMap& map);

// "Locations" are the font-independent counterpart of page numbers: one per
// kLocationGlyphs glyphs of text (Kindle style), so they stay put when the
// font size or the window changes.
constexpr size_t kLocationGlyphs = 128;
inline size_t glyph_location(size_t glyph_index) {
  return glyph_index / kLocationGlyphs + 1;
}

}  // namespace xanatoo
