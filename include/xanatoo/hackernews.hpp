// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Hacker News pages that open in-app, e.g.:
//   https://news.ycombinator.com/            (front page, also /news)
//   https://news.ycombinator.com/newest      (/best, /ask, /show; ?p=2)
//   https://news.ycombinator.com/item?id=1   (story + comment tree)
bool is_hn_url(std::string_view s);

// Map HN comment HTML (<p>, <a>, <i>, <pre><code>, "> quote" paragraphs)
// to Paragraph / BlockQuote / CodeBlock children of `parent`.
void hn_append_html(Document& doc, NodeId parent, std::string_view html);

#if defined(XANATOO_HN) && XANATOO_HN
// Listings: Firebase story ids (ranked) + one Algolia search for the
// metadata. Items: the whole comment tree in one Algolia request.
// Builds a Document with Thread + Message nodes (read-only).
std::optional<Document> hn_fetch(std::string_view url,
                                 std::string* err = nullptr);
#endif

}  // namespace xanatoo
