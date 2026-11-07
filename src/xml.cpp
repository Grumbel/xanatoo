// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/parse.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xanatoo {
namespace {

// Not a general XML implementation. XML-looking input outside this fixed
// vocabulary is rejected, never interpreted as a document type or entity.
constexpr size_t kMaxDepth = 256;

bool xml_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool xml_name_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool xml_name_cont(char c) {
  return xml_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool legal_xml_char(uint32_t cp) {
  return cp == 9 || cp == 10 || cp == 13 ||
         (cp >= 32 && cp <= 0xd7ff) ||
         (cp >= 0xe000 && cp <= 0xfffd) ||
         (cp >= 0x10000 && cp <= 0x10ffff);
}

// Check XML 1.0 character range and well-formed UTF-8, including overlong
// encodings, surrogates and non-scalar values. No locale or XML library.
bool valid_xml_utf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char a = static_cast<unsigned char>(s[i]);
    uint32_t cp = a;
    size_t len = 1;
    if (a >= 0xc2 && a <= 0xdf) {
      cp = a & 0x1f;
      len = 2;
    } else if (a >= 0xe0 && a <= 0xef) {
      cp = a & 0x0f;
      len = 3;
    } else if (a >= 0xf0 && a <= 0xf4) {
      cp = a & 0x07;
      len = 4;
    } else if (a >= 0x80) {
      return false;
    }
    if (i + len > s.size()) return false;
    for (size_t j = 1; j < len; ++j) {
      const unsigned char b = static_cast<unsigned char>(s[i + j]);
      if ((b & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (b & 0x3f);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
        (len == 4 && cp < 0x10000) || !legal_xml_char(cp))
      return false;
    i += len;
  }
  return true;
}

void append_utf8(uint32_t cp, std::string& out) {
  if (cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 63)));
  } else if (cp <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (cp & 63)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
}

void escape_xml(std::string_view value, std::string& out, bool attribute) {
  if (!valid_xml_utf8(value))
    throw std::invalid_argument("XML export: invalid UTF-8 or XML character");
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"':
        if (attribute) out += "&quot;";
        else out.push_back(c);
        break;
      case '\'':
        if (attribute) out += "&apos;";
        else out.push_back(c);
        break;
      case '\r': out += "&#13;"; break;
      case '\n':
        if (attribute) out += "&#10;";
        else out.push_back(c);
        break;
      case '\t':
        if (attribute) out += "&#9;";
        else out.push_back(c);
        break;
      default: out.push_back(c); break;
    }
  }
}

void attr(std::string& out, std::string_view name, std::string_view value) {
  out += ' ';
  out += name;
  out += "=\"";
  escape_xml(value, out, true);
  out += '"';
}

void attr_num(std::string& out, std::string_view name, uint64_t value) {
  attr(out, name, std::to_string(value));
}

std::string float_string(float value) {
  if (!std::isfinite(value))
    throw std::invalid_argument("XML export: non-finite scale");
  // GCC 9's libstdc++ (used by ArkOS/R36S) only implements the integer
  // charconv overloads. Use the classic locale and enough digits to recover
  // the exact float, independently of the user's numeric locale.
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
  return out.str();
}

const char* block_tag(const Node& n) {
  switch (n.kind) {
    case NodeKind::Document: return "doc";
    case NodeKind::Heading: return "h";
    case NodeKind::Header: return "header";
    case NodeKind::Article: return "article";
    case NodeKind::Section: return "section";
    case NodeKind::Paragraph: return "p";
    case NodeKind::List: return n.level == 1 ? "ol" : "ul";
    case NodeKind::ListItem: return "li";
    case NodeKind::Image: return "image";
    case NodeKind::Stretch: return "stretch";
    case NodeKind::Columns: return "columns";
    case NodeKind::Column: return "column";
    case NodeKind::BlockQuote: return "quote";
    case NodeKind::HorizontalRule: return "hr";
    case NodeKind::Transclude: return "include";
    case NodeKind::CodeBlock: return "codeblock";
    case NodeKind::Note: return "note";
    case NodeKind::DefList: return "deflist";
    case NodeKind::Term: return "term";
    case NodeKind::Definition: return "def";
    case NodeKind::Figure: return "figure";
    case NodeKind::Caption: return "caption";
    case NodeKind::Biblio: return "biblio";
    case NodeKind::BiblioEntry: return "ref";
    case NodeKind::Table: return "table";
    case NodeKind::TableRow: return "tr";
    case NodeKind::TableCell: return n.level == 1 ? "th" : "td";
    case NodeKind::Thread: return "thread";
    case NodeKind::Message: return "message";
    case NodeKind::Footnote: return "footnote";
    case NodeKind::Collection: return "collection";
    case NodeKind::Chrome: return "chrome";
    case NodeKind::Aside: return "aside";
  }
  throw std::invalid_argument("XML export: unknown node kind");
}

