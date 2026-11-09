// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Persistent *observations*, not a trusted global Nostr directory.

#include "xanatoo/nostr_store.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/nostr_verify.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <set>
#include <utility>

#ifndef XANATOO_SQLITE
#define XANATOO_SQLITE 0
#endif
#if XANATOO_SQLITE
#include <sqlite3.h>
#endif

namespace xanatoo {
namespace {

std::string cache_test_path;

bool hex_pubkey(std::string_view str) {
  return str.size() == 64 &&
      std::all_of(str.begin(), str.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
      });
}

#if XANATOO_SQLITE

bool table_has_column(sqlite3* db, const char* table, const char* column) {
  const std::string sql = std::string("PRAGMA table_info(") + table + ");";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    return false;
  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    if (name && column == std::string_view(name)) { found = true; break; }
  }
  sqlite3_finalize(st);
  return found;
}

sqlite3* open_cache(bool writing) {
  if (writing && no_save()) return nullptr;
  const std::string dir = cache_test_path.empty() ? state_dir() : std::string();
  const std::string path = cache_test_path.empty() ?
      (dir.empty() ? std::string() : dir + "/nostr.sqlite") : cache_test_path;
  if (path.empty()) return nullptr;
  std::error_code ec;
  if (writing ? (!cache_test_path.empty() ? false : !ensure_dir(dir)) :
                !std::filesystem::exists(path, ec))
    return nullptr;
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return nullptr;
  }
  sqlite3_busy_timeout(db, 1500);
  const bool old_events = !table_has_column(db, "events", "verification");
  const bool old_people = !table_has_column(db, "people", "profile_verification");
  const bool missing_groups = !table_has_column(db, "groups", "group_id");
  if (writing || (!no_save() && (old_events || old_people || missing_groups))) {
    const char* schema =
        "CREATE TABLE IF NOT EXISTS events ("
        " id TEXT PRIMARY KEY, pubkey TEXT NOT NULL, kind INTEGER NOT NULL,"
        " created_at INTEGER NOT NULL, content TEXT NOT NULL,"
        " raw_json TEXT NOT NULL, first_seen INTEGER NOT NULL,"
        " verification INTEGER NOT NULL DEFAULT 0);"
        "CREATE INDEX IF NOT EXISTS nostr_events_author ON events(pubkey, kind);"
        "CREATE INDEX IF NOT EXISTS nostr_events_time ON events(created_at DESC);"
        "CREATE TABLE IF NOT EXISTS people ("
        " pubkey TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '',"
        " about TEXT NOT NULL DEFAULT '', nip05 TEXT NOT NULL DEFAULT '',"
        " profile_time INTEGER NOT NULL DEFAULT 0,"
        " profile_verification INTEGER NOT NULL DEFAULT 0,"
        " first_seen INTEGER NOT NULL, last_seen INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS event_relays ("
        " event_id TEXT NOT NULL, relay TEXT NOT NULL,"
        " PRIMARY KEY(event_id,relay));"
        "CREATE TABLE IF NOT EXISTS event_refs ("
        " event_id TEXT NOT NULL, referenced_id TEXT NOT NULL,"
        " PRIMARY KEY(event_id,referenced_id));"
        "CREATE INDEX IF NOT EXISTS nostr_event_refs_target ON event_refs(referenced_id);"
        "CREATE TABLE IF NOT EXISTS local_identities ("
        " pubkey TEXT PRIMARY KEY, label TEXT NOT NULL, added INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS local_settings ("
        " key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS groups ("
        " relay TEXT NOT NULL, group_id TEXT NOT NULL,"
        " name TEXT NOT NULL DEFAULT '', about TEXT NOT NULL DEFAULT '',"
        " parent_id TEXT NOT NULL DEFAULT '',"
        " private_group INTEGER NOT NULL DEFAULT 0,"
        " hidden INTEGER NOT NULL DEFAULT 0,"
        " restricted INTEGER NOT NULL DEFAULT 0,"
        " closed INTEGER NOT NULL DEFAULT 0,"
        " metadata_time INTEGER NOT NULL DEFAULT 0,"
        " metadata_verification INTEGER NOT NULL DEFAULT 0,"
        " first_seen INTEGER NOT NULL, last_seen INTEGER NOT NULL,"
        " PRIMARY KEY(relay, group_id));"
        "CREATE TABLE IF NOT EXISTS group_events ("
        " relay TEXT NOT NULL, group_id TEXT NOT NULL, event_id TEXT NOT NULL,"
        " PRIMARY KEY(relay,group_id,event_id));"
        "CREATE INDEX IF NOT EXISTS nostr_group_events_event"
        " ON group_events(event_id);";
    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
      sqlite3_close(db);
      return nullptr;
    }
  }
  // Preserve earlier observations, but do not misrepresent pre-verifier
  // records as trusted. They remain in SQLite with verification=0 and are
  // excluded from validated event queries until legitimately refetched.
  // Reading an existing database may trigger migration, except --no-save.
  if (!no_save()) {
    if ((old_events && !table_has_column(db, "events", "verification") &&
         sqlite3_exec(db, "ALTER TABLE events ADD COLUMN verification INTEGER NOT NULL DEFAULT 0",
                      nullptr, nullptr, nullptr) != SQLITE_OK) ||
        (old_people && !table_has_column(db, "people", "profile_verification") &&
         sqlite3_exec(db, "ALTER TABLE people ADD COLUMN profile_verification INTEGER NOT NULL DEFAULT 0",
                      nullptr, nullptr, nullptr) != SQLITE_OK)) {
      sqlite3_close(db);
      return nullptr;
    }
  }
  return db;
}

