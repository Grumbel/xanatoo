// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace glyphtoo {

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

}  // namespace glyphtoo
