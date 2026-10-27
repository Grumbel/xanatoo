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
  Heading,           // legacy ATX depth (prefer Header + Section)
  Header,            // generic title (HTML <header> role; no h1–h6 rank)
  Article,           // self-contained composition (HTML <article>)
  Section,           // thematic grouping of blocks (HTML <section>)
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
  Thread,          // discussion container (mailing list / forum thread)
  Message,         // one post or comment; may reply to another Message
  Footnote,        // note body (Paragraph children); label = key of its refs
  Collection,      // ordered set of items (Image / Figure) shown as cards
};

enum class InlineKind : uint8_t {
  Text,
  Emphasis,
  Strong,
  Strike,  // GFM ~~strikethrough~~
  Link,
  Code,
  Cite,  // citation pointer; href holds bibliography key
  FootnoteRef,  // footnote mark; href holds the Footnote key, text the mark
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
  int level = 0;                  // legacy Heading 1-6; List ordered flag; TableCell header
  std::string alt;                // image alt text
  std::string src;                // image path
  bool expanded = true;           // for Stretch
  std::string summary;            // stretchtext collapsed label
  float display_scale = 1.f;      // image interactive scale (1 = natural fit)
  std::string label;              // stable semantic anchor (links: #label)
  std::string tags;               // org-style tags without outer colons ("work:urgent")
  // CodeBlock: language id in `src` (e.g. "cpp"); Note: optional kind in `src`
  // Transclude: summary holds optional :excerpt phrase (range within target)
  // TableCell: level 1 = header cell, 0 = body cell
  // Thread: label = thread id; summary = subject
  // Message: label = message-id; src = from/author; summary = date (ISO);
  //          alt = in-reply-to id, or "about:#anchor" for passage comments
  // Header: text title (no rank); src may hold org TODO keyword; tags for org.
  // Footnote: label = key; body blocks are children (usually Paragraphs).
  // Collection: summary = default view hint ("grid", "list", "full");
  //             items are Image / Figure children (view-local layout).
  // Article/Section: containers grouping blocks.
};

// Document-level semantic metadata (not layout).
struct DocumentMeta {
  std::string title;
  std::string lang;   // BCP 47, e.g. "en"
  std::string author;
  // Markdown YAML front matter, verbatim (between the --- lines) so a save
  // keeps keys xanatoo does not read; title / author / lang come from it.
  std::string front_matter;
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
