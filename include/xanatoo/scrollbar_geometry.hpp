// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>

namespace xanatoo {

// Geometry shared by scrollbar painting and pointer interaction. The full
// height is the track: there are no arrow-button reservations at either end.
struct ScrollbarGeometry {
  float height = 1.f;
  float thumb_height = 1.f;
  float travel = 0.f;
  float thumb_top = 0.f;

  [[nodiscard]] float scroll_for_thumb(float top, float max_scroll) const {
    if (travel <= 0.f || max_scroll <= 0.f) return 0.f;
    return std::clamp(top / travel, 0.f, 1.f) * max_scroll;
  }

  [[nodiscard]] float scroll_for_click(float y, float max_scroll) const {
    return scroll_for_thumb(y - thumb_height * 0.5f, max_scroll);
  }
};

inline ScrollbarGeometry scrollbar_geometry(float view_height,
                                            float content_height,
                                            float scroll,
                                            float max_scroll) {
  ScrollbarGeometry g;
  g.height = std::max(1.f, view_height);
  g.thumb_height = std::clamp(
      std::max(20.f, g.height * (g.height / std::max(g.height, content_height))),
      1.f, g.height);
  g.travel = std::max(0.f, g.height - g.thumb_height);
  g.thumb_top = g.travel *
      (max_scroll > 0.f ? std::clamp(scroll / max_scroll, 0.f, 1.f) : 0.f);
  return g;
}

}  // namespace xanatoo
