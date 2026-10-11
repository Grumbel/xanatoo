// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/org.hpp"
#include "xanatoo/util.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

bool path_uses_org(std::string_view path) {
  return path_ends_with_ci(path, ".org");
}

namespace {

bool is_ws(char c) {
  return c == ' ' || c == '\t';
}

std::string_view trim_view(std::string_view s) {
  while (!s.empty() && (is_ws(s.front()) || s.front() == '\r'))
    s.remove_prefix(1);
  while (!s.empty() && (is_ws(s.back()) || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

std::string to_upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}

// Case-insensitive prefix test without allocating a full uppercased line.
bool starts_with_ci(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = s[i], b = prefix[i];
    if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
    if (a != b) return false;
  }
  return true;
}

bool is_todo_keyword(std::string_view w) {
  static const char* kws[] = {"TODO", "DONE", "NEXT", "WAITING", "CANCELLED",
                              "CANCELED", "HOLD", "WIP", nullptr};
  std::string u = to_upper(std::string(w));
  for (const char** p = kws; *p; ++p)
    if (u == *p) return true;
  return false;
}

// Parse org emphasis / links into node.spans
void parse_org_inlines(Node& node, std::string_view text) {
  auto push = [&](InlineKind k, std::string_view t, std::string href = {}) {
    if (t.empty() && href.empty()) return;
    InlineSpan sp;
    sp.kind = k;
    sp.text = std::string(t);
    sp.href = std::move(href);
    node.spans.push_back(std::move(sp));
  };

  size_t i = 0;
  while (i < text.size()) {
    // [[target][desc]] or [[target]]
    if (text[i] == '[' && i + 1 < text.size() && text[i + 1] == '[') {
      size_t end = text.find("]]", i + 2);
      if (end != std::string_view::npos) {
        std::string_view inner = text.substr(i + 2, end - (i + 2));
        std::string target, desc;
        size_t sep = inner.find("][");
        if (sep != std::string_view::npos) {
          target = std::string(inner.substr(0, sep));
          desc = std::string(inner.substr(sep + 2));
        } else {
          target = std::string(inner);
          desc = target;
        }
        // Internal headline targets → #anchor
        if (!target.empty() && target[0] != '#' &&
            target.find("://") == std::string::npos &&
            target.find('.') == std::string::npos &&
            target.find('/') == std::string::npos) {
          target = "#" + target;
        }
        push(InlineKind::Link, desc, target);
        i = end + 2;
        continue;
      }
    }

    // Paired markup: *bold* /italic/ =code= ~code~ +strike+
    auto try_pair = [&](char marker, InlineKind kind) -> bool {
      if (text[i] != marker) return false;
      // Opening: start of text or after whitespace; not doubled
      bool open_ok = (i == 0 || is_ws(text[i - 1]) || text[i - 1] == '(' ||
                      text[i - 1] == '[');
      if (!open_ok) return false;
      size_t j = i + 1;
      if (j >= text.size() || is_ws(text[j]) || text[j] == marker) return false;
      while (j < text.size() && text[j] != marker) ++j;
      if (j >= text.size() || j == i + 1) return false;
      // Closing: not preceded by whitespace
      if (is_ws(text[j - 1])) return false;
      push(kind, text.substr(i + 1, j - i - 1));
      i = j + 1;
      return true;
    };

    if (try_pair('*', InlineKind::Strong)) continue;
    if (try_pair('/', InlineKind::Emphasis)) continue;
    if (try_pair('=', InlineKind::Code)) continue;
    if (try_pair('~', InlineKind::Code)) continue;
    if (try_pair('+', InlineKind::Strike)) continue;

    // Accumulate plain text until next special
    size_t start = i;
    ++i;
    while (i < text.size()) {
      char c = text[i];
      if (c == '[' && i + 1 < text.size() && text[i + 1] == '[') break;
      if (c == '*' || c == '/' || c == '=' || c == '~' || c == '+') {
        // Possible markup open — let outer loop decide
        bool open_ok = (i == 0 || is_ws(text[i - 1]) || text[i - 1] == '(');
        if (open_ok) break;
      }
      ++i;
    }
    push(InlineKind::Text, text.substr(start, i - start));
  }
}

struct Line {
  std::string text;
  int indent = 0;  // leading spaces (tabs → 2)
  bool blank = false;
};

std::vector<Line> split_lines(std::string_view source) {
  std::vector<Line> out;
  // Approximate line count from newlines.
  size_t approx = 1;
  for (char c : source)
    if (c == '\n') ++approx;
  out.reserve(approx);
  size_t i = 0;
  while (i <= source.size()) {
    size_t eol = source.find('\n', i);
    if (eol == std::string_view::npos) eol = source.size();
    std::string_view raw = source.substr(i, eol - i);
    if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    Line L;
    int ind = 0;
    size_t p = 0;
    while (p < raw.size() && is_ws(raw[p])) {
      ind += (raw[p] == '\t') ? 2 : 1;
      ++p;
    }
    L.indent = ind;
    L.text = std::string(raw.substr(p));
    L.blank = L.text.empty();
    out.push_back(std::move(L));
    if (eol >= source.size()) break;
    i = eol + 1;
  }
  return out;
}

// Heading: stars at column 0 only (indent == 0 and text starts with *)
struct HeadingParts {
  int level = 0;
  std::string todo;
  std::string title;
  std::string tags;  // "a:b" without outer colons
};

bool parse_heading_line(const Line& L, HeadingParts& out) {
  if (L.indent != 0) return false;
  if (L.text.empty() || L.text[0] != '*') return false;
  size_t stars = 0;
  while (stars < L.text.size() && L.text[stars] == '*') ++stars;
  if (stars == 0 || stars >= L.text.size() || !is_ws(L.text[stars]))
    return false;
  out.level = static_cast<int>(stars);
  std::string_view rest = trim_view(std::string_view(L.text).substr(stars));
  // Optional TODO keyword
  size_t sp = 0;
  while (sp < rest.size() && !is_ws(rest[sp])) ++sp;
  std::string_view first = rest.substr(0, sp);
  if (is_todo_keyword(first)) {
    out.todo = std::string(first);
    rest = trim_view(rest.substr(sp));
  }
  // Optional trailing tags :foo:bar:
  out.tags.clear();
  out.title = std::string(rest);
  if (!out.title.empty() && out.title.back() == ':') {
    /* tag block is the last whitespace-separated token */
    // find start of tag block: last space before a colon-run
    size_t i = out.title.size();
    while (i > 0 && out.title[i - 1] != ' ' && out.title[i - 1] != '\t') --i;
    std::string_view maybe = trim_view(std::string_view(out.title).substr(i));
    if (maybe.size() >= 3 && maybe.front() == ':' && maybe.back() == ':') {
      out.tags = std::string(maybe.substr(1, maybe.size() - 2));
      // strip trailing empty segments from double colons
      while (!out.tags.empty() && out.tags.back() == ':') out.tags.pop_back();
      out.title = std::string(trim_view(std::string_view(out.title).substr(0, i)));
    }
  }
  return true;
}

struct ListLead {
  bool ok = false;
  bool ordered = false;
  int number = 0;
  bool checkbox = false;
  bool checked = false;
  std::string body;
};

ListLead parse_list_lead(std::string_view text) {
  ListLead L;
  if (text.empty()) return L;
  size_t i = 0;
  // ordered: 1. or 1)
  if (std::isdigit(static_cast<unsigned char>(text[0]))) {
    int n = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      n = n * 10 + (text[i] - '0');
      ++i;
    }
    if (i < text.size() && (text[i] == '.' || text[i] == ')')) {
      ++i;
      if (i < text.size() && is_ws(text[i])) {
        while (i < text.size() && is_ws(text[i])) ++i;
        L.ok = true;
        L.ordered = true;
        L.number = n;
      }
    }
  } else if (text[0] == '-' || text[0] == '+') {
    if (text.size() >= 2 && is_ws(text[1])) {
      i = 1;
      while (i < text.size() && is_ws(text[i])) ++i;
      L.ok = true;
    }
  }
  // Indented "* " is a list bullet; column-0 "* " is a heading (handled earlier).
  else if (text[0] == '*' && text.size() >= 2 && is_ws(text[1])) {
    i = 1;
    while (i < text.size() && is_ws(text[i])) ++i;
    L.ok = true;
  }
  if (!L.ok) return L;
  // Checkbox
  if (i + 2 < text.size() && text[i] == '[') {
    char mid = text[i + 1];
    if ((mid == ' ' || mid == 'X' || mid == 'x') && text[i + 2] == ']') {
      L.checkbox = true;
      L.checked = (mid == 'X' || mid == 'x');
      i += 3;
      while (i < text.size() && is_ws(text[i])) ++i;
    }
  }
  L.body = std::string(text.substr(i));
  return L;
}

bool is_hr_line(std::string_view t) {
  if (t.size() < 5) return false;
  for (char c : t)
    if (c != '-') return false;
  return true;
}

bool is_table_row(std::string_view t) {
  return !t.empty() && t.front() == '|' && t.find('|', 1) != std::string_view::npos;
}

bool is_table_sep(std::string_view t) {
  // |---+---|
  if (!is_table_row(t)) return false;
  for (char c : t) {
    if (c == '|' || c == '-' || c == '+' || is_ws(c)) continue;
    return false;
  }
  return true;
}

std::vector<std::string> split_table_cells(std::string_view row) {
  std::vector<std::string> cells;
  if (row.empty() || row.front() != '|') return cells;
  size_t i = 1;
  while (i < row.size()) {
    size_t j = row.find('|', i);
    if (j == std::string_view::npos) j = row.size();
    cells.emplace_back(trim_view(row.substr(i, j - i)));
    i = (j < row.size()) ? j + 1 : j;
  }
  // trailing empty after last |
  if (!cells.empty() && cells.back().empty()) cells.pop_back();
  return cells;
}

}  // namespace

