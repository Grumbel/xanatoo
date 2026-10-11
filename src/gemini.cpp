// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/gemini.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#endif

namespace xanatoo {

bool is_gemini_url(std::string_view url) {
  if (url.size() < 9) return false;
  auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  return lower(url[0]) == 'g' && lower(url[1]) == 'e' && lower(url[2]) == 'm' &&
         lower(url[3]) == 'i' && lower(url[4]) == 'n' && lower(url[5]) == 'i' &&
         url[6] == ':' && url[7] == '/' && url[8] == '/';
}

std::optional<GeminiUrl> parse_gemini_url(std::string_view url) {
  if (!is_gemini_url(url)) return std::nullopt;
  std::string_view rest = url.substr(9);  // after gemini://
  // strip fragment
  size_t hash = rest.find('#');
  if (hash != std::string_view::npos) rest = rest.substr(0, hash);

  size_t slash = rest.find('/');
  std::string_view authority =
      (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
  std::string_view path =
      (slash == std::string_view::npos) ? std::string_view("/") : rest.substr(slash);
  if (path.empty()) path = "/";

  if (authority.empty()) return std::nullopt;

  GeminiUrl out;
  out.port = 1965;
  // [ipv6]:port or host:port or host
  if (!authority.empty() && authority.front() == '[') {
    size_t br = authority.find(']');
    if (br == std::string_view::npos) return std::nullopt;
    out.host = std::string(authority.substr(1, br - 1));
    if (br + 1 < authority.size() && authority[br + 1] == ':') {
      out.port = std::atoi(std::string(authority.substr(br + 2)).c_str());
      if (out.port <= 0) out.port = 1965;
    }
  } else {
    size_t colon = authority.rfind(':');
    // Avoid treating IPv4 as host:port wrongly — only split if port is digits
    if (colon != std::string_view::npos && colon + 1 < authority.size()) {
      bool digits = true;
      for (size_t i = colon + 1; i < authority.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(authority[i]))) {
          digits = false;
          break;
        }
      }
      if (digits) {
        out.host = std::string(authority.substr(0, colon));
        out.port = std::atoi(std::string(authority.substr(colon + 1)).c_str());
        if (out.port <= 0) out.port = 1965;
      } else {
        out.host = std::string(authority);
      }
    } else {
      out.host = std::string(authority);
    }
  }
  if (out.host.empty()) return std::nullopt;
  out.path_query = std::string(path);

  std::ostringstream canon;
  canon << "gemini://" << out.host;
  if (out.port != 1965) canon << ':' << out.port;
  canon << out.path_query;
  out.canonical = canon.str();
  return out;
}

static std::string join_path(std::string_view base_path, std::string_view rel) {
  if (rel.empty()) return std::string(base_path);
  if (rel.front() == '/') return std::string(rel);
  std::string dir(base_path);
  size_t slash = dir.rfind('/');
  if (slash == std::string::npos)
    dir = "/";
  else
    dir.resize(slash + 1);
  dir.append(rel);
  // crude .. resolution
  std::string out;
  size_t i = 0;
  while (i < dir.size()) {
    if (dir[i] == '/') {
      out.push_back('/');
      ++i;
      continue;
    }
    size_t j = dir.find('/', i);
    if (j == std::string::npos) j = dir.size();
    std::string seg = dir.substr(i, j - i);
    i = j;
    if (seg == ".") continue;
    if (seg == "..") {
      if (out.size() > 1) {
        if (out.back() == '/') out.pop_back();
        size_t p = out.rfind('/');
        if (p == std::string::npos)
          out.clear();
        else
          out.resize(p + 1);
      }
      continue;
    }
    out += seg;
  }
  if (out.empty()) out = "/";
  return out;
}

std::string resolve_gemini_url(std::string_view base, std::string_view ref) {
  if (ref.empty()) return std::string(base);
  if (is_gemini_url(ref)) return std::string(ref);
  // other absolute schemes: return as-is
  if (ref.find("://") != std::string_view::npos) return std::string(ref);

  auto b = parse_gemini_url(base);
  if (!b) return {};

  std::string path;
  if (!ref.empty() && ref.front() == '/') {
    path = std::string(ref);
  } else {
    // Strip ?query from the base path before joining (query is not hierarchical).
    std::string base_path = b->path_query;
    size_t qpos = base_path.find('?');
    if (qpos != std::string::npos) base_path.resize(qpos);
    path = join_path(base_path, ref);
  }

  std::ostringstream o;
  o << "gemini://" << b->host;
  if (b->port != 1965) o << ':' << b->port;
  o << path;
  return o.str();
}

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI

