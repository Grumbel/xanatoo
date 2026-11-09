// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Pure NIP-29 discovery tests: no network, TLS, SQLite or SDL dependencies.
#include "xanatoo/nostr_relays.hpp"
#include "xanatoo/nostr_nip11.hpp"

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
  assert(nostr_nip11_url("wss://groups.example.org/path") ==
         "https://groups.example.org/path");
  assert(nostr_nip11_url("http://groups.example.org").empty());
  const std::string master(64, 'a'), admin(64, 'b');
  const auto nip11 = nostr_parse_nip11("{\"pubkey\":\"" + admin +
      "\",\"supported_nips\":[1,11,29],\"other\":{\"self\":\"" + admin +
      "\"},\"self\":\"" + master + "\"}");
  assert(nip11 && nip11->relay_self == master && nip11->advertises_nip29);
  assert(nostr_relay_master_matches(master, master, true));
  assert(!nostr_relay_master_matches(master, master, false));
  assert(!nostr_relay_master_matches(master, admin, true));
  const auto nested = nostr_parse_nip11("{\"pubkey\":\"" + admin +
      "\",\"nested\":{\"supported_nips\":[29]}}");
  assert(nested && nested->relay_self.empty() && !nested->advertises_nip29);
  const auto string_nip = nostr_parse_nip11("{\"supported_nips\":[\"29\"]}");
  assert(string_nip && !string_nip->advertises_nip29);
  assert(!nostr_parse_nip11("<html>not NIP-11</html>"));
  std::cout << "NIP-29 relay normalization and NIP-51 hints: PASS\n";
}
