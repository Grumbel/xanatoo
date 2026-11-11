// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/https.hpp"
#include "xanatoo/http_url.hpp"
#include "xanatoo/net_address.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace xanatoo {
namespace {

int tcp_connect(const std::string& host, int port, std::string* err,
                bool public_only = false) {
  addrinfo hints {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  std::string port_s = std::to_string(port);
  if (const int gai = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res)) {
    if (err) *err = "DNS lookup failed for " + host + " (" + gai_strerror(gai) + ")";
    return -1;
  }
  int fd = -1;
  int last_errno = 0;
  bool attempted = false;
  for (addrinfo* p = res; p; p = p->ai_next) {
    if (public_only && !public_ip_address(p->ai_addr)) continue;
    attempted = true;
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (public_only) {
      // Untrusted auto-discovery must not block on an unresponsive host for
      // the default OS TCP connect timeout (sometimes many minutes).
      const int flags = fcntl(fd, F_GETFL, 0);
      if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
        int result = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
          pollfd wait{fd, POLLOUT, 0};
          if (::poll(&wait, 1, 4000) > 0) {
            socklen_t len = sizeof(result);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &len) == 0 &&
                result == 0) result = 0;
            else result = -1;
          } else result = -1;
        }
        fcntl(fd, F_SETFL, flags);
        if (result == 0) break;
      }
    } else if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }
    last_errno = errno;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0 && err) *err = attempted ? "connect failed to " + host + ":" +
                                            port_s + " (" +
                                            std::strerror(last_errno) + ")" :
                          "private/local address excluded for " + host;
  return fd;
}

// A single HTTP/1.1 connection. The TLS layer owns secure sockets; the raw
// layer owns plain sockets. No TLS handshake is attempted for http://.
class HttpConnection {
public:
  HttpConnection() = default;
  HttpConnection(const HttpConnection&) = delete;
  HttpConnection& operator=(const HttpConnection&) = delete;
  ~HttpConnection() { if (plain_fd_ >= 0) ::close(plain_fd_); }

  bool open(const HttpUrl& url, std::string* err,
            HttpRequestSecurity security = {}) {
    if (security.require_https && !url.secure) {
      if (err) *err = "authenticated request requires HTTPS";
      return false;
    }
    const int fd = tcp_connect(url.host, url.port, err, security.public_only);
    if (fd < 0) return false;
    secure_ = url.secure;
    if (secure_) return tls_.connect(fd, url.host, err, security.verify_tls);
    plain_fd_ = fd;
    return true;
  }

  bool write_all(const void* data, size_t size) {
    if (secure_) return tls_.write_all(data, size);
    const char* ptr = static_cast<const char*>(data);
    while (size) {
#ifdef MSG_NOSIGNAL
      constexpr int flags = MSG_NOSIGNAL;
#else
      constexpr int flags = 0;
#endif
      const ssize_t n = ::send(plain_fd_, ptr, size, flags);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      ptr += n;
      size -= static_cast<size_t>(n);
    }
    return true;
  }

  int read(void* dst, size_t size) {
    if (secure_) return tls_.read(dst, size);
    for (;;) {
      const ssize_t n = ::recv(plain_fd_, dst, size, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) read_errno_ = errno;
      return static_cast<int>(n);
    }
  }

