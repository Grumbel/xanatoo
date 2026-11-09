<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# TLS identity verification

Xanatoo uses `TlsStream` for HTTPS, secure Nostr WebSockets (`wss://`) and
Gemini, with OpenSSL on desktop and Mbed TLS on Android.

| Transport | Certificate and hostname verification | Notes |
|---|---|---|
| `https://` | Required by default | Buffered APIs, HTML, media, images and streaming downloads share this policy. |
| Nostr `wss://` | Required | Invalid/untrusted relay certificates prevent the WebSocket connection. |
| NIP-11 `https://` | Required | Additionally demands HTTPS, public resolved IP addresses for automatic discovery, and a 256 KiB response limit. |
| Gemini `gemini://` | **Not implemented** | Self-signed certificates remain accepted without TOFU/pinning. Treat the server as unauthenticated. |
| `http://` | Not applicable | Explicitly requested plaintext HTTP is allowed. Redirects from HTTPS back to HTTP are rejected. |

For verified connections, the certificate must chain to a trusted root and
match the requested hostname (or IP address). The client **fails closed** if
the trust store is unavailable or the certificate is invalid. There is no
"ignore certificate errors" switch exposed by the browsing UI. Redirects
can upgrade HTTP to HTTPS but never downgrade an HTTPS connection to HTTP.

## System trust stores

- **OpenSSL:** uses the system-configured default trust paths, including its
  standard `SSL_CERT_FILE` and `SSL_CERT_DIR` overrides.
- **Mbed TLS:** parses a CA bundle from `SSL_CERT_FILE` or
  `NIX_SSL_CERT_FILE`, followed by common Linux CA bundles; if none exist,
  attempts `SSL_CERT_DIR`, Android Conscrypt's
  `/apex/com.android.conscrypt/cacerts`, `/system/etc/security/cacerts`, then
  `/etc/ssl/certs`. The parsed roots are cached across connections. This
  requires a build with `MBEDTLS_FS_IO` enabled.

On NixOS, a development shell normally provides `SSL_CERT_FILE`; if a
deployment has no system CA bundle, arrange one in the runtime environment
before launching Xanatoo. Do not disable verification to work around an
unavailable trust store.

Mbed TLS CA loading and verification still need tests on the physical Android
targets (especially Fire OS 5 and newer Conscrypt layouts). Some Android
platform configurations may not expose CA files in a form that the embedded
Mbed TLS build can read; in that case HTTPS/WSS intentionally refuse to
connect until the CA loading path is fixed.

## Regression coverage and limitations

`tests/tls_peer_tests.cpp` opens an offline TLS connection to a temporary
OpenSSL test server with an ephemeral local CA. It checks a trusted matching
hostname, mismatched hostname, and an untrusted certificate, plus the
explicit unverified Gemini behavior. `tests/http_url_tests.cpp` checks
HTTPS-to-HTTP downgrade rejection. The tests do not replace integration
testing of real sites, operating-system trust stores or Android's Mbed TLS
backend.

**Not yet solved:** Gemini TOFU/pinning and a certificate-change prompt;
verified HTTPS/WSS alone does not authenticate Nostr event authors, relay
group membership, NIP-11 master-key claims, or API authorization. Each has
its own cryptographic and application-level checks.
