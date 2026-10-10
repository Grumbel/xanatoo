// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/types.hpp"
#include "glyphtoo/layout.hpp"
#include <string>
#include <optional>

namespace glyphtoo {

// Caret is pure document data — never stores pixels.
struct Caret {
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  size_t char_index = 0;  // codepoint index within the span
};

struct CaretScreenPos {
  float x = 0.f;
  float y = 0.f;      // baseline
  float height = 0.f;
  bool valid = false;
};

// Map a screen point to a caret location using the current layout.
std::optional<Caret> hit_test(const LayoutResult& layout,
                              float sx, float sy, float scroll_y);

// Compute the on-screen position of a caret for drawing.
CaretScreenPos caret_screen_pos(const LayoutResult& layout,
                                const Caret& caret,
                                float scroll_y,
                                float line_height);

// Editing operations (mutate Document, caller re-layouts afterwards).
void insert_text(Document& doc, Caret& caret, const std::string& utf8);
void delete_backward(Document& doc, Caret& caret);
void delete_forward(Document& doc, Caret& caret);

// Move caret by one codepoint (within the node). Returns false if at boundary.
bool move_left(Document& doc, Caret& caret);
bool move_right(Document& doc, Caret& caret);

// Move to previous / next text-bearing node (simple linear order for now).
bool move_to_prev_node(Document& doc, Caret& caret);
bool move_to_next_node(Document& doc, Caret& caret);

// Ensure caret is inside a valid text node; if not, place at first text.
void ensure_valid_caret(Document& doc, Caret& caret);

}  // namespace glyphtoo
