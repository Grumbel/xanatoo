// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>
#include <string_view>

namespace xanatoo {

// A text drop can be a plain URL or a text/uri-list (comments and CRLF).
// Only navigation targets are accepted: ordinary prose must not open a local
// filename, and unsupported schemes such as javascript:/data: are ignored.
inline std::string dropped_navigation_target(std::string_view text) {
  constexpr std::string_view spaces = " \t\r\n";
  auto trim = [&](std::string_view& s) {
    const size_t begin = s.find_first_not_of(spaces);
    if (begin == std::string_view::npos) { s = {}; return; }
    s.remove_prefix(begin);
    s.remove_suffix(s.size() - s.find_last_not_of(spaces) - 1);
  };
  auto valid = [](std::string_view s) {
    for (const unsigned char c : s)
      if (c <= 0x20 || c == 0x7f) return false;
    return true;
  };
  auto prefix = [](std::string_view s, std::string_view p) {
    return s.starts_with(p);
  };
  auto local_file = [](std::string_view uri) -> std::string {
    uri.remove_prefix(7); // file://
    if (uri.starts_with("localhost/")) uri.remove_prefix(9);
    if (uri.empty() || uri.front() != '/') return {};
    std::string path;
    path.reserve(uri.size());
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    for (size_t i = 0; i < uri.size(); ++i) {
      char c = uri[i];
      if (c == '%') {
        if (i + 2 >= uri.size()) return {};
        const int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
        if (hi < 0 || lo < 0) return {};
        c = static_cast<char>(hi * 16 + lo);
        i += 2;
      }
      if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return {};
      path.push_back(c);
    }
#if defined(_WIN32)
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
      path.erase(path.begin());
#endif
    return path;
  };

  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    trim(line);
    if (line.empty() || line.front() == '#' || !valid(line)) continue;
    if (prefix(line, "file://")) {
      if (std::string path = local_file(line); !path.empty()) return path;
      continue;
    }
    if (prefix(line, "https://") || prefix(line, "http://") ||
        prefix(line, "gemini://")) {
      const size_t start = line.find("://") + 3;
      if (start < line.size() && line[start] != '/' &&
          line[start] != '?' && line[start] != '#')
        return std::string(line);
      continue;
    }
    for (std::string_view scheme : {"hn:", "reddit:", "lemmy:",
                                    "nostr:", "wikipedia:", "feed:",
                                    "sitemap:", "search:", "db:",
                                    "db-original:", "draft:"})
      if (line.starts_with(scheme) && line.size() > scheme.size())
        return std::string(line);
  }
  return {};
}

} // namespace xanatoo
