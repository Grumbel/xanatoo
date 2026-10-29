// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/revstore.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#ifndef XANATOO_SQLITE
#define XANATOO_SQLITE 1
#endif
#if XANATOO_SQLITE
#include <sqlite3.h>
#endif

#include <ctime>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

namespace xanatoo {

std::string content_id(std::string_view bytes) {
  return "sha256:" + sha256_hex(bytes);
}

std::string new_doc_key() {
  std::random_device rd;
  static const char* hex = "0123456789abcdef";
  std::string key = "doc:";
  for (int i = 0; i < 8; ++i) {  // 8 × 32 bits
    const uint32_t v = rd();
    for (int s = 28; s >= 0; s -= 4) key.push_back(hex[(v >> s) & 0xf]);
  }
  return key;
}

std::string file_doc_key(std::string_view path) {
  std::error_code ec;
  fs::path p = fs::weakly_canonical(fs::absolute(fs::path(std::string(path)), ec), ec);
  if (ec) p = fs::path(std::string(path));
  return "file:" + p.lexically_normal().string();
}

namespace {

std::string g_db_path;  // empty = state_dir()/revisions.sqlite

#if XANATOO_SQLITE
sqlite3* g_db = nullptr;
bool g_db_failed = false;

sqlite3* db() {
  if (g_db) return g_db;
  if (g_db_failed) return nullptr;
  std::string path = g_db_path;
  if (path.empty()) {
    // --no-save: no store at all, so edits are confirmed, never dropped.
    if (no_save()) return nullptr;
    const std::string dir = state_dir();
    if (ensure_dir(dir)) path = dir + "/revisions.sqlite";
  }
  const char* schema =
      "CREATE TABLE IF NOT EXISTS objects ("
      "  id TEXT PRIMARY KEY, data BLOB NOT NULL);"
      "CREATE TABLE IF NOT EXISTS revisions ("
      "  id TEXT PRIMARY KEY, doc_key TEXT NOT NULL, parent TEXT,"
      "  content TEXT NOT NULL, kind TEXT NOT NULL, time INTEGER NOT NULL);"
      "CREATE INDEX IF NOT EXISTS revisions_doc ON revisions(doc_key, time);"
      "CREATE TABLE IF NOT EXISTS heads ("
      "  doc_key TEXT PRIMARY KEY, latest TEXT, draft TEXT, base TEXT,"
      "  title TEXT, time INTEGER);";
  if (path.empty() || sqlite3_open(path.c_str(), &g_db) != SQLITE_OK ||
      sqlite3_exec(g_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
    if (g_db) sqlite3_close(g_db);
    g_db = nullptr;
    g_db_failed = true;
    return nullptr;
  }
  return g_db;
}

// Prepared statement with text parameters bound in order; finalised on scope
// exit.
struct Stmt {
  sqlite3_stmt* st = nullptr;
  Stmt(sqlite3* d, const char* sql, std::initializer_list<std::string_view> args) {
    if (sqlite3_prepare_v2(d, sql, -1, &st, nullptr) != SQLITE_OK) {
      st = nullptr;
      return;
    }
    int i = 1;
    for (std::string_view a : args)
      sqlite3_bind_text(st, i++, a.data(), static_cast<int>(a.size()),
                        SQLITE_TRANSIENT);
  }
  ~Stmt() {
    if (st) sqlite3_finalize(st);
  }
  bool row() { return st && sqlite3_step(st) == SQLITE_ROW; }
  bool run() { return st && sqlite3_step(st) == SQLITE_DONE; }
  std::string text(int col) {
    const auto* t = sqlite3_column_text(st, col);
    return t ? std::string(reinterpret_cast<const char*>(t),
                           static_cast<size_t>(sqlite3_column_bytes(st, col)))
             : std::string();
  }
};

bool put_object(sqlite3* d, std::string_view id, std::string_view data) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(d, "INSERT OR IGNORE INTO objects(id, data) VALUES(?,?);",
                         -1, &st, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(st, 1, id.data(), static_cast<int>(id.size()),
                    SQLITE_TRANSIENT);
  sqlite3_bind_blob(st, 2, data.data(), static_cast<int>(data.size()),
                    SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

// Immutable revision record; its id is the content id of the canonical text.
std::string put_revision(sqlite3* d, std::string_view key,
                         std::string_view parent, std::string_view content,
                         std::string_view kind, int64_t time) {
  const std::string t = std::to_string(time);
  std::string rec = "xanatoo-revision 1\n";
  rec += "doc " + std::string(key) + "\n";
  rec += "parent " + std::string(parent) + "\n";
  rec += "content " + std::string(content) + "\n";
  rec += "kind " + std::string(kind) + "\n";
  rec += "time " + t + "\n";
  const std::string id = content_id(rec);
  Stmt s(d,
         "INSERT OR IGNORE INTO revisions(id, doc_key, parent, content, kind, "
         "time) VALUES(?,?,NULLIF(?,''),?,?,CAST(? AS INTEGER));",
         {id, key, parent, content, kind, t});
  return s.run() ? id : std::string();
}

struct HeadRow {
  bool found = false;
  std::string latest, draft, base;
};

HeadRow head_row(sqlite3* d, std::string_view key) {
  HeadRow h;
  Stmt s(d, "SELECT latest, draft, base FROM heads WHERE doc_key = ?;", {key});
  if (s.row()) {
    h.found = true;
    h.latest = s.text(0);
    h.draft = s.text(1);
    h.base = s.text(2);
  }
  return h;
}

bool set_head(sqlite3* d, std::string_view key, std::string_view latest,
              std::string_view draft, std::string_view base,
              std::string_view title, int64_t time) {
  Stmt s(d,
         "INSERT INTO heads(doc_key, latest, draft, base, title, time) "
         "VALUES(?,?,NULLIF(?,''),NULLIF(?,''),?,CAST(? AS INTEGER)) "
         "ON CONFLICT(doc_key) DO UPDATE SET latest = excluded.latest, "
         "draft = excluded.draft, base = excluded.base, "
         "title = excluded.title, time = excluded.time;",
         {key, latest, draft, base, title, std::to_string(time)});
  return s.run();
}

// BEGIN … COMMIT, rolled back unless commit() is reached.
struct Txn {
  sqlite3* d;
  bool done = false;
  explicit Txn(sqlite3* db_) : d(db_) {
    sqlite3_exec(d, "BEGIN;", nullptr, nullptr, nullptr);
  }
  bool commit() {
    done = sqlite3_exec(d, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    return done;
  }
  ~Txn() {
    if (!done) sqlite3_exec(d, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
};
#endif

}  // namespace

void revstore_use_path(std::string path) {
  g_db_path = std::move(path);
#if XANATOO_SQLITE
  if (g_db) sqlite3_close(g_db);
  g_db = nullptr;
  g_db_failed = false;
#endif
}

#if XANATOO_SQLITE

static const char* kDraftSelect =
    "SELECT h.doc_key, h.draft, r.content, h.base, h.title, h.time "
    "FROM heads h JOIN revisions r ON r.id = h.draft ";

static DraftHead draft_from(Stmt& s) {
  DraftHead h;
  h.key = s.text(0);
  h.revision = s.text(1);
  h.content = s.text(2);
  h.base = s.text(3);
  h.title = s.text(4);
  h.time = sqlite3_column_int64(s.st, 5);
  return h;
}

std::optional<DraftHead> find_draft(std::string_view key) {
  sqlite3* d = db();
  if (!d) return std::nullopt;
  Stmt s(d, (std::string(kDraftSelect) + "WHERE h.doc_key = ?;").c_str(),
         {key});
  if (!s.row()) return std::nullopt;
  return draft_from(s);
}

std::vector<DraftHead> list_drafts() {
  std::vector<DraftHead> out;
  sqlite3* d = db();
  if (!d) return out;
  Stmt s(d, (std::string(kDraftSelect) + "ORDER BY h.time DESC;").c_str(), {});
  while (s.row()) out.push_back(draft_from(s));
  return out;
}

std::optional<std::string> load_object(std::string_view id) {
  sqlite3* d = db();
  if (!d) return std::nullopt;
  Stmt s(d, "SELECT data FROM objects WHERE id = ?;", {id});
  if (!s.row()) return std::nullopt;
  const void* p = sqlite3_column_blob(s.st, 0);
  const int n = sqlite3_column_bytes(s.st, 0);
  return std::string(static_cast<const char*>(p), p ? static_cast<size_t>(n) : 0);
}

std::string put_draft(std::string_view key, std::string_view text,
                      std::string_view base_text, std::string_view title) {
  sqlite3* d = db();
  if (!d || key.empty()) return {};
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  const std::string cid = content_id(text);
  Txn txn(d);
  const HeadRow h = head_row(d, key);
  std::string parent = h.latest;
  std::string base = h.base;
  if (!base_text.empty()) {
    const std::string bid = content_id(base_text);
    if (bid != h.base) {  // first edit of this file version
      if (!put_object(d, bid, base_text)) return {};
      parent = put_revision(d, key, parent, bid, "file", now);
      if (parent.empty()) return {};
      base = bid;
    }
  }
  if (!put_object(d, cid, text)) return {};
  const std::string rev = put_revision(d, key, parent, cid, "draft", now);
  if (rev.empty() || !set_head(d, key, rev, rev, base, title, now)) return {};
  return txn.commit() ? cid : std::string();
}

bool record_save(std::string_view key, std::string_view text) {
  sqlite3* d = db();
  if (!d || key.empty()) return false;
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  const std::string cid = content_id(text);
  Txn txn(d);
  const HeadRow h = head_row(d, key);
  if (!put_object(d, cid, text)) return false;
  const std::string rev = put_revision(d, key, h.latest, cid, "save", now);
  if (rev.empty() || !set_head(d, key, rev, "", cid, "", now)) return false;
  return txn.commit();
}

bool drop_draft(std::string_view key) {
  sqlite3* d = db();
  if (!d) return false;
  Stmt s(d, "UPDATE heads SET draft = NULL WHERE doc_key = ?;", {key});
  return s.run();
}

#else

std::optional<DraftHead> find_draft(std::string_view) { return std::nullopt; }
std::vector<DraftHead> list_drafts() { return {}; }
std::optional<std::string> load_object(std::string_view) { return std::nullopt; }
std::string put_draft(std::string_view, std::string_view, std::string_view,
                      std::string_view) {
  return {};
}
bool record_save(std::string_view, std::string_view) { return false; }
bool drop_draft(std::string_view) { return false; }

#endif

}  // namespace xanatoo
