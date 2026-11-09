// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <vector>

#include "xanatoo/types.hpp"

namespace xanatoo {

// Approximate vertical positions of changed nodes in document order. This
// deliberately does not depend on the visible layout: a virtualized page
// may not have materialized the changed block until the user scrolls to it.
// Each position is a fraction in [0, 1] for a whole-document overview.
inline std::vector<float> diff_overview_positions(
    const Document& doc, const std::vector<NodeId>& changed) {
  if (doc.root == INVALID_NODE || changed.empty()) return {};
  const std::unordered_set<NodeId> marked(changed.begin(), changed.end());
  std::vector<size_t> at;
  size_t total = 0;
  std::unordered_set<NodeId> visited;
  std::function<void(NodeId)> walk = [&](NodeId id) {
    if (id == INVALID_NODE || id > doc.nodes.size() ||
        !visited.insert(id).second) return;
    // Count ordinary content-bearing nodes, including empty paragraphs;
    // container nodes aren't a unit of vertical text height on their own.
    const Node& n = doc.nodes[static_cast<size_t>(id - 1)];
    if (n.children.empty() || !n.spans.empty()) {
      if (marked.contains(id)) at.push_back(total);
      ++total;
    } else if (marked.contains(id)) {
      at.push_back(total);
    }
    for (NodeId child : n.children) walk(child);
  };
  walk(doc.root);
  std::vector<float> positions;
  positions.reserve(at.size());
  for (size_t i : at)
    positions.push_back(total > 1 ? static_cast<float>(i) /
                       static_cast<float>(total - 1) : 0.f);
  std::sort(positions.begin(), positions.end());
  return positions;
}

}  // namespace xanatoo