static bool ssl_inited = false;
static void ensure_ssl() {
  if (ssl_inited) return;
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  ssl_inited = true;
}

static int tcp_connect(const std::string& host, int port, std::string* err) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  std::string port_s = std::to_string(port);
  int ga = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
  if (ga != 0) {
    if (err) *err = std::string("DNS: ") + gai_strerror(ga);
    return -1;
  }
  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv{};
    tv.tv_sec = 20;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0 && err) *err = "connect failed";
  return fd;
}

static std::optional<GeminiResponse> gemini_fetch_once(const GeminiUrl& u,
                                                       std::string* err) {
  ensure_ssl();
  int fd = tcp_connect(u.host, u.port, err);
  if (fd < 0) return std::nullopt;

  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx) {
    ::close(fd);
    if (err) *err = "SSL_CTX_new failed";
    return std::nullopt;
  }
  // Gemini commonly uses self-signed certs; do not fail handshake on verify.
  SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

  SSL* ssl = SSL_new(ctx);
  if (!ssl) {
    SSL_CTX_free(ctx);
    ::close(fd);
    if (err) *err = "SSL_new failed";
    return std::nullopt;
  }
  SSL_set_fd(ssl, fd);
  SSL_set_tlsext_host_name(ssl, u.host.c_str());

  if (SSL_connect(ssl) != 1) {
    if (err) *err = "TLS handshake failed";
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return std::nullopt;
  }

  std::string req = u.canonical + "\r\n";
  if (SSL_write(ssl, req.data(), static_cast<int>(req.size())) <= 0) {
    if (err) *err = "TLS write failed";
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return std::nullopt;
  }

  std::string data;
  char buf[4096];
  for (;;) {
    int n = SSL_read(ssl, buf, sizeof(buf));
    if (n <= 0) break;
    data.append(buf, static_cast<size_t>(n));
    if (data.size() > 8 * 1024 * 1024) break;  // 8 MiB cap
  }

  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(ctx);
  ::close(fd);

  // Header: STATUS SP META\r\n
  size_t line_end = data.find("\r\n");
  if (line_end == std::string::npos) {
    if (err) *err = "malformed Gemini response (no header)";
    return std::nullopt;
  }
  std::string header = data.substr(0, line_end);
  GeminiResponse resp;
  resp.final_url = u.canonical;
  if (header.size() < 2 ||
      !std::isdigit(static_cast<unsigned char>(header[0])) ||
      !std::isdigit(static_cast<unsigned char>(header[1]))) {
    if (err) *err = "malformed Gemini status";
    return std::nullopt;
  }
  resp.status = (header[0] - '0') * 10 + (header[1] - '0');
  size_t meta_off = 2;
  if (meta_off < header.size() && header[meta_off] == ' ') ++meta_off;
  resp.meta = header.substr(meta_off);
  resp.body = data.substr(line_end + 2);
  return resp;
}

std::optional<GeminiResponse> gemini_fetch(std::string_view url,
                                           std::string* err) {
  auto u = parse_gemini_url(url);
  if (!u) {
    if (err) *err = "not a gemini:// URL";
    return std::nullopt;
  }

  for (int hop = 0; hop < 5; ++hop) {
    auto resp = gemini_fetch_once(*u, err);
    if (!resp) return std::nullopt;
    if (resp->status == 30 || resp->status == 31) {
      // redirect — meta is new URL
      std::string next = resolve_gemini_url(u->canonical, resp->meta);
      if (next.empty() || !is_gemini_url(next)) {
        if (err) *err = "bad redirect target";
        return std::nullopt;
      }
      u = parse_gemini_url(next);
      if (!u) return std::nullopt;
      continue;
    }
    return resp;
  }
  if (err) *err = "too many redirects";
  return std::nullopt;
}

#endif  // XANATOO_GEMINI

}  // namespace xanatoo