bool block_kind(std::string_view s, NodeKind& kind, int& level) {
  // Spellings are intentionally fixed; <image> is a block, <img> an inline.
  constexpr std::pair<std::string_view, NodeKind> names[] = {
      {"doc", NodeKind::Document}, {"h", NodeKind::Heading},
      {"header", NodeKind::Header}, {"article", NodeKind::Article},
      {"section", NodeKind::Section}, {"p", NodeKind::Paragraph},
      {"ul", NodeKind::List}, {"ol", NodeKind::List},
      {"li", NodeKind::ListItem}, {"image", NodeKind::Image},
      {"stretch", NodeKind::Stretch}, {"columns", NodeKind::Columns},
      {"column", NodeKind::Column}, {"quote", NodeKind::BlockQuote},
      {"hr", NodeKind::HorizontalRule}, {"include", NodeKind::Transclude},
      {"codeblock", NodeKind::CodeBlock}, {"note", NodeKind::Note},
      {"deflist", NodeKind::DefList}, {"term", NodeKind::Term},
      {"def", NodeKind::Definition}, {"figure", NodeKind::Figure},
      {"caption", NodeKind::Caption}, {"biblio", NodeKind::Biblio},
      {"ref", NodeKind::BiblioEntry}, {"table", NodeKind::Table},
      {"tr", NodeKind::TableRow}, {"td", NodeKind::TableCell},
      {"th", NodeKind::TableCell}, {"thread", NodeKind::Thread},
      {"message", NodeKind::Message}, {"footnote", NodeKind::Footnote},
      {"collection", NodeKind::Collection}, {"chrome", NodeKind::Chrome},
      {"aside", NodeKind::Aside}};
  for (auto [name, value] : names) {
    if (name != s) continue;
    kind = value;
    level = (s == "ol" || s == "th") ? 1 : 0;
    return true;
  }
  return false;
}

bool inline_kind(std::string_view s, InlineKind& kind) {
  constexpr std::pair<std::string_view, InlineKind> names[] = {
      {"text", InlineKind::Text}, {"em", InlineKind::Emphasis},
      {"strong", InlineKind::Strong}, {"strike", InlineKind::Strike},
      {"a", InlineKind::Link}, {"code", InlineKind::Code},
      {"cite", InlineKind::Cite}, {"fnref", InlineKind::FootnoteRef},
      {"img", InlineKind::Image}, {"math", InlineKind::Math}};
  for (auto [name, value] : names) {
    if (s == name) {
      kind = value;
      return true;
    }
  }
  return false;
}

const char* inline_tag(InlineKind kind) {
  switch (kind) {
    case InlineKind::Text: return "text";
    case InlineKind::Emphasis: return "em";
    case InlineKind::Strong: return "strong";
    case InlineKind::Strike: return "strike";
    case InlineKind::Link: return "a";
    case InlineKind::Code: return "code";
    case InlineKind::Cite: return "cite";
    case InlineKind::FootnoteRef: return "fnref";
    case InlineKind::Image: return "img";
    case InlineKind::Math: return "math";
  }
  throw std::invalid_argument("XML export: unknown inline kind");
}

bool accepts_inline(NodeKind kind) {
  switch (kind) {
    case NodeKind::Heading:
    case NodeKind::Header:
    case NodeKind::Paragraph:
    case NodeKind::ListItem:
    case NodeKind::BlockQuote:
    case NodeKind::CodeBlock:
    case NodeKind::Note:
    case NodeKind::Term:
    case NodeKind::Definition:
    case NodeKind::Caption:
    case NodeKind::BiblioEntry:
    case NodeKind::TableCell:
    case NodeKind::Message: return true;
    default: return false;
  }
}

