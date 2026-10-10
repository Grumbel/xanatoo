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
         k == NodeKind::ListItem || k == NodeKind::BlockQuote;
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


void insert_styled(Document& doc, Caret& caret, const std::string& utf8,
                   InlineKind kind, const std::string& href) {
  if (caret.node == INVALID_NODE || utf8.empty()) return;
  Node& n = doc.get(caret.node);
  if (n.spans.empty()) {
    InlineSpan s;
    s.kind = kind;
    s.text = utf8;
    s.href = href;
    n.spans.push_back(std::move(s));
    caret.span_index = 0;
    caret.char_index = utf8_char_count(utf8);
    return;
  }
  if (caret.span_index >= n.spans.size()) {
    caret.span_index = n.spans.size() - 1;
    caret.char_index = utf8_char_count(n.spans.back().text);
  }

  size_t si = caret.span_index;
  InlineSpan& cur = n.spans[si];
  size_t byte_off = utf8_byte_offset(cur.text, caret.char_index);
  std::string before = cur.text.substr(0, byte_off);
  std::string after = cur.text.substr(byte_off);

  InlineSpan mid;
  mid.kind = kind;
  mid.text = utf8;
  mid.href = href;

  // Rebuild: before | mid | after (drop empty before/after)
  std::vector<InlineSpan> rebuilt;
  for (size_t i = 0; i < si; ++i) rebuilt.push_back(std::move(n.spans[i]));
  if (!before.empty()) {
    InlineSpan b = cur;
    b.text = before;
    rebuilt.push_back(std::move(b));
  }
  size_t mid_index = rebuilt.size();
  rebuilt.push_back(std::move(mid));
  if (!after.empty()) {
    InlineSpan a = cur;
    a.text = after;
    a.href = (a.kind == InlineKind::Link) ? a.href : std::string{};
    rebuilt.push_back(std::move(a));
  }
  for (size_t i = si + 1; i < n.spans.size(); ++i)
    rebuilt.push_back(std::move(n.spans[i]));
  n.spans = std::move(rebuilt);
  caret.span_index = mid_index;
  caret.char_index = utf8_char_count(utf8);
}

static void apply_style_one_node(Document& doc, NodeId nid,
                                 size_t from_span, size_t from_ch,
                                 size_t to_span, size_t to_ch,
                                 InlineKind kind, const std::string& href) {
  Selection local;
  local.anchor = {nid, from_span, from_ch};
  local.focus = {nid, to_span, to_ch};
  if (local.empty()) return;

  std::string text = selected_text(doc, local);
  if (text.empty()) return;

  const Node& n = doc.get(nid);
  bool already = false;
  if (from_span == to_span && from_span < n.spans.size()) {
    const InlineSpan& sp = n.spans[from_span];
    if (sp.kind == kind && from_ch == 0 &&
        to_ch == utf8_char_count(sp.text)) {
      already = true;
    }
  }

  delete_selection(doc, local);
  if (already) {
    insert_text(doc, local.focus, text);
  } else {
    insert_styled(doc, local.focus, text, kind, href);
  }
}

void apply_style_to_selection(Document& doc, Selection& sel, InlineKind kind,
                              const std::string& href) {
  if (sel.empty()) return;
  Caret a = sel.anchor, b = sel.focus;
  if (caret_cmp(doc, a, b) > 0) std::swap(a, b);

  if (a.node == b.node) {
    apply_style_one_node(doc, a.node, a.span_index, a.char_index,
                         b.span_index, b.char_index, kind, href);
    sel.anchor = sel.focus = a;  // approximate; caret may have moved
    // Re-find end of styled run roughly at a
    ensure_valid_caret(doc, sel.focus);
    sel.anchor = sel.focus;
    return;
  }

  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  auto rank = [&](NodeId id) -> size_t {
    auto it = std::find(nodes.begin(), nodes.end(), id);
    return it == nodes.end() ? nodes.size() : static_cast<size_t>(it - nodes.begin());
  };
  size_t ra = rank(a.node), rb = rank(b.node);
  if (ra > rb) {
    std::swap(a, b);
    std::swap(ra, rb);
  }

  // Apply from last node to first so earlier indices stay stable enough
  for (size_t i = rb + 1; i-- > ra; ) {
    if (i >= nodes.size()) continue;
    NodeId nid = nodes[i];
    Node& n = doc.get(nid);
    if (n.spans.empty()) continue;
    size_t last = n.spans.size() - 1;
    size_t last_len = utf8_char_count(n.spans[last].text);
    size_t fs = 0, fc = 0, ts = last, tc = last_len;
    if (i == ra) {
      fs = a.span_index;
      fc = a.char_index;
    }
    if (i == rb) {
      ts = b.span_index;
      tc = b.char_index;
    }
    apply_style_one_node(doc, nid, fs, fc, ts, tc, kind, href);
  }

  ensure_valid_caret(doc, a);
  sel.anchor = sel.focus = a;
}

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

