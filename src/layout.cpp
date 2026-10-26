// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/layout.hpp"
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

void LayoutEngine::layout_children_sequential(
    const Document& doc, const std::vector<NodeId>& children, float x, float& y,
    float max_width, LayoutResult& out) {
  for (NodeId child : children) {
    if (try_skip_block(child, x, y, max_width, out)) continue;
    layout_node(doc, child, x, y, max_width, out);
  }
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
  float mx = options_.margin_x;
  float y = options_.margin_y;
  float content_w = std::max(40.f, max_width - 2.f * mx);
  if (options_.max_content_width > 0.f &&
      content_w > options_.max_content_width) {
    const float extra = content_w - options_.max_content_width;
    mx += extra * 0.5f;
    content_w = options_.max_content_width;
  }
  layout_node(doc, doc.root, mx, y, content_w, result);
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
  auto before = [&](BlockRole r) { return st[r].space_before.px(em); };
  auto after = [&](BlockRole r) { return st[r].space_after.px(em); };
  auto indent = [&](BlockRole r) { return st[r].indent.px(em); };

  switch (node.kind) {
    case NodeKind::Document:
      if (focus_section_ != INVALID_NODE) {
        // Only section tree on the path to the focus; skip preamble blocks.
        for (NodeId child : node.children) {
          const Node& ch = doc.get(child);
          if (ch.kind == NodeKind::Section || ch.kind == NodeKind::Article)
            layout_node(doc, child, x, y, max_width, out);
        }
      } else {
        layout_children_sequential(doc, node.children, x, y, max_width, out);
      }
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

        out.glyphs.insert(out.glyphs.end(),
                          col_out.glyphs.begin(), col_out.glyphs.end());
        out.boxes.insert(out.boxes.end(),
                         col_out.boxes.begin(), col_out.boxes.end());
        out.images.insert(out.images.end(),
                          col_out.images.begin(), col_out.images.end());
        out.quote_bars.insert(out.quote_bars.end(),
                              col_out.quote_bars.begin(),
                              col_out.quote_bars.end());
        out.note_bars.insert(out.note_bars.end(),
                             col_out.note_bars.begin(),
                             col_out.note_bars.end());
        out.code_blocks.insert(out.code_blocks.end(),
                               col_out.code_blocks.begin(),
                               col_out.code_blocks.end());
        out.table_lines.insert(out.table_lines.end(),
                               col_out.table_lines.begin(),
                               col_out.table_lines.end());
        out.table_header_fills.insert(out.table_header_fills.end(),
                                      col_out.table_header_fills.begin(),
                                      col_out.table_header_fills.end());
        out.hr_lines.insert(out.hr_lines.end(),
                            col_out.hr_lines.begin(), col_out.hr_lines.end());
        out.column_gaps.insert(out.column_gaps.end(),
                               col_out.column_gaps.begin(),
                               col_out.column_gaps.end());

        if (cy > max_bottom) max_bottom = cy;

        if (i + 1 < cols.size()) {
          ColumnGap gap;
          gap.columns = id;
          gap.left = cols[i];
          gap.right = cols[i + 1];
          gap.x = cx + col_w + gutter * 0.5f;
          gap.y = start_y;
          gap.height = 0.f;
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
        break;
      }
      layout_children_sequential(doc, body, x + inset, y, max_width - inset, out);
      y += after(BlockRole::Section);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
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
      layout_role_text(doc, header, BlockRole::Thread, x, y, max_width, out);
      for (auto& g : out.glyphs) {
        if (g.node == header.id) g.node = id;
      }
      y += 8.f;
      layout_children_sequential(doc, node.children, x, y, max_width, out);
      y += after(BlockRole::Thread);
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Message: {
      y += before(BlockRole::Message);
      float start_y = y;
      // Indent by reply depth (follow in-reply-to chain via labels).
      int depth = 0;
      if (!node.alt.empty() && node.alt.rfind("about:", 0) != 0) {
        std::string cur = node.alt;
        for (int guard = 0; guard < 24; ++guard) {
          if (cur.empty()) break;
          std::string key = cur[0] == '#' ? cur.substr(1) : cur;
          NodeId parent_msg = INVALID_NODE;
          auto it = g_msg_label_map.find(key);
          if (it != g_msg_label_map.end()) parent_msg = it->second;
          if (parent_msg == INVALID_NODE) break;
          ++depth;
          cur = doc.get(parent_msg).alt;
          if (cur.rfind("about:", 0) == 0) break;
        }
      }
      if (depth > 8) depth = 8;
      const float indent = 16.f * static_cast<float>(depth);
      const float bar_w = 3.f;
      float body_x = x + indent + bar_w + 10.f;
      float body_w = max_width - indent - bar_w - 10.f;
      if (body_w < 48.f) {
        body_x = x + 8.f;
        body_w = max_width - 8.f;
      }

      // Header: author · date · reply/about
      Node header = node;
      header.kind = NodeKind::Paragraph;
      header.spans.clear();
      {
        InlineSpan a;
        a.kind = InlineKind::Strong;
        a.text = node.src.empty() ? "(unknown)" : node.src;
        header.spans.push_back(std::move(a));
      }
      if (!node.summary.empty()) {
        InlineSpan d;
        d.kind = InlineKind::Text;
        d.text = "  ·  ";
        d.text += node.summary;
        header.spans.push_back(std::move(d));
      }
      if (!node.alt.empty()) {
        InlineSpan r;
        r.kind = InlineKind::Emphasis;
        if (node.alt.rfind("about:", 0) == 0) {
          r.text = "  ·  re ";
          r.text += node.alt.substr(6);
        } else {
          r.text = "  ·  reply to #";
          r.text += node.alt;
        }
        header.spans.push_back(std::move(r));
      }
      layout_role_text(doc, header, BlockRole::Message, body_x, y, body_w, out);
      for (auto& g : out.glyphs) {
        if (g.node == header.id) g.node = id;
      }
      y += 4.f;
      for (NodeId child : node.children)
        layout_node(doc, child, body_x, y, body_w, out);
      y += after(BlockRole::Message);
      out.message_bars.push_back(
          {id, x + indent, start_y, bar_w, y - start_y});
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
          out.glyphs.insert(out.glyphs.end(), cell_out.glyphs.begin(),
                            cell_out.glyphs.end());
          out.boxes.insert(out.boxes.end(), cell_out.boxes.begin(),
                           cell_out.boxes.end());
          out.images.insert(out.images.end(), cell_out.images.begin(),
                            cell_out.images.end());
          out.quote_bars.insert(out.quote_bars.end(),
                                cell_out.quote_bars.begin(),
                                cell_out.quote_bars.end());
          out.note_bars.insert(out.note_bars.end(),
                               cell_out.note_bars.begin(),
                               cell_out.note_bars.end());
          out.code_blocks.insert(out.code_blocks.end(),
                                 cell_out.code_blocks.begin(),
                                 cell_out.code_blocks.end());
          out.table_lines.insert(out.table_lines.end(),
                                 cell_out.table_lines.begin(),
                                 cell_out.table_lines.end());
          out.table_header_fills.insert(out.table_header_fills.end(),
                                        cell_out.table_header_fills.begin(),
                                        cell_out.table_header_fills.end());
          out.hr_lines.insert(out.hr_lines.end(), cell_out.hr_lines.begin(),
                              cell_out.hr_lines.end());
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
          target_id = find_anchor_node(*src_doc, href_part);
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

void LayoutEngine::layout_role_text(const Document& doc, const Node& node,
                                    BlockRole role, float x, float& y,
                                    float max_width, LayoutResult& out) {
  layout_paragraph(doc, node, x, y, max_width, out, (*style_)[role].font, role);
}

void LayoutEngine::layout_paragraph(const Document& doc, const Node& node,
                                    float x, float& y, float max_width,
                                    LayoutResult& out, FontRole base_role,
                                    BlockRole block) {
  (void)doc;
  struct CharInfo {
    uint32_t cp;
    size_t span_idx;
    size_t char_idx;
    InlineKind kind;
    FontRole role;
  };
  std::vector<CharInfo> chars;
  for (size_t si = 0; si < node.spans.size(); ++si) {
    std::vector<uint32_t> cps;
    decode_utf8(node.spans[si].text, cps);
    FontRole r = style_role(base_role, node.spans[si].kind);
    for (size_t ci = 0; ci < cps.size(); ++ci)
      chars.push_back({cps[ci], si, ci, node.spans[si].kind, r});
  }

  Font& metrics = fonts_.pick(base_role == FontRole::Heading ? FontRole::Heading
                                                             : FontRole::Body);
  if (chars.empty()) {
    y += metrics.line_height();
    return;
  }

  const float line_limit = x + max_width;  // exclusive right edge of measure
  float baseline = y + metrics.ascender();

  auto char_adv = [&](const CharInfo& c) -> float {
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
    float space_adv =
        static_cast<float>(metrics.glyph(' ').advance >> 6);
    if (space_adv < 1.f) space_adv = 4.f;
    for (size_t i = lo; i < content_hi; ++i) {
      natural += char_adv(chars[i]);
      if (chars[i].cp == ' ') {
        if (seen_nonspace) in_space = true;
      } else {
        if (in_space) ++gaps;
        in_space = false;
        seen_nonspace = true;
      }
    }

    float gap_extra = 0.f;
    const bool can_justify =
        options_.justify && !last_or_hard && gaps > 0 &&
        base_role != FontRole::Code && base_role != FontRole::Heading;
    if (can_justify) {
      float room = max_width - natural;
      if (room > 0.5f) {
        gap_extra = room / static_cast<float>(gaps);
        const float cap = space_adv * 3.f;
        if (gap_extra > cap) gap_extra = cap;
      }
    }

    float lx = x;
    bool after_space_run = false;
    bool had_nonspace = false;
    for (size_t i = lo; i < hi; ++i) {
      const CharInfo& c = chars[i];
      Font& font = fonts_.pick(c.role);
      const Glyph& g = font.glyph(c.cp);
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
      // Never place the pen past the measure for content glyphs.
      float adv = static_cast<float>(g.advance >> 6);
      if (c.cp != ' ' && lx + adv > line_limit + 0.01f && lx > x + 0.01f) {
        // Safety: should not happen if wrap is correct; still clamp.
        // (Do not skip the glyph — caret indices must stay complete.)
      }
      PositionedGlyph pg;
      pg.codepoint = c.cp;
      pg.x = lx + static_cast<float>(g.bearing_x);
      pg.y = baseline;
      pg.node = node.id;
      pg.span_index = c.span_idx;
      pg.char_index = c.char_idx;
      pg.kind = c.kind;
      pg.font = c.role;
      pg.color = style_->text_color(block, c.kind);
      out.glyphs.push_back(pg);
      lx += adv;
    }
    baseline += metrics.line_height() * options_.line_spacing;
  };

  // Word-wrap: build runs separated by spaces / hard breaks.
  size_t line_lo = 0;       // start of current line
  size_t word_lo = 0;       // start of current word
  float line_w = 0.f;       // width of committed content on this line
  float word_w = 0.f;       // width of current word (no leading spaces)
  bool in_word = false;

  auto flush_line = [&](size_t line_hi, bool hard) {
    if (line_hi > line_lo) emit_line(line_lo, line_hi, hard);
    line_lo = line_hi;
    line_w = 0.f;
  };

  for (size_t i = 0; i < chars.size(); ++i) {
    const uint32_t cp = chars[i].cp;

    if (cp == '\n' || cp == '\r') {
      // Commit any partial word, then hard-break.
      if (in_word) {
        // word is [word_lo, i)
        if (line_w > 0.f && line_w + word_w > max_width && word_w <= max_width) {
          flush_line(word_lo, false);
        }
        line_w += word_w;
        in_word = false;
        word_w = 0.f;
      }
      flush_line(i, true);
      // Placeholder so caret can sit on the newline index.
      {
        PositionedGlyph pg;
        pg.codepoint = ' ';
        pg.x = x;
        pg.y = baseline;
        pg.node = node.id;
        pg.span_index = chars[i].span_idx;
        pg.char_index = chars[i].char_idx;
        pg.kind = chars[i].kind;
        pg.font = chars[i].role;
        pg.color = style_->text_color(block, chars[i].kind);
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

    if (cp == ' ') {
      if (in_word) {
        // Finish word: does it fit on this line?
        if (line_w > 0.f && line_w + word_w > max_width && word_w <= max_width) {
          flush_line(word_lo, false);
        }
        // If the word alone is wider than the measure, emit it on its own
        // line (may overflow slightly; rare for Latin body text).
        if (line_w == 0.f && word_w > max_width) {
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
      if (line_w > 0.f && line_w + adv > max_width) {
        flush_line(i, false);
        word_lo = i + 1;
        // skip this space
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
  if (in_word) {
    if (line_w > 0.f && line_w + word_w > max_width && word_w <= max_width)
      flush_line(word_lo, false);
    line_w += word_w;
  }
  if (line_lo < chars.size()) flush_line(chars.size(), true);

  y = baseline - metrics.ascender() + metrics.line_height() * 0.2f;
}

}  // namespace xanatoo
