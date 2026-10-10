// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/editor.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace glyphtoo {

static std::pair<uint32_t, size_t> decode_cp(const std::string& s, size_t byte_i) {
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
  return {c, 1};
}

static size_t utf8_char_count(const std::string& s) {
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

static size_t utf8_byte_offset(const std::string& s, size_t char_index) {
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

static bool is_text_node(NodeKind k) {
  return k == NodeKind::Paragraph || k == NodeKind::Heading ||
         k == NodeKind::ListItem;
}

static void collect_text_nodes(const Document& doc, NodeId id, std::vector<NodeId>& out) {
  const Node& n = doc.get(id);
  if (is_text_node(n.kind)) {
    out.push_back(id);
  }
  for (NodeId c : n.children) {
    collect_text_nodes(doc, c, out);
  }
}

struct CaretKey {
  size_t node_ord = 0;
  size_t span = 0;
  size_t ch = 0;
};

static CaretKey make_key(const Document& doc, const Caret& c) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  CaretKey k;
  auto it = std::find(nodes.begin(), nodes.end(), c.node);
  k.node_ord = (it == nodes.end()) ? nodes.size() : static_cast<size_t>(it - nodes.begin());
  k.span = c.span_index;
  k.ch = c.char_index;
  return k;
}

static bool key_less(const CaretKey& a, const CaretKey& b) {
  if (a.node_ord != b.node_ord) return a.node_ord < b.node_ord;
  if (a.span != b.span) return a.span < b.span;
  return a.ch < b.ch;
}

// ---- Selection helpers ----

int caret_cmp(const Document& doc, const Caret& a, const Caret& b) {
  CaretKey ka = make_key(doc, a);
  CaretKey kb = make_key(doc, b);
  if (key_less(ka, kb)) return -1;
  if (key_less(kb, ka)) return 1;
  return 0;
}

// ---- Hit testing ----

std::optional<Caret> hit_test(const LayoutResult& layout,
                              float sx, float sy, float scroll_y) {
  if (layout.glyphs.empty()) return std::nullopt;

  float best_dist = std::numeric_limits<float>::max();
  Caret best{};
  bool found = false;

  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    float gy = g.y - scroll_y;
    float gx = g.x;
    float gw = 8.f;
    if (i + 1 < layout.glyphs.size() &&
        layout.glyphs[i + 1].node == g.node &&
        std::abs(layout.glyphs[i + 1].y - g.y) < 0.5f) {
      gw = layout.glyphs[i + 1].x - g.x;
      if (gw < 1.f) gw = 8.f;
    }
    float cx = gx + gw * 0.5f;
    float cy = gy - 8.f;
    float dx = sx - cx;
    float dy = sy - cy;
    float d = dx * dx + dy * dy * 3.f;
    if (d < best_dist) {
      best_dist = d;
      best.node = g.node;
      best.span_index = g.span_index;
      best.char_index = g.char_index + (sx > cx ? 1 : 0);
      found = true;
    }
  }
  if (!found) return std::nullopt;
  return best;
}

std::optional<NodeId> hit_test_stretch(const LayoutResult& layout,
                                       float sx, float sy, float scroll_y) {
  for (const auto& box : layout.boxes) {
    // Stretch boxes are marked by the layout engine via a convention:
    // we store stretch node ids in boxes; caller checks node kind.
    float by = box.y - scroll_y;
    if (sx >= box.x && sx <= box.x + box.width &&
        sy >= by && sy <= by + box.height) {
      return box.node;
    }
  }
  return std::nullopt;
}

CaretScreenPos caret_screen_pos(const LayoutResult& layout,
                                const Caret& caret,
                                float scroll_y,
                                float line_height) {
  CaretScreenPos pos;
  pos.height = line_height;

  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    if (g.node != caret.node || g.span_index != caret.span_index) continue;

    if (g.char_index == caret.char_index) {
      pos.x = g.x;
      pos.y = g.y - scroll_y;
      pos.valid = true;
      return pos;
    }
    if (g.char_index < caret.char_index) {
      float gw = 8.f;
      if (i + 1 < layout.glyphs.size() &&
          layout.glyphs[i + 1].node == g.node &&
          layout.glyphs[i + 1].span_index == g.span_index &&
          std::abs(layout.glyphs[i + 1].y - g.y) < 0.5f) {
        gw = layout.glyphs[i + 1].x - g.x;
      }
      pos.x = g.x + gw;
      pos.y = g.y - scroll_y;
      pos.valid = true;
    }
  }

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

