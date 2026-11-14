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
| Gemini `gemini://` | TOFU (SHA-256 leaf certificate pin) | First contact is unauthenticated; later connections require the same certificate per host and port. |
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

## Gemini trust on first use

Gemini often uses self-signed certificates instead of publicly trusted CAs.
On the **first** successful TLS handshake with a host and port, Xanatoo hashes
the server's leaf certificate (SHA-256), pins it in
`state_dir()/gemini-known-hosts` and sends the request — without asking, like
SSH's `accept-new` or other Gemini clients. There is nothing to compare a
first certificate against, so a question would only teach clicking "yes";
it also needs no keyboard, which touch devices lack. First use is therefore
unauthenticated: an attacker present on the very first connection can have
their certificate pinned. Compare the fingerprint in the Certificates panel
through an independent channel when that matters.

Subsequent connections require the pinned fingerprint, including after
redirects to another server. A changed certificate **stops the fetch before
the URL is sent**. The page then explains the change and shows both full
fingerprints, the address bar's site icon turns into a warning, and the
**Certificates** panel opens on that server with two buttons:

- **Trust new certificate** replaces the pin and reloads the page over a
  fresh TLS connection. The replacement is compare-and-set against the pin
  that triggered the warning, so a stale confirmation cannot overwrite a pin
  changed meanwhile; the certificate is checked again before the reloaded
  request is sent, and any third fingerprint is refused.
- **Keep old certificate** leaves the pin as it is and goes back.

A certificate change can be a legitimate renewal or an active interception;
there is no automatic acceptance of a replaced certificate. Background
fetches (tabs, subscriptions) record the change too; tabs open the panel.

The Certificates panel (Panels → Certificates, or a click / tap on the
Gemini icon in the favicon's place of the address bar) is a dockable panel.
It lists every pinned server and every server met this session; for the
selected one it shows the pinned fingerprint, whether it was trusted on this
session's first visit, and — for servers connected this session — the
leaf certificate's subject, issuer, validity start/end in UTC, and
self-signed status. **Forget** removes the pin, so the next visit trusts
whatever the server presents then. The SHA-256 fingerprints are grouped into
short blocks so they can wrap without losing any hex digits. OpenSSL
verifies a purported self-signature using the leaf's own public key. The
Mbed TLS backend reports the subject/issuer names and dates but marks
self-signature verification as **unknown**: matching issuer and subject
names alone would not prove a valid self-signature. These fields are
descriptive and **do not establish CA or hostname authentication** for
Gemini. Peer-controlled names are shown as literal text, not Markdown.

Store read errors, invalid fingerprints, and other TLS failures do **not**
offer a bypass.

The entries use a `sha256 <hex-encoded-host>:<port> <fingerprint>` format;
the host is hex-encoded so IPv6 addresses cannot introduce ambiguous delimiters.
An invalid or unreadable pin file is an error rather than a fresh first use.

With `--no-save`, Xanatoo still checks any existing saved pins, but new pins
and explicitly approved replacements live only in memory for that process;
the on-disk file remains unchanged (and Forget cannot remove a saved pin). A later change to the saved pin invalidates
the temporary override. With normal persistence enabled, an
unavailable or unwritable state directory blocks a new Gemini connection;
the URL is never sent without first establishing trust.

On NixOS, a development shell normally provides `SSL_CERT_FILE` (or
`NIX_SSL_CERT_FILE`). The OpenSSL backend explicitly loads that PEM bundle
as well as the default verification paths, so packaged OpenSSL builds do not
have to recognize the environment override during default-path lookup. If a
deployment has no system CA bundle, arrange one in the runtime environment
before launching Xanatoo. Do not disable verification to work around an
unavailable trust store.

Mbed TLS CA loading and verification still need tests on the physical Android
targets (especially Fire OS 5 and newer Conscrypt layouts). Some Android
platform configurations may not expose CA files in a form that the embedded
Mbed TLS build can read; in that case HTTPS/WSS intentionally refuse to
connect until the CA loading path is fixed.

## Regression coverage and limitations

`tests/tls_peer_tests.cpp` opens an offline `socketpair(AF_UNIX)` TLS connection to a temporary
OpenSSL test server with an ephemeral local CA. It checks a trusted matching
hostname, mismatched hostname, and an untrusted certificate, plus the
explicit Gemini handshake, fingerprint and certificate metadata, including a
self-issued leaf signed by a different key (which is not self-signed).
`tests/gemini_tofu_tests.cpp` tests first-contact challenges, approval before
pinning, concurrent first-use approvals, listing and forgetting pins, persisted/session-only pins, hostname
and port separation, structured mismatch reports, explicit replacement
approvals, stale approvals, session-only overrides and corrupted-store
behavior without SDL.
`tests/http_url_tests.cpp` checks
HTTPS-to-HTTP downgrade rejection. The tests do not replace integration
testing of real sites, operating-system trust stores or Android's Mbed TLS
backend.

**Not yet solved:** separate modal certificate inspection with SAN display,
explicit expiry warnings and Mbed TLS self-signature verification. Verified
HTTPS/WSS alone does not
authenticate Nostr event authors, relay group membership, NIP-11 master-key
claims, or API authorization. Each has its own cryptographic and
application-level checks.

## Error reporting and close-delimited bodies

- `TlsStream::last_error()` says why the last read failed (OpenSSL error
  queue, errno, timeout, Mbed TLS code). `https.cpp` adds the stage (connect,
  handshake, waiting for headers, body at N bytes) and `html_page.cpp` adds
  hints for refused pages (Cloudflare `cf-mitigated`, CAPTCHA, "requires
  JavaScript", `Retry-After`, `Server`).
- Servers such as YouTube end a body that has neither `Content-Length` nor
  chunking by closing TCP without a TLS `close_notify`. OpenSSL 3 raises
  "unexpected eof while reading"; like curl, `TlsStream::read` returns 0 (EOF)
  for it. Length-delimited and chunked bodies still fail as "incomplete".
