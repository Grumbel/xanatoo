// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/locator.hpp"

#include "xanatoo/editor.hpp"
#include "xanatoo/util.hpp"

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
  for (const SelectorName& n : kNames)
    if (n.kind == kind) return n.name;
  return {};
}

std::string format_selector(const Selector& sel) {
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

}  // namespace xanatoo
