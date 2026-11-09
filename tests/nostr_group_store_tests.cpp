// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// NIP-29 metadata must be both BIP-340-signed and NIP-11-master bound.
// These fixtures are independently signed with BIP-340 test private key 3.

#include "xanatoo/nostr_store.hpp"
#include "xanatoo/paths.hpp"

#include <sqlite3.h>

#ifdef NDEBUG
#undef NDEBUG  // Assertions execute SQLite setup; also run them in Release.
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr const char* kSelf =
    "f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9";
constexpr const char* kOriginal =
    R"({"id":"57be22489918c23cc660b9aaf61d9c871cf28950821da6bcd2e75efdf360548f","pubkey":"f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9","created_at":321,"kind":39000,"tags":[["d","verified-group"],["name","Verified NIP29"]],"content":"","sig":"334111a62a0863f7421baf17d6391e5801f6dfa3f591246c3236976e103f8d0cde5aea92eebb0082283067df8de03c061db66a12e407d73da797f36123929671"})";
constexpr const char* kNewer =
    R"({"id":"022656fe6f62c4d88facaffe5c8e6ee5449326260bd8ce0d7777e0a583aa171d","pubkey":"f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9","created_at":400,"kind":39000,"tags":[["d","verified-group"],["name","New version"]],"content":"","sig":"5f46db63099186da59fc61050258b2248a0adddd3a310c837d94dab050e4c8b08126cce7d32a24ae7f1a384f9e8b3fe63bcb90279a88febd2b0d51d81ebbe6aa"})";

bool column_exists(sqlite3* db, const char* col) {
  sqlite3_stmt* st = nullptr;
  assert(sqlite3_prepare_v2(db, "PRAGMA table_info(groups)", -1, &st, nullptr) ==
         SQLITE_OK);
  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    if (name && std::string(name) == col) found = true;
  }
  sqlite3_finalize(st);
  return found;
}

}  // namespace

int main() {
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const auto stamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path db_path = fs::temp_directory_path() /
      ("xanatoo-nip11-sqlite-" + stamp + ".sqlite");
  fs::remove(db_path);
  nostr_set_cache_test_path(db_path.string());
  const std::string relay = "wss://groups.example.org";

  nostr_cache_events({kOriginal}, relay);
  auto groups = nostr_cached_groups();
  assert(groups.size() == 1);
  assert(groups[0].name == "Verified NIP29");
  assert(groups[0].metadata_verification == 2);
  assert(!groups[0].relay_master_verified);
  nostr_cache_events({kOriginal}, relay, std::string(64, 'b'));
  assert(!nostr_cached_groups()[0].relay_master_verified);

  nostr_cache_events({kOriginal}, relay, kSelf);
  assert(nostr_cached_groups()[0].relay_master_verified);
  // Losing the NIP-11 endpoint must not downgrade an already trusted event.
  nostr_cache_events({kOriginal}, relay);
  assert(nostr_cached_groups()[0].relay_master_verified);
  // A newer signed but unbound event cannot displace relay-issued metadata.
  nostr_cache_events({kNewer}, relay);
  assert(nostr_cached_groups()[0].name == "Verified NIP29");
  nostr_cache_events({kNewer}, relay, kSelf);
  assert(nostr_cached_groups()[0].name == "New version");
  assert(nostr_cached_groups()[0].relay_master_verified);
  nostr_cache_events({kOriginal}, "wss://another.example.org");
  groups = nostr_cached_groups();
  assert(groups.size() == 2);
  assert(!groups[0].relay_master_verified || !groups[1].relay_master_verified);

  set_no_save(true);
  nostr_cache_events({kNewer}, "wss://never-saved.example.org", kSelf);
  assert(nostr_cached_groups().size() == 2);
  set_no_save(false);
  nostr_set_cache_test_path({});
  fs::remove(db_path);

  // Existing groups must migrate as *unbound*, and --no-save must not write
  // to a pre-migration database even when it is read.
  const fs::path old_path = fs::temp_directory_path() /
      ("xanatoo-nip11-old-" + stamp + ".sqlite");
  fs::remove(old_path);
  sqlite3* db = nullptr;
  assert(sqlite3_open(old_path.string().c_str(), &db) == SQLITE_OK);
  const char* legacy =
      "CREATE TABLE groups (relay TEXT,group_id TEXT,name TEXT,about TEXT,"
      "parent_id TEXT,private_group INTEGER,hidden INTEGER,restricted INTEGER,"
      "closed INTEGER,metadata_time INTEGER,metadata_verification INTEGER,"
      "first_seen INTEGER,last_seen INTEGER,PRIMARY KEY(relay,group_id));"
      "INSERT INTO groups VALUES ('wss://old.example','legacy','Old','',"
      "'',0,0,0,0,100,2,100,100);";
  assert(sqlite3_exec(db, legacy, nullptr, nullptr, nullptr) == SQLITE_OK);
  assert(!column_exists(db, "relay_master_verified"));
  sqlite3_close(db);
  nostr_set_cache_test_path(old_path.string());
  set_no_save(true);
  (void)nostr_cache_stats();
  set_no_save(false);
  assert(sqlite3_open(old_path.string().c_str(), &db) == SQLITE_OK);
  assert(!column_exists(db, "relay_master_verified"));
  sqlite3_close(db);
  (void)nostr_cache_stats();  // triggers conservative schema migration
  assert(sqlite3_open(old_path.string().c_str(), &db) == SQLITE_OK);
  assert(column_exists(db, "relay_master_verified"));
  assert(column_exists(db, "metadata_event_id"));
  sqlite3_close(db);
  groups = nostr_cached_groups();
  assert(groups.size() == 1 && groups[0].name == "Old" &&
         !groups[0].relay_master_verified);
  nostr_set_cache_test_path({});
  fs::remove(old_path);
  std::cout << "NIP-11 group cache, trust priority, migration: PASS\n";
}
