// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/editor.hpp"
#include <cmath>
#include <algorithm>

namespace glyphtoo {

namespace {

// Decode one UTF-8 codepoint starting at byte offset; returns (cp, bytes_consumed)
std::pair<uint32_t, size_t> decode_cp(const std::string& s, size_t byte_i) {
  if (byte_i >= s.size()) return {0, 0};
  unsigned char c = static_cast<unsigned char>(s[byte_i]);
  if (c < 0x80) return {c, 1};
  if ((c & 0xe0) == 0xc0 && byte_i + 1 < s.size())
    return {((c & 0x1f) << 6) | (s[byte_i + 1] & 0x3f), 2};
  if ((c & 0xf0) == 0xe0 && byte_i + 2 < s.size())
    return {((c & 0x0f) << 12) | ((s[byte_i + 1] & 0x3f) << 6) | (s[byte_i + 2] & 0x3f), 3};
  if ((c & 0xf8) == 0xf0 && byte_i + 3 < s.size())
    return {((c & 0x07) << 18) | ((s[byte_i + 1] & 0x3f) << 12) |
            ((s[byte_i + 2] & 0x3f) << 6) | (s[byte_i + 3] & 0x3f), 4};
  return {c, 1};  // invalid, skip one byte
}

size_t utf8_char_count(const std::string& s) {
  size_t n = 0, i = 0;
  while (i < s.size()) {
    auto [cp, len] = decode_cp(s, i);
    (void)cp;
    if (len == 0) break;
    i += len;
    ++n;
  }
  return n;
}

// Byte offset of the N-th codepoint (or end)
size_t utf8_byte_offset(const std::string& s, size_t char_index) {
  size_t i = 0, n = 0;
  while (i < s.size() && n < char_index) {
    auto [cp, len] = decode_cp(s, i);
    (void)cp;
    if (len == 0) break;
    i += len;
    ++n;
  }
  return i;
}

bool is_text_node(NodeKind k) {
  return k == NodeKind::Paragraph || k == NodeKind::Heading ||
         k == NodeKind::ListItem;
}

// Collect text-bearing nodes in document order (depth-first)
void collect_text_nodes(const Document& doc, NodeId id, std::vector<NodeId>& out) {
  const Node& n = doc.get(id);
  if (is_text_node(n.kind) && !n.spans.empty()) {
    out.push_back(id);
  }
  for (NodeId c : n.children) {
    collect_text_nodes(doc, c, out);
  }
}

}  // namespace

std::optional<Caret> hit_test(const LayoutResult& layout,
                              float sx, float sy, float scroll_y) {
  if (layout.glyphs.empty()) return std::nullopt;

  float best_dist = 1e30f;
  Caret best{};
  bool found = false;

  // Prefer the glyph whose baseline is closest vertically, then x
  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    float gy = g.y - scroll_y;
    float gx = g.x;
    // Approximate glyph width from next glyph or fixed
    float gw = 8.f;
    if (i + 1 < layout.glyphs.size() &&
        layout.glyphs[i + 1].node == g.node &&
        layout.glyphs[i + 1].y == g.y) {
      gw = layout.glyphs[i + 1].x - g.x;
    }
    float cx = gx + gw * 0.5f;
    float cy = gy - 6.f;  // mid of line roughly
    float dx = sx - cx;
    float dy = sy - cy;
    float d = dx * dx + dy * dy * 4.f;  // prefer horizontal matches a bit less
    if (d < best_dist) {
      best_dist = d;
      best.node = g.node;
      best.span_index = g.span_index;
      // Place after this glyph if click is past midpoint
      best.char_index = g.char_index + (sx > cx ? 1 : 0);
      found = true;
    }
  }

  if (!found) return std::nullopt;
  return best;
}

CaretScreenPos caret_screen_pos(const LayoutResult& layout,
                                const Caret& caret,
                                float scroll_y,
                                float line_height) {
  CaretScreenPos pos;
  pos.height = line_height;

  // Find the glyph at or just before the caret
  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    if (g.node != caret.node) continue;
    if (g.span_index != caret.span_index) continue;

    if (g.char_index == caret.char_index) {
      pos.x = g.x;
      pos.y = g.y - scroll_y;
      pos.valid = true;
      return pos;
    }
    // If caret is past this glyph, remember its right edge
    if (g.char_index < caret.char_index) {
      float gw = 8.f;
      if (i + 1 < layout.glyphs.size() &&
          layout.glyphs[i + 1].node == g.node &&
          layout.glyphs[i + 1].span_index == g.span_index &&
          layout.glyphs[i + 1].y == g.y) {
        gw = layout.glyphs[i + 1].x - g.x;
      } else {
        // last glyph of line — approximate
        gw = 8.f;
      }
      pos.x = g.x + gw;
      pos.y = g.y - scroll_y;
      pos.valid = true;
    }
  }

  // Caret at end of empty span / node: try to use a layout box
  if (!pos.valid) {
    for (const auto& box : layout.boxes) {
      if (box.node == caret.node) {
        pos.x = box.x;
        pos.y = box.y + line_height * 0.75f - scroll_y;
        pos.valid = true;
        break;
      }
    }
  }
  return pos;
}

