// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/https.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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

namespace {

// Decode a "Transfer-Encoding: chunked" body (hex size line, data, CRLF).
std::string dechunk(const std::string& in) {
  std::string out;
  size_t p = 0;
  while (p < in.size()) {
    size_t eol = in.find("\r\n", p);
    if (eol == std::string::npos) break;
    size_t n = std::strtoul(in.c_str() + p, nullptr, 16);
    if (n == 0) break;
    p = eol + 2;
    if (p + n > in.size()) n = in.size() - p;
    out.append(in, p, n);
    p += n + 2;
  }
  return out;
}

}  // namespace

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
  if (ascii_lower(header_val("transfer-encoding")).find("chunked") !=
      std::string::npos)
    out.body = dechunk(out.body);
  return out;
}

std::optional<HttpResult> https_get(std::string_view url,
                                    std::string_view user_agent,
                                    std::string* err) {
  return https_request("GET", url, user_agent, {}, {}, err);
}

std::optional<HttpResult> https_get_follow(std::string_view url,
                                           std::string_view user_agent,
                                           std::string* err,
                                           std::string* final_url) {
  std::string current(url);
  std::optional<HttpResult> resp;
  for (int hop = 0; hop < 5; ++hop) {
    resp = https_get(current, user_agent, err);
    if (!resp || resp->status < 300 || resp->status >= 400 ||
        resp->location.empty())
      break;
    std::string loc = resp->location;
    if (!starts_with_ci(loc, "https://") && !starts_with_ci(loc, "http://")) {
      // Relative redirect: resolve against scheme://host of `current`.
      size_t host_end = current.find('/', current.find("://") + 3);
      std::string origin = current.substr(0, host_end);
      if (loc.empty() || loc[0] != '/') loc.insert(loc.begin(), '/');
      loc = origin + loc;
    }
    if (starts_with_ci(loc, "http://")) loc = "https://" + loc.substr(7);
    current = std::move(loc);
  }
  if (final_url) *final_url = current;
  return resp;
}

namespace {

std::string header_value(const std::string& headers, const char* key) {
  const std::string lower = ascii_lower(headers);
  const std::string needle = std::string("\n") + key + ":";
  size_t at = lower.find(needle);
  if (at == std::string::npos) return {};
  at = headers.find(':', at) + 1;
  const size_t end = headers.find("\r\n", at);
  if (end == std::string::npos) return {};
  std::string_view value(headers.data() + at, end - at);
  trim_ascii(value);
  return std::string(value);
}

struct ChunkDecoder {
  enum class State { Size, Data, CrLf, Done } state = State::Size;
  std::string pending;
  uint64_t left = 0;

  bool feed(std::string_view bytes, const HttpBodyCallback& sink,
            std::string* err) {
    pending.append(bytes);
    while (true) {
      if (state == State::Done) return true;
      if (state == State::Size) {
        const size_t end = pending.find("\r\n");
        if (end == std::string::npos) {
          if (pending.size() > 128) {
            if (err) *err = "oversized chunk header";
            return false;
          }
          return true;
        }
        std::string_view hex(pending.data(), end);
        hex = hex.substr(0, hex.find(';'));
        size_t n = 0;
        auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), n,
                                         16);
        if (ec != std::errc{} || ptr != hex.data() + hex.size()) {
          if (err) *err = "invalid HTTP chunk size";
          return false;
        }
        pending.erase(0, end + 2);
        left = n;
        state = left ? State::Data : State::Done;
      } else if (state == State::Data) {
        if (pending.empty()) return true;
        const size_t n = static_cast<size_t>(
            std::min<uint64_t>(left, pending.size()));
        if (n && !sink(std::string_view(pending.data(), n))) return false;
        pending.erase(0, n);
        left -= n;
        if (!left) state = State::CrLf;
      } else if (state == State::CrLf) {
        if (pending.size() < 2) return true;
        if (pending.compare(0, 2, "\r\n") != 0) {
          if (err) *err = "invalid HTTP chunk delimiter";
          return false;
        }
        pending.erase(0, 2);
        state = State::Size;
      }
    }
  }
};