struct Statement {
  sqlite3_stmt* p = nullptr;
  Statement(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &p, nullptr) != SQLITE_OK) p = nullptr;
  }
  ~Statement() { if (p) sqlite3_finalize(p); }
  bool valid() const { return p != nullptr; }
  void reset() { sqlite3_reset(p); sqlite3_clear_bindings(p); }
  void bind(int col, std::string_view v) {
    // A default-constructed empty string_view has data()==nullptr, which
    // sqlite3_bind_text interprets as SQL NULL rather than the empty string.
    // Empty search queries must match all rows, not produce no results.
    sqlite3_bind_text(p, col, v.empty() ? "" : v.data(),
                      static_cast<int>(v.size()), SQLITE_TRANSIENT);
  }
  void bind(int col, int64_t v) { sqlite3_bind_int64(p, col, v); }
  bool exec() { return sqlite3_step(p) == SQLITE_DONE; }
  bool next() { return sqlite3_step(p) == SQLITE_ROW; }
  std::string text(int col) const {
    const unsigned char* s = sqlite3_column_text(p, col);
    return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
  }
  int64_t integer(int col) const { return sqlite3_column_int64(p, col); }
};

// Close only after every prepared statement in the surrounding scope is
// finalized. sqlite3_close() otherwise returns SQLITE_BUSY and leaks a DB
// handle on each fetch/search.
struct DbOwner {
  sqlite3* db = nullptr;
  ~DbOwner() { if (db) sqlite3_close(db); }
};

#endif
}  // namespace

void nostr_set_cache_test_path(std::string path) {
  cache_test_path = std::move(path);
}

