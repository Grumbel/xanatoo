// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/types.hpp"
#include <stdexcept>
#include <cassert>

namespace glyphtoo {

NodeId Document::create(NodeKind kind, NodeId parent) {
  NodeId id = next_id++;
  Node n;
  n.id = id;
  n.kind = kind;
  n.parent = parent;
  nodes.push_back(std::move(n));
  if (parent != INVALID_NODE) {
    add_child(parent, id);
  }
  if (kind == NodeKind::Document) {
    root = id;
  }
  return id;
}

Node& Document::get(NodeId id) {
  assert(id != INVALID_NODE && id < next_id);
  // IDs start at 1; vector is 0-based and may have holes later.
  // For the simple contiguous case we store at index id-1.
  size_t idx = static_cast<size_t>(id - 1);
  if (idx >= nodes.size()) {
    throw std::runtime_error("invalid node id");
  }
  return nodes[idx];
}

const Node& Document::get(NodeId id) const {
  assert(id != INVALID_NODE && id < next_id);
  size_t idx = static_cast<size_t>(id - 1);
  if (idx >= nodes.size()) {
    throw std::runtime_error("invalid node id");
  }
  return nodes[idx];
}

void Document::add_child(NodeId parent, NodeId child) {
  get(parent).children.push_back(child);
  get(child).parent = parent;
}

void Document::append_text(NodeId node, const std::string& text,
                           InlineKind kind) {
  InlineSpan span;
  span.kind = kind;
  span.text = text;
  get(node).spans.push_back(std::move(span));
}

}  // namespace glyphtoo
