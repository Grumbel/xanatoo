// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Minimal TLS client stream for the network clients (HTTPS, Gemini, Nostr
// WSS). Backends: tls_openssl.cpp (desktop default) or tls_mbedtls.cpp
// (Android; -DXANATOO_TLS=mbedtls on desktop), chosen by CMake / ndk-build.
//
// TLS 1.2+, SNI = host. The peer certificate is NOT verified (Gemini relies
// on self-signed certificates; HTTPS / WSS verification is still to do).

#include <cstddef>
#include <memory>
#include <string>

namespace xanatoo {

class TlsStream {
 public:
  TlsStream();
  ~TlsStream();
  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  // Handshake over the connected TCP socket `fd`. The stream owns `fd` from
  // here on (closed by close() / the destructor, also when this fails).
  bool connect(int fd, const std::string& host, std::string* err = nullptr);
  bool connected() const;

  // Whole buffer; false on error.
  bool write_all(const void* data, size_t n);
  // Up to n bytes: > 0 read, 0 = closed by peer, < 0 = error / timeout
  // (the socket's SO_RCVTIMEO).
  int read(void* buf, size_t n);
  // Decrypted bytes buffered inside TLS (readable without blocking).
  bool pending() const;

  void close();

  // Name of the compiled-in backend ("OpenSSL", "Mbed TLS").
  static const char* backend();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xanatoo
