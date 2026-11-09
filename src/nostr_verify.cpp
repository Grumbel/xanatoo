// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// NIP-01 canonical event hashes and BIP-340 Schnorr verification.

#include "xanatoo/nostr_verify.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#ifndef XANATOO_NOSTR_VERIFY_OPENSSL
#define XANATOO_NOSTR_VERIFY_OPENSSL 0
#endif
#if XANATOO_NOSTR_VERIFY_OPENSSL
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#endif

namespace xanatoo {
namespace {

bool hex_string(std::string_view s, size_t length) {
  return s.size() == length &&
      std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
      });
}

unsigned char hex_digit(char c) {
  if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
  return static_cast<unsigned char>(c - 'A' + 10);
}

std::vector<unsigned char> from_hex(std::string_view h) {
  std::vector<unsigned char> out;
  out.reserve(h.size() / 2);
  for (size_t i = 0; i + 1 < h.size(); i += 2)
    out.push_back(static_cast<unsigned char>((hex_digit(h[i]) << 4) | hex_digit(h[i + 1])));
  return out;
}

void skip_ws(std::string_view s, size_t& i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

// Read a JSON string, retaining Unicode code points and their order.
bool json_string(std::string_view s, size_t& i, std::string& out) {
  if (i >= s.size() || s[i] != '"') return false;
  const size_t start = ++i;
  while (i < s.size()) {
    if (s[i] == '\\') {
      if (i + 1 >= s.size()) return false;
      const char escape = s[i + 1];
      if (escape == 'u') {
        if (i + 5 >= s.size()) return false;
        for (size_t n = i + 2; n < i + 6; ++n)
          if (!std::isxdigit(static_cast<unsigned char>(s[n]))) return false;
        i += 6;
        continue;
      }
      if (escape != '"' && escape != '\\' && escape != '/' &&
          escape != 'b' && escape != 'f' && escape != 'n' &&
          escape != 'r' && escape != 't') return false;
      i += 2;
    } else if (s[i] == '"') {
      out = json_unescape(s.substr(start, i - start));
      ++i;
      return true;
    } else {
      if (static_cast<unsigned char>(s[i]) < 0x20) return false;
      ++i;
    }
  }
  return false;
}

bool top_field(std::string_view top, std::string_view key, bool is_string) {
  const std::string pattern = "\"" + std::string(key) + "\"";
  size_t p = 0, value = std::string_view::npos;
  while ((p = top.find(pattern, p)) != std::string_view::npos) {
    size_t prior = p;
    while (prior > 0 && std::isspace(static_cast<unsigned char>(top[prior - 1]))) --prior;
    size_t after = p + pattern.size();
    skip_ws(top, after);
    if (prior > 0 && (top[prior - 1] == '{' || top[prior - 1] == ',') &&
        after < top.size() && top[after] == ':') {
      if (value != std::string_view::npos) return false;
      value = after + 1;
    }
    p += pattern.size();
  }
  if (value == std::string_view::npos) return false;
  p = value;
  skip_ws(top, p);
  if (is_string) {
    std::string ignored;
    return json_string(top, p, ignored);
  }
  const size_t start = p;
  while (p < top.size() && std::isdigit(static_cast<unsigned char>(top[p]))) ++p;
  if (p == start || (p < top.size() && top[p] != ',' && top[p] != '}' &&
                     !std::isspace(static_cast<unsigned char>(top[p]))))
    return false;
  return true;
}

// NIP-01 tags are arrays of arrays containing only strings. Reject malformed
// arrays instead of hashing an incomplete interpretation of a relay message.
bool json_tags(std::string_view json, std::vector<std::vector<std::string>>& out) {
  size_t p = std::string_view::npos, search = 0;
  while ((search = json.find("\"tags\"", search)) != std::string_view::npos) {
    size_t prior = search;
    while (prior > 0 && std::isspace(static_cast<unsigned char>(json[prior - 1]))) --prior;
    size_t after = search + 6;
    skip_ws(json, after);
    if (prior > 0 && (json[prior - 1] == '{' || json[prior - 1] == ',') &&
        after < json.size() && json[after] == ':') {
      if (p != std::string_view::npos) return false;
      p = after;
    }
    search += 6;
  }
  if (p == std::string_view::npos) return false;
  skip_ws(json, p);
  if (p >= json.size() || json[p++] != ':') return false;
  skip_ws(json, p);
  if (p >= json.size() || json[p++] != '[') return false;
  skip_ws(json, p);
  if (p < json.size() && json[p] == ']') return true;
  for (;;) {
    if (p >= json.size() || json[p++] != '[') return false;
    std::vector<std::string> tag;
    skip_ws(json, p);
    if (p < json.size() && json[p] != ']') {
      for (;;) {
        std::string entry;
        if (!json_string(json, p, entry)) return false;
        tag.push_back(std::move(entry));
        skip_ws(json, p);
        if (p >= json.size()) return false;
        if (json[p] == ']') break;
        if (json[p++] != ',') return false;
        skip_ws(json, p);
      }
    }
    if (p >= json.size() || json[p++] != ']') return false;
    out.push_back(std::move(tag));
    skip_ws(json, p);
    if (p >= json.size()) return false;
    if (json[p] == ']') return true;
    if (json[p++] != ',') return false;
    skip_ws(json, p);
  }
}

void append_json_string(std::string& out, std::string_view value) {
  constexpr char hexd[] = "0123456789abcdef";
  out.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(hexd[c >> 4]);
          out.push_back(hexd[c & 15]);
        } else out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('"');
}

}  // namespace

