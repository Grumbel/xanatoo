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

// Like dock_slices, but a shaded panel (rolled up to its title strip) takes
// exactly `shaded_px` and the unshaded ones share what is left by their
// shares. When the shaded strips do not fit, they shrink evenly.
inline std::vector<int> dock_slices_shaded(const std::vector<float>& shares,
                                           const std::vector<bool>& shaded,
                                           int extent, int shaded_px) {
  extent = std::max(0, extent);
  int n_shaded = 0;
  std::vector<float> open;
  for (size_t i = 0; i < shares.size(); ++i) {
    if (i < shaded.size() && shaded[i])
      ++n_shaded;
    else
      open.push_back(shares[i]);
  }
  if (n_shaded == 0) return dock_slices(shares, extent);
  const int strip = std::min(std::max(0, shaded_px), extent / n_shaded);
  const auto open_edges =
      dock_slices(open, extent - n_shaded * strip);
  std::vector<int> edges;
  edges.reserve(shares.size() + 1);
  edges.push_back(0);
  int pos = 0;
  size_t k = 0;
  for (size_t i = 0; i < shares.size(); ++i) {
    if (i < shaded.size() && shaded[i]) {
      pos += strip;
    } else {
      pos += open_edges[k + 1] - open_edges[k];
      ++k;
    }
    edges.push_back(pos);
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
