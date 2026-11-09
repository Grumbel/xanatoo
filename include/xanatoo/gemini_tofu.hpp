// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>
#include <string_view>

namespace xanatoo {

// A certificate replacement observed during a Gemini TLS handshake. Both
// fingerprints are full, lowercase SHA-256 hashes of the DER leaf certificate.
// This record is only produced for a pin mismatch, not for TLS/store errors.
struct GeminiCertificateChange {
  std::string host;
  int port = 1965;
  std::string previous_sha256;
  std::string received_sha256;
};

// The first certificate seen for a host:port. When the caller requests a
// challenge, no URL is sent and no pin is saved until explicitly approved.
struct GeminiCertificateFirstUse {
  std::string host;
  int port = 1965;
  std::string fingerprint_sha256;
};

// Trust a Gemini server's first certificate and reject later changes. When
// `first_use` is non-null, unknown certificates instead produce a pending
// first-use challenge and return false without saving or sending the URL.
// The pin is keyed by (case-insensitive host, port), not by URL path.
// `directory` is the state directory; when `persist` is false, existing pins
// are still enforced but new pins are session-only. This function is safe
// for concurrent calls within the process and has no SDL/TLS dependency.
// A failed store read/write or a changed certificate fails closed.
bool gemini_tofu_check(std::string_view directory, bool persist,
                       std::string_view host, int port,
                       std::string_view fingerprint_sha256,
                       std::string* err = nullptr,
                       GeminiCertificateChange* changed = nullptr,
                       GeminiCertificateFirstUse* first_use = nullptr);

// Confirm a first-use challenge. No existing different pin may be replaced;
// another fetch may have already confirmed this exact fingerprint, in which
// case confirmation is idempotent. `persist=false` never touches the store.
bool gemini_tofu_approve_first_use(std::string_view directory, bool persist,
                                  const GeminiCertificateFirstUse& first_use,
                                  std::string* err = nullptr);

// Explicitly approve a previously observed change. The old pin must still
// match the observation, or the replacement fails closed. With --no-save the
// approval lasts for this process only, including when an older saved pin
// exists; subsequent disk changes invalidate that session approval.
bool gemini_tofu_replace(std::string_view directory, bool persist,
                         const GeminiCertificateChange& change,
                         std::string* err = nullptr);

}  // namespace xanatoo
