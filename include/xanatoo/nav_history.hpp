// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xanatoo/editor.hpp"

namespace xanatoo::app {

// The link on the preceding page which opened a history entry. Identity is
// preferred to href when the source document has not changed; href remains
// useful after reloading a page whose node IDs may have changed.
struct NavLink {
  std::string href;
  std::string from_url;
  xanatoo::NodeId node = xanatoo::INVALID_NODE;
  size_t span = 0;
};

struct NavHistoryEntry {
  std::string url;
  xanatoo::Selection sel{};
  float scroll = 0.f;
  NavLink via;
};

struct NavHistory {
  std::vector<NavHistoryEntry> entries;
  int pos = -1;
  int restoring = -1;  // Back/Forward destination waiting for a page load
  NavLink pending_link;  // source of an asynchronous new navigation
};

inline void nav_history_capture(NavHistory& h, std::string_view visible,
                                const xanatoo::Selection& sel, float scroll) {
  if (h.restoring >= 0 || h.pos < 0 ||
      h.pos >= static_cast<int>(h.entries.size())) return;
  NavHistoryEntry& e = h.entries[static_cast<size_t>(h.pos)];
  if (e.url != visible) return;
  e.sel = sel;
  if (std::isfinite(scroll)) e.scroll = std::max(0.f, scroll);
}

inline bool nav_history_push(NavHistory& h, NavHistoryEntry entry,
                             size_t limit) {
  if (entry.url.empty()) return false;
  if (h.pos >= 0 && h.pos < static_cast<int>(h.entries.size()) &&
      h.entries[static_cast<size_t>(h.pos)].url == entry.url)
    return false;
  if (h.pos + 1 < static_cast<int>(h.entries.size()))
    h.entries.resize(static_cast<size_t>(h.pos + 1));
  h.entries.push_back(std::move(entry));
  if (h.entries.size() > limit)
    h.entries.erase(h.entries.begin(),
                    h.entries.begin() + (h.entries.size() - limit));
  h.pos = static_cast<int>(h.entries.size()) - 1;
  return true;
}

inline const NavLink* nav_history_forward_link(const NavHistory& h,
                                               std::string_view visible) {
  if (h.restoring >= 0 || h.pos < 0 ||
      h.pos + 1 >= static_cast<int>(h.entries.size()) ||
      h.entries[static_cast<size_t>(h.pos)].url != visible)
    return nullptr;
  const NavLink& link = h.entries[static_cast<size_t>(h.pos + 1)].via;
  return !link.href.empty() && link.from_url == visible ? &link : nullptr;
}

}  // namespace xanatoo::app
