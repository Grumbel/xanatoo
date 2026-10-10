// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace glyphtoo {

using NodeId = uint64_t;
constexpr NodeId INVALID_NODE = 0;

enum class NodeKind : uint8_t {
  Document,
  Heading,
  Paragraph,
  List,
  ListItem,
  Image,
  Stretch,   // expandable block
  Columns,
  Column,
};

enum class InlineKind : uint8_t {
  Text,
  Emphasis,
  Strong,
  Link,
  Code,
};

struct InlineSpan {
  InlineKind kind = InlineKind::Text;
  std::string text;
  std::string href;   // for Link
  NodeId target = INVALID_NODE;
};

struct Node {
  NodeId id = INVALID_NODE;
  NodeKind kind = NodeKind::Paragraph;
  NodeId parent = INVALID_NODE;
  std::vector<NodeId> children;
  std::vector<InlineSpan> spans;  // leaf text content
  int level = 0;                  // heading level 1-6
  std::string alt;                // image alt text
  std::string src;                // image path
  bool expanded = true;           // for Stretch
  std::string summary;            // stretchtext collapsed label
  float display_scale = 1.f;      // image interactive scale (1 = natural fit)
};

// Simple arena-style store: contiguous nodes, IDs are indices+1
struct Document {
  std::vector<Node> nodes;
  NodeId root = INVALID_NODE;
  NodeId next_id = 1;

  NodeId create(NodeKind kind, NodeId parent = INVALID_NODE);
  Node& get(NodeId id);
  const Node& get(NodeId id) const;
  void add_child(NodeId parent, NodeId child);
  void append_text(NodeId node, const std::string& text,
                   InlineKind kind = InlineKind::Text);

  // Insert `child` as a sibling immediately after `after` under the same parent.
  // Returns false if after has no parent or is not found among siblings.
  bool insert_after(NodeId after, NodeId child);
};

}  // namespace glyphtoo
