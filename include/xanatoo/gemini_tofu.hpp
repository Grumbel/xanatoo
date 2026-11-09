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

// Trust a Gemini server's first certificate and reject later changes.
// The pin is keyed by (case-insensitive host, port), not by URL path.
// `directory` is the state directory; when `persist` is false, existing pins
// are still enforced but new pins are session-only. This function is safe
// for concurrent calls within the process and has no SDL/TLS dependency.
// A failed store read/write or a changed certificate fails closed.
bool gemini_tofu_check(std::string_view directory, bool persist,
                       std::string_view host, int port,
                       std::string_view fingerprint_sha256,
                       std::string* err = nullptr,
                       GeminiCertificateChange* changed = nullptr);

// Explicitly approve a previously observed change. The old pin must still
// match the observation, or the replacement fails closed. With --no-save the
// approval lasts for this process only, including when an older saved pin
// exists; subsequent disk changes invalidate that session approval.
bool gemini_tofu_replace(std::string_view directory, bool persist,
                         const GeminiCertificateChange& change,
                         std::string* err = nullptr);

}  // namespace xanatoo
