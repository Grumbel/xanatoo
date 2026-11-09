// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Local Nostr catalogue snapshot for two native dockable panels.

#include "xanatoo/app.hpp"
#include "xanatoo/nostr_store.hpp"

namespace xanatoo::app {

void refresh_nostr_catalogue(App& app) {
  constexpr int kPageSize = 100;
  app.nostr_stats = nostr_cache_stats();
  app.nostr_people = nostr_cached_people(app.nostr_search, kPageSize + 1,
                                         app.nostr_page * kPageSize);
  app.nostr_events = nostr_cached_events(app.nostr_search, kPageSize + 1,
                                         app.nostr_page * kPageSize);
  app.nostr_groups = nostr_cached_groups(app.nostr_search, kPageSize + 1,
                                         app.nostr_page * kPageSize);
  app.nostr_people_more = app.nostr_people.size() > kPageSize;
  app.nostr_events_more = app.nostr_events.size() > kPageSize;
  app.nostr_groups_more = app.nostr_groups.size() > kPageSize;
  if (app.nostr_people_more) app.nostr_people.resize(kPageSize);
  if (app.nostr_events_more) app.nostr_events.resize(kPageSize);
  if (app.nostr_groups_more) app.nostr_groups.resize(kPageSize);
  app.nostr_identities = nostr_local_identities();
  if (app.nostr_active_identity.empty())
    app.nostr_active_identity = nostr_active_local_identity();
  if (app.nostr_active_identity.empty() && !app.nostr_identities.empty())
    app.nostr_active_identity = app.nostr_identities.front().pubkey;
}

}  // namespace xanatoo::app
