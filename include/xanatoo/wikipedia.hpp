// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Native Wikipedia articles: "wikipedia:Title" / "wikipedia:de:Title"
// fetch the Parsoid HTML of the REST API and map it through html_page.
// Article links inside become wikipedia: refs again; everything else
// (files, categories, special pages) stays an ordinary web link.

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

inline constexpr std::string_view kWikipediaPrefix = "wikipedia:";

struct WikipediaRef {
  std::string lang = "en";
  std::string title;     // underscores for spaces, not percent-encoded
  std::string fragment;  // without '#'
};

// "wikipedia:Title", "wikipedia:de:Title#Section"; a lower-case 2–3 letter
// (or "xx-yy") first part is the language, default "en".
bool is_wikipedia_ref(std::string_view s);
std::optional<WikipediaRef> parse_wikipedia_ref(std::string_view s);

// Canonical ref: "wikipedia:Title" for en, else "wikipedia:lang:Title".
std::string wikipedia_ref_string(const WikipediaRef& r);

// https://{lang}.wikipedia.org/api/rest_v1/page/html/{Title}
std::string wikipedia_rest_url(const WikipediaRef& r);
// https://{lang}.wikipedia.org/wiki/{Title}
std::string wikipedia_page_url(const WikipediaRef& r);

// https://xx(.m).wikipedia.org/wiki/Title article → ref; nullopt for other
// hosts, non-article namespaces (File:, Category:, Special:, …) and
// non-/wiki/ paths. Used by the link rewriter and the "native view" button.
std::optional<WikipediaRef> wikipedia_ref_from_url(std::string_view url);

// Rewrite article links in `doc`: links to `self` become "#fragment",
// other articles "wikipedia:…".
void wikipedia_rewrite_links(Document& doc, const WikipediaRef& self);

// Native form of a web page URL ("" when there is none). Currently
// Wikipedia articles only.
std::string native_url_for(std::string_view url);

#if defined(XANATOO_WIKIPEDIA) && XANATOO_WIKIPEDIA
// Fetch an article; `final_ref` receives the canonical ref after
// redirects (with the requested fragment).
std::optional<Document> wikipedia_fetch(std::string_view ref, std::string* err,
                                        std::string* final_ref = nullptr);
#endif

}  // namespace xanatoo
