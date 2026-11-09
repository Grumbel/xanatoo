// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Pure NIP-29 discovery tests: no network, TLS, SQLite or SDL dependencies.
#include "xanatoo/nostr_relays.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
  using namespace xanatoo;
  const std::string relay = "wss://groups.example.org";
  assert(nostr_normalize_relay("groups.example.org") == relay);
  assert(nostr_normalize_relay(" https://groups.example.org/ ") == relay);
  assert(nostr_normalize_relay("ws://groups.example.org") == relay);
  assert(nostr_normalize_relay("wss://groups.example.org/") == relay);
  assert(nostr_normalize_relay("GROUPS.example.org:443/path") ==
         "wss://groups.example.org:443/path");
  assert(nostr_normalize_relay(" ").empty());
  assert(nostr_normalize_relay("wss://").empty());
  assert(nostr_normalize_relay("https://user@groups.example.org").empty());
  assert(nostr_normalize_relay("ftp://groups.example.org").empty());
  assert(nostr_normalize_relay("groups.example.org:65536").empty());
  assert(nostr_normalize_relay("groups.example.org:0").empty());
  assert(nostr_normalize_relay("groups.example.org/#frag").empty());
  assert(nostr_normalize_relay("groups.example.org/?q=1").empty());
  assert(nostr_normalize_relay("groups.example.org/\n") == relay);

  const auto hints = nostr_group_list_relay_hints({
      {"group", "tech", "groups.example.org", "Tech"},
      {"r", "wss://groups.example.org"},
      {"r", "https://another.example"},
      {"group", "demo", "ftp://invalid.example"},
      {"group", "missing-relay"},
      {"p", "not-a-relay"}});
  assert((hints == std::vector<std::string>{
      relay, "wss://another.example"}));
  std::cout << "NIP-29 relay normalization and NIP-51 hints: PASS\n";
}
