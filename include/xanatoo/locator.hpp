// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/editor.hpp"
#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// Addresses of passages, see docs/PASSAGES.md:
//   base//selector//selector#selector
// `//` cuts (the passage is shown as a document of its own), `#` points (the
// whole thing, with the passage marked).

enum class SelectorKind : uint8_t { Id, Path, Text, Rect, At };

struct Selector {
  SelectorKind kind = SelectorKind::Id;
  // Decoded payload. Text stays percent-encoded: its grammar splits on ','
  // and '-' first, so an escaped comma must survive until then.
  std::string value;

  bool operator==(const Selector& o) const {
    return kind == o.kind && value == o.value;
  }
};

struct Locator {
  std::string base;  // the only part buffers, caches and fetches ever see
  std::vector<Selector> pipes;
  std::optional<Selector> point;

  bool has_selectors() const { return !pipes.empty() || point.has_value(); }
};

// Never fails: without selectors the whole input is the base. Only known
// selector names at the end of the string are taken, thumtoo pipes
// (`//page:12`) and any other `//` stay in the base.
Locator parse_locator(std::string_view address);
std::string format_locator(const Locator& loc);

// One step towards the whole document: drops the point, else the last pipe
// becomes the point (the context of a cut, the cut still marked). False
// without selectors.
bool locator_up(Locator& loc);

enum class Resolve : uint8_t { Exact, Lost };

// Node named by an Id or Path selector, searched below `within`
// (INVALID_NODE = the whole document). A heading stands for its section.
Resolve resolve_node(const Document& doc, NodeId within, const Selector& sel,
                     NodeId& out);

// The selector that names `node`: its label or heading, else a child-index
// path.
Selector node_selector(const Document& doc, NodeId node);

// From the first to the last character of the text below `node`.
Selection node_range(const Document& doc, NodeId node);

// Text selectors, grammar of the web's text fragments, parts percent-encoded:
//   [prefix-,]start[,end][,-suffix]   a range
//   prefix-,,-suffix                  a point between two contexts
// Matching collapses whitespace and ignores ASCII case.
struct TextQuote {
  std::string prefix, start, end, suffix;  // decoded
};
std::optional<TextQuote> parse_text_quote(std::string_view value);
std::string format_text_quote(const TextQuote& q);

// Range (or empty selection for a point) a Text selector names below
// `within` (INVALID_NODE = the whole document); the first match wins.
Resolve resolve_text(const Document& doc, NodeId within, const Selector& sel,
                     Selection& out);

// Text selector for `sel` (empty: a point at its caret), with as much
// context as it takes to be unique below `within`.
Selector text_selector(const Document& doc, NodeId within,
                       const Selection& sel);

}  // namespace xanatoo