void nostr_cache_events(const std::vector<std::string>& events,
                        std::string_view relay) {
#if XANATOO_SQLITE
  if (events.empty()) return;
  sqlite3* db = open_cache(true);
  if (!db) return;
  DbOwner owner{db};
  Statement put_event(db, "INSERT INTO events"
       "(id,pubkey,kind,created_at,content,raw_json,first_seen,verification)"
       "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET"
       " pubkey=excluded.pubkey,kind=excluded.kind,"
       " created_at=excluded.created_at,content=excluded.content,"
       " raw_json=excluded.raw_json,verification=excluded.verification"
       " WHERE events.verification<excluded.verification;");
  Statement put_person(db, "INSERT INTO people(pubkey,first_seen,last_seen)"
       " VALUES(?,?,?) ON CONFLICT(pubkey) DO UPDATE SET"
       " last_seen=MAX(last_seen,excluded.last_seen);");
  Statement put_profile(db, "UPDATE people SET name=?,about=?,nip05=?,"
       " profile_time=?,profile_verification=? WHERE pubkey=? AND"
       " (profile_verification<? OR"
       " (profile_verification=? AND profile_time<=?));");
  Statement put_relay(db, "INSERT OR IGNORE INTO event_relays(event_id,relay)"
       " VALUES(?,?);");
  Statement put_ref(db, "INSERT OR IGNORE INTO event_refs(event_id,referenced_id)"
       " VALUES(?,?);");
  Statement put_group(db, "INSERT INTO groups(relay,group_id,first_seen,last_seen)"
       " VALUES(?,?,?,?) ON CONFLICT(relay,group_id) DO UPDATE SET"
       " last_seen=MAX(last_seen,excluded.last_seen);");
  Statement put_group_event(db, "INSERT OR IGNORE INTO group_events"
       " (relay,group_id,event_id) VALUES(?,?,?);");
  Statement put_group_meta(db, "UPDATE groups SET name=?,about=?,parent_id=?,"
       " private_group=?,hidden=?,restricted=?,closed=?,"
       " metadata_time=?,metadata_verification=?"
       " WHERE relay=? AND group_id=? AND"
       " (metadata_verification<? OR"
       " (metadata_verification=? AND metadata_time<=?));");
  if (!put_event.valid() || !put_person.valid() || !put_profile.valid() ||
      !put_relay.valid() || !put_ref.valid() || !put_group.valid() ||
      !put_group_event.valid() || !put_group_meta.valid() ||
      sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
    return;
  }
  const int64_t observed = static_cast<int64_t>(std::time(nullptr));
  bool ok = true;
  for (const std::string& raw : events) {
    const NostrVerification verification = nostr_verify_event(raw);
    if (verification == NostrVerification::Invalid) continue;
    // An ID match alone does not authenticate the author on a build without
    // Schnorr verification. Preserve that distinction in every cache row.
    const std::string obj = json_top_level(raw);
    std::string id = ascii_lower(json_str_field(obj, "id"));
    std::string author = ascii_lower(json_str_field(obj, "pubkey"));
    if (!hex_pubkey(id) || !hex_pubkey(author)) continue;
    const int64_t timestamp = json_int_field(obj, "created_at");
    const int64_t kind = json_int_field(obj, "kind");
    const std::string content = json_str_field(obj, "content");
    put_event.reset();
    put_event.bind(1, id);
    put_event.bind(2, author);
    put_event.bind(3, kind);
    put_event.bind(4, timestamp);
    put_event.bind(5, content);
    put_event.bind(6, raw);
    put_event.bind(7, observed);
    put_event.bind(8, static_cast<int64_t>(verification));
    if (!put_event.exec()) { ok = false; break; }
    if (!relay.empty()) {
      put_relay.reset();
      put_relay.bind(1, id);
      put_relay.bind(2, relay);
      if (!put_relay.exec()) { ok = false; break; }
    }
    const auto tags = nostr_event_tags(raw);
    std::set<std::string> people{author};
    for (const auto& tag : tags) {
      if (tag.size() < 2 || !hex_pubkey(tag[1])) continue;
      if (tag[0] == "p") people.insert(ascii_lower(tag[1]));
      if (tag[0] == "e") {
        put_ref.reset();
        put_ref.bind(1, id);
        put_ref.bind(2, ascii_lower(tag[1]));
        if (!put_ref.exec()) { ok = false; break; }
      }
    }
    if (!ok) break;
    if (!relay.empty()) {
      const auto group_id_valid = [](std::string_view gid) {
        return !gid.empty() && gid.size() <= 256 &&
            std::none_of(gid.begin(), gid.end(), [](unsigned char c) {
              return c < 0x20 || c == 0x7f;
            });
      };
      // Use the relay that delivered the event, never just its h/d tag. A
      // group ID by itself is NOT a globally unique NIP-29 group identity.
      std::set<std::string> group_ids;
      std::string d, name, about, parent;
      bool private_group = false, hidden = false, restricted = false, closed = false;
      for (const auto& tag : tags) {
        if (tag.empty()) continue;
        if (kind == 39000) {
          if (tag[0] == "d" && tag.size() > 1) d = tag[1];
          if (tag[0] == "name" && tag.size() > 1) name = tag[1].substr(0, 256);
          if (tag[0] == "about" && tag.size() > 1) about = tag[1].substr(0, 4096);
          if (tag[0] == "parent" && tag.size() > 1) parent = tag[1].substr(0, 256);
          if (tag[0] == "private") private_group = true;
          if (tag[0] == "hidden") hidden = true;
          if (tag[0] == "restricted") restricted = true;
          if (tag[0] == "closed") closed = true;
        } else if (tag[0] == "h" && tag.size() > 1 && group_id_valid(tag[1])) {
          group_ids.insert(tag[1]);
        }
      }
      if (kind == 39000 && group_id_valid(d)) group_ids.insert(d);
      for (const auto& gid : group_ids) {
        put_group.reset();
        put_group.bind(1, relay);
        put_group.bind(2, gid);
        put_group.bind(3, observed);
        put_group.bind(4, observed);
        if (!put_group.exec()) { ok = false; break; }
        if (kind == 39000) {
          put_group_meta.reset();
          put_group_meta.bind(1, name);
          put_group_meta.bind(2, about);
          put_group_meta.bind(3, parent);
          put_group_meta.bind(4, int64_t(private_group));
          put_group_meta.bind(5, int64_t(hidden));
          put_group_meta.bind(6, int64_t(restricted));
          put_group_meta.bind(7, int64_t(closed));
          put_group_meta.bind(8, timestamp);
          put_group_meta.bind(9, static_cast<int64_t>(verification));
          put_group_meta.bind(10, relay);
          put_group_meta.bind(11, gid);
          put_group_meta.bind(12, static_cast<int64_t>(verification));
          put_group_meta.bind(13, static_cast<int64_t>(verification));
          put_group_meta.bind(14, timestamp);
          if (!put_group_meta.exec()) { ok = false; break; }
        } else {
          put_group_event.reset();
          put_group_event.bind(1, relay);
          put_group_event.bind(2, gid);
          put_group_event.bind(3, id);
          if (!put_group_event.exec()) { ok = false; break; }
        }
      }
      if (!ok) break;
    }
    for (auto& pk : nostr_mentioned_pubkeys(content)) people.insert(pk);
    for (const auto& pk : people) {
      put_person.reset();
      put_person.bind(1, pk);
      put_person.bind(2, observed);
      put_person.bind(3, observed);
      if (!put_person.exec()) { ok = false; break; }
    }
    if (!ok) break;
    if (kind == 0 && timestamp > 0) {
      std::string name = json_str_field(content, "display_name");
      if (name.empty()) name = json_str_field(content, "displayName");
      if (name.empty()) name = json_str_field(content, "name");
      put_profile.reset();
      put_profile.bind(1, name.substr(0, 256));
      put_profile.bind(2, json_str_field(content, "about").substr(0, 4096));
      put_profile.bind(3, json_str_field(content, "nip05").substr(0, 256));
      put_profile.bind(4, timestamp);
      put_profile.bind(5, static_cast<int64_t>(verification));
      put_profile.bind(6, author);
      put_profile.bind(7, static_cast<int64_t>(verification));
      put_profile.bind(8, static_cast<int64_t>(verification));
      put_profile.bind(9, timestamp);
      if (!put_profile.exec()) { ok = false; break; }
    }
  }
  sqlite3_exec(db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
#else
  (void)events; (void)relay;
#endif
}

NostrCacheStats nostr_cache_stats() {
  NostrCacheStats out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  if (!table_has_column(db, "events", "verification")) {
    Statement old(db, "SELECT COUNT(*) FROM events;");
    if (old.valid() && old.next()) out.legacy_events = old.integer(0);
    return out;
  }
  // --no-save must not migrate a 310-era cache just to show its counters.
  // The group table might not exist yet; everything else stays readable.
  const std::string group_count_sql =
      table_has_column(db, "groups", "group_id") ?
      "(SELECT COUNT(*) FROM groups)" : "0";
  const std::string stats_sql = std::string("SELECT") +
       " (SELECT COUNT(*) FROM events WHERE verification>0),"
       " (SELECT COUNT(*) FROM events WHERE kind=1 AND verification>0),"
       " (SELECT COUNT(*) FROM people WHERE profile_verification>0),"
       " (SELECT COUNT(*) FROM people),"
       " (SELECT COUNT(DISTINCT r.relay) FROM event_relays r"
       " JOIN events e ON e.id=r.event_id WHERE e.verification>0),"
       " (SELECT COUNT(*) FROM events WHERE verification=2),"
       " (SELECT COUNT(*) FROM events WHERE verification=0)," +
       group_count_sql + ";";
  Statement st(db, stats_sql.c_str());
  if (st.valid() && st.next()) {
    out.events = st.integer(0);
    out.notes = st.integer(1);
    out.profiles = st.integer(2);
    out.pubkeys = st.integer(3);
    out.relays = st.integer(4);
    out.signed_events = st.integer(5);
    out.legacy_events = st.integer(6);
    out.groups = st.integer(7);
  }
#endif
  return out;
}

std::vector<NostrCachedPerson> nostr_cached_people(std::string_view query,
                                                   int limit, int offset) {
  std::vector<NostrCachedPerson> out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, "SELECT p.pubkey,"
       " CASE WHEN p.profile_verification>0 THEN p.name ELSE '' END,"
       " CASE WHEN p.profile_verification>0 THEN p.about ELSE '' END,"
       " CASE WHEN p.profile_verification>0 THEN p.nip05 ELSE '' END,p.last_seen,"
       " COUNT(e.id),COALESCE(SUM(CASE WHEN e.kind=1 THEN 1 ELSE 0 END),0),"
       " p.profile_verification"
       " FROM people p LEFT JOIN events e ON e.pubkey=p.pubkey AND e.verification>0"
       " WHERE instr(lower(p.pubkey),lower(?))>0 OR"
       " (p.profile_verification>0 AND instr(lower(p.name),lower(?))>0) OR"
       " (p.profile_verification>0 AND instr(lower(p.nip05),lower(?))>0)"
       " GROUP BY p.pubkey ORDER BY 7 DESC,p.last_seen DESC LIMIT ? OFFSET ?;");
  if (st.valid()) {
    for (int i = 1; i <= 3; ++i) st.bind(i, query);
    st.bind(4, static_cast<int64_t>(std::clamp(limit, 1, 500)));
    st.bind(5, static_cast<int64_t>(std::max(0, offset)));
    while (st.next()) out.push_back({st.text(0), st.text(1), st.text(2),
                                    st.text(3), st.integer(6), st.integer(5),
                                    st.integer(4), static_cast<int>(st.integer(7))});
  }
#else
  (void)query; (void)limit; (void)offset;
#endif
  return out;
}

