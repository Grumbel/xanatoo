// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/http_url.hpp"

#include <charconv>
#include <vector>

namespace xanatoo {
namespace {

bool valid_url_bytes(std::string_view s) {
  for (unsigned char c : s)
    if (c <= 32 || c == 127) return false;
  return true;
}

bool starts_ci(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    const char c = s[i] >= 'A' && s[i] <= 'Z' ?
        static_cast<char>(s[i] - 'A' + 'a') : s[i];
    if (c != prefix[i]) return false;
  }
  return true;
}

std::string normalize_path(std::string_view path) {
  const size_t query = path.find_first_of("?#");
  const std::string_view pathname = path.substr(0, query);
  const std::string_view suffix = query == std::string_view::npos ?
      std::string_view{} : path.substr(query);
  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start < pathname.size()) {
    const size_t end = pathname.find('/', start);
    const std::string_view segment = pathname.substr(start,
        end == std::string_view::npos ? end : end - start);
    if (segment == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (!segment.empty() && segment != ".") {
      segments.push_back(segment);
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  std::string result = "/";
  for (const auto& segment : segments) {
    if (result.size() > 1) result += '/';
    result += segment;
  }
  if (pathname.size() > 1 &&
      (pathname.back() == '/' || pathname.ends_with("/.") ||
       pathname.ends_with("/..")) && result.back() != '/') result += '/';
  result += suffix;
  return result;
}

}  // namespace

std::optional<HttpUrl> parse_http_url(std::string_view url) {
  HttpUrl result;
  size_t prefix = 0;
  if (starts_ci(url, "https://")) {
    result.secure = true;
    result.port = 443;
    prefix = 8;
  } else if (starts_ci(url, "http://")) {
    prefix = 7;
  } else {
    return std::nullopt;
  }
  if (!valid_url_bytes(url)) return std::nullopt;
  const std::string_view rest = url.substr(prefix);
  const size_t split = rest.find_first_of("/?#");
  const std::string_view authority = rest.substr(0, split);
  if (authority.empty() || authority.find('@') != std::string_view::npos ||
      authority.find('\\') != std::string_view::npos) return std::nullopt;
  result.authority = std::string(authority);
  std::string_view port;
  if (authority.front() == '[') {
    const size_t end = authority.find(']');
    if (end == std::string_view::npos || end == 1) return std::nullopt;
    result.host = std::string(authority.substr(1, end - 1));
    if (end + 1 < authority.size()) {
      if (authority[end + 1] != ':') return std::nullopt;
      port = authority.substr(end + 2);
    }
  } else {
    const size_t colon = authority.find(':');
    result.host = std::string(authority.substr(0, colon));
    if (colon != std::string_view::npos) port = authority.substr(colon + 1);
  }
  if (result.host.empty() || (result.host.find(':') != std::string::npos &&
      authority.front() != '[')) return std::nullopt;
  if (authority.back() == ':') return std::nullopt;
  if (!port.empty()) {
    const auto [end, ec] = std::from_chars(port.data(),
        port.data() + port.size(), result.port);
    if (ec != std::errc{} || end != port.data() + port.size() ||
        result.port < 1 || result.port > 65535) return std::nullopt;
  }
  std::string_view target = split == std::string_view::npos ?
      std::string_view{} : rest.substr(split);
  target = target.substr(0, target.find('#'));
  if (target.empty()) result.target = "/";
  else if (target.front() == '?') result.target = "/" + std::string(target);
  else result.target = std::string(target);
  return result;
}

std::optional<std::string> resolve_http_redirect(std::string_view base,
                                                 std::string_view location) {
  const auto url = parse_http_url(base);
  if (!url || !valid_url_bytes(location) || location.empty()) return std::nullopt;
  if (starts_ci(location, "https://") ||
      starts_ci(location, "http://")) {
    if (!parse_http_url(location)) return std::nullopt;
    return std::string(location);
  }
  if (location.find(':') < location.find_first_of("/?#")) return std::nullopt;
  const std::string origin = std::string(url->secure ? "https://" : "http://") +
      url->authority;
  if (location.starts_with("//")) {
    const std::string result = std::string(url->secure ? "https:" : "http:") +
        std::string(location);
    return parse_http_url(result) ? std::optional(result) : std::nullopt;
  }
  if (location.front() == '/')
    return origin + std::string(location);  // preserve intentional // in paths
  if (location.front() == '#') return std::string(base.substr(0, base.find('#')))
      + std::string(location);
  if (location.front() == '?') {
    const std::string_view path = url->target.substr(0, url->target.find('?'));
    return origin + std::string(path) + std::string(location);
  }
  const std::string_view path = url->target.substr(0, url->target.find('?'));
  const size_t slash = path.rfind('/');
  const std::string_view parent = slash == std::string_view::npos ?
      std::string_view("/") : path.substr(0, slash + 1);
  return origin + normalize_path(std::string(parent) + std::string(location));
}

}  // namespace xanatoo
