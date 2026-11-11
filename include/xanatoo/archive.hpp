// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Read-only Internet Archive pages (archive.org needs JavaScript, so its own
// pages cannot be shown). Public JSON APIs only; no account, no posting.
// See docs/ARCHIVE.md.
//
//   ia:ID                  item (book, video, audio, …): details, description,
//                          files to download; a collection lists its members
//   ia:ID?page=N           page N of a collection's members
//
// The pure parts (ref parsing, JSON → Markdown) are tested offline in
// tests/archive_tests.cpp; only ia_fetch touches the network.

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

struct IaRef {
  std::string id;  // item identifier
  int page = 1;    // member page of a collection
};

std::optional<IaRef> parse_ia_ref(std::string_view ref);
bool is_ia_ref(std::string_view ref);
std::string ia_ref_string(const IaRef& r);
// https://archive.org/details/ID
std::string ia_web_url(const IaRef& r);
// archive.org/details/ID (or /download/ID, /stream/ID, /embed/ID) → "ia:ID";
// "" for anything else (search, account and wayback pages stay web pages).
std::string ia_ref_from_web_url(std::string_view url);

constexpr int kIaPageRows = 50;

// Items listed per member page: the advancedsearch URL for a collection.
std::string ia_members_url(const IaRef& r);
std::string ia_metadata_url(const IaRef& r);

// "texts" / "movies" / "audio" / "collection" … of a /metadata reply; "" when
// the reply is empty (unknown or hidden item).
std::string ia_mediatype(std::string_view metadata_json);

// Markdown of a /metadata/ID reply. `members_json` is the advancedsearch
// reply for a collection's members (may be empty). Pure; an empty or
// malformed reply gives a short "not found" page.
std::string ia_markdown(std::string_view metadata_json,
                        std::string_view members_json, const IaRef& ref);

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
std::optional<Document> ia_fetch(std::string_view ref, std::string* err = nullptr);
#endif

}  // namespace xanatoo
