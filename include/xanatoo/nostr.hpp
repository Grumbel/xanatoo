// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// Detect Nostr identifiers / URLs that xanatoo can open:
//   note1…  nevent1…  nostr:note1…  nostr:nevent1…
//   nostr:<64-hex>  (raw event id)
//   nostr:recent  (live kind-1 sample from public relays)
bool is_nostr_ref(std::string_view s);

// Decode NIP-19 note1/nevent1 (or 64-hex) to a 64-char lowercase hex event id.
// Optionally collect relay hints from nevent TLVs into *relays.
std::optional<std::string> nostr_decode_event_id(std::string_view ref,
                                                 std::vector<std::string>* relays = nullptr);

#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
// Fetch a root note and kind-1 replies from public relays (blocking, short timeout).
// Builds a Document with Thread + Message nodes for the existing layout path.
// On failure returns nullopt and sets *err.
std::optional<Document> nostr_fetch_thread(std::string_view ref,
                                           std::string* err = nullptr);
#endif

}  // namespace xanatoo
