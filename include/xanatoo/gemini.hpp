// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>

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

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
struct GeminiResponse {
  int status = 0;
  std::string meta;   // MIME or redirect URL or prompt
  std::string body;
  std::string final_url;  // after limited redirects
};

// Fetch over TLS (TOFU-style: accept cert without pinning for now).
// Follows at most a few 30/31 redirects. On failure, nullopt and err set.
std::optional<GeminiResponse> gemini_fetch(std::string_view url,
                                           std::string* err = nullptr);
#endif

}  // namespace xanatoo
