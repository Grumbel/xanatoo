// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/layout.hpp"

namespace glyphtoo {

LayoutEngine::LayoutEngine(Font& body, Font& heading, ImageCache* images)
    : body_(body), heading_(heading), images_(images) {}

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
    case NodeKind::List:
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      break;

    case NodeKind::Column:
      // Column content is laid out by the parent Columns node.
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      break;

    case NodeKind::Columns: {
      // Side-by-side columns. Equal width split with a small gutter.
      std::vector<NodeId> cols;
      for (NodeId child : node.children) {
        if (doc.get(child).kind == NodeKind::Column) {
          cols.push_back(child);
        }
      }
      if (cols.empty()) {
        // No explicit column children — fall back to stacking everything
        for (NodeId child : node.children) {
          layout_node(doc, child, x, y, max_width, out);
        }
        break;
      }

      const float gutter = 16.f;
      const float n = static_cast<float>(cols.size());
      const float col_w = (max_width - gutter * (n - 1.f)) / n;
      if (col_w < 40.f) {
        // Too narrow — stack vertically instead of crushing text
        for (NodeId child : cols) {
          layout_node(doc, child, x, y, max_width, out);
        }
        break;
      }

      float start_y = y;
      float max_bottom = y;

      for (size_t i = 0; i < cols.size(); ++i) {
        float cx = x + static_cast<float>(i) * (col_w + gutter);
        float cy = start_y;

        // Layout this column into a temporary result, then merge
        LayoutResult col_out;
        layout_node(doc, cols[i], cx, cy, col_w, col_out);

        // Merge glyphs / boxes / images
        out.glyphs.insert(out.glyphs.end(),
                          col_out.glyphs.begin(), col_out.glyphs.end());
        out.boxes.insert(out.boxes.end(),
                         col_out.boxes.begin(), col_out.boxes.end());
        out.images.insert(out.images.end(),
                          col_out.images.begin(), col_out.images.end());

        if (cy > max_bottom) max_bottom = cy;
      }

      // Optional subtle column separators
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
        layout_paragraph(doc, fake, x, y, max_width, out, body_, FontRole::Body);
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
          layout_paragraph(doc, fake, x, y, max_width, out, body_, FontRole::Body);
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
      Font& f = (node.level <= 2) ? heading_ : body_;
      FontRole role = (node.level <= 2) ? FontRole::Heading : FontRole::Body;
      float start_y = y;
      y += (node.level == 1) ? 12.f : 8.f;
      layout_paragraph(doc, node, x, y, max_width, out, f, role);
      y += (node.level == 1) ? 14.f : 10.f;
      out.boxes.push_back({id, x, start_y, max_width, y - start_y});
      break;
    }

    case NodeKind::Paragraph:
    case NodeKind::ListItem: {
      float start_y = y;
      layout_paragraph(doc, node, x, y, max_width, out, body_, FontRole::Body);
      y += 14.f;
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
          // Cap height so a huge photo does not dominate the view
          float max_h = 480.f;
          if (img.height * scale > max_h) {
            scale = max_h / static_cast<float>(img.height);
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
        layout_paragraph(doc, fake, x, y2, max_width, out, body_, FontRole::Body);
        y = y2 + 10.f;
      } else {
        y += disp_h + 14.f;
      }
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
                                    LayoutResult& out, Font& font,
                                    FontRole role) {
  (void)doc;
  struct CharInfo {
    uint32_t cp;
    size_t span_idx;
    size_t char_idx;
    InlineKind kind;
  };
  std::vector<CharInfo> chars;
  for (size_t si = 0; si < node.spans.size(); ++si) {
    std::vector<uint32_t> cps;
    decode_utf8(node.spans[si].text, cps);
    for (size_t ci = 0; ci < cps.size(); ++ci) {
      chars.push_back({cps[ci], si, ci, node.spans[si].kind});
    }
  }

  if (chars.empty()) {
    y += font.line_height();
    return;
  }

  float pen_x = x;
  float baseline = y + font.ascender();
  float line_start = x;
  size_t line_start_idx = 0;

  auto break_line = [&](size_t end_idx) {
    float lx = line_start;
    for (size_t i = line_start_idx; i < end_idx; ++i) {
      const Glyph& g = font.glyph(chars[i].cp);
      PositionedGlyph pg;
      pg.codepoint = chars[i].cp;
      pg.x = lx + static_cast<float>(g.bearing_x);
      pg.y = baseline;
      pg.node = node.id;
      pg.span_index = chars[i].span_idx;
      pg.char_index = chars[i].char_idx;
      pg.kind = chars[i].kind;
      pg.font = role;
      out.glyphs.push_back(pg);
      lx += static_cast<float>(g.advance >> 6);
    }
    baseline += font.line_height() * 1.3f;
    pen_x = x;
    line_start = x;
    line_start_idx = end_idx;
  };

  for (size_t i = 0; i < chars.size(); ++i) {
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

  y = baseline - font.ascender() + font.line_height() * 0.2f;
}

}  // namespace glyphtoo
