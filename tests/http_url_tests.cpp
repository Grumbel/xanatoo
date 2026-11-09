// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/http_url.hpp"

#include <cstdio>
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
}

int main() {
  using xanatoo::parse_http_url;
  using xanatoo::resolve_http_redirect;
  using xanatoo::http_redirect_keeps_security;
  auto plain = parse_http_url("http://localhost:8080/a/b?x=1#ignored");
  check(plain.has_value(), "http URL accepted");
  if (plain) {
    check(!plain->secure && plain->port == 8080, "HTTP uses plain TCP");
    check(plain->host == "localhost", "HTTP hostname");
    check(plain->authority == "localhost:8080", "Host preserves port");
    check(plain->target == "/a/b?x=1", "fragment stripped");
  }
  auto secure = parse_http_url("https://example.org/?q=1");
  check(secure && secure->secure && secure->port == 443, "HTTPS uses TLS");
  auto root = parse_http_url("http://example.org?q=1");
  check(root && root->port == 80 && root->target == "/?q=1",
        "default port and query-only target");
  auto ipv6 = parse_http_url("http://[::1]:8443/file");
  check(ipv6 && ipv6->host == "::1" && ipv6->authority == "[::1]:8443" &&
        ipv6->port == 8443, "IPv6 with explicit port");
  for (const auto bad : {"ftp://example.org/", "http://", "http://x:0/",
                          "http://x:65536/", "http://x:/", "http://x:a/",
                          "http://user:pass@x/", "http://x/\r\nInjected: yes"})
    check(!parse_http_url(bad), bad);
  auto location = [](std::string_view base, std::string_view next) {
    return resolve_http_redirect(base, next).value_or("<invalid>");
  };
  check(location("http://a.example/one/two", "/three") ==
        "http://a.example/three", "HTTP root relative redirect");
  check(location("http://a.example/one/two", "../three?x=1") ==
        "http://a.example/three?x=1", "HTTP parent relative redirect");
  check(location("http://a.example/one/two?x=1", "?y=2") ==
        "http://a.example/one/two?y=2", "HTTP query redirect");
  check(location("http://a.example/", "//b.example/hi") ==
        "http://b.example/hi", "scheme-relative keeps HTTP");
  check(location("https://a.example/", "//b.example/hi") ==
        "https://b.example/hi", "scheme-relative keeps HTTPS");
  check(location("https://a.example/", "http://b.example/hi") ==
        "http://b.example/hi", "HTTPS to HTTP preserves destination scheme");
  check(location("http://a.example/", "https://b.example/hi") ==
        "https://b.example/hi", "HTTP to HTTPS upgrade via Location");
  check(http_redirect_keeps_security("http://a.example", "http://b.example"),
        "explicit HTTP browsing and redirects remain available");
  check(http_redirect_keeps_security("http://a.example", "https://b.example"),
        "HTTP to HTTPS redirect allowed");
  check(http_redirect_keeps_security("https://a.example", "https://b.example"),
        "HTTPS to HTTPS redirect allowed");
  check(!http_redirect_keeps_security("https://a.example", "http://b.example"),
        "HTTPS to HTTP downgrade must be rejected");
  check(!resolve_http_redirect("http://a.example", "ftp://other/a"),
        "reject unsupported redirect scheme");
  return failures ? 1 : 0;
}
