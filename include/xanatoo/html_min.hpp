// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Forgiving HTML fragment → document nodes, for remote content (feed
// entries, Hacker News comments, …). Tag soup is fine: unclosed <p> / <li>
// close implicitly, unknown wrappers (div, span, section) are unwrapped,
// script / style / iframe are dropped with their contents.

#include "xanatoo/types.hpp"

#include <string>
#include <string_view>

namespace xanatoo {

struct HtmlOptions {
  std::string base_url;    // relative href / src resolve against this
  bool gt_quotes = false;  // paragraphs starting with ">" → BlockQuote (HN)
  // Whole web pages (html_page.cpp):
  bool sections = false;     // h1–h6 open nested Section + Header (outline)
  bool anchors = false;      // id= → node label; same-page links stay "#id"
  std::string page_url;      // "same page" for anchors (default: base_url)
  bool drop_chrome = false;  // drop nav / aside / form / footer, hidden and
                             // screen-reader-only elements, alt="" images
};

// Append the blocks of `html` as children of `parent`:
// p / div → Paragraph, h1–h6 → Heading, ul / ol / li → List / ListItem,
// blockquote → BlockQuote, pre → CodeBlock, table → Table, hr; inline
// a / em / strong / code / del → spans, br → line break. Images become
// "[image: alt]" links (remote images are not fetched).
void html_append_blocks(Document& doc, NodeId parent, std::string_view html,
                        const HtmlOptions& opt = {});

// Tags stripped, entities decoded, whitespace collapsed and trimmed.
std::string html_to_plain(std::string_view html);

// Value of attribute `name` inside a tag's attribute text
// (`href="x" class=y`), entities decoded; empty when absent.
std::string html_attr(std::string_view attrs, std::string_view name);
bool html_has_attr(std::string_view attrs, std::string_view name);

// Index of the '>' closing the tag that starts at `lt` (quote-aware);
// npos when unterminated.
size_t html_tag_end(std::string_view html, size_t lt);

// Index just past the element whose open tag ends at `after_open`
// (nesting of the same tag counted); html.size() when unclosed.
size_t html_element_end(std::string_view html, size_t after_open,
                        std::string_view tag);

// Resolve `href` against `base` (absolute, //host, /path, ?q, #f, relative).
std::string resolve_url(std::string_view base, std::string_view href);

}  // namespace xanatoo
