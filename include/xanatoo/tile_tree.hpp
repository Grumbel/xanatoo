// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace xanatoo::app {

enum class TileAxis { Columns, Rows };

struct TileRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct TileArea {
  int view = -1;
  TileRect rect;
};

struct TileDivider {
  int node = -1;
  TileAxis axis = TileAxis::Columns;
  TileRect rect;
};

struct TileGeometry {
  std::vector<TileArea> tiles;
  std::vector<TileDivider> dividers;
};

// A geometry-only binary tree. Leaf IDs are external view IDs; this class
// neither owns a document nor knows how a relationship like Trail behaves.
// Its split and close operations preserve all surviving view identities.
class TileTree {
public:
  explicit TileTree(int first_view = 0) { nodes_.push_back(Node{.view = first_view}); }

  bool contains(int view) const { return leaf_node(view) >= 0; }

  int leaf_count() const {
    int count = 0;
    for (const Node& node : nodes_)
      if (node.active && node.view >= 0) ++count;
    return count;
  }

  bool split(int view, int new_view, TileAxis axis, float ratio = 0.5f) {
    const int target = leaf_node(view);
    if (target < 0 || new_view < 0 || contains(new_view)) return false;
    const int first = static_cast<int>(nodes_.size());
    nodes_.push_back(Node{.parent = target, .view = view});
    nodes_.push_back(Node{.parent = target, .view = new_view});
    Node& old = nodes_[static_cast<size_t>(target)];
    old.view = -1;
    old.first = first;
    old.second = first + 1;
    old.axis = axis;
    old.ratio = std::clamp(ratio, 0.05f, 0.95f);
    return true;
  }

  // The final remaining tile cannot be closed.
  bool close(int view) {
    const int leaf = leaf_node(view);
    if (leaf < 0 || leaf == root_) return false;
    const int parent = nodes_[static_cast<size_t>(leaf)].parent;
    const Node& p = nodes_[static_cast<size_t>(parent)];
    const int sibling = p.first == leaf ? p.second : p.first;
    Node replacement = nodes_[static_cast<size_t>(sibling)];
    replacement.parent = p.parent;
    nodes_[static_cast<size_t>(leaf)].active = false;
    nodes_[static_cast<size_t>(sibling)].active = false;
    nodes_[static_cast<size_t>(parent)] = replacement;
    if (replacement.view < 0) {
      nodes_[static_cast<size_t>(replacement.first)].parent = parent;
      nodes_[static_cast<size_t>(replacement.second)].parent = parent;
    }
    return true;
  }

  bool set_axis(int divider, TileAxis axis) {
    if (!valid_divider(divider)) return false;
    nodes_[static_cast<size_t>(divider)].axis = axis;
    return true;
  }

  bool set_ratio(int divider, float ratio) {
    if (!valid_divider(divider) || !std::isfinite(ratio)) return false;
    nodes_[static_cast<size_t>(divider)].ratio = std::clamp(ratio, 0.05f, 0.95f);
    return true;
  }

  TileGeometry geometry(TileRect rect, int gutter) const {
    TileGeometry output;
    calculate(root_, rect, std::max(0, gutter), output);
    return output;
  }

private:
  bool valid_divider(int index) const {
    return index >= 0 && index < static_cast<int>(nodes_.size()) &&
           nodes_[static_cast<size_t>(index)].active &&
           nodes_[static_cast<size_t>(index)].view < 0;
  }

  struct Node {
    int parent = -1;
    int first = -1;
    int second = -1;
    int view = -1;
    TileAxis axis = TileAxis::Columns;
    float ratio = 0.5f;
    bool active = true;
  };

  int leaf_node(int view) const {
    for (size_t i = 0; i < nodes_.size(); ++i)
      if (nodes_[i].active && nodes_[i].view == view)
        return static_cast<int>(i);
    return -1;
  }

  void calculate(int index, TileRect rc, int gutter, TileGeometry& out) const {
    const Node& node = nodes_[static_cast<size_t>(index)];
    if (node.view >= 0) {
      out.tiles.push_back(TileArea{node.view, rc});
      return;
    }
    const bool columns = node.axis == TileAxis::Columns;
    const int length = columns ? rc.w : rc.h;
    const int gap = std::min(gutter, std::max(0, length - 2));
    const int available = std::max(0, length - gap);
    const int minimum = columns ? 120 : 100;
    const int lower = available >= minimum * 2 ? minimum :
                      available >= 2 ? 1 : 0;
    const int first_size = available < 2 ? available : std::clamp(
        static_cast<int>(std::lround(node.ratio * available)),
        lower, available - lower);
    const int second_size = available - first_size;
    TileRect first = rc, second = rc, divider = rc;
    if (columns) {
      first.w = first_size;
      second.x += first_size + gap;
      second.w = second_size;
      divider.x += first_size;
      divider.w = gap;
    } else {
      first.h = first_size;
      second.y += first_size + gap;
      second.h = second_size;
      divider.y += first_size;
      divider.h = gap;
    }
    out.dividers.push_back(TileDivider{index, node.axis, divider});
    calculate(node.first, first, gutter, out);
    calculate(node.second, second, gutter, out);
  }

  int root_ = 0;
  std::vector<Node> nodes_;
};

}  // namespace xanatoo::app
