// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Forgiving HTML fragment → document nodes (see html_min.hpp). A streaming
// tokenizer feeds a builder that keeps a stack of open block frames.
// Paragraphs are created lazily on the first visible text, so wrapper
// markup never leaves empty blocks behind.

#include "xanatoo/html_min.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

namespace {

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

template <size_t N>
bool one_of(std::string_view t, const std::array<std::string_view, N>& set) {
  return std::find(set.begin(), set.end(), t) != set.end();
}

// Dropped together with everything inside them.
constexpr std::array<std::string_view, 12> kSkipTags = {
    "script", "style",  "iframe", "noscript", "svg",     "template",
    "head",   "object", "select", "textarea", "button",  "title"};

constexpr std::array<std::string_view, 14> kVoidTags = {
    "br",   "hr",    "img",  "meta",  "link",   "input", "source",
    "wbr",  "col",   "area", "base",  "embed",  "param", "track"};

// Unwrapped block wrappers: a paragraph boundary, nothing more.
constexpr std::array<std::string_view, 22> kBlockTags = {
    "p",      "div",    "section", "article", "header",  "footer",
    "aside",  "nav",    "main",    "figure",  "dl",      "dt",
    "dd",     "address", "center", "details", "summary", "fieldset",
    "legend", "caption", "body",   "html"};

// Page chrome, dropped in drop_chrome mode.
constexpr std::array<std::string_view, 5> kChromeTags = {
    "nav", "aside", "form", "footer", "dialog"};

// Class tokens of elements dropped in drop_chrome mode: screen-reader-only
// text, edit links, navigation boxes, share / cookie banners.
constexpr std::array<std::string_view, 9> kChromeClasses = {
    "noprint", "navbox", "sidebar",     "share",        "social",
    "hidden",  "metadata", "toc-toggle", "advertisement"};

// Substrings of class tokens that mark chrome ("vector-dropdown",
// "page-toolbar", "mw-editsection", "cookie-banner", …).
constexpr std::array<std::string_view, 10> kChromeClassParts = {
    "dropdown",   "toolbar", "navbar",       "breadcrumb",    "editsection",
    "cookie",     "sr-only", "visually-hidden", "screen-reader", "skip-link"};

// ARIA landmark roles that are not the content.
constexpr std::array<std::string_view, 6> kChromeRoles = {
    "navigation", "banner", "contentinfo", "search", "complementary", "dialog"};

enum class FrameKind : uint8_t {
  Container,  // List / Table / TableRow: blocks go inside
  Leaf,       // BlockQuote / ListItem / TableCell / Heading / CodeBlock
  Pseudo,     // matched tag inside a leaf, flattened into its text
};

struct Frame {
  std::string tag;
  FrameKind kind = FrameKind::Pseudo;
  NodeId node = INVALID_NODE;
  int items = 0;  // ordered list: last ordinal
  bool pre = false;
  bool layout = false;  // layout <table>: rows / cells are just boundaries
  int strong_add = 0;
  int em_add = 0;
};

class Builder {
 public:
  Builder(Document& doc, NodeId parent, const HtmlOptions& opt)
      : doc_(doc), opt_(opt), root_(parent) {}

  void open(const std::string& tag, std::string_view attrs);
  // A <table> used for page layout (nested tables, role=presentation).
  void open_layout_table() {
    block_boundary();
    Frame f;
    f.tag = "table";
    f.layout = true;
    push(std::move(f));
  }
  void close(const std::string& tag);
  void text(std::string_view raw);
  void finish() {
    close_para();
    while (!frames_.empty()) pop();
  }

 private:
  struct Sec {
    NodeId node;
    int level;
  };

  void open_tag(const std::string& tag, std::string_view attrs);
  bool open_section(const std::string& tag);
  void note_id(std::string id);
  void take_pending_id(NodeId n);
  bool in_container() const;
  bool in_layout_table() const;
  NodeId owner() const;
  bool pre_mode() const;
  NodeId container() const;
  NodeId target() const { return owner() != INVALID_NODE ? owner() : para_; }
  NodeId ensure_target();
  void close_para();
  void finalize(NodeId n);
  void add_break();
  void push_text(NodeId t, InlineKind kind, std::string s,
                 const std::string& href = {});
  void emit(std::string s);
  void pop();
  bool pop_to(std::string_view tag);
  void push(Frame f) {
    close_para();
    if (f.kind == FrameKind::Leaf) take_pending_id(f.node);
    frames_.push_back(std::move(f));
  }
  void block_boundary() {
    if (owner() != INVALID_NODE) add_break();
    else close_para();
  }
  int top_index(std::string_view tag) const;
  char last_char(NodeId n) const;

