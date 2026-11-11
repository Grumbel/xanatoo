// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Small JSON tree for deeply nested API replies (YouTube InnerTube), where the
// flat helpers of json_min.hpp would hit the wrong key. Header-only, no
// exceptions, bounded nesting. Pure data + free functions.

#include "xanatoo/json_min.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xanatoo {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> items;                           // Array
  std::vector<std::pair<std::string, Json>> fields;  // Object, in source order
};

namespace json_dom_detail {

struct Reader {
  std::string_view t;
  size_t i = 0;

  void ws() {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' ||
                            t[i] == '\r'))
      ++i;
  }

  bool string(std::string& out) {
    if (i >= t.size() || t[i] != '"') return false;
    const size_t start = ++i;
    while (i < t.size() && t[i] != '"') i += t[i] == '\\' ? 2 : 1;
    if (i >= t.size()) return false;
    out = json_unescape(t.substr(start, i - start));
    ++i;
    return true;
  }

  bool value(Json& v, int depth) {
    if (depth > 200) return false;
    ws();
    if (i >= t.size()) return false;
    const char c = t[i];
    if (c == '{') {
      v.type = Json::Type::Object;
      ++i;
      ws();
      if (i < t.size() && t[i] == '}') return ++i, true;
      for (;;) {
        ws();
        std::string key;
        if (!string(key)) return false;
        ws();
        if (i >= t.size() || t[i] != ':') return false;
        ++i;
        Json child;
        if (!value(child, depth + 1)) return false;
        v.fields.emplace_back(std::move(key), std::move(child));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') return ++i, true;
        return false;
      }
    }
    if (c == '[') {
      v.type = Json::Type::Array;
      ++i;
      ws();
      if (i < t.size() && t[i] == ']') return ++i, true;
      for (;;) {
        Json child;
        if (!value(child, depth + 1)) return false;
        v.items.push_back(std::move(child));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == ']') return ++i, true;
        return false;
      }
    }
    if (c == '"') {
      v.type = Json::Type::String;
      return string(v.s);
    }
    if (t.substr(i, 4) == "true") {
      v.type = Json::Type::Bool, v.b = true, i += 4;
      return true;
    }
    if (t.substr(i, 5) == "false") {
      v.type = Json::Type::Bool, i += 5;
      return true;
    }
    if (t.substr(i, 4) == "null") return i += 4, true;
    const size_t start = i;
    while (i < t.size() && (t[i] == '-' || t[i] == '+' || t[i] == '.' ||
                            t[i] == 'e' || t[i] == 'E' ||
                            (t[i] >= '0' && t[i] <= '9')))
      ++i;
    if (i == start) return false;
    v.type = Json::Type::Number;
    v.n = std::strtod(std::string(t.substr(start, i - start)).c_str(), nullptr);
    return true;
  }
};

}  // namespace json_dom_detail

inline std::optional<Json> json_parse(std::string_view text) {
  json_dom_detail::Reader r{text, 0};
  Json v;
  if (!r.value(v, 0)) return std::nullopt;
  return v;
}

// Member of an object, nullptr when absent or `v` is no object.
inline const Json* json_get(const Json& v, std::string_view key) {
  if (v.type != Json::Type::Object) return nullptr;
  for (const auto& f : v.fields)
    if (f.first == key) return &f.second;
  return nullptr;
}

// Array element, nullptr out of range.
inline const Json* json_at(const Json& v, size_t index) {
  if (v.type != Json::Type::Array || index >= v.items.size()) return nullptr;
  return &v.items[index];
}

// Follow "a/b/c" (object keys; all-digit segments index arrays). nullptr when
// any step is missing.
inline const Json* json_path(const Json& v, std::string_view path) {
  const Json* cur = &v;
  while (cur && !path.empty()) {
    const size_t slash = path.find('/');
    const std::string_view seg = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view()
                                           : path.substr(slash + 1);
    const bool digits = !seg.empty() && seg.find_first_not_of("0123456789") ==
                                            std::string_view::npos;
    cur = digits && cur->type == Json::Type::Array
              ? json_at(*cur, std::strtoul(std::string(seg).c_str(), nullptr, 10))
              : json_get(*cur, seg);
  }
  return cur;
}

inline std::string json_string(const Json* v) {
  return v && v->type == Json::Type::String ? v->s : std::string();
}

// Number or numeric string → integer; 0 otherwise.
inline long long json_number(const Json* v) {
  if (!v) return 0;
  if (v->type == Json::Type::Number) return static_cast<long long>(v->n);
  if (v->type == Json::Type::String)
    return std::strtoll(v->s.c_str(), nullptr, 10);
  return 0;
}

// Pre-order walk (document order). `fn(key, value)` is called for every object
// member; return false from it to stop. Returns false when stopped.
template <class Fn>
bool json_walk(const Json& v, Fn&& fn) {
  if (v.type == Json::Type::Object) {
    for (const auto& f : v.fields) {
      if (!fn(std::string_view(f.first), f.second)) return false;
      if (!json_walk(f.second, fn)) return false;
    }
  } else if (v.type == Json::Type::Array) {
    for (const Json& c : v.items)
      if (!json_walk(c, fn)) return false;
  }
  return true;
}

// First member called `key` anywhere below `v` (document order).
inline const Json* json_find(const Json& v, std::string_view key) {
  const Json* found = nullptr;
  json_walk(v, [&](std::string_view k, const Json& c) {
    if (k != key) return true;
    found = &c;
    return false;
  });
  return found;
}

// Every member called `key` below `v`, outermost first; matches are not
// searched further, so a nested match inside a match is not listed.
inline void json_find_all(const Json& v, std::string_view key,
                          std::vector<const Json*>& out) {
  if (v.type == Json::Type::Object) {
    for (const auto& f : v.fields) {
      if (f.first == key) out.push_back(&f.second);
      else json_find_all(f.second, key, out);
    }
  } else if (v.type == Json::Type::Array) {
    for (const Json& c : v.items) json_find_all(c, key, out);
  }
}

}  // namespace xanatoo