Document parse_org(std::string_view source) {
  Document doc;
  // Heuristic: ~one node per 40–80 bytes of source on typical Org files.
  doc.nodes.reserve(source.size() / 48 > 64 ? source.size() / 48 : 64);
  doc.root = doc.create(NodeKind::Document);

  auto lines = split_lines(source);
  size_t i = 0;

  // List frame stack: (list_node, indent)
  struct Frame {
    NodeId list = INVALID_NODE;
    int indent = 0;
  };
  std::vector<Frame> list_frames;

  auto close_lists = [&]() { list_frames.clear(); };

  auto current_parent = [&]() -> NodeId {
    if (!list_frames.empty()) {
      NodeId list = list_frames.back().list;
      if (!doc.get(list).children.empty()) {
        NodeId last = doc.get(list).children.back();
        if (doc.get(last).kind == NodeKind::ListItem) return last;
      }
      return list;
    }
    return doc.root;
  };

  // Flush a paragraph buffer
  std::string para_buf;
  auto flush_para = [&]() {
    if (para_buf.empty()) return;
    NodeId n = doc.create(NodeKind::Paragraph, current_parent());
    // If parent is ListItem, body is under the item
    parse_org_inlines(doc.get(n), trim_view(para_buf));
    para_buf.clear();
  };

  auto ensure_list = [&](int indent, bool ordered) -> NodeId {
    while (!list_frames.empty() && list_frames.back().indent > indent)
      list_frames.pop_back();
    if (!list_frames.empty() && list_frames.back().indent == indent) {
      Node& L = doc.get(list_frames.back().list);
      // mismatch ordered/unordered → new list
      if ((L.level == 1) == ordered) return list_frames.back().list;
      list_frames.pop_back();
    }
    NodeId parent = doc.root;
    if (!list_frames.empty()) {
      // nest under previous item
      NodeId list = list_frames.back().list;
      if (!doc.get(list).children.empty())
        parent = doc.get(list).children.back();
      else
        parent = list;
    }
    NodeId top = doc.create(NodeKind::List, parent);
    doc.get(top).level = ordered ? 1 : 0;
    list_frames.push_back({top, indent});
    return top;
  };

  // Pending PROPERTIES drawer for last heading
  NodeId last_heading = INVALID_NODE;
  bool in_props = false;

  enum class BlockKind { None, Src, Example, Quote };
  BlockKind block = BlockKind::None;
  NodeId block_node = INVALID_NODE;
  std::string block_acc;

  auto flush_block = [&]() {
    if (block == BlockKind::None || block_node == INVALID_NODE) return;
    if (block == BlockKind::Quote) {
      parse_org_inlines(doc.get(block_node), block_acc);
    } else {
      doc.get(block_node).spans.clear();
      InlineSpan sp;
      sp.kind = InlineKind::Text;
      sp.text = block_acc;
      doc.get(block_node).spans.push_back(std::move(sp));
    }
    block = BlockKind::None;
    block_node = INVALID_NODE;
    block_acc.clear();
  };

  while (i < lines.size()) {
    const Line& L = lines[i];
    std::string_view t = L.text;

    // Inside a block
    if (block != BlockKind::None) {
      bool end = false;
      if (block == BlockKind::Src && starts_with_ci(t, "#+END_SRC"))
        end = true;
      else if (block == BlockKind::Example && starts_with_ci(t, "#+END_EXAMPLE"))
        end = true;
      else if (block == BlockKind::Quote && starts_with_ci(t, "#+END_QUOTE"))
        end = true;
      if (end) {
        flush_block();
        ++i;
        continue;
      }
      if (!block_acc.empty()) block_acc.push_back('\n');
      // Preserve indent inside blocks: reconstruct leading spaces
      block_acc.append(static_cast<size_t>(L.indent), ' ');
      block_acc += L.text;
      ++i;
      continue;
    }

    // PROPERTIES drawer
    if (in_props) {
      if (starts_with_ci(t, ":END:")) {
        in_props = false;
        ++i;
        continue;
      }
      // :CUSTOM_ID: value
      if (last_heading != INVALID_NODE && t.size() > 1 && t[0] == ':') {
        size_t colon2 = t.find(':', 1);
        if (colon2 != std::string::npos) {
          std::string key = to_upper(std::string(t.substr(1, colon2 - 1)));
          std::string val =
              std::string(trim_view(std::string_view(t).substr(colon2 + 1)));
          if (key == "CUSTOM_ID" || key == "ID")
            doc.get(last_heading).label = val;
        }
      }
      ++i;
      continue;
    }

    if (L.blank) {
      flush_para();
      close_lists();
      ++i;
      continue;
    }

    // Comments
    if (starts_with(t, "# ") || t == "#") {
      ++i;
      continue;
    }

    // Meta keywords
    if (starts_with_ci(t, "#+TITLE:")) {
      doc.meta.title =
          std::string(trim_view(std::string_view(t).substr(8)));
      ++i;
      continue;
    }
    if (starts_with_ci(t, "#+AUTHOR:")) {
      doc.meta.author =
          std::string(trim_view(std::string_view(t).substr(9)));
      ++i;
      continue;
    }
    if (starts_with_ci(t, "#+LANGUAGE:") || starts_with_ci(t, "#+LANG:")) {
      size_t col = t.find(':');
      doc.meta.lang =
          std::string(trim_view(std::string_view(t).substr(col + 1)));
      ++i;
      continue;
    }

    // Begin blocks
    if (starts_with_ci(t, "#+BEGIN_SRC")) {
      flush_para();
      close_lists();
      std::string_view rest = trim_view(std::string_view(t).substr(11));
      std::string lang;
      size_t sp = 0;
      while (sp < rest.size() && !is_ws(rest[sp])) ++sp;
      lang = std::string(rest.substr(0, sp));
      block_node = doc.create(NodeKind::CodeBlock, doc.root);
      doc.get(block_node).src = lang;
      block = BlockKind::Src;
      block_acc.clear();
      ++i;
      continue;
    }
    if (starts_with_ci(t, "#+BEGIN_EXAMPLE")) {
      flush_para();
      close_lists();
      block_node = doc.create(NodeKind::CodeBlock, doc.root);
      block = BlockKind::Example;
      block_acc.clear();
      ++i;
      continue;
    }
    if (starts_with_ci(t, "#+BEGIN_QUOTE")) {
      flush_para();
      close_lists();
      block_node = doc.create(NodeKind::BlockQuote, doc.root);
      block = BlockKind::Quote;
      block_acc.clear();
      ++i;
      continue;
    }

    // Other #+ lines ignored
    if (starts_with(t, "#+")) {
      ++i;
      continue;
    }

    if (starts_with_ci(t, ":PROPERTIES:")) {
      in_props = true;
      ++i;
      continue;
    }

    // Horizontal rule
    if (is_hr_line(t)) {
      flush_para();
      close_lists();
      doc.create(NodeKind::HorizontalRule, doc.root);
      ++i;
      continue;
    }

    // Headings
    HeadingParts hp;
    if (parse_heading_line(L, hp)) {
      flush_para();
      close_lists();
      NodeId n = doc.create(NodeKind::Header, doc.root);
      doc.get(n).level = hp.level;
      doc.get(n).src = hp.todo;
      doc.get(n).tags = hp.tags;
      parse_org_inlines(doc.get(n), hp.title);
      last_heading = n;
      ++i;
      continue;
    }

    // Table
    if (is_table_row(t) && !is_table_sep(t)) {
      flush_para();
      close_lists();
      NodeId table = doc.create(NodeKind::Table, doc.root);
      bool header_done = false;
      while (i < lines.size() && is_table_row(lines[i].text)) {
        if (is_table_sep(lines[i].text)) {
          header_done = true;
          ++i;
          continue;
        }
        auto cells = split_table_cells(lines[i].text);
        NodeId row = doc.create(NodeKind::TableRow, table);
        for (const auto& c : cells) {
          NodeId cell = doc.create(NodeKind::TableCell, row);
          // First row before separator → header cells
          if (!header_done) doc.get(cell).level = 1;
          parse_org_inlines(doc.get(cell), c);
        }
        ++i;
      }
      continue;
    }

    // List item
    ListLead lead = parse_list_lead(t);
    if (lead.ok) {
      flush_para();
      NodeId list = ensure_list(L.indent, lead.ordered);
      NodeId item = doc.create(NodeKind::ListItem, list);
      if (lead.ordered) doc.get(item).level = lead.number;
      if (lead.checkbox)
        doc.get(item).label = lead.checked ? "task-done" : "task";
      parse_org_inlines(doc.get(item), lead.body);
      ++i;
      continue;
    }

    // Ordinary paragraph text
    if (!list_frames.empty()) {
      // continuation under last list item
      if (!para_buf.empty()) para_buf.push_back(' ');
      para_buf += L.text;
    } else {
      if (!para_buf.empty()) para_buf.push_back(' ');
      para_buf += L.text;
    }
    ++i;
  }
  flush_block();
  flush_para();
  return doc;
}

