// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

#ifndef XANATOO_SQLITE
#define XANATOO_SQLITE 1
#endif
#if XANATOO_SQLITE
#include <sqlite3.h>
#endif

#include <cstdio>
#include <filesystem>

namespace xanatoo {
namespace {

#if XANATOO_SQLITE
sqlite3* open_db() {
  std::string path = sidestore_path();
  if (path.empty()) return nullptr;
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return nullptr;
  }
  char* err = nullptr;
  const char* schema =
      "CREATE TABLE IF NOT EXISTS section_fold ("
      "  doc_key TEXT NOT NULL,"
      "  section_key TEXT NOT NULL,"
      "  folded INTEGER NOT NULL DEFAULT 1,"
      "  PRIMARY KEY (doc_key, section_key)"
      ");";
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
    if (err) sqlite3_free(err);
    sqlite3_close(db);
    return nullptr;
  }
  return db;
}
#endif

std::string child_index_path(const Document& doc, NodeId section) {
  std::vector<int> indices;
  NodeId cur = section;
  while (cur != INVALID_NODE) {
    const Node& n = doc.get(cur);
    if (n.parent == INVALID_NODE) break;
    const Node& p = doc.get(n.parent);
    int idx = -1;
    for (size_t i = 0; i < p.children.size(); ++i) {
      if (p.children[i] == cur) {
        idx = static_cast<int>(i);
        break;
      }
    }
    if (idx < 0) break;
    indices.push_back(idx);
    cur = n.parent;
    if (n.parent == doc.root) break;
  }
  std::string out = "path:";
  for (size_t i = indices.size(); i > 0; --i) {
    if (out.size() > 5) out.push_back('.');
    out += std::to_string(indices[i - 1]);
  }
  return out;
}

}  // namespace

std::string sidestore_path() {
  std::string dir = config_dir();
  if (dir.empty()) return {};
  return dir + "/sidestore.sqlite";
}

std::string section_stable_key(const Document& doc, NodeId section) {
  if (section == INVALID_NODE) return {};
  const Node& n = doc.get(section);
  if (!n.label.empty()) return std::string("label:") + n.label;
  // Prefer first header label or path
  for (NodeId c : n.children) {
    const Node& ch = doc.get(c);
    if (ch.kind == NodeKind::Header || ch.kind == NodeKind::Heading) {
      if (!ch.label.empty()) return std::string("label:") + ch.label;
      break;
    }
  }
  return child_index_path(doc, section);
}

#if XANATOO_SQLITE
void load_section_folds(std::string_view doc_key,
                        std::unordered_set<std::string>& folded_keys) {
  folded_keys.clear();
  if (doc_key.empty()) return;
  sqlite3* db = open_db();
  if (!db) return;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT section_key FROM section_fold WHERE doc_key = ? AND folded = 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  sqlite3_bind_text(st, 1, doc_key.data(), static_cast<int>(doc_key.size()),
                    SQLITE_TRANSIENT);
  while (sqlite3_step(st) == SQLITE_ROW) {
    const char* k =
        reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    if (k) folded_keys.insert(k);
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
}

void save_section_fold(std::string_view doc_key, std::string_view section_key,
                       bool folded) {
  if (doc_key.empty() || section_key.empty()) return;
  sqlite3* db = open_db();
  if (!db) return;
  if (folded) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO section_fold(doc_key, section_key, folded) VALUES(?,?,1) "
        "ON CONFLICT(doc_key, section_key) DO UPDATE SET folded = 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, doc_key.data(), static_cast<int>(doc_key.size()),
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, section_key.data(),
                        static_cast<int>(section_key.size()), SQLITE_TRANSIENT);
      sqlite3_step(st);
      sqlite3_finalize(st);
    }
  } else {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "DELETE FROM section_fold WHERE doc_key = ? AND section_key = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, doc_key.data(), static_cast<int>(doc_key.size()),
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, section_key.data(),
                        static_cast<int>(section_key.size()), SQLITE_TRANSIENT);
      sqlite3_step(st);
      sqlite3_finalize(st);
    }
  }
  sqlite3_close(db);
}

#else
// Built without SQLite (e.g. Android first port): folds are not persisted.
void load_section_folds(std::string_view, std::unordered_set<std::string>& folded_keys) {
  folded_keys.clear();
}
void save_section_fold(std::string_view, std::string_view, bool) {}
#endif

void folds_keys_to_ids(const Document& doc,
                       const std::unordered_set<std::string>& keys,
                       std::unordered_set<NodeId>& out_ids) {
  out_ids.clear();
  if (keys.empty()) return;
  for (const Node& n : doc.nodes) {
    if (n.kind != NodeKind::Section && n.kind != NodeKind::Article) continue;
    std::string k = section_stable_key(doc, n.id);
    if (keys.count(k)) out_ids.insert(n.id);
  }
}

void folds_ids_to_keys(const Document& doc,
                       const std::unordered_set<NodeId>& ids,
                       std::unordered_set<std::string>& out_keys) {
  out_keys.clear();
  for (NodeId id : ids) {
    std::string k = section_stable_key(doc, id);
    if (!k.empty()) out_keys.insert(std::move(k));
  }
}

}  // namespace xanatoo
