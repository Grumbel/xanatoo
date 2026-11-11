// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/tls.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/buffer.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <cstdlib>
#include <ctime>
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

// SSL_get1_peer_certificate was introduced in OpenSSL 3.0. The older
// SSL_get_peer_certificate has identical ownership semantics (the caller
// owns one reference), so both paths must be paired with X509_free().
X509* peer_certificate_owned(SSL* ssl) {
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3 && \
    !defined(LIBRESSL_VERSION_NUMBER)
  return SSL_get1_peer_certificate(ssl);
#else
  return SSL_get_peer_certificate(ssl);
#endif
}

std::string certificate_name(X509_NAME* name) {
  if (!name) return {};
  BIO* output = BIO_new(BIO_s_mem());
  if (!output) return {};
  std::string result;
  if (X509_NAME_print_ex(output, name, 0, XN_FLAG_RFC2253) >= 0) {
    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(output, &buffer);
    if (buffer && buffer->data) {
      const size_t size = std::min(static_cast<size_t>(buffer->length),
                                   size_t{1024});
      result.assign(buffer->data, size);
      if (size < buffer->length) result += "…";
    }
  }
  BIO_free(output);
  return result;
}

std::string certificate_time(const ASN1_TIME* time) {
  if (!time) return {};
  std::tm utc{};
  if (ASN1_TIME_to_tm(time, &utc) != 1) return {};
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &utc) == 0)
    return {};
  return buffer;
}

}  // namespace

struct TlsStream::Impl {
  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  int fd = -1;
  std::string last_error;
};

namespace {

// Turn the failed SSL call `r` into text: the OpenSSL error queue (reason
// such as "unexpected eof while reading"), else the socket's errno.
std::string describe_ssl_failure(SSL* ssl, int r, int saved_errno) {
  const int code = SSL_get_error(ssl, r);
  std::string out;
  switch (code) {
    case SSL_ERROR_ZERO_RETURN: return "closed by peer (TLS close_notify)";
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      out = "timed out waiting for data (SO_RCVTIMEO)"; break;
    case SSL_ERROR_SYSCALL:
      if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
        out = "timed out waiting for data (no bytes for 15 s)";
      else if (saved_errno)
        out = std::string("socket error: ") + std::strerror(saved_errno);
      else
        out = "connection closed by peer without TLS close_notify";
      break;
    case SSL_ERROR_SSL: out = "TLS protocol error"; break;
    default: out = "SSL_get_error=" + std::to_string(code); break;
  }
  unsigned long e;
  while ((e = ERR_get_error()) != 0) {
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    out += "; "; out += buf;
  }
  return out;
}

}  // namespace

const std::string& TlsStream::last_error() const { return impl_->last_error; }

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
    // OpenSSL's default-path lookup normally honors SSL_CERT_FILE, but some
    // packaged builds have a fixed CA store. Explicitly load the configured
    // bundle too, so Nix and test-local trust anchors work consistently.
    const char* ca_file = std::getenv("SSL_CERT_FILE");
    if (!ca_file || !*ca_file) ca_file = std::getenv("NIX_SSL_CERT_FILE");
    if (ca_file && *ca_file &&
        SSL_CTX_load_verify_locations(impl_->ctx, ca_file, nullptr) != 1) {
      if (err) *err = std::string("cannot load TLS CA bundle: ") + ca_file;
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
    if (err) {
      const int code = SSL_get_error(impl_->ssl, rc);
      const int saved_errno = errno;
      std::string msg = "TLS handshake with " + host + " failed";
      const long vr = verify_peer ? SSL_get_verify_result(impl_->ssl) : X509_V_OK;
      if (vr != X509_V_OK)
        msg += ": certificate rejected: " +
               std::string(X509_verify_cert_error_string(vr)) +
               " (X509 code " + std::to_string(vr) + ")";
      else if (code == SSL_ERROR_SYSCALL)
        msg += saved_errno == EAGAIN || saved_errno == EWOULDBLOCK
                   ? ": timed out"
                   : saved_errno ? std::string(": ") + std::strerror(saved_errno)
                                 : ": connection closed during handshake";
      unsigned long e;
      while ((e = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        msg += "; "; msg += buf;
      }
      *err = msg;
    }
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

std::optional<std::string> TlsStream::peer_certificate_sha256() const {
  if (!connected()) return std::nullopt;
  X509* cert = peer_certificate_owned(impl_->ssl);
  if (!cert) return std::nullopt;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  const bool hashed = X509_digest(cert, EVP_sha256(), digest, &size) == 1;
  X509_free(cert);
  if (!hashed || size != 32) return std::nullopt;
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (unsigned i = 0; i < size; ++i) {
    result += hex[digest[i] >> 4];
    result += hex[digest[i] & 15];
  }
  return result;
}

std::optional<TlsPeerCertificate> TlsStream::peer_certificate_details() const {
  if (!connected()) return std::nullopt;
  X509* cert = peer_certificate_owned(impl_->ssl);
  if (!cert) return std::nullopt;
  TlsPeerCertificate details;
  details.subject = certificate_name(X509_get_subject_name(cert));
  details.issuer = certificate_name(X509_get_issuer_name(cert));
  details.not_before = certificate_time(X509_get0_notBefore(cert));
  details.not_after = certificate_time(X509_get0_notAfter(cert));
  details.self_issued = X509_NAME_cmp(X509_get_subject_name(cert),
                                      X509_get_issuer_name(cert)) == 0;
  details.self_signed = false;
  if (details.self_issued) {
    EVP_PKEY* key = X509_get_pubkey(cert);
    if (key) {
      details.self_signed = X509_verify(cert, key) == 1;
      EVP_PKEY_free(key);
    } else {
      details.self_signed.reset();
    }
  }
  X509_free(cert);
  return details;
}

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
  ERR_clear_error();
  errno = 0;
  const int r = SSL_read(impl_->ssl, buf, static_cast<int>(n));
  if (r > 0) return r;
  const int saved = errno;
  const int code = SSL_get_error(impl_->ssl, r);
  if (code == SSL_ERROR_ZERO_RETURN) return 0;
  // Many servers (YouTube, ...) end a close-delimited HTTP body by closing
  // the TCP connection without a TLS close_notify. OpenSSL 3 reports that as
  // an SSL error; curl and browsers treat it as end-of-data. Callers that
  // know the expected length (Content-Length / chunked) still detect
  // truncation themselves.
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING  // OpenSSL >= 3.0
  if (code == SSL_ERROR_SSL &&
      ERR_GET_REASON(ERR_peek_error()) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
    impl_->last_error = "connection closed by peer without TLS close_notify";
    ERR_clear_error();
    return 0;
  }
#endif
  impl_->last_error = describe_ssl_failure(impl_->ssl, r, saved);
  return -1;
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