// --- serialize -------------------------------------------------------------

namespace {

void ser_inlines_org(const Node& node, std::string& out) {
  for (const auto& sp : node.spans) {
    switch (sp.kind) {
      case InlineKind::Strong:
        out.push_back('*');
        out += sp.text;
        out.push_back('*');
        break;
      case InlineKind::Emphasis:
        out.push_back('/');
        out += sp.text;
        out.push_back('/');
        break;
      case InlineKind::Code:
        out.push_back('=');
        out += sp.text;
        out.push_back('=');
        break;
      case InlineKind::Strike:
        out.push_back('+');
        out += sp.text;
        out.push_back('+');
        break;
      case InlineKind::Link:
      case InlineKind::Cite: {
        std::string target = sp.href;
        if (sp.kind == InlineKind::Cite && !target.empty() && target[0] != '#')
          target = "#" + target;
        out += "[[";
        out += target;
        if (sp.text != target && !sp.text.empty()) {
          out += "][";
          out += sp.text;
        }
        out += "]]";
        break;
      }
      default:
        out += sp.text;
        break;
    }
  }
}

void ser_node_org(const Document& doc, NodeId id, std::string& out, int list_indent);

void ser_children(const Document& doc, const Node& node, std::string& out,
                  int list_indent) {
  for (NodeId c : node.children) ser_node_org(doc, c, out, list_indent);
}

void ser_node_org(const Document& doc, NodeId id, std::string& out,
                  int list_indent) {
  const Node& node = doc.get(id);
  switch (node.kind) {
    case NodeKind::Document:
      if (!doc.meta.title.empty()) {
        out += "#+TITLE: ";
        out += doc.meta.title;
        out += "\n";
      }
      if (!doc.meta.author.empty()) {
        out += "#+AUTHOR: ";
        out += doc.meta.author;
        out += "\n";
      }
      if (!doc.meta.lang.empty()) {
        out += "#+LANGUAGE: ";
        out += doc.meta.lang;
        out += "\n";
      }
      if (!doc.meta.title.empty() || !doc.meta.author.empty() ||
          !doc.meta.lang.empty())
        out += "\n";
      ser_children(doc, node, out, 0);
      break;

    case NodeKind::Header:
    case NodeKind::Heading: {
      int depth = node.level > 0 ? node.level : 1;
      if (depth > 8) depth = 8;
      out.append(static_cast<size_t>(depth), '*');
      out.push_back(' ');
      if (!node.src.empty()) {
        out += node.src;
        out.push_back(' ');
      }
      ser_inlines_org(node, out);
      if (!node.tags.empty()) {
        out += " :";
        out += node.tags;
        out += ":";
      }
      out += "\n";
      if (!node.label.empty()) {
        out += ":PROPERTIES:\n:CUSTOM_ID: ";
        out += node.label;
        out += "\n:END:\n";
      }
      out += "\n";
      break;
    }

    case NodeKind::Paragraph:
      ser_inlines_org(node, out);
      out += "\n\n";
      break;

    case NodeKind::BlockQuote:
      out += "#+BEGIN_QUOTE\n";
      ser_inlines_org(node, out);
      out += "\n#+END_QUOTE\n\n";
      break;

    case NodeKind::CodeBlock:
      out += "#+BEGIN_SRC";
      if (!node.src.empty()) {
        out.push_back(' ');
        out += node.src;
      }
      out += "\n";
      for (const auto& sp : node.spans) out += sp.text;
      if (out.empty() || out.back() != '\n') out.push_back('\n');
      out += "#+END_SRC\n\n";
      break;

    case NodeKind::HorizontalRule:
      out += "-----\n\n";
      break;

    case NodeKind::List: {
      for (NodeId c : node.children) ser_node_org(doc, c, out, list_indent);
      break;
    }

    case NodeKind::ListItem: {
      out.append(static_cast<size_t>(list_indent), ' ');
      const Node& parent = doc.get(node.parent);
      if (parent.kind == NodeKind::List && parent.level == 1) {
        int n = node.level > 0 ? node.level : 1;
        out += std::to_string(n);
        out += ". ";
      } else {
        out += "- ";
      }
      if (node.label == "task")
        out += "[ ] ";
      else if (node.label == "task-done")
        out += "[X] ";
      ser_inlines_org(node, out);
      out += "\n";
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::List)
          ser_node_org(doc, c, out, list_indent + 2);
        else if (doc.get(c).kind == NodeKind::Paragraph) {
          out.append(static_cast<size_t>(list_indent + 2), ' ');
          ser_inlines_org(doc.get(c), out);
          out += "\n";
        }
      }
      break;
    }

