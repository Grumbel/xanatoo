// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Minimal TLS client stream for the network clients (HTTPS, Gemini, Nostr
// WSS). Backends: tls_openssl.cpp (desktop default) or tls_mbedtls.cpp
// (Android; -DXANATOO_TLS=mbedtls on desktop), chosen by CMake / ndk-build.
//
// TLS 1.2+, SNI = host. Peer certificates and hostnames are verified by
// default. Gemini deliberately opts out of CA verification because it commonly
// uses self-signed certificates; it verifies a stored TOFU fingerprint instead.
// If no system CA store is available, verified connections fail closed.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace xanatoo {

// Human-readable information from the leaf certificate after a successful
// handshake. Dates are in UTC. `self_issued` only compares subject and issuer;
// `self_signed` additionally verifies the leaf's own signature when supported
// (nullopt means the backend could not determine this).
struct TlsPeerCertificate {
  std::string subject;
  std::string issuer;
  std::string not_before;
  std::string not_after;
  bool self_issued = false;
  std::optional<bool> self_signed;
};

class TlsStream {
 public:
  TlsStream();
  ~TlsStream();
  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  // Handshake over the connected TCP socket `fd`. The stream owns `fd` from
  // here on (closed by close() / the destructor, also when this fails).
  bool connect(int fd, const std::string& host, std::string* err = nullptr,
               bool verify_peer = true);
  bool connected() const;

  // SHA-256 of the peer's full DER leaf certificate (lowercase hex), after
  // a successful handshake. No certificate or hash error => nullopt.
  // Gemini checks this before sending any application data.
  std::optional<std::string> peer_certificate_sha256() const;

  // Certificate identity and validity for Gemini's explicit trust review.
  // This is descriptive information, not proof of CA or hostname trust.
  std::optional<TlsPeerCertificate> peer_certificate_details() const;

  // Whole buffer; false on error.
  bool write_all(const void* data, size_t n);
  // Up to n bytes: > 0 read, 0 = closed by peer, < 0 = error / timeout
  // (the socket's SO_RCVTIMEO).
  int read(void* buf, size_t n);
  // Why the last read() / write_all() failed (library error string, errno
  // text, "timed out", "connection reset" ...); empty after success.
  const std::string& last_error() const;
  // Decrypted bytes buffered inside TLS (readable without blocking).
  bool pending() const;

  void close();

  // Name of the compiled-in backend ("OpenSSL", "Mbed TLS").
  static const char* backend();
  // Library version at runtime, plus the built-against one where they can
  // differ ("OpenSSL 3.4.1 11 Feb 2025 (built against …)").
  static std::string version();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xanatoo
