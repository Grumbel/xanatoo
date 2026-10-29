// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Small DEFLATE decoder (RFC 1951) with the gzip (RFC 1952) and zlib
// (RFC 1950) wrappers, after zlib's "puff": no dependency, so it builds the
// same on desktop and Android. Used for *.xml.gz sitemaps.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Raw DEFLATE stream; `consumed` (optional) gets the bytes read. nullopt on
// corrupt input or when the output would exceed `max_out`.
std::optional<std::string> inflate_raw(std::string_view in,
                                       size_t max_out = 64u << 20,
                                       size_t* consumed = nullptr);

// gzip member (1f 8b …); the CRC is checked, the size modulo 2^32.
std::optional<std::string> gunzip(std::string_view in,
                                  size_t max_out = 64u << 20);

// zlib stream (78 xx …); the Adler-32 is checked.
std::optional<std::string> zlib_inflate(std::string_view in,
                                        size_t max_out = 64u << 20);

inline bool is_gzip(std::string_view in) {
  return in.size() >= 2 && static_cast<unsigned char>(in[0]) == 0x1f &&
         static_cast<unsigned char>(in[1]) == 0x8b;
}

}  // namespace xanatoo