std::vector<NostrCachedEvent> nostr_cached_events(std::string_view query,
                                                  int limit, int offset) {
  std::vector<NostrCachedEvent> out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, "SELECT e.id,e.pubkey,"
       " CASE WHEN p.profile_verification>0 THEN p.name ELSE '' END,"
       " e.content,e.kind,e.created_at,e.verification"
       " FROM events e LEFT JOIN people p ON p.pubkey=e.pubkey"
       " WHERE e.verification>0 AND (instr(lower(e.content),lower(?))>0 OR"
       " instr(lower(e.pubkey),lower(?))>0 OR"
       " (p.profile_verification>0 AND instr(lower(p.name),lower(?))>0))"
       " ORDER BY e.created_at DESC,e.id DESC LIMIT ? OFFSET ?;");
  if (st.valid()) {
    for (int i = 1; i <= 3; ++i) st.bind(i, query);
    st.bind(4, static_cast<int64_t>(std::clamp(limit, 1, 500)));
    st.bind(5, static_cast<int64_t>(std::max(0, offset)));
    while (st.next()) out.push_back({st.text(0), st.text(1), st.text(2),
                                    st.text(3), static_cast<int>(st.integer(4)),
                                    st.integer(5), static_cast<int>(st.integer(6))});
  }
#else
  (void)query; (void)limit; (void)offset;
