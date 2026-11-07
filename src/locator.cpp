// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/locator.hpp"

#include "xanatoo/editor.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <charconv>

namespace xanatoo {

namespace {

struct SelectorName {
  std::string_view name;
  SelectorKind kind;
};
constexpr SelectorName kNames[] = {
    {"id", SelectorKind::Id},     {"path", SelectorKind::Path},
    {"text", SelectorKind::Text}, {"rect", SelectorKind::Rect},
    {"at", SelectorKind::At},
};

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A '%' that starts no escape stays as it is ("100%").
std::string percent_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const int hi = hex_value(s[i + 1]);
      const int lo = hex_value(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

// Only what would break the address; UTF-8 stays readable.
std::string percent_encode(std::string_view s) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= ' ' || u == 0x7f || c == '%' || c == '/' || c == '#' ||
        c == '?' || c == '|') {
      out.push_back('%');
      out.push_back(kHex[u >> 4]);
      out.push_back(kHex[u & 15]);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::optional<Selector> parse_selector(std::string_view s) {
  for (const auto& [name, kind] : {
           SelectorName{"source-raw", SelectorKind::SourceRaw},
           {"source-sexpr", SelectorKind::SourceSexpr},
           {"source-xml", SelectorKind::SourceXml}})
    if (s == name) return Selector{kind, {}};
  const size_t colon = s.find(':');
  if (colon == std::string_view::npos || colon + 1 == s.size())
    return std::nullopt;
  const std::string_view name = s.substr(0, colon);
  const std::string_view payload = s.substr(colon + 1);
  for (const SelectorName& n : kNames)
    if (n.name == name)
      return Selector{n.kind, n.kind == SelectorKind::Text
                                  ? std::string(payload)
                                  : percent_decode(payload)};
  return std::nullopt;
}

std::string_view selector_name(SelectorKind kind) {
  if (kind == SelectorKind::SourceRaw) return "source-raw";
  if (kind == SelectorKind::SourceSexpr) return "source-sexpr";
  if (kind == SelectorKind::SourceXml) return "source-xml";
  for (const SelectorName& n : kNames)
    if (n.kind == kind) return n.name;
  return {};
}

std::string format_selector(const Selector& sel) {
  if (is_source_pipe(sel.kind)) return std::string(selector_name(sel.kind));
  return std::string(selector_name(sel.kind)) + ":" +
         (sel.kind == SelectorKind::Text ? sel.value : percent_encode(sel.value));
}

// A bare fragment is a label, as on the web. Everything else carries its name.
std::string format_point(const Selector& sel) {
  if (sel.kind == SelectorKind::Id &&
      sel.value.find(':') == std::string::npos)
    return percent_encode(sel.value);
  return format_selector(sel);
}

Selector parse_point(std::string_view frag) {
  constexpr std::string_view kWebText = ":~:text=";
  if (starts_with(frag, kWebText))
    return {SelectorKind::Text, std::string(frag.substr(kWebText.size()))};
  if (auto sel = parse_selector(frag)) return *sel;
  return {SelectorKind::Id, percent_decode(frag)};
}

bool is_heading(const Node& n) {
  return n.kind == NodeKind::Header || n.kind == NodeKind::Heading;
}

bool inside(const Document& doc, NodeId id, NodeId within) {
  if (within == INVALID_NODE) return true;
  for (NodeId cur = id; cur != INVALID_NODE; cur = doc.get(cur).parent)
    if (cur == within) return true;
  return false;
}

std::string child_path(const Document& doc, NodeId node) {
  std::string path;
  for (NodeId cur = node; cur != doc.root && cur != INVALID_NODE;) {
    const NodeId parent = doc.get(cur).parent;
    if (parent == INVALID_NODE) break;
    const auto& kids = doc.get(parent).children;
    size_t i = 0;
    while (i < kids.size() && kids[i] != cur) ++i;
    path = std::to_string(i) + (path.empty() ? "" : "." + path);
    cur = parent;
  }
  return path;
}

NodeId node_at_path(const Document& doc, NodeId from, std::string_view path) {
  NodeId cur = from;
  while (!path.empty()) {
    const size_t dot = path.find('.');
    const std::string_view part = path.substr(0, dot);
    size_t i = 0;
    const auto [end, ec] =
        std::from_chars(part.data(), part.data() + part.size(), i);
    if (ec != std::errc() || end != part.data() + part.size())
      return INVALID_NODE;
    const auto& kids = doc.get(cur).children;
    if (i >= kids.size()) return INVALID_NODE;
    cur = kids[i];
    path = dot == std::string_view::npos ? std::string_view()
                                         : path.substr(dot + 1);
  }
  return cur;
}

// Text parts escape the grammar's own ',' and '-' too.
std::string quote_encode(std::string_view s) {
  std::string out;
  for (char c : percent_encode(s)) {
    if (c == ',') out += "%2C";
    else if (c == '-') out += "%2D";
    else if (c == '&') out += "%26";
    else out.push_back(c);
  }
  return out;
}

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Whitespace runs as one space, trimmed, lower case (offsets stay bytes).
std::string normalize(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (is_space(c)) {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return ascii_lower(out);
}

// The text below a scope as one normalized string. at[i] is the caret of the
// character byte i belongs to; at[size] the end. The space between two blocks
// stands at the end of the first.
struct FlatText {
  std::string text;
  std::vector<Caret> at;
};

FlatText flatten(const Document& doc, NodeId scope) {
  FlatText f;
  std::vector<NodeId> nodes;
  collect_text_nodes(doc, scope != INVALID_NODE ? scope : doc.root, nodes);
  Caret last{};
  for (NodeId id : nodes) {
    const Node& n = doc.get(id);
    for (size_t si = 0; si < n.spans.size(); ++si) {
      const std::string& t = n.spans[si].text;
      size_t ci = 0;
      for (size_t i = 0; i < t.size(); ++ci) {
        size_t len = 1;
        const auto u = static_cast<unsigned char>(t[i]);
        if (u >= 0xf0) len = 4;
        else if (u >= 0xe0) len = 3;
        else if (u >= 0xc0) len = 2;
        len = std::min(len, t.size() - i);
        const Caret c{id, si, ci};
        if (is_space(t[i])) {
          if (!f.text.empty() && f.text.back() != ' ') {
            f.text.push_back(' ');
            f.at.push_back(c);
          }
        } else {
          for (size_t k = 0; k < len; ++k) {
            f.text.push_back(t[i + k]);
            f.at.push_back(c);
          }
        }
        i += len;
      }
      last = {id, si, ci};
    }
    if (!f.text.empty() && f.text.back() != ' ') {
      f.text.push_back(' ');
      f.at.push_back(last);
    }
  }
  if (!f.text.empty() && f.text.back() == ' ') {
    f.text.pop_back();
    f.at.pop_back();
  }
  f.at.push_back(last);
  f.text = ascii_lower(f.text);
  return f;
}

// `hay` before `pos` ends with `s` (one space between allowed).
bool ends_before(const std::string& hay, size_t pos, const std::string& s) {
  if (s.empty()) return true;
  if (pos > 0 && hay[pos - 1] == ' ') --pos;
  return pos >= s.size() && hay.compare(pos - s.size(), s.size(), s) == 0;
}

bool starts_after(const std::string& hay, size_t pos, const std::string& s) {
  if (s.empty()) return true;
  if (pos < hay.size() && hay[pos] == ' ') ++pos;
  return hay.compare(pos, s.size(), s) == 0;
}

struct Match {
  size_t begin = 0, end = 0;
};

// Up to `limit` matches of `q` (normalized) in `f`.
std::vector<Match> find_quote(const FlatText& f, const TextQuote& q,
                              size_t limit) {
  std::vector<Match> out;
  const std::string& t = f.text;
  if (q.start.empty()) {  // a point
    if (q.prefix.empty() && q.suffix.empty()) return out;
    const std::string& key = q.prefix.empty() ? q.suffix : q.prefix;
    for (size_t pos = t.find(key); pos != std::string::npos && out.size() < limit;
         pos = t.find(key, pos + 1)) {
      size_t at = q.prefix.empty() ? pos : pos + key.size();
      if (!q.prefix.empty() && !starts_after(t, at, q.suffix)) continue;
      if (!q.prefix.empty() && !q.suffix.empty() && at < t.size() && t[at] == ' ')
        ++at;
      out.push_back({at, at});
    }
    return out;
  }
  for (size_t pos = t.find(q.start); pos != std::string::npos && out.size() < limit;
       pos = t.find(q.start, pos + 1)) {
    if (!ends_before(t, pos, q.prefix)) continue;
    size_t end = pos + q.start.size();
    if (!q.end.empty()) {
      const size_t e = t.find(q.end, end);
      if (e == std::string::npos) break;
      end = e + q.end.size();
    }
    if (!starts_after(t, end, q.suffix)) continue;
    out.push_back({pos, end});
  }
  return out;
}

TextQuote normalized(TextQuote q) {
  q.prefix = normalize(q.prefix);
  q.start = normalize(q.start);
  q.end = normalize(q.end);
  q.suffix = normalize(q.suffix);
  return q;
}

size_t offset_of(const Document& doc, const FlatText& f, const Caret& c) {
  for (size_t i = 0; i + 1 < f.at.size(); ++i)
    if (caret_cmp(doc, f.at[i], c) >= 0) return i;
  return f.text.size();
}

// `n` whole words of `t` before / after `pos`.
std::string words_before(const std::string& t, size_t pos, int n) {
  size_t b = pos;
  while (b > 0 && t[b - 1] == ' ') --b;
  const size_t e = b;
  for (int k = 0; k < n && b > 0; ++k) {
    while (b > 0 && t[b - 1] != ' ') --b;
    if (k + 1 < n) while (b > 0 && t[b - 1] == ' ') --b;
  }
  return t.substr(b, e - b);
}

std::string words_after(const std::string& t, size_t pos, int n) {
  size_t b = pos;
  while (b < t.size() && t[b] == ' ') ++b;
  size_t e = b;
  for (int k = 0; k < n && e < t.size(); ++k) {
    while (e < t.size() && t[e] != ' ') ++e;
    if (k + 1 < n) while (e < t.size() && t[e] == ' ') ++e;
  }
  return t.substr(b, e - b);
}

// Flat range [from, to) in the case of the document: its source characters
// with whitespace collapsed.
std::string normalize_keep_case(const Document& doc, const FlatText& f,
                                size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to; ++i) {
    if (f.text[i] == ' ') {
      out.push_back(' ');
      continue;
    }
    const Caret& c = f.at[i];
    const std::string& t = doc.get(c.node).spans[c.span_index].text;
    size_t byte = 0;  // the source byte of character c.char_index
    for (size_t ci = 0; byte < t.size(); ++byte)
      if ((static_cast<unsigned char>(t[byte]) & 0xc0) != 0x80 &&
          ci++ == c.char_index)
        break;
    size_t k = i;  // bytes of this character in the flat text
    while (k < to && f.text[k] != ' ' && f.at[k] == c) ++k;
    out.append(t, byte, k - i);
    i = k - 1;
  }
  return out;
}

}  // namespace

Locator parse_locator(std::string_view address) {
  Locator loc;
  std::string_view rest = address;
  if (const size_t hash = rest.find('#');
      hash != std::string_view::npos && hash + 1 < rest.size()) {
    loc.point = parse_point(rest.substr(hash + 1));
    rest = rest.substr(0, hash);
  }
  // Right to left: plain URLs have `//` of their own, the first thing that
  // is no selector ends the chain.
  while (true) {
    const size_t at = rest.rfind("//");
    if (at == std::string_view::npos || at == 0 ||
        rest[at - 1] == ':' || rest[at - 1] == '/')
      break;
    auto sel = parse_selector(rest.substr(at + 2));
    if (!sel) break;
    loc.pipes.insert(loc.pipes.begin(), std::move(*sel));
    rest = rest.substr(0, at);
  }
  loc.base = std::string(rest);
  return loc;
}

std::string format_locator(const Locator& loc) {
  std::string out = loc.base;
  for (const Selector& sel : loc.pipes) out += "//" + format_selector(sel);
  if (loc.point) out += "#" + format_point(*loc.point);
  return out;
}

bool locator_up(Locator& loc) {
  if (loc.point) {
    loc.point.reset();
    return true;
  }
  if (loc.pipes.empty()) return false;
  if (is_source_pipe(loc.pipes.back().kind)) {
    loc.pipes.pop_back();  // a view transformation, not a markable fragment
    return true;
  }
  loc.point = std::move(loc.pipes.back());
  loc.pipes.pop_back();
  return true;
}

Resolve resolve_node(const Document& doc, NodeId within, const Selector& sel,
                     NodeId& out) {
  NodeId id = INVALID_NODE;
  if (sel.kind == SelectorKind::Id)
    id = find_anchor_node(doc, "#" + sel.value);
  else if (sel.kind == SelectorKind::Path)
    id = node_at_path(doc, within != INVALID_NODE ? within : doc.root,
                      sel.value);
  if (id == INVALID_NODE || !inside(doc, id, within)) return Resolve::Lost;
  if (const NodeId parent = doc.get(id).parent;
      is_heading(doc.get(id)) && parent != INVALID_NODE &&
      enclosing_section(doc, id) == parent)
    id = parent;
  out = id;
  return Resolve::Exact;
}

Selector node_selector(const Document& doc, NodeId node) {
  const Node& n = doc.get(node);
  NodeId named = node;
  for (NodeId c : n.children)
    if (is_heading(doc.get(c))) {
      named = c;
      break;
    }
  std::string label = address_label(doc, node);
  if (label.empty()) label = address_label(doc, named);
  if (!label.empty()) {
    // Duplicate headings: a label is only good if it comes back here.
    NodeId back = INVALID_NODE;
    const Selector sel{SelectorKind::Id, label};
    if (resolve_node(doc, INVALID_NODE, sel, back) == Resolve::Exact &&
        back == node)
      return sel;
  }
  return {SelectorKind::Path, child_path(doc, node)};
}

Selection node_range(const Document& doc, NodeId node) {
  std::vector<NodeId> texts;
  collect_text_nodes(doc, node, texts);
  if (texts.empty()) return {{node, 0, 0}, {node, 0, 0}};
  const Node& last = doc.get(texts.back());
  Caret end{texts.back(), 0, 0};
  if (!last.spans.empty()) {
    end.span_index = last.spans.size() - 1;
    for (char c : last.spans.back().text)
      if ((static_cast<unsigned char>(c) & 0xc0) != 0x80) ++end.char_index;
  }
  return {{texts.front(), 0, 0}, end};
}

std::optional<TextQuote> parse_text_quote(std::string_view value) {
  std::vector<std::string_view> parts;
  for (size_t at = 0;;) {
    const size_t comma = value.find(',', at);
    parts.push_back(value.substr(at, comma == std::string_view::npos
                                         ? std::string_view::npos
                                         : comma - at));
    if (comma == std::string_view::npos) break;
    at = comma + 1;
  }
  TextQuote q;
  if (parts.size() > 1 && ends_with(parts.front(), "-")) {
    q.prefix = percent_decode(parts.front().substr(0, parts.front().size() - 1));
    parts.erase(parts.begin());
  }
  if (parts.size() > 1 && starts_with(parts.back(), "-")) {
    q.suffix = percent_decode(parts.back().substr(1));
    parts.pop_back();
  }
  if (parts.empty() || parts.size() > 2) return std::nullopt;
  q.start = percent_decode(parts[0]);
  if (parts.size() == 2) q.end = percent_decode(parts[1]);
  if (q.start.empty() && (!q.end.empty() || (q.prefix.empty() && q.suffix.empty())))
    return std::nullopt;
  return q;
}

std::string format_text_quote(const TextQuote& q) {
  std::string out;
  if (!q.prefix.empty() || q.start.empty()) out += quote_encode(q.prefix) + "-,";
  out += quote_encode(q.start);
  if (!q.end.empty()) out += "," + quote_encode(q.end);
  if (!q.suffix.empty() || q.start.empty()) out += ",-" + quote_encode(q.suffix);
  return out;
}

Resolve resolve_text(const Document& doc, NodeId within, const Selector& sel,
                     Selection& out) {
  if (sel.kind != SelectorKind::Text) return Resolve::Lost;
  const auto q = parse_text_quote(sel.value);
  if (!q) return Resolve::Lost;
  const FlatText f = flatten(doc, within);
  const auto found = find_quote(f, normalized(*q), 1);
  if (found.empty()) return Resolve::Lost;
  out.anchor = f.at[found[0].begin];
  out.focus = f.at[found[0].end];
  return Resolve::Exact;
}

Selector text_selector(const Document& doc, NodeId within,
                       const Selection& sel) {
  const FlatText f = flatten(doc, within);
  size_t a = offset_of(doc, f, sel.anchor);
  size_t b = offset_of(doc, f, sel.focus);
  if (a > b) std::swap(a, b);
  const std::string& t = f.text;
  while (a < b && t[a] == ' ') ++a;
  while (b > a && t[b - 1] == ' ') --b;
  // The selector keeps the case of the document, matching ignores it.
  auto original = [&](size_t from, size_t to) {
    if (from >= to) return std::string();
    return normalize_keep_case(doc, f, from, to);
  };
  TextQuote q;
  if (a < b) {
    const std::string body = t.substr(a, b - a);
    if (body.size() <= 80 ||
        std::count(body.begin(), body.end(), ' ') < 8) {
      q.start = original(a, b);
    } else {
      const std::string head = words_after(t, a, 4);
      const std::string tail = words_before(t, b, 4);
      q.start = original(a, a + head.size());
      q.end = original(b - tail.size(), b);
    }
  }
  for (int n = a < b ? 0 : 3; n <= 24; n = n == 0 ? 2 : n * 2) {
    const std::string pre = words_before(t, a, n);
    const std::string post = words_after(t, b, n);
    size_t pb = a;
    while (pb > 0 && t[pb - 1] == ' ') --pb;
    size_t sb = b;
    while (sb < t.size() && t[sb] == ' ') ++sb;
    q.prefix = original(pb - pre.size(), pb);
    q.suffix = original(sb, sb + post.size());
    const auto found = find_quote(f, normalized(q), 2);
    if (found.size() == 1) break;
  }
  return {SelectorKind::Text, format_text_quote(q)};
}

}  // namespace xanatoo
