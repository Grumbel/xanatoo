// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace xanatoo {

// Right-click Back/Forward targets in nearest-first order. The signed step
// count is relative to the position in the actual per-pane or split trail.
struct HistoryMenuItem {
  std::string url;
  int steps = 0;
};

inline std::vector<HistoryMenuItem> history_menu_items(
    const std::vector<std::string>& urls, int pos, int direction,
    size_t limit = 20) {
  std::vector<HistoryMenuItem> out;
  if (pos < 0 || pos >= static_cast<int>(urls.size()) ||
      (direction != -1 && direction != 1)) return out;
  for (int i = pos + direction;
       i >= 0 && i < static_cast<int>(urls.size()) && out.size() < limit;
       i += direction)
    out.push_back({urls[static_cast<size_t>(i)], i - pos});
  return out;
}

}  // namespace xanatoo