bool stream_once(const std::string& url, std::string_view agent,
                 const HttpHeaderCallback& on_headers,
                 const HttpBodyCallback& on_body, HttpStreamHeaders& result,
                 std::string* err) {
  if (!starts_with_ci(url, "https://")) {
    if (err) *err = "only https supported";
    return false;
  }
  std::string_view rest(url.data() + 8, url.size() - 8);
  const size_t split = rest.find_first_of("/?#");
  std::string host(rest.substr(0, split));
  const std::string resource = split == std::string_view::npos ? "/" :
      rest[split] == '/' ? std::string(rest.substr(split)) :
      rest[split] == '?' ? "/" + std::string(rest.substr(split)) : "/";
  // Fragments are client-side addresses and never belong in the request.
  const std::string path = resource.substr(0, resource.find('#'));
  int port = 443;
  if (const size_t colon = host.rfind(':'); colon != std::string::npos) {
    port = std::atoi(host.c_str() + colon + 1);
    if (port <= 0) port = 443;
    host.resize(colon);
  }
  int fd = tcp_connect(host, port, err);
  if (fd < 0) return false;
  TlsStream tls;
  if (!tls.connect(fd, host, err)) return false;
  const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
      "\r\nUser-Agent: " + std::string(agent) +
      "\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
      "Connection: close\r\n\r\n";
  if (!tls.write_all(req.data(), req.size())) {
    if (err) *err = "TLS write failed";
    return false;
  }
  std::string pending;
  ChunkDecoder chunked;
  bool have_headers = false;
  bool is_chunked = false;
  int64_t left = -1;
  char buf[8192];
  for (;;) {
    const int n = tls.read(buf, sizeof(buf));
    if (n < 0) {
      if (err) *err = "TLS receive failed or timed out";
      return false;
    }
    if (n == 0) break;
    std::string_view data(buf, static_cast<size_t>(n));
    if (!have_headers) {
      pending.append(data);
      const size_t end = pending.find("\r\n\r\n");
      if (end == std::string::npos) {
        if (pending.size() > 128 * 1024) {
          if (err) *err = "HTTP headers too large";
          return false;
        }
        continue;
      }
      const std::string headers = pending.substr(0, end);
      const size_t sp = headers.find(' ');
      if (sp == std::string::npos || sp + 4 > headers.size()) {
        if (err) *err = "malformed HTTP status";
        return false;
      }
      result.status = std::atoi(headers.c_str() + sp + 1);
      result.location = header_value(headers, "location");
      result.content_type = header_value(headers, "content-type");
      result.content_disposition = header_value(headers, "content-disposition");
      const std::string length = header_value(headers, "content-length");
      if (!length.empty()) {
        auto [p, ec] = std::from_chars(length.data(),
                                      length.data() + length.size(), left);
        if (ec != std::errc{} || p != length.data() + length.size() ||
            left < 0) {
          if (err) *err = "invalid Content-Length";
          return false;
        }
      }
      is_chunked = ascii_lower(header_value(headers, "transfer-encoding"))
                       .find("chunked") != std::string::npos;
      result.content_length = is_chunked ? -1 : left;
      have_headers = true;
      if (!on_headers(result)) return false;
      // Redirect bodies are irrelevant: follow Location immediately rather
      // than draining a potentially large response before the next request.
      if (result.status >= 300 && result.status < 400 &&
          !result.location.empty()) return true;
      if (!is_chunked && left == 0) return true;
      pending.erase(0, end + 4);
      data = pending;
    }
    if (is_chunked) {
      if (!chunked.feed(data, on_body, err)) return false;
      if (chunked.state == ChunkDecoder::State::Done) return true;
    } else {
      const size_t use = left < 0 ? data.size() :
          static_cast<size_t>(std::min<int64_t>(left, data.size()));
      if (use && !on_body(data.substr(0, use))) return false;
      if (left >= 0) {
        left -= static_cast<int64_t>(use);
        if (!left) return true;
      }
    }
    if (have_headers) pending.clear();
  }
  if (!have_headers) {
    if (err) *err = "incomplete HTTP headers";
    return false;
  }
  if ((is_chunked && chunked.state != ChunkDecoder::State::Done) ||
      (!is_chunked && left > 0)) {
    if (err) *err = "incomplete HTTP body";
    return false;
  }
  return true;
}

}  // namespace

bool https_get_follow_stream(std::string_view url, std::string_view user_agent,
                             const HttpHeaderCallback& on_headers,
                             const HttpBodyCallback& on_body,
                             std::string* err, std::string* final_url) {
  std::string current(url);
  for (int hop = 0; hop < 5; ++hop) {
    HttpStreamHeaders headers;
    const bool ok = stream_once(current, user_agent,
        [&](const HttpStreamHeaders& h) {
          if (h.status >= 300 && h.status < 400 && !h.location.empty())
            return true;
          return on_headers(h);
        },
        [&](std::string_view bytes) {
          if (headers.status >= 300 && headers.status < 400 &&
              !headers.location.empty()) return true;
          return on_body(bytes);
        }, headers, err);
    if (!ok) return false;
    if (headers.status < 300 || headers.status >= 400 ||
        headers.location.empty()) {
      if (final_url) *final_url = current;
      return true;
    }
    std::string loc = headers.location;
    if (!starts_with_ci(loc, "https://") && !starts_with_ci(loc, "http://")) {
      const size_t host_end = current.find('/', current.find("://") + 3);
      const std::string origin = current.substr(0, host_end);
      if (loc.empty() || loc[0] != '/') loc.insert(loc.begin(), '/');
      loc = origin + loc;
    }
    if (starts_with_ci(loc, "http://")) loc = "https://" + loc.substr(7);
    current = std::move(loc);
  }
  if (err) *err = "too many HTTP redirects";
  return false;
}

}  // namespace xanatoo