void write_inlines(const Node& node, std::string& out) {
  if (!accepts_inline(node.kind) && !node.spans.empty())
    throw std::invalid_argument("XML export: inline text on a container node");
  for (const auto& sp : node.spans) {
    const bool plain = sp.kind == InlineKind::Text &&
        sp.role == InlineRole::Normal && sp.href.empty() && sp.rel.empty() &&
        sp.datetime.empty() && sp.target == INVALID_NODE && sp.src.empty() &&
        sp.alt.empty() && !sp.width && !sp.height && !sp.display;
    if (plain) {
      escape_xml(sp.text, out, false);
      continue;
    }
    out += '<';
    out += inline_tag(sp.kind);
    if (sp.role == InlineRole::Metadata) attr(out, "role", "metadata");
    if (!sp.href.empty()) attr(out, "href", sp.href);
    if (!sp.rel.empty()) attr(out, "rel", sp.rel);
    if (!sp.datetime.empty()) attr(out, "datetime", sp.datetime);
    if (sp.target != INVALID_NODE) attr_num(out, "target", sp.target);
    if (!sp.src.empty()) attr(out, "src", sp.src);
    if (!sp.alt.empty()) attr(out, "alt", sp.alt);
    if (sp.width) attr_num(out, "width", sp.width);
    if (sp.height) attr_num(out, "height", sp.height);
    if (sp.display) attr(out, "display", "true");
    if (is_inline_object(sp.kind)) {
      out += "/>";
    } else {
      out += '>';
      escape_xml(sp.text, out, false);
      out += "</";
      out += inline_tag(sp.kind);
      out += '>';
    }
  }
}

void write_node(const Document& doc, NodeId id, std::string& out,
                size_t depth, bool pretty) {
  if (depth >= kMaxDepth)
    throw std::invalid_argument("XML export: nesting too deep");
  const Node& node = doc.get(id);
  if (node.id != id)
    throw std::invalid_argument("XML export: invalid node ID");
  const char* tag = block_tag(node);
  if (pretty) out.append(depth * 2, ' ');
  out += '<';
  out += tag;
  attr_num(out, "nid", node.id);
  const std::string_view name = tag;
  if (node.level && !(node.level == 1 &&
                      (name == "ol" || name == "th")))
    attr(out, "level", std::to_string(node.level));
  if (node.col_span != 1) attr(out, "colspan", std::to_string(node.col_span));
  if (node.row_span != 1) attr(out, "rowspan", std::to_string(node.row_span));
  if (!node.alt.empty()) attr(out, "alt", node.alt);
  if (!node.src.empty()) attr(out, "src", node.src);
  if (!node.expanded) attr(out, "expanded", "false");
  if (!node.summary.empty()) attr(out, "summary", node.summary);
  if (!node.float_side.empty()) attr(out, "float", node.float_side);
  if (node.display_scale != 1.f) attr(out, "scale", float_string(node.display_scale));
  if (!node.label.empty()) attr(out, "label", node.label);
  if (!node.tags.empty()) attr(out, "tags", node.tags);
  if (!node.style.empty()) attr(out, "style", node.style);
  if (node.kind == NodeKind::Document) {
    if (!doc.meta.title.empty()) attr(out, "title", doc.meta.title);
    if (!doc.meta.lang.empty()) attr(out, "lang", doc.meta.lang);
    if (!doc.meta.author.empty()) attr(out, "author", doc.meta.author);
  }
  const bool has_front = node.kind == NodeKind::Document &&
                         !doc.meta.front_matter.empty();
  if (node.children.empty() && node.spans.empty() && !has_front) {
    out += "/>";
    if (pretty) out += '\n';
    return;
  }
  out += '>';
  const bool multiline = node.spans.empty() && (has_front || !node.children.empty());
  if (multiline && pretty) out += '\n';
  if (has_front) {
    if (multiline && pretty) out.append((depth + 1) * 2, ' ');
    out += "<front-matter>";
    escape_xml(doc.meta.front_matter, out, false);
    out += "</front-matter>";
    if (multiline && pretty) out += '\n';
  }
  write_inlines(node, out);
  for (NodeId child : node.children)
    write_node(doc, child, out, depth + 1, multiline && pretty);
  if (multiline && pretty) out.append(depth * 2, ' ');
  out += "</";
  out += tag;
  out += '>';
  if (pretty) out += '\n';
}