    case NodeKind::Table: {
      for (NodeId rid : node.children) {
        const Node& row = doc.get(rid);
        if (row.kind != NodeKind::TableRow) continue;
        out.push_back('|');
        bool any_header = false;
        for (NodeId cid : row.children) {
          const Node& cell = doc.get(cid);
          if (cell.kind != NodeKind::TableCell) continue;
          if (cell.level > 0) any_header = true;
          out.push_back(' ');
          ser_inlines_org(cell, out);
          out += " |";
        }
        out += "\n";
        if (any_header) {
          // emit a separator after a header row
          out.push_back('|');
          for (NodeId cid : row.children) {
            if (doc.get(cid).kind != NodeKind::TableCell) continue;
            out += "---+";
          }
          if (!out.empty() && out.back() == '+') out.back() = '|';
          out += "\n";
        }
      }
      out += "\n";
      break;
    }

    case NodeKind::Article:
    case NodeKind::Section:
    case NodeKind::Stretch:
    case NodeKind::Columns:
    case NodeKind::Column:
    case NodeKind::Note:
    case NodeKind::Figure:
    case NodeKind::DefList:
    case NodeKind::Biblio:
    case NodeKind::Thread:
      ser_children(doc, node, out, list_indent);
      break;

    default:
      break;
  }
}

}  // namespace

std::string serialize_org(const Document& doc) {
  std::string out;
  if (doc.root != INVALID_NODE) ser_node_org(doc, doc.root, out, 0);
  return out;
}

}  // namespace xanatoo
