// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/layout.hpp"
#include <cctype>

namespace glyphtoo {

LayoutEngine::LayoutEngine(Font& font) : font_(font) {}

LayoutResult LayoutEngine::layout(const Document& doc, float max_width) {
  LayoutResult result;
  if (doc.root == INVALID_NODE) return result;
  float y = 16.f;
  layout_node(doc, doc.root, 24.f, y, max_width - 48.f, result);
  result.content_height = y + 32.f;
  return result;
}

void LayoutEngine::layout_node(const Document& doc, NodeId id,
                               float x, float& y, float max_width,
                               LayoutResult& out) {
  const Node& node = doc.get(id);

  switch (node.kind) {
    case NodeKind::Document:
    case NodeKind::Columns:
    case NodeKind::Column:
    case NodeKind::List:
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      break;

    case NodeKind::Heading: {
      float scale = 1.0f + 0.35f * (4 - std::min(node.level, 4));
      // Temporarily we just use the same font; scale is visual later.
      float start_y = y;
      layout_paragraph(doc, node, x, y, max_width, out, scale);
      y += 8.f * scale;
      LayoutBox box{id, x, start_y, max_width, y - start_y};
      out.boxes.push_back(box);
      break;
    }

    case NodeKind::Paragraph:
    case NodeKind::ListItem: {
      float start_y = y;
      layout_paragraph(doc, node, x, y, max_width, out);
      y += 12.f;
      LayoutBox box{id, x, start_y, max_width, y - start_y};
      out.boxes.push_back(box);
      break;
    }

    case NodeKind::Image: {
      // Placeholder box for now
      float h = 120.f;
      LayoutBox box{id, x, y, max_width, h};
      out.boxes.push_back(box);
      y += h + 12.f;
      break;
    }

    case NodeKind::Stretch:
      // For now treat as normal block; expand/collapse later
      for (NodeId child : node.children) {
        layout_node(doc, child, x, y, max_width, out);
      }
      break;

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
                                    LayoutResult& out, float font_scale) {
  // Collect all codepoints with source tracking
  struct CharInfo {
    uint32_t cp;
    size_t span_idx;
    size_t char_idx;
  };
  std::vector<CharInfo> chars;
  for (size_t si = 0; si < node.spans.size(); ++si) {
    std::vector<uint32_t> cps;
    decode_utf8(node.spans[si].text, cps);
    for (size_t ci = 0; ci < cps.size(); ++ci) {
      chars.push_back({cps[ci], si, ci});
    }
  }

  if (chars.empty()) {
    y += font_.line_height() * font_scale;
    return;
  }

  float pen_x = x;
  float baseline = y + font_.ascender() * font_scale;
  float line_start = x;
  size_t line_start_idx = 0;

  auto break_line = [&](size_t end_idx) {
    // Emit glyphs for the line
    float lx = line_start;
    for (size_t i = line_start_idx; i < end_idx; ++i) {
      const Glyph& g = font_.glyph(chars[i].cp);
      PositionedGlyph pg;
      pg.codepoint = chars[i].cp;
      pg.x = lx + g.bearing_x * font_scale;
      pg.y = baseline;
      pg.node = node.id;
      pg.span_index = chars[i].span_idx;
      pg.char_index = chars[i].char_idx;
      out.glyphs.push_back(pg);
      lx += (g.advance >> 6) * font_scale;
    }
    baseline += font_.line_height() * font_scale * 1.25f;
    pen_x = x;
    line_start = x;
    line_start_idx = end_idx;
  };

  for (size_t i = 0; i < chars.size(); ++i) {
    const Glyph& g = font_.glyph(chars[i].cp);
    float adv = (g.advance >> 6) * font_scale;

    // Simple word-wrap: break on space when exceeding width
    if (pen_x + adv > x + max_width && chars[i].cp != ' ' && i > line_start_idx) {
      // Find last space
      size_t break_at = i;
      for (size_t j = i; j > line_start_idx; --j) {
        if (chars[j - 1].cp == ' ') {
          break_at = j;
          break;
        }
      }
      if (break_at == i) break_at = i;  // hard break
      break_line(break_at);
      // Skip leading spaces on new line
      while (i < chars.size() && chars[i].cp == ' ') ++i;
      if (i >= chars.size()) break;
      --i;  // loop will ++
      continue;
    }

    pen_x += adv;
  }
  // Final line
  if (line_start_idx < chars.size()) {
    break_line(chars.size());
  }

  y = baseline - font_.ascender() * font_scale + font_.line_height() * font_scale * 0.25f;
}

}  // namespace glyphtoo
