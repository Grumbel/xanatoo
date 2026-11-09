// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// NIP-11 relay metadata. Pure parsing and matching: network trust is supplied
// by the caller, which MUST validate the HTTPS certificate and hostname.

#include "xanatoo/json_min.hpp"
#include "xanatoo/nostr_relays.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

struct NostrNip11Info {
  std::string relay_self;  // NIP-11 "self" (NOT "pubkey", the admin contact)
  bool advertises_nip29 = false;
};

inline std::string nip11_lower(std::string_view input) {
  std::string out(input);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

inline std::string nostr_nip11_url(std::string_view relay) {
  if (relay.rfind("wss://", 0) != 0 ||
      nostr_normalize_relay(relay) != relay) return {};
  return "https" + std::string(relay.substr(3));
}

inline std::optional<NostrNip11Info> nostr_parse_nip11(std::string_view json) {
  if (json.size() > 128 * 1024) return std::nullopt;
  while (!json.empty() && std::isspace(static_cast<unsigned char>(json.front())))
    json.remove_prefix(1);
  while (!json.empty() && std::isspace(static_cast<unsigned char>(json.back())))
    json.remove_suffix(1);
  if (json.size() < 2 || json.front() != '{' || json.back() != '}')
    return std::nullopt;
  NostrNip11Info out;
  out.relay_self = nip11_lower(json_str_field(json_top_level(json), "self"));
  const auto is_hex = [](unsigned char c) { return std::isxdigit(c) != 0; };
  if (out.relay_self.size() != 64 ||
      !std::all_of(out.relay_self.begin(), out.relay_self.end(), is_hex))
    out.relay_self.clear();

  // Search only top-level keys: a nested "supported_nips" field must not
  // masquerade as the relay's own capabilities.
  int depth = 0;
  for (size_t i = 0; i < json.size();) {
    const char c = json[i];
    if (c == '"') {
      const size_t start = ++i;
      while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\') ++i;
        ++i;
      }
      if (i >= json.size()) break;
      const std::string_view key = json.substr(start, i - start);
      ++i;
      if (depth != 1 || key != "supported_nips") continue;
      while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
      if (i >= json.size() || json[i++] != ':') continue;
      while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
      if (i >= json.size() || json[i++] != '[') continue;
      // Integers only, as required by NIP-11; quoted strings are ignored.
      for (;;) {
        while (i < json.size() && (std::isspace(static_cast<unsigned char>(json[i])) ||
                                    json[i] == ',')) ++i;
        if (i >= json.size() || json[i] == ']') break;
        int nip = 0;
        const char* first = json.data() + i;
        const char* last = json.data() + json.size();
        const auto parsed = std::from_chars(first, last, nip);
        if (parsed.ec != std::errc{} || parsed.ptr == first) break;
        if (nip == 29) out.advertises_nip29 = true;
        i = static_cast<size_t>(parsed.ptr - json.data());
        if (i >= json.size() || (json[i] != ',' && json[i] != ']' &&
            !std::isspace(static_cast<unsigned char>(json[i])))) break;
      }
      break;
    }
    if (c == '{' || c == '[') ++depth;
    if (c == '}' || c == ']') --depth;
    if (depth < 0) return std::nullopt;
    ++i;
  }
  return out;
}

inline bool nostr_relay_master_matches(std::string_view relay_self,
                                        std::string_view event_author,
                                        bool signature_verified) {
  return signature_verified && relay_self.size() == 64 &&
      nip11_lower(relay_self) == nip11_lower(event_author);
}

}  // namespace xanatoo