  Document& doc_;
  const HtmlOptions& opt_;
  NodeId root_;
  std::vector<Frame> frames_;
  std::vector<Sec> secs_;   // open Sections (sections mode)
  std::string pending_id_;  // id= waiting for the next block (anchors mode)
  NodeId para_ = INVALID_NODE;
  int em_ = 0, strong_ = 0, code_ = 0, strike_ = 0;
  std::vector<std::string> links_;  // "" = <a> without href
  bool pre_start_ = false;          // drop the newline right after <pre>
};

NodeId Builder::owner() const {
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
    if (it->kind == FrameKind::Leaf) return it->node;
    if (it->kind == FrameKind::Container) return INVALID_NODE;
  }
  return INVALID_NODE;
}

bool Builder::pre_mode() const {
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
    if (it->pre) return true;
    if (it->kind != FrameKind::Pseudo) return false;
  }
  return false;
}

NodeId Builder::container() const {
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
    if (it->kind == FrameKind::Container) return it->node;
  return secs_.empty() ? root_ : secs_.back().node;
}

// Innermost table frame is a layout table.
bool Builder::in_layout_table() const {
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
    if (it->tag == "table") return it->layout;
  return false;
}

bool Builder::in_container() const {
  for (const Frame& f : frames_)
    if (f.kind == FrameKind::Container) return true;
  return false;
}

void Builder::take_pending_id(NodeId n) {
  if (pending_id_.empty() || n == INVALID_NODE) return;
  if (doc_.get(n).label.empty()) doc_.get(n).label = std::move(pending_id_);
  pending_id_.clear();
}

// An element id becomes the label of the block it sits in (so "#id" links
// jump there), or of the next block when none is open yet.
void Builder::note_id(std::string id) {
  // "task" / "task-done" labels draw list checkboxes.
  if (id.empty() || id == "task" || id == "task-done") return;
  NodeId t = target();
  if (t != INVALID_NODE && doc_.get(t).label.empty())
    doc_.get(t).label = std::move(id);
  else
    pending_id_ = std::move(id);
}

NodeId Builder::ensure_target() {
  if (NodeId t = target(); t != INVALID_NODE) return t;
  NodeId c = container();
  NodeKind ck = doc_.get(c).kind;
  if (ck == NodeKind::Table || ck == NodeKind::TableRow) return INVALID_NODE;
  para_ = doc_.create(ck == NodeKind::List ? NodeKind::ListItem
                                           : NodeKind::Paragraph,
                      c);
  take_pending_id(para_);
  return para_;
}

char Builder::last_char(NodeId n) const {
  if (n == INVALID_NODE) return '\0';
  const auto& spans = doc_.get(n).spans;
  for (auto it = spans.rbegin(); it != spans.rend(); ++it)
    if (!it->text.empty()) return it->text.back();
  return '\0';
}

// Trim trailing whitespace; HN-style "> " paragraphs become quotes.
void Builder::finalize(NodeId n) {
  auto& spans = doc_.get(n).spans;
  while (!spans.empty()) {
    std::string& t = spans.back().text;
    while (!t.empty() && is_space(t.back())) t.pop_back();
    if (!t.empty()) break;
    spans.pop_back();
  }
  Node& node = doc_.get(n);
  if (opt_.gt_quotes && node.kind == NodeKind::Paragraph && !spans.empty() &&
      spans.front().kind == InlineKind::Text && starts_with(spans.front().text, ">")) {
    node.kind = NodeKind::BlockQuote;
    std::string& t = spans.front().text;
    size_t k = 1;
    while (k < t.size() && t[k] == ' ') ++k;
    t.erase(0, k);
  }
}

void Builder::close_para() {
  if (para_ == INVALID_NODE) return;
  finalize(para_);
  para_ = INVALID_NODE;
}