std::vector<SelRect> selection_rects(const LayoutResult& layout,
                                     const Selection& sel,
                                     float scroll_y,
                                     float line_height) {
  std::vector<SelRect> rects;
  if (sel.empty() || layout.glyphs.empty()) return rects;

  // Build ordered range by scanning glyphs and checking against caret keys
  // We use a simple approach: collect glyphs whose (node,span,char) falls
  // between the two carets. Without doc here we approximate by glyph order
  // which matches document order for a single layout pass.

  auto glyph_key = [](const PositionedGlyph& g) {
    return std::tuple{g.node, g.span_index, g.char_index};
  };

  auto a = glyph_key(PositionedGlyph{
      0, 0, 0, sel.anchor.node, sel.anchor.span_index, sel.anchor.char_index});
  auto b = glyph_key(PositionedGlyph{
      0, 0, 0, sel.focus.node, sel.focus.span_index, sel.focus.char_index});
  if (a > b) std::swap(a, b);

  // Group selected glyphs by baseline into runs
  struct Run {
    float x0, x1, y;
  };
  std::vector<Run> runs;
  Run cur{};
  bool in_run = false;

  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    auto k = glyph_key(g);
    // Include glyph if its position is >= start and < end (caret is between)
    bool selected = (k >= a && k < b);
    // Also include the glyph right at the end caret if it's exclusive — skip
    if (!selected) {
      if (in_run) {
        runs.push_back(cur);
        in_run = false;
      }
      continue;
    }
    float gw = 8.f;
    if (i + 1 < layout.glyphs.size() &&
        layout.glyphs[i + 1].node == g.node &&
        std::abs(layout.glyphs[i + 1].y - g.y) < 0.5f) {
      gw = layout.glyphs[i + 1].x - g.x;
    }
    if (!in_run) {
      cur = {g.x, g.x + gw, g.y};
      in_run = true;
    } else if (std::abs(g.y - cur.y) < 0.5f) {
      cur.x1 = g.x + gw;
    } else {
      runs.push_back(cur);
      cur = {g.x, g.x + gw, g.y};
    }
  }
  if (in_run) runs.push_back(cur);

  for (const auto& r : runs) {
    SelRect sr;
    sr.x = r.x0;
    sr.y = r.y - line_height * 0.85f - scroll_y;
    sr.w = std::max(2.f, r.x1 - r.x0);
    sr.h = line_height * 0.95f;
    rects.push_back(sr);
  }
  return rects;
}

// ---- Editing ----

