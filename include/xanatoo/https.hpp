// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Minimal HTTPS client (OpenSSL). Used by Reddit, Lemmy, and similar
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

}  // namespace xanatoo
