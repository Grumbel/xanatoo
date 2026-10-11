// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"
#include <string>
#include <string_view>

namespace xanatoo {

// Org-mode subset → document model.
//
// Supported:
//   * / ** / *** … headings (optional TODO/DONE/… keyword, :tags:)
//   paragraphs (blank-line separated)
//   - / + / * lists (indent nesting); [ ] / [X] checkboxes
//   1. ordered lists
//   | pipe | tables |
//   #+BEGIN_SRC / #+END_SRC, #+BEGIN_EXAMPLE, #+BEGIN_QUOTE
//   [[target][desc]] / [[target]] links
//   *bold* /italic/ =code= ~code~ +strike+
//   #+TITLE: #+AUTHOR: #+LANGUAGE:  document meta
//   :PROPERTIES: … :CUSTOM_ID: … :END:  → node.label
//   # comments and other #+keywords ignored
//
// Mapping:
//   heading depth → Header.level
//   TODO keyword  → Header.src
//   tags          → Node.tags  (stored without surrounding colons: "a:b")
//   CUSTOM_ID     → Node.label
//
// Extensions .org prefer this format.

bool path_uses_org(std::string_view path);

Document parse_org(std::string_view source);
// Best-effort export of the subset we understand.
std::string serialize_org(const Document& doc);

}  // namespace xanatoo