void Builder::push_text(NodeId t, InlineKind kind, std::string s,
                        const std::string& href) {
  auto& spans = doc_.get(t).spans;
  if (!spans.empty() && spans.back().kind == kind && spans.back().href == href) {
    spans.back().text += s;
    return;
  }
  InlineSpan span;
  span.kind = kind;
  span.text = std::move(s);
  span.href = href;
  spans.push_back(std::move(span));
}

void Builder::add_break() {
  NodeId t = target();
  if (t == INVALID_NODE) return;
  char c = last_char(t);
  if (c == '\0' || c == '\n') return;
  auto& spans = doc_.get(t).spans;
  while (!spans.empty() && !spans.back().text.empty() &&
         spans.back().text.back() == ' ')
    spans.back().text.pop_back();
  push_text(t, InlineKind::Text, "\n");
}

void Builder::emit(std::string s) {
  if (s.empty()) return;
  NodeId t = ensure_target();
  if (t == INVALID_NODE) return;
  std::string href;
  for (auto it = links_.rbegin(); it != links_.rend(); ++it)
    if (!it->empty()) {
      href = *it;
      break;
    }
  InlineKind kind = InlineKind::Text;
  if (pre_mode()) kind = InlineKind::Text;
  else if (!href.empty()) kind = InlineKind::Link;
  else if (code_ > 0) kind = InlineKind::Code;
  else if (strong_ > 0) kind = InlineKind::Strong;
  else if (em_ > 0) kind = InlineKind::Emphasis;
  else if (strike_ > 0) kind = InlineKind::Strike;
  push_text(t, kind, std::move(s), kind == InlineKind::Link ? href : std::string{});
}

