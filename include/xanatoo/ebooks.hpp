// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Standard Ebooks catalogue pages and book pages, e.g.:
//   https://standardebooks.org/ebooks                 (?query=dickens&page=2)
//   https://standardebooks.org/ebooks/charles-dickens (author listing)
//   https://standardebooks.org/subjects/fiction
//   https://standardebooks.org/ebooks/jane-austen/emma (book → epub)
bool is_standard_ebooks_url(std::string_view s);

// Direct EPUB links (path ends in .epub) and Project Gutenberg book pages /
// epub links (https://www.gutenberg.org/ebooks/1342[.epub3.images]).
bool is_remote_epub_url(std::string_view s);

inline bool is_ebook_url(std::string_view s) {
  return is_standard_ebooks_url(s) || is_remote_epub_url(s);
}

// Where to download the EPUB for a book URL; empty for catalogue pages.
std::string ebook_download_url(std::string_view s);

// Standard Ebooks catalogue HTML (<li typeof="schema:Book"> entries) →
// Document with a book list and a "Next page" link. `page_url` is the URL
// the HTML came from (title / paging).
Document parse_standard_ebooks_listing(std::string_view html,
                                       std::string_view page_url);

#if defined(XANATOO_EBOOKS) && XANATOO_EBOOKS
// Catalogue pages are scraped into a list; book URLs download the EPUB to
// the cache (reused when present) and import it via pandoc. For books
// `local_path` receives the derived Markdown path (images resolve there).
std::optional<Document> ebook_fetch(std::string_view url,
                                    std::string* err = nullptr,
                                    std::string* local_path = nullptr);
#endif

}  // namespace xanatoo