template<class T>
bool parse_unsigned(std::string_view s, T& value) {
  if (s.empty() || s.front() == '-') return false;
  T result{};
  auto status = std::from_chars(s.data(), s.data() + s.size(), result);
  if (status.ec != std::errc{} || status.ptr != s.data() + s.size()) return false;
  value = result;
  return true;
}

class XmlReader {
public:
  explicit XmlReader(std::string_view source) : source_(source) {}

  Document read() {
    skip_space();
    if (!starts("<doc") || (pos_ + 4 < source_.size() &&
                           xml_name_cont(source_[pos_ + 4])))
      fail("expected <doc> root");
    read_node(INVALID_NODE, 0);
    skip_space();
    if (pos_ != source_.size()) fail("trailing content");
    for (Node& n : doc_.nodes) {
      for (InlineSpan& span : n.spans) {
        if (span.target == INVALID_NODE) continue;
        auto it = ids_.find(span.target);
        if (it == ids_.end()) fail("inline target refers to unknown nid");
        span.target = it->second;
      }
    }
    return std::move(doc_);
  }

private:
  using Attributes = std::vector<std::pair<std::string, std::string>>;
  struct StartTag {
    std::string name;
    Attributes attrs;
    bool empty = false;
  };

  [[noreturn]] void fail(std::string_view why) const {
    throw std::invalid_argument("XML at byte " + std::to_string(pos_) +
                                ": " + std::string(why));
  }

  bool starts(std::string_view prefix) const {
    return source_.substr(pos_, prefix.size()) == prefix;
  }

  void skip_space() {
    while (pos_ < source_.size() && xml_space(source_[pos_])) ++pos_;
  }

  std::string read_name() {
    const size_t begin = pos_;
    if (pos_ >= source_.size() || !xml_name_start(source_[pos_]))
      fail("expected name (no namespace prefixes)");
    do { ++pos_; }
    while (pos_ < source_.size() && xml_name_cont(source_[pos_]));
    return std::string(source_.substr(begin, pos_ - begin));
  }

  std::string read_escaped(char terminator) {
    std::string result;
    while (pos_ < source_.size() && source_[pos_] != terminator) {
      if (starts("]]>") && terminator == '<')
        fail("CDATA end delimiter is not allowed in text");
      if (source_[pos_] == '<' && terminator != '<') fail("unescaped <");
      if (source_[pos_] != '&') {
        const char c = source_[pos_++];
        if (terminator == '<' && c == '\r') {
          if (pos_ < source_.size() && source_[pos_] == '\n') ++pos_;
          result += '\n';  // XML's physical newline normalization
        } else if (terminator != '<' &&
                   (c == '\t' || c == '\r' || c == '\n')) {
          if (c == '\r' && pos_ < source_.size() && source_[pos_] == '\n')
            ++pos_;
          result += ' ';  // XML attribute whitespace normalization
        } else {
          result += c;
        }
        continue;
      }
      ++pos_;
      const size_t begin = pos_;
      while (pos_ < source_.size() && source_[pos_] != ';' &&
             pos_ - begin <= 32) ++pos_;
      if (pos_ >= source_.size() || source_[pos_] != ';')
        fail("unterminated entity");
      const std::string_view entity = source_.substr(begin, pos_ - begin);
      ++pos_;
      if (entity == "amp") result += '&';
      else if (entity == "lt") result += '<';
      else if (entity == "gt") result += '>';
      else if (entity == "quot") result += '"';
      else if (entity == "apos") result += '\'';
      else if (!entity.empty() && entity[0] == '#') {
        const bool hex = entity.size() > 1 &&
                         (entity[1] == 'x' || entity[1] == 'X');
        const std::string_view digits = entity.substr(hex ? 2 : 1);
        uint32_t cp = 0;
        auto r = std::from_chars(digits.data(), digits.data() + digits.size(),
                                 cp, hex ? 16 : 10);
        if (digits.empty() || r.ec != std::errc{} ||
            r.ptr != digits.data() + digits.size() || !legal_xml_char(cp))
          fail("invalid numeric character reference");
        append_utf8(cp, result);
      } else {
        fail("unknown entity (no custom entities)");
      }
    }
    if (!valid_xml_utf8(result)) fail("invalid UTF-8 or XML character");
    return result;
  }

