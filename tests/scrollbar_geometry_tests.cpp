// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/scrollbar_geometry.hpp"

#include <cassert>
#include <cmath>

int main() {
  using xanatoo::scrollbar_geometry;
  auto close = [](float a, float b) { return std::fabs(a - b) < 0.001f; };

  // The thumb covers the entire track at zero scroll range; no arrow gaps.
  const auto idle = scrollbar_geometry(200.f, 180.f, 0.f, 0.f);
  assert(close(idle.thumb_height, 200.f));
  assert(close(idle.thumb_top, 0.f));
  assert(close(idle.travel, 0.f));

  const auto top = scrollbar_geometry(200.f, 1000.f, 0.f, 800.f);
  const auto mid = scrollbar_geometry(200.f, 1000.f, 400.f, 800.f);
  const auto bottom = scrollbar_geometry(200.f, 1000.f, 800.f, 800.f);
  assert(close(top.thumb_height, 40.f));
  assert(close(top.thumb_top, 0.f));
  assert(close(mid.thumb_top, 80.f));
  assert(close(bottom.thumb_top + bottom.thumb_height, 200.f));
  assert(close(top.scroll_for_thumb(80.f, 800.f), 400.f));
  assert(close(top.scroll_for_click(20.f, 800.f), 0.f));
  assert(close(top.scroll_for_click(180.f, 800.f), 800.f));
  assert(close(top.scroll_for_thumb(999.f, 800.f), 800.f));

  // An otherwise fitting document can still scroll into the 40px reading
  // pad. That range must leave space for a draggable thumb.
  const auto padded = scrollbar_geometry(200.f, 240.f, 0.f, 40.f);
  assert(padded.thumb_height < padded.height);
  assert(close(padded.scroll_for_thumb(padded.travel, 40.f), 40.f));

  // Extremely short tracks remain well-defined and draggable without
  // overlapping up/down button hit regions.
  const auto tiny = scrollbar_geometry(8.f, 10000.f, 100.f, 9992.f);
  assert(close(tiny.thumb_height, 8.f));
  assert(close(tiny.scroll_for_thumb(5.f, 9992.f), 0.f));
}
