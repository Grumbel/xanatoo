// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/parse.hpp"
#include <cctype>
#include <algorithm>
#include <vector>

namespace glyphtoo {

namespace {

struct Line {
  std::string_view text;
  bool blank = false;
};

std::vector<Line> split_lines(std::string_view src) {
  std::vector<Line> lines;
  size_t i = 0;
  while (i < src.size()) {
    size_t start = i;
    while (i < src.size() && src[i] != '\n') ++i;
    std::string_view line = src.substr(start, i - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    bool blank = true;
    for (char c : line) {
      if (!std::isspace(static_cast<unsigned char>(c))) {
        blank = false;
        break;
      }
    }
    lines.push_back({line, blank});
    if (i < src.size()) ++i;  // skip \n
  }
  return lines;
}

// Detect a list-item marker at the start of a trimmed line.
// Returns bytes to skip past the marker, or 0 if not a list item.
// Sets *ordered_index to 1-based index for "N. " markers, or 0 for "- "/"* ".
size_t list_marker_len(std::string_view trimmed, int* ordered_index) {
  if (ordered_index) *ordered_index = 0;
  if (trimmed.size() >= 2 &&
      (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
      trimmed[1] == ' ') {
    return 2;
  }
  // Ordered: one or more digits, then ". "
  size_t i = 0;
  while (i < trimmed.size() &&
         std::isdigit(static_cast<unsigned char>(trimmed[i])))
    ++i;
  if (i > 0 && i + 1 < trimmed.size() && trimmed[i] == '.' &&
      trimmed[i + 1] == ' ') {
    if (ordered_index) {
      int n = 0;
      for (size_t k = 0; k < i; ++k)
        n = n * 10 + (trimmed[k] - '0');
      *ordered_index = n > 0 ? n : 1;
    }
    return i + 2;
  }
  return 0;
}

// Parse inline spans for a single paragraph/heading line.
void parse_inlines(Node& node, std::string_view text) {
  size_t i = 0;
  std::string plain;

  auto flush_plain = [&]() {
    if (!plain.empty()) {
      InlineSpan s;
      s.kind = InlineKind::Text;
      s.text = plain;
      node.spans.push_back(std::move(s));
      plain.clear();
    }
  };

  while (i < text.size()) {
    // **strong**
    if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
      size_t end = text.find("**", i + 2);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Strong;
        s.text = std::string(text.substr(i + 2, end - (i + 2)));
        node.spans.push_back(std::move(s));
        i = end + 2;
        continue;
      }
    }
    // *emphasis*
    if (text[i] == '*') {
      size_t end = text.find('*', i + 1);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Emphasis;
        s.text = std::string(text.substr(i + 1, end - (i + 1)));
        node.spans.push_back(std::move(s));
        i = end + 1;
        continue;
      }
    }
    // [text](url) or [text](#id)
    if (text[i] == '[') {
      size_t mid = text.find("](", i + 1);
      if (mid != std::string_view::npos) {
        size_t end = text.find(')', mid + 2);
        if (end != std::string_view::npos) {
          flush_plain();
          InlineSpan s;
          s.kind = InlineKind::Link;
          s.text = std::string(text.substr(i + 1, mid - (i + 1)));
          s.href = std::string(text.substr(mid + 2, end - (mid + 2)));
          node.spans.push_back(std::move(s));
          i = end + 1;
          continue;
        }
      }
    }
    // `code`
    if (text[i] == '`') {
      size_t end = text.find('`', i + 1);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Code;
        s.text = std::string(text.substr(i + 1, end - (i + 1)));
        node.spans.push_back(std::move(s));
        i = end + 1;
        continue;
      }
    }

    plain.push_back(text[i]);
    ++i;
  }
  flush_plain();
}

}  // namespace