  StartTag read_start() {
    if (!starts("<")) fail("expected opening tag");
    ++pos_;
    StartTag tag;
    tag.name = read_name();
    std::unordered_set<std::string> used;
    while (true) {
      const size_t before_space = pos_;
      skip_space();
      if (starts("/>")) {
        pos_ += 2;
        tag.empty = true;
        return tag;
      }
      if (starts(">")) {
        ++pos_;
        return tag;
      }
      if (before_space == pos_) fail("whitespace required before attribute");
      const std::string name = read_name();
      if (!used.insert(name).second) fail("duplicate attribute");
      skip_space();
      if (!starts("=")) fail("expected = after attribute");
      ++pos_;
      skip_space();
      if (pos_ >= source_.size() ||
          (source_[pos_] != '"' && source_[pos_] != '\''))
        fail("attribute must be quoted");
      const char quote = source_[pos_++];
      std::string value = read_escaped(quote);
      if (pos_ >= source_.size()) fail("unterminated attribute");
      ++pos_;
      tag.attrs.emplace_back(name, std::move(value));
    }
  }

  void read_close(std::string_view name) {
    if (!starts("</")) fail("expected closing tag");
    pos_ += 2;
    const std::string found = read_name();
    if (found != name)
      fail("mismatched closing tag: expected " + std::string(name) +
           ", found " + found);
    skip_space();
    if (!starts(">")) fail("expected > after closing tag");
    ++pos_;
  }

  std::string read_text() {
    return read_escaped('<');
  }

  bool whitespace_only(std::string_view text) const {
    for (char c : text) if (!xml_space(c)) return false;
    return true;
  }

  template<class T>
  T number(std::string_view value, std::string_view name) {
    T result{};
    if (!parse_unsigned(value, result)) fail("invalid unsigned attribute: " + std::string(name));
    return result;
  }

