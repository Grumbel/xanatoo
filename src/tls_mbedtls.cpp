// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// TlsStream over Mbed TLS 3.x (Android; desktop with -DXANATOO_TLS=mbedtls).
// Uses the caller's socket through send/recv callbacks so its SO_RCVTIMEO /
// SO_SNDTIMEO keep working.

#include "xanatoo/tls.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/version.h>
#include <psa/crypto.h>

#include <cerrno>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace xanatoo {

namespace {

#ifdef MBEDTLS_ENTROPY_HARDWARE_ALT
// Entropy source when the platform one is disabled (Android build, see
// mk/android/mbedtls/Android.mk): /dev/urandom, which never blocks, unlike
// getrandom() on old kernels.
extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, size_t len,
                                     size_t* olen) {
  *olen = 0;
  FILE* f = std::fopen("/dev/urandom", "rb");
  if (!f) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  *olen = std::fread(output, 1, len, f);
  std::fclose(f);
  return *olen == len ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}
#endif

int send_cb(void* ctx, const unsigned char* buf, size_t len) {
  const int fd = *static_cast<int*>(ctx);
  const ssize_t n = ::send(fd, buf, len, MSG_NOSIGNAL);
  if (n >= 0) return static_cast<int>(n);
  if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_TIMEOUT;
  return MBEDTLS_ERR_NET_SEND_FAILED;
}

int recv_cb(void* ctx, unsigned char* buf, size_t len) {
  const int fd = *static_cast<int*>(ctx);
  const ssize_t n = ::recv(fd, buf, len, 0);
  if (n >= 0) return static_cast<int>(n);  // 0 = peer closed
  if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
  // SO_RCVTIMEO expired: fail like OpenSSL does instead of spinning.
  if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_TIMEOUT;
  return MBEDTLS_ERR_NET_RECV_FAILED;
}

std::string error_text(int rc) {
  char buf[160];
  mbedtls_strerror(rc, buf, sizeof(buf));
  return buf;
}

}  // namespace

struct TlsStream::Impl {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_ssl_config conf;
  mbedtls_ssl_context ssl;
  int fd = -1;
  bool live = false;  // contexts initialised (free them in close())
  bool ok = false;    // handshake done
};

TlsStream::TlsStream() : impl_(std::make_unique<Impl>()) {}
TlsStream::~TlsStream() { close(); }

const char* TlsStream::backend() { return "Mbed TLS"; }

// Linked statically, so the runtime version is the built one.
std::string TlsStream::version() { return MBEDTLS_VERSION_STRING_FULL; }

bool TlsStream::connect(int fd, const std::string& host, std::string* err) {
  close();
  Impl& m = *impl_;
  m.fd = fd;
  auto fail = [&](const std::string& what, int rc) {
    if (err) *err = what + ": " + error_text(rc) + " (" + host + ")";
    close();
    return false;
  };
  // TLS 1.3 in Mbed TLS 3.x runs on PSA crypto; idempotent.
  if (const psa_status_t ps = psa_crypto_init(); ps != PSA_SUCCESS) {
    if (err) *err = "psa_crypto_init failed (" + std::to_string(ps) + ")";
    close();
    return false;
  }
  mbedtls_entropy_init(&m.entropy);
  mbedtls_ctr_drbg_init(&m.drbg);
  mbedtls_ssl_config_init(&m.conf);
  mbedtls_ssl_init(&m.ssl);
  m.live = true;

  static const char pers[] = "xanatoo-tls";
  int rc = mbedtls_ctr_drbg_seed(&m.drbg, mbedtls_entropy_func, &m.entropy,
                                 reinterpret_cast<const unsigned char*>(pers),
                                 sizeof(pers) - 1);
  if (rc != 0) return fail("TLS RNG seed failed", rc);
  rc = mbedtls_ssl_config_defaults(&m.conf, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM,
                                   MBEDTLS_SSL_PRESET_DEFAULT);
  if (rc != 0) return fail("TLS config failed", rc);
  mbedtls_ssl_conf_min_tls_version(&m.conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_authmode(&m.conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&m.conf, mbedtls_ctr_drbg_random, &m.drbg);
  rc = mbedtls_ssl_setup(&m.ssl, &m.conf);
  if (rc != 0) return fail("TLS setup failed", rc);
  rc = mbedtls_ssl_set_hostname(&m.ssl, host.c_str());  // SNI
  if (rc != 0) return fail("TLS SNI failed", rc);
  mbedtls_ssl_set_bio(&m.ssl, &m.fd, send_cb, recv_cb, nullptr);

  while ((rc = mbedtls_ssl_handshake(&m.ssl)) != 0) {
    if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
      return fail("TLS handshake failed", rc);
  }
  m.ok = true;
  return true;
}

bool TlsStream::connected() const { return impl_->ok; }

bool TlsStream::write_all(const void* data, size_t n) {
  if (!impl_->ok) return false;
  const unsigned char* p = static_cast<const unsigned char*>(data);
  while (n) {
    const int r = mbedtls_ssl_write(&impl_->ssl, p, n);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
      continue;
    if (r <= 0) return false;
    p += r;
    n -= static_cast<size_t>(r);
  }
  return true;
}

int TlsStream::read(void* buf, size_t n) {
  if (!impl_->ok) return -1;
  for (;;) {
    const int r =
        mbedtls_ssl_read(&impl_->ssl, static_cast<unsigned char*>(buf), n);
    if (r > 0) return r;
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    // TLS 1.3 post-handshake messages: not data, read on.
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
      continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
    return -1;
  }
}

bool TlsStream::pending() const {
  return impl_->ok && mbedtls_ssl_get_bytes_avail(&impl_->ssl) > 0;
}

void TlsStream::close() {
  Impl& m = *impl_;
  if (m.ok) mbedtls_ssl_close_notify(&m.ssl);
  if (m.live) {
    mbedtls_ssl_free(&m.ssl);
    mbedtls_ssl_config_free(&m.conf);
    mbedtls_ctr_drbg_free(&m.drbg);
    mbedtls_entropy_free(&m.entropy);
  }
  m.live = m.ok = false;
  if (m.fd >= 0) {
    ::close(m.fd);
    m.fd = -1;
  }
}

}  // namespace xanatoo
