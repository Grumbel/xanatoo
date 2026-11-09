// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Watched changing collections. Fetching is done by app_fetch's read-only
// background worker; the UI never blocks and opening a subscription is normal
// navigation. Do not keep pointers into App::subscriptions across mutations.

#include <algorithm>
#include <ctime>
#include <string_view>

#include "xanatoo/app.hpp"
#include "xanatoo/feed.hpp"
#include "xanatoo/hackernews.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/locator.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {
namespace {

int64_t now_seconds() { return static_cast<int64_t>(std::time(nullptr)); }

Subscription* lookup(App& app, std::string_view address) {
  for (auto& s : app.subscriptions)
    if (s.address == address) return &s;
  return nullptr;
}

std::string normalize(std::string_view address) {
  trim_ascii(address);
  std::string result = parse_locator(address).base;
  // A web link to a built-in community/list and its native address should
  // identify the same subscription, avoiding duplicate watchers.
  if (auto native = native_url_for(result); !native.empty())
    result = std::move(native);
  return result;
}

}  // namespace

bool subscribable_source(std::string_view address) {
  if (address.empty() || address.size() > 1024 ||
      address.find_first_of("\r\n\t") != std::string_view::npos)
    return false;
  std::string url = normalize(address);
  if (xanatoo::is_feed_url(url)) return true;
  if (xanatoo::is_lemmy_ref(url)) {
    // A single post has no changing collection to watch. Community, person
    // and instance front page do.
    return url.find("/post/") == std::string::npos &&
           url.find("/comment/") == std::string::npos;
  }
  if (xanatoo::is_nostr_ref(url)) {
    const std::string low = ascii_lower(url);
    return starts_with(low, "nostr:npub1") ||
           starts_with(low, "nostr:nprofile1") ||
           low == "nostr:recent" || low == "nostr:groups" ||
           starts_with(low, "nostr:group@") || starts_with(low, "nostr:groups@");
  }
  // Group/list pages on the other built-in read-only services.
  if (xanatoo::is_reddit_ref(url))
    return url.find("/comments/") == std::string::npos &&
           url.find("/gallery/") == std::string::npos;
  if (xanatoo::is_hn_ref(url))
    return url.find("item?id=") == std::string::npos;
  return false;
}

std::string subscription_source_address(std::string_view address) {
  return normalize(address);
}

void add_subscription(App& app, std::string address) {
  address = normalize(address);
  if (!subscribable_source(address)) {
    flash_status(app, "Subscribe to a Lemmy community/user, Nostr author/group, or feed");
    return;
  }
  std::string why;
  if (!remote_fetch_supported(address, &why)) {
    flash_status(app, "Cannot subscribe: " + why);
    return;
  }
  if (lookup(app, address)) {
    app.subscriptions_panel_open = true;
    flash_status(app, "Already subscribed to " + address);
    return;
  }
  if (app.subscriptions.size() >= 1024) {
    flash_status(app, "Subscription limit reached (1024)");
    return;
  }
  Subscription s;
  s.address = std::move(address);
  s.title = s.address;
  s.refresh_requested = true;
  app.subscriptions.push_back(std::move(s));
  save_subscriptions(app.subscriptions);
  app.subscriptions_panel_open = true;
  flash_status(app, "Subscribed; checking for latest posts");
}

void remove_subscription(App& app, const std::string& address) {
  const size_t n = app.subscriptions.size();
  app.subscriptions.erase(std::remove_if(app.subscriptions.begin(),
      app.subscriptions.end(), [&](const auto& s) { return s.address == address; }),
      app.subscriptions.end());
  if (app.subscriptions.size() != n) {
    save_subscriptions(app.subscriptions);
    flash_status(app, "Removed subscription");
  }
}

void mark_subscription_read(App& app, const std::string& address) {
  if (Subscription* s = lookup(app, address)) {
    if (!s->unread) return;
    s->unread = 0;
    save_subscriptions(app.subscriptions);
  }
}

void open_subscription(App& app, const std::string& address) {
  if (!lookup(app, address)) return;
  mark_subscription_read(app, address);
  navigate_to_url(app, address);
}

void request_subscription_refresh(App& app, const std::string& address) {
  bool found = false;
  for (auto& s : app.subscriptions) {
    if (!address.empty() && s.address != address) continue;
    s.refresh_requested = true;
    found = true;
  }
  if (found) flash_status(app, "Subscription refresh queued");
}

void set_subscription_interval(App& app, const std::string& address, int minutes) {
  if (!subscription_interval_valid(minutes)) return;
  if (Subscription* s = lookup(app, address)) {
    s->interval_minutes = minutes;
    save_subscriptions(app.subscriptions);
  }
}

void poll_subscriptions(App& app) {
  const int64_t now = now_seconds();
  size_t active = 0;
  for (const auto& s : app.subscriptions) if (s.pending_job) ++active;
  if (active >= 2) return;  // Never fan out the whole list of relays/instances.

  // Requested checks go first, even if regular automatic checks are throttled.
  for (int pass = 0; pass < 2 && active < 2; ++pass) {
    for (auto& s : app.subscriptions) {
      if (s.pending_job || (pass == 0 && !s.refresh_requested) ||
          (pass == 1 && (s.refresh_requested || now < app.next_subscription_auto_at)))
        continue;
      if (!subscription_due(s, now)) continue;
      if (!remote_fetch_supported(s.address, nullptr)) {
        s.error = "Network client not available in this build";
        s.checked_at = now;
        s.refresh_requested = false;
        save_subscriptions(app.subscriptions);
        continue;
      }
      s.pending_job = start_fetch(app, FetchPurpose::Subscription, -1,
                                  s.address, false);
      s.refresh_requested = false;
      app.next_subscription_auto_at = now + 5;  // one automatic start / 5s
      ++active;
      if (pass == 1) return;
      if (active >= 2) break;
    }
  }
}

void finish_subscription_fetch(App& app, const FetchJob& job) {
  Subscription* s = lookup(app, job.url);
  // Removing and readding a subscription must not let an old result overwrite
  // the new one (and a removed feed must never resurrect itself).
  if (!s || s->pending_job != job.id) return;
  s->pending_job = 0;
  s->checked_at = now_seconds();
  if (!job.shared->doc || job.shared->http_status != 0) {
    s->error = job.shared->error.empty() ? "Fetch failed" : job.shared->error;
  } else {
    auto ids = subscription_item_keys(*job.shared->doc);
    if (ids.empty()) {
      s->error = "No identifiable posts in this source";
    } else {
      const uint32_t new_count = subscription_update(*s, std::move(ids), s->checked_at);
      if (new_count) flash_status(app, std::to_string(new_count) +
          " new item(s) in " + s->title);
      if (s->title == s->address && !job.shared->doc->meta.title.empty())
        s->title = job.shared->doc->meta.title.substr(0, 180);
    }
  }
  save_subscriptions(app.subscriptions);
}

}  // namespace xanatoo::app
