// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/editor.hpp"
#include "xanatoo/util.hpp"
#include <cctype>
#include <cmath>
#include <algorithm>
#include <limits>

namespace xanatoo {

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

  // Prefer glyphs whose line band contains sy; fall back to nearest overall.
  // Baseline is g.y; after scroll, gy is viewport Y in content space.
  float best_on = std::numeric_limits<float>::max();
  float best_any = std::numeric_limits<float>::max();
  Caret best_on_c{};
  Caret best_any_c{};
  bool found_on = false, found_any = false;

  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    // Org TODO/tag decorations are laid out with node = INVALID_NODE — not
    // a valid caret target.
    if (g.node == INVALID_NODE) continue;
    float gy = g.y - scroll_y;
    // Glyphs are laid out top-to-bottom overall; skip lines far from the pointer
    // so 10MB documents are not O(all glyphs) on every motion event.
    if (gy < sy - 120.f || gy > sy + 80.f) continue;
    float gx = g.x;
    float gw = 8.f;
    if (i + 1 < layout.glyphs.size() &&
        layout.glyphs[i + 1].node == g.node &&
        std::abs(layout.glyphs[i + 1].y - g.y) < 0.5f) {
      gw = layout.glyphs[i + 1].x - g.x;
      if (gw < 1.f) gw = 8.f;
    }
    float mid = gx + gw * 0.5f;
    size_t ch = g.char_index + (sx >= mid ? 1 : 0);
    float dx = sx - mid;
    float cy = gy - 6.f;  // mid of typical body ink band
    float dy = sy - cy;
    float d = dx * dx + dy * dy * 4.f;

    if (d < best_any) {
      best_any = d;
      best_any_c = Caret{g.node, g.span_index, ch};
      found_any = true;
    }
    const bool on_line = (sy >= gy - 18.f && sy <= gy + 6.f);
    if (on_line && d < best_on) {
      best_on = d;
      best_on_c = Caret{g.node, g.span_index, ch};
      found_on = true;
    }
  }
  if (found_on) return best_on_c;
  if (found_any) return best_any_c;
  return std::nullopt;
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
  if (n.id != caret.node) return;
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