std::optional<std::string> nostr_event_id(std::string_view event_json) {
  const std::string top = json_top_level(event_json);
  const std::string pubkey = json_str_field(top, "pubkey");
  if (!hex_string(pubkey, 64) || pubkey != ascii_lower(pubkey) ||
      !top_field(top, "pubkey", true) ||
      !top_field(top, "content", true) ||
      !top_field(top, "created_at", false) ||
      !top_field(top, "kind", false))
    return std::nullopt;
  const int64_t created_at = json_int_field(top, "created_at");
  const int64_t kind = json_int_field(top, "kind");
  if (created_at < 0 || kind < 0 || kind > 65535) return std::nullopt;
  const std::string content = json_str_field(top, "content");
  std::vector<std::vector<std::string>> tags;
  if (!json_tags(event_json, tags)) return std::nullopt;

  std::string canonical = "[0,";
  append_json_string(canonical, pubkey);
  canonical += "," + std::to_string(created_at) + "," + std::to_string(kind) + ",[";
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i) canonical.push_back(',');
    canonical.push_back('[');
    for (size_t j = 0; j < tags[i].size(); ++j) {
      if (j) canonical.push_back(',');
      append_json_string(canonical, tags[i][j]);
    }
    canonical.push_back(']');
  }
  canonical += "],";
  append_json_string(canonical, content);
  canonical.push_back(']');
  return sha256_hex(canonical);
}

bool nostr_verify_bip340(std::string_view pubkey_hex,
                         std::string_view message_hex,
                         std::string_view sig_hex) {
#if XANATOO_NOSTR_VERIFY_OPENSSL
  if (!hex_string(pubkey_hex, 64) || !hex_string(message_hex, 64) ||
      !hex_string(sig_hex, 128)) return false;
  const auto pk = from_hex(pubkey_hex);
  const auto msg = from_hex(message_hex);
  const auto sig = from_hex(sig_hex);
  auto group = std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)>(
      EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
  auto ctx = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>(BN_CTX_new(), BN_CTX_free);
  if (!group || !ctx) return false;
  auto bn = [](const unsigned char* p) {
    return std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_bin2bn(p, 32, nullptr), BN_free);
  };
  auto r = bn(sig.data()), s = bn(sig.data() + 32);
  auto order = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto prime = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto e = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto ne = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto x = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto y = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
  auto point = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>(EC_POINT_new(group.get()), EC_POINT_free);
  auto result = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>(EC_POINT_new(group.get()), EC_POINT_free);
  if (!r || !s || !order || !prime || !e || !ne || !x || !y || !point || !result ||
      !EC_GROUP_get_order(group.get(), order.get(), ctx.get()) ||
      !EC_GROUP_get_curve(group.get(), prime.get(), nullptr, nullptr, ctx.get()) ||
      BN_cmp(r.get(), prime.get()) >= 0 || BN_cmp(s.get(), order.get()) >= 0)
    return false;
  std::array<unsigned char, 33> compressed{};
  compressed[0] = 0x02;  // BIP-340 lift_x chooses the even Y point.
  std::copy(pk.begin(), pk.end(), compressed.begin() + 1);
  if (!EC_POINT_oct2point(group.get(), point.get(), compressed.data(), compressed.size(), ctx.get()))
    return false;

  const std::string tag = "BIP0340/challenge";
  const auto tagged = from_hex(sha256_hex(tag));
  std::string challenge;
  challenge.reserve(160);
  challenge.append(reinterpret_cast<const char*>(tagged.data()), tagged.size());
  challenge.append(reinterpret_cast<const char*>(tagged.data()), tagged.size());
  challenge.append(reinterpret_cast<const char*>(sig.data()), 32);
  challenge.append(reinterpret_cast<const char*>(pk.data()), pk.size());
  challenge.append(reinterpret_cast<const char*>(msg.data()), msg.size());
  const auto hash = from_hex(sha256_hex(challenge));
  if (!BN_bin2bn(hash.data(), 32, e.get()) ||
      !BN_mod(e.get(), e.get(), order.get(), ctx.get()) ||
      !BN_mod_sub(ne.get(), order.get(), e.get(), order.get(), ctx.get()) ||
      !EC_POINT_mul(group.get(), result.get(), s.get(), point.get(), ne.get(), ctx.get()) ||
      EC_POINT_is_at_infinity(group.get(), result.get()) != 0 ||
      !EC_POINT_get_affine_coordinates(group.get(), result.get(), x.get(), y.get(), ctx.get()))
    return false;
  return !BN_is_odd(y.get()) && BN_cmp(x.get(), r.get()) == 0;
#else
  (void)pubkey_hex; (void)message_hex; (void)sig_hex;
  return false;
#endif
}

NostrVerification nostr_verify_event(std::string_view json) {
  auto id = nostr_event_id(json);
  if (!id) return NostrVerification::Invalid;
  const std::string top = json_top_level(json);
  const std::string declared = json_str_field(top, "id");
  const std::string pubkey = json_str_field(top, "pubkey");
  const std::string signature = json_str_field(top, "sig");
  if (!hex_string(declared, 64) || ascii_lower(declared) != *id ||
      !hex_string(signature, 128)) return NostrVerification::Invalid;
#if XANATOO_NOSTR_VERIFY_OPENSSL
  if (!nostr_verify_bip340(pubkey, *id, signature)) return NostrVerification::Invalid;
  return NostrVerification::SignatureValid;
#else
  return NostrVerification::HashOnly;
#endif
}

}  // namespace xanatoo
