// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Search engines and the "search:" address (docs/SEARCH.md).
//
//   search:<engine>:<query>     the results of <engine> for <query>
//
// The query is stored percent-encoded with '+' for spaces, so an address
// never holds '#', '|' or blanks that locator.hpp would cut at. An engine is
// a URL template; fetching a search page fetches the expanded URL through
// the ordinary dispatch (feed, web page, native ref), so results are
// documents like any other and Back / bookmarks / session restore work.
//
// Built-in engines live in the table in search.cpp; the user adds or replaces
// engines in `user_data_dir()/search.sexp`:
//   (engine "id" (name "Title") (keyword "k") (badge "T")
//                (url "https://host/find?q=%s") (kind feed) (space "+")
//                (site "host" "ref:"))
// `kind` says how the answer becomes a document: page (default, a web page
// or any page xanatoo opens), feed (RSS / Atom) or the name of a JSON
// mapper in search_results.hpp (wikipedia, hn, archive, lemmy, bluesky,
// gutendex): native result documents with links to wikipedia: / hn: /
// lemmy: pages.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

inline constexpr std::string_view kSearchPrefix = "search:";

struct SearchEngine {
  std::string id;       // address part: [a-z0-9-]+
  std::string name;     // menu / tooltip title
  std::string keyword;  // Ctrl+L: "<keyword> <query>"; may be empty
  std::string badge;    // 1–3 letters on the engine button
  std::string url;      // template with one %s for the encoded query
  std::string kind = "page";  // page | feed | a JSON mapper (see above)
  std::string space = "+";  // what a blank becomes in the URL
  // Addresses this engine searches, for the location bar's search button:
  // an entry with ':' is an address prefix ("hn:", "wikipedia:de:"), any
  // other a host that also matches its subdomains ("ycombinator.com").
  std::vector<std::string> sites;
};

// The engines shipped with xanatoo, in menu order.
const std::vector<SearchEngine>& builtin_engines();

// Engines from the text of a search.sexp; malformed ones are skipped and
// described in `warnings` when given.
std::vector<SearchEngine> parse_engines(std::string_view source,
                                        std::vector<std::string>* warnings = nullptr);

// Built-in engines, then the user's file: an entry with a known id replaces
// the built-in one in place, new ids are appended.
std::vector<SearchEngine> load_engines();

struct SearchRef {
  std::string engine;
  std::string query;  // percent-encoded form
};

bool is_search_ref(std::string_view s);
std::optional<SearchRef> parse_search_ref(std::string_view s);

// Address of a search; `raw` is what the user typed. Empty when the query is
// blank or the engine id is not a valid id.
std::string search_ref(std::string_view engine, std::string_view raw);

// "electron spin" ↔ "electron+spin"; everything but [A-Za-z0-9-._~] as %XX.
std::string search_encode(std::string_view raw);
std::string search_decode(std::string_view encoded);

// What a search address needs: the URL to fetch, and how to read the answer.
struct SearchPlan {
  std::string url;    // the engine's template with the query put in
  std::string kind;   // SearchEngine::kind
  std::string query;  // decoded, for titles
};
std::optional<SearchPlan> search_plan(std::string_view ref,
                                      const std::vector<SearchEngine>& engines);

// Kinds an engine may name: "page", "feed" and the JSON mappers.
bool is_search_kind(std::string_view kind);
// A JSON mapper (not "page" / "feed").
bool is_json_search_kind(std::string_view kind);

// The URL that opens through the ordinary dispatch ("feed:…" for a feed):
// what a page or feed search fetches. Empty for an unknown engine, a
// malformed address or a JSON engine (those go through search_fetch).
std::string search_target(std::string_view ref,
                          const std::vector<SearchEngine>& engines);

// "w electron" with keyword w → the search address. Empty unless the first
// word is an engine keyword and a query follows.
std::string search_from_keyword(std::string_view text,
                                const std::vector<SearchEngine>& engines);

// The engine that searches the service an address belongs to: the engine of
// a search: address, else the longest matching `sites` entry. Empty when no
// engine claims the address.
std::string engine_for_address(std::string_view address,
                               const std::vector<SearchEngine>& engines);

// Query of a search address, decoded for display ("" for other addresses).
std::string search_query_text(std::string_view ref);

}  // namespace xanatoo
