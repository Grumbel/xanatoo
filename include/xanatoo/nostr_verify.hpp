// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// NIP-01 event integrity. HashOnly is intentionally NOT an authenticated
// identity: a relay can invent any content/key pair and recompute its hash.
enum class NostrVerification { Invalid, HashOnly, SignatureValid };

// Reconstruct the NIP-01 canonical [0,pubkey,created_at,kind,tags,content]
// serialization and SHA-256. Returns nullopt for invalid event JSON.
std::optional<std::string> nostr_event_id(std::string_view event_json);
NostrVerification nostr_verify_event(std::string_view event_json);

// BIP-340 testable separately against the upstream official vectors.
// Returns false on builds without an approved secp256k1 crypto backend.
bool nostr_verify_bip340(std::string_view pubkey_hex,
                         std::string_view msg_hash_hex,
                         std::string_view signature_hex);

}  // namespace xanatoo
