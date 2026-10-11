// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/types.hpp"
#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace xanatoo {

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

namespace {
// Stable sentinel so concurrent get() calls do not clobber each other's refs.
Node& invalid_node_mut() {
  static Node dummy = [] {
    Node n;
    n.id = INVALID_NODE;
    n.kind = NodeKind::Paragraph;
    n.parent = INVALID_NODE;
    return n;
  }();
  // Keep dummy inert even if a caller writes through a non-const ref.
  dummy.children.clear();
  dummy.spans.clear();
  dummy.id = INVALID_NODE;
  dummy.kind = NodeKind::Paragraph;
  dummy.parent = INVALID_NODE;
  return dummy;
}
const Node& invalid_node_const() {
  static const Node dummy = [] {
    Node n;
    n.id = INVALID_NODE;
    n.kind = NodeKind::Paragraph;
    n.parent = INVALID_NODE;
    return n;
  }();
  return dummy;
}
}  // namespace

Node& Document::get(NodeId id) {
  size_t idx = static_cast<size_t>(id - 1);
  if (id == INVALID_NODE || idx >= nodes.size())
    return invalid_node_mut();
  return nodes[idx];
}

const Node& Document::get(NodeId id) const {
  size_t idx = static_cast<size_t>(id - 1);
  if (id == INVALID_NODE || idx >= nodes.size())
    return invalid_node_const();
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

bool Document::insert_after(NodeId after, NodeId child) {
  Node& after_n = get(after);
  if (after_n.parent == INVALID_NODE) return false;
  Node& parent = get(after_n.parent);
  auto it = std::find(parent.children.begin(), parent.children.end(), after);
  if (it == parent.children.end()) return false;
  get(child).parent = after_n.parent;
  parent.children.insert(it + 1, child);
  return true;
}

}  // namespace xanatoo
