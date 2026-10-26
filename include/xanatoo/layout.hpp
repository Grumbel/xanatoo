// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/images.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "xanatoo/view_settings.hpp"

namespace xanatoo {

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
  // Vertical bars for notes (distinct colour from quotes).
  std::vector<LayoutBox> note_bars;
  // Vertical bars for messages / comments (thread depth uses x indent).
  std::vector<LayoutBox> message_bars;
  // Soft background rectangles for fenced code blocks.
  std::vector<LayoutBox> code_blocks;
  // Table grid lines. height <= 2 → horizontal; width <= 2 → vertical.
  std::vector<LayoutBox> table_lines;
  // Soft fill under header cells (TableCell level > 0).
  std::vector<LayoutBox> table_header_fills;
  // Horizontal rules (x, y = center line, width, height = stroke thickness).
  std::vector<LayoutBox> hr_lines;
  std::vector<ColumnGap> column_gaps;
  float content_height = 0.f;
  // True when a viewport was active and some sequential blocks were skipped
  // via height hints (glyphs only materialised near the view).
  bool virtualized = false;
  // Document Y band that was fully materialised (top − overscan … bottom + overscan).
  float materialised_top = 0.f;
  float materialised_bottom = 0.f;
  // Unique per LayoutEngine::layout call (0 = empty/default). Lets the pane
  // raster cache tell a fresh layout from the one it last drew.
  uint64_t generation = 0;
};

// Optional viewport for large-document virtualization.
// When bottom > top, sequential block children outside
// [top − overscan, bottom + overscan] may be skipped if a height hint exists.
struct LayoutViewport {
  float top = 0.f;
  float bottom = 0.f;
  float overscan = 1200.f;
  bool active() const { return bottom > top; }
};

// Drop cached external documents used by :::include path.md[…].
// Call after reload so changed includes are picked up.
void clear_external_doc_cache();

// Per-layout rendering options (from ViewSettings). Geometry only.
struct LayoutOptions {
  float margin_x = 32.f;
  float margin_y = 24.f;
  float line_spacing = 1.30f;
  float max_content_width = 0.f;  // 0 = fill pane
  bool justify = false;

  static LayoutOptions from_view(const ViewSettings& v) {
    LayoutOptions o;
    o.margin_x = v.margin_x;
    o.margin_y = v.margin_y;
    o.line_spacing = v.line_spacing;
    o.max_content_width = v.max_content_width;
    o.justify = v.justify;
    return o;
  }
};

class LayoutEngine {
public:
  LayoutEngine(FontSet& fonts, ImageCache* images = nullptr);

  void set_options(const LayoutOptions& o) { options_ = o; }
  const LayoutOptions& options() const { return options_; }

  // height_hints: optional prior block heights (from a previous LayoutResult's
  // boxes). Used only when viewport is active to skip off-screen sequential
  // blocks without re-measuring them.
  // folded_sections: view-local set of Section/Article ids whose body is
  // hidden (header still laid out). Not stored in the document.
  // focus_section: when not INVALID_NODE, only that Section/Article (and its
  // descendants) is fully laid out; ancestors show header only; other
  // branches are omitted. View filter — document unchanged.
  LayoutResult layout(const Document& doc, float max_width,
                      const std::string& image_base = {},
                      const LayoutViewport& viewport = {},
                      const std::unordered_map<NodeId, float>* height_hints = nullptr,
                      const std::unordered_set<NodeId>* folded_sections = nullptr,
                      NodeId focus_section = INVALID_NODE);

private:
  FontSet& fonts_;
  ImageCache* images_;
  LayoutOptions options_{};

  std::string image_base_;
  LayoutViewport viewport_;
  const std::unordered_map<NodeId, float>* height_hints_ = nullptr;
  const std::unordered_set<NodeId>* folded_sections_ = nullptr;
  NodeId focus_section_ = INVALID_NODE;
  bool skipped_any_ = false;

  // If the block has a height hint and lies fully outside the materialised
  // band, advance y by that height, emit a stub box, and return true.
  bool try_skip_block(NodeId id, float x, float& y, float max_width,
                      LayoutResult& out);

  void layout_children_sequential(const Document& doc,
                                  const std::vector<NodeId>& children,
                                  float x, float& y, float max_width,
                                  LayoutResult& out);

  void layout_node(const Document& doc, NodeId id,
                   float x, float& y, float max_width,
                   LayoutResult& out);
  void layout_paragraph(const Document& doc, const Node& node,
                        float x, float& y, float max_width,
                        LayoutResult& out, FontRole base_role);
};

}  // namespace xanatoo
