// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Site structure from XML sitemaps (sitemaps.org): robots.txt `Sitemap:`
// lines or /sitemap.xml, sitemap indexes followed, the flat URL list
// turned into a path tree. Shown as a `sitemap:host` page (sections per
// directory, so outline and folding work) and in the Sitemap panel.

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

inline constexpr std::string_view kSitemapPrefix = "sitemap:";

struct SitemapEntry {
  std::string loc;
  std::string lastmod;  // as given (W3C datetime), may be empty
};

// One sitemap file: <urlset> pages, or <sitemapindex> child sitemaps.
struct SitemapFile {
  std::vector<SitemapEntry> urls;
  std::vector<SitemapEntry> sitemaps;
};

SitemapFile parse_sitemap_xml(std::string_view xml);

// `Sitemap:` lines of a robots.txt (any case), in order.
std::vector<std::string> robots_sitemaps(std::string_view robots_txt);

// "sitemap:example.org" (a path or full URL after the prefix is accepted:
// only the host counts).
bool is_sitemap_ref(std::string_view s);
std::string sitemap_ref_host(std::string_view ref);
// Lower-case host of an http(s) URL, empty for anything else.
std::string url_host(std::string_view url);

// Compare key for page URLs: host without "www.", path without trailing
// '/', query kept, scheme and fragment dropped.
std::string site_url_key(std::string_view url);

// Pages as a path tree. Nodes are in pre-order (a node's subtree follows
// it), children: pages first, then directories, each by label.
struct SiteNode {
  std::string label;    // path segment(s), percent-decoded; "/" for root
  std::string url;      // page URL; empty = directory without own page
  std::string lastmod;
  int parent = -1;
  int depth = 0;        // root = 0
  std::vector<int> kids;
  int pages = 0;        // pages in this subtree, including itself
};

struct SiteTree {
  std::string host;
  std::vector<SiteNode> nodes;       // [0] = site root
  std::vector<std::string> sources;  // sitemap files read
  std::vector<std::string> notes;    // skipped files, truncation, …
};

// Paths with one child and no page of their own fold into "a/b".
SiteTree build_site_tree(std::string_view host,
                         const std::vector<SitemapEntry>& urls);
// Node showing `url` (site_url_key match), -1 when not in the tree.
int site_tree_find(const SiteTree& t, std::string_view url);

// `sitemap:host` page: recently changed pages (when the sitemap has
// dates), then one Section per directory with its pages as a List.
Document site_tree_document(const SiteTree& t);

#if defined(XANATOO_WEB) && XANATOO_WEB
// Locate and read the host's sitemaps (robots.txt, then /sitemap.xml,
// /sitemap_index.xml, /wp-sitemap.xml), following indexes up to a cap.
std::optional<SiteTree> sitemap_fetch_tree(std::string_view host,
                                           std::string* err = nullptr);
std::optional<Document> sitemap_fetch(std::string_view ref,
                                      std::string* err = nullptr);
#endif

}  // namespace xanatoo
