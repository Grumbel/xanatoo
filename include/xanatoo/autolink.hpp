// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xanatoo {

inline void autolink_append_utf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) {
    o += static_cast<char>(cp);
  } else if (cp < 0x800) {
    o += static_cast<char>(0xC0 | (cp >> 6));
    o += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    o += static_cast<char>(0xE0 | (cp >> 12));
    o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    o += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    o += static_cast<char>(0xF0 | (cp >> 18));
    o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    o += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Bare http:// and https:// URLs in running text (display-time autolinking,
// ViewSettings::auto_links). Pure: works on decoded code points so layout can
// mark characters without touching the document.
//
// Returns [begin, end) ranges into `cps`. A URL starts at a scheme that does
// not follow a letter or digit, runs to white space or one of < > " ` { } [ ],
// keeps balanced parentheses (Wikipedia titles) and drops trailing sentence
// punctuation and an unbalanced ")".
inline std::vector<std::pair<size_t, size_t>> find_urls(
    const std::vector<uint32_t>& cps) {
  std::vector<std::pair<size_t, size_t>> out;
  auto lower = [](uint32_t c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
  };
  auto matches = [&](size_t at, const char* s) {
    size_t i = 0;
    for (; s[i]; ++i)
      if (at + i >= cps.size() || lower(cps[at + i]) !=
                                      static_cast<unsigned char>(s[i]))
        return false;
    return true;
  };
  auto alnum = [](uint32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c >= 0x80;
  };
  size_t i = 0;
  while (i < cps.size()) {
    const uint32_t c = lower(cps[i]);
    size_t scheme = 0;
    if (c == 'h' && (i == 0 || !alnum(cps[i - 1]))) {
      if (matches(i, "https://")) scheme = 8;
      else if (matches(i, "http://")) scheme = 7;
    }
    if (!scheme) {
      ++i;
      continue;
    }
    size_t end = i + scheme;
    int depth = 0;
    while (end < cps.size()) {
      const uint32_t u = cps[end];
      if (u <= 32 || u == '<' || u == '>' || u == '"' || u == '`' ||
          u == '{' || u == '}' || u == '[' || u == ']' || u == 0xFFFC)
        break;
      if (u == '(') ++depth;
      if (u == ')') {
        if (depth == 0) break;
        --depth;
      }
      ++end;
    }
    while (end > i + scheme) {
      const uint32_t t = cps[end - 1];
      if (t == '.' || t == ',' || t == ';' || t == ':' || t == '!' ||
          t == '?' || t == '\'' || t == '*' || t == '_' || t == '~')
        --end;
      else
        break;
    }
    if (end > i + scheme) out.emplace_back(i, end);
    i = end > i ? std::max(end, i + 1) : i + 1;
  }
  return out;
}

}  // namespace xanatoo
