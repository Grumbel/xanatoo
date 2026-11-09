// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace xanatoo {

// Boundaries for a row/column of docked panels. Shares are relative, rather
// than pixel sizes, so an arrangement survives window resizing. A missing or
// malformed share is treated as 1 to keep old sessions usable.
inline std::vector<int> dock_slices(const std::vector<float>& shares,
                                    int extent) {
  std::vector<int> edges;
  edges.reserve(shares.size() + 1);
  edges.push_back(0);
  double sum = 0;
  for (float weight : shares)
    sum += std::isfinite(weight) && weight > 0.f ? weight : 1.;
  double acc = 0;
  for (float weight : shares) {
    acc += std::isfinite(weight) && weight > 0.f ? weight : 1.;
    edges.push_back(std::clamp(static_cast<int>(std::lround(
        std::max(0, extent) * acc / sum)), 0, std::max(0, extent)));
  }
  return edges;
}

// Adjust just the two panels bordering a dragged separator. The neighbouring
// panels keep their combined share, and each keeps a grabbable minimum size.
inline std::pair<float, float> dock_resize_pair(float first_share,
                                                float second_share,
                                                int first_px, int second_px,
                                                int delta_px) {
  const int total = first_px + second_px;
  if (total <= 0) return {first_share, second_share};
  const int min_px = std::min(56, total / 3);
  const int first = std::clamp(first_px + delta_px, min_px, total - min_px);
  const float sum = first_share + second_share;
  return {sum * first / total, sum * (total - first) / total};
}

}  // namespace xanatoo
