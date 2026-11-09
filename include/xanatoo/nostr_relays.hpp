// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// NIP-29 discovery accepts a hostname as well as a full secure relay URL.
// This validates the subset supported by our WebSocket client (DNS/IPv4,
// optional numeric port and path), and never connects over cleartext.
inline std::string nostr_normalize_relay(std::string_view input) {
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
    input.remove_prefix(1);
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
    input.remove_suffix(1);
  if (input.empty()) return {};

  std::string url(input);
  auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  const auto scheme_end = url.find("://");
  if (scheme_end != std::string::npos) {
    std::string scheme = url.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), lower);
    if (scheme != "wss" && scheme != "ws" &&
        scheme != "https" && scheme != "http") return {};
    url = url.substr(scheme_end + 3);
  }
  const auto path_start = url.find('/');
  const std::string authority = url.substr(0, path_start);
  if (authority.empty() || authority.size() > 253) return {};
  const auto colon = authority.find(':');
  const std::string_view host(authority.data(),
                              colon == std::string::npos ? authority.size() : colon);
  if (host.empty() || host.front() == '.' || host.back() == '.') return {};
  for (unsigned char c : host) {
    if (!std::isalnum(c) && c != '-' && c != '.' && c != '_') return {};
  }
  for (size_t i = 0; i < host.size(); ++i)
    url[i] = lower(static_cast<unsigned char>(url[i]));
  if (colon != std::string::npos) {
    const std::string_view port(authority.data() + colon + 1,
                                authority.size() - colon - 1);
    if (port.empty() || port.size() > 5 || port == "0" ||
        !std::all_of(port.begin(), port.end(), [](unsigned char c) {
          return std::isdigit(c) != 0;
        })) return {};
    unsigned value = 0;
    for (char c : port) value = value * 10 + static_cast<unsigned>(c - '0');
    if (value == 0 || value > 65535) return {};
  }
  if (path_start != std::string::npos) {
    for (unsigned char c : std::string_view(url).substr(path_start)) {
      if (c <= 0x20 || c >= 0x7f || c == '#' || c == '?' || c == '\\') return {};
    }
  }
  std::string result = "wss://" + url;
  while (result.size() > 6 && result.back() == '/') result.pop_back();
  return result.size() <= 512 ? result : std::string();
}

// NIP-51 kind:10009 stores ["group", group-id, relay, name?] and ["r", relay].
// These are *hints*, not trustworthy group metadata or proof of membership.
inline std::vector<std::string> nostr_group_list_relay_hints(
    const std::vector<std::vector<std::string>>& tags) {
  std::vector<std::string> relays;
  for (const auto& tag : tags) {
    if (relays.size() >= 64) break;  // untrusted public event data
    const bool group = tag.size() >= 3 && tag[0] == "group";
    const bool relay = tag.size() >= 2 && tag[0] == "r";
    if (!group && !relay) continue;
    const std::string url = nostr_normalize_relay(tag[group ? 2 : 1]);
    if (!url.empty() && std::find(relays.begin(), relays.end(), url) == relays.end())
      relays.push_back(url);
  }
  return relays;
}

}  // namespace xanatoo
