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
        i += 4;
        o.push_back('?');
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
