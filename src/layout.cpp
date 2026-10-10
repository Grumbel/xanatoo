// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/layout.hpp"

namespace glyphtoo {

LayoutEngine::LayoutEngine(FontSet& fonts, ImageCache* images)
    : fonts_(fonts), images_(images) {}

static FontRole style_role(FontRole base, InlineKind kind) {
  if (kind == InlineKind::Code) return FontRole::Code;
  if (base == FontRole::Heading) return FontRole::Heading;
  const bool strong = (kind == InlineKind::Strong);
  const bool emph = (kind == InlineKind::Emphasis);
  if (strong && emph) return FontRole::BodyBoldItalic;
  if (strong) return FontRole::BodyBold;
  if (emph) return FontRole::BodyItalic;
  return FontRole::Body;
}

static void decode_utf8(const std::string& text, std::vector<uint32_t>& out);

LayoutResult LayoutEngine::layout(const Document& doc, float max_width,
                                   const std::string& image_base) {
  LayoutResult result;
  image_base_ = image_base;
  if (doc.root == INVALID_NODE) return result;
  float y = 24.f;
  layout_node(doc, doc.root, 32.f, y, max_width - 64.f, result);
  result.content_height = y + 48.f;
  return result;
}

void LayoutEngine::layout_node(const Document& doc, NodeId id,
                               float x, float& y, float max_width,
                               LayoutResult& out) {
  const Node& node = doc.get(id);

  switch (node.kind) {
    case NodeKind::Document:
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      break;

    case NodeKind::List:
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      y += 8.f;  // trailing gap after the list block
      break;

    case NodeKind::Column:
      // Column content is laid out by the parent Columns node.
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
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
        if (col_w < 40.f) col_w = 40.f;
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
      layout_paragraph(doc, node, x, y, max_width, out, role);
      y += (node.level == 1) ? 14.f : 10.f;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Paragraph: {
      float start_y = y;
      layout_paragraph(doc, node, x, y, max_width, out, FontRole::Body);
      y += 14.f;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::BlockQuote: {
      float start_y = y;
      const float bar_w = 3.f;
      const float indent = 14.f;
      float text_x = x + bar_w + indent;
      float text_w = max_width - bar_w - indent;
      if (text_w < 40.f) {
        text_x = x + 8.f;
        text_w = max_width - 8.f;
      }
      layout_paragraph(doc, node, text_x, y, text_w, out, FontRole::Body);
      y += 12.f;
      out.quote_bars.push_back({id, x, start_y, bar_w, y - start_y});
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::ListItem: {
      float start_y = y;
      const float bullet_slot = 28.f;
      // Marker: "•" for unordered, "N." for ordered (level holds the number)
      std::string marker =
          (node.level > 0) ? (std::to_string(node.level) + ".") : "•";
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
        out.glyphs.push_back(pg);
        mx += static_cast<float>(g.advance >> 6);
      }
      float text_x = x + bullet_slot;
      float text_w = max_width - bullet_slot;
      if (text_w < 40.f) {
        text_x = x + 16.f;
        text_w = max_width - 16.f;
      }
      layout_paragraph(doc, node, text_x, y, text_w, out, FontRole::Body);
      // Nested lists under this item
      for (NodeId child : node.children) {
        if (doc.get(child).kind == NodeKind::List) {
          layout_node(doc, child, x + bullet_slot, y,
                      max_width - bullet_slot, out);
        }
      }
      y += 8.f;  // tighter spacing between list items
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
      y += 10.f;
      const float thickness = 2.f;
      out.hr_lines.push_back({id, x, y, max_width, thickness});
      y += 10.f + thickness;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Transclude: {
      // Live embedding: resolve src as #anchor and layout a snapshot of
      // the target's text (glyphs tagged with this Transclude node).
      float start_y = y;
      const float indent = 12.f;
      const float bar_w = 3.f;

      std::string href = node.src;
      if (!href.empty() && href[0] != '#') href = "#" + href;

      // Resolve target (same rules as jump_to_href, local to avoid editor dep)
      NodeId target_id = INVALID_NODE;
      if (!href.empty() && href[0] == '#') {
        auto lower = [](std::string s) {
          for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
          return s;
        };
        std::string tlow = lower(href.substr(1));
        // Collect text-bearing nodes
        std::vector<NodeId> stack = {doc.root};
        std::vector<NodeId> text_nodes;
        while (!stack.empty()) {
          NodeId cur = stack.back();
          stack.pop_back();
          if (cur == INVALID_NODE) continue;
          const Node& cn = doc.get(cur);
          if (cn.kind == NodeKind::Paragraph || cn.kind == NodeKind::Heading ||
              cn.kind == NodeKind::ListItem || cn.kind == NodeKind::BlockQuote) {
            text_nodes.push_back(cur);
          }
          for (auto it = cn.children.rbegin(); it != cn.children.rend(); ++it)
            stack.push_back(*it);
        }
        for (NodeId tid : text_nodes) {
          const Node& tn = doc.get(tid);
          std::string text;
          for (const auto& sp : tn.spans) text += sp.text;
          if (lower(text) == tlow) {
            target_id = tid;
            break;
          }
        }
        if (target_id == INVALID_NODE) {
          for (NodeId tid : text_nodes) {
            const Node& tn = doc.get(tid);
            if (tn.kind != NodeKind::Heading) continue;
            std::string text;
            for (const auto& sp : tn.spans) text += sp.text;
            if (lower(text).find(tlow) != std::string::npos) {
              target_id = tid;
              break;
            }
          }
        }
      }

      // Label line
      {
        Node label;
        label.id = id;
        label.kind = NodeKind::Paragraph;
        InlineSpan s;
        s.kind = InlineKind::Emphasis;
        s.text = "↗ ";
        s.text += href.empty() ? std::string{"(no target)"} : href;
        label.spans.push_back(std::move(s));
        layout_paragraph(doc, label, x + bar_w + indent, y,
                         max_width - bar_w - indent, out, FontRole::Body);
        y += 4.f;
      }

      // Body: copy of target spans, or a missing marker
      {
        Node body;
        body.id = id;
        body.kind = NodeKind::Paragraph;
        if (target_id != INVALID_NODE) {
          body.spans = doc.get(target_id).spans;
        } else {
          InlineSpan s;
          s.kind = InlineKind::Emphasis;
          s.text = "[missing transclusion]";
          body.spans.push_back(std::move(s));
        }
        layout_paragraph(doc, body, x + bar_w + indent, y,
                         max_width - bar_w - indent, out, FontRole::Body);
      }
      y += 10.f;
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

void LayoutEngine::layout_paragraph(const Document& doc, const Node& node,
                                    float x, float& y, float max_width,
                                    LayoutResult& out, FontRole base_role) {
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
    for (size_t ci = 0; ci < cps.size(); ++ci) {
      chars.push_back({cps[ci], si, ci, node.spans[si].kind, r});
    }
  }

  Font& metrics = fonts_.pick(base_role == FontRole::Heading ? FontRole::Heading
                                                             : FontRole::Body);
  if (chars.empty()) {
    y += metrics.line_height();
    return;
  }

  float pen_x = x;
  float baseline = y + metrics.ascender();
  float line_start = x;
  size_t line_start_idx = 0;

  auto break_line = [&](size_t end_idx) {
    float lx = line_start;
    for (size_t i = line_start_idx; i < end_idx; ++i) {
      Font& font = fonts_.pick(chars[i].role);
      const Glyph& g = font.glyph(chars[i].cp);
      PositionedGlyph pg;
      pg.codepoint = chars[i].cp;
      pg.x = lx + static_cast<float>(g.bearing_x);
      pg.y = baseline;
      pg.node = node.id;
      pg.span_index = chars[i].span_idx;
      pg.char_index = chars[i].char_idx;
      pg.kind = chars[i].kind;
      pg.font = chars[i].role;
      out.glyphs.push_back(pg);
      lx += static_cast<float>(g.advance >> 6);
    }
    baseline += metrics.line_height() * 1.3f;
    pen_x = x;
    line_start = x;
    line_start_idx = end_idx;
  };

  for (size_t i = 0; i < chars.size(); ++i) {
    Font& font = fonts_.pick(chars[i].role);
    const Glyph& g = font.glyph(chars[i].cp);
    float adv = static_cast<float>(g.advance >> 6);

    if (pen_x + adv > x + max_width && chars[i].cp != ' ' && i > line_start_idx) {
      size_t break_at = i;
      for (size_t j = i; j > line_start_idx; --j) {
        if (chars[j - 1].cp == ' ') {
          break_at = j;
          break;
        }
      }
      break_line(break_at);
      while (i < chars.size() && chars[i].cp == ' ') ++i;
      if (i >= chars.size()) break;
      --i;
      continue;
    }
    pen_x += adv;
  }
  if (line_start_idx < chars.size()) {
    break_line(chars.size());
  }

  y = baseline - metrics.ascender() + metrics.line_height() * 0.2f;
}

}  // namespace glyphtoo
