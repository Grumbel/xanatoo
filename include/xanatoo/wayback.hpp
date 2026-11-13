// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Old versions of web pages from the Internet Archive's Wayback Machine.
// See docs/ARCHIVE.md.
//
//   wayback:https://example.com/page             nearest snapshot
//   wayback:20150301/https://example.com/page    nearest to that date
//                                                (YYYY, YYYYMM … YYYYMMDDhhmmss)
//
// The ref fetches https://web.archive.org/web/<ts>id_/<url> (id_ = the page as
// archived, without the Wayback toolbar) through the ordinary web page reader.
// Everything here is pure; the fetch is `web_fetch` on `wayback_snapshot_url`.

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

struct WaybackRef {
  std::string timestamp;  // "" = nearest to now, else 4–14 digits
  std::string url;        // original http(s) URL
};

std::optional<WaybackRef> parse_wayback_ref(std::string_view ref);
bool is_wayback_ref(std::string_view ref);
std::string wayback_ref_string(const WaybackRef& r);
// https://web.archive.org/web/<ts>id_/<url>
std::string wayback_snapshot_url(const WaybackRef& r);
// A web.archive.org snapshot URL (any modifier: id_, if_, im_, none) → its
// ref with the snapshot's timestamp; nullopt for anything else.
std::optional<WaybackRef> wayback_ref_from_snapshot_url(std::string_view url);
// The ref to look up for a page address: plain http(s) pages only (not
// archive.org itself, not local or native refs); "" otherwise.
std::string wayback_lookup_ref(std::string_view page_url);
// "2015-03-01 12:00:00" style label of a snapshot timestamp ("" if invalid).
std::string wayback_timestamp_label(std::string_view timestamp);

}  // namespace xanatoo
