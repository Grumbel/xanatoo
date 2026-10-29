// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// C++17-friendly prefix/suffix checks (std::*::starts_with is C++20).
inline bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}
inline bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ASCII-only lowercasing (anchors, extensions, comparisons).
std::string ascii_lower(std::string s);

inline std::string ascii_lower(std::string_view s) {
  return ascii_lower(std::string(s));
}

// Case-insensitive ASCII prefix.
inline bool starts_with_ci(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = s[i], b = prefix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

// Trim ASCII space/tab from both ends (in place).
inline void trim_ascii(std::string_view& s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '	'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '	'))
    s.remove_suffix(1);
}

bool is_text_node_kind(NodeKind k);

// Depth-first list of text-bearing nodes under id (typically doc.root).
void collect_text_nodes(const Document& doc, NodeId id,
                        std::vector<NodeId>& out);

// Resolve #anchor to a text node: exact plain-text match, then heading
// substring. "#fn:key" names a footnote (its first text node). Returns
// INVALID_NODE if not found.
NodeId find_anchor_node(const Document& doc, std::string_view href);

// The Footnote node whose label is `key` (ASCII case-insensitive).
NodeId find_footnote(const Document& doc, std::string_view key);

// "#fn:key" — in-document href of a footnote reference.
inline std::string footnote_href(std::string_view key) {
  return "#fn:" + std::string(key);
}

// Concatenate a node's inline spans.
std::string plain_text(const Document& doc, NodeId id);

// Raster formats supported by stb_image in this project.
bool is_raster_image_path(std::string_view path);

// Case-insensitive suffix check (e.g. ".sexp").
bool path_ends_with_ci(std::string_view path, std::string_view suffix);


// Remove the last UTF-8 code point (no-op on an empty string).
void utf8_pop_back(std::string& s);

// Decode HTML character references to UTF-8: numeric &#NN; / &#xHH;, the
// XML five, Latin-1, lower-case Greek and common punctuation (&rsquo;
// &mdash; &hellip; …). Unknown names stay as written.
std::string html_decode_entities(std::string_view s);

// SHA-256 of `data` as 64 lower-case hex digits (content ids, revstore.hpp).
std::string sha256_hex(std::string_view data);

// Charset of a fetched body: the Content-Type "charset=" parameter, else
// <meta charset> / http-equiv / <?xml encoding?> near the top. Lower-case,
// empty when undeclared.
std::string detect_charset(std::string_view content_type,
                           std::string_view body);

// `body` as UTF-8. Valid UTF-8 is kept whatever the label says; otherwise
// Latin-1 / Windows-1252 / undeclared bytes are converted. Other code
// pages are returned unchanged.
std::string to_utf8(std::string body, std::string_view charset);

// Write a config / state file atomically (gamepkgstd DIR-14): <path>.tmp
// in the same directory (created if missing), flushed, renamed over
// <path>. False, with an error naming the path on stderr, on failure.
bool write_file_atomic(const std::string& path, std::string_view content);

// Full path to the bookmarks file under state_dir().
std::string bookmarks_path();

// Load bookmarks (one URL/path per line; blank lines and # comments ignored).
std::vector<std::string> load_bookmarks();

// Save bookmarks (atomically; nothing with --no-save). False on I/O error.
bool save_bookmarks(const std::vector<std::string>& entries);

}  // namespace xanatoo