#endif
  return out;
}

std::vector<NostrCachedGroup> nostr_cached_groups(std::string_view query,
                                                 int limit, int offset) {
  std::vector<NostrCachedGroup> out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, "SELECT g.relay,g.group_id,"
       " CASE WHEN g.metadata_verification>0 THEN g.name ELSE '' END,"
       " CASE WHEN g.metadata_verification>0 THEN g.about ELSE '' END,"
       " CASE WHEN g.metadata_verification>0 THEN g.parent_id ELSE '' END,"
       " COUNT(e.id),COALESCE(SUM(CASE WHEN e.kind IN (1,9,11) THEN 1 ELSE 0 END),0),"
       " g.last_seen,g.metadata_verification,g.private_group,g.hidden,g.restricted,g.closed"
       " FROM groups g LEFT JOIN group_events ge"
       " ON ge.relay=g.relay AND ge.group_id=g.group_id"
       " LEFT JOIN events e ON e.id=ge.event_id AND e.verification>0"
       " WHERE instr(lower(g.relay),lower(?))>0 OR"
       " instr(lower(g.group_id),lower(?))>0 OR"
       " (g.metadata_verification>0 AND instr(lower(g.name),lower(?))>0) OR"
       " (g.metadata_verification>0 AND instr(lower(g.about),lower(?))>0) OR"
       " (g.metadata_verification>0 AND instr(lower(g.parent_id),lower(?))>0)"
       " GROUP BY g.relay,g.group_id"
       " ORDER BY 7 DESC,g.last_seen DESC,g.relay,g.group_id LIMIT ? OFFSET ?;");
  if (st.valid()) {
    for (int i = 1; i <= 5; ++i) st.bind(i, query);
    st.bind(6, static_cast<int64_t>(std::clamp(limit, 1, 500)));
    st.bind(7, static_cast<int64_t>(std::max(0, offset)));
    while (st.next()) {
      NostrCachedGroup group;
      group.relay = st.text(0);
      group.group_id = st.text(1);
      group.name = st.text(2);
      group.about = st.text(3);
      group.parent_id = st.text(4);
      group.event_count = st.integer(5);
      group.message_count = st.integer(6);
      group.last_seen = st.integer(7);
      group.metadata_verification = static_cast<int>(st.integer(8));
      group.private_group = st.integer(9) != 0;
      group.hidden = st.integer(10) != 0;
      group.restricted = st.integer(11) != 0;
      group.closed = st.integer(12) != 0;
      out.push_back(std::move(group));
    }
  }
