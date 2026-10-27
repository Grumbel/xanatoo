// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Intentionally tiny JSON helpers for read-only API clients (Reddit, Lemmy).
// Not a full parser — string/number/object-slice extraction only.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

inline void json_append_utf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) {
    o.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline uint32_t json_hex4(std::string_view s) {
  return static_cast<uint32_t>(
      std::strtoul(std::string(s.substr(0, 4)).c_str(), nullptr, 16));
}

inline std::string json_unescape(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      if (c == 'n') o.push_back('\n');
      else if (c == 'r') o.push_back('\r');
      else if (c == 't') o.push_back('\t');
      else if (c == '"' || c == '\\' || c == '/') o.push_back(c);
      else if (c == 'u' && i + 4 < s.size()) {
        uint32_t cp = json_hex4(s.substr(i + 1));
        i += 4;
        // UTF-16 surrogate pair: \uD83D\uDE00
        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < s.size() &&
            s[i + 1] == '\\' && s[i + 2] == 'u') {
          uint32_t lo = json_hex4(s.substr(i + 3));
          if (lo >= 0xDC00 && lo < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 6;
          }
        }
        if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
        json_append_utf8(o, cp);
      } else
        o.push_back(c);
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

inline std::string json_str_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = 0;
  while ((p = json.find(pat, p)) != std::string_view::npos) {
    if (p > 0) {
      char prev = json[p - 1];
      if (prev != '{' && prev != ',' && prev != ' ' && prev != '\n' &&
          prev != '\r' && prev != '\t') {
        p += pat.size();
        continue;
      }
    }
    size_t c = json.find(':', p + pat.size());
    if (c == std::string_view::npos) return {};
    size_t v = c + 1;
    while (v < json.size() &&
           std::isspace(static_cast<unsigned char>(json[v])))
      ++v;
    if (v >= json.size()) return {};
    if (json[v] == 'n' && json.substr(v, 4) == "null") return {};
    if (json[v] != '"') return {};
    ++v;
    std::string out;
    while (v < json.size()) {
      if (json[v] == '\\' && v + 1 < json.size()) {
        out.push_back('\\');
        out.push_back(json[v + 1]);
        v += 2;
        continue;
      }
      if (json[v] == '"') break;
      out.push_back(json[v++]);
    }
    return json_unescape(out);
  }
  return {};
}

inline int64_t json_int_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return 0;
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return 0;
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  return std::strtoll(std::string(json.substr(p, 24)).c_str(), nullptr, 10);
}

inline bool json_bool_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return false;
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return false;
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  return p + 4 <= json.size() && json.substr(p, 4) == "true";
}

// Copy of object `obj` with every nested object / array value replaced by
// null, so the flat *_field lookups only see its own top-level fields.
inline std::string json_top_level(std::string_view obj) {
  std::string out;
  out.reserve(obj.size());
  int depth = 0;
  for (size_t i = 0; i < obj.size(); ++i) {
    char c = obj[i];
    if (c == '"') {
      size_t start = i++;
      while (i < obj.size() && obj[i] != '"') i += (obj[i] == '\\') ? 2 : 1;
      if (depth <= 1) out.append(obj.substr(start, i - start + 1));
      continue;
    }
    if (c == '{' || c == '[') {
      if (depth == 1) out += "null";
      ++depth;
      if (depth == 1) out.push_back(c);
      continue;
    }
    if (c == '}' || c == ']') {
      --depth;
      if (depth == 0) out.push_back(c);
      continue;
    }
    if (depth <= 1) out.push_back(c);
  }
  return out;
}

// Object value after "key": { ... } (brace-matched, string-aware).
inline std::string_view json_object_after_key(std::string_view json,
                                             std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return {};
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return {};
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  if (p >= json.size() || json[p] != '{') return {};
  int depth = 0;
  size_t start = p;
  for (size_t i = p; i < json.size(); ++i) {
    char c = json[i];
    if (c == '"') {
      ++i;
      while (i < json.size()) {
        if (json[i] == '\\') {
          i += 2;
          continue;
        }
        if (json[i] == '"') break;
        ++i;
      }
      continue;
    }
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) return json.substr(start, i - start + 1);
    }
  }
  return {};
}

// Array of objects after "key": [ {...}, ... ].
inline std::vector<std::string> json_object_array_after_key(
    std::string_view json, std::string_view key) {
  std::vector<std::string> out;
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return out;
  p = json.find('[', p + pat.size());
  if (p == std::string_view::npos) return out;
  ++p;
  while (p < json.size()) {
    while (p < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[p])) || json[p] == ','))
      ++p;
    if (p >= json.size() || json[p] == ']') break;
    if (json[p] != '{') break;
    int depth = 0;
    size_t start = p;
    for (size_t i = p; i < json.size(); ++i) {
      char c = json[i];
      if (c == '"') {
        ++i;
        while (i < json.size()) {
          if (json[i] == '\\') {
            i += 2;
            continue;
          }
          if (json[i] == '"') break;
          ++i;
        }
        continue;
      }
      if (c == '{') ++depth;
      else if (c == '}') {
        --depth;
        if (depth == 0) {
          out.emplace_back(json.substr(start, i - start + 1));
          p = i + 1;
          break;
        }
      }
    }
    if (depth != 0) break;
  }
  return out;
}

}  // namespace xanatoo