void insert_text(Document& doc, Caret& caret, const std::string& utf8) {
  if (caret.node == INVALID_NODE || utf8.empty()) return;
  // Reject control chars except we allow nothing special here
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
    if (caret.span_index > 0) {
      --caret.span_index;
      caret.char_index = utf8_char_count(n.spans[caret.span_index].text);
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

bool split_block(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (!is_text_node(n.kind)) return false;

  // Create a new sibling of the same kind
  NodeId new_id = doc.create(n.kind, INVALID_NODE);  // parent set by insert_after
  Node& neu = doc.get(new_id);
  neu.level = n.level;

  // Move spans after the caret into the new node
  if (!n.spans.empty() && caret.span_index < n.spans.size()) {
    InlineSpan& cur = n.spans[caret.span_index];
    size_t byte_off = utf8_byte_offset(cur.text, caret.char_index);

    // Right half of current span
    InlineSpan right;
    right.kind = cur.kind;
    right.href = cur.href;
    right.target = cur.target;
    right.text = cur.text.substr(byte_off);
    cur.text.erase(byte_off);

    // Collect remaining spans
    std::vector<InlineSpan> moved;
    if (!right.text.empty()) moved.push_back(std::move(right));
    for (size_t i = caret.span_index + 1; i < n.spans.size(); ++i) {
      moved.push_back(std::move(n.spans[i]));
    }
    n.spans.resize(caret.span_index + 1);
    // Drop empty trailing span
    if (!n.spans.empty() && n.spans.back().text.empty() && n.spans.size() > 1) {
      n.spans.pop_back();
    }
    neu.spans = std::move(moved);
  }

  if (!doc.insert_after(caret.node, new_id)) {
    // Fallback: add as child of parent
    if (n.parent != INVALID_NODE) {
      doc.add_child(n.parent, new_id);
    } else {
      return false;
    }
  }

  caret.node = new_id;
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

static void erase_range_in_node(Node& n, size_t from_span, size_t from_ch,
                                size_t to_span, size_t to_ch) {
  if (n.spans.empty()) return;
  auto erase_in_span = [&](size_t si, size_t from, size_t to) {
    if (si >= n.spans.size()) return;
    InlineSpan& sp = n.spans[si];
    size_t from_b = utf8_byte_offset(sp.text, from);
    size_t to_b = utf8_byte_offset(sp.text, to);
    if (to_b > from_b) sp.text.erase(from_b, to_b - from_b);
  };
  if (from_span == to_span) {
    erase_in_span(from_span, from_ch, to_ch);
    return;
  }
  size_t start_len = utf8_char_count(n.spans[from_span].text);
  erase_in_span(from_span, from_ch, start_len);
  for (size_t si = from_span + 1; si < to_span && si < n.spans.size(); ++si) {
    n.spans[si].text.clear();
  }
  if (to_span < n.spans.size()) {
    erase_in_span(to_span, 0, to_ch);
  }
}

void delete_selection(Document& doc, Selection& sel) {
  if (sel.empty()) return;
  Caret a = sel.anchor;
  Caret b = sel.focus;
  if (caret_cmp(doc, a, b) > 0) std::swap(a, b);

  if (a.node == b.node) {
    Node& n = doc.get(a.node);
    erase_range_in_node(n, a.span_index, a.char_index, b.span_index, b.char_index);
    sel.anchor = sel.focus = a;
    return;
  }

  // Multi-node: clear from caret a to end of start node, clear full middle
  // text nodes, clear from start of end node to caret b.
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);

  auto rank = [&](NodeId id) -> size_t {
    auto it = std::find(nodes.begin(), nodes.end(), id);
    return it == nodes.end() ? nodes.size() : static_cast<size_t>(it - nodes.begin());
  };
  size_t ra = rank(a.node);
  size_t rb = rank(b.node);
  if (ra > rb) {
    std::swap(a, b);
    std::swap(ra, rb);
  }

  // Start node: from a to end
  {
    Node& n = doc.get(a.node);
    if (!n.spans.empty()) {
      size_t last = n.spans.size() - 1;
      size_t last_len = utf8_char_count(n.spans[last].text);
      erase_range_in_node(n, a.span_index, a.char_index, last, last_len);
    }
  }
  // Middle nodes: clear all text
  for (size_t i = ra + 1; i < rb && i < nodes.size(); ++i) {
    doc.get(nodes[i]).spans.clear();
  }
  // End node: from start to b
  {
    Node& n = doc.get(b.node);
    if (!n.spans.empty()) {
      erase_range_in_node(n, 0, 0, b.span_index, b.char_index);
    }
  }

  sel.anchor = sel.focus = a;
}

// ---- Movement ----

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

bool move_up(const LayoutResult& layout, Caret& caret, float preferred_x) {
  // Find current baseline
  float cur_y = -1.f;
  for (const auto& g : layout.glyphs) {
    if (g.node == caret.node && g.span_index == caret.span_index &&
        g.char_index == caret.char_index) {
      cur_y = g.y;
      break;
    }
  }
  if (cur_y < 0.f) {
    // Try nearest glyph of this node
    for (const auto& g : layout.glyphs) {
      if (g.node == caret.node) {
        cur_y = g.y;
        break;
      }
    }
  }
  if (cur_y < 0.f) return false;

  // Find the highest baseline strictly above cur_y
  float target_y = -1.f;
  for (const auto& g : layout.glyphs) {
    if (g.y < cur_y - 0.5f) {
      if (target_y < 0.f || g.y > target_y) target_y = g.y;
    }
  }
  if (target_y < 0.f) return false;

  // Pick glyph on that baseline closest in x to preferred_x
  float best_dx = std::numeric_limits<float>::max();
  Caret best = caret;
  bool found = false;
  for (const auto& g : layout.glyphs) {
    if (std::abs(g.y - target_y) > 0.5f) continue;
    float dx = std::abs(g.x - preferred_x);
    if (dx < best_dx) {
      best_dx = dx;
      best.node = g.node;
      best.span_index = g.span_index;
      best.char_index = g.char_index;
      found = true;
    }
  }
  if (found) {
    caret = best;
    return true;
  }
  return false;
}

bool move_down(const LayoutResult& layout, Caret& caret, float preferred_x) {
  float cur_y = -1.f;
  for (const auto& g : layout.glyphs) {
    if (g.node == caret.node && g.span_index == caret.span_index &&
        g.char_index == caret.char_index) {
      cur_y = g.y;
      break;
    }
  }
  if (cur_y < 0.f) {
    for (const auto& g : layout.glyphs) {
      if (g.node == caret.node) {
        cur_y = g.y;
        break;
      }
    }
  }
  if (cur_y < 0.f) return false;

  float target_y = -1.f;
  for (const auto& g : layout.glyphs) {
    if (g.y > cur_y + 0.5f) {
      if (target_y < 0.f || g.y < target_y) target_y = g.y;
    }
  }
  if (target_y < 0.f) return false;

  float best_dx = std::numeric_limits<float>::max();
  Caret best = caret;
  bool found = false;
  for (const auto& g : layout.glyphs) {
    if (std::abs(g.y - target_y) > 0.5f) continue;
    float dx = std::abs(g.x - preferred_x);
    if (dx < best_dx) {
      best_dx = dx;
      best.node = g.node;
      best.span_index = g.span_index;
      best.char_index = g.char_index;
      found = true;
    }
  }
  if (found) {
    caret = best;
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


std::string node_text(const Document& doc, NodeId id) {
  const Node& n = doc.get(id);
  std::string out;
  for (const auto& sp : n.spans) {
    out += sp.text;
  }
  return out;
}

static std::string slice_node_text(const Node& n, size_t from_span, size_t from_ch,
                                   size_t to_span, size_t to_ch) {
  if (n.spans.empty()) return {};
  auto slice_span = [](const InlineSpan& sp, size_t from, size_t to) {
    size_t from_b = utf8_byte_offset(sp.text, from);
    size_t to_b = utf8_byte_offset(sp.text, to);
    if (to_b < from_b) return std::string{};
    return sp.text.substr(from_b, to_b - from_b);
  };
  std::string out;
  if (from_span == to_span) {
    if (from_span < n.spans.size()) {
      out = slice_span(n.spans[from_span], from_ch, to_ch);
    }
    return out;
  }
  if (from_span < n.spans.size()) {
    size_t len = utf8_char_count(n.spans[from_span].text);
    out += slice_span(n.spans[from_span], from_ch, len);
  }
  for (size_t si = from_span + 1; si < to_span && si < n.spans.size(); ++si) {
    out += n.spans[si].text;
  }
  if (to_span < n.spans.size()) {
    out += slice_span(n.spans[to_span], 0, to_ch);
  }
  return out;
}

std::string selected_text(const Document& doc, const Selection& sel) {
  if (sel.empty()) return {};
  Caret a = sel.anchor;
  Caret b = sel.focus;
  if (caret_cmp(doc, a, b) > 0) std::swap(a, b);

  if (a.node == b.node) {
    return slice_node_text(doc.get(a.node), a.span_index, a.char_index,
                           b.span_index, b.char_index);
  }

  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  auto rank = [&](NodeId id) -> size_t {
    auto it = std::find(nodes.begin(), nodes.end(), id);
    return it == nodes.end() ? nodes.size() : static_cast<size_t>(it - nodes.begin());
  };
  size_t ra = rank(a.node);
  size_t rb = rank(b.node);
  if (ra > rb) {
    std::swap(a, b);
    std::swap(ra, rb);
  }

  std::string out;
  // Start node: from a to end
  {
    const Node& n = doc.get(a.node);
    if (!n.spans.empty()) {
      size_t last = n.spans.size() - 1;
      size_t last_len = utf8_char_count(n.spans[last].text);
      out += slice_node_text(n, a.span_index, a.char_index, last, last_len);
    }
  }
  for (size_t i = ra + 1; i < rb && i < nodes.size(); ++i) {
    if (!out.empty()) out += "\n\n";
    out += node_text(doc, nodes[i]);
  }
  {
    const Node& n = doc.get(b.node);
    if (!n.spans.empty()) {
      if (!out.empty()) out += "\n\n";
      out += slice_node_text(n, 0, 0, b.span_index, b.char_index);
    }
  }
  return out;
}

// ---- History ----

History::History(size_t max_depth) : max_depth_(max_depth) {}

void History::reset(const Document& doc, const Selection& sel) {
  undo_.clear();
  redo_.clear();
  undo_.push_back({doc, sel});
}

void History::push(const Document& doc, const Selection& sel) {
  redo_.clear();
  undo_.push_back({doc, sel});
  if (undo_.size() > max_depth_) {
    undo_.erase(undo_.begin());
  }
}

bool History::undo(Document& doc, Selection& sel) {
  if (undo_.size() <= 1) return false;
  redo_.push_back({doc, sel});
  undo_.pop_back();
  doc = undo_.back().doc;
  sel = undo_.back().sel;
  return true;
}

bool History::redo(Document& doc, Selection& sel) {
  if (redo_.empty()) return false;
  undo_.push_back({doc, sel});
  doc = redo_.back().doc;
  sel = redo_.back().sel;
  redo_.pop_back();
  return true;
}

}  // namespace glyphtoo
