// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Minimal HTTPS client (TlsStream: OpenSSL or Mbed TLS). Used by Reddit, Lemmy, and similar
// read-only network document sources.

struct HttpResult {
  int status = 0;
  std::string body;
  std::string location;
  std::string content_type;
};

// HTTP/1.1 request over TLS. `extra_headers` are full header lines ending
// with "\r\n" (e.g. "Authorization: Bearer …\r\n"). `body` is optional
// request body (sets Content-Length when non-empty).
std::optional<HttpResult> https_request(std::string_view method,
                                        std::string_view url,
                                        std::string_view user_agent,
                                        std::string_view extra_headers,
                                        std::string_view body,
                                        std::string* err = nullptr);

std::optional<HttpResult> https_get(std::string_view url,
                                    std::string_view user_agent,
                                    std::string* err = nullptr);

// GET following up to five redirects (relative Location resolved, http
// upgraded to https). `final_url` receives the last URL requested.
std::optional<HttpResult> https_get_follow(std::string_view url,
                                           std::string_view user_agent,
                                           std::string* err = nullptr,
                                           std::string* final_url = nullptr);

// Shared TCP helper (Gemini, Nostr WSS); wrap the fd in a TlsStream.
// Returns a connected socket fd, or -1. Caller owns the fd.
int tcp_connect_host(const std::string& host, int port,
                     std::string* err = nullptr);

}  // namespace xanatoo
