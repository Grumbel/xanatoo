// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <string>
#include <string_view>

namespace xanatoo {

// Service refs name a page in its native (Thread + Message / article) view;
// the plain https:// URL of the same page opens as an ordinary web page.
//   hn:                      front page        (hn:newest, hn:ask, hn:?p=2)
//   hn:item?id=123           story + comments
//   reddit:r/linux           subreddit         (reddit: = front page)
//   reddit:r/linux/comments/abc/title/
//   lemmy:lemmy.world/c/technology
//   lemmy:lemmy.world/post/123
// The part after the prefix mirrors the web URL's path (Lemmy: host/path).
bool is_hn_ref(std::string_view s);
bool is_reddit_ref(std::string_view s);
bool is_lemmy_ref(std::string_view s);

// Web URL → ref ("" when the URL is not a page of that service).
std::string hn_ref_from_url(std::string_view url);
std::string reddit_ref_from_url(std::string_view url);
std::string lemmy_ref_from_url(std::string_view url);

// Ref (hn: / reddit: / lemmy: / wikipedia:) → https web URL, "" otherwise.
std::string web_url_for(std::string_view ref);

// Native form of a web page URL ("" when there is none).
std::string native_url_for(std::string_view url);

// The other view of the same page: web URL → native ref, ref → web URL.
// "" when the page has only one form.
std::string alternate_view_url(std::string_view url);

// One level up the page hierarchy, "" at the top or when there is none:
//   /a/b/c.md → /a/b/ → /a/ → /      (absolute local paths, dirs end in '/')
//   https://h/a/b?q#f → https://h/a/ → https://h/   (also gemini:// etc.)
//   reddit:r/x/comments/id/t/ → reddit:r/x/ → reddit:
//   hn:item?id=1 → hn:          lemmy:h/c/x, lemmy:h/post/1 → https://h/
// Other refs (wikipedia:, nostr:, feed:, …) have no parent.
std::string up_url(std::string_view url);

// In a fetched native view, turn links to other pages of the same service
// (by the ref's prefix: "hn:" / "reddit:") into refs so they stay native.
// Lemmy views get refs from lemmy.cpp directly.
void native_rewrite_links(Document& doc, std::string_view ref);

}  // namespace xanatoo
