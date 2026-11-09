// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <cassert>
#include <cmath>
#include <vector>

#include "xanatoo/dock_slices.hpp"

int main() {
  using xanatoo::dock_slices;
  using xanatoo::dock_resize_pair;

  assert((dock_slices({}, 100) == std::vector<int>{0}));
  assert((dock_slices({1.f}, 300) == std::vector<int>{0, 300}));
  assert((dock_slices({1.f, 1.f}, 201) ==
          std::vector<int>{0, 101, 201}));
  assert((dock_slices({1.f, 2.f, 1.f}, 400) ==
          std::vector<int>{0, 100, 300, 400}));
  assert((dock_slices({0.f, -1.f, NAN}, 300) ==
          std::vector<int>{0, 100, 200, 300}));

  auto [a, b] = dock_resize_pair(1.f, 1.f, 100, 100, 40);
  assert(std::abs(a - 1.4f) < 0.001f);
  assert(std::abs(b - 0.6f) < 0.001f);
  assert((dock_slices({a, b}, 200) == std::vector<int>{0, 140, 200}));
  // Clamp at a grabbable minimum, preserve the pair's total share.
  auto [c, d] = dock_resize_pair(a, b, 140, 60, 9999);
  assert(std::abs(c - 1.44f) < 0.001f);
  assert(std::abs(d - 0.56f) < 0.001f);
  auto [e, f] = dock_resize_pair(1.f, 3.f, 12, 18, -999);
  assert(e > 0.f && f > 0.f && std::abs(e + f - 4.f) < 0.001f);
}
