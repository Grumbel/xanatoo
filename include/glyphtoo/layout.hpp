// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/types.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/images.hpp"
#include <vector>
#include <string>

namespace glyphtoo {

struct PositionedGlyph {
  uint32_t codepoint = 0;
  float x = 0.f;
  float y = 0.f;   // baseline
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  size_t char_index = 0;
  InlineKind kind = InlineKind::Text;
  FontRole font = FontRole::Body;
};

struct LayoutBox {
  NodeId node = INVALID_NODE;
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
};

struct PositionedImage {
  NodeId node = INVALID_NODE;
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
  std::string src;
  std::string base;  // directory for relative path resolution
};

// Draggable gap between two Column children under a Columns node.
struct ColumnGap {
  NodeId columns = INVALID_NODE;
  NodeId left = INVALID_NODE;
  NodeId right = INVALID_NODE;
  float x = 0.f;       // center of the gutter
  float y = 0.f;
  float height = 0.f;
};

struct LayoutResult {
  std::vector<PositionedGlyph> glyphs;
  std::vector<LayoutBox> boxes;
  std::vector<PositionedImage> images;
  // Vertical bars for block quotes (content coordinates).
  std::vector<LayoutBox> quote_bars;
  // Horizontal rules (x, y = center line, width, height = stroke thickness).
  std::vector<LayoutBox> hr_lines;
  std::vector<ColumnGap> column_gaps;
  float content_height = 0.f;
};

// Drop cached external documents used by :::include path.md[…].
// Call after reload so changed includes are picked up.
void clear_external_doc_cache();

class LayoutEngine {
public:
  LayoutEngine(FontSet& fonts, ImageCache* images = nullptr);

  LayoutResult layout(const Document& doc, float max_width,
                      const std::string& image_base = {});

private:
  FontSet& fonts_;
  ImageCache* images_;

  std::string image_base_;

  void layout_node(const Document& doc, NodeId id,
                   float x, float& y, float max_width,
                   LayoutResult& out);
  void layout_paragraph(const Document& doc, const Node& node,
                        float x, float& y, float max_width,
                        LayoutResult& out, FontRole base_role);
};

}  // namespace glyphtoo
