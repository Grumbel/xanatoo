// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "xanatoo/types.hpp"

namespace xanatoo {

// Read-only watch on a changing collection, separate from a position bookmark.
// The last successful list of stable item IDs is a *rolling baseline*, not a
// complete archive. First fetch establishes the baseline without marking old
// posts unread. All times are wall-clock Unix seconds, for restarts/sleep.
struct Subscription {
  std::string address;
  std::string title;
  int interval_minutes = 15;
  int64_t checked_at = 0;
  uint32_t unread = 0;
  bool initialized = false;
  std::vector<std::string> recent_ids;
  // Transient UI / scheduler fields, not serialized.
  std::string error;
  uint64_t pending_job = 0;
  bool refresh_requested = false;
};

inline constexpr size_t kSubscriptionSnapshotLimit = 256;

inline bool subscription_interval_valid(int minutes) {
  return minutes == 5 || minutes == 15 || minutes == 30 ||
         minutes == 60 || minutes == 180 || minutes == 720 ||
         minutes == 1440;
}

inline bool subscription_due(const Subscription& s, int64_t now) {
  if (s.pending_job) return false;
  if (s.refresh_requested) return true;
  if (s.checked_at <= 0) return true;
  // A clock moved backwards: don't stall forever on a future timestamp.
  if (s.checked_at > now) return true;
  return now - s.checked_at >= static_cast<int64_t>(s.interval_minutes) * 60;
}

// The keys represent *entries*, not their mutable scores, reaction counts, or
// summaries. On ordinary feeds message IDs are ordinal, so prefer an item link
// or a stable content fallback to avoid marking the whole feed new on reorder.
inline std::vector<std::string> subscription_item_keys(const Document& doc) {
  std::vector<std::string> result;
  std::unordered_set<std::string> unique;
  auto find_item_link = [&](const auto& self, NodeId id, int depth) -> std::string {
    if (!id || depth > 8 || id > doc.nodes.size()) return {};
    const Node& node = doc.get(id);
    for (const auto& span : node.spans)
      if (span.kind == InlineKind::Link && !span.href.empty())
        return span.href;
    for (NodeId child : node.children) {
      auto href = self(self, child, depth + 1);
      if (!href.empty()) return href;
    }
    return {};
  };
  for (const Node& n : doc.nodes) {
    // HN lists use Article; forum, Nostr and RSS feeds use Message.
    if (n.kind != NodeKind::Message && n.kind != NodeKind::Article) continue;
    std::string key;
    const bool ordinal = n.label.size() > 1 && n.label[0] == 'e' &&
        std::all_of(n.label.begin() + 1, n.label.end(),
                    [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (!n.label.empty() && !ordinal) key = "id:" + n.label;
    else {
      std::string link = find_item_link(find_item_link, n.id, 0);
      if (!link.empty()) key = "link:" + link;
      else if (ordinal) {
        // A linkless feed entry still needs a meaningful ID; use the visible
        // first paragraph instead of the unstable position in the list.
        std::string text;
        for (NodeId id : n.children) {
          if (id > doc.nodes.size()) continue;
          for (const auto& span : doc.get(id).spans) {
            text += span.text;
            if (text.size() > 1024) break;
          }
          if (!text.empty()) break;
        }
        if (!text.empty()) key = "text:" + text.substr(0, 1024);
      }
    }
    if (!key.empty() && unique.insert(key).second) {
      result.push_back(std::move(key));
      if (result.size() >= kSubscriptionSnapshotLimit) break;
    }
  }
  return result;
}

inline uint32_t subscription_update(Subscription& s,
                                     std::vector<std::string> ids,
                                     int64_t now) {
  if (ids.size() > kSubscriptionSnapshotLimit)
    ids.resize(kSubscriptionSnapshotLimit);
  const std::unordered_set<std::string> before(s.recent_ids.begin(),
                                                s.recent_ids.end());
  std::unordered_set<std::string> incoming;
  uint32_t added = 0;
  std::vector<std::string> distinct;
  distinct.reserve(ids.size());
  for (auto& id : ids) {
    if (id.empty() || !incoming.insert(id).second) continue;
    if (s.initialized && !before.contains(id)) ++added;
    distinct.push_back(std::move(id));
  }
  s.recent_ids = std::move(distinct);
  s.initialized = true;
  s.checked_at = now;
  s.unread = static_cast<uint32_t>(std::min<uint64_t>(
      static_cast<uint64_t>(s.unread) + added,
      std::numeric_limits<uint32_t>::max()));
  s.error.clear();
  return added;
}

// Persisted in state_dir()/subscriptions.txt; no I/O under --no-save.
std::vector<Subscription> load_subscriptions();
bool save_subscriptions(const std::vector<Subscription>& entries);

}  // namespace xanatoo
