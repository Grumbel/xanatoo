// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/http_status.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace {
int failures = 0;
void check(bool ok, std::string_view what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %.*s\n", static_cast<int>(what.size()),
                 what.data());
    ++failures;
  }
}
} // namespace

int main() {
  using namespace xanatoo;
  check(http_status_line(404) == "HTTP 404 Not Found", "known status phrase");
  check(http_status_line(599) == "HTTP 599", "unassigned status keeps code");
  check(http_status_line(403) == "HTTP 403 Forbidden", "forbidden phrase");
  check(http_status_line(429) == "HTTP 429 Too Many Requests", "rate limit phrase");
  check(http_status_line(503) == "HTTP 503 Service Unavailable", "service unavailable phrase");
  check(http_status_error(403, "www.loc.gov").find(
      "HTTP 403 Forbidden from www.loc.gov — The server refused access.") == 0,
      "LoC search 403 includes host and explanation");
  check(http_status_error(429).find("Wait before trying again") != std::string::npos,
        "429 suggests retry later");
  check(http_status_error(500).find("unexpected problem") != std::string::npos,
        "500 explains server problem");
  check(http_status_error(401).find("authentication") != std::string::npos,
        "401 identifies authentication problem");
  check(http_status_error(404).find("not found") != std::string::npos,
        "404 identifies missing resource");
  check(http_status_error(599).find("The server could not complete") != std::string::npos,
        "unknown server code gets generic explanation");
  check(http_status_error(499).find("The server rejected") != std::string::npos,
        "unknown client code gets generic explanation");
  check(http_status_error(302).find("redirect was not completed") != std::string::npos,
        "unexpected redirect explained");
  check(http_status_error(0) == "No HTTP response was received.",
        "zero is not reported as a valid HTTP status");
  check(http_status_explanation(200).empty(), "successful responses need no error explanation");
  return failures ? 1 : 0;
}