void insert_text(Document& doc, Caret& caret, const std::string& utf8) {
  if (caret.node == INVALID_NODE || utf8.empty()) return;
  Node& n = doc.get(caret.node);
  if (n.spans.empty()) {
    InlineSpan s;
    s.kind = InlineKind::Text;
    s.text = utf8;
    n.spans.push_back(std::move(s));
    caret.span_index = 0;
    caret.char_index = utf8_char_count(utf8);
    return;
  }
  if (caret.span_index >= n.spans.size()) {
    caret.span_index = n.spans.size() - 1;
    caret.char_index = utf8_char_count(n.spans.back().text);
  }
  InlineSpan& span = n.spans[caret.span_index];
  size_t byte_off = utf8_byte_offset(span.text, caret.char_index);
  span.text.insert(byte_off, utf8);
  caret.char_index += utf8_char_count(utf8);
}

void delete_backward(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return;
  Node& n = doc.get(caret.node);
  if (n.spans.empty()) return;

  if (caret.char_index == 0) {
    // Try previous span
    if (caret.span_index > 0) {
      --caret.span_index;
      caret.char_index = utf8_char_count(n.spans[caret.span_index].text);
      // fall through to delete last char of previous span
    } else {
      return;
    }
  }

  InlineSpan& span = n.spans[caret.span_index];
  if (span.text.empty()) return;

  size_t char_count = utf8_char_count(span.text);
  if (caret.char_index > char_count) caret.char_index = char_count;
  if (caret.char_index == 0) return;

  size_t end_byte = utf8_byte_offset(span.text, caret.char_index);
  size_t start_byte = utf8_byte_offset(span.text, caret.char_index - 1);
  span.text.erase(start_byte, end_byte - start_byte);
  --caret.char_index;
}

void delete_forward(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return;
  Node& n = doc.get(caret.node);
  if (n.spans.empty() || caret.span_index >= n.spans.size()) return;

  InlineSpan& span = n.spans[caret.span_index];
  size_t char_count = utf8_char_count(span.text);
  if (caret.char_index >= char_count) {
    // next span
    if (caret.span_index + 1 < n.spans.size()) {
      ++caret.span_index;
      caret.char_index = 0;
      delete_forward(doc, caret);
    }
    return;
  }
  size_t start_byte = utf8_byte_offset(span.text, caret.char_index);
  size_t end_byte = utf8_byte_offset(span.text, caret.char_index + 1);
  span.text.erase(start_byte, end_byte - start_byte);
}

bool move_left(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (caret.char_index > 0) {
    --caret.char_index;
    return true;
  }
  if (caret.span_index > 0) {
    --caret.span_index;
    caret.char_index = utf8_char_count(n.spans[caret.span_index].text);
    return true;
  }
  return false;
}

bool move_right(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (caret.span_index >= n.spans.size()) return false;
  size_t len = utf8_char_count(n.spans[caret.span_index].text);
  if (caret.char_index < len) {
    ++caret.char_index;
    return true;
  }
  if (caret.span_index + 1 < n.spans.size()) {
    ++caret.span_index;
    caret.char_index = 0;
    return true;
  }
  return false;
}

bool move_to_prev_node(Document& doc, Caret& caret) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  auto it = std::find(nodes.begin(), nodes.end(), caret.node);
  if (it == nodes.begin() || it == nodes.end()) return false;
  --it;
  caret.node = *it;
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

bool move_to_next_node(Document& doc, Caret& caret) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  auto it = std::find(nodes.begin(), nodes.end(), caret.node);
  if (it == nodes.end() || (it + 1) == nodes.end()) return false;
  ++it;
  caret.node = *it;
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

void ensure_valid_caret(Document& doc, Caret& caret) {
  if (caret.node != INVALID_NODE) {
    const Node& n = doc.get(caret.node);
    if (is_text_node(n.kind)) {
      if (n.spans.empty()) {
        caret.span_index = 0;
        caret.char_index = 0;
        return;
      }
      if (caret.span_index >= n.spans.size()) {
        caret.span_index = n.spans.size() - 1;
        caret.char_index = utf8_char_count(n.spans.back().text);
      }
      return;
    }
  }
  // Place at first text node
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  if (!nodes.empty()) {
    caret.node = nodes.front();
    caret.span_index = 0;
    caret.char_index = 0;
  } else {
    caret = {};
  }
}

}  // namespace glyphtoo