Document parse_markup(std::string_view source) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);

  auto lines = split_lines(source);
  size_t i = 0;

  // Simple stack for nested containers (stretch, columns, column)
  std::vector<NodeId> stack;
  stack.push_back(root);

  auto current_parent = [&]() -> NodeId { return stack.back(); };

  while (i < lines.size()) {
    const Line& L = lines[i];

    if (L.blank) {
      ++i;
      continue;
    }

    std::string_view t = L.text;
    // Trim leading spaces for block detection (but keep for paragraphs)
    size_t lead = 0;
    while (lead < t.size() && (t[lead] == ' ' || t[lead] == '\t')) ++lead;
    std::string_view trimmed = t.substr(lead);

    // Closing fence
    if (trimmed == ":::" || trimmed.starts_with("::: ")) {
      // Treat bare ::: as close of current container if possible
      if (stack.size() > 1) {
        stack.pop_back();
      }
      ++i;
      continue;
    }

    // Opening fences
    if (trimmed.starts_with(":::stretch")) {
      NodeId n = doc.create(NodeKind::Stretch, current_parent());
      // Parse summary="..." if present
      auto pos = trimmed.find("summary=\"");
      if (pos != std::string_view::npos) {
        pos += 9; // after summary="
        auto end = trimmed.find('"', pos);
        if (end != std::string_view::npos) {
          doc.get(n).summary = std::string(trimmed.substr(pos, end - pos));
        }
      }
      doc.get(n).expanded = false; // start collapsed — classic stretchtext
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::columns")) {
      NodeId n = doc.create(NodeKind::Columns, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::column")) {
      NodeId n = doc.create(NodeKind::Column, current_parent());
      // Optional weight=1.5 (relative column width)
      auto wpos = trimmed.find("weight=");
      if (wpos != std::string_view::npos) {
        wpos += 7;
        size_t wend = wpos;
        while (wend < trimmed.size() &&
               (std::isdigit(static_cast<unsigned char>(trimmed[wend])) ||
                trimmed[wend] == '.'))
          ++wend;
        try {
          float w = std::stof(std::string(trimmed.substr(wpos, wend - wpos)));
          if (w > 0.1f && w < 20.f) doc.get(n).display_scale = w;
        } catch (...) {}
      }
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::note")) {
      // Treat note as a nested paragraph-like container for now
      NodeId n = doc.create(NodeKind::Paragraph, current_parent());
      // just open; content follows until :::
      stack.push_back(n);
      ++i;
      continue;
    }
    // Self-closing include:
    //   :::include #LocalHeading
    //   :::include LocalHeading          → #LocalHeading
    //   :::include other.md#Section      → cross-document (path kept)
    //   :::include other.md              → whole external document body
    //   :::include "path with spaces.md#H"
    if (trimmed.starts_with(":::include")) {
      NodeId n = doc.create(NodeKind::Transclude, current_parent());
      std::string_view rest = trimmed.substr(10);  // after :::include
      while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t'))
        rest.remove_prefix(1);
      if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
        char q = rest[0];
        rest.remove_prefix(1);
        size_t end = rest.find(q);
        if (end != std::string_view::npos) rest = rest.substr(0, end);
      }
      std::string target(rest);
      while (!target.empty() &&
             (target.back() == ' ' || target.back() == '\t'))
        target.pop_back();
      // Local anchors get a leading '#'; paths (file.md, dir/x, …) stay raw.
      auto looks_like_path = [](const std::string& s) {
        if (s.empty() || s[0] == '#') return false;
        size_t hash = s.find('#');
        std::string path_part = hash == std::string::npos ? s : s.substr(0, hash);
        if (path_part.empty()) return false;
        return path_part.find('/') != std::string::npos ||
               path_part.find('\\') != std::string::npos ||
               path_part.find('.') != std::string::npos;
      };
      if (!target.empty() && target[0] != '#' && !looks_like_path(target))
        target = "#" + target;
      doc.get(n).src = target;
      ++i;
      continue;
    }

    // Image ![alt](src) optional {scale=1.5}
    if (trimmed.starts_with("![")) {
      size_t mid = trimmed.find("](");
      size_t end = trimmed.find(')', mid == std::string_view::npos ? 0 : mid);
      if (mid != std::string_view::npos && end != std::string_view::npos) {
        NodeId n = doc.create(NodeKind::Image, current_parent());
        doc.get(n).alt = std::string(trimmed.substr(2, mid - 2));
        doc.get(n).src = std::string(trimmed.substr(mid + 2, end - (mid + 2)));
        // Optional {scale=…} after the closing paren
        size_t brace = trimmed.find("{scale=", end);
        if (brace != std::string_view::npos) {
          size_t bend = trimmed.find('}', brace);
          if (bend != std::string_view::npos) {
            std::string num(trimmed.substr(brace + 7, bend - (brace + 7)));
            try {
              float s = std::stof(num);
              if (s > 0.05f && s < 10.f) doc.get(n).display_scale = s;
            } catch (...) {}
          }
        }
        ++i;
        continue;
      }
    }

    // Horizontal rule: a line of only -, *, or _ (at least three)
    if (trimmed.size() >= 3) {
      char hc = trimmed[0];
      if (hc == '-' || hc == '*' || hc == '_') {
        bool all_same = true;
        for (char ch : trimmed) {
          if (ch != hc) {
            all_same = false;
            break;
          }
        }
        if (all_same) {
          doc.create(NodeKind::HorizontalRule, current_parent());
          ++i;
          continue;
        }
      }
    }

    // Headings
    int heading_level = 0;
    if (trimmed.starts_with("###### ")) heading_level = 6;
    else if (trimmed.starts_with("##### ")) heading_level = 5;
    else if (trimmed.starts_with("#### ")) heading_level = 4;
    else if (trimmed.starts_with("### ")) heading_level = 3;
    else if (trimmed.starts_with("## ")) heading_level = 2;
    else if (trimmed.starts_with("# ")) heading_level = 1;

    if (heading_level > 0) {
      size_t skip = heading_level + 1;
      NodeId n = doc.create(NodeKind::Heading, current_parent());
      doc.get(n).level = heading_level;
      parse_inlines(doc.get(n), trimmed.substr(skip));
      ++i;
      continue;
    }

    // Unordered / ordered list (supports nested items via leading indent)
    {
      int ord = 0;
      size_t mlen = list_marker_len(trimmed, &ord);
      if (mlen > 0) {
        // Stack of (list_id, indent of items in that list)
        struct Frame {
          NodeId list;
          int indent;
        };
        std::vector<Frame> frames;
        int base_indent = static_cast<int>(lines[i].text.size() - trimmed.size());
        // recomputed properly:
        {
          std::string_view raw = lines[i].text;
          size_t lead = 0;
          while (lead < raw.size() && (raw[lead] == ' ' || raw[lead] == '\t'))
            ++lead;
          base_indent = static_cast<int>(lead);
        }
        NodeId top = doc.create(NodeKind::List, current_parent());
        doc.get(top).level = (ord > 0) ? 1 : 0;
        frames.push_back({top, base_indent});
        int item_index = 0;

        while (i < lines.size() && !lines[i].blank) {
          std::string_view lt = lines[i].text;
          size_t llead = 0;
          while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
            ++llead;
          std::string_view ltrim = lt.substr(llead);
          int item_ord = 0;
          size_t imlen = list_marker_len(ltrim, &item_ord);
          if (imlen == 0) break;
          int ind = static_cast<int>(llead);

          // Pop to a frame whose indent is strictly less than this line's
          // when this line is less-or-equally indented than the current frame
          while (frames.size() > 1 && ind <= frames.back().indent)
            frames.pop_back();

          // Deeper indent → nest under the previous item of the current list
          if (ind > frames.back().indent) {
            Node& cur_list = doc.get(frames.back().list);
            if (cur_list.children.empty()) {
              // No host item yet; treat as same level
            } else {
              NodeId host = cur_list.children.back();
              if (doc.get(host).kind == NodeKind::ListItem) {
                NodeId nested = doc.create(NodeKind::List, host);
                // Inherit ordered/unordered from the marker on this line
                doc.get(nested).level = (item_ord > 0) ? 1 : 0;
                frames.push_back({nested, ind});
              }
            }
          }

          NodeId list = frames.back().list;
          bool list_ordered = doc.get(list).level == 1;
          bool item_ordered = item_ord > 0;
          // Kind mismatch at this level: close the list run
          if (item_ordered != list_ordered && frames.size() == 1) break;
          // If nested and kind differs, still accept (nested can differ)

          NodeId item = doc.create(NodeKind::ListItem, list);
          ++item_index;
          doc.get(item).level = list_ordered
                                    ? (item_ord > 0 ? item_ord : item_index)
                                    : 0;
          parse_inlines(doc.get(item), ltrim.substr(imlen));
          ++i;
        }
        // Renumber every list we created
        for (const auto& f : frames)
          ;  // renumber happens via display numbers already set
        continue;
      }
    }

    // Block quote: consecutive lines starting with "> "
    if (trimmed.starts_with("> ")) {
      std::string quote;
      while (i < lines.size() && !lines[i].blank) {
        std::string_view lt = lines[i].text;
        size_t llead = 0;
        while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
          ++llead;
        std::string_view ltrim = lt.substr(llead);
        if (!ltrim.starts_with("> ")) break;
        if (!quote.empty()) quote.push_back(' ');
        quote.append(ltrim.substr(2));
        ++i;
      }
      if (!quote.empty()) {
        NodeId n = doc.create(NodeKind::BlockQuote, current_parent());
        parse_inlines(doc.get(n), quote);
      }
      continue;
    }

    // Paragraph: gather consecutive non-blank, non-fence lines
    std::string para;
    while (i < lines.size() && !lines[i].blank) {
      std::string_view lt = lines[i].text;
      size_t llead = 0;
      while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t')) ++llead;
      std::string_view ltrim = lt.substr(llead);
      if (ltrim.starts_with(":::") || ltrim.starts_with("# ") ||
          ltrim.starts_with("## ") || ltrim.starts_with("![") ||
          ltrim.starts_with("> ") ||
          list_marker_len(ltrim, nullptr) > 0) {
        break;
      }
      if (!para.empty()) para.push_back(' ');
      // Prefer the original line content without leading indent for simplicity
      para.append(ltrim);
      ++i;
    }
    if (!para.empty()) {
      NodeId n = doc.create(NodeKind::Paragraph, current_parent());
      parse_inlines(doc.get(n), para);
    }
  }

  return doc;
}

