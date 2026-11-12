// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// JSON search engines (search.hpp kinds wikipedia, hn, archive, lemmy,
// bluesky, gutendex): the answer of the service's API becomes a result
// document whose links point at native pages — wikipedia: articles,
// hn:item?id= threads, lemmy:host/post/N, Gutenberg ebooks — so a click
// stays in xanatoo's own views. Each mapper writes Markdown, which the
// ordinary parser turns into the document (docs/SEARCH.md).

#include "xanatoo/search.hpp"
#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Markdown for `json`, the answer to `plan.url`. Pure: no network. A
// malformed or empty answer gives a short "No results" page, never throws.
std::string search_results_markdown(const SearchPlan& plan, std::string_view json);

// The Markdown escaped for running text (not for link text, which the
// parser reads literally): `\ ` * _ [ ]` anywhere, and '#', '-', '+' where a
// line would start a heading or list.
std::string search_md_escape(std::string_view text);

// Media record addresses: media:<source>:<id>. Only whitelisted sources and
// validated identifiers produce API URLs (no arbitrary-host fetches).
bool is_media_ref(std::string_view ref);
std::optional<SearchPlan> media_plan(std::string_view ref);
std::string media_results_markdown(const SearchPlan& plan, std::string_view json);

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
// Fetch the answer and map it. Null with `err` set on a network or HTTP error.
std::optional<Document> search_fetch(const SearchPlan& plan, std::string* err);
std::optional<Document> media_fetch(const SearchPlan& plan, std::string* err);
#endif

}  // namespace xanatoo
