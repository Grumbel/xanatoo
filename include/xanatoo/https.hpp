// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstddef>
#include <optional>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>

namespace xanatoo {

// Minimal HTTP/1.1 client. http:// uses plain TCP (port 80); https:// uses
// TlsStream (OpenSSL or Mbed TLS, port 443). The historical `https_*` names
// accept either scheme so existing service clients share one implementation.

struct HttpResult {
  int status = 0;
  std::string body;
  std::string location;
  std::string content_type;
};

// Applied at connection time (before DNS-address selection / TLS handshake).
// HTTPS always verifies its certificate by default; http:// remains available
// as explicitly plaintext browsing. NIP-11 additionally requires HTTPS,
// public-only resolved addresses, and a small response bound.
struct HttpRequestSecurity {
  bool verify_tls = true;     // require trusted CA and hostname for HTTPS
  bool public_only = false;   // reject private/local resolved IPs
  bool require_https = false; // reject http:// before opening a socket
  size_t max_response_bytes = 12 * 1024 * 1024;
};

// Streaming GET for large responses. Headers are delivered before any body
// bytes; on_body receives decoded transfer chunks, without buffering the
// entire file. Returning false from either callback aborts the transfer.
struct HttpStreamHeaders {
  int status = 0;
  std::string location;
  std::string content_type;
  std::string content_disposition;
  int64_t content_length = -1; // unknown, e.g. chunked
};
using HttpHeaderCallback = std::function<bool(const HttpStreamHeaders&)>;
using HttpBodyCallback = std::function<bool(std::string_view)>;

bool https_get_follow_stream(std::string_view url, std::string_view user_agent,
                             const HttpHeaderCallback& on_headers,
                             const HttpBodyCallback& on_body,
                             std::string* err = nullptr,
                             std::string* final_url = nullptr);

// HTTP/1.1 request over TCP or TLS. `extra_headers` are full header lines ending
// with "\r\n" (e.g. "Authorization: Bearer …\r\n"). `body` is optional
// request body (sets Content-Length when non-empty).
std::optional<HttpResult> https_request(std::string_view method,
                                        std::string_view url,
                                        std::string_view user_agent,
                                        std::string_view extra_headers,
                                        std::string_view body,
                                        std::string* err = nullptr,
                                        HttpRequestSecurity security = {});

std::optional<HttpResult> https_get(std::string_view url,
                                    std::string_view user_agent,
                                    std::string* err = nullptr);

// GET following up to five redirects (including relative and cross-scheme
// Location headers). `final_url` receives the last URL requested.
std::optional<HttpResult> https_get_follow(std::string_view url,
                                           std::string_view user_agent,
                                           std::string* err = nullptr,
                                           std::string* final_url = nullptr);

// Shared TCP helper (Gemini, Nostr WSS); wrap the fd in a TlsStream.
// Returns a connected socket fd, or -1. Caller owns the fd.
int tcp_connect_host(const std::string& host, int port,
                     std::string* err = nullptr);

}  // namespace xanatoo
