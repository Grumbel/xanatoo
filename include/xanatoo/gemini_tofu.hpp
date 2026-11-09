// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>
#include <string_view>

namespace xanatoo {

// Trust a Gemini server's first certificate and reject later changes.
// The pin is keyed by (case-insensitive host, port), not by URL path.
// `directory` is the state directory; when `persist` is false, existing pins
// are still enforced but new pins are session-only. This function is safe
// for concurrent calls within the process and has no SDL/TLS dependency.
// A failed store read/write or a changed certificate fails closed.
bool gemini_tofu_check(std::string_view directory, bool persist,
                       std::string_view host, int port,
                       std::string_view fingerprint_sha256,
                       std::string* err = nullptr);

}  // namespace xanatoo
