// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/https.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/util.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace xanatoo {
namespace {

int tcp_connect(const std::string& host, int port, std::string* err) {
  addrinfo hints {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  std::string port_s = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) {
    if (err) *err = "DNS failed for " + host;
    return -1;
  }
  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0 && err) *err = "connect failed to " + host;
  return fd;
}



}  // namespace

int tcp_connect_host(const std::string& host, int port, std::string* err) {
  return tcp_connect(host, port, err);
}

std::optional<HttpResult> https_request(std::string_view method,
                                        std::string_view url,
                                        std::string_view user_agent,
                                        std::string_view extra_headers,
                                        std::string_view body_in,
                                        std::string* err) {
  if (!starts_with_ci(url, "https://")) {
    if (err) *err = "only https supported";
    return std::nullopt;
  }
  std::string_view rest = url.substr(8);
  size_t slash = rest.find('/');
  std::string host = std::string(
      slash == std::string_view::npos ? rest : rest.substr(0, slash));
  std::string path = std::string(
      slash == std::string_view::npos ? "/" : rest.substr(slash));
  int port = 443;
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) {
    port = std::atoi(host.c_str() + colon + 1);
    if (port <= 0) port = 443;
    host = host.substr(0, colon);
  }

  int fd = tcp_connect(host, port, err);
  if (fd < 0) return std::nullopt;
  TlsStream tls;
  if (!tls.connect(fd, host, err)) return std::nullopt;

  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "User-Agent: " << user_agent << "\r\n"
      << "Accept: application/json, text/plain, */*\r\n"
      << "Accept-Language: en-US,en;q=0.9\r\n"
      << "Connection: close\r\n";
  if (!extra_headers.empty()) req << extra_headers;
  if (!body_in.empty())
    req << "Content-Length: " << body_in.size() << "\r\n";
  req << "\r\n";
  if (!body_in.empty()) req << body_in;
  std::string req_s = req.str();
  if (!tls.write_all(req_s.data(), req_s.size())) {
    if (err) *err = "TLS write failed";
    return std::nullopt;
  }

  std::string data;
  char buf[4096];
  for (;;) {
    int n = tls.read(buf, sizeof(buf));
    if (n <= 0) break;
    data.append(buf, static_cast<size_t>(n));
    if (data.size() > 12 * 1024 * 1024) break;
  }
  tls.close();

  size_t hdr_end = data.find("\r\n\r\n");
  if (hdr_end == std::string::npos) {
    if (err) *err = "malformed HTTP response";
    return std::nullopt;
  }
  std::string headers = data.substr(0, hdr_end);
  HttpResult out;
  out.body = data.substr(hdr_end + 4);

  {
    size_t sp1 = headers.find(' ');
    if (sp1 != std::string::npos) {
      size_t sp2 = headers.find(' ', sp1 + 1);
      std::string code = (sp2 == std::string::npos)
                             ? headers.substr(sp1 + 1, 3)
                             : headers.substr(sp1 + 1, sp2 - sp1 - 1);
      out.status = std::atoi(code.c_str());
    }
  }

  std::string hlow = ascii_lower(headers);
  auto header_val = [&](const char* name) -> std::string {
    std::string key = std::string("\n") + name + ":";
    size_t loc = hlow.find(key);
    if (loc == std::string::npos) return {};
    size_t v0 = headers.find(':', loc);
    size_t v1 = headers.find("\r\n", v0);
    if (v0 == std::string::npos || v1 == std::string::npos) return {};
    std::string val = headers.substr(v0 + 1, v1 - v0 - 1);
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
      val.erase(val.begin());
    return val;
  };
  out.location = header_val("location");
  out.content_type = header_val("content-type");
  return out;
}

std::optional<HttpResult> https_get(std::string_view url,
                                    std::string_view user_agent,
                                    std::string* err) {
  return https_request("GET", url, user_agent, {}, {}, err);
}

}  // namespace xanatoo