static void renumber_ordered_list(Document& doc, NodeId list_id);

void delete_backward(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return;
  Node& n = doc.get(caret.node);

  if (caret.char_index == 0 && caret.span_index == 0) {
    // At start of block: merge with previous text-bearing node
    NodeId cur_id = caret.node;
    std::vector<NodeId> nodes;
    if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
    auto it = std::find(nodes.begin(), nodes.end(), cur_id);
    if (it == nodes.begin() || it == nodes.end()) return;
    NodeId prev_id = *(it - 1);
    Node& prev = doc.get(prev_id);
    Node& cur = doc.get(cur_id);

    size_t join_span = prev.spans.empty() ? 0 : prev.spans.size() - 1;
    size_t join_ch = prev.spans.empty() ? 0 : utf8_char_count(prev.spans.back().text);

    for (auto& sp : cur.spans) {
      prev.spans.push_back(std::move(sp));
    }
    cur.spans.clear();

    NodeId list_to_renumber = INVALID_NODE;
    if (cur.parent != INVALID_NODE) {
      Node& parent = doc.get(cur.parent);
      if (parent.kind == NodeKind::List) list_to_renumber = cur.parent;
      auto cit = std::find(parent.children.begin(), parent.children.end(), cur_id);
      if (cit != parent.children.end()) parent.children.erase(cit);
      // Drop empty list containers left behind
      if (parent.kind == NodeKind::List && parent.children.empty() &&
          parent.parent != INVALID_NODE) {
        Node& gp = doc.get(parent.parent);
        auto lit =
            std::find(gp.children.begin(), gp.children.end(), cur.parent);
        if (lit != gp.children.end()) gp.children.erase(lit);
        list_to_renumber = INVALID_NODE;
      }
    }
    if (list_to_renumber != INVALID_NODE) {
      renumber_ordered_list(doc, list_to_renumber);
    }

    caret.node = prev_id;
    caret.span_index = join_span;
    caret.char_index = join_ch;
    return;
  }

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




bool move_block_up(Document& doc, NodeId block) {
  if (block == INVALID_NODE) return false;
  Node& n = doc.get(block);
  if (n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  auto it = std::find(parent.children.begin(), parent.children.end(), block);
  if (it == parent.children.begin() || it == parent.children.end()) return false;
  std::iter_swap(it, it - 1);
  return true;
}

bool move_block_down(Document& doc, NodeId block) {
  if (block == INVALID_NODE) return false;
  Node& n = doc.get(block);
  if (n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  auto it = std::find(parent.children.begin(), parent.children.end(), block);
  if (it == parent.children.end() || (it + 1) == parent.children.end()) return false;
  std::iter_swap(it, it + 1);
  return true;
}

bool relocate_block(Document& doc, NodeId block, NodeId target, bool before) {
  if (block == INVALID_NODE || target == INVALID_NODE || block == target)
    return false;
  Node& bn = doc.get(block);
  Node& tn = doc.get(target);
  if (bn.parent == INVALID_NODE || tn.parent == INVALID_NODE) return false;

  // Refuse to nest a node under itself or under one of its descendants.
  for (NodeId walk = tn.parent; walk != INVALID_NODE; walk = doc.get(walk).parent) {
    if (walk == block) return false;
  }

  NodeId old_parent_id = bn.parent;
  NodeId new_parent_id = tn.parent;
  Node& old_parent = doc.get(old_parent_id);
  Node& new_parent = doc.get(new_parent_id);

  // Structural containers stay put (Columns/Column/Document/List wrappers)
  if (bn.kind == NodeKind::Document || bn.kind == NodeKind::Columns ||
      bn.kind == NodeKind::Column || bn.kind == NodeKind::List)
    return false;
  // Don't drop onto the structural wrapper itself as a "sibling" of content
  // that lives under a different kind of parent — target must be a real sibling
  // candidate under new_parent.
  if (tn.kind == NodeKind::Document || tn.kind == NodeKind::Columns)
    return false;

  auto bit =
      std::find(old_parent.children.begin(), old_parent.children.end(), block);
  auto tit =
      std::find(new_parent.children.begin(), new_parent.children.end(), target);
  if (bit == old_parent.children.end() || tit == new_parent.children.end())
    return false;

  if (old_parent_id == new_parent_id) {
    size_t bidx = static_cast<size_t>(bit - old_parent.children.begin());
    size_t tidx = static_cast<size_t>(tit - new_parent.children.begin());
    old_parent.children.erase(bit);
    if (bidx < tidx) --tidx;
    size_t insert_at = before ? tidx : tidx + 1;
    if (insert_at > new_parent.children.size())
      insert_at = new_parent.children.size();
    new_parent.children.insert(
        new_parent.children.begin() + static_cast<std::ptrdiff_t>(insert_at),
        block);
  } else {
    // Cross-parent: detach, then attach
    old_parent.children.erase(bit);
    bn.parent = new_parent_id;

    // List membership: convert kind when crossing List boundary
    if (old_parent.kind == NodeKind::List && new_parent.kind != NodeKind::List) {
      if (bn.kind == NodeKind::ListItem) {
        bn.kind = NodeKind::Paragraph;
        bn.level = 0;
      }
    } else if (old_parent.kind != NodeKind::List &&
               new_parent.kind == NodeKind::List) {
      if (bn.kind == NodeKind::Paragraph || bn.kind == NodeKind::Heading ||
          bn.kind == NodeKind::BlockQuote) {
        bn.kind = NodeKind::ListItem;
        bn.level = (new_parent.level == 1) ? 1 : 0;
      }
    }

    size_t tidx = static_cast<size_t>(
        std::find(new_parent.children.begin(), new_parent.children.end(),
                  target) -
        new_parent.children.begin());
    // target is still in new_parent
    size_t insert_at = before ? tidx : tidx + 1;
    if (insert_at > new_parent.children.size())
      insert_at = new_parent.children.size();
    new_parent.children.insert(
        new_parent.children.begin() + static_cast<std::ptrdiff_t>(insert_at),
        block);

    // Drop empty List left behind
    if (old_parent.kind == NodeKind::List && old_parent.children.empty() &&
        old_parent.parent != INVALID_NODE) {
      Node& gp = doc.get(old_parent.parent);
      auto oit = std::find(gp.children.begin(), gp.children.end(), old_parent_id);
      if (oit != gp.children.end()) gp.children.erase(oit);
      old_parent.parent = INVALID_NODE;
    } else if (old_parent.kind == NodeKind::List) {
      renumber_ordered_list(doc, old_parent_id);
    }
  }

  if (new_parent.kind == NodeKind::List)
    renumber_ordered_list(doc, new_parent_id);
  return true;
}

static void copy_node_deep(Document& doc, NodeId src_id, NodeId new_parent,
                           NodeId& out_id);

std::optional<NodeId> duplicate_block(Document& doc, NodeId block) {
  if (block == INVALID_NODE) return std::nullopt;
  Node& src = doc.get(block);
  if (src.parent == INVALID_NODE) return std::nullopt;

  NodeId nid = doc.create(src.kind, INVALID_NODE);
  Node& dst = doc.get(nid);
  dst.kind = src.kind;
  dst.level = src.level;
  dst.alt = src.alt;
  dst.src = src.src;
  dst.expanded = src.expanded;
  dst.summary = src.summary;
  dst.display_scale = src.display_scale;
  dst.spans = src.spans;  // copy spans
  dst.parent = src.parent;

  // Deep-copy children
  for (NodeId cid : src.children) {
    NodeId child_copy = INVALID_NODE;
    copy_node_deep(doc, cid, nid, child_copy);
  }

  if (!doc.insert_after(block, nid)) {
    // fallback: append under parent
    doc.add_child(src.parent, nid);
  }
  return nid;
}

bool toggle_list(Document& doc, Caret& caret, bool ordered) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);

  // Unwrap: ListItem → Paragraph, drop empty List
  if (n.kind == NodeKind::ListItem) {
    NodeId item_id = caret.node;
    NodeId list_id = n.parent;
    if (list_id == INVALID_NODE) return false;
    Node& list = doc.get(list_id);
    if (list.kind != NodeKind::List) return false;
    NodeId list_parent = list.parent;
    if (list_parent == INVALID_NODE) return false;

    Node& lp = doc.get(list_parent);

    auto iit = std::find(list.children.begin(), list.children.end(), item_id);
    if (iit != list.children.end()) list.children.erase(iit);
    n.kind = NodeKind::Paragraph;
    n.level = 0;
    n.parent = list_parent;

    if (list.children.empty()) {
      auto lit = std::find(lp.children.begin(), lp.children.end(), list_id);
      if (lit != lp.children.end()) {
        *lit = item_id;
      } else {
        lp.children.push_back(item_id);
      }
      list.parent = INVALID_NODE;
    } else {
      auto lit = std::find(lp.children.begin(), lp.children.end(), list_id);
      size_t insert_at =
          (lit != lp.children.end())
              ? static_cast<size_t>(lit - lp.children.begin()) + 1
              : lp.children.size();
      if (insert_at > lp.children.size()) insert_at = lp.children.size();
      lp.children.insert(
          lp.children.begin() + static_cast<std::ptrdiff_t>(insert_at),
          item_id);
      renumber_ordered_list(doc, list_id);
    }
    return true;
  }

  // Wrap: Paragraph / Heading / BlockQuote → ListItem under new List
  if (n.kind != NodeKind::Paragraph && n.kind != NodeKind::Heading &&
      n.kind != NodeKind::BlockQuote) {
    return false;
  }
  if (n.parent == INVALID_NODE) return false;

  NodeId block_id = caret.node;
  NodeId parent_id = n.parent;
  Node& parent = doc.get(parent_id);
  auto pit =
      std::find(parent.children.begin(), parent.children.end(), block_id);
  if (pit == parent.children.end()) return false;
  size_t idx = static_cast<size_t>(pit - parent.children.begin());

  NodeId list_id = doc.create(NodeKind::List, INVALID_NODE);
  Node& list = doc.get(list_id);
  list.level = ordered ? 1 : 0;
  list.parent = parent_id;

  n.kind = NodeKind::ListItem;
  n.level = ordered ? 1 : 0;
  n.parent = list_id;
  list.children.push_back(block_id);

  parent.children[idx] = list_id;
  return true;
}

bool indent_list_item(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& item = doc.get(caret.node);
  if (item.kind != NodeKind::ListItem) return false;
  NodeId list_id = item.parent;
  if (list_id == INVALID_NODE) return false;
  Node& list = doc.get(list_id);
  if (list.kind != NodeKind::List) return false;

  auto it = std::find(list.children.begin(), list.children.end(), caret.node);
  if (it == list.children.begin() || it == list.children.end()) return false;
  NodeId prev_id = *(it - 1);
  Node& prev = doc.get(prev_id);
  if (prev.kind != NodeKind::ListItem) return false;

  // Prefer an existing nested List under the previous item
  NodeId nested = INVALID_NODE;
  for (NodeId c : prev.children) {
    if (doc.get(c).kind == NodeKind::List) {
      nested = c;
      break;
    }
  }
  if (nested == INVALID_NODE) {
    nested = doc.create(NodeKind::List, prev_id);
    Node& nl = doc.get(nested);
    nl.level = list.level;  // same ordered/unordered as outer
    nl.parent = prev_id;
  }

  list.children.erase(it);
  item.parent = nested;
  doc.get(nested).children.push_back(caret.node);

  // Renumber both lists
  renumber_ordered_list(doc, list_id);
  renumber_ordered_list(doc, nested);
  return true;
}

bool outdent_list_item(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& item = doc.get(caret.node);
  if (item.kind != NodeKind::ListItem) return false;
  NodeId nested_id = item.parent;
  if (nested_id == INVALID_NODE) return false;
  Node& nested = doc.get(nested_id);
  if (nested.kind != NodeKind::List) return false;
  NodeId host_item_id = nested.parent;
  if (host_item_id == INVALID_NODE) return false;
  Node& host_item = doc.get(host_item_id);
  if (host_item.kind != NodeKind::ListItem) return false;
  NodeId outer_id = host_item.parent;
  if (outer_id == INVALID_NODE) return false;
  Node& outer = doc.get(outer_id);
  if (outer.kind != NodeKind::List) return false;

  auto nit =
      std::find(nested.children.begin(), nested.children.end(), caret.node);
  if (nit == nested.children.end()) return false;
  nested.children.erase(nit);

  auto oit =
      std::find(outer.children.begin(), outer.children.end(), host_item_id);
  if (oit == outer.children.end()) return false;
  size_t insert_at = static_cast<size_t>(oit - outer.children.begin()) + 1;
  item.parent = outer_id;
  outer.children.insert(
      outer.children.begin() + static_cast<std::ptrdiff_t>(insert_at),
      caret.node);

  if (nested.children.empty()) {
    auto hit = std::find(host_item.children.begin(), host_item.children.end(),
                         nested_id);
    if (hit != host_item.children.end()) host_item.children.erase(hit);
    nested.parent = INVALID_NODE;
  } else {
    renumber_ordered_list(doc, nested_id);
  }
  renumber_ordered_list(doc, outer_id);
  return true;
}

static void copy_node_deep(Document& doc, NodeId src_id, NodeId new_parent,
                           NodeId& out_id) {
  const Node& src = doc.get(src_id);
  out_id = doc.create(src.kind, new_parent);
  Node& dst = doc.get(out_id);
  dst.level = src.level;
  dst.alt = src.alt;
  dst.src = src.src;
  dst.expanded = src.expanded;
  dst.summary = src.summary;
  dst.display_scale = src.display_scale;
  dst.spans = src.spans;
  for (NodeId cid : src.children) {
    NodeId child_copy = INVALID_NODE;
    copy_node_deep(doc, cid, out_id, child_copy);
  }
}

NodeId enclosing_stretch(const Document& doc, NodeId id) {
  while (id != INVALID_NODE) {
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Stretch) return id;
    id = n.parent;
  }
  return INVALID_NODE;
}

