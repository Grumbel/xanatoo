// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// RSS / Atom feed URLs that open in-app: an explicit `feed:` prefix
// (`feed:https://example.org/` also autodiscovers from an HTML page), or
// an http(s) URL that looks like a feed: *.rss / *.atom / *.rdf / *.xml,
// a /feed, /rss, /atom segment, /feeds/…, or ?feed=rss2 / ?format=atom.
bool is_feed_url(std::string_view s);

// The URL to request: "feed:" stripped, http upgraded to https.
std::string feed_http_url(std::string_view s);

// Root element is <rss>, <feed> (Atom) or <rdf:RDF> (RSS 1.0).
bool looks_like_feed(std::string_view body);

// First <link rel="alternate" type="application/(rss|atom)+xml"> of an HTML
// page, resolved against `page_url`; empty when none.
std::string feed_autodiscover(std::string_view html, std::string_view page_url);

// Feed XML → Document: feed title / subtitle, then a Thread with one
// Message per entry (author, date, linked title, content via html_min).
std::optional<Document> parse_feed(std::string_view xml,
                                   std::string_view feed_url,
                                   std::string* err = nullptr);

#if defined(XANATOO_FEEDS) && XANATOO_FEEDS
// Fetch and parse; an HTML page is followed to its autodiscovered feed.
std::optional<Document> feed_fetch(std::string_view url,
                                   std::string* err = nullptr);
#endif

}  // namespace xanatoo
