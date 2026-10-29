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
//   npub1…  nprofile1…  nostr:npub1…  (an author's recent notes)
//   nostr:recent  (live kind-1 sample from public relays)
bool is_nostr_ref(std::string_view s);

// Decode NIP-19 note1/nevent1 (or 64-hex) to a 64-char lowercase hex event id.
// Optionally collect relay hints from nevent TLVs into *relays.
std::optional<std::string> nostr_decode_event_id(std::string_view ref,
                                                 std::vector<std::string>* relays = nullptr);

// Decode NIP-19 npub1/nprofile1 to a 64-char lowercase hex pubkey.
// Optionally collect relay hints from nprofile TLVs into *relays.
std::optional<std::string> nostr_decode_pubkey(std::string_view ref,
                                               std::vector<std::string>* relays = nullptr);

// Bech32-encode a 64-hex id with the given prefix ("npub", "note");
// empty if hex is not 64 hex digits.
std::string nostr_bech32(std::string_view hrp, std::string_view hex);

#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
// Fetch a root note and kind-1 replies from public relays (blocking, short
// timeout), or for npub/nprofile refs the author's profile and recent notes.
// Builds a Document with Thread + Message nodes for the existing layout path;
// author names link to nostr:npub1…, dates to nostr:note1….
// On failure returns nullopt and sets *err.
std::optional<Document> nostr_fetch_thread(std::string_view ref,
                                           std::string* err = nullptr);
#endif

}  // namespace xanatoo