  int signed_number(std::string_view value) {
    int result = 0;
    auto r = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || r.ec != std::errc{} ||
        r.ptr != value.data() + value.size()) fail("invalid integer attribute");
    return result;
  }

  float scale(std::string_view value) {
    float result = 0.f;
    std::istringstream in{std::string(value)};
    in.imbue(std::locale::classic());
    in >> std::noskipws >> result;
    if (value.empty() || !in || in.peek() != std::char_traits<char>::eof() ||
        !std::isfinite(result))
      fail("invalid scale attribute");
    return result;
  }

  bool boolean(std::string_view value) {
    if (value == "true") return true;
    if (value == "false") return false;
    fail("expected true or false");
  }

  void node_attribute(Node& node, const std::string& key,
                      const std::string& value, NodeId id) {
    if (key == "nid") {
      const NodeId old = number<NodeId>(value, "nid");
      if (old == INVALID_NODE || !ids_.emplace(old, id).second)
        fail("duplicate or zero nid");
    } else if (key == "level") node.level = signed_number(value);
    else if (key == "colspan") node.col_span = signed_number(value);
    else if (key == "rowspan") node.row_span = signed_number(value);
    else if (key == "alt") node.alt = value;
    else if (key == "src") node.src = value;
    else if (key == "expanded") node.expanded = boolean(value);
    else if (key == "summary") node.summary = value;
    else if (key == "float") node.float_side = value;
    else if (key == "scale") node.display_scale = scale(value);
    else if (key == "label") node.label = value;
    else if (key == "tags") node.tags = value;
    else if (key == "style") node.style = value;
    else if (node.kind == NodeKind::Document && key == "title") doc_.meta.title = value;
    else if (node.kind == NodeKind::Document && key == "lang") doc_.meta.lang = value;
    else if (node.kind == NodeKind::Document && key == "author") doc_.meta.author = value;
    else fail("unknown node attribute: " + key);
  }

  void read_inline(NodeId id, StartTag tag, InlineKind kind) {
    InlineSpan span;
    span.kind = kind;
    for (auto& [key, value] : tag.attrs) {
      if (key == "role") {
        if (value != "metadata" && value != "normal") fail("unknown inline role");
        span.role = value == "metadata" ? InlineRole::Metadata : InlineRole::Normal;
      } else if (key == "href") span.href = value;
      else if (key == "rel") span.rel = value;
      else if (key == "datetime") span.datetime = value;
      else if (key == "target") span.target = number<NodeId>(value, "target");
      else if (key == "src") span.src = value;
      else if (key == "alt") span.alt = value;
      else if (key == "width") span.width = number<uint16_t>(value, "width");
      else if (key == "height") span.height = number<uint16_t>(value, "height");
      else if (key == "display") span.display = boolean(value);
      else fail("unknown inline attribute: " + key);
    }
    if (is_inline_object(kind)) {
      if (!tag.empty) {
        read_close(tag.name);
      }
      span.text = kInlineImageText;
    } else if (tag.empty) {
      span.text.clear();
    } else {
      span.text = read_text();
      read_close(tag.name); // nested inline markup is not representable
    }
    doc_.get(id).spans.push_back(std::move(span));
  }

  void read_front_matter(StartTag tag) {
    if (!tag.attrs.empty()) fail("front-matter has no attributes");
    if (seen_front_matter_) fail("duplicate front-matter");
    seen_front_matter_ = true;
    if (tag.empty) return;
    doc_.meta.front_matter = read_text();
    read_close("front-matter");
  }

  void read_node(NodeId parent, size_t depth) {
    if (depth >= kMaxDepth) fail("nesting too deep");
    StartTag tag = read_start();
    NodeKind kind{};
    int level = 0;
    if (!block_kind(tag.name, kind, level)) fail("unknown block element: " + tag.name);
    if ((parent == INVALID_NODE) != (kind == NodeKind::Document))
      fail("doc must be the sole root element");
    const NodeId id = doc_.create(kind, parent);
    doc_.get(id).level = level;
    for (const auto& [key, value] : tag.attrs)
      node_attribute(doc_.get(id), key, value, id);
    if (tag.empty) return;
    while (true) {
      if (pos_ >= source_.size()) fail("unclosed element");
      if (starts("</")) {
        read_close(tag.name);
        return;
      }
      if (starts("<")) {
        if (kind == NodeKind::Document && starts("<front-matter")) {
          StartTag sub = read_start();
          if (sub.name != "front-matter") fail("invalid metadata element");
          read_front_matter(std::move(sub));
          continue;
        }
        // Only look at the syntactic name; read_start() consumes attributes.
        const size_t saved = pos_;
        ++pos_;
        const std::string name = read_name();
        pos_ = saved;
        InlineKind inline_type{};
        if (accepts_inline(kind) && inline_kind(name, inline_type)) {
          read_inline(id, read_start(), inline_type);
        } else {
          read_node(id, depth + 1);
        }
      } else {
        std::string value = read_text();
        if (accepts_inline(kind)) {
          if (!value.empty()) doc_.append_text(id, value);
        } else if (!whitespace_only(value)) {
          fail("text is not allowed in this container");
        }
      }
    }
  }

  std::string_view source_;
  size_t pos_ = 0;
  Document doc_;
  std::unordered_map<NodeId, NodeId> ids_;
  bool seen_front_matter_ = false;
};

}  // namespace

bool path_uses_xml(std::string_view path) {
  if (path.size() < 4) return false;
  const std::string_view ext = path.substr(path.size() - 4);
  return ext[0] == '.' &&
      (ext[1] == 'x' || ext[1] == 'X') &&
      (ext[2] == 'm' || ext[2] == 'M') &&
      (ext[3] == 'l' || ext[3] == 'L');
}

Document parse_xml(std::string_view source) {
  return XmlReader(source).read();
}

std::string serialize_xml(const Document& doc) {
  if (doc.root == INVALID_NODE || doc.get(doc.root).kind != NodeKind::Document)
    throw std::invalid_argument("XML export: missing document root");
  std::string result;
  write_node(doc, doc.root, result, 0, true);
  return result;
}

}  // namespace xanatoo