std::optional<NodeId> unwrap_stretch(Document& doc, NodeId block_or_stretch) {
  NodeId sid = enclosing_stretch(doc, block_or_stretch);
  if (sid == INVALID_NODE) return std::nullopt;
  Node& stretch = doc.get(sid);
  if (stretch.parent == INVALID_NODE) return std::nullopt;
  Node& parent = doc.get(stretch.parent);
  auto it = std::find(parent.children.begin(), parent.children.end(), sid);
  if (it == parent.children.end()) return std::nullopt;
  size_t idx = static_cast<size_t>(it - parent.children.begin());

  std::vector<NodeId> kids = stretch.children;
  stretch.children.clear();
  // Remove stretch from parent
  parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(idx));
  // Insert children in its place
  for (size_t i = 0; i < kids.size(); ++i) {
    NodeId cid = kids[i];
    doc.get(cid).parent = parent.id;
    parent.children.insert(
        parent.children.begin() + static_cast<std::ptrdiff_t>(idx + i), cid);
  }
  if (kids.empty()) return std::nullopt;
  return kids.front();
}

bool wrap_block_in_stretch(Document& doc, NodeId block,
                           const std::string& summary) {
  if (block == INVALID_NODE) return false;
  Node& n = doc.get(block);
  if (n.parent == INVALID_NODE) return false;
  if (n.kind == NodeKind::Stretch || n.kind == NodeKind::Document) return false;

  NodeId parent = n.parent;
  Node& p = doc.get(parent);
  auto it = std::find(p.children.begin(), p.children.end(), block);
  if (it == p.children.end()) return false;
  size_t idx = static_cast<size_t>(it - p.children.begin());

  // Create stretch under same parent, then move block under it
  NodeId sid = doc.create(NodeKind::Stretch, INVALID_NODE);
  Node& stretch = doc.get(sid);
  stretch.parent = parent;
  stretch.expanded = false;
  if (!summary.empty()) {
    stretch.summary = summary;
  } else {
    std::string text;
    for (const auto& sp : n.spans) text += sp.text;
    if (text.size() > 48) text = text.substr(0, 48) + "…";
    stretch.summary = text.empty() ? "…" : text;
  }

  // Replace block with stretch in parent children
  p.children[idx] = sid;
  // Detach block from parent and attach under stretch
  n.parent = sid;
  stretch.children.push_back(block);
  return true;
}