#else
  (void)query; (void)limit; (void)offset;
#endif
  return out;
}

std::vector<std::string> nostr_cached_group_json(std::string_view relay,
                                                  std::string_view group_id) {
  std::vector<std::string> out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, "SELECT e.raw_json FROM group_events ge JOIN events e"
       " ON e.id=ge.event_id WHERE ge.relay=? AND ge.group_id=?"
       " AND e.verification>0 ORDER BY e.created_at DESC LIMIT 150;");
  if (st.valid()) {
    st.bind(1, relay);
    st.bind(2, group_id);
    while (st.next()) out.push_back(st.text(0));
  }
#else
  (void)relay; (void)group_id;
#endif
  return out;
}

namespace {

#if XANATOO_SQLITE
std::vector<std::string> cached_raw(const char* sql, std::string_view key) {
  std::vector<std::string> out;
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, sql);
  if (st.valid()) {
    if (sqlite3_bind_parameter_count(st.p) > 0) st.bind(1, key);
    if (sqlite3_bind_parameter_count(st.p) > 1) st.bind(2, key);
    while (st.next()) out.push_back(st.text(0));
  }
  return out;
}
#endif

}  // namespace

std::vector<std::string> nostr_cached_thread_json(std::string_view id) {
#if XANATOO_SQLITE
  if (hex_pubkey(id)) return cached_raw(
      "SELECT raw_json FROM events WHERE verification>0"
      " AND (id=? OR id IN"
      " (SELECT event_id FROM event_refs WHERE referenced_id=?))"
      " ORDER BY created_at LIMIT 150;", id);
#else
  (void)id;
#endif
  return {};
}