bool redirect_transclude_caret(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  const Node& n = doc.get(caret.node);
  if (n.id != caret.node) return false;
  if (n.kind != NodeKind::Transclude) return false;
  // path.md#anchor → external (no write-through in this process)
  const std::string& src = n.src;
  if (src.empty()) return false;
  // Detect external path: anything before '#' that looks like a file path
  size_t hash = src.find('#');
  std::string path_part = (hash == std::string::npos) ? src : src.substr(0, hash);
  std::string href_part =
      (hash == std::string::npos) ? std::string{} : src.substr(hash);
  if (path_part.find('.') != std::string::npos ||
      path_part.find('/') != std::string::npos ||
      path_part.find('\\') != std::string::npos) {
    // External file include — leave caret on the Transclude (read-only)
    return false;
  }
  if (href_part.empty()) {
    if (!src.empty() && src[0] == '#')
      href_part = src;
    else
      href_part = std::string("#") + src;
  }
  NodeId target = find_anchor_node(doc, href_part);
  if (target == INVALID_NODE || target == caret.node) return false;
  caret.node = target;
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

void insert_text(Document& doc, Caret& caret, const std::string& utf8) {
  if (caret.node == INVALID_NODE || utf8.empty()) return;
  redirect_transclude_caret(doc, caret);
  // External transclusion body is a read-only mirror — refuse mutation
  if (doc.get(caret.node).kind == NodeKind::Transclude) return;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return;  // stale caret → invalid-node sentinel
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
  redirect_transclude_caret(doc, caret);
  if (doc.get(caret.node).kind == NodeKind::Transclude) return;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return;

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
  redirect_transclude_caret(doc, caret);
  if (doc.get(caret.node).kind == NodeKind::Transclude) return;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return;
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
  if (n.id != block || n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  if (parent.id != n.parent) return false;
  auto it = std::find(parent.children.begin(), parent.children.end(), block);
  if (it == parent.children.begin() || it == parent.children.end()) return false;
  std::iter_swap(it, it - 1);
  return true;
}

bool move_block_down(Document& doc, NodeId block) {
  if (block == INVALID_NODE) return false;
  Node& n = doc.get(block);
  if (n.id != block || n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  if (parent.id != n.parent) return false;
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
          bn.kind == NodeKind::Header || bn.kind == NodeKind::BlockQuote) {
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
  // Prefer a structural unit: Caption/Image inside Figure → whole Figure;
  // TableCell/TableRow → whole Table.
  {
    NodeId walk = block;
    for (int guard = 0; guard < 8 && walk != INVALID_NODE; ++guard) {
      const Node& n = doc.get(walk);
      if (n.kind == NodeKind::Caption || n.kind == NodeKind::Image) {
        if (n.parent != INVALID_NODE &&
            doc.get(n.parent).kind == NodeKind::Figure) {
          walk = n.parent;
          continue;
        }
      }
      if (n.kind == NodeKind::TableCell || n.kind == NodeKind::TableRow) {
        if (n.parent != INVALID_NODE) {
          walk = n.parent;
          continue;
        }
      }
      break;
    }
    // Climb to Table if we stopped on a TableRow
    if (walk != INVALID_NODE && doc.get(walk).kind == NodeKind::TableRow &&
        doc.get(walk).parent != INVALID_NODE &&
        doc.get(doc.get(walk).parent).kind == NodeKind::Table) {
      walk = doc.get(walk).parent;
    }
    block = walk;
  }
  Node& src = doc.get(block);
  if (src.parent == INVALID_NODE) return std::nullopt;

  NodeId nid = doc.create(src.kind, INVALID_NODE);
  Node& dst = doc.get(nid);
  dst.kind = src.kind;
  dst.level = src.level;
  dst.alt = src.alt;
  dst.src = src.src;
  dst.label = src.label;
  dst.tags = src.tags;
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
  if (src.parent != INVALID_NODE && doc.get(src.parent).kind == NodeKind::List)
    renumber_ordered_list(doc, src.parent);
  return nid;
}

std::optional<NodeId> insert_sibling_after(Document& doc, Caret& caret,
                                          NodeKind kind) {
  if (caret.node == INVALID_NODE) return std::nullopt;
  NodeId block = caret.node;
  Node& bn = doc.get(block);
  if (bn.parent == INVALID_NODE) return std::nullopt;

  if (bn.kind == NodeKind::Document || bn.kind == NodeKind::Columns ||
      bn.kind == NodeKind::Column || bn.kind == NodeKind::List) {
    return std::nullopt;
  }

  // Nested table cells: inserting a block while the caret is on a TableCell
  // (or inside a pure-inline cell) appends a child block rather than a
  // sibling cell, which would skew the grid.
  const bool blockish =
      kind == NodeKind::Paragraph || kind == NodeKind::Heading ||
      kind == NodeKind::Header ||
      kind == NodeKind::BlockQuote || kind == NodeKind::Note ||
      kind == NodeKind::CodeBlock || kind == NodeKind::List ||
      kind == NodeKind::HorizontalRule;
  NodeId cell_host = INVALID_NODE;
  if (blockish && bn.kind == NodeKind::TableCell) {
    cell_host = block;
  } else if (blockish && bn.parent != INVALID_NODE &&
             doc.get(bn.parent).kind == NodeKind::TableCell) {
    // Already nested — normal sibling under the cell is fine (falls through).
  }
  if (cell_host != INVALID_NODE) {
    Node& cell = doc.get(cell_host);
    // Promote existing inlines into a paragraph so they are not lost.
    if (!cell.spans.empty() && cell.children.empty()) {
      NodeId para = doc.create(NodeKind::Paragraph, cell_host);
      doc.get(para).spans = std::move(cell.spans);
      cell.spans.clear();
    }
    NodeId nid = doc.create(kind, cell_host);
    Node& nn = doc.get(nid);
    if (kind == NodeKind::Heading || kind == NodeKind::Header) nn.level = 0;
    if (kind == NodeKind::HorizontalRule) {
      NodeId para = doc.create(NodeKind::Paragraph, cell_host);
      caret.node = para;
      caret.span_index = 0;
      caret.char_index = 0;
      return nid;
    }
    if (kind == NodeKind::Paragraph || kind == NodeKind::Heading ||
        kind == NodeKind::Header || kind == NodeKind::BlockQuote ||
        kind == NodeKind::Note || kind == NodeKind::CodeBlock) {
      caret.node = nid;
      caret.span_index = 0;
      caret.char_index = 0;
    }
    return nid;
  }

  NodeId nid = doc.create(kind, INVALID_NODE);
  Node& nn = doc.get(nid);
  nn.kind = kind;
  nn.parent = bn.parent;
  if (kind == NodeKind::Heading || kind == NodeKind::Header) nn.level = 0;
  if (kind == NodeKind::ListItem) {
    Node& parent = doc.get(bn.parent);
    nn.level = (parent.kind == NodeKind::List && parent.level == 1) ? 1 : 0;
  }

  if (!doc.insert_after(block, nid)) {
    doc.add_child(bn.parent, nid);
  }

  if (kind == NodeKind::HorizontalRule) {
    NodeId para = doc.create(NodeKind::Paragraph, INVALID_NODE);
    doc.get(para).parent = bn.parent;
    if (!doc.insert_after(nid, para)) doc.add_child(bn.parent, para);
    caret.node = para;
    caret.span_index = 0;
    caret.char_index = 0;
    return nid;
  }

  if (kind == NodeKind::Paragraph || kind == NodeKind::Heading ||
      kind == NodeKind::Header || kind == NodeKind::BlockQuote ||
      kind == NodeKind::ListItem || kind == NodeKind::Note ||
      kind == NodeKind::CodeBlock || kind == NodeKind::Term ||
      kind == NodeKind::Definition) {
    caret.node = nid;
    caret.span_index = 0;
    caret.char_index = 0;
  }

  if (kind == NodeKind::ListItem && doc.get(bn.parent).kind == NodeKind::List)
    renumber_ordered_list(doc, bn.parent);

  return nid;
}

std::optional<NodeId> insert_table(Document& doc, Caret& caret) {
  auto table_id = insert_sibling_after(doc, caret, NodeKind::Table);
  if (!table_id) return std::nullopt;
  NodeId table = *table_id;
  // Header row
  NodeId hr = doc.create(NodeKind::TableRow, table);
  NodeId th1 = doc.create(NodeKind::TableCell, hr);
  doc.get(th1).level = 1;
  doc.append_text(th1, "Header");
  NodeId th2 = doc.create(NodeKind::TableCell, hr);
  doc.get(th2).level = 1;
  doc.append_text(th2, "Value");
  // Body row
  NodeId br = doc.create(NodeKind::TableRow, table);
  NodeId td1 = doc.create(NodeKind::TableCell, br);
  doc.append_text(td1, "");
  NodeId td2 = doc.create(NodeKind::TableCell, br);
  doc.append_text(td2, "");
  caret.node = td1;
  caret.span_index = 0;
  caret.char_index = 0;
  return table;
}

std::optional<NodeId> insert_deflist(Document& doc, Caret& caret) {
  auto dl = insert_sibling_after(doc, caret, NodeKind::DefList);
  if (!dl) return std::nullopt;
  NodeId list = *dl;
  NodeId term = doc.create(NodeKind::Term, list);
  doc.append_text(term, "Term");
  NodeId def = doc.create(NodeKind::Definition, list);
  doc.append_text(def, "Definition");
  caret.node = term;
  caret.span_index = 0;
  caret.char_index = 0;
  // Select term text for easy overwrite? leave caret at start
  return list;
}

std::optional<NodeId> insert_columns(Document& doc, Caret& caret, int n) {
  if (n < 2) n = 2;
  if (n > 6) n = 6;
  auto cols_id = insert_sibling_after(doc, caret, NodeKind::Columns);
  if (!cols_id) return std::nullopt;
  NodeId cols = *cols_id;
  NodeId first_para = INVALID_NODE;
  for (int i = 0; i < n; ++i) {
    NodeId col = doc.create(NodeKind::Column, cols);
    doc.get(col).display_scale = 1.f;
    NodeId para = doc.create(NodeKind::Paragraph, col);
    doc.append_text(para, "");
    if (i == 0) first_para = para;
  }
  if (first_para != INVALID_NODE) {
    caret.node = first_para;
    caret.span_index = 0;
    caret.char_index = 0;
  }
  return cols;
}

std::optional<NodeId> insert_biblio_entry(Document& doc, Caret& caret,
                                         const std::string& key) {
  // Prefer appending to an existing Biblio; otherwise create one after the
  // caret's block.
  NodeId biblio = INVALID_NODE;
  if (doc.root != INVALID_NODE) {
    for (const Node& n : doc.nodes) {
      if (n.id != INVALID_NODE && n.kind == NodeKind::Biblio) {
        biblio = n.id;
        break;
      }
    }
  }
  if (biblio == INVALID_NODE) {
    auto b = insert_sibling_after(doc, caret, NodeKind::Biblio);
    if (!b) return std::nullopt;
    biblio = *b;
  }
  NodeId entry = doc.create(NodeKind::BiblioEntry, biblio);
  doc.get(entry).label = key.empty() ? "key" : key;
  doc.append_text(entry, "Reference text");
  caret.node = entry;
  caret.span_index = 0;
  caret.char_index = 0;
  return entry;
}

std::optional<NodeId> insert_transclude(Document& doc, Caret& caret,
                                        const std::string& src) {
  if (src.empty()) return std::nullopt;
  auto tid = insert_sibling_after(doc, caret, NodeKind::Transclude);
  if (!tid) {
    // Fallback when caret is on a container: create under root after last child
    if (doc.root == INVALID_NODE) return std::nullopt;
    NodeId n = doc.create(NodeKind::Transclude, doc.root);
    doc.get(n).src = src;
    caret.node = n;
    caret.span_index = 0;
    caret.char_index = 0;
    return n;
  }
  doc.get(*tid).src = src;
  caret.node = *tid;
  caret.span_index = 0;
  caret.char_index = 0;
  return tid;
}

std::optional<NodeId> insert_image(Document& doc, Caret& caret,
                                   const std::string& path,
                                   const std::string& alt) {
  if (path.empty()) return std::nullopt;

  // Resolve a block we can insert after. Containers (Document/Columns/List)
  // are not valid insertion anchors; prefer the caret node, then the last
  // child of the root.
  NodeId block = caret.node;
  if (block != INVALID_NODE) {
    const Node& bn = doc.get(block);
    if (bn.kind == NodeKind::Document || bn.kind == NodeKind::Columns ||
        bn.kind == NodeKind::Column || bn.kind == NodeKind::List ||
        bn.parent == INVALID_NODE) {
      block = INVALID_NODE;
    }
  }
  if (block == INVALID_NODE && doc.root != INVALID_NODE) {
    const Node& root = doc.get(doc.root);
    if (!root.children.empty()) block = root.children.back();
  }
  if (block == INVALID_NODE || doc.get(block).parent == INVALID_NODE)
    return std::nullopt;

  Node& bn = doc.get(block);
  NodeId nid = doc.create(NodeKind::Image, INVALID_NODE);
  Node& img = doc.get(nid);
  img.src = path;
  img.alt = alt.empty() ? path : alt;
  img.display_scale = 1.f;

  if (!doc.insert_after(block, nid)) {
    doc.add_child(bn.parent, nid);
  }

  NodeId para = doc.create(NodeKind::Paragraph, INVALID_NODE);
  if (!doc.insert_after(nid, para)) doc.add_child(bn.parent, para);
  caret.node = para;
  caret.span_index = 0;
  caret.char_index = 0;
  return nid;
}

std::optional<NodeId> insert_figure(Document& doc, Caret& caret,
                                    const std::string& path,
                                    const std::string& alt,
                                    const std::string& caption) {
  if (path.empty()) return std::nullopt;
  auto fig_id = insert_sibling_after(doc, caret, NodeKind::Figure);
  if (!fig_id) return std::nullopt;
  NodeId fig = *fig_id;
  NodeId img = doc.create(NodeKind::Image, fig);
  doc.get(img).src = path;
  doc.get(img).alt = alt.empty() ? path : alt;
  doc.get(img).display_scale = 1.f;
  NodeId cap = doc.create(NodeKind::Caption, fig);
  doc.append_text(cap, caption.empty() ? "Caption" : caption);
  caret.node = cap;
  caret.span_index = 0;
  caret.char_index = 0;
  return fig;
}

bool set_heading_level(Document& doc, Caret& caret, int level) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (level <= 0) {
    if (n.kind != NodeKind::Heading && n.kind != NodeKind::Header &&
        n.kind != NodeKind::BlockQuote)
      return false;
    n.kind = NodeKind::Paragraph;
    n.level = 0;
    return true;
  }
  if (level > 6) level = 6;
  if (n.kind != NodeKind::Paragraph && n.kind != NodeKind::Heading &&
      n.kind != NodeKind::Header && n.kind != NodeKind::BlockQuote &&
      n.kind != NodeKind::ListItem && n.kind != NodeKind::Note)
    return false;
  // Prefer semantic Header; keep level only for legacy outline/export hints.
  n.kind = NodeKind::Header;
  n.level = level;
  return true;
}

bool make_header(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (n.kind != NodeKind::Paragraph && n.kind != NodeKind::Heading &&
      n.kind != NodeKind::Header && n.kind != NodeKind::BlockQuote &&
      n.kind != NodeKind::ListItem && n.kind != NodeKind::Note)
    return false;
  n.kind = NodeKind::Header;
  n.level = 0;
  return true;
}

static bool is_sectioning_boundary(NodeKind k) {
  return k == NodeKind::Section || k == NodeKind::Article ||
         k == NodeKind::Header || k == NodeKind::Heading ||
         k == NodeKind::HorizontalRule || k == NodeKind::Thread;
}

static bool wrap_in_sectioning(Document& doc, Caret& caret, NodeKind kind) {
  if (caret.node == INVALID_NODE) return false;
  if (kind != NodeKind::Section && kind != NodeKind::Article) return false;
  Node& n = doc.get(caret.node);
  if (n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  std::vector<NodeId> siblings = parent.children;
  size_t idx = 0;
  bool found = false;
  for (; idx < siblings.size(); ++idx) {
    if (siblings[idx] == caret.node) {
      found = true;
      break;
    }
  }
  if (!found) return false;

  std::vector<NodeId> take;
  for (size_t i = idx; i < siblings.size(); ++i) {
    NodeId cid = siblings[i];
    const Node& c = doc.get(cid);
    if (i > idx && is_sectioning_boundary(c.kind)) break;
    if (c.kind == NodeKind::Section || c.kind == NodeKind::Article) break;
    take.push_back(cid);
  }
  if (take.empty()) return false;

  NodeId sec = doc.create(kind, INVALID_NODE);
  doc.get(sec).parent = parent.id;

  std::vector<NodeId> rebuilt;
  rebuilt.reserve(siblings.size() - take.size() + 1);
  for (size_t i = 0; i < idx; ++i) rebuilt.push_back(siblings[i]);
  rebuilt.push_back(sec);
  for (size_t i = idx + take.size(); i < siblings.size(); ++i)
    rebuilt.push_back(siblings[i]);
  parent.children = std::move(rebuilt);

  for (NodeId id : take) {
    Node& ch = doc.get(id);
    ch.parent = sec;
    doc.get(sec).children.push_back(id);
  }
  return true;
}

bool wrap_in_section(Document& doc, Caret& caret) {
  return wrap_in_sectioning(doc, caret, NodeKind::Section);
}

bool wrap_in_article(Document& doc, Caret& caret) {
  return wrap_in_sectioning(doc, caret, NodeKind::Article);
}

bool unwrap_sectioning(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  NodeId cur = caret.node;
  // Walk up to nearest Section/Article
  NodeId sec = INVALID_NODE;
  for (NodeId id = cur; id != INVALID_NODE; id = doc.get(id).parent) {
    NodeKind k = doc.get(id).kind;
    if (k == NodeKind::Section || k == NodeKind::Article) {
      sec = id;
      break;
    }
  }
  if (sec == INVALID_NODE) return false;
  Node& s = doc.get(sec);
  if (s.parent == INVALID_NODE) return false;
  Node& parent = doc.get(s.parent);
  size_t idx = 0;
  for (; idx < parent.children.size(); ++idx)
    if (parent.children[idx] == sec) break;
  if (idx >= parent.children.size()) return false;

  std::vector<NodeId> kids = s.children;
  s.children.clear();
  parent.children.erase(parent.children.begin() +
                        static_cast<std::ptrdiff_t>(idx));
  for (size_t i = 0; i < kids.size(); ++i) {
    doc.get(kids[i]).parent = parent.id;
    parent.children.insert(parent.children.begin() +
                               static_cast<std::ptrdiff_t>(idx + i),
                           kids[i]);
  }
  return true;
}

bool toggle_task_item(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (n.kind == NodeKind::ListItem) {
    if (n.label == "task") {
      n.label = "task-done";
    } else if (n.label == "task-done") {
      n.label.clear();
    } else {
      // Ordered items stay numbered; clear number-only identity via label.
      n.label = "task";
    }
    return true;
  }
  // Promote paragraph-like text to an unordered task item.
  if (!toggle_list(doc, caret, false)) return false;
  Node& item = doc.get(caret.node);
  if (item.kind == NodeKind::ListItem) item.label = "task";
  return true;
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
      n.kind != NodeKind::Header && n.kind != NodeKind::BlockQuote) {
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
  dst.label = src.label;
  dst.tags = src.tags;
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
  if (list.id != list_id || list.kind != NodeKind::List || list.level != 1)
    return;
  int num = 1;
  for (NodeId c : list.children) {
    Node& item = doc.get(c);
    if (item.id == c && item.kind == NodeKind::ListItem) {
      item.level = num++;
    }
  }
}

// Remove a child id from its parent’s children vector (does not destroy the node).
static bool detach_from_parent(Document& doc, NodeId id) {
  Node& n = doc.get(id);
  if (n.id != id || n.parent == INVALID_NODE) return false;
  Node& parent = doc.get(n.parent);
  if (parent.id != n.parent) return false;  // stale parent id
  auto it = std::find(parent.children.begin(), parent.children.end(), id);
  if (it == parent.children.end()) return false;
  parent.children.erase(it);
  n.parent = INVALID_NODE;
  return true;
}

bool split_block(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return false;
  if (!is_text_node_kind(n.kind)) return false;

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

  // Code blocks: Enter inserts a hard newline inside the block (layout
  // respects '\n'). Splitting into two CodeBlock siblings is rarely wanted.
  if (n.kind == NodeKind::CodeBlock) {
    insert_text(doc, caret, "\n");
    return true;
  }

  // Table cells: Enter inserts a new row after the current one, with the
  // same column count. Caret moves to the cell in the same column.
  // (Splitting into a sibling cell would skew the column grid.)
  if (n.kind == NodeKind::TableCell) {
    NodeId cell_id = caret.node;
    NodeId row_id = n.parent;
    if (row_id == INVALID_NODE || doc.get(row_id).kind != NodeKind::TableRow)
      return false;
    Node& row = doc.get(row_id);
    NodeId table_id = row.parent;
    if (table_id == INVALID_NODE || doc.get(table_id).kind != NodeKind::Table)
      return false;

    // Column index of the current cell among TableCell siblings
    size_t col = 0;
    size_t n_cols = 0;
    for (NodeId c : row.children) {
      if (doc.get(c).kind != NodeKind::TableCell) continue;
      if (c == cell_id) col = n_cols;
      ++n_cols;
    }
    if (n_cols == 0) return false;

    NodeId new_row = doc.create(NodeKind::TableRow, INVALID_NODE);
    if (!doc.insert_after(row_id, new_row)) {
      doc.add_child(table_id, new_row);
    }
    NodeId focus_cell = INVALID_NODE;
    for (size_t i = 0; i < n_cols; ++i) {
      NodeId nc = doc.create(NodeKind::TableCell, new_row);
      // Preserve header-ness only for the first body? New rows are body cells.
      doc.append_text(nc, "");
      if (i == col) focus_cell = nc;
    }
    if (focus_cell == INVALID_NODE) return false;
    caret.node = focus_cell;
    caret.span_index = 0;
    caret.char_index = 0;
    return true;
  }

  // Header / legacy Heading: Enter starts body text (Paragraph sibling), not
  // another title — matches article/section writing flow.
  NodeKind sibling_kind = n.kind;
  if (n.kind == NodeKind::Header || n.kind == NodeKind::Heading)
    sibling_kind = NodeKind::Paragraph;

  // Create a new sibling of the same kind (ListItem stays a ListItem).
  NodeId new_id = doc.create(sibling_kind, INVALID_NODE);  // parent set by insert_after
  Node& neu = doc.get(new_id);
  if (sibling_kind == n.kind)
    neu.level = n.level;
  else
    neu.level = 0;

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

// Drop empty inline spans; keep the node itself (may be an empty paragraph).
static void compact_spans(Node& n) {
  // Keep empty-text spans that still carry an href (link/cite with no label).
  n.spans.erase(std::remove_if(n.spans.begin(), n.spans.end(),
                               [](const InlineSpan& s) {
                                 return s.text.empty() && s.href.empty();
                               }),
                n.spans.end());
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
  redirect_transclude_caret(doc, sel.anchor);
  redirect_transclude_caret(doc, sel.focus);
  if (doc.get(sel.focus.node).kind == NodeKind::Transclude) return;
  Caret a = sel.anchor;
  Caret b = sel.focus;
  if (caret_cmp(doc, a, b) > 0) std::swap(a, b);

  if (a.node == b.node) {
    Node& n = doc.get(a.node);
    if (n.id != a.node) return;
    erase_range_in_node(n, a.span_index, a.char_index, b.span_index, b.char_index);
    compact_spans(n);
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
    compact_spans(n);
  }
  // Middle nodes: detach emptied text blocks (leave start/end anchors)
  for (size_t i = ra + 1; i < rb && i < nodes.size(); ++i) {
    NodeId mid = nodes[i];
    doc.get(mid).spans.clear();
    detach_from_parent(doc, mid);
  }
  // End node: from start to b
  {
    Node& n = doc.get(b.node);
    if (!n.spans.empty()) {
      erase_range_in_node(n, 0, 0, b.span_index, b.char_index);
    }
    compact_spans(n);
  }

  sel.anchor = sel.focus = a;
}

// ---- Movement ----

bool move_left(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return false;
  if (caret.char_index > 0) {
    --caret.char_index;
    return true;
  }
  if (caret.span_index > 0 && !n.spans.empty()) {
    if (caret.span_index > n.spans.size())
      caret.span_index = n.spans.size();
    --caret.span_index;
    caret.char_index = utf8_char_count(n.spans[caret.span_index].text);
    return true;
  }
  return false;
}

bool move_right(Document& doc, Caret& caret) {
  if (caret.node == INVALID_NODE) return false;
  Node& n = doc.get(caret.node);
  if (n.id != caret.node) return false;
  if (n.spans.empty() || caret.span_index >= n.spans.size()) return false;
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

bool advance_caret(Document& doc, Caret& caret, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!move_right(doc, caret)) return false;
  }
  return true;
}

void select_node(Document& doc, Selection& sel, NodeId node) {
  if (node == INVALID_NODE) return;
  const Node& n = doc.get(node);
  if (n.id != node) return;
  if (!is_text_node_kind(n.kind)) return;
  sel.anchor = {node, 0, 0};
  sel.focus = {node, 0, 0};
  if (n.spans.empty()) return;
  sel.focus.span_index = n.spans.size() - 1;
  sel.focus.char_index = utf8_char_count(n.spans.back().text);
}

void select_all(Document& doc, Selection& sel) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  if (nodes.empty()) {
    sel = {};
    return;
  }
  sel.anchor = {nodes.front(), 0, 0};
  const Node& last = doc.get(nodes.back());
  sel.focus = {nodes.back(), 0, 0};
  if (!last.spans.empty()) {
    sel.focus.span_index = last.spans.size() - 1;
    sel.focus.char_index = utf8_char_count(last.spans.back().text);
  }
}

void select_word_at(Document& doc, Selection& sel) {
  if (sel.focus.node == INVALID_NODE) return;
  Caret c = sel.focus;
  move_word_left(doc, c);
  sel.anchor = c;
  sel.focus = c;
  move_word_right(doc, sel.focus);
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
  NodeId id = find_anchor_node(doc, href);
  if (id == INVALID_NODE) return std::nullopt;
  return Caret{id, 0, 0};
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

std::optional<LinkHit> hit_test_link_ex(const Document& doc,
                                        const LayoutResult& layout,
                                        float mx, float my, float scroll_y) {
  const float adj_y = my + scroll_y;
  for (size_t i = 0; i < layout.glyphs.size(); ++i) {
    const auto& g = layout.glyphs[i];
    // Skip far-away lines (glyphs are laid out top-to-bottom overall).
    if (g.y < adj_y - 120.f || g.y > adj_y + 80.f) continue;
    if (g.kind != InlineKind::Link && g.kind != InlineKind::Cite) continue;
    float x0 = g.x;
    float x1 = x0 + 10.f;
    size_t j = i;
    while (j + 1 < layout.glyphs.size() &&
           layout.glyphs[j + 1].kind == g.kind &&
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
        const auto& sp = n.spans[g.span_index];
        LinkHit hit;
        hit.kind = sp.kind;
        hit.rel = sp.rel;
        hit.node = g.node;
        hit.span_index = g.span_index;
        if (sp.kind == InlineKind::Cite) {
          if (!sp.href.empty() && sp.href[0] == '#')
            hit.href = sp.href;
          else
            hit.href = std::string("#") + sp.href;
        } else {
          hit.href = sp.href;
        }
        return hit;
      }
    }
    i = j;
  }
  return std::nullopt;
}

std::optional<std::string> hit_test_link(const Document& doc,
                                         const LayoutResult& layout,
                                         float mx, float my, float scroll_y) {
  auto hit = hit_test_link_ex(doc, layout, mx, my, scroll_y);
  if (!hit) return std::nullopt;
  return hit->href;
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
    if (g.node == INVALID_NODE) continue;
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
    if (g.node == INVALID_NODE) continue;
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

bool move_to_document_start(Document& doc, Caret& caret) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  if (nodes.empty()) return false;
  caret.node = nodes.front();
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

bool move_to_document_end(Document& doc, Caret& caret) {
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  if (nodes.empty()) return false;
  caret.node = nodes.back();
  const Node& n = doc.get(caret.node);
  if (n.spans.empty()) {
    caret.span_index = 0;
    caret.char_index = 0;
  } else {
    caret.span_index = n.spans.size() - 1;
    caret.char_index = utf8_char_count(n.spans.back().text);
  }
  return true;
}

void ensure_valid_caret(Document& doc, Caret& caret) {
  if (caret.node != INVALID_NODE) {
    const Node& n = doc.get(caret.node);
    // Reject free/deleted slots and non-text kinds
    if (n.id == caret.node && is_text_node_kind(n.kind)) {
      if (n.spans.empty()) {
        caret.span_index = 0;
        caret.char_index = 0;
        return;
      }
      if (caret.span_index >= n.spans.size()) {
        caret.span_index = n.spans.size() - 1;
        caret.char_index = utf8_char_count(n.spans.back().text);
      } else {
        const size_t max_ch =
            utf8_char_count(n.spans[caret.span_index].text);
        if (caret.char_index > max_ch) caret.char_index = max_ch;
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
    return;
  }
  // Empty document: seed a paragraph so typing can resume.
  if (doc.root == INVALID_NODE) {
    doc.root = doc.create(NodeKind::Document);
  }
  NodeId para = doc.create(NodeKind::Paragraph, doc.root);
  caret.node = para;
  caret.span_index = 0;
  caret.char_index = 0;
}


std::string node_text(const Document& doc, NodeId id) {
  return plain_text(doc, id);
}

void paste_plain_text(Document& doc, Caret& caret, const std::string& text) {
  if (text.empty() || caret.node == INVALID_NODE) return;
  redirect_transclude_caret(doc, caret);
  if (doc.get(caret.node).kind == NodeKind::Transclude) return;

  // Normalize newlines
  std::string norm;
  norm.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') continue;
      norm.push_back('\n');
    } else {
      norm.push_back(text[i]);
    }
  }

  // Code blocks keep literal newlines (and tabs); do not paragraph-split.
  if (doc.get(caret.node).kind == NodeKind::CodeBlock) {
    insert_text(doc, caret, norm);
    return;
  }

  // Split on blank lines into paragraph chunks
  std::vector<std::string> chunks;
  size_t i = 0;
  while (i < norm.size()) {
    while (i < norm.size() && norm[i] == '\n') ++i;  // skip leading blanks
    if (i >= norm.size()) break;
    size_t start = i;
    while (i < norm.size()) {
      if (norm[i] == '\n' && i + 1 < norm.size() && norm[i + 1] == '\n') break;
      ++i;
    }
    std::string chunk = norm.substr(start, i - start);
    // Single newlines → spaces
    for (char& c : chunk) {
      if (c == '\n' || c == '\t') c = ' ';
    }
    // Collapse runs of spaces
    std::string cleaned;
    bool prev_space = false;
    for (char c : chunk) {
      if (c == ' ') {
        if (!prev_space) cleaned.push_back(' ');
        prev_space = true;
      } else {
        cleaned.push_back(c);
        prev_space = false;
      }
    }
    // Trim
    while (!cleaned.empty() && cleaned.front() == ' ') cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    if (!cleaned.empty()) chunks.push_back(std::move(cleaned));
    while (i < norm.size() && norm[i] == '\n') ++i;
  }

  if (chunks.empty()) return;

  // First chunk into the current caret position
  insert_text(doc, caret, chunks[0]);

  // Remaining chunks → new sibling paragraphs
  for (size_t ci = 1; ci < chunks.size(); ++ci) {
    if (!insert_sibling_after(doc, caret, NodeKind::Paragraph)) {
      // Fallback: append with separators
      insert_text(doc, caret, "\n\n");
      insert_text(doc, caret, chunks[ci]);
      continue;
    }
    insert_text(doc, caret, chunks[ci]);
  }
}

DocStats document_stats(const Document& doc) {
  DocStats s;
  std::vector<NodeId> nodes;
  if (doc.root != INVALID_NODE) collect_text_nodes(doc, doc.root, nodes);
  for (NodeId id : nodes) {
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header)
      ++s.headings;
    ++s.text_blocks;
    const std::string t = node_text(doc, id);
    s.chars += t.size();
    bool in_word = false;
    for (unsigned char c : t) {
      const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
      if (ws) {
        in_word = false;
      } else if (!in_word) {
        in_word = true;
        ++s.words;
      }
    }
  }
  return s;
}

namespace {

// Split plain text into words with [begin,end) character indices.
struct WordTok {
  std::string text;
  size_t begin = 0;
  size_t end = 0;
};

std::vector<WordTok> tokenize_words(const std::string& s) {
  std::vector<WordTok> out;
  size_t i = 0;
  const size_t n = s.size();
  auto is_word = [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
  };
  while (i < n) {
    while (i < n && !is_word(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= n) break;
    size_t start = i;
    while (i < n && is_word(static_cast<unsigned char>(s[i]))) ++i;
    WordTok t;
    t.text = s.substr(start, i - start);
    // Character indices: for ASCII-heavy text byte offset ≈ char index;
    // use utf8_char_count on prefixes for correctness.
    t.begin = utf8_char_count(s.substr(0, start));
    t.end = utf8_char_count(s.substr(0, i));
    out.push_back(std::move(t));
  }
  return out;
}

// Jaccard-ish similarity on word sets (0..1).
float word_similarity(const std::vector<WordTok>& wa,
                      const std::vector<WordTok>& wb) {
  if (wa.empty() && wb.empty()) return 1.f;
  if (wa.empty() || wb.empty()) return 0.f;
  std::vector<std::string> sa, sb;
  sa.reserve(wa.size());
  sb.reserve(wb.size());
  for (const auto& w : wa) sa.push_back(ascii_lower(w.text));
  for (const auto& w : wb) sb.push_back(ascii_lower(w.text));
  std::sort(sa.begin(), sa.end());
  std::sort(sb.begin(), sb.end());
  size_t inter = 0;
  size_t i = 0, j = 0;
  while (i < sa.size() && j < sb.size()) {
    if (sa[i] == sb[j]) {
      ++inter;
      ++i;
      ++j;
    } else if (sa[i] < sb[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  size_t uni = sa.size() + sb.size() - inter;
  if (uni == 0) return 1.f;
  return static_cast<float>(inter) / static_cast<float>(uni);
}

void word_lcs_marks(const std::vector<WordTok>& wa,
                    const std::vector<WordTok>& wb,
                    NodeId node_a, NodeId node_b,
                    std::vector<DiffWordMark>& out_a,
                    std::vector<DiffWordMark>& out_b) {
  const size_t na = wa.size(), nb = wb.size();
  if (na == 0 && nb == 0) return;
  const size_t cols = nb + 1;
  std::vector<size_t> dp((na + 1) * cols, 0);
  auto at = [&](size_t i, size_t j) -> size_t& { return dp[i * cols + j]; };
  for (size_t i = 1; i <= na; ++i) {
    for (size_t j = 1; j <= nb; ++j) {
      if (ascii_lower(wa[i - 1].text) == ascii_lower(wb[j - 1].text))
        at(i, j) = at(i - 1, j - 1) + 1;
      else
        at(i, j) = std::max(at(i - 1, j), at(i, j - 1));
    }
  }
  std::vector<char> keep_a(na, 0), keep_b(nb, 0);
  size_t i = na, j = nb;
  while (i > 0 && j > 0) {
    if (ascii_lower(wa[i - 1].text) == ascii_lower(wb[j - 1].text) &&
        at(i, j) == at(i - 1, j - 1) + 1) {
      keep_a[i - 1] = 1;
      keep_b[j - 1] = 1;
      --i;
      --j;
    } else if (at(i - 1, j) >= at(i, j - 1)) {
      --i;
    } else {
      --j;
    }
  }
  for (size_t k = 0; k < na; ++k) {
    if (keep_a[k]) continue;
    DiffWordMark m;
    m.node = node_a;
    m.char_begin = wa[k].begin;
    m.char_end = wa[k].end;
    out_a.push_back(m);
  }
  for (size_t k = 0; k < nb; ++k) {
    if (keep_b[k]) continue;
    DiffWordMark m;
    m.node = node_b;
    m.char_begin = wb[k].begin;
    m.char_end = wb[k].end;
    out_b.push_back(m);
  }
}

}  // namespace

void compute_diff_marks(const Document& a, const Document& b,
                        std::vector<NodeId>& changed_a,
                        std::vector<NodeId>& changed_b,
                        std::vector<DiffWordMark>* word_a,
                        std::vector<DiffWordMark>* word_b) {
  changed_a.clear();
  changed_b.clear();
  if (word_a) word_a->clear();
  if (word_b) word_b->clear();
  std::vector<NodeId> nodes_a, nodes_b;
  if (a.root != INVALID_NODE) collect_text_nodes(a, a.root, nodes_a);
  if (b.root != INVALID_NODE) collect_text_nodes(b, b.root, nodes_b);

  const size_t na = nodes_a.size();
  const size_t nb = nodes_b.size();
  if (na == 0 && nb == 0) return;

  // Plain-text fingerprints for equality (structural block alignment).
  std::vector<std::string> ta(na), tb(nb);
  for (size_t i = 0; i < na; ++i) ta[i] = node_text(a, nodes_a[i]);
  for (size_t j = 0; j < nb; ++j) tb[j] = node_text(b, nodes_b[j]);

  // Longest common subsequence of equal block texts. Nodes not in the
  // LCS are treated as changed / inserted / deleted. This survives
  // insertions and deletions without cascading false positives the way
  // a pure index-aligned scan does.
  // dp[i][j] = LCS length of ta[0..i) and tb[0..j). Flat (na+1)*(nb+1).
  const size_t cols = nb + 1;
  std::vector<size_t> dp((na + 1) * cols, 0);
  auto at = [&](size_t i, size_t j) -> size_t& { return dp[i * cols + j]; };

  for (size_t i = 1; i <= na; ++i) {
    for (size_t j = 1; j <= nb; ++j) {
      if (ta[i - 1] == tb[j - 1])
        at(i, j) = at(i - 1, j - 1) + 1;
      else
        at(i, j) = std::max(at(i - 1, j), at(i, j - 1));
    }
  }

  std::vector<char> keep_a(na, 0), keep_b(nb, 0);
  size_t i = na, j = nb;
  while (i > 0 && j > 0) {
    if (ta[i - 1] == tb[j - 1] && at(i, j) == at(i - 1, j - 1) + 1) {
      keep_a[i - 1] = 1;
      keep_b[j - 1] = 1;
      --i;
      --j;
    } else if (at(i - 1, j) >= at(i, j - 1)) {
      --i;
    } else {
      --j;
    }
  }

  std::vector<size_t> rest_a, rest_b;
  for (size_t k = 0; k < na; ++k)
    if (!keep_a[k]) rest_a.push_back(k);
  for (size_t k = 0; k < nb; ++k)
    if (!keep_b[k]) rest_b.push_back(k);

  // Pair similar remaining blocks as "modified" for word-level marks.
  std::vector<char> paired_a(rest_a.size(), 0), paired_b(rest_b.size(), 0);
  if (word_a && word_b) {
    for (size_t ia = 0; ia < rest_a.size(); ++ia) {
      if (paired_a[ia]) continue;
      float best = 0.f;
      size_t best_ib = rest_b.size();
      auto wa = tokenize_words(ta[rest_a[ia]]);
      for (size_t ib = 0; ib < rest_b.size(); ++ib) {
        if (paired_b[ib]) continue;
        auto wb = tokenize_words(tb[rest_b[ib]]);
        float sim = word_similarity(wa, wb);
        if (sim > best) {
          best = sim;
          best_ib = ib;
        }
      }
      // Require meaningful overlap to treat as a modification, not
      // unrelated insert+delete.
      if (best_ib < rest_b.size() && best >= 0.35f) {
        paired_a[ia] = 1;
        paired_b[best_ib] = 1;
        auto wb = tokenize_words(tb[rest_b[best_ib]]);
        word_lcs_marks(wa, wb, nodes_a[rest_a[ia]], nodes_b[rest_b[best_ib]],
                       *word_a, *word_b);
      }
    }
  }

  for (size_t k = 0; k < na; ++k)
    if (!keep_a[k]) changed_a.push_back(nodes_a[k]);
  for (size_t k = 0; k < nb; ++k)
    if (!keep_b[k]) changed_b.push_back(nodes_b[k]);
}

bool table_move_cell(Document& doc, Caret& caret, bool forward) {
  if (caret.node == INVALID_NODE) return false;
  const Node& cell = doc.get(caret.node);
  if (cell.kind != NodeKind::TableCell) return false;
  NodeId row_id = cell.parent;
  if (row_id == INVALID_NODE || doc.get(row_id).kind != NodeKind::TableRow)
    return false;
  NodeId table_id = doc.get(row_id).parent;
  if (table_id == INVALID_NODE || doc.get(table_id).kind != NodeKind::Table)
    return false;

  // Flatten all cells in document order within the table.
  std::vector<NodeId> cells;
  for (NodeId rid : doc.get(table_id).children) {
    if (doc.get(rid).kind != NodeKind::TableRow) continue;
    for (NodeId cid : doc.get(rid).children) {
      if (doc.get(cid).kind == NodeKind::TableCell) cells.push_back(cid);
    }
  }
  if (cells.size() < 2) return false;
  auto it = std::find(cells.begin(), cells.end(), caret.node);
  if (it == cells.end()) return false;
  size_t idx = static_cast<size_t>(it - cells.begin());
  if (forward) {
    if (idx + 1 >= cells.size()) return false;
    ++idx;
  } else {
    if (idx == 0) return false;
    --idx;
  }
  caret.node = cells[idx];
  caret.span_index = 0;
  caret.char_index = 0;
  return true;
}

void collect_outline(const Document& doc, std::vector<OutlineEntry>& out) {
  out.clear();
  if (doc.root == INVALID_NODE) return;
  // DFS with section depth: Article/Section increase depth for nested headers.
  struct Frame {
    NodeId id;
    int depth;
  };
  std::vector<Frame> stack;
  stack.push_back({doc.root, 0});
  while (!stack.empty()) {
    Frame fr = stack.back();
    stack.pop_back();
    const Node& n = doc.get(fr.id);
    if (n.id != fr.id) continue;  // stale child id — skip
    int depth = fr.depth;
    if (n.kind == NodeKind::Section || n.kind == NodeKind::Article)
      depth = fr.depth + 1;
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header) {
      OutlineEntry e;
      e.node = fr.id;
      if (n.kind == NodeKind::Heading && n.level > 0)
        e.level = std::max(1, std::min(6, n.level));
      else
        e.level = std::max(1, std::min(8, depth > 0 ? depth : 1));
      e.text = node_text(doc, fr.id);
      if (e.text.empty()) e.text = "(empty header)";
      out.push_back(std::move(e));
    }
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
      stack.push_back({*it, depth});
  }
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
  std::string q = ascii_lower(query);

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

std::optional<Caret> find_prev(const Document& doc, const std::string& query,
                               const Caret& from, size_t& match_len) {
  match_len = 0;
  if (query.empty() || doc.root == INVALID_NODE) return std::nullopt;

  std::string q = ascii_lower(query);

  std::vector<NodeId> nodes;
  collect_text_nodes(doc, doc.root, nodes);
  if (nodes.empty()) return std::nullopt;

  auto node_rank = [&](NodeId id) -> size_t {
    auto it = std::find(nodes.begin(), nodes.end(), id);
    return it == nodes.end() ? 0 : static_cast<size_t>(it - nodes.begin());
  };

  size_t start_rank = node_rank(from.node);

  auto search_node_back = [&](NodeId nid, size_t to_span,
                              size_t to_ch) -> std::optional<Caret> {
    const Node& n = doc.get(nid);
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
    if (lower.empty() || q.size() > lower.size()) return std::nullopt;

    // End exclusive index: first map entry at or after (to_span, to_ch)
    size_t end = 0;
    bool found = false;
    for (size_t i = 0; i < map.size(); ++i) {
      if (map[i].span > to_span ||
          (map[i].span == to_span && map[i].ch >= to_ch)) {
        end = i;
        found = true;
        break;
      }
    }
    if (!found) end = lower.size();
    if (end == 0) return std::nullopt;

    // Search in [0, end) for the last occurrence of q
    size_t search_end = end;
    if (search_end < q.size()) return std::nullopt;
    auto pos = lower.rfind(q, search_end - q.size());
    if (pos == std::string::npos || pos + q.size() > end) return std::nullopt;

    Caret c;
    c.node = nid;
    c.span_index = map[pos].span;
    c.char_index = map[pos].ch;
    match_len = q.size();
    return c;
  };

  // Search current node before caret, then earlier nodes, then wrap from end
  for (size_t pass = 0; pass < 2; ++pass) {
    if (pass == 0) {
      // Current node: only before the caret
      if (auto hit =
              search_node_back(nodes[start_rank], from.span_index, from.char_index))
        return hit;
      // Earlier nodes (full text)
      for (size_t i = start_rank; i > 0; --i) {
        size_t idx = i - 1;
        const Node& n = doc.get(nodes[idx]);
        size_t last_span = n.spans.empty() ? 0 : n.spans.size() - 1;
        size_t last_ch = 0;
        if (!n.spans.empty())
          last_ch = utf8_char_count(n.spans[last_span].text) + 1;
        if (auto hit = search_node_back(nodes[idx], last_span, last_ch))
          return hit;
      }
    } else {
      // Wrap: nodes after start_rank, from the end
      for (size_t i = nodes.size(); i > start_rank + 1; --i) {
        size_t idx = i - 1;
        const Node& n = doc.get(nodes[idx]);
        size_t last_span = n.spans.empty() ? 0 : n.spans.size() - 1;
        size_t last_ch = 0;
        if (!n.spans.empty())
          last_ch = utf8_char_count(n.spans[last_span].text) + 1;
        if (auto hit = search_node_back(nodes[idx], last_span, last_ch))
          return hit;
      }
    }
  }
  return std::nullopt;
}

size_t replace_all(Document& doc, const std::string& query,
                   const std::string& replacement) {
  if (query.empty() || doc.root == INVALID_NODE) return 0;
  size_t count = 0;
  std::vector<NodeId> nodes;
  collect_text_nodes(doc, doc.root, nodes);
  std::string q = ascii_lower(query);
  const size_t qlen = q.size();
  if (qlen == 0) return 0;

  for (NodeId nid : nodes) {
    size_t search_from = 0;  // index into lower map for this node
    for (;;) {
      Node& n = doc.get(nid);
      struct Map {
        size_t span;
        size_t ch;
      };
      std::string lower;
      std::vector<Map> map;
      for (size_t si = 0; si < n.spans.size(); ++si) {
        size_t ci = 0, bi = 0;
        const std::string& t = n.spans[si].text;
        while (bi < t.size()) {
          auto [cp, len] = decode_cp(t, bi);
          if (len == 0) break;
          char ascii = (cp < 128) ? static_cast<char>(cp) : '?';
          if (ascii >= 'A' && ascii <= 'Z')
            ascii = static_cast<char>(ascii - 'A' + 'a');
          lower.push_back(ascii);
          map.push_back({si, ci});
          bi += len;
          ++ci;
        }
      }
      if (search_from >= lower.size()) break;
      auto pos = lower.find(q, search_from);
      if (pos == std::string::npos || pos >= map.size()) break;
      Caret a{nid, map[pos].span, map[pos].ch};
      Caret b = a;
      advance_caret(doc, b, qlen);
      Selection sel;
      sel.anchor = a;
      sel.focus = b;
      delete_selection(doc, sel);
      size_t repl_chars = 0;
      if (!replacement.empty()) {
        insert_text(doc, sel.focus, replacement);
        repl_chars = utf8_char_count(replacement);
      }
      ++count;
      // Skip past the just-written replacement so we do not loop when
      // replacement contains the query (e.g. "a" → "aa").
      search_from = pos + std::max(size_t{1}, repl_chars);
    }
  }
  return count;
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
  // Full document snapshots are O(nodes). Cap depth hard on large trees so
  // a 10MB org file does not keep dozens of full copies in RAM.
  size_t depth = max_depth_;
  const size_t n = doc.nodes.size();
  if (n > 80000)
    depth = 4;
  else if (n > 20000)
    depth = 8;
  else if (n > 5000)
    depth = 16;
  undo_.push_back({doc, sel});
  while (undo_.size() > depth) undo_.pop_front();
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

}  // namespace xanatoo
