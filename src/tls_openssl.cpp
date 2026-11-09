// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/tls.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <mutex>

namespace xanatoo {

namespace {

// Pages are fetched on worker threads, so guard the one-time init.
void init_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
  });
}

}  // namespace

struct TlsStream::Impl {
  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  int fd = -1;
};

TlsStream::TlsStream() : impl_(std::make_unique<Impl>()) {}
TlsStream::~TlsStream() { close(); }

const char* TlsStream::backend() { return "OpenSSL"; }

std::string TlsStream::version() {
  return std::string(OpenSSL_version(OPENSSL_VERSION)) +
         " (built against " OPENSSL_VERSION_TEXT ")";
}

bool TlsStream::connect(int fd, const std::string& host, std::string* err,
                        bool verify_peer) {
  close();
  impl_->fd = fd;
  init_once();
  impl_->ctx = SSL_CTX_new(TLS_client_method());
  if (!impl_->ctx) {
    if (err) *err = "SSL_CTX_new failed";
    close();
    return false;
  }
  SSL_CTX_set_min_proto_version(impl_->ctx, TLS1_2_VERSION);
  if (verify_peer) {
    if (SSL_CTX_set_default_verify_paths(impl_->ctx) != 1) {
      if (err) *err = "no system TLS trust store";
      close();
      return false;
    }
    SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_PEER, nullptr);
  } else {
    SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_NONE, nullptr);
  }
  impl_->ssl = SSL_new(impl_->ctx);
  if (!impl_->ssl) {
    if (err) *err = "SSL_new failed";
    close();
    return false;
  }
  SSL_set_fd(impl_->ssl, fd);
  SSL_set_tlsext_host_name(impl_->ssl, host.c_str());
  if (verify_peer) {
    unsigned char ipbuf[16];
    const bool ip = inet_pton(AF_INET, host.c_str(), ipbuf) == 1 ||
                    inet_pton(AF_INET6, host.c_str(), ipbuf) == 1;
    const int rc = ip ? X509_VERIFY_PARAM_set1_ip_asc(
                            SSL_get0_param(impl_->ssl), host.c_str()) :
                        SSL_set1_host(impl_->ssl, host.c_str());
    if (rc != 1) {
      if (err) *err = "failed to set TLS certificate identity check";
      close();
      return false;
    }
  }
  const int rc = SSL_connect(impl_->ssl);
  if (rc != 1) {
    if (err)
      *err = "TLS handshake failed (" +
             std::to_string(SSL_get_error(impl_->ssl, rc)) + ") " + host +
             (verify_peer ? "; certificate result " +
                 std::to_string(SSL_get_verify_result(impl_->ssl)) : "");
    close();
    return false;
  }
  if (verify_peer && SSL_get_verify_result(impl_->ssl) != X509_V_OK) {
    if (err) *err = "TLS certificate verification failed for " + host;
    close();
    return false;
  }
  return true;
}

bool TlsStream::connected() const { return impl_->ssl != nullptr; }

bool TlsStream::write_all(const void* data, size_t n) {
  if (!impl_->ssl) return false;
  const char* p = static_cast<const char*>(data);
  while (n) {
    const int r = SSL_write(impl_->ssl, p, static_cast<int>(n));
    if (r <= 0) return false;
    p += r;
    n -= static_cast<size_t>(r);
  }
  return true;
}

int TlsStream::read(void* buf, size_t n) {
  if (!impl_->ssl) return -1;
  const int r = SSL_read(impl_->ssl, buf, static_cast<int>(n));
  if (r > 0) return r;
  return SSL_get_error(impl_->ssl, r) == SSL_ERROR_ZERO_RETURN ? 0 : -1;
}

bool TlsStream::pending() const {
  return impl_->ssl && SSL_pending(impl_->ssl) > 0;
}

void TlsStream::close() {
  if (impl_->ssl) {
    SSL_shutdown(impl_->ssl);
    SSL_free(impl_->ssl);
    impl_->ssl = nullptr;
  }
  if (impl_->ctx) {
    SSL_CTX_free(impl_->ctx);
    impl_->ctx = nullptr;
  }
  if (impl_->fd >= 0) {
    ::close(impl_->fd);
    impl_->fd = -1;
  }
}

}  // namespace xanatoo
