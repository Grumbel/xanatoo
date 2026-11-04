// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/layout.hpp"
#include "xanatoo/math_font.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace xanatoo {

namespace {

// True if `id` is `root` or nested under `root`.
bool is_section_kind(NodeKind kind) {
  return kind == NodeKind::Section || kind == NodeKind::Article;
}

bool node_is_descendant(const Document& doc, NodeId root, NodeId id) {
  if (root == INVALID_NODE || id == INVALID_NODE) return false;
  for (NodeId cur = id; cur != INVALID_NODE && cur < doc.next_id;
       cur = doc.get(cur).parent) {
    if (cur == root) return true;
  }
  return false;
}


// Cached external documents for :::include path.md[#anchor].
// Keyed by resolved absolute path. Cleared via clear_external_doc_cache().
std::map<std::string, Document>& external_doc_cache_map() {
  static std::map<std::string, Document> cache;
  return cache;
}

void clear_external_doc_cache_impl() {
  external_doc_cache_map().clear();
}

const Document* load_external_doc(const std::string& path) {
  if (path.empty()) return nullptr;
  std::string key = path;
  try {
    key = std::filesystem::absolute(path).lexically_normal().string();
  } catch (...) {
  }
  auto& cache = external_doc_cache_map();
  auto it = cache.find(key);
  if (it != cache.end()) return &it->second;

  std::ifstream in(path);
  if (!in) return nullptr;
  std::string source((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  Document doc = parse_document(source, path);
  auto [ins, _] = cache.emplace(key, std::move(doc));
  return &ins->second;
}

// Split src into (path, #anchor). path empty ⇒ same-document.
// Forms: "#H", "file.md#H", "file.md", "dir/x.md#H"
void split_include_src(const std::string& src, std::string& path_out,
                       std::string& href_out) {
  path_out.clear();
  href_out.clear();
  if (src.empty()) return;
  if (src[0] == '#') {
    href_out = src;
    return;
  }
  size_t hash = src.find('#');
  if (hash == std::string::npos) {
    path_out = src;
    return;
  }
  path_out = src.substr(0, hash);
  href_out = src.substr(hash);  // includes '#'
}

}  // namespace

void clear_external_doc_cache() {
  clear_external_doc_cache_impl();
}

static const DocStyle& default_style() {
  static const DocStyle style = builtin_style();
  return style;
}

LayoutEngine::LayoutEngine(FontSet& fonts, ImageCache* images)
    : fonts_(fonts), images_(images), style_(&default_style()) {}

void LayoutEngine::set_style(const DocStyle* style) {
  style_ = style ? style : &default_style();
}

// Face for an inline span inside a block set in `base` (a bold / italic
// block face combines with the span's own emphasis).
static FontRole style_role(FontRole base, InlineKind kind) {
  switch (base) {  // small block faces: the body logic, then made small
    case FontRole::SmallCode: return FontRole::SmallCode;
    case FontRole::Small: return small_role(style_role(FontRole::Body, kind));
    case FontRole::SmallBold:
      return small_role(style_role(FontRole::BodyBold, kind));
    case FontRole::SmallItalic:
      return small_role(style_role(FontRole::BodyItalic, kind));
    case FontRole::SmallBoldItalic:
      return small_role(style_role(FontRole::BodyBoldItalic, kind));
    default: break;
  }
  if (kind == InlineKind::Code || base == FontRole::Code) return FontRole::Code;
  if (base == FontRole::Heading) return FontRole::Heading;
  const bool strong = kind == InlineKind::Strong ||
                      base == FontRole::BodyBold ||
                      base == FontRole::BodyBoldItalic;
  const bool emph = kind == InlineKind::Emphasis ||
                    base == FontRole::BodyItalic ||
                    base == FontRole::BodyBoldItalic;
  if (strong && emph) return FontRole::BodyBoldItalic;
  if (strong) return FontRole::BodyBold;
  if (emph) return FontRole::BodyItalic;
  return FontRole::Body;
}

static void decode_utf8(const std::string& text, std::vector<uint32_t>& out);

// Unicode superscript for an ASCII digit, else 0.
static uint32_t superscript_digit(uint32_t cp) {
  switch (cp) {
    case '1': return 0x00B9;
    case '2': return 0x00B2;
    case '3': return 0x00B3;
    default:
      return (cp >= '0' && cp <= '9') ? 0x2070 + (cp - '0') : 0;
  }
}

// Message in-reply-to label → node id (rebuilt each layout).
thread_local std::unordered_map<std::string, NodeId> g_msg_label_map;

bool LayoutEngine::try_skip_block(NodeId id, float x, float& y, float max_width,
                                  LayoutResult& out) {
  if (!viewport_.active() || !height_hints_ || id == INVALID_NODE)
    return false;
  auto it = height_hints_->find(id);
  if (it == height_hints_->end()) return false;
  const float h = it->second;
  if (h < 1.f) return false;
  const float y0 = y;
  const float y1 = y + h;
  const float band0 = viewport_.top - viewport_.overscan;
  const float band1 = viewport_.bottom + viewport_.overscan;
  if (y1 < band0 || y0 > band1) {
    out.boxes.push_back({id, x, y0, max_width, h});
    y = y1;
    skipped_any_ = true;
    return true;
  }
  return false;
}

// Everything `in` laid out (a column, a table cell) added to `out`.
static void append_result(LayoutResult& out, const LayoutResult& in) {
  auto add = [](auto& dst, const auto& src) {
    dst.insert(dst.end(), src.begin(), src.end());
  };
  add(out.glyphs, in.glyphs);
  add(out.boxes, in.boxes);
  add(out.images, in.images);
  add(out.maths, in.maths);
  add(out.quote_bars, in.quote_bars);
  add(out.note_bars, in.note_bars);
  add(out.message_bars, in.message_bars);
  add(out.message_cards, in.message_cards);
  add(out.code_blocks, in.code_blocks);
  add(out.table_lines, in.table_lines);
  add(out.table_header_fills, in.table_header_fills);
  add(out.hr_lines, in.hr_lines);
  add(out.role_fills, in.role_fills);
  add(out.role_rules, in.role_rules);
  add(out.role_frames, in.role_frames);
  add(out.column_gaps, in.column_gaps);
  add(out.page_gaps, in.page_gaps);
}

// Blocks with a box of their own (fills, grids, pictures) that cannot run
// past a floating aside: they start below it.
static bool clears_floats(NodeKind k) {
  switch (k) {
    case NodeKind::Table:
    case NodeKind::CodeBlock:
    case NodeKind::Image:
    case NodeKind::Figure:
    case NodeKind::Columns:
    case NodeKind::Collection:
    case NodeKind::Thread:
    case NodeKind::Transclude:
    case NodeKind::HorizontalRule:
      return true;
    default:
      return false;
  }
}

void LayoutEngine::float_band(float top, float bottom, float& x,
                              float& w) const {
  float l = x, r = x + w;
  for (const FloatBox& f : floats_) {
    if (f.bottom <= top || f.top >= bottom) continue;
    if (f.right <= l || f.left >= r) continue;  // not in this column
    if (f.side == FloatSide::Right) r = std::min(r, f.left);
    else l = std::max(l, f.right);
  }
  if (r - l < 1.f) return;  // no room at all: overlap rather than vanish
  x = l;
  w = r - l;
}

float LayoutEngine::clear_floats(float x, float w, float y) const {
  float out = y;
  for (const FloatBox& f : floats_) {
    if (f.bottom <= y || f.top > y + 0.5f) continue;
    if (f.right <= x || f.left >= x + w) continue;
    out = std::max(out, f.bottom);
  }
  return out;
}

void LayoutEngine::layout_children_sequential(
    const Document& doc, const std::vector<NodeId>& children, float x, float& y,
    float max_width, LayoutResult& out) {
  for (NodeId child : children) {
    if (child == INVALID_NODE) continue;
    const NodeKind kind = doc.get(child).kind;
    if (!floats_.empty() && clears_floats(kind))
      y = clear_floats(x, max_width, y);
    // Side boxes may not advance y: their box height is no hint.
    if (kind != NodeKind::Aside &&
        try_skip_block(child, x, y, max_width, out))
      continue;
    layout_node(doc, child, x, y, max_width, out);
  }
}

std::vector<NodeId> chrome_regions(const Document& doc) {
  std::vector<NodeId> out;
  if (doc.root == INVALID_NODE) return out;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    if (id == INVALID_NODE || id >= doc.next_id) continue;
    const Node& n = doc.get(id);
    if (n.id != id) continue;
    if (n.kind == NodeKind::Chrome) {
      out.push_back(id);
      continue;
    }
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
      stack.push_back(*it);
  }
  return out;
}

bool margin_chrome(const Document& doc, const Node& chrome) {
  if (chrome.kind != NodeKind::Chrome) return false;
  if (chrome.summary != "navigation" && chrome.summary != "sidebar")
    return false;
  std::vector<NodeId> stack(chrome.children.begin(), chrome.children.end());
  while (!stack.empty()) {
    const NodeId id = stack.back();
    stack.pop_back();
    if (id == INVALID_NODE || id >= doc.next_id) continue;
    const Node& n = doc.get(id);
    if (n.id != id) continue;
    switch (n.kind) {
      case NodeKind::Table:
      case NodeKind::CodeBlock:
      case NodeKind::Columns:
      case NodeKind::Collection:
        return false;  // needs the column's width
      default:
        break;
    }
    stack.insert(stack.end(), n.children.begin(), n.children.end());
  }
  return true;
}

std::string chrome_hidden_text(const Document& doc,
                               const std::vector<NodeId>& regions) {
  if (regions.empty()) return {};
  std::vector<std::string> kinds;
  for (NodeId id : regions) {
    const std::string& k = doc.get(id).summary;
    if (!k.empty() && std::find(kinds.begin(), kinds.end(), k) == kinds.end())
      kinds.push_back(k);
  }
  std::string t = regions.size() == 1
                      ? std::string("1 part of the page hidden")
                      : std::to_string(regions.size()) + " parts of the page hidden";
  if (!kinds.empty()) {
    t += " (";
    for (size_t i = 0; i < kinds.size() && i < 4; ++i) {
      if (i) t += ", ";
      t += kinds[i];
    }
    if (kinds.size() > 4) t += ", …";
    t += ")";
  }
  return t;
}

CollectionView default_collection_view(const Node& collection) {
  CollectionView v;
  const std::string hint = ascii_lower(collection.summary);
  if (hint == "list") v.layout = CollectionLayout::List;
  else if (hint == "full") v.layout = CollectionLayout::Full;
  return v;
}

std::vector<CollectionItem> collection_items(const Document& doc,
                                             NodeId collection,
                                             CollectionSort sort) {
  std::vector<CollectionItem> items;
  const Node& coll = doc.get(collection);
  if (coll.id != collection) return items;
  auto plain = [](const Node& n) {
    std::string t;
    for (const auto& sp : n.spans) t += sp.text;
    return t;
  };
  for (NodeId c : coll.children) {
    const Node& ch = doc.get(c);
    if (ch.id != c) continue;
    CollectionItem it;
    it.item = c;
    if (ch.kind == NodeKind::Image) {
      it.image = c;
      it.src = ch.src;
      it.caption = ch.alt;
    } else if (ch.kind == NodeKind::Figure) {
      for (NodeId g : ch.children) {
        const Node& gn = doc.get(g);
        if (gn.kind == NodeKind::Image && it.image == INVALID_NODE) {
          it.image = g;
          it.src = gn.src;
          if (it.caption.empty()) it.caption = gn.alt;
        } else if (gn.kind == NodeKind::Caption) {
          it.caption = plain(gn);
        }
      }
    } else {
      continue;
    }
    items.push_back(std::move(it));
  }
  if (sort != CollectionSort::Document) {
    auto name = [](const CollectionItem& it) {
      return ascii_lower(std::filesystem::path(it.src).filename().string());
    };
    std::stable_sort(items.begin(), items.end(),
                     [&](const CollectionItem& a, const CollectionItem& b) {
                       return sort == CollectionSort::Name ? name(a) < name(b)
                                                           : name(b) < name(a);
                     });
  }
  return items;
}

// Cards for a Collection: a grid of thumbnails with captions, a list
// (small thumbnail + caption), or the plain children (Full). Other
// children (intro paragraphs, …) come first at full width.
void LayoutEngine::layout_remote_image(const Document& doc, const Node& node,
                                       float x, float& y, float max_width,
                                       LayoutResult& out) {
  const float dp = options_.px_scale;
  const float max_h = 360.f * dp;
  float w = std::min(max_width, 320.f * dp);
  float h = 180.f * dp;
  if (images_) {
    const Image& img = images_->thumb(node.src, image_base_, kRemoteThumbEdge);
    if (!img.ok && !img.pending) {
      out.boxes.push_back({node.id, x, y, max_width, 0.f});
      return;
    }
    if (img.ok && img.width > 0 && img.height > 0) {
      // Thumbnail size at most: never blown up past what was downloaded.
      const float sc = std::min({dp, max_width / img.width, max_h / img.height});
      w = img.width * sc;
      h = img.height * sc;
    }
  }
  const float start_y = y;
  PositionedImage pi{node.id, x, y, w, h, node.src, image_base_};
  pi.thumb = kRemoteThumbEdge;
  out.images.push_back(std::move(pi));
  y += h + 6.f * dp;
  if (!node.alt.empty()) {
    Node cap = node;
    cap.kind = NodeKind::Paragraph;
    cap.spans.clear();
    InlineSpan s;
    s.kind = InlineKind::Emphasis;
    s.text = node.alt;
    cap.spans.push_back(std::move(s));
    layout_paragraph(doc, cap, x, y, max_width, out, FontRole::Body);
  }
  y += 8.f * dp;
  out.boxes.push_back({node.id, x, start_y, max_width, y - start_y});
}

void LayoutEngine::layout_collection(const Document& doc, NodeId id, float x,
                                     float& y, float max_width,
                                     LayoutResult& out) {
  const Node& node = doc.get(id);
  const DocStyle& st = *style_;
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  CollectionView view = default_collection_view(node);
  if (collection_views_) {
    auto it = collection_views_->find(id);
    if (it != collection_views_->end()) view = it->second;
  }
  y += st[BlockRole::Figure].space_before.px(em, dp);
  const float start_y = y;

  if (view.layout == CollectionLayout::Full) {
    for (NodeId c : node.children) layout_node(doc, c, x, y, max_width, out);
  } else {
    for (NodeId c : node.children) {
      const NodeKind k = doc.get(c).kind;
      if (k != NodeKind::Image && k != NodeKind::Figure)
        layout_node(doc, c, x, y, max_width, out);
    }
    const auto items = collection_items(doc, id, view.sort);
    const float gap = 12.f * dp;
    const bool list = view.layout == CollectionLayout::List;
    const float box = list ? std::max(48.f, view.thumb_px * 0.5f) * dp
                           : std::max(48.f, view.thumb_px) * dp;
    int cols = list ? 1 : std::max(1, static_cast<int>((max_width + gap) /
                                                       (box + gap)));
    cols = std::min<int>(cols, std::max<size_t>(1, items.size()));
    const float cell_w =
        list ? max_width : (max_width - gap * static_cast<float>(cols - 1)) /
                               static_cast<float>(cols);
    const float img_w = list ? box : cell_w;

    // Caption text for one card (Figure caption node, else image alt).
    auto caption = [&](const CollectionItem& it, float cx, float& cy,
                       float w) {
      const Node& item = doc.get(it.item);
      for (NodeId g : item.children) {
        const Node& gn = doc.get(g);
        if (gn.kind == NodeKind::Caption) {
          layout_role_text(doc, gn, BlockRole::Caption, cx, cy, w, out);
          return;
        }
      }
      if (it.image == INVALID_NODE || it.caption.empty()) return;
      Node fake = doc.get(it.image);
      fake.kind = NodeKind::Paragraph;
      fake.spans.clear();
      InlineSpan sp;
      sp.kind = InlineKind::Emphasis;
      sp.text = it.caption;
      fake.spans.push_back(std::move(sp));
      layout_role_text(doc, fake, BlockRole::Caption, cx, cy, w, out);
    };

    float row_y = y;
    float row_h = 0.f;
    for (size_t i = 0; i < items.size(); ++i) {
      const auto& it = items[i];
      const int col = static_cast<int>(i % static_cast<size_t>(cols));
      if (col == 0 && i > 0) {
        row_y += row_h + gap;
        row_h = 0.f;
      }
      const float cx = x + static_cast<float>(col) * (cell_w + gap);
      // Thumbnail fitted into img_w × box, centred.
      float tw = img_w, th = box;
      const int edge = static_cast<int>(std::max(img_w, box));
      if (images_ && !it.src.empty()) {
        const Image& img = images_->thumb(it.src, image_base_, edge);
        if (img.ok && img.width > 0 && img.height > 0) {
          const float sc = std::min(img_w / static_cast<float>(img.width),
                                    box / static_cast<float>(img.height));
          tw = static_cast<float>(img.width) * sc;
          th = static_cast<float>(img.height) * sc;
        }
      }
      const float ix = cx + (img_w - tw) * 0.5f;
      const float iy = row_y + (box - th) * 0.5f;
      if (it.image != INVALID_NODE) {
        PositionedImage pi;
        pi.node = it.image;
        pi.x = ix;
        pi.y = iy;
        pi.width = tw;
        pi.height = th;
        pi.src = it.src;
        pi.base = image_base_;
        pi.thumb = edge;
        pi.group = id;
        out.images.push_back(std::move(pi));
        out.boxes.push_back({it.image, ix, iy, tw, th});
      }
      float cy;
      float text_x;
      float text_w;
      if (list) {
        text_x = cx + box + gap;
        text_w = std::max(40.f, cell_w - box - gap);
        cy = row_y + 2.f;
      } else {
        text_x = cx;
        text_w = cell_w;
        cy = row_y + box + 4.f * dp;
      }
      const size_t first_glyph = out.glyphs.size();
      caption(it, text_x, cy, text_w);
      if (!list) {
        // Centre each caption line under the thumbnail.
        size_t a = first_glyph;
        while (a < out.glyphs.size()) {
          size_t b = a;
          float right = text_x;
          while (b < out.glyphs.size() && out.glyphs[b].y == out.glyphs[a].y) {
            const auto& g = out.glyphs[b];
            right = std::max(right, g.x + static_cast<float>(
                                              fonts_.pick(g.font)
                                                  .glyph(g.codepoint)
                                                  .advance >> 6));
            ++b;
          }
          const float shift = std::max(0.f, (text_w - (right - text_x)) * 0.5f);
          for (size_t k = a; k < b; ++k) out.glyphs[k].x += shift;
          a = b;
        }
      }
      const float h = std::max(box, cy - row_y);
      row_h = std::max(row_h, h);
      out.boxes.push_back({it.item, cx, row_y, cell_w, h});
    }
    if (!items.empty()) y = row_y + row_h;
  }
  y += st[BlockRole::Figure].space_after.px(em, dp);
  out.boxes.push_back({id, x, start_y, max_width, y - start_y});
}

LayoutResult LayoutEngine::layout(
    const Document& doc, float max_width, const std::string& image_base,
    const LayoutViewport& viewport,
    const std::unordered_map<NodeId, float>* height_hints,
    const std::unordered_set<NodeId>* folded_sections,
    NodeId focus_section) {
  static uint64_t next_generation = 0;
  LayoutResult result;
  result.generation = ++next_generation;
  image_base_ = image_base;
  viewport_ = viewport;
  height_hints_ = height_hints;
  folded_sections_ = folded_sections;
  focus_section_ = focus_section;
  skipped_any_ = false;
  floats_.clear();
  if (viewport_.active()) {
    result.materialised_top = viewport_.top - viewport_.overscan;
    result.materialised_bottom = viewport_.bottom + viewport_.overscan;
  }
  if (doc.root == INVALID_NODE) return result;
  // Rough glyph budget: a few characters per node on average.
  // When virtualizing, most blocks are skipped so reserve less aggressively.
  const size_t glyph_budget =
      (viewport_.active() && height_hints_ && !height_hints_->empty())
          ? doc.nodes.size() * 2
          : doc.nodes.size() * 8;
  result.glyphs.reserve(glyph_budget);
  result.boxes.reserve(doc.nodes.size());
  g_msg_label_map.clear();
  for (const auto& n : doc.nodes) {
    if (n.kind == NodeKind::Message && !n.label.empty())
      g_msg_label_map.emplace(n.label, n.id);
  }
  float mx = std::max(options_.margin_x, options_.margin_pct * max_width);
  float y = options_.margin_y;
  float content_w = std::max(40.f, max_width - 2.f * mx);
  if (options_.measure_chars > 0) {
    // Average lowercase advance of the body font: follows zoom and sheet.
    const float ch =
        fonts_.pick(FontRole::Body).measure("abcdefghijklmnopqrstuvwxyz") /
        26.f;
    const float cap = std::max(40.f, options_.measure_chars * ch);
    if (content_w > cap) {
      mx += (content_w - cap) * 0.5f;
      content_w = cap;
    }
  }
  setup_margins(doc, mx, content_w, max_width);
  if (style_->has_margin()) {
    const float pad = 0.75f * static_cast<float>(fonts_.regular.pixel_size());
    result.page_x = mx - pad;
    result.page_w = content_w + 2.f * pad;
  }
  layout_node(doc, doc.root, mx, y, content_w, result);
  if (!result.page_gaps.empty()) {
    // A gap at the very top / bottom of the page is the margin itself.
    LayoutBox& first = result.page_gaps.front();
    if (first.y <= options_.margin_y + 0.5f) {
      first.height += first.y;
      first.y = 0.f;
    }
    LayoutBox& last = result.page_gaps.back();
    if (last.y + last.height >= y - 0.5f) last.height += 1.0e5f;
  }
  for (const FloatBox& f : floats_) y = std::max(y, f.bottom);
  floats_.clear();
  for (const MarginArea& m : margins_) y = std::max(y, m.bottom);
  result.content_height = y + options_.margin_y + 24.f;
  result.virtualized = skipped_any_;
  height_hints_ = nullptr;
  folded_sections_ = nullptr;
  focus_section_ = INVALID_NODE;
  return result;
}

void LayoutEngine::layout_node(const Document& doc, NodeId id,
                               float x, float& y, float max_width,
                               LayoutResult& out) {
  if (id == INVALID_NODE) return;
  const Node& node = doc.get(id);
  if (node.id != id) return;  // stale child id → invalid-node sentinel

  // Role spacing from the style sheet; em follows the body size (zoom).
  const DocStyle& st = *style_;
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  auto before = [&](BlockRole r) { return st[r].space_before.px(em, dp); };
  auto after = [&](BlockRole r) { return st[r].space_after.px(em, dp); };
  auto indent = [&](BlockRole r) { return st[r].indent.px(em, dp); };

  switch (node.kind) {
    case NodeKind::Document:
      if (focus_section_ != INVALID_NODE) {
        // A cut that is no part of the section tree is laid out on its own.
        NodeId top = focus_section_;
        while (doc.get(top).parent != id &&
               is_section_kind(doc.get(top).kind))
          top = doc.get(top).parent;
        if (doc.get(top).parent != id || !is_section_kind(doc.get(top).kind)) {
          layout_node(doc, focus_section_, x, y, max_width, out);
          break;
        }
        // Only section tree on the path to the focus; skip preamble blocks.
        for (NodeId child : node.children) {
          const Node& ch = doc.get(child);
          if (ch.kind == NodeKind::Section || ch.kind == NodeKind::Article)
            layout_node(doc, child, x, y, max_width, out);
        }
      } else {
        if (hide_chrome_) layout_chrome_notice(doc, x, y, max_width, out);
        layout_children_sequential(doc, node.children, x, y, max_width, out);
      }
      break;

    case NodeKind::Chrome: {
      if (hide_chrome_) break;  // reader mode
      // Navigation / side bars shown in the Page panel instead.
      if (hide_panel_chrome_ && margin_chrome(doc, node)) break;
      float start_y = y;
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Aside:
      layout_aside(doc, node, x, y, max_width, out);
      break;

    case NodeKind::List:
      y += before(BlockRole::List);
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      y += after(BlockRole::List);  // trailing gap after the list block
      break;

    case NodeKind::Column:
      // Column content is laid out by the parent Columns node.
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      break;

    case NodeKind::Columns: {
      // Side-by-side columns. Widths from Column.display_scale (relative weight).
      std::vector<NodeId> cols;
      for (NodeId child : node.children) {
        if (doc.get(child).kind == NodeKind::Column) {
          cols.push_back(child);
        }
      }
      if (cols.empty()) {
        for (NodeId child : node.children) {
          layout_node(doc, child, x, y, max_width, out);
        }
        break;
      }

      const float gutter = 16.f;
      const float n = static_cast<float>(cols.size());
      float weight_sum = 0.f;
      std::vector<float> weights(cols.size(), 1.f);
      for (size_t i = 0; i < cols.size(); ++i) {
        float w = doc.get(cols[i]).display_scale;
        if (w < 0.1f) w = 1.f;
        weights[i] = w;
        weight_sum += w;
      }
      if (weight_sum < 0.1f) weight_sum = n;

      const float usable = max_width - gutter * (n - 1.f);
      if (usable / n < 40.f) {
        for (NodeId child : cols) {
          layout_node(doc, child, x, y, max_width, out);
        }
        break;
      }

      float start_y = y;
      float max_bottom = y;
      float cx = x;
      size_t gap_base = out.column_gaps.size();

      for (size_t i = 0; i < cols.size(); ++i) {
        float col_w = usable * (weights[i] / weight_sum);
        // Keep columns inside the usable width (never force min 40 past usable).
        if (col_w < 1.f) col_w = 1.f;
        if (col_w > usable) col_w = usable;
        float cy = start_y;

        LayoutResult col_out;
        layout_node(doc, cols[i], cx, cy, col_w, col_out);

        append_result(out, col_out);

        if (cy > max_bottom) max_bottom = cy;

        if (i + 1 < cols.size()) {
          ColumnGap gap;
          gap.columns = id;
          gap.left = cols[i];
          gap.right = cols[i + 1];
          gap.x = cx + col_w + gutter * 0.5f;
          gap.y = start_y;
          gap.height = 0.f;
          gap.left_w = col_w;
          gap.right_w = usable * (weights[i + 1] / weight_sum);
          out.column_gaps.push_back(gap);
        }
        cx += col_w + gutter;
      }

      for (size_t gi = gap_base; gi < out.column_gaps.size(); ++gi) {
        if (out.column_gaps[gi].columns == id)
          out.column_gaps[gi].height = max_bottom - start_y;
      }

      out.boxes.push_back({id, x, start_y, max_width, max_bottom - start_y});
      y = max_bottom + 12.f;
      break;
    }

    case NodeKind::Stretch: {
      if (!node.expanded) {
        // Collapsed: show summary as a single interactive line
        Node fake = node;
        fake.kind = NodeKind::Paragraph;
        fake.spans.clear();
        InlineSpan s;
        s.kind = InlineKind::Link;  // visual cue
        s.text = node.summary.empty() ? "▸ …" : ("▸ " + node.summary);
        fake.spans.push_back(std::move(s));
        float start_y = y;
        layout_paragraph(doc, fake, x, y, max_width, out, FontRole::Body);
        // Rewrite glyph node ids back to the stretch node so hit-testing works
        for (auto& g : out.glyphs) {
          if (g.node == fake.id) g.node = id;
        }
        y += 10.f;
        out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      } else {
        // Expanded: optional summary header + children
        if (!node.summary.empty()) {
          Node fake = node;
          fake.kind = NodeKind::Paragraph;
          fake.spans.clear();
          InlineSpan s;
          s.kind = InlineKind::Link;
          s.text = "▾ " + node.summary;
          fake.spans.push_back(std::move(s));
          float start_y = y;
          layout_paragraph(doc, fake, x, y, max_width, out, FontRole::Body);
          for (auto& g : out.glyphs) {
            if (g.node == fake.id) g.node = id;
          }
          out.boxes.push_back({id, x, start_y, max_width, y - start_y});
        }
        for (NodeId child : node.children) {
          layout_node(doc, child, x + 12.f, y, max_width - 12.f, out);
        }
        y += 6.f;
      }
      break;
    }

    case NodeKind::Heading: {
      FontRole role = (node.level <= 2) ? FontRole::Heading : FontRole::Body;
      float start_y = y;
      y += (node.level == 1) ? 12.f : 8.f;
      layout_paragraph(doc, node, x, y, max_width, out, role,
                       BlockRole::Header);
      y += (node.level == 1) ? 14.f : 10.f;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Header: {
      float start_y = y;
      y += before(BlockRole::Header);
      const float text_y = y;
      // Org TODO / :tags: live in src + tags. Show them as decoration glyphs
      // that do not own a real span index (node = INVALID) so caret/edit stay
      // on the title text only.
      if (!node.src.empty() || !node.tags.empty()) {
        const size_t glyph0 = out.glyphs.size();
        size_t deco_before = node.src.empty() ? 0 : 1;
        Node vis = node;
        vis.spans.clear();
        if (!node.src.empty()) {
          InlineSpan todo;
          todo.kind = InlineKind::Strong;
          todo.text = node.src + " ";
          vis.spans.push_back(std::move(todo));
        }
        for (const auto& sp : node.spans) vis.spans.push_back(sp);
        size_t deco_tag_span = std::string::npos;
        if (!node.tags.empty()) {
          deco_tag_span = vis.spans.size();
          InlineSpan tg;
          tg.kind = InlineKind::Code;
          tg.text = "  :" + node.tags + ":";
          vis.spans.push_back(std::move(tg));
        }
        layout_role_text(doc, vis, BlockRole::Header, x, y, max_width, out);
        for (size_t gi = glyph0; gi < out.glyphs.size(); ++gi) {
          auto& g = out.glyphs[gi];
          g.node = id;
          if (g.span_index < deco_before ||
              (deco_tag_span != std::string::npos &&
               g.span_index == deco_tag_span)) {
            g.node = INVALID_NODE;  // decoration — not editable
          } else if (deco_before > 0) {
            g.span_index -= deco_before;
          }
        }
      } else {
        layout_role_text(doc, node, BlockRole::Header, x, y, max_width, out);
      }
      add_role_band(BlockRole::Header, id, x, text_y, max_width, y, out);
      y += after(BlockRole::Header);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Article:
    case NodeKind::Section: {
      // Section-focus view: show only the focused section (full), its
      // ancestors (header only), and descendants of the focus (full).
      if (focus_section_ != INVALID_NODE) {
        const bool is_focus = (id == focus_section_);
        const bool under_focus = node_is_descendant(doc, focus_section_, id);
        const bool is_ancestor = node_is_descendant(doc, id, focus_section_);
        if (!is_focus && !under_focus && !is_ancestor)
          break;  // sibling / other branch — omit entirely
        if (is_ancestor && !is_focus && !under_focus) {
          float start_y = y;
          float inset =
              (node.kind == NodeKind::Section) ? indent(BlockRole::Section) : 0.f;
          y += before(BlockRole::Section);
          for (NodeId child : node.children) {
            const Node& ch = doc.get(child);
            if (ch.kind == NodeKind::Header || ch.kind == NodeKind::Heading)
              layout_node(doc, child, x + inset, y, max_width - inset, out);
          }
          // Recurse into section children so the path reaches the focus.
          for (NodeId child : node.children) {
            const Node& ch = doc.get(child);
            if (ch.kind == NodeKind::Section || ch.kind == NodeKind::Article)
              layout_node(doc, child, x + inset, y, max_width - inset, out);
          }
          y += before(BlockRole::Section);
          out.boxes.push_back({id, x, start_y, max_width, y - start_y});
          break;
        }
        // focus or descendant: fall through to full layout
      }
      // Articles on one page are separated by a gap where the margin shows.
      const bool article = node.kind == NodeKind::Article;
      auto gap = [&](float h) {
        if (h > 0.f) out.page_gaps.push_back({id, x, y, max_width, h});
        y += h;
      };
      if (article) gap(before(BlockRole::Article));
      float start_y = y;
      // Nested sections indent slightly; articles keep full width.
      float inset =
          (node.kind == NodeKind::Section) ? indent(BlockRole::Section) : 0.f;
      y += before(BlockRole::Section);
      const bool folded =
          folded_sections_ && folded_sections_->find(id) != folded_sections_->end();
      // Always materialise title headers so fold state stays visible.
      std::vector<NodeId> body;
      body.reserve(node.children.size());
      for (NodeId child : node.children) {
        const Node& ch = doc.get(child);
        if (ch.kind == NodeKind::Header || ch.kind == NodeKind::Heading)
          layout_node(doc, child, x + inset, y, max_width - inset, out);
        else
          body.push_back(child);
      }
      if (folded) {
        // Compact cue under the header (view-local fold; not Stretch).
        y += before(BlockRole::Section);
        out.boxes.push_back({id, x, start_y, max_width, y - start_y});
        if (article) gap(after(BlockRole::Article));
        break;
      }
      layout_children_sequential(doc, body, x + inset, y, max_width - inset, out);
      y += after(BlockRole::Section);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      if (article) gap(after(BlockRole::Article));
      break;
    }

    case NodeKind::Paragraph: {
      float start_y = y;
      y += before(BlockRole::Paragraph);
      layout_role_text(doc, node, BlockRole::Paragraph, x, y, max_width, out);
      y += after(BlockRole::Paragraph);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::BlockQuote: {
      y += before(BlockRole::BlockQuote);
      float start_y = y;
      const float bar_w = 3.f;
      const float inset = indent(BlockRole::BlockQuote);
      float text_x = x + bar_w + inset;
      float text_w = max_width - bar_w - inset;
      if (text_w < 40.f) {
        text_x = x + 8.f;
        text_w = max_width - 8.f;
      }
      layout_role_text(doc, node, BlockRole::BlockQuote, text_x, y, text_w, out);
      y += after(BlockRole::BlockQuote);
      out.quote_bars.push_back({id, x, start_y, bar_w, y - start_y});
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Note: {
      y += before(BlockRole::Note);
      float start_y = y;
      const float bar_w = 3.f;
      const float inset = indent(BlockRole::Note);
      float text_x = x + bar_w + inset;
      float text_w = max_width - bar_w - inset;
      if (text_w < 40.f) {
        text_x = x + 8.f;
        text_w = max_width - 8.f;
      }
      layout_role_text(doc, node, BlockRole::Note, text_x, y, text_w, out);
      y += after(BlockRole::Note);
      out.note_bars.push_back({id, x, start_y, bar_w, y - start_y});
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Thread: {
      float start_y = y;
      // Subject line
      Node header = node;
      header.kind = NodeKind::Paragraph;
      header.spans.clear();
      InlineSpan hs;
      hs.kind = InlineKind::Strong;
      hs.text = node.summary.empty() ? "Thread" : node.summary;
      if (!node.label.empty()) {
        hs.text += "  ·  #";
        hs.text += node.label;
      }
      header.spans.push_back(std::move(hs));
      y += before(BlockRole::Thread);
      const float text_y = y;
      layout_role_text(doc, header, BlockRole::Thread, x, y, max_width, out);
      add_role_band(BlockRole::Thread, id, x, text_y, max_width, y, out);
      for (auto& g : out.glyphs) {
        if (g.node == header.id) g.node = id;
      }
      y += 8.f;
      // Replies by the thread starter get the `message-op` author colour.
      const std::string outer_op = std::move(thread_op_);
      thread_op_.clear();
      for (NodeId child : node.children) {
        const Node& ch = doc.get(child);
        if (ch.kind == NodeKind::Message) {
          thread_op_ = ch.src;
          break;
        }
      }
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      thread_op_ = outer_op;
      y += after(BlockRole::Thread);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Message: {
      y += before(BlockRole::Message);
      float start_y = y;
      // Indent by reply depth (follow in-reply-to chain via labels).
      int depth = 0;
      NodeId reply_to = INVALID_NODE;
      if (!node.alt.empty() && node.alt.rfind("about:", 0) != 0) {
        std::string cur = node.alt;
        for (int guard = 0; guard < 24; ++guard) {
          if (cur.empty()) break;
          std::string key = cur[0] == '#' ? cur.substr(1) : cur;
          NodeId parent_msg = INVALID_NODE;
          auto it = g_msg_label_map.find(key);
          if (it != g_msg_label_map.end()) parent_msg = it->second;
          if (parent_msg == INVALID_NODE) break;
          if (depth == 0) reply_to = parent_msg;
          ++depth;
          cur = doc.get(parent_msg).alt;
          if (cur.rfind("about:", 0) == 0) break;
        }
      }
      const bool alt_card = (depth % 2) == 1;
      if (depth > 8) depth = 8;
      const float indent = 16.f * static_cast<float>(depth);
      const float bar_w = 3.f;
      const float pad = 6.f * dp;  // card inset above the header / at the right
      float body_x = x + indent + bar_w + 10.f;
      float body_w = max_width - indent - bar_w - 10.f - pad;
      if (body_w < 48.f) {
        body_x = x + 8.f;
        body_w = max_width - 8.f;
      }

      // Header: author · date · reply/about, coloured by the message-author /
      // message-op / message-meta roles so posts separate at a glance.
      // Optional semantic spans on Message (0 = author, 1 = date, 2 = reply)
      // annotate the reconstructed header without making layout HN-specific.
      auto header_link = [&](size_t i) -> const InlineSpan* {
        return i < node.spans.size() && node.spans[i].kind == InlineKind::Link
                   ? &node.spans[i]
                   : nullptr;
      };
      Node header = node;
      header.kind = NodeKind::Paragraph;
      header.spans.clear();
      // Header text is synthesized from Message fields. Keep separators as
      // distinct, non-interactive spans, and map the other glyphs back to
      // their semantic Message spans for hit-testing and timestamp hovers.
      constexpr size_t kNoHeaderOwner = static_cast<size_t>(-1);
      std::vector<size_t> header_owner;
      auto add_header = [&](InlineSpan span, size_t owner) {
        header.spans.push_back(std::move(span));
        header_owner.push_back(owner);
      };
      auto add_separator = [&]() {
        InlineSpan sep;
        sep.text = "  ·  ";
        add_header(std::move(sep), kNoHeaderOwner);
      };
      {
        InlineSpan a;
        a.kind = InlineKind::Strong;
        a.text = node.src.empty() ? "(unknown)" : node.src;
        if (const InlineSpan* l = header_link(0)) {
          a.kind = InlineKind::Link;
          a.href = l->href;
        }
        add_header(std::move(a), 0);
      }
      if (!node.summary.empty()) {
        add_separator();
        InlineSpan d;
        d.kind = InlineKind::Text;
        d.text = node.summary;
        if (node.spans.size() > 1) {
          d.datetime = node.spans[1].datetime;
        }
        if (const InlineSpan* l = header_link(1)) {
          d.kind = InlineKind::Link;
          d.href = l->href;
        }
        add_header(std::move(d), 1);
      }
      if (!node.alt.empty()) {
        add_separator();
        InlineSpan r;
        r.kind = InlineKind::Emphasis;
        if (node.alt.rfind("about:", 0) == 0) {
          r.text = "re ";
          r.text += node.alt.substr(6);
        } else if (reply_to != INVALID_NODE &&
                   !doc.get(reply_to).src.empty()) {
          // Name only: "Alice Example <alice@…>" → "Alice Example".
          std::string who = doc.get(reply_to).src;
          const size_t lt = who.find(" <");
          if (lt != std::string::npos && lt > 0) who.resize(lt);
          r.text = "reply to ";
          r.text += who;
        } else {
          r.text = "reply to #";
          r.text += node.alt;
        }
        if (const InlineSpan* l = header_link(2)) {
          r.kind = InlineKind::Link;
          r.href = l->href;
        }
        add_header(std::move(r), 2);
      }
      y += pad;
      const size_t header_glyphs = out.glyphs.size();
      layout_role_text(doc, header, BlockRole::Message, body_x, y, body_w, out);
      const bool is_op = depth > 0 && !thread_op_.empty() &&
                         node.src == thread_op_;
      const BlockStyle& author_st =
          st[is_op ? BlockRole::MessageOp : BlockRole::MessageAuthor];
      const BlockStyle& meta_st = st[BlockRole::MessageMeta];
      for (size_t gi = header_glyphs; gi < out.glyphs.size(); ++gi) {
        PositionedGlyph& g = out.glyphs[gi];
        const size_t owner = g.span_index < header_owner.size()
                                 ? header_owner[g.span_index]
                                 : kNoHeaderOwner;
        if (owner == kNoHeaderOwner) {
          // Decorations have no underlying document span/caret or hyperlink.
          g.node = INVALID_NODE;
        } else {
          g.node = id;
          g.span_index = owner;
        }
        const BlockStyle& bs = owner == 0 ? author_st : meta_st;
        if (bs.has_color) g.color = bs.color;
      }
      y += 4.f;
      for (NodeId child : node.children)
        layout_node(doc, child, body_x, y, body_w, out);
      out.message_cards.push_back(
          {{id, x + indent, start_y, max_width - indent, y - start_y},
           alt_card});
      out.message_bars.push_back(
          {id, x + indent, start_y, bar_w, y - start_y});
      y += after(BlockRole::Message);
      // The box keeps the trailing gap: its height is the virtualization hint.
      out.boxes.push_back({id, x + indent, start_y, max_width - indent,
                           y - start_y});
      break;
    }

    case NodeKind::CodeBlock: {
      y += before(BlockRole::CodeBlock);
      float start_y = y;
      const float pad = indent(BlockRole::CodeBlock);
      float text_x = x + pad;
      float text_w = max_width - pad * 2.f;
      if (text_w < 40.f) {
        text_x = x;
        text_w = max_width;
      }
      layout_role_text(doc, node, BlockRole::CodeBlock, text_x, y, text_w, out);
      y += after(BlockRole::CodeBlock);
      out.code_blocks.push_back({id, x, start_y, max_width, y - start_y});
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::DefList: {
      float start_y = y;
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      y += after(BlockRole::List);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Term: {
      y += before(BlockRole::Term);
      float start_y = y;
      layout_role_text(doc, node, BlockRole::Term, x, y, max_width, out);
      y += after(BlockRole::Term);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Definition: {
      y += before(BlockRole::Definition);
      float start_y = y;
      const float inset = indent(BlockRole::Definition);
      float text_x = x + inset;
      float text_w = max_width - inset;
      if (text_w < 40.f) {
        text_x = x + 8.f;
        text_w = max_width - 8.f;
      }
      layout_role_text(doc, node, BlockRole::Definition, text_x, y, text_w, out);
      y += after(BlockRole::Definition);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Collection:
      layout_collection(doc, id, x, y, max_width, out);
      break;

    case NodeKind::Figure: {
      y += before(BlockRole::Figure);
      float start_y = y;
      for (NodeId c : node.children)
        layout_node(doc, c, x, y, max_width, out);
      y += after(BlockRole::Figure);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Caption: {
      y += before(BlockRole::Caption);
      float start_y = y;
      const float pad = 4.f + indent(BlockRole::Caption);
      layout_role_text(doc, node, BlockRole::Caption, x + pad, y,
                       max_width - pad * 2.f, out);
      y += after(BlockRole::Caption);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Footnote: {
      // Body blocks hang right of the key; the key sits on the first line.
      const float start_y = y;
      y += before(BlockRole::Footnote);
      const float hang = indent(BlockRole::Footnote);
      const size_t first = out.glyphs.size();
      const float body_y = y;
      // Paragraphs take the footnote role (font, colour, tighter spacing).
      for (size_t ci = 0; ci < node.children.size(); ++ci) {
        const NodeId cid = node.children[ci];
        const Node& child = doc.get(cid);
        if (child.id != cid) continue;
        if (child.kind != NodeKind::Paragraph) {
          layout_node(doc, cid, x + hang, y, max_width - hang, out);
          continue;
        }
        if (ci > 0) y += after(BlockRole::Footnote);
        const float py = y;
        layout_role_text(doc, child, BlockRole::Footnote, x + hang, y,
                         max_width - hang, out);
        out.boxes.push_back({cid, x + hang, py, max_width - hang, y - py});
      }
      const FontRole mrole = st[BlockRole::Footnote].font;
      Font& mf = fonts_.pick(mrole);
      float baseline = body_y + static_cast<float>(mf.ascender());
      for (size_t gi = first; gi < out.glyphs.size(); ++gi) {
        if (out.glyphs[gi].node == INVALID_NODE) continue;
        baseline = out.glyphs[gi].y;
        break;
      }
      std::string marker = node.label;
      if (!marker.empty() &&
          marker.find_first_not_of("0123456789") == std::string::npos)
        marker += ".";
      std::vector<uint32_t> m_cps;
      decode_utf8(marker, m_cps);
      float mx = x;
      for (uint32_t cp : m_cps) {
        const Glyph& g = mf.glyph(cp);
        PositionedGlyph pg;
        pg.codepoint = cp;
        pg.x = mx + static_cast<float>(g.bearing_x);
        pg.y = baseline;
        pg.node = INVALID_NODE;  // decoration, not a caret target
        pg.kind = InlineKind::FootnoteRef;
        pg.font = mrole;
        pg.color = st.text_color(BlockRole::Footnote, InlineKind::FootnoteRef);
        pg.advance = mx + static_cast<float>(g.advance >> 6) - pg.x;
        out.glyphs.push_back(pg);
        mx += static_cast<float>(g.advance >> 6);
      }
      if (node.children.empty()) y += mf.line_height();
      y += after(BlockRole::Footnote);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Biblio: {
      float start_y = y;
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      y += after(BlockRole::List);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::BiblioEntry: {
      float start_y = y;
      // Hanging indent style for references
      y += before(BlockRole::BiblioEntry);
      const float hang = indent(BlockRole::BiblioEntry);
      layout_role_text(doc, node, BlockRole::BiblioEntry, x + hang, y,
                       max_width - hang, out);
      y += after(BlockRole::BiblioEntry);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Table: {
      y += before(BlockRole::Table);
      float start_y = y;
      std::vector<NodeId> rows;
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::TableRow) rows.push_back(c);
      }
      if (rows.empty()) {
        for (NodeId c : node.children)
          layout_node(doc, c, x, y, max_width, out);
      } else {
        // Column weights from the first row's cells (display_scale).
        // All rows share the same relative widths; dragging a separator
        // updates the first-row cell weights (and is mirrored on drag).
        std::vector<NodeId> weight_cells;
        for (NodeId c : doc.get(rows[0]).children) {
          if (doc.get(c).kind == NodeKind::TableCell) weight_cells.push_back(c);
        }
        size_t n_cols = weight_cells.size();
        for (NodeId rid : rows) {
          size_t cells = 0;
          for (NodeId c : doc.get(rid).children)
            if (doc.get(c).kind == NodeKind::TableCell) ++cells;
          if (cells > n_cols) n_cols = cells;
        }
        std::vector<float> weights(n_cols, 1.f);
        float weight_sum = 0.f;
        for (size_t i = 0; i < n_cols; ++i) {
          float w = 1.f;
          if (i < weight_cells.size()) {
            w = doc.get(weight_cells[i]).display_scale;
            if (w < 0.15f) w = 0.15f;
            if (w > 20.f) w = 20.f;
          }
          weights[i] = w;
          weight_sum += w;
        }
        if (weight_sum < 0.01f) weight_sum = static_cast<float>(n_cols);

        std::vector<float> col_widths(n_cols);
        for (size_t i = 0; i < n_cols; ++i) {
          col_widths[i] = max_width * (weights[i] / weight_sum);
          if (col_widths[i] < 32.f) col_widths[i] = 32.f;
        }

        std::vector<float> row_bottoms;
        for (NodeId rid : rows) {
          layout_node(doc, rid, x, y, max_width, out);
          row_bottoms.push_back(y);
        }

        const float stroke = 1.f;
        float table_h = y - start_y;
        if (table_h < 1.f) table_h = 1.f;
        out.table_lines.push_back({id, x, start_y, max_width, stroke});
        out.table_lines.push_back({id, x, y - stroke, max_width, stroke});
        out.table_lines.push_back({id, x, start_y, stroke, table_h});
        out.table_lines.push_back(
            {id, x + max_width - stroke, start_y, stroke, table_h});
        for (size_t r = 0; r + 1 < row_bottoms.size(); ++r) {
          float ly = row_bottoms[r] - stroke * 0.5f;
          out.table_lines.push_back({id, x, ly, max_width, stroke});
        }
        // Vertical separators + draggable gaps (same mechanism as Columns)
        if (n_cols > 1) {
          float cx = x;
          for (size_t c = 0; c + 1 < n_cols; ++c) {
            cx += col_widths[c];
            float lx = cx - stroke * 0.5f;
            out.table_lines.push_back({id, lx, start_y, stroke, table_h});
            if (c < weight_cells.size() && c + 1 < weight_cells.size()) {
              ColumnGap gap;
              gap.columns = id;  // table id (hit-test uses left/right cells)
              gap.left = weight_cells[c];
              gap.right = weight_cells[c + 1];
              gap.x = cx;
              gap.y = start_y;
              gap.height = table_h;
              gap.left_w = col_widths[c];
              gap.right_w = col_widths[c + 1];
              out.column_gaps.push_back(gap);
            }
          }
        }
      }
      y += after(BlockRole::Table);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::TableRow: {
      float start_y = y;
      std::vector<NodeId> cells;
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::TableCell) cells.push_back(c);
      }
      if (cells.empty()) {
        for (NodeId c : node.children)
          layout_node(doc, c, x, y, max_width, out);
      } else {
        // Weights: prefer first row of parent table (shared column model)
        std::vector<float> weights(cells.size(), 1.f);
        if (node.parent != INVALID_NODE &&
            doc.get(node.parent).kind == NodeKind::Table) {
          for (NodeId rid : doc.get(node.parent).children) {
            if (doc.get(rid).kind != NodeKind::TableRow) continue;
            size_t i = 0;
            for (NodeId c : doc.get(rid).children) {
              if (doc.get(c).kind != NodeKind::TableCell) continue;
              if (i < weights.size()) {
                float w = doc.get(c).display_scale;
                if (w < 0.15f) w = 0.15f;
                if (w > 20.f) w = 20.f;
                weights[i] = w;
              }
              ++i;
            }
            break;  // first row only
          }
        } else {
          for (size_t i = 0; i < cells.size(); ++i) {
            float w = doc.get(cells[i]).display_scale;
            if (w < 0.15f) w = 0.15f;
            if (w > 20.f) w = 20.f;
            weights[i] = w;
          }
        }
        float weight_sum = 0.f;
        for (float w : weights) weight_sum += w;
        if (weight_sum < 0.01f) weight_sum = static_cast<float>(cells.size());

        float max_bottom = start_y;
        const float cell_pad = 4.f;
        float cx = x;
        for (size_t i = 0; i < cells.size(); ++i) {
          float col_w = max_width * (weights[i] / weight_sum);
          if (col_w < 32.f) col_w = 32.f;
          float cy = start_y + cell_pad;
          LayoutResult cell_out;
          layout_node(doc, cells[i], cx + cell_pad, cy, col_w - cell_pad * 2.f,
                      cell_out);
          append_result(out, cell_out);
          if (cy > max_bottom) max_bottom = cy;
          cx += col_w;
        }
        y = max_bottom + cell_pad + 2.f;
      }
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::TableCell: {
      float start_y = y;
      // Nested block cells: when the cell has block children, lay those out
      // instead of (or after) the cell's own inline spans.
      bool has_blocks = false;
      for (NodeId c : node.children) {
        const NodeKind ck = doc.get(c).kind;
        if (ck != NodeKind::Document && ck != NodeKind::Column) {
          has_blocks = true;
          break;
        }
      }
      if (has_blocks) {
        // Optional leading inlines become a synthetic first paragraph area
        if (!node.spans.empty()) {
          FontRole role =
              (node.level > 0) ? FontRole::BodyBold : FontRole::Body;
          layout_paragraph(doc, node, x, y, max_width, out, role,
                           BlockRole::Table);
          y += 2.f;
        }
        for (NodeId c : node.children)
          layout_node(doc, c, x, y, max_width, out);
        y += 2.f;
      } else {
        FontRole role =
            (node.level > 0) ? FontRole::BodyBold : FontRole::Body;
        layout_paragraph(doc, node, x, y, max_width, out, role,
                         BlockRole::Table);
        y += 4.f;
      }
      if (node.level > 0) {
        // Header fill covers the laid-out cell content area.
        out.table_header_fills.push_back(
            {id, x - 2.f, start_y - 2.f, max_width + 4.f, y - start_y + 2.f});
      }
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::ListItem: {
      y += before(BlockRole::ListItem);
      float start_y = y;
      const float bullet_slot = indent(BlockRole::ListItem);
      // Marker: task checkbox, ordered "N.", or unordered "•"
      std::string marker;
      if (node.label == "task-done")
        marker = "☑";
      else if (node.label == "task")
        marker = "☐";
      else if (node.level > 0)
        marker = std::to_string(node.level) + ".";
      else
        marker = "•";
      std::vector<uint32_t> m_cps;
      decode_utf8(marker, m_cps);
      float mx = x;
      Font& marker_font = fonts_.pick(FontRole::Body);
      float baseline = y + static_cast<float>(marker_font.ascender());
      for (uint32_t cp : m_cps) {
        const Glyph& g = marker_font.glyph(cp);
        PositionedGlyph pg;
        pg.codepoint = cp;
        pg.x = mx + static_cast<float>(g.bearing_x);
        pg.y = baseline;
        pg.node = id;
        pg.span_index = 0;
        pg.char_index = 0;
        pg.kind = InlineKind::Text;
        pg.font = FontRole::Body;
        pg.color = st.text_color(BlockRole::ListItem, InlineKind::Text);
        pg.advance = mx + static_cast<float>(g.advance >> 6) - pg.x;
        out.glyphs.push_back(pg);
        mx += static_cast<float>(g.advance >> 6);
      }
      float text_x = x + bullet_slot;
      float text_w = max_width - bullet_slot;
      if (text_w < 40.f) {
        text_x = x + 16.f;
        text_w = max_width - 16.f;
      }
      layout_role_text(doc, node, BlockRole::ListItem, text_x, y, text_w, out);
      // Nested lists under this item
      for (NodeId child : node.children) {
        if (doc.get(child).kind == NodeKind::List) {
          layout_node(doc, child, x + bullet_slot, y,
                      max_width - bullet_slot, out);
        }
      }
      y += after(BlockRole::ListItem);  // tighter spacing between list items
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Image: {
      if (is_remote_image(node.src)) {
        layout_remote_image(doc, node, x, y, max_width, out);
        break;
      }
      float disp_w = max_width;
      float disp_h = 120.f;
      if (images_ && !node.src.empty()) {
        const Image& img = images_->get(node.src, image_base_);
        if (img.ok && img.width > 0) {
          float scale = 1.f;
          if (static_cast<float>(img.width) > max_width) {
            scale = max_width / static_cast<float>(img.width);
          }
          float max_h = 480.f;
          if (img.height * scale > max_h) {
            scale = max_h / static_cast<float>(img.height);
          }
          // User interactive scale on top of fit-to-column
          float user = node.display_scale > 0.05f ? node.display_scale : 1.f;
          scale *= user;
          // Keep within column
          if (img.width * scale > max_width) {
            scale = max_width / static_cast<float>(img.width);
          }
          disp_w = img.width * scale;
          disp_h = img.height * scale;
        }
      }
      out.images.push_back({id, x, y, disp_w, disp_h, node.src, image_base_});
      out.boxes.push_back({id, x, y, disp_w, disp_h});
      // Alt text under image if present
      if (!node.alt.empty()) {
        float y2 = y + disp_h + 4.f;
        Node fake = node;
        fake.kind = NodeKind::Paragraph;
        fake.spans.clear();
        InlineSpan s;
        s.kind = InlineKind::Emphasis;
        s.text = node.alt;
        fake.spans.push_back(std::move(s));
        layout_paragraph(doc, fake, x, y2, max_width, out, FontRole::Body);
        y = y2 + 10.f;
      } else {
        y += disp_h + 14.f;
      }
      break;
    }

    case NodeKind::HorizontalRule: {
      float start_y = y;
      y += before(BlockRole::Rule);
      const float thickness = 2.f;
      out.hr_lines.push_back({id, x, y, max_width, thickness});
      y += after(BlockRole::Rule) + thickness;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Transclude: {
      // Live embedding: resolve src as local #anchor or path.md[#anchor]
      // and layout a snapshot of the target's text (glyphs tagged with
      // this Transclude node). Cross-document paths are relative to the
      // host document directory (image_base_).
      y += before(BlockRole::Transclusion);
      float start_y = y;
      const float inset = indent(BlockRole::Transclusion);
      const float bar_w = 3.f;

      std::string path_part, href_part;
      split_include_src(node.src, path_part, href_part);

      const Document* src_doc = &doc;
      bool external = false;
      if (!path_part.empty()) {
        std::filesystem::path resolved = path_part;
        if (!image_base_.empty() && resolved.is_relative())
          resolved = std::filesystem::path(image_base_) / path_part;
        const Document* ext = load_external_doc(resolved.string());
        if (ext) {
          src_doc = ext;
          external = true;
        } else {
          src_doc = nullptr;
        }
      }

      NodeId target_id = INVALID_NODE;
      std::vector<InlineSpan> body_spans;
      if (src_doc) {
        if (!href_part.empty()) {
          target_id = transclude_text_node(
              *src_doc, find_anchor_node(*src_doc, href_part));
          if (target_id != INVALID_NODE)
            body_spans = src_doc->get(target_id).spans;
          // Range/excerpt: keep only the span(s) containing the phrase,
          // or trim text around the first match (semantic range, not bytes).
          if (!body_spans.empty() && !node.summary.empty()) {
            const std::string& ex = node.summary;
            std::vector<InlineSpan> filtered;
            for (const auto& sp : body_spans) {
              if (sp.text.find(ex) != std::string::npos) {
                InlineSpan cut = sp;
                // Prefer exposing the excerpt itself when the span is long
                size_t pos = sp.text.find(ex);
                if (sp.text.size() > ex.size() * 3 && pos != std::string::npos) {
                  size_t start = pos > 40 ? pos - 40 : 0;
                  size_t end = std::min(sp.text.size(), pos + ex.size() + 40);
                  cut.text = sp.text.substr(start, end - start);
                  if (start > 0) cut.text = "…" + cut.text;
                  if (end < sp.text.size()) cut.text += "…";
                }
                filtered.push_back(std::move(cut));
              }
            }
            if (!filtered.empty()) body_spans = std::move(filtered);
          }
        } else if (external) {
          // Whole-document include: concatenate first few text nodes.
          std::vector<NodeId> stack = {src_doc->root};
          int taken = 0;
          while (!stack.empty() && taken < 8) {
            NodeId cur = stack.back();
            stack.pop_back();
            if (cur == INVALID_NODE) continue;
            const Node& cn = src_doc->get(cur);
            if (cn.kind == NodeKind::Paragraph || cn.kind == NodeKind::Heading ||
                cn.kind == NodeKind::Header ||
                cn.kind == NodeKind::ListItem || cn.kind == NodeKind::BlockQuote) {
              if (!body_spans.empty()) {
                InlineSpan gap;
                gap.kind = InlineKind::Text;
                gap.text = " ";
                body_spans.push_back(std::move(gap));
              }
              for (const auto& sp : cn.spans) body_spans.push_back(sp);
              ++taken;
            }
            for (auto it = cn.children.rbegin(); it != cn.children.rend(); ++it)
              stack.push_back(*it);
          }
        }
      }

      std::string label_href = node.src.empty() ? std::string{"(no target)"}
                                                : node.src;

      // Label line (always tagged with the Transclude node so clicks can
      // follow the include and structure highlight marks the embed).
      {
        Node label;
        label.id = id;
        label.kind = NodeKind::Paragraph;
        InlineSpan s;
        s.kind = InlineKind::Emphasis;
        s.text = "↗ ";
        s.text += label_href;
        label.spans.push_back(std::move(s));
        layout_role_text(doc, label, BlockRole::Transclusion, x + bar_w + inset,
                         y, max_width - bar_w - inset, out);
        y += 4.f;
      }

      // Body: copy of target spans, or a missing marker.
      // Same-document includes tag glyphs with the *source* node id so
      // caret/edit hit-tests write through to the live passage. External
      // includes stay tagged with the Transclude node (read-only mirror).
      {
        Node body;
        body.kind = NodeKind::Paragraph;
        const bool local_writable =
            !external && target_id != INVALID_NODE && src_doc == &doc;
        body.id = local_writable ? target_id : id;
        if (!body_spans.empty()) {
          body.spans = std::move(body_spans);
        } else {
          InlineSpan s;
          s.kind = InlineKind::Emphasis;
          if (!src_doc && !path_part.empty())
            s.text = "[missing file: " + path_part + "]";
          else
            s.text = "[missing transclusion]";
          body.spans.push_back(std::move(s));
          body.id = id;  // missing marker is not writable
        }
        layout_role_text(doc, body, BlockRole::Transclusion, x + bar_w + inset,
                         y, max_width - bar_w - inset, out);
      }
      y += after(BlockRole::Transclusion);
      out.quote_bars.push_back({id, x, start_y, bar_w, y - start_y});
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    default:
      break;
  }
}

static void decode_utf8(const std::string& text,
                        std::vector<uint32_t>& out) {
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp = 0;
    if (c < 0x80) {
      cp = c;
      ++i;
    } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
      cp = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f);
      i += 2;
    } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
      cp = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      i += 3;
    } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
      cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
           ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
      i += 4;
    } else {
      ++i;
      continue;
    }
    out.push_back(cp);
  }
}

void LayoutEngine::layout_aside(const Document& doc, const Node& node,
                                float x, float& y, float max_width,
                                LayoutResult& out) {
  const BlockStyle& bs = (*style_)[BlockRole::Aside];
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  const float pad = bs.indent.px(em, dp);
  const float gap = em;
  FloatSide side = bs.side;
  if (node.src == "left") side = FloatSide::Left;
  else if (node.src == "right") side = FloatSide::Right;
  if (bs.placement == Placement::Margin && side != FloatSide::None &&
      !in_margin_ && layout_margin_box(doc, node, BlockRole::Aside, side, {},
                                       y, out))
    return;
  // In the column: small text and the aside colour all the same.
  const bool was_small = small_;
  const BlockRole was_ctx = ctx_role_;
  small_ = is_small_role(bs.font);
  ctx_role_ = BlockRole::Aside;
  struct Restore {
    bool& s; BlockRole& c; bool sv; BlockRole cv;
    ~Restore() { s = sv; c = cv; }
  } restore{small_, ctx_role_, was_small, was_ctx};
  float want = bs.width.px(em, dp);
  if (want < 1.f) want = 16.f * em;
  const float aw = std::min(want, max_width * 0.45f);
  // Float only while it stays a box and the text beside it stays readable.
  const bool floating = bs.placement == Placement::Float &&
                        side != FloatSide::None && !in_margin_ &&
                        aw >= 6.f * em && max_width - aw - gap >= 10.f * em;

  auto frame = [&](float bx, float top, float w, float bottom) {
    const LayoutBox box{node.id, bx, top, w, bottom - top};
    if (bs.background.visible()) out.role_fills.push_back({box, BlockRole::Aside});
    if (bs.border.visible()) out.role_frames.push_back({box, BlockRole::Aside});
  };

  if (!floating) {
    y = clear_floats(x, max_width, y);
    y += bs.space_before.px(em, dp);
    const float top = y;
    const size_t fill_at = out.role_fills.size();
    float iy = top + pad;
    layout_children_sequential(doc, node.children, x + pad, iy,
                               std::max(40.f, max_width - 2.f * pad), out);
    const float bottom = iy + pad;
    frame(x, top, max_width, bottom);
    // The fill goes under the children laid out before it.
    if (bs.background.visible())
      std::rotate(out.role_fills.begin() + static_cast<std::ptrdiff_t>(fill_at),
                  out.role_fills.end() - 1, out.role_fills.end());
    out.boxes.push_back({node.id, x, top, max_width, bottom - top});
    y = bottom + bs.space_after.px(em, dp);
    return;
  }

  float top = y + bs.space_before.px(em, dp);
  const float bx = side == FloatSide::Right ? x + max_width - aw : x;
  // Below earlier boxes on the same spot (stacked asides).
  for (bool moved = true; moved;) {
    moved = false;
    for (const FloatBox& f : floats_) {
      if (f.top <= top && f.bottom > top && f.right > bx && f.left < bx + aw) {
        top = f.bottom;
        moved = true;
      }
    }
  }
  const size_t fill_at = out.role_fills.size();
  float iy = top + pad;
  layout_children_sequential(doc, node.children, bx + pad, iy,
                             std::max(20.f, aw - 2.f * pad), out);
  // Trailing gap of the last child is padding enough.
  const float bottom = iy + pad * 0.5f;
  frame(bx, top, aw, bottom);
  if (bs.background.visible())
    std::rotate(out.role_fills.begin() + static_cast<std::ptrdiff_t>(fill_at),
                out.role_fills.end() - 1, out.role_fills.end());
  FloatBox fb;
  fb.top = top;
  fb.bottom = bottom + bs.space_after.px(em, dp);
  fb.side = side;
  fb.left = side == FloatSide::Right ? bx - gap : bx;
  fb.right = side == FloatSide::Right ? bx + aw : bx + aw + gap;
  floats_.push_back(fb);
  out.boxes.push_back({node.id, bx, top, aw, bottom - top});
  // y stays: the following blocks run beside the box.
}

// Margin room for the side boxes of `doc`: the margin as the view settings
// leave it, widened at the cost of the text column (down to kMinText) when
// a side has boxes and is too narrow for them.
void LayoutEngine::setup_margins(const Document& doc, float& mx,
                                 float& content_w, float max_width) {
  margins_[0] = margins_[1] = MarginArea{};
  in_margin_ = 0;
  small_ = false;
  ctx_role_ = BlockRole::Count;
  const DocStyle& st = *style_;
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  float want[2] = {0.f, 0.f};
  auto need = [&](BlockRole role, FloatSide side) {
    const BlockStyle& bs = st[role];
    if (bs.placement != Placement::Margin || side == FloatSide::None) return;
    float w = bs.width.px(em, dp);
    if (w < 1.f) w = 14.f * em;
    float& slot = want[side == FloatSide::Left ? 0 : 1];
    slot = std::max(slot, w);
  };
  for (const Node& n : doc.nodes) {
    if (n.parent == INVALID_NODE) continue;  // unhooked arena entries
    if (n.kind == NodeKind::Aside) {
      FloatSide side = st[BlockRole::Aside].side;
      if (n.src == "left") side = FloatSide::Left;
      else if (n.src == "right") side = FloatSide::Right;
      need(BlockRole::Aside, side);
    }
  }
  if (want[0] <= 0.f && want[1] <= 0.f) return;
  const float gap = 1.25f * em;  // margin box ↔ text column
  const float edge = std::max(options_.margin_x, 0.5f * em);
  const float min_text = 28.f * em;  // the column never gets narrower
  const float min_box = 8.f * em;    // a margin box never gets narrower
  for (int s = 0; s < 2; ++s) {
    if (want[s] <= 0.f) continue;
    const float free = s == 0 ? mx - edge - gap
                              : max_width - (mx + content_w) - edge - gap;
    float w = free;
    if (free < want[s]) {
      const float take =
          std::min(want[s] - free, std::max(0.f, content_w - min_text));
      w = free + take;
      if (w < min_box) continue;  // no room: these boxes go into the flow
      content_w -= take;
      if (s == 0) mx += take;
    }
    w = std::min(w, want[s]);
    MarginArea& m = margins_[s];
    m.w = w;
    m.x = s == 0 ? mx - gap - w : mx + content_w + gap;
  }
}

bool LayoutEngine::layout_margin_box(const Document& doc, const Node& node,
                                     BlockRole role, FloatSide side,
                                     const std::string& label, float y,
                                     LayoutResult& out) {
  MarginArea& m = margins_[side == FloatSide::Left ? 0 : 1];
  if (m.w <= 0.f) return false;
  const BlockStyle& bs = (*style_)[role];
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  const float pad = bs.indent.px(em, dp);
  // Beside its flow position, below the boxes already in this margin.
  const float top = std::max(y + bs.space_before.px(em, dp), m.bottom);

  const bool was_small = small_;
  const BlockRole was_ctx = ctx_role_;
  small_ = is_small_role(bs.font);
  ctx_role_ = role;
  ++in_margin_;
  const size_t fill_at = out.role_fills.size();
  const float ix = m.x + pad;
  const float iw = std::max(20.f, m.w - 2.f * pad);
  float iy = top + pad;
  if (!label.empty()) {
    // Decoration: no node, so it is neither text to edit nor a link.
    Node head;
    head.id = INVALID_NODE;
    head.kind = NodeKind::Paragraph;
    InlineSpan sp;
    sp.kind = InlineKind::Strong;
    sp.text = label;
    head.spans.push_back(std::move(sp));
    layout_paragraph(doc, head, ix, iy, iw, out, FontRole::Body, role);
    iy += 0.3f * em;
  }
  layout_children_sequential(doc, node.children, ix, iy, iw, out);
  --in_margin_;
  small_ = was_small;
  ctx_role_ = was_ctx;
  // The padding replaces the trailing gap of a last paragraph.
  if (!node.children.empty() && node.children.back() != INVALID_NODE &&
      doc.get(node.children.back()).kind == NodeKind::Paragraph)
    iy -= std::min((*style_)[BlockRole::Paragraph].space_after.px(em, dp),
                   iy - top);
  const float bottom = iy + pad;
  const LayoutBox box{node.id, m.x, top, m.w, bottom - top};
  if (bs.background.visible()) {
    out.role_fills.push_back({box, role});
    // Under the text laid out before it.
    std::rotate(out.role_fills.begin() + static_cast<std::ptrdiff_t>(fill_at),
                out.role_fills.end() - 1, out.role_fills.end());
  }
  if (bs.border.visible()) {
    if (bs.background.visible()) {
      out.role_frames.push_back({box, role});
    } else {
      // A bar on the side facing the text.
      const float bar = std::max(2.f, 2.f * dp);
      const float bx = side == FloatSide::Left ? m.x + m.w - bar : m.x;
      out.role_rules.push_back({{node.id, bx, top, bar, bottom - top}, role});
    }
  }
  out.boxes.push_back(box);
  m.bottom = bottom + bs.space_after.px(em, dp);
  return true;
}

void LayoutEngine::inline_image_size(const InlineSpan& sp, float max_w,
                                     float& w, float& h, bool& remote) {
  const float dp = options_.px_scale;
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  remote = is_remote_image(sp.src);
  float nat_w = 0.f, nat_h = 0.f;  // natural size in CSS px
  if (images_ && !sp.src.empty()) {
    const Image& img = remote
                           ? images_->thumb(sp.src, image_base_, kRemoteThumbEdge)
                           : images_->get(sp.src, image_base_);
    if (img.ok && img.width > 0 && img.height > 0) {
      nat_w = static_cast<float>(img.width);
      nat_h = static_cast<float>(img.height);
    }
  }
  if (sp.width > 0 && sp.height > 0) {
    w = sp.width * dp;
    h = sp.height * dp;
  } else if (sp.width > 0 || sp.height > 0) {
    const float aspect = nat_w > 0.f ? nat_h / nat_w : 1.f;
    w = sp.width > 0 ? sp.width * dp : sp.height * dp / aspect;
    h = sp.height > 0 ? sp.height * dp : sp.width * dp * aspect;
  } else if (nat_w > 0.f) {
    w = nat_w * dp;
    h = nat_h * dp;
  } else {
    w = h = em;  // pending / missing: a box the size of a letter
  }
  if (w > max_w && w > 0.f) {
    h *= max_w / w;
    w = max_w;
  }
  w = std::max(1.f, w);
  h = std::max(1.f, h);
}

void LayoutEngine::layout_chrome_notice(const Document& doc, float x,
                                        float& y, float max_width,
                                        LayoutResult& out) {
  const std::string what = chrome_hidden_text(doc, chrome_regions(doc));
  if (what.empty()) return;
  // Laid out as the root's text, then made decoration: not editable, and
  // the click goes through out.chrome_notice.
  Node notice;
  notice.id = doc.root;
  notice.kind = NodeKind::Paragraph;
  InlineSpan lead;
  lead.kind = InlineKind::Emphasis;
  lead.text = "Reader mode: " + what + " · ";
  notice.spans.push_back(std::move(lead));
  InlineSpan show;
  show.kind = InlineKind::Link;
  show.text = "Show all";
  notice.spans.push_back(std::move(show));
  const DocStyle& st = *style_;
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  const float start_y = y;
  y += st[BlockRole::Paragraph].space_before.px(em, dp);
  const size_t first = out.glyphs.size();
  layout_role_text(doc, notice, BlockRole::Paragraph, x, y, max_width, out);
  for (size_t i = first; i < out.glyphs.size(); ++i)
    if (out.glyphs[i].node == doc.root) out.glyphs[i].node = INVALID_NODE;
  y += st[BlockRole::Paragraph].space_after.px(em, dp);
  out.chrome_notice = {INVALID_NODE, x, start_y, max_width, y - start_y};
}

std::shared_ptr<const MathBox> LayoutEngine::math_box(const InlineSpan& sp) {
  MathFont* face = fonts_.math_font();
  if (!face) return nullptr;
  const std::string key = (sp.display ? "D" : "T") + sp.src;
  auto it = math_cache_.find(key);
  if (it != math_cache_.end()) return it->second;
  const MathParse parsed = parse_math(sp.src);
  std::shared_ptr<const MathBox> box;
  // Unknown commands stay visible as text inside the formula; only a source
  // that does not parse at all falls back to showing the TeX.
  if (parsed.error.empty() || !parsed.root.kids.empty())
    box = std::make_shared<MathBox>(layout_math(parsed.root, *face, sp.display));
  if (math_cache_.size() > 2000) math_cache_.clear();
  math_cache_[key] = box;
  return box;
}

void LayoutEngine::layout_role_text(const Document& doc, const Node& node,
                                    BlockRole role, float x, float& y,
                                    float max_width, LayoutResult& out) {
  layout_paragraph(doc, node, x, y, max_width, out, (*style_)[role].font, role);
}

void LayoutEngine::add_role_band(BlockRole role, NodeId id, float x,
                                 float top, float width, float bottom,
                                 LayoutResult& out) {
  const BlockStyle& bs = (*style_)[role];
  const float em = static_cast<float>(fonts_.regular.pixel_size());
  const float dp = options_.px_scale;
  // The band reaches a little past the text so it does not touch the glyphs.
  const float pad_x = 0.35f * em;
  const float pad_y = 0.2f * em;
  if (bs.background.visible())
    out.role_fills.push_back({{id, x - pad_x, top - pad_y, width + 2.f * pad_x,
                               bottom - top + 2.f * pad_y},
                              role});
  if (bs.border.visible())
    out.role_rules.push_back(
        {{id, x, bottom + pad_y, width, std::max(1.f, dp)}, role});
}

void LayoutEngine::layout_paragraph(const Document& doc, const Node& node,
                                    float x, float& y, float max_width,
                                    LayoutResult& out, FontRole base_role,
                                    BlockRole block) {
  (void)doc;
  if (small_) base_role = small_role(base_role);
  // Text inside a side box takes the box's colour unless its own block or
  // inline role has one.
  const bool ctx_color = ctx_role_ != BlockRole::Count &&
                         (*style_)[ctx_role_].has_color &&
                         !(*style_)[block].has_color;
  struct CharInfo {
    uint32_t cp;
    size_t span_idx;
    size_t char_idx;
    InlineKind kind;
    FontRole role;
    float rise = 0.f;  // baseline lift (footnote marks without superscripts)
    float img_w = 0.f;  // inline image: an atom of this size (else 0)
    float img_h = 0.f;
    bool remote = false;
    // A formula (also an atom): `img_h` above the baseline, `img_d` below.
    float img_d = 0.f;
    float math_px = 0.f;
    bool display = false;
    std::shared_ptr<const MathBox> math = nullptr;
  };
  std::vector<CharInfo> chars;
  const bool small_base = is_small_role(base_role);
  for (size_t si = 0; si < node.spans.size(); ++si) {
    const InlineSpan& span = node.spans[si];
    std::vector<uint32_t> cps;
    decode_utf8(span.text, cps);
    const InlineKind kind = span.kind;
    FontRole r = style_role(base_role, kind);
    const InlineStyle& inl = (*style_)[kind];
    if (inl.has_font) r = inl.font;
    if (span.role == InlineRole::Metadata && style_->metadata.has_font)
      r = style_->metadata.font;
    if (kind == InlineKind::Math) {
      // One atom set from the font's math table; without a math font (or a
      // formula that does not parse) the TeX source is shown as code.
      const InlineSpan& sp = node.spans[si];
      const float em = static_cast<float>(fonts_.pick(r).pixel_size());
      if (auto box = math_box(sp)) {
        CharInfo c{kInlineImageCp, si, 0, kind, r};
        c.img_w = std::max(1.f, box->width * em);
        c.img_h = box->ascent * em;
        c.img_d = box->depth * em;
        c.math_px = em;
        c.display = sp.display;
        c.math = std::move(box);
        chars.push_back(std::move(c));
      } else {
        const FontRole code = small_base ? FontRole::SmallCode : FontRole::Code;
        std::vector<uint32_t> src;
        decode_utf8(sp.src, src);
        for (uint32_t cp : src) chars.push_back(CharInfo{cp, si, 0, kind, code});
      }
      continue;
    }
    for (size_t ci = 0; ci < cps.size(); ++ci) {
      CharInfo c{cps[ci], si, ci, kind, r};
      if (kind == InlineKind::FootnoteRef) {
        // Footnote mark: superscript digit when the face has one (same
        // char index, so caret / selection are unaffected), else raised.
        Font& f = fonts_.pick(r);
        const uint32_t sup = superscript_digit(c.cp);
        if (sup && f.has_glyph(sup))
          c.cp = sup;
        else
          c.rise = static_cast<float>(f.ascender()) * 0.4f;
      } else if (kind == InlineKind::Image && c.cp == kInlineImageCp) {
        inline_image_size(node.spans[si], max_width, c.img_w, c.img_h,
                          c.remote);
      }
      chars.push_back(c);
    }
  }

  Font& metrics = fonts_.pick(base_role == FontRole::Heading ? FontRole::Heading
                              : small_base                  ? FontRole::Small
                                                            : FontRole::Body);
  if (chars.empty()) {
    y += metrics.line_height();
    return;
  }

  float baseline = y + metrics.ascender();
  // The current line: left edge and width, narrowed beside floating boxes.
  float lx0 = x;
  float lw = max_width;
  auto place_line = [&]() {
    lx0 = x;
    lw = max_width;
    if (floats_.empty()) return;
    const float top = baseline - metrics.ascender();
    float_band(top, top + metrics.line_height(), lx0, lw);
  };
  place_line();

  auto char_adv = [&](const CharInfo& c) -> float {
    if (c.img_w > 0.f) return c.img_w;
    return static_cast<float>(fonts_.pick(c.role).glyph(c.cp).advance >> 6);
  };

  // Emit one visual line: chars[lo, hi). last_or_hard → no justify.
  auto emit_line = [&](size_t lo, size_t hi, bool last_or_hard) {
    // Trim trailing spaces from the measured/justified run.
    size_t content_hi = hi;
    while (content_hi > lo && chars[content_hi - 1].cp == ' ') --content_hi;

    float natural = 0.f;
    int gaps = 0;
    bool seen_nonspace = false;
    bool in_space = false;
    float img_up = 0.f;  // inline images taller than the ascender lower the line
    float img_down = 0.f;  // formulas deeper than the descender: extra below
    float space_adv =
        static_cast<float>(metrics.glyph(' ').advance >> 6);
    if (space_adv < 1.f) space_adv = 4.f;
    for (size_t i = lo; i < content_hi; ++i) {
      natural += char_adv(chars[i]);
      if (chars[i].img_h > 0.f)
        img_up = std::max(img_up, chars[i].img_h -
                                      static_cast<float>(metrics.ascender()));
      if (chars[i].img_d > 0.f)
        img_down = std::max(img_down,
                            chars[i].img_d + static_cast<float>(metrics.descender()));
      if (chars[i].cp == ' ') {
        if (seen_nonspace) in_space = true;
      } else {
        if (in_space) ++gaps;
        in_space = false;
        seen_nonspace = true;
      }
    }
    baseline += img_up;

    float gap_extra = 0.f;
    const bool can_justify =
        options_.justify && !last_or_hard && gaps > 0 &&
        base_role != FontRole::Code && base_role != FontRole::SmallCode &&
        base_role != FontRole::Heading;
    if (can_justify) {
      float room = lw - natural;
      if (room > 0.5f) {
        gap_extra = room / static_cast<float>(gaps);
        const float cap = space_adv * 3.f;
        if (gap_extra > cap) gap_extra = cap;
      }
    }

    float lx = lx0;
    // A display formula alone on its line is centred.
    if (content_hi == lo + 1 && chars[lo].display && natural < lw)
      lx += (lw - natural) * 0.5f;
    bool after_space_run = false;
    bool had_nonspace = false;
    for (size_t i = lo; i < hi; ++i) {
      const CharInfo& c = chars[i];
      if (i < content_hi) {
        if (c.cp == ' ') {
          if (had_nonspace) after_space_run = true;
        } else {
          if (after_space_run && gap_extra > 0.f) {
            lx += gap_extra;
            after_space_run = false;
          }
          had_nonspace = true;
        }
      }
      PositionedGlyph pg;
      pg.codepoint = c.cp;
      pg.y = baseline - c.rise;
      pg.node = node.id;
      pg.span_index = c.span_idx;
      pg.char_index = c.char_idx;
      pg.kind = c.kind;
      pg.font = c.role;
      const InlineRole semantic = node.spans[c.span_idx].role;
      pg.color = ctx_color && !(*style_)[c.kind].has_color &&
                             !(semantic == InlineRole::Metadata &&
                               style_->metadata.has_color)
                     ? (*style_)[ctx_role_].color
                     : style_->text_color(block, c.kind, semantic);
      if (c.math) {
        // Formula: drawn from its box at the baseline; the glyph is its
        // caret slot.
        pg.x = lx;
        pg.advance = c.img_w;
        PositionedMath pm;
        pm.node = node.id;
        pm.span_index = c.span_idx;
        pm.x = lx;
        pm.y = baseline;
        pm.px = c.math_px;
        pm.display = c.display;
        pm.box = c.math;
        pm.color = pg.color;
        out.maths.push_back(std::move(pm));
        out.glyphs.push_back(pg);
        lx += c.img_w;
        continue;
      }
      if (c.img_w > 0.f) {
        // Inline image: sits on the baseline; the glyph is its caret slot.
        pg.x = lx;
        pg.advance = c.img_w;
        PositionedImage im{node.id, lx, baseline - c.img_h, c.img_w, c.img_h,
                           node.spans[c.span_idx].src, image_base_};
        if (c.remote) im.thumb = kRemoteThumbEdge;
        im.in_text = true;
        im.span_index = c.span_idx;
        out.images.push_back(std::move(im));
        out.glyphs.push_back(pg);
        lx += c.img_w;
        continue;
      }
      Font& font = fonts_.pick(c.role);
      const Glyph& g = font.glyph(c.cp);
      const float adv = static_cast<float>(g.advance >> 6);
      pg.x = lx + static_cast<float>(g.bearing_x);
      pg.advance = lx + adv - pg.x;
      out.glyphs.push_back(pg);
      lx += adv;
    }
    baseline += metrics.line_height() * options_.line_spacing + img_down;
  };

  // Word-wrap: build runs separated by spaces / hard breaks.
  size_t line_lo = 0;       // start of current line
  size_t word_lo = 0;       // start of current word
  float line_w = 0.f;       // width of committed content on this line
  float word_w = 0.f;       // width of current word (no leading spaces)
  bool in_word = false;

  auto flush_line = [&](size_t line_hi, bool hard) {
    if (line_hi > line_lo) {
      emit_line(line_lo, line_hi, hard);
      place_line();
    }
    line_lo = line_hi;
    line_w = 0.f;
  };
  // Commit the word in progress: it moves to a new line when it does not fit.
  auto end_word = [&]() {
    if (!in_word) return;
    if (line_w > 0.f && line_w + word_w > lw && word_w <= lw)
      flush_line(word_lo, false);
    line_w += word_w;
    in_word = false;
    word_w = 0.f;
  };

  for (size_t i = 0; i < chars.size(); ++i) {
    const uint32_t cp = chars[i].cp;

    if (cp == '\n' || cp == '\r') {
      // Commit any partial word, then hard-break.
      end_word();
      flush_line(i, true);
      // Placeholder so caret can sit on the newline index.
      {
        PositionedGlyph pg;
        pg.codepoint = ' ';
        pg.x = lx0;
        pg.y = baseline;
        pg.node = node.id;
        pg.span_index = chars[i].span_idx;
        pg.char_index = chars[i].char_idx;
        pg.kind = chars[i].kind;
        pg.font = chars[i].role;
        pg.color = style_->text_color(
            block, chars[i].kind, node.spans[chars[i].span_idx].role);
        out.glyphs.push_back(pg);
      }
      if (cp == '\r' && i + 1 < chars.size() && chars[i + 1].cp == '\n') ++i;
      line_lo = i + 1;
      word_lo = line_lo;
      line_w = 0.f;
      word_w = 0.f;
      in_word = false;
      continue;
    }

    float adv = char_adv(chars[i]);

    if (chars[i].img_w > 0.f) {
      // An inline image is a word of its own: breaks before and after it.
      end_word();
      if (line_w > 0.f && line_w + adv > lw) flush_line(i, false);
      line_w += adv;
      word_lo = i + 1;
      continue;
    }

    if (cp == ' ') {
      if (in_word) {
        // Finish word: does it fit on this line?
        if (line_w > 0.f && line_w + word_w > lw && word_w <= lw) {
          flush_line(word_lo, false);
        }
        // If the word alone is wider than the measure, emit it on its own
        // line (may overflow slightly; rare for Latin body text).
        if (line_w == 0.f && word_w > lw) {
          flush_line(i, false);  // emit the overlong word
          word_lo = i;
          word_w = 0.f;
          in_word = false;
          // fall through to treat this space as leading on the new line
        } else {
          line_w += word_w;
          in_word = false;
          word_w = 0.f;
        }
      }
      // Space: only keep it if it still fits; otherwise start a new line
      // (space is discarded as trailing / not carried).
      if (line_w > 0.f && line_w + adv > lw) {
        // The space ends this line (trimmed there), never starts the next.
        flush_line(i + 1, false);
        word_lo = i + 1;
        continue;
      }
      if (line_w > 0.f) {
        line_w += adv;  // keep inter-word space
      } else {
        // Leading space on an empty line — skip (word_lo advances)
        word_lo = i + 1;
      }
      continue;
    }

    // Non-space: accumulate into the current word.
    if (!in_word) {
      in_word = true;
      word_lo = i;
      word_w = 0.f;
    }
    word_w += adv;
  }

  // Tail: last word + line.
  end_word();
  if (line_lo < chars.size()) flush_line(chars.size(), true);

  y = baseline - metrics.ascender() + metrics.line_height() * 0.2f;
}

}  // namespace xanatoo