std::vector<std::string> nostr_cached_author_json(std::string_view pk) {
#if XANATOO_SQLITE
  if (hex_pubkey(pk)) return cached_raw(
      "SELECT raw_json FROM events WHERE pubkey=? AND kind IN (0,1)"
      " AND verification>0"
      " ORDER BY created_at DESC LIMIT 150;", pk);
#else
  (void)pk;
#endif
  return {};
}

std::vector<std::string> nostr_cached_recent_json() {
#if XANATOO_SQLITE
  return cached_raw("SELECT raw_json FROM events WHERE kind=1 AND verification>0"
                    " ORDER BY created_at DESC LIMIT 80;", {});
#endif
  return {};
}

std::vector<NostrLocalIdentity> nostr_local_identities() {
  std::vector<NostrLocalIdentity> out;
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return out;
  DbOwner owner{db};
  Statement st(db, "SELECT pubkey,label FROM local_identities ORDER BY added, pubkey;");
  if (st.valid()) while (st.next()) out.push_back({st.text(0), st.text(1)});
#endif
  return out;
}

bool nostr_add_watch_identity(std::string_view label, std::string_view ref) {
#if XANATOO_SQLITE
  auto pk = nostr_decode_pubkey(ref);
  if (!pk && hex_pubkey(ref)) pk = ascii_lower(ref);
  if (!pk || no_save()) return false;
  sqlite3* db = open_cache(true);
  if (!db) return false;
  DbOwner owner{db};
  Statement st(db, "INSERT INTO local_identities(pubkey,label,added)"
                   " VALUES(?,?,?) ON CONFLICT(pubkey) DO UPDATE SET label=excluded.label;");
  bool ok = false;
  if (st.valid()) {
    st.bind(1, *pk);
    st.bind(2, label.empty() ? std::string_view("Watch-only") : label.substr(0, 128));
    st.bind(3, static_cast<int64_t>(std::time(nullptr)));
    ok = st.exec();
  }
  return ok;
#else
  (void)label; (void)ref;
  return false;
#endif
}

std::string nostr_active_local_identity() {
#if XANATOO_SQLITE
  sqlite3* db = open_cache(false);
  if (!db) return {};
  DbOwner owner{db};
  Statement st(db, "SELECT value FROM local_settings WHERE key='active-identity';");
  if (st.valid() && st.next()) return st.text(0);
#endif
  return {};
}

bool nostr_set_active_local_identity(std::string_view pubkey) {
#if XANATOO_SQLITE
  if (!hex_pubkey(pubkey) || no_save()) return false;
  sqlite3* db = open_cache(true);
  if (!db) return false;
  DbOwner owner{db};
  Statement st(db, "INSERT INTO local_settings(key,value)"
                   " SELECT 'active-identity',pubkey FROM local_identities"
                   " WHERE pubkey=? ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
  if (st.valid()) {
    st.bind(1, pubkey);
    return st.exec() && sqlite3_changes(db) > 0;
  }
#else
  (void)pubkey;
#endif
  return false;
}

}  // namespace xanatoo
