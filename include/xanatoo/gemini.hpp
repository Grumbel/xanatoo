// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "xanatoo/gemini_tofu.hpp"
#include "xanatoo/tls.hpp"

namespace xanatoo {

// True for gemini:// URLs (scheme case-insensitive).
bool is_gemini_url(std::string_view url);

// Split gemini://host[:port]/path[?query]
struct GeminiUrl {
  std::string host;
  int port = 1965;
  std::string path_query;  // includes leading / ; empty → "/"
  std::string canonical;   // reconstructed request URL
};
std::optional<GeminiUrl> parse_gemini_url(std::string_view url);

// Resolve a link target against a base gemini:// URL (absolute, scheme-relative,
// or path-relative). Returns empty if unresolvable.
std::string resolve_gemini_url(std::string_view base, std::string_view ref);

// The server certificate of the last connection a fetch made (after
// redirects, the final host). `first_contact`: nothing was pinned for
// host:port, so this certificate was trusted and pinned now (TOFU).
struct GeminiPeer {
  std::string host;
  int port = 1965;
  std::string sha256;
  bool first_contact = false;
  std::optional<TlsPeerCertificate> details;
};

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
struct GeminiResponse {
  int status = 0;
  std::string meta;   // MIME or redirect URL or prompt
  std::string body;
  std::string final_url;  // after limited redirects
};

// Fetch over TLS. A first certificate per host:port is trusted and pinned
// (session-only with --no-save); a changed one is refused before any URL is
// sent and reported through `changed` (the user decides in the Certificates
// panel). Follows at most a few 30/31 redirects. On failure, nullopt and err.
std::optional<GeminiResponse> gemini_fetch(std::string_view url,
                                           std::string* err = nullptr,
                                           GeminiCertificateChange* changed = nullptr,
                                           GeminiPeer* peer = nullptr);
#endif

}  // namespace xanatoo
