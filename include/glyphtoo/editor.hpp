// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/types.hpp"
#include "glyphtoo/layout.hpp"
#include <string>
#include <optional>
#include <vector>

namespace glyphtoo {

// Caret is pure document data — never stores pixels.
struct Caret {
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  size_t char_index = 0;  // codepoint index within the span

  bool operator==(const Caret& o) const {
    return node == o.node && span_index == o.span_index &&
           char_index == o.char_index;
  }
  bool operator!=(const Caret& o) const { return !(*this == o); }
};

struct CaretScreenPos {
  float x = 0.f;
  float y = 0.f;      // baseline
  float height = 0.f;
  bool valid = false;
};

// Selection is two carets. When anchor == focus the selection is empty.
struct Selection {
  Caret anchor;
  Caret focus;

  bool empty() const { return anchor == focus; }
  Caret start() const;  // document-order minimum
  Caret end() const;
};

// Map a screen point to a caret location using the current layout.
std::optional<Caret> hit_test(const LayoutResult& layout,
                              float sx, float sy, float scroll_y);

// Hit-test for stretchtext toggle boxes.
std::optional<NodeId> hit_test_stretch(const LayoutResult& layout,
                                       float sx, float sy, float scroll_y);

// Compute the on-screen position of a caret for drawing.
CaretScreenPos caret_screen_pos(const LayoutResult& layout,
                                const Caret& caret,
                                float scroll_y,
                                float line_height);

// Glyph ranges that form the selection highlight (screen-space, already
// adjusted for scroll).
struct SelRect {
  float x, y, w, h;
};
std::vector<SelRect> selection_rects(const LayoutResult& layout,
                                     const Selection& sel,
                                     float scroll_y,
                                     float line_height);

// Editing operations (mutate Document, caller re-layouts afterwards).
void insert_text(Document& doc, Caret& caret, const std::string& utf8);
void delete_backward(Document& doc, Caret& caret);
void delete_forward(Document& doc, Caret& caret);

// Split the current paragraph/heading at the caret into two sibling blocks.
// Caret moves to the start of the new block. Returns true on success.
bool split_block(Document& doc, Caret& caret);

// Delete the current selection (or nothing if empty). Caret becomes the start.
void delete_selection(Document& doc, Selection& sel);

// Move caret by one codepoint (within the node). Returns false if at boundary.
bool move_left(Document& doc, Caret& caret);
bool move_right(Document& doc, Caret& caret);

// Vertical movement using layout geometry (preferred x preserved by caller).
bool move_up(const LayoutResult& layout, Caret& caret, float preferred_x);
bool move_down(const LayoutResult& layout, Caret& caret, float preferred_x);

// Move to previous / next text-bearing node.
bool move_to_prev_node(Document& doc, Caret& caret);
bool move_to_next_node(Document& doc, Caret& caret);

void ensure_valid_caret(Document& doc, Caret& caret);

// Document-order comparison of two carets (-1, 0, +1).
int caret_cmp(const Document& doc, const Caret& a, const Caret& b);

}  // namespace glyphtoo
