// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Do not follow relay addresses learned from untrusted Nostr events into
// localhost, local networks, or special-purpose IP ranges (including DNS
// responses that resolve an otherwise public-looking hostname locally).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace xanatoo {

inline bool public_ip_address(const sockaddr* addr) {
  if (addr->sa_family == AF_INET) {
    const auto ip = ntohl(reinterpret_cast<const sockaddr_in*>(addr)->sin_addr.s_addr);
    const unsigned a = ip >> 24, b = (ip >> 16) & 255;
    return !(a == 0 || a == 10 || a == 127 || a >= 224 ||
             (a == 100 && b >= 64 && b <= 127) ||
             (a == 169 && b == 254) ||
             (a == 172 && b >= 16 && b <= 31) ||
             (a == 192 && (b == 0 || b == 168)) ||
             (a == 198 && (b == 18 || b == 19)));
  }
  if (addr->sa_family == AF_INET6) {
    const auto* ip = &reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr;
    return !(IN6_IS_ADDR_LOOPBACK(ip) || IN6_IS_ADDR_UNSPECIFIED(ip) ||
             IN6_IS_ADDR_LINKLOCAL(ip) || IN6_IS_ADDR_MULTICAST(ip) ||
             IN6_IS_ADDR_V4MAPPED(ip) || (ip->s6_addr[0] & 0xfe) == 0xfc);
  }
  return false;
}

}  // namespace xanatoo