void Builder::text(std::string_view raw) {
  std::string s = html_decode_entities(raw);
  if (pre_mode()) {
    if (pre_start_ && !s.empty() && s[0] == '\n') s.erase(0, 1);
    if (pre_start_ && !s.empty()) pre_start_ = false;
    emit(std::move(s));
    return;
  }
  std::string out;
  bool space = false;
  for (char c : s) {
    // U+00A0 stays: it is deliberate spacing.
    if (is_space(c)) {
      space = true;
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  if (space) out.push_back(' ');
  char last = last_char(target());
  if (!out.empty() && out[0] == ' ' &&
      (last == '\0' || last == ' ' || last == '\n'))
    out.erase(0, 1);
  emit(std::move(out));
}

int Builder::top_index(std::string_view tag) const {
  for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i)
    if (frames_[static_cast<size_t>(i)].tag == tag) return i;
  return -1;
}

void Builder::pop() {
  close_para();
  Frame f = std::move(frames_.back());
  frames_.pop_back();
  if (f.kind == FrameKind::Leaf) finalize(f.node);
  strong_ -= f.strong_add;
  em_ -= f.em_add;
  if (f.pre) {
    pre_start_ = false;
    if (f.kind == FrameKind::Pseudo) add_break();
  }
  if (f.kind == FrameKind::Pseudo && f.tag[0] == 'h') add_break();
}

bool Builder::pop_to(std::string_view tag) {
  int i = top_index(tag);
  if (i < 0) return false;
  while (static_cast<int>(frames_.size()) > i) pop();
  return true;
}

void Builder::open(const std::string& tag, std::string_view attrs) {
  if (!opt_.anchors) {
    open_tag(tag, attrs);
    return;
  }
  std::string id = html_attr(attrs, "id");
  if (id.empty() && tag == "a") id = html_attr(attrs, "name");
  // An element's own id beats a wrapper's (<section id=x><h2 id=Title>).
  if (!id.empty()) pending_id_.clear();
  open_tag(tag, attrs);
  note_id(std::move(id));
}

// h1–h6 at block level in sections mode: close sections of the same or a
// deeper level, open a Section with a Header title.
bool Builder::open_section(const std::string& tag) {
  if (!opt_.sections || owner() != INVALID_NODE || in_container()) return false;
  const int level = tag[1] - '0';
  close_para();
  while (!secs_.empty() && secs_.back().level >= level) secs_.pop_back();
  NodeId sec = doc_.create(NodeKind::Section, container());
  doc_.get(sec).level = level;
  secs_.push_back({sec, level});
  Frame f;
  f.tag = tag;
  f.kind = FrameKind::Leaf;
  f.node = doc_.create(NodeKind::Header, sec);
  doc_.get(f.node).level = level;
  push(std::move(f));
  return true;
}

void Builder::open_tag(const std::string& tag, std::string_view attrs) {
  const bool in_leaf = owner() != INVALID_NODE;

  // Inline markup.
  if (tag == "a") {
    std::string href = html_attr(attrs, "href");
    if (!href.empty()) href = resolve_url(opt_.base_url, href);
    if (opt_.anchors && !href.empty() && href[0] != '#') {
      // Same-page link → in-document "#id".
      std::string_view page =
          opt_.page_url.empty() ? opt_.base_url : opt_.page_url;
      page = page.substr(0, page.find('#'));
      if (!page.empty() && href.size() > page.size() &&
          starts_with(href, page) && href[page.size()] == '#')
        href.erase(0, page.size());
    }
    links_.push_back(std::move(href));
    return;
  }
  if (tag == "em" || tag == "i" || tag == "cite" || tag == "dfn" ||
      tag == "var") {
    ++em_;
    return;
  }
  if (tag == "strong" || tag == "b") {
    ++strong_;
    return;
  }
  if (tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp") {
    ++code_;
    return;
  }
  if (tag == "del" || tag == "s" || tag == "strike") {
    ++strike_;
    return;
  }
  if (tag == "br") {
    if (pre_mode()) emit("\n");
    else if (!in_leaf && para_ != INVALID_NODE && last_char(para_) == '\n')
      close_para();  // <br><br> separates paragraphs
    else add_break();
    return;
  }
  if (tag == "img") {
    std::string src = html_attr(attrs, "src");
    if (src.empty()) return;
    // alt="" marks a decorative image.
    if (opt_.drop_chrome && html_has_attr(attrs, "alt") &&
        html_attr(attrs, "alt").empty())
      return;
    // 1×1 spacers / tracking pixels.
    if (opt_.drop_chrome) {
      std::string w = html_attr(attrs, "width"), h = html_attr(attrs, "height");
      if ((!w.empty() && std::atoi(w.c_str()) <= 2) ||
          (!h.empty() && std::atoi(h.c_str()) <= 2))
        return;
    }
    std::string alt = html_to_plain(html_attr(attrs, "alt"));
    NodeId t = ensure_target();
    if (t == INVALID_NODE) return;
    push_text(t, InlineKind::Link, alt.empty() ? "[image]" : "[image: " + alt + "]",
              resolve_url(opt_.base_url, src));
    return;
  }

  // Lists.
  if (tag == "ul" || tag == "ol") {
    Frame f;
    f.tag = tag;
    NodeId host = owner();
    if (host != INVALID_NODE && doc_.get(host).kind == NodeKind::ListItem &&
        top_index("li") >= 0 &&
        frames_[static_cast<size_t>(top_index("li"))].node == host) {
      f.kind = FrameKind::Container;
      f.node = doc_.create(NodeKind::List, host);
    } else if (in_leaf) {
      add_break();  // list inside a quote / cell: "• " lines
    } else {
      close_para();
      f.kind = FrameKind::Container;
      f.node = doc_.create(NodeKind::List, container());
    }
    if (f.node != INVALID_NODE) doc_.get(f.node).level = tag == "ol" ? 1 : 0;
    push(std::move(f));
    return;
  }
  if (tag == "li") {
    if (!frames_.empty() && frames_.back().tag == "li") pop();
    if (!frames_.empty() && frames_.back().kind == FrameKind::Container &&
        doc_.get(frames_.back().node).kind == NodeKind::List) {
      Frame& list = frames_.back();
      Frame f;
      f.tag = "li";
      f.kind = FrameKind::Leaf;
      f.node = doc_.create(NodeKind::ListItem, list.node);
      doc_.get(f.node).level = doc_.get(list.node).level == 1 ? ++list.items : 0;
      push(std::move(f));
      return;
    }
    block_boundary();
    emit("• ");
    Frame f;
    f.tag = "li";
    push(std::move(f));
    return;
  }

  // Quotes, code, headings.
  if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' &&
      open_section(tag))
    return;
  if (tag == "blockquote" || tag == "pre" ||
      (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')) {
    Frame f;
    f.tag = tag;
    f.pre = tag == "pre";
    if (tag[0] == 'h') f.strong_add = 1;
    if (in_leaf) {
      add_break();
    } else {
      close_para();
      f.kind = FrameKind::Leaf;
      NodeKind k = tag == "pre"          ? NodeKind::CodeBlock
                   : tag == "blockquote" ? NodeKind::BlockQuote
                                         : NodeKind::Heading;
      f.node = doc_.create(k, container());
      if (k == NodeKind::Heading) doc_.get(f.node).level = tag[1] - '0';
    }
    strong_ += f.strong_add;
    push(std::move(f));
    pre_start_ = tag == "pre";
    return;
  }

  // Tables.
  if (tag == "table") {
    Frame f;
    f.tag = tag;
    if (in_leaf) {
      add_break();
    } else {
      close_para();
      f.kind = FrameKind::Container;
      f.node = doc_.create(NodeKind::Table, container());
    }
    push(std::move(f));
    return;
  }
  if ((tag == "tr" || tag == "td" || tag == "th") && in_layout_table()) {
    block_boundary();
    return;
  }
  if (tag == "tr" || tag == "td" || tag == "th") {
    if (!frames_.empty() && (frames_.back().tag == "td" || frames_.back().tag == "th"))
      pop();
    if (tag == "tr" && !frames_.empty() && frames_.back().tag == "tr") pop();
    const bool in_table = !frames_.empty() &&
                          frames_.back().kind == FrameKind::Container &&
                          (doc_.get(frames_.back().node).kind == NodeKind::Table ||
                           doc_.get(frames_.back().node).kind == NodeKind::TableRow);
    if (!in_table) {
      // Table markup inside a leaf (or stray cells): plain separators.
      if (tag == "tr") add_break();
      else if (target() != INVALID_NODE && last_char(target()) != '\n' &&
               last_char(target()) != '\0')
        emit(" | ");
      return;
    }
    if (tag != "tr" && doc_.get(frames_.back().node).kind == NodeKind::Table)
      open("tr", {});  // cell without a row
    Frame f;
    f.tag = tag;
    if (tag == "tr") {
      f.kind = FrameKind::Container;
      f.node = doc_.create(NodeKind::TableRow, frames_.back().node);
    } else {
      f.kind = FrameKind::Leaf;
      f.node = doc_.create(NodeKind::TableCell, frames_.back().node);
      doc_.get(f.node).level = tag == "th" ? 1 : 0;
    }
    push(std::move(f));
    return;
  }

  if (tag == "hr") {
    if (in_leaf) {
      add_break();
    } else {
      close_para();
      doc_.create(NodeKind::HorizontalRule, container());
    }
    return;
  }
  if (tag == "figcaption") {
    block_boundary();
    Frame f;
    f.tag = tag;
    f.em_add = 1;
    ++em_;
    push(std::move(f));
    return;
  }
  if (one_of(tag, kBlockTags)) block_boundary();
}

void Builder::close(const std::string& tag) {
  if (tag == "a") {
    if (!links_.empty()) links_.pop_back();
    return;
  }
  if (tag == "em" || tag == "i" || tag == "cite" || tag == "dfn" ||
      tag == "var") {
    em_ = std::max(0, em_ - 1);
    return;
  }
  if (tag == "strong" || tag == "b") {
    strong_ = std::max(0, strong_ - 1);
    return;
  }
  if (tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp") {
    code_ = std::max(0, code_ - 1);
    return;
  }
  if (tag == "del" || tag == "s" || tag == "strike") {
    strike_ = std::max(0, strike_ - 1);
    return;
  }
  if ((tag == "tr" || tag == "td" || tag == "th") && in_layout_table()) {
    block_boundary();
    return;
  }
  if (tag == "ul" || tag == "ol" || tag == "li" || tag == "blockquote" ||
      tag == "pre" || tag == "table" || tag == "tr" || tag == "td" ||
      tag == "th" || tag == "figcaption" ||
      (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')) {
    // A list / table close also ends its unclosed items / cells.
    if (pop_to(tag)) {
      if (owner() != INVALID_NODE) add_break();
    }
    return;
  }
  if (one_of(tag, kBlockTags)) block_boundary();
}

// Lower-case tag name at the start of `s` ("div", "h2", "content:encoded").
std::string tag_name(std::string_view s) {
  size_t e = 0;
  while (e < s.size() &&
         (std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '-' ||
          s[e] == ':'))
    ++e;
  return ascii_lower(s.substr(0, e));
}

// End of the tag starting at `lt` (index of '>'), quote-aware.
size_t tag_end(std::string_view html, size_t lt) {
  char quote = 0;
  for (size_t i = lt + 1; i < html.size(); ++i) {
    char c = html[i];
    if (quote) {
      if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      return i;
    }
  }
  return std::string_view::npos;
}

// "<br/>" / "<x a='b' />", but not the unquoted "<a href=x/>".
bool ends_self_closed(std::string_view tag) {
  if (tag.empty() || tag.back() != '/') return false;
  if (tag.size() == 1) return true;
  char c = tag[tag.size() - 2];
  return is_space(c) || c == '"' || c == '\'' ||
         tag.find('=') == std::string_view::npos;
}

// Hidden, aria-hidden or carrying a chrome class token.
bool is_chrome(std::string_view attrs) {
  if (html_has_attr(attrs, "hidden")) return true;
  if (ascii_lower(html_attr(attrs, "aria-hidden")) == "true") return true;
  std::string style = ascii_lower(html_attr(attrs, "style"));
  style.erase(std::remove(style.begin(), style.end(), ' '), style.end());
  if (style.find("display:none") != std::string::npos) return true;
  if (one_of(std::string_view(ascii_lower(html_attr(attrs, "role"))), kChromeRoles))
    return true;
  std::string cls = ascii_lower(html_attr(attrs, "class"));
  size_t p = 0;
  while (p < cls.size()) {
    size_t e = cls.find_first_of(" \t\n", p);
    if (e == std::string::npos) e = cls.size();
    std::string_view tok = std::string_view(cls).substr(p, e - p);
    if (!tok.empty() && one_of(tok, kChromeClasses)) return true;
    for (std::string_view part : kChromeClassParts)
      if (tok.find(part) != std::string_view::npos) return true;
    p = e + 1;
  }
  return false;
}

size_t find_ci(std::string_view hay, std::string_view needle, size_t from);

// role=presentation, or a table that holds another table (old-school page
// grids), or a lone big cell.
bool is_layout_table(std::string_view html, size_t after_open,
                     std::string_view attrs) {
  if (ascii_lower(html_attr(attrs, "role")) == "presentation") return true;
  size_t end = html_element_end(html, after_open, "table");
  std::string_view inner = html.substr(after_open, end - after_open);
  if (find_ci(inner, "<table", 0) != std::string_view::npos) return true;
  // A single row: a frame around content, not data.
  size_t rows = 0;
  for (size_t p = 0; (p = find_ci(inner, "<tr", p)) != std::string_view::npos;
       p += 3)
    if (p + 3 < inner.size() && !std::isalnum(static_cast<unsigned char>(inner[p + 3])))
      ++rows;
  return rows <= 1;
}

size_t find_ci(std::string_view hay, std::string_view needle, size_t from) {
  for (size_t i = from; i + needle.size() <= hay.size(); ++i)
    if (starts_with_ci(hay.substr(i), needle)) return i;
  return std::string_view::npos;
}

}  // namespace

namespace {

std::optional<std::string_view> find_attr(std::string_view attrs,
                                          std::string_view name) {
  size_t i = 0;
  while (i < attrs.size()) {
    while (i < attrs.size() && (is_space(attrs[i]) || attrs[i] == '/')) ++i;
    size_t n0 = i;
    while (i < attrs.size() && !is_space(attrs[i]) && attrs[i] != '=' &&
           attrs[i] != '>' && attrs[i] != '/')
      ++i;
    std::string_view key = attrs.substr(n0, i - n0);
    while (i < attrs.size() && is_space(attrs[i])) ++i;
    std::string_view value;
    if (i < attrs.size() && attrs[i] == '=') {
      ++i;
      while (i < attrs.size() && is_space(attrs[i])) ++i;
      if (i < attrs.size() && (attrs[i] == '"' || attrs[i] == '\'')) {
        char q = attrs[i++];
        size_t v0 = i;
        while (i < attrs.size() && attrs[i] != q) ++i;
        value = attrs.substr(v0, i - v0);
        if (i < attrs.size()) ++i;
      } else {
        size_t v0 = i;
        while (i < attrs.size() && !is_space(attrs[i]) && attrs[i] != '>') ++i;
        value = attrs.substr(v0, i - v0);
      }
    }
    if (key.empty()) {
      if (i < attrs.size()) ++i;
      continue;
    }
    if (key.size() == name.size() && starts_with_ci(key, name)) return value;
  }
  return std::nullopt;
}

}  // namespace

std::string html_attr(std::string_view attrs, std::string_view name) {
  auto v = find_attr(attrs, name);
  return v ? html_decode_entities(*v) : std::string{};
}

size_t html_tag_end(std::string_view html, size_t lt) {
  return tag_end(html, lt);
}

bool html_has_attr(std::string_view attrs, std::string_view name) {
  return find_attr(attrs, name).has_value();
}

size_t html_element_end(std::string_view html, size_t after_open,
                        std::string_view tag) {
  if (tag == "script" || tag == "style") {
    // Raw text: "a<b" in a script is not a tag.
    size_t c = find_ci(html, "</" + std::string(tag), after_open);
    size_t e = c == std::string_view::npos ? c : tag_end(html, c);
    return e == std::string_view::npos ? html.size() : e + 1;
  }
  int depth = 1;
  size_t i = after_open;
  while (i < html.size()) {
    size_t lt = html.find('<', i);
    if (lt == std::string_view::npos) break;
    const bool closing = lt + 1 < html.size() && html[lt + 1] == '/';
    const size_t at = lt + 1 + (closing ? 1 : 0);
    size_t e = tag_end(html, lt);
    if (e == std::string_view::npos) break;
    if (tag_name(html.substr(at)) == tag) {
      const bool self_close = ends_self_closed(html.substr(lt, e - lt));
      if (closing) {
        if (--depth == 0) return e + 1;
      } else if (!self_close) {
        ++depth;
      }
    }
    i = e + 1;
  }
  return html.size();
}

std::string resolve_url(std::string_view base, std::string_view href) {
  std::string_view h = href;
  trim_ascii(h);
  if (h.empty() || base.empty()) return std::string(h);
  // Has a scheme ("https:", "mailto:", "gemini:")?
  size_t colon = h.find(':');
  if (colon != std::string_view::npos &&
      h.find_first_of("/?#") > colon && colon > 0)
    return std::string(h);
  size_t scheme_end = base.find("://");
  if (scheme_end == std::string_view::npos) return std::string(h);
  if (starts_with(h, "//"))
    return std::string(base.substr(0, scheme_end + 1)) + std::string(h);
  size_t host_end = base.find_first_of("/?#", scheme_end + 3);
  std::string_view origin = base.substr(0, host_end);
  if (h[0] == '/') return std::string(origin) + std::string(h);
  std::string_view no_frag = base.substr(0, base.find('#'));
  if (h[0] == '#') return std::string(no_frag) + std::string(h);
  std::string_view no_query = no_frag.substr(0, no_frag.find('?'));
  if (h[0] == '?') return std::string(no_query) + std::string(h);
  // Relative path: directory of the base path, then fold "./" and "../".
  std::string dir = host_end == std::string_view::npos
                        ? "/"
                        : std::string(no_query.substr(host_end));
  dir.erase(dir.rfind('/') + 1);
  std::string path = dir + std::string(h);
  std::vector<std::string> segs;
  size_t p = 1;
  while (p <= path.size()) {
    size_t e = path.find('/', p);
    if (e == std::string::npos) e = path.size();
    std::string seg = path.substr(p, e - p);
    if (seg == "..") {
      if (!segs.empty()) segs.pop_back();
      if (e == path.size()) segs.emplace_back();
    } else if (seg != "." || e == path.size()) {
      segs.push_back(seg == "." ? std::string{} : seg);
    }
    p = e + 1;
  }
  std::string out(origin);
  for (const auto& s : segs) out += "/" + s;
  return out;
}

std::string html_to_plain(std::string_view html) {
  std::string raw;
  for (size_t i = 0; i < html.size(); ++i) {
    const bool tag = html[i] == '<' && i + 1 < html.size() &&
                     (std::isalpha(static_cast<unsigned char>(html[i + 1])) ||
                      html[i + 1] == '/' || html[i + 1] == '!');
    if (!tag) {
      raw.push_back(html[i]);
      continue;
    }
    size_t e = tag_end(html, i);
    if (e == std::string_view::npos) break;
    // Inline tags join words ("<b>x</b>y"); block tags separate them.
    std::string name = tag_name(html.substr(i + 1 + (html[i + 1] == '/')));
    if (name == "br" || name == "li" || name == "td" || name == "th" ||
        name == "tr" || (name.size() == 2 && name[0] == 'h') ||
        one_of(name, kBlockTags))
      raw.push_back(' ');
    i = e;
  }
  std::string s = html_decode_entities(raw);
  std::string out;
  bool space = false;
  for (char c : s) {
    if (is_space(c)) {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

void html_append_blocks(Document& doc, NodeId parent, std::string_view html,
                        const HtmlOptions& opt) {
  Builder b(doc, parent, opt);
  size_t i = 0;
  size_t text_start = 0;
  auto flush_text = [&](size_t end) {
    if (end > text_start) b.text(html.substr(text_start, end - text_start));
  };
  while (i < html.size()) {
    if (html[i] != '<') {
      ++i;
      continue;
    }
    std::string_view rest = html.substr(i);
    if (starts_with(rest, "<!--")) {
      flush_text(i);
      size_t e = html.find("-->", i + 4);
      i = e == std::string_view::npos ? html.size() : e + 3;
      text_start = i;
      continue;
    }
    if (starts_with(rest, "<![CDATA[")) {
      flush_text(i);
      size_t e = html.find("]]>", i + 9);
      size_t end = e == std::string_view::npos ? html.size() : e;
      b.text(html.substr(i + 9, end - i - 9));
      i = e == std::string_view::npos ? html.size() : e + 3;
      text_start = i;
      continue;
    }
    const bool closing = rest.size() > 1 && rest[1] == '/';
    const size_t name_at = closing ? 2 : 1;
    const bool tagish =
        rest.size() > name_at &&
        (std::isalpha(static_cast<unsigned char>(rest[name_at])) ||
         (!closing && (rest[1] == '!' || rest[1] == '?')));
    if (!tagish) {  // a literal '<' in text
      ++i;
      continue;
    }
    size_t e = tag_end(html, i);
    if (e == std::string_view::npos) break;
    flush_text(i);
    text_start = e + 1;
    if (rest[1] == '!' || rest[1] == '?') {  // doctype / processing instruction
      i = e + 1;
      continue;
    }
    std::string_view inside = html.substr(i + name_at, e - i - name_at);
    std::string name = tag_name(inside);
    if (closing) {
      b.close(name);
      i = e + 1;
      continue;
    }
    std::string_view attrs = inside.substr(name.size());
    const bool self_close = ends_self_closed(attrs);
    if (one_of(name, kSkipTags) && !self_close) {
      size_t c = find_ci(html, "</" + name, e + 1);
      size_t ce = c == std::string_view::npos ? std::string_view::npos
                                              : tag_end(html, c);
      i = ce == std::string_view::npos ? html.size() : ce + 1;
      text_start = i;
      continue;
    }
    if (opt.drop_chrome && !self_close && !one_of(name, kVoidTags) &&
        (one_of(name, kChromeTags) || is_chrome(attrs))) {
      i = html_element_end(html, e + 1, name);
      text_start = i;
      continue;
    }
    if (opt.drop_chrome && name == "table" && !self_close &&
        is_layout_table(html, e + 1, attrs)) {
      b.open_layout_table();
      i = e + 1;
      continue;
    }
    b.open(name, attrs);
    if (self_close && !one_of(name, kVoidTags)) b.close(name);
    i = e + 1;
  }
  flush_text(html.size());
  b.finish();
}

}  // namespace xanatoo
