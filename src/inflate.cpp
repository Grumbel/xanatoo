// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/inflate.hpp"

#include <array>
#include <cstdint>

namespace xanatoo {

namespace {

constexpr int kMaxBits = 15;

// Canonical Huffman code: symbol counts per length, symbols by code.
struct Huffman {
  std::array<uint16_t, kMaxBits + 1> count{};
  std::array<uint16_t, 288> symbol{};
};

struct Bits {
  std::string_view in;
  size_t pos = 0;
  uint32_t buf = 0;
  int cnt = 0;
  bool bad = false;

  int need(int n) {
    uint32_t v = buf;
    while (cnt < n) {
      if (pos >= in.size()) {
        bad = true;
        return 0;
      }
      v |= static_cast<uint32_t>(static_cast<unsigned char>(in[pos++])) << cnt;
      cnt += 8;
    }
    buf = v >> n;
    cnt -= n;
    return static_cast<int>(v & ((1u << n) - 1));
  }
};

// Builds `h` from code lengths; false for an over-subscribed set (an
// incomplete one is allowed, as in puff, for single-code distance tables).
bool build(Huffman& h, const uint16_t* lengths, int n) {
  h.count.fill(0);
  for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
  if (h.count[0] == n) return true;  // no codes: fine until one is used
  int left = 1;
  for (int len = 1; len <= kMaxBits; ++len) {
    left <<= 1;
    left -= h.count[static_cast<size_t>(len)];
    if (left < 0) return false;
  }
  std::array<uint16_t, kMaxBits + 1> offs{};
  for (int len = 1; len < kMaxBits; ++len)
    offs[static_cast<size_t>(len + 1)] = static_cast<uint16_t>(
        offs[static_cast<size_t>(len)] + h.count[static_cast<size_t>(len)]);
  for (int s = 0; s < n; ++s)
    if (lengths[s] != 0)
      h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
  return true;
}

int decode(Bits& b, const Huffman& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= kMaxBits; ++len) {
    code |= b.need(1);
    if (b.bad) return -1;
    const int count = h.count[static_cast<size_t>(len)];
    if (code - count < first)
      return h.symbol[static_cast<size_t>(index + (code - first))];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

constexpr std::array<uint16_t, 29> kLenBase{
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLenExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                            1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                            4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                             4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                             9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits& b, std::string& out, size_t max_out, const Huffman& lit,
           const Huffman& dist) {
  for (;;) {
    int sym = decode(b, lit);
    if (sym < 0) return false;
    if (sym < 256) {
      if (out.size() >= max_out) return false;
      out.push_back(static_cast<char>(sym));
    } else if (sym == 256) {
      return true;
    } else {
      sym -= 257;
      if (sym >= 29) return false;
      const size_t len = kLenBase[static_cast<size_t>(sym)] +
                         static_cast<size_t>(b.need(kLenExtra[static_cast<size_t>(sym)]));
      const int ds = decode(b, dist);
      if (ds < 0 || ds >= 30) return false;
      const size_t d = kDistBase[static_cast<size_t>(ds)] +
                       static_cast<size_t>(b.need(kDistExtra[static_cast<size_t>(ds)]));
      if (b.bad || d > out.size() || out.size() + len > max_out) return false;
      for (size_t i = 0; i < len; ++i) out.push_back(out[out.size() - d]);
    }
  }
}

bool fixed_block(Bits& b, std::string& out, size_t max_out) {
  static const auto tables = [] {
    std::pair<Huffman, Huffman> t;
    std::array<uint16_t, 288> l{};
    int s = 0;
    for (; s < 144; ++s) l[static_cast<size_t>(s)] = 8;
    for (; s < 256; ++s) l[static_cast<size_t>(s)] = 9;
    for (; s < 280; ++s) l[static_cast<size_t>(s)] = 7;
    for (; s < 288; ++s) l[static_cast<size_t>(s)] = 8;
    build(t.first, l.data(), 288);
    std::array<uint16_t, 30> d{};
    d.fill(5);
    build(t.second, d.data(), 30);
    return t;
  }();
  return codes(b, out, max_out, tables.first, tables.second);
}

bool dynamic_block(Bits& b, std::string& out, size_t max_out) {
  const int nlen = b.need(5) + 257;
  const int ndist = b.need(5) + 1;
  const int ncode = b.need(4) + 4;
  if (b.bad || nlen > 286 || ndist > 30) return false;
  static constexpr std::array<uint8_t, 19> kOrder{
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  std::array<uint16_t, 320> lengths{};
  for (int i = 0; i < ncode; ++i)
    lengths[kOrder[static_cast<size_t>(i)]] = static_cast<uint16_t>(b.need(3));
  if (b.bad) return false;
  Huffman lencode;
  if (!build(lencode, lengths.data(), 19)) return false;
  int i = 0;
  while (i < nlen + ndist) {
    int sym = decode(b, lencode);
    if (sym < 0) return false;
    if (sym < 16) {
      lengths[static_cast<size_t>(i++)] = static_cast<uint16_t>(sym);
      continue;
    }
    uint16_t len = 0;
    int rep = 0;
    if (sym == 16) {
      if (i == 0) return false;
      len = lengths[static_cast<size_t>(i - 1)];
      rep = 3 + b.need(2);
    } else if (sym == 17) {
      rep = 3 + b.need(3);
    } else {
      rep = 11 + b.need(7);
    }
    if (b.bad || i + rep > nlen + ndist) return false;
    while (rep-- > 0) lengths[static_cast<size_t>(i++)] = len;
  }
  if (lengths[256] == 0) return false;  // no end-of-block code
  Huffman lit, dist;
  if (!build(lit, lengths.data(), nlen)) return false;
  if (!build(dist, lengths.data() + nlen, ndist)) return false;
  return codes(b, out, max_out, lit, dist);
}

bool stored_block(Bits& b, std::string& out, size_t max_out) {
  b.buf = 0;  // to a byte boundary
  b.cnt = 0;
  if (b.pos + 4 > b.in.size()) return false;
  auto byte = [&](size_t k) {
    return static_cast<unsigned>(static_cast<unsigned char>(b.in[b.pos + k]));
  };
  const unsigned len = byte(0) | (byte(1) << 8);
  const unsigned nlen = byte(2) | (byte(3) << 8);
  if (len != (~nlen & 0xffffu)) return false;
  b.pos += 4;
  if (b.pos + len > b.in.size() || out.size() + len > max_out) return false;
  out.append(b.in.substr(b.pos, len));
  b.pos += len;
  return true;
}

uint32_t crc32(std::string_view s) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  uint32_t c = 0xffffffffu;
  for (char ch : s)
    c = table[(c ^ static_cast<unsigned char>(ch)) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

uint32_t adler32(std::string_view s) {
  uint32_t a = 1, b = 0;
  for (char ch : s) {
    a = (a + static_cast<unsigned char>(ch)) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

uint32_t le32(std::string_view s, size_t at) {
  uint32_t v = 0;
  for (int k = 3; k >= 0; --k)
    v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<size_t>(k)]);
  return v;
}

}  // namespace

std::optional<std::string> inflate_raw(std::string_view in, size_t max_out,
                                       size_t* consumed) {
  Bits b;
  b.in = in;
  std::string out;
  for (;;) {
    const int last = b.need(1);
    const int type = b.need(2);
    if (b.bad) return std::nullopt;
    bool ok = false;
    switch (type) {
      case 0:
        ok = stored_block(b, out, max_out);
        break;
      case 1:
        ok = fixed_block(b, out, max_out);
        break;
      case 2:
        ok = dynamic_block(b, out, max_out);
        break;
      default:
        break;
    }
    if (!ok || b.bad) return std::nullopt;
    if (last) break;
  }
  if (consumed) *consumed = b.pos;  // partial last byte counts as read
  return out;
}

std::optional<std::string> gunzip(std::string_view in, size_t max_out) {
  if (in.size() < 18 || !is_gzip(in) || in[2] != 8) return std::nullopt;
  const unsigned flags = static_cast<unsigned char>(in[3]);
  size_t pos = 10;
  if (flags & 4) {  // FEXTRA
    if (pos + 2 > in.size()) return std::nullopt;
    pos += 2 + (static_cast<unsigned char>(in[pos]) |
                (static_cast<size_t>(static_cast<unsigned char>(in[pos + 1])) << 8));
  }
  for (unsigned f : {8u, 16u}) {  // FNAME, FCOMMENT: zero-terminated
    if (!(flags & f)) continue;
    while (pos < in.size() && in[pos] != '\0') ++pos;
    ++pos;
  }
  if (flags & 2) pos += 2;  // FHCRC
  if (pos >= in.size()) return std::nullopt;
  size_t used = 0;
  auto out = inflate_raw(in.substr(pos), max_out, &used);
  if (!out) return std::nullopt;
  pos += used;
  if (pos + 8 > in.size()) return std::nullopt;
  if (le32(in, pos) != crc32(*out) ||
      le32(in, pos + 4) != static_cast<uint32_t>(out->size()))
    return std::nullopt;
  return out;
}

std::optional<std::string> zlib_inflate(std::string_view in, size_t max_out) {
  if (in.size() < 6) return std::nullopt;
  const unsigned cmf = static_cast<unsigned char>(in[0]);
  const unsigned flg = static_cast<unsigned char>(in[1]);
  if ((cmf & 0x0f) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20))
    return std::nullopt;
  size_t used = 0;
  auto out = inflate_raw(in.substr(2), max_out, &used);
  if (!out || 2 + used + 4 > in.size()) return std::nullopt;
  const size_t at = 2 + used;
  uint32_t want = 0;
  for (size_t k = 0; k < 4; ++k)
    want = (want << 8) | static_cast<unsigned char>(in[at + k]);
  if (want != adler32(*out)) return std::nullopt;
  return out;
}

}  // namespace xanatoo
