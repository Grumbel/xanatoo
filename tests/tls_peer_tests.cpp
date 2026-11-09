// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Offline TLS verification regressions using a local in-memory TLS server.
// No external network, CA files, OpenSSL executable, or SDL required.

#include "xanatoo/tls.hpp"

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace {

void check(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}

template<typename T, void (*Deleter)(T*)>
using Owner = std::unique_ptr<T, decltype(Deleter)>;

struct TestIdentity {
  Owner<EVP_PKEY, EVP_PKEY_free> key{nullptr, EVP_PKEY_free};
  Owner<X509, X509_free> cert{nullptr, X509_free};

  TestIdentity() {
    Owner<EVP_PKEY_CTX, EVP_PKEY_CTX_free> generator(
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
    check(bool(generator), "create EC key generator");
    check(EVP_PKEY_keygen_init(generator.get()) == 1, "init EC key generator");
    check(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(generator.get(),
        NID_X9_62_prime256v1) == 1, "set EC curve");
    EVP_PKEY* generated = nullptr;
    check(EVP_PKEY_keygen(generator.get(), &generated) == 1, "generate EC key");
    key.reset(generated);
    cert.reset(X509_new());
    check(bool(cert), "create certificate");
    check(X509_set_version(cert.get(), 2) == 1, "set X509v3");
    check(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 42) == 1,
          "set certificate serial");
    check(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) != nullptr,
          "set validity start");
    check(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) != nullptr,
          "set validity end");
    check(X509_set_pubkey(cert.get(), key.get()) == 1, "set certificate public key");
    auto* subject = X509_get_subject_name(cert.get());
    check(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("relay.example"), -1, -1, 0) == 1,
        "set certificate common name");
    check(X509_set_issuer_name(cert.get(), subject) == 1, "set certificate issuer");
    Owner<X509_EXTENSION, X509_EXTENSION_free> san(
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name,
                             const_cast<char*>("DNS:relay.example")),
        X509_EXTENSION_free);
    check(bool(san) && X509_add_ext(cert.get(), san.get(), -1) == 1,
          "set certificate subjectAltName");
    check(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0,
          "sign test certificate");
  }
};

bool handshake(SSL_CTX* server_ctx, const std::string& host,
               bool verify_peer) {
  int fds[2];
  check(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "create socketpair");
  std::thread server([&] {
    SSL* ssl = SSL_new(server_ctx);
    if (ssl) {
      SSL_set_fd(ssl, fds[1]);
      if (SSL_accept(ssl) == 1) SSL_shutdown(ssl);
      SSL_free(ssl);
    }
    ::close(fds[1]);
  });
  xanatoo::TlsStream client;
  std::string err;
  const bool ok = client.connect(fds[0], host, &err, verify_peer);
  client.close();
  server.join();
  if (!ok) std::cerr << "Expected TLS rejection for " << host << ": " << err << '\n';
  return ok;
}

}  // namespace

int main() {
  try {
    std::signal(SIGPIPE, SIG_IGN);
    TestIdentity identity;
    Owner<SSL_CTX, SSL_CTX_free> server(SSL_CTX_new(TLS_server_method()),
                                       SSL_CTX_free);
    check(bool(server), "create TLS server context");
    check(SSL_CTX_use_certificate(server.get(), identity.cert.get()) == 1,
          "configure server certificate");
    check(SSL_CTX_use_PrivateKey(server.get(), identity.key.get()) == 1,
          "configure server key");

    char path[] = "/tmp/xanatoo-ca-XXXXXX";
    const int fd = mkstemp(path);
    check(fd >= 0, "create temporary test CA file");
    FILE* pem = fdopen(fd, "wb");
    check(pem != nullptr, "open temporary CA file");
    check(PEM_write_X509(pem, identity.cert.get()) == 1, "write test CA certificate");
    std::fclose(pem);
    const char* previous = std::getenv("SSL_CERT_FILE");
    const std::string previous_file = previous ? previous : "";
    check(setenv("SSL_CERT_FILE", path, 1) == 0, "configure test CA store");

    check(handshake(server.get(), "relay.example", true),
          "trusted certificate and matching hostname should connect");
    check(!handshake(server.get(), "different.example", true),
          "trusted certificate with wrong hostname must fail");
    check(handshake(server.get(), "different.example", false),
          "explicit Gemini-style self-signed exception should still work");

    TestIdentity untrusted_identity;
    Owner<SSL_CTX, SSL_CTX_free> untrusted(SSL_CTX_new(TLS_server_method()),
                                          SSL_CTX_free);
    check(bool(untrusted), "create untrusted TLS server context");
    check(SSL_CTX_use_certificate(untrusted.get(), untrusted_identity.cert.get()) == 1,
          "configure untrusted certificate");
    check(SSL_CTX_use_PrivateKey(untrusted.get(), untrusted_identity.key.get()) == 1,
          "configure untrusted private key");
    check(!handshake(untrusted.get(), "relay.example", true),
          "matching hostname with untrusted certificate must fail");
    check(handshake(untrusted.get(), "relay.example", false),
          "Gemini-style self-signed exception permits untrusted certificate");
    if (previous) setenv("SSL_CERT_FILE", previous_file.c_str(), 1);
    else unsetenv("SSL_CERT_FILE");
    std::remove(path);
    std::cout << "TLS peer certificate / hostname verification: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "TLS peer test failed: " << error.what() << '\n';
    return 1;
  }
}
