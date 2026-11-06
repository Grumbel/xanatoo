// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

#include "xanatoo/types.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace xanatoo {

// Derived occupancy; a spanning cell owns every slot in its rectangle.
struct TablePlacement {
  NodeId id = INVALID_NODE;
  size_t row = 0, col = 0, rows = 1, cols = 1;
};

struct TableGrid {
  std::vector<NodeId> rows;
  std::vector<TablePlacement> cells;
  std::vector<std::vector<NodeId>> slots;
  size_t columns = 0;
};

inline TableGrid build_table_grid(const Document& doc, NodeId table) {
  TableGrid grid;
  for (NodeId child : doc.get(table).children)
    if (doc.get(child).kind == NodeKind::TableRow) grid.rows.push_back(child);
  grid.slots.resize(grid.rows.size());
  for (size_t r = 0; r < grid.rows.size(); ++r) {
    size_t col = 0;
    for (NodeId id : doc.get(grid.rows[r]).children) {
      const Node& cell = doc.get(id);
      if (cell.kind != NodeKind::TableCell) continue;
      const size_t cs = static_cast<size_t>(std::clamp(cell.col_span, 1, 1000));
      const size_t rs = std::min(grid.rows.size() - r,
          static_cast<size_t>(std::clamp(cell.row_span, 1, 65534)));
      for (;;) {
        bool free = true;
        for (size_t c = col; c < col + cs && c < grid.slots[r].size(); ++c)
          if (grid.slots[r][c] != INVALID_NODE) { free = false; col = c + 1; break; }
        if (free) break;
      }
      for (size_t rr = r; rr < r + rs; ++rr) {
        if (grid.slots[rr].size() < col + cs)
          grid.slots[rr].resize(col + cs, INVALID_NODE);
        for (size_t cc = col; cc < col + cs; ++cc) grid.slots[rr][cc] = id;
      }
      grid.cells.push_back({id, r, col, rs, cs});
      col += cs;
      grid.columns = std::max(grid.columns, col);
    }
  }
  for (auto& row : grid.slots) row.resize(grid.columns, INVALID_NODE);
  return grid;
}

}  // namespace xanatoo