namespace {

void serialize_inlines(const Node& node, std::string& out) {
  for (const auto& sp : node.spans) {
    switch (sp.kind) {
      case InlineKind::Strong:
        out += "**";
        out += sp.text;
        out += "**";
        break;
      case InlineKind::Emphasis:
        out += "*";
        out += sp.text;
        out += "*";
        break;
      case InlineKind::Code:
        out += "`";
        out += sp.text;
        out += "`";
        break;
      case InlineKind::Link:
        out += "[";
        out += sp.text;
        out += "](";
        out += sp.href;
        out += ")";
        break;
      default:
        out += sp.text;
        break;
    }
  }
}

void serialize_node(const Document& doc, NodeId id, std::string& out,
                    int indent) {
  const Node& node = doc.get(id);
  std::string pad(static_cast<size_t>(indent), ' ');

  switch (node.kind) {
    case NodeKind::Document:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      break;

    case NodeKind::Heading: {
      out += pad;
      out.append(static_cast<size_t>(std::max(1, std::min(node.level, 6))), '#');
      out += ' ';
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::Paragraph: {
      out += pad;
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::BlockQuote: {
      out += pad;
      out += "> ";
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::HorizontalRule: {
      out += pad;
      out += "---\n\n";
      break;
    }

    case NodeKind::Transclude: {
      out += pad;
      out += ":::include ";
      out += node.src;
      out += "\n\n";
      break;
    }

    case NodeKind::ListItem: {
      out += pad;
      // level > 0 means ordered list item with that display number
      if (node.level > 0) {
        out += std::to_string(node.level);
        out += ". ";
      } else {
        out += "- ";
      }
      serialize_inlines(node, out);
      out += "\n";
      // Nested lists under this item
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::List)
          serialize_node(doc, c, out, indent + 2);
      }
      break;
    }

    case NodeKind::Image: {
      out += pad;
      out += "![";
      out += node.alt;
      out += "](";
      out += node.src;
      out += ")";
      if (node.display_scale > 0.05f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += "{scale=";
        out += std::to_string(node.display_scale);
        // trim excess float digits: keep ~2
        // (to_string is fine for round-trip)
        out += "}";
      }
      out += "\n\n";
      break;
    }

    case NodeKind::Stretch: {
      out += pad;
      out += ":::stretch";
      if (!node.summary.empty()) {
        out += " summary=\"";
        out += node.summary;
        out += "\"";
      }
      out += "\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Columns: {
      out += pad;
      out += ":::columns\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Column: {
      out += pad;
      out += ":::column";
      if (node.display_scale > 0.15f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += " weight=";
        out += std::to_string(node.display_scale);
      }
      out += "\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::List:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      out += "\n";
      break;

    default:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      break;
  }
}

}  // namespace

std::string serialize_markup(const Document& doc) {
  std::string out;
  if (doc.root != INVALID_NODE) {
    serialize_node(doc, doc.root, out, 0);
  }
  return out;
}

}  // namespace glyphtoo
