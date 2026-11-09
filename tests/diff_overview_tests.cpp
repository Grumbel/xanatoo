// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <cassert>
#include <cmath>

#include "xanatoo/diff_overview.hpp"

int main() {
  xanatoo::Document doc;
  doc.root = 1;
  doc.nodes.resize(5);
  for (size_t i = 0; i < doc.nodes.size(); ++i)
    doc.nodes[i].id = i + 1;
  doc.nodes[0].children = {2, 3, 5};
  doc.nodes[1].kind = xanatoo::NodeKind::Paragraph;
  doc.nodes[2].kind = xanatoo::NodeKind::Section;
  doc.nodes[2].children = {4};
  doc.nodes[3].kind = xanatoo::NodeKind::Paragraph;
  doc.nodes[4].kind = xanatoo::NodeKind::Paragraph;

  // The tree's visual order is 2, 4, 5, not the storage order 2, 3, 4, 5.
  auto positions = xanatoo::diff_overview_positions(doc, {5, 2, 4});
  assert(positions.size() == 3);
  assert(std::abs(positions[0] - 0.f) < 0.001f);
  assert(std::abs(positions[1] - 0.5f) < 0.001f);
  assert(std::abs(positions[2] - 1.f) < 0.001f);

  positions = xanatoo::diff_overview_positions(doc, {4, 99});
  assert(positions.size() == 1);
  assert(std::abs(positions[0] - 0.5f) < 0.001f);
  assert(xanatoo::diff_overview_positions(doc, {}).empty());
  doc.root = xanatoo::INVALID_NODE;
  assert(xanatoo::diff_overview_positions(doc, {4}).empty());
}
