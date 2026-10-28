// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// Local revision store (~/.config/xanatoo/revisions.sqlite), first step
// towards immutable, content-addressed revisions (docs/REVISIONS.md):
//
//   objects    content id ("sha256:<hex>") → bytes, never changed
//   revisions  immutable records: doc key, parent revision, content id,
//              kind ("file" | "draft" | "save"), time; the revision id is
//              the content id of the record's canonical text
//   heads      per doc key: latest revision, current draft (unsaved edits)
//              and the content id of the saved file the draft started from
//
// A doc key names a logical document: "file:<absolute path>" for local
// files, "doc:<64 hex>" (random 256 bits) for documents without a path.
// Without SQLite every call fails (returns false / nothing).

// "sha256:<hex>" of `bytes`.
std::string content_id(std::string_view bytes);

// Fresh random "doc:<64 hex>" key.
std::string new_doc_key();

// "file:<canonical absolute path>".
std::string file_doc_key(std::string_view path);

// Use `path` instead of the default database (tests). Closes an open one.
void revstore_use_path(std::string path);

struct DraftHead {
  std::string key;
  std::string revision;  // id of the draft revision
  std::string content;   // content id of the draft text
  std::string base;      // content id of the saved text it started from
  std::string title;     // short label for listings
  int64_t time = 0;      // unix seconds of the last draft write
};

// Unsaved edits of `key`, if any.
std::optional<DraftHead> find_draft(std::string_view key);
std::vector<DraftHead> list_drafts();  // newest first

std::optional<std::string> load_object(std::string_view id);

// Record `text` as the draft of `key`. `base_text` is the saved text the
// edits started from (stored once as a "file" revision so the chain begins
// at what was on disk; empty for documents that never had a file).
// Returns the content id of `text`, or empty when the store failed.
std::string put_draft(std::string_view key, std::string_view text,
                      std::string_view base_text, std::string_view title);

// `text` was written to the file of `key`: a "save" revision, no draft.
bool record_save(std::string_view key, std::string_view text);

// Forget the draft of `key` (its revisions stay in the store).
bool drop_draft(std::string_view key);

}  // namespace xanatoo
