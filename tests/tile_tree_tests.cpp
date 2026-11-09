// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <cassert>
#include <set>

#include "xanatoo/tile_tree.hpp"

using namespace xanatoo::app;

int main() {
  TileTree tree(0);
  assert(tree.leaf_count() == 1);
  assert(!tree.close(0));
  assert(tree.split(0, 1, TileAxis::Columns));
  assert(!tree.split(0, 1, TileAxis::Rows));
  auto g = tree.geometry({0, 0, 100, 80}, 8);
  assert(g.tiles.size() == 2 && g.dividers.size() == 1);
  assert(g.tiles[0].view == 0 && g.tiles[0].rect.w == 46);
  assert(g.tiles[1].view == 1 && g.tiles[1].rect.x == 54);
  assert(tree.set_ratio(g.dividers[0].node, 0.7f));
  assert(tree.geometry({0, 0, 100, 80}, 8).tiles[0].rect.w == 64);
  assert(tree.set_axis(g.dividers[0].node, TileAxis::Rows));
  assert(tree.geometry({0, 0, 100, 80}, 8).tiles[1].rect.y > 0);
  assert(!tree.set_axis(999, TileAxis::Columns));
  assert(!tree.set_ratio(999, 0.5f));
  assert(tree.set_axis(g.dividers[0].node, TileAxis::Columns));

  TileTree stacked(7);
  assert(stacked.split(7, 8, TileAxis::Rows, 0.75f));
  const auto stacked_g = stacked.geometry({10, 20, 300, 400}, 12);
  assert(stacked_g.tiles[0].rect.y == 20);
  // The minimum 100px second pane clamps the requested 75% split.
  assert(stacked_g.tiles[1].rect.y == 320);
  assert(stacked_g.dividers[0].axis == TileAxis::Rows);
  assert(stacked_g.dividers[0].rect.h == 12);
  const auto tiny = stacked.geometry({0, 0, 1, 1}, 8);
  assert(tiny.tiles.size() == 2);
  assert(tiny.tiles[0].rect.h + tiny.tiles[1].rect.h <= 1);

  assert(tree.split(0, 2, TileAxis::Rows, 0.25f));
  assert(tree.split(2, 3, TileAxis::Columns));
  assert(tree.leaf_count() == 4);
  g = tree.geometry({0, 0, 100, 80}, 8);
  assert(g.tiles.size() == 4 && g.dividers.size() == 3);
  std::set<int> views;
  for (const TileArea& tile : g.tiles) {
    assert(tile.rect.w > 0 && tile.rect.h > 0);
    views.insert(tile.view);
  }
  assert((views == std::set<int>{0, 1, 2, 3}));

  // Closing a nested leaf collapses its parent but retains sibling view IDs.
  assert(tree.close(2));
  assert(tree.leaf_count() == 3 && !tree.contains(2));
  assert(tree.contains(0) && tree.contains(1) && tree.contains(3));
  assert(tree.close(3));
  assert(tree.leaf_count() == 2);
  assert(tree.close(0));
  assert(tree.leaf_count() == 1 && tree.contains(1));
  assert(!tree.close(1));
  g = tree.geometry({0, 0, 100, 80}, 8);
  assert(g.tiles.size() == 1 && g.dividers.empty());
}