  // Reason for the last failed read() (empty if unknown).
  std::string last_error() const {
    if (secure_) return tls_.last_error();
    return std::strerror(read_errno_);
  }

private:
  int read_errno_ = 0;
  int plain_fd_ = -1;
  bool secure_ = false;
  TlsStream tls_;
};

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
                                        std::string* err,
                                        HttpRequestSecurity security) {
  const auto parsed = parse_http_url(url);
  if (!parsed) {
    if (err) *err = "invalid http(s) URL";
    return std::nullopt;
  }
  HttpConnection conn;
  if (!conn.open(*parsed, err, security)) return std::nullopt;

  std::ostringstream req;
  const bool custom_accept = ascii_lower(extra_headers).find("accept:") !=
      std::string::npos;
  req << method << " " << parsed->target << " HTTP/1.1\r\n"
      << "Host: " << parsed->authority << "\r\n"
      << "User-Agent: " << user_agent << "\r\n";
  if (!custom_accept) req << "Accept: application/json, text/plain, */*\r\n";
  req << "Accept-Language: en-US,en;q=0.9\r\n"
      << "Connection: close\r\n";
  if (!extra_headers.empty()) req << extra_headers;
  if (!body_in.empty())
    req << "Content-Length: " << body_in.size() << "\r\n";
  req << "\r\n";
  if (!body_in.empty()) req << body_in;
  std::string req_s = req.str();
  if (!conn.write_all(req_s.data(), req_s.size())) {
    if (err) *err = "HTTP write failed";
    return std::nullopt;
  }

  std::string data;
  char buf[4096];
  for (;;) {
    int n = conn.read(buf, sizeof(buf));
    if (n <= 0) break;
    data.append(buf, static_cast<size_t>(n));
    if (data.size() > security.max_response_bytes) {
      if (err) *err = "HTTP response exceeds size limit";
      return std::nullopt;
    }
  }

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
    if (v0 == std::string::npos) return {};
    // The block has no CRLF after its last header.
    if (v1 == std::string::npos) v1 = headers.size();
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
    auto next = resolve_http_redirect(current, resp->location);
    if (!next) {
      if (err) *err = "unsupported HTTP redirect: " + resp->location;
      return std::nullopt;
    }
    if (!http_redirect_keeps_security(current, *next)) {
      if (err) *err = "HTTPS redirect refused: destination uses plaintext HTTP";
      return std::nullopt;
    }
    current = std::move(*next);
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
  const auto parsed = parse_http_url(url);
  if (!parsed) {
    if (err) *err = "invalid http(s) URL";
    return false;
  }
  HttpConnection conn;
  if (!conn.open(*parsed, err)) return false;
  const std::string req = "GET " + parsed->target + " HTTP/1.1\r\nHost: " + parsed->authority +
      "\r\nUser-Agent: " + std::string(agent) +
      "\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
      "Connection: close\r\n\r\n";
  if (!conn.write_all(req.data(), req.size())) {
    if (err) *err = "HTTP write failed";
    return false;
  }
  std::string pending;
  ChunkDecoder chunked;
  bool have_headers = false;
  bool is_chunked = false;
  int64_t left = -1;
  char buf[8192];
  uint64_t total_read = 0;
  for (;;) {
    const int n = conn.read(buf, sizeof(buf));
    if (n < 0) {
      if (err) {
        std::string where = have_headers
            ? "while receiving the response body (" +
                  std::to_string(total_read) + " bytes received" +
                  (left >= 0 ? " of " + std::to_string(total_read + left) : "") +
                  (is_chunked ? ", chunked" : "") + ")"
            : "while waiting for the response headers (" +
                  std::to_string(total_read) + " bytes received)";
        *err = "Network error " + where + ": " + conn.last_error();
      }
      return false;
    }
    if (n == 0) break;
    total_read += static_cast<uint64_t>(n);
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
      result.server = header_value(headers, "server");
      result.cf_mitigated = header_value(headers, "cf-mitigated");
      result.retry_after = header_value(headers, "retry-after");
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
    auto next = resolve_http_redirect(current, headers.location);
    if (!next) {
      if (err) *err = "unsupported HTTP redirect: " + headers.location;
      return false;
    }
    if (!http_redirect_keeps_security(current, *next)) {
      if (err) *err = "HTTPS redirect refused: destination uses plaintext HTTP";
      return false;
    }
    current = std::move(*next);
  }
  if (err) *err = "too many HTTP redirects";
  return false;
}

}  // namespace xanatoo
