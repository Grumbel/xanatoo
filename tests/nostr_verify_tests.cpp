// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Official BIP-340 vectors and NIP-01 serialization smoke tests.
#include "xanatoo/nostr_verify.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace xanatoo;
  // Standalone NIP-01 fixture signed with the public BIP-340 test private
  // key 3. Checked against an independent Python big-integer implementation.
  const std::string signed_event =
      R"({"id":"da67626634b798564d957125c4105766d6a681ff62eb48a87f5ec4e819ff0b0b","pubkey":"f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9","created_at":123,"kind":1,"tags":[],"content":"hello","sig":"2d5ebb7dc3a294651cbe7ff4d3fe01819ce4ec5f1676e1af0133196cb8a66981ec1ae0a2c01fc6684241556dba3d54ab778ca4ce9c2e0379eb300d0a2f657643"})";
  assert(nostr_event_id(signed_event) ==
         "da67626634b798564d957125c4105766d6a681ff62eb48a87f5ec4e819ff0b0b");
#if XANATOO_NOSTR_VERIFY_OPENSSL
  assert(nostr_verify_event(signed_event) == NostrVerification::SignatureValid);
#else
  assert(nostr_verify_event(signed_event) == NostrVerification::HashOnly);
#endif
  std::string altered = signed_event;
  altered.replace(altered.find("hello"), 5, "hallo");
  assert(nostr_verify_event(altered) == NostrVerification::Invalid);
  const std::string pubkey =
      "F9308A019258C31049344F85F89D5229B531C845836F99B08601F113BCE036F9";
  const std::string zero(64, '0');
  const std::string valid =
      "E907831F80848D1069A5371B402410364BDF1C5F8307B0084C55F1CE2DCA8215"
      "25F66A4A85EA8B71E482A74F382D2CE5EBEEE8FDB2172F477DF4900D310536C0";
#if XANATOO_NOSTR_VERIFY_OPENSSL
  assert(nostr_verify_bip340(pubkey, zero, valid));  // vector 0
  assert(!nostr_verify_bip340(pubkey, zero, zero + zero));
  assert(!nostr_verify_bip340(std::string(64, 'f'), zero, valid));
  // Official vector 6: R has odd Y.
  assert(!nostr_verify_bip340(
      "DFF1D77F2A671C5F36183726DB2341BE58FEAE1DA2DECED843240F7B502BA659",
      "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
      "FFF97BD5755EEEA420453A14355235D382F6472F8568A18B2F057A1460297556"
      "3CC27944640AC607CD107AE10923D9EF7A73C643E166BE5EBEAFA34B1AC553E2"));
#else
  assert(!nostr_verify_bip340(pubkey, zero, valid));
#endif

  const std::string raw = "{\"id\":\"" + zero + "\",\"pubkey\":\"" +
      std::string(64, 'a') + "\",\"created_at\":123,\"kind\":1,"
      "\"content\":\"hello\\nworld\",\"tags\":[[\"p\",\"bob\"],[\"t\",\"C++\"]],"
      "\"sig\":\"" + zero + zero + "\"}";
  const auto id = nostr_event_id(raw);
  assert(id && id->size() == 64);
  // SHA-256 of this independently serialized NIP-01 array, checked against
  // sha256sum (not generated using nostr_event_id itself).
  const std::string simple = "{\"pubkey\":\"" + std::string(64, 'a') +
      "\",\"created_at\":1,\"kind\":1,\"tags\":[],\"content\":\"hello\"}";
  assert(nostr_event_id(simple) ==
         "f67935eddb3744c8798809e3758dcbb80dd0d04400842506030ada9680bfdf5a");
  std::string populated = raw;
  populated.replace(populated.find(zero), 64, *id);
#if XANATOO_NOSTR_VERIFY_OPENSSL
  assert(nostr_verify_event(populated) == NostrVerification::Invalid);
#else
  assert(nostr_verify_event(populated) == NostrVerification::HashOnly);
#endif
  populated.replace(populated.find(*id), 64, zero);
  assert(nostr_verify_event(populated) == NostrVerification::Invalid);
  assert(!nostr_event_id("{\"pubkey\":\"bad\",\"tags\":[]}"));
  assert(!nostr_event_id("{\"pubkey\":\"" + std::string(64, 'a') +
                          "\",\"created_at\":1,\"kind\":1,\"content\":\"x\","
                          "\"tags\":[[\"t\",9]]}"));
  assert(nostr_event_id("{\"pubkey\":\"" + std::string(64, 'a') +
                        "\",\"created_at\":1,\"kind\":1,\"content\":\"tags\",\"tags\":[]}"));
  assert(!nostr_event_id("{\"pubkey\":\"" + std::string(64, 'a') +
                          "\",\"created_at\":1.5,\"kind\":1,\"content\":\"x\",\"tags\":[]}"));
  std::cout << "NIP-01/BIP-340 verifier smoke: PASS\n";
}
