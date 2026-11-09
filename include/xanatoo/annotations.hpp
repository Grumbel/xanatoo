// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// An annotation targets content without changing it. The revision records
// the source as it existed when the annotation was attached; the locator can
// later be re-resolved against another revision (or reported unresolved).
struct AnnotationTarget {
  std::string document_key;  // stable document identity, not a pane/NodeId
  std::string revision;      // source revision/content identity at creation
  std::string locator;       // persistent passage address, see locator.hpp

  bool operator==(const AnnotationTarget&) const = default;
};

struct Annotation {
  std::string id;             // stable independently of edits to the body
  std::string kind;           // highlight, note, link, correction, ...
  std::string body;           // empty for a plain highlight
  std::string style;          // optional named style, independent of document
  std::vector<AnnotationTarget> targets;  // may span different documents

  bool operator==(const Annotation&) const = default;
};

// An independently versioned collection. Different users' layers can be
// composed over the same document without copying or editing that document.
struct AnnotationLayer {
  std::string id;
  std::string owner;   // descriptive identity, not yet authenticated
  std::string name;
  std::vector<Annotation> annotations;
  std::string revision; // loaded/saved object head; not part of its payload
};

std::string new_annotation_id();

// Canonical, length-delimited binary payload. Unlike file export, it is
// independent of Markdown/Org/XML and preserves arbitrary UTF-8 text.
std::string serialize_annotation_layer(const AnnotationLayer& layer);
std::optional<AnnotationLayer> parse_annotation_layer(std::string_view payload);

// Returns a new local layer or nullopt if the revision store is unavailable.
std::optional<AnnotationLayer> create_annotation_layer(std::string_view owner,
                                                        std::string_view name);
// Historical revision may be supplied to inspect an older layer snapshot.
std::optional<AnnotationLayer> load_annotation_layer(std::string_view id,
                                                      std::string_view revision = {});
std::vector<AnnotationLayer> list_annotation_layers();
// Atomic optimistic update. Fails if the layer was modified by another writer;
// on success it advances layer.revision. Nothing touches source documents.
bool save_annotation_layer(AnnotationLayer& layer);

// Versioned edits. A failed commit leaves both the layer contents and its
// revision cursor untouched; deletion only removes the annotation from the
// new head (the earlier snapshot remains available by revision id).
bool add_annotation(AnnotationLayer& layer, Annotation annotation);
bool update_annotation_body(AnnotationLayer& layer, std::string_view annotation_id,
                            std::string_view body);
bool remove_annotation(AnnotationLayer& layer, std::string_view annotation_id);

}  // namespace xanatoo
