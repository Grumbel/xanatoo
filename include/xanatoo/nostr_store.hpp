// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

struct NostrCacheStats {
  int64_t events = 0;
  int64_t notes = 0;
  int64_t profiles = 0;
  int64_t pubkeys = 0;
  int64_t relays = 0;
  int64_t signed_events = 0;  // BIP-340 signature-verified events
  int64_t legacy_events = 0;  // observations from before verification
  int64_t groups = 0;         // distinct (relay, group ID) observations
};

struct NostrCachedPerson {
  std::string pubkey;
  std::string name;  // unverified kind-0 metadata, may be absent
  std::string about;
  std::string nip05; // unverified claim, not proof of NIP-05 ownership
  int64_t post_count = 0;  // locally observed unique kind-1 events
  int64_t event_count = 0;
  int64_t last_seen = 0;
  int profile_verification = 0;  // 1: hash checked; 2: BIP-340 checked
};

struct NostrCachedEvent {
  std::string id;
  std::string pubkey;
  std::string author_name;
  std::string content;
  int kind = 0;
  int64_t created_at = 0;
  int verification = 0;
};

// NIP-29 groups are relay-scoped: the same group_id on two different relays
// denotes two independently governed groups. Metadata is only an observation;
// verifying its event signature does NOT prove it came from the relay's NIP-11
// advertised master key.
struct NostrCachedGroup {
  std::string relay;
  std::string group_id;
  std::string name;
  std::string about;
  std::string parent_id;
  int64_t event_count = 0;  // unique validated h-tagged events from this relay
  int64_t message_count = 0;  // locally observed kinds 1, 9, 11
  int64_t last_seen = 0;
  int metadata_verification = 0;
  bool private_group = false;
  bool hidden = false;
  bool restricted = false;
  bool closed = false;
};

struct NostrLocalIdentity {
  std::string pubkey;
  std::string label;
  // Only watch-only identities for now: no local private keys are stored.
};

// Cache the exact event JSONs returned by a relay, including kind-0 profiles.
// Idempotent on event id; sources are deduplicated per (event, relay).
// An unverified relay event is *never* proof of identity. Do not persist with
// --no-save, or when the SQLite feature is unavailable.
void nostr_cache_events(const std::vector<std::string>& event_json,
                        std::string_view relay);
NostrCacheStats nostr_cache_stats();
std::vector<NostrCachedPerson> nostr_cached_people(std::string_view query = {},
                                                   int limit = 100,
                                                   int offset = 0);
std::vector<NostrCachedEvent> nostr_cached_events(std::string_view query = {},
                                                  int limit = 100,
                                                  int offset = 0);
std::vector<NostrCachedGroup> nostr_cached_groups(std::string_view query = {},
                                                 int limit = 100,
                                                 int offset = 0);
std::vector<std::string> nostr_cached_group_json(std::string_view relay,
                                                  std::string_view group_id);
// Exact relay JSON for offline fallback. These are local, unverified copies.
std::vector<std::string> nostr_cached_thread_json(std::string_view event_id);
std::vector<std::string> nostr_cached_author_json(std::string_view pubkey);
std::vector<std::string> nostr_cached_recent_json();

// Local *watch-only* profiles, not signing accounts. Adding one cannot
// publish events; a NIP-46/NIP-49 signer workflow is future work.
std::vector<NostrLocalIdentity> nostr_local_identities();
bool nostr_add_watch_identity(std::string_view label, std::string_view npub_or_hex);
std::string nostr_active_local_identity();
bool nostr_set_active_local_identity(std::string_view hex_pubkey);

// Test-only override. Pass an empty string to restore the XDG state path.
void nostr_set_cache_test_path(std::string path);

}  // namespace xanatoo
