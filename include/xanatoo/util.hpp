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

bool is_text_node_kind(NodeKind k);

// Depth-first list of text-bearing nodes under id (typically doc.root).
void collect_text_nodes(const Document& doc, NodeId id,
                        std::vector<NodeId>& out);

// Resolve #anchor to a text node: exact plain-text match, then heading
// substring. Returns INVALID_NODE if not found.
NodeId find_anchor_node(const Document& doc, std::string_view href);

// Concatenate a node's inline spans.
std::string plain_text(const Document& doc, NodeId id);

// Raster formats supported by stb_image in this project.
bool is_raster_image_path(std::string_view path);

// Case-insensitive suffix check (e.g. ".sexp").
bool path_ends_with_ci(std::string_view path, std::string_view suffix);


// XDG config directory for xanatoo (~/.config/xanatoo). Created if missing.
// Returns empty string if the home directory cannot be resolved.
std::string config_dir();

// Full path to the bookmarks file under config_dir().
std::string bookmarks_path();

// Load bookmarks (one URL/path per line; blank lines and # comments ignored).
std::vector<std::string> load_bookmarks();

// Save bookmarks (creates config dir as needed). Returns false on I/O error.
bool save_bookmarks(const std::vector<std::string>& entries);

}  // namespace xanatoo
