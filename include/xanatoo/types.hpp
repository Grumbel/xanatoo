// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace xanatoo {

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
  BlockQuote,      // Markdown "> …" indented quotation
  HorizontalRule,  // Markdown "---" thematic break
  Transclude,      // :::include #Target — live embedding of a passage
  CodeBlock,       // semantic preformatted code (:lang for language)
  Note,            // semantic aside / callout (not a layout box)
  DefList,         // definition list container
  Term,            // term in a definition list
  Definition,      // definition body for the preceding term
  Figure,          // media + caption as one semantic unit
  Caption,         // caption under a figure (child of Figure)
  Biblio,          // bibliography section (container)
  BiblioEntry,     // one reference; label/key matches cite keys
  Table,           // semantic grid (rows of cells; not pixel layout)
  TableRow,
  TableCell,       // level 0 = data cell, 1 = header cell
};

enum class InlineKind : uint8_t {
  Text,
  Emphasis,
  Strong,
  Link,
  Code,
  Cite,  // citation pointer; href holds bibliography key
};

struct InlineSpan {
  InlineKind kind = InlineKind::Text;
  std::string text;
  std::string href;   // Link target, or Cite key
  std::string rel;    // typed relation: glossary, source, contra, …
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
  std::string label;              // stable semantic anchor (links: #label)
  // CodeBlock: language id in `src` (e.g. "cpp"); Note: optional kind in `src`
  // Transclude: summary holds optional :excerpt phrase (range within target)
  // TableCell: level 1 = header cell, 0 = body cell
};

// Document-level semantic metadata (not layout).
struct DocumentMeta {
  std::string title;
  std::string lang;   // BCP 47, e.g. "en"
  std::string author;
};

// Simple arena-style store: contiguous nodes, IDs are indices+1
struct Document {
  std::vector<Node> nodes;
  NodeId root = INVALID_NODE;
  NodeId next_id = 1;
  DocumentMeta meta;

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

}  // namespace xanatoo