static bool node_content_empty(const Node& n) {
  for (const auto& sp : n.spans) {
    if (!sp.text.empty()) return false;
  }
  return true;
}

// Renumber ordered-list items 1..N under a List node.
static void renumber_ordered_list(Document& doc, NodeId list_id) {
  if (list_id == INVALID_NODE) return;
  Node& list = doc.get(list_id);
  if (list.kind != NodeKind::List || list.level != 1) return;
  int num = 1;
  for (NodeId c : list.children) {
    Node& item = doc.get(c);
    if (item.kind == NodeKind::ListItem) {
      item.level = num++;
    }
  }
}

// Remove a child id from its parent’s children vector (does not destroy the node).
static bool detach_from_parent(Document& doc, NodeId id) {
  Node& n = doc.get(id);
  if (n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  auto it = std::find(parent.children.begin(), parent.children.end(), id);
  if (it == parent.children.end()) return false;
  parent.children.erase(it);
  n.parent = INVALID_NODE;
  return true;
}

bool split_block(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (!is_text_node(n.kind)) return false;

  // Enter on an empty list item exits the list: drop the item and place a
  // fresh paragraph after the list (common editor behaviour).
  if (n.kind == NodeKind::ListItem && node_content_empty(n) &&
      caret.span_index == 0 && caret.char_index == 0) {
    NodeId item_id = caret.node;
    NodeId list_id = n.parent;
    if (list_id == INVALID_NODE) return false;
    Node& list = doc.get(list_id);
    if (list.kind != NodeKind::List) return false;

    // Remember list position before we possibly remove it.
    NodeId list_parent = list.parent;
    size_t list_index = 0;
    bool have_index = false;
    if (list_parent != INVALID_NODE) {
      Node& lp = doc.get(list_parent);
      auto lit = std::find(lp.children.begin(), lp.children.end(), list_id);
      if (lit != lp.children.end()) {
        list_index = static_cast<size_t>(lit - lp.children.begin());
        have_index = true;
      }
    }

    detach_from_parent(doc, item_id);

    // If the list is now empty, remove it and put a paragraph where it was.
    if (list.children.empty()) {
      detach_from_parent(doc, list_id);
      NodeId para = doc.create(NodeKind::Paragraph, INVALID_NODE);
      if (list_parent != INVALID_NODE) {
        Node& lp = doc.get(list_parent);
        doc.get(para).parent = list_parent;
        if (have_index && list_index <= lp.children.size()) {
          lp.children.insert(lp.children.begin() +
                                 static_cast<std::ptrdiff_t>(list_index),
                             para);
        } else {
          lp.children.push_back(para);
        }
      }
      caret.node = para;
      caret.span_index = 0;
      caret.char_index = 0;
      return true;
    }

    renumber_ordered_list(doc, list_id);

    NodeId para = doc.create(NodeKind::Paragraph, INVALID_NODE);
    if (!doc.insert_after(list_id, para)) {
      if (list_parent != INVALID_NODE) doc.add_child(list_parent, para);
      else return false;
    }
    caret.node = para;
    caret.span_index = 0;
    caret.char_index = 0;
    return true;
  }

  // Create a new sibling of the same kind (ListItem stays a ListItem).
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

  // Ordered lists: renumber so the new item and following ones stay sequential.
  if (n.kind == NodeKind::ListItem && n.parent != INVALID_NODE) {
    renumber_ordered_list(doc, n.parent);
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

static bool is_word_char(uint32_t cp) {
  if (cp >= '0' && cp <= '9') return true;
  if (cp >= 'A' && cp <= 'Z') return true;
  if (cp >= 'a' && cp <= 'z') return true;
  if (cp == '_' || cp >= 0x80) return true;
  return false;
}

static uint32_t char_at(const Node& n, size_t span, size_t ch) {
  if (span >= n.spans.size()) return 0;
  const std::string& t = n.spans[span].text;
  size_t bi = utf8_byte_offset(t, ch);
  auto [cp, len] = decode_cp(t, bi);
  return cp;
}

bool move_word_left(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  if (!move_left(doc, caret)) {
    if (!move_to_prev_node(doc, caret)) return false;
  }
  // Skip non-word to the left
  while (!(caret.span_index == 0 && caret.char_index == 0)) {
    Caret prev = caret;
    if (!move_left(doc, prev) || prev.node != caret.node) break;
    uint32_t cp = char_at(doc.get(prev.node), prev.span_index, prev.char_index);
    if (is_word_char(cp)) break;
    caret = prev;
  }
  // Skip word chars to the left
  while (!(caret.span_index == 0 && caret.char_index == 0)) {
    Caret prev = caret;
    if (!move_left(doc, prev) || prev.node != caret.node) break;
    uint32_t cp = char_at(doc.get(prev.node), prev.span_index, prev.char_index);
    if (!is_word_char(cp)) break;
    caret = prev;
  }
  return true;
}

bool move_word_right(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  auto at_end = [&](const Node& n) {
    if (n.spans.empty()) return true;
    size_t last = n.spans.size() - 1;
    return caret.span_index >= last &&
           caret.char_index >= utf8_char_count(n.spans[last].text);
  };
  Node* n = &doc.get(caret.node);
  while (!at_end(*n)) {
    uint32_t cp = char_at(*n, caret.span_index, caret.char_index);
    if (is_word_char(cp)) break;
    if (!move_right(doc, caret)) {
      if (!move_to_next_node(doc, caret)) return true;
      n = &doc.get(caret.node);
      break;
    }
  }
  n = &doc.get(caret.node);
  while (!at_end(*n)) {
    uint32_t cp = char_at(*n, caret.span_index, caret.char_index);
    if (!is_word_char(cp)) break;
    if (!move_right(doc, caret)) {
      move_to_next_node(doc, caret);
      break;
    }
  }
  return true;
}

std::optional<Caret> jump_to_href(const Document& doc, const std::string& href) {
  if (href.empty() || href[0] != '#') return std::nullopt;
  std::string target = href.substr(1);
  auto lower = [](std::string s) {
    for (char& c : s)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
  };
  std::string tlow = lower(target);
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  for (NodeId id : nodes) {
    const Node& n = doc.get(id);
    std::string text;
    for (const auto& sp : n.spans) text += sp.text;
    if (lower(text) == tlow) return Caret{id, 0, 0};
  }
  for (NodeId id : nodes) {
    const Node& n = doc.get(id);
    if (n.kind != NodeKind::Heading) continue;
    std::string text;
    for (const auto& sp : n.spans) text += sp.text;
    if (lower(text).find(tlow) != std::string::npos) return Caret{id, 0, 0};
  }
  return std::nullopt;
}

std::optional<std::string> link_preview_text(const Document& doc,
                                            const std::string& href) {
  if (href.empty() || href[0] != '#') return std::nullopt;
  auto dest = jump_to_href(doc, href);
  if (!dest) return std::nullopt;
  std::string text = node_text(doc, dest->node);
  // Collapse internal whitespace for a single-line tooltip.
  std::string compact;
  compact.reserve(text.size());
  bool space = false;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      space = true;
      continue;
    }
    if (space && !compact.empty()) compact.push_back(' ');
    space = false;
    compact.push_back(c);
  }
  constexpr size_t kMax = 80;
  if (compact.size() > kMax) {
    // Truncate on a UTF-8 boundary.
    size_t cut = kMax;
    while (cut > 0 &&
           (static_cast<unsigned char>(compact[cut]) & 0xc0) == 0x80)
      --cut;
    compact.resize(cut);
    compact += "…";
  }
  if (compact.empty()) return std::string{"(empty)"};
  return compact;
}

std::optional<std::string> hit_test_link(const Document& doc,
                                         const LayoutResult& layout,
                                         float mx, float my, float scroll_y) {
  const float adj_y = my + scroll_y;
  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    if (g.kind != InlineKind::Link) continue;
    float x0 = g.x;
    float x1 = x0 + 10.f;
    size_t j = i;
    while (j + 1 < layout.glyphs.size() &&
           layout.glyphs[j + 1].kind == InlineKind::Link &&
           layout.glyphs[j + 1].node == g.node &&
           layout.glyphs[j + 1].span_index == g.span_index) {
      ++j;
      x1 = layout.glyphs[j].x + 10.f;
    }
    float y0 = g.y - 18.f;
    float y1 = g.y + 6.f;
    if (mx >= x0 && mx <= x1 && adj_y >= y0 && adj_y <= y1) {
      const Node& n = doc.get(g.node);
      if (g.span_index < n.spans.size()) {
        return n.spans[g.span_index].href;
      }
    }
    i = j;
  }
  return std::nullopt;
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

void compute_diff_marks(const Document& a, const Document& b,
                        std::vector<NodeId>& changed_a,
                        std::vector<NodeId>& changed_b) {
  changed_a.clear();
  changed_b.clear();
  std::vector<NodeId> nodes_a, nodes_b;
  if (a.root != INVALID_NODE) collect_text_nodes(a, a.root, nodes_a);
  if (b.root != INVALID_NODE) collect_text_nodes(b, b.root, nodes_b);

  const size_t n = std::min(nodes_a.size(), nodes_b.size());
  for (size_t i = 0; i < n; ++i) {
    if (node_text(a, nodes_a[i]) != node_text(b, nodes_b[i])) {
      changed_a.push_back(nodes_a[i]);
      changed_b.push_back(nodes_b[i]);
    }
  }
  for (size_t i = n; i < nodes_a.size(); ++i) changed_a.push_back(nodes_a[i]);
  for (size_t i = n; i < nodes_b.size(); ++i) changed_b.push_back(nodes_b[i]);
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


std::optional<Caret> find_next(const Document& doc, const std::string& query,
                               const Caret& from, size_t& match_len) {
  match_len = 0;
  if (query.empty() || doc.root == INVALID_NODE) return std::nullopt;

  // Lowercase query for case-insensitive ASCII search
  std::string q = query;
  for (char& ch : q) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }

  std::vector<NodeId> nodes;
  collect_text_nodes(doc, doc.root, nodes);
  if (nodes.empty()) return std::nullopt;

  auto node_rank = [&](NodeId id) -> size_t {
    auto it = std::find(nodes.begin(), nodes.end(), id);
    return it == nodes.end() ? 0 : static_cast<size_t>(it - nodes.begin());
  };

  size_t start_rank = node_rank(from.node);
  // Start searching from the character after `from`
  size_t start_span = from.span_index;
  size_t start_ch = from.char_index + 1;

  auto search_node = [&](NodeId nid, size_t from_span, size_t from_ch) -> std::optional<Caret> {
    const Node& n = doc.get(nid);
    // Build full text with codepoint index map back to (span, ch)
    struct Map { size_t span; size_t ch; };
    std::string lower;
    std::vector<Map> map;
    for (size_t si = 0; si < n.spans.size(); ++si) {
      size_t ci = 0;
      size_t bi = 0;
      const std::string& t = n.spans[si].text;
      while (bi < t.size()) {
        auto [cp, len] = decode_cp(t, bi);
        if (len == 0) break;
        char ascii = (cp < 128) ? static_cast<char>(cp) : '?';
        if (ascii >= 'A' && ascii <= 'Z') ascii = static_cast<char>(ascii - 'A' + 'a');
        lower.push_back(ascii);
        map.push_back({si, ci});
        bi += len;
        ++ci;
      }
    }
    // Determine byte start in lower from (from_span, from_ch)
    size_t start = 0;
    for (size_t i = 0; i < map.size(); ++i) {
      if (map[i].span > from_span || (map[i].span == from_span && map[i].ch >= from_ch)) {
        start = i;
        break;
      }
      start = i + 1;
    }
    if (start >= lower.size()) return std::nullopt;
    auto pos = lower.find(q, start);
    if (pos == std::string::npos) return std::nullopt;
    Caret c;
    c.node = nid;
    c.span_index = map[pos].span;
    c.char_index = map[pos].ch;
    match_len = q.size();  // ASCII query length ≈ codepoints for ASCII
    return c;
  };

  // Search from current node onward, then wrap
  for (size_t pass = 0; pass < 2; ++pass) {
    size_t i0 = (pass == 0) ? start_rank : 0;
    size_t i1 = (pass == 0) ? nodes.size() : start_rank + 1;
    for (size_t i = i0; i < i1 && i < nodes.size(); ++i) {
      size_t fs = 0, fc = 0;
      if (pass == 0 && i == start_rank) {
        fs = start_span;
        fc = start_ch;
      }
      if (auto hit = search_node(nodes[i], fs, fc)) {
        return hit;
      }
    }
  }
  return std::nullopt;
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
