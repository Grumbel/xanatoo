// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Read-only, locally observed Lemmy catalogue. Each record remembers the
// instance on which its link works; federated people/communities include the
// actor's remote hostname in their handle when necessary.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

enum class LemmyEntryKind : uint8_t { Instance, Community, Person, Post };

struct LemmyDirectoryEntry {
  LemmyEntryKind kind = LemmyEntryKind::Instance;
  std::string instance;  // instance providing this API view
  std::string name;      // domain, community/user handle, or post id
  std::string title;     // human-facing name/title
  std::string ref;       // lemmy: native address
  int64_t score = 0;    // post score, community subscribers, or post count
};

// Accept hostnames or https://host[/...] URLs, never credentials, ports,
// private/single-label hosts, arbitrary schemes, or path data as a hostname.
// Returns the normalized ASCII hostname; empty for invalid input.
std::string lemmy_instance_host(std::string_view input);
std::string lemmy_query_escape(std::string_view input);

// No I/O: summarize a successful Lemmy API v3 response. Handles post and
// comment views, community and person views, search, and federated peers.
std::vector<LemmyDirectoryEntry> lemmy_discover_payload(
    std::string_view instance, std::string_view json);

// Thread-safe, bounded on-disk catalogue at state_dir()/lemmy-directory.txt.
// --no-save still keeps observations in memory for the current session.
std::vector<LemmyDirectoryEntry> lemmy_directory_list();
bool lemmy_directory_add_instance(std::string_view input);
void lemmy_directory_observe(std::string_view instance, std::string_view json);

}  // namespace xanatoo
