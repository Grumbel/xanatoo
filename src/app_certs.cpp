// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Gemini server certificates: trust on first use without asking, a warning
// only when a server later presents another certificate. The address bar
// shows the Gemini icon in the favicon's place; it opens the Certificates
// panel (WindowRole::Certificates), where pins are viewed, accepted, kept
// or forgotten. Pins themselves live in gemini_tofu.cpp.

#include <algorithm>
#include <iostream>
#include <map>

#include "xanatoo/app.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/gemini_tofu.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

// "host:port" back into its parts (the host may itself contain ':').
bool split_cert_key(const std::string& key, std::string& host, int& port) {
  const size_t colon = key.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= key.size())
    return false;
  host = key.substr(0, colon);
  port = 0;
  for (size_t i = colon + 1; i < key.size(); ++i) {
    if (key[i] < '0' || key[i] > '9' || port > 65535) return false;
    port = port * 10 + (key[i] - '0');
  }
  return port >= 1 && port <= 65535;
}

std::string pane_cert_key(App& app, int pi) {
  if (!view_live(app, pi)) return {};
  const std::string& path = path_of(app, app.panes[pi]);
  if (!xanatoo::is_gemini_url(path)) return {};
  const auto u = xanatoo::parse_gemini_url(path);
  return u ? gemini_cert_key(u->host, u->port) : std::string{};
}

std::string self_signed_text(const xanatoo::TlsPeerCertificate& c) {
  if (c.self_signed.has_value())
    return *c.self_signed ? "Yes (self-signature verified)" : "No";
  return c.self_issued ?
      "Unknown (subject and issuer match; signature not verified)" : "Unknown";
}

}  // namespace

std::string gemini_cert_key(std::string_view host, int port) {
  return xanatoo::ascii_lower(host) + ":" + std::to_string(port);
}

void refresh_cert_pins(App& app) {
  app.certs.error.clear();
  app.certs.pins = xanatoo::gemini_tofu_list(xanatoo::state_dir(),
                                            &app.certs.error);
}

void note_gemini_peer(App& app, const FetchJob& job) {
  const FetchShared& r = *job.shared;
  if (r.gemini_change) {
    const auto& ch = *r.gemini_change;
    const std::string key = gemini_cert_key(ch.host, ch.port);
    GeminiSiteCert& c = app.gemini_certs[key];
    if (r.gemini_peer) c.peer = *r.gemini_peer;
    c.peer.host = ch.host;
    c.peer.port = ch.port;
    c.peer.sha256 = ch.received_sha256;
    c.peer.first_contact = false;
    c.change = ch;
    // Only a page the user asked for warns; background checks just record.
    if (job.purpose == FetchPurpose::Navigate ||
        job.purpose == FetchPurpose::Tab) {
      c.url = job.url;
      c.pane = job.purpose == FetchPurpose::Navigate ? job.pane : -1;
      show_certificates(app, key);
      std::cerr << "gemini: certificate of " << key << " changed; refused "
                << ch.received_sha256 << " (pinned " << ch.previous_sha256
                << ")\n";
    }
    return;
  }
  if (!r.gemini_peer || r.gemini_peer->host.empty()) return;
  const auto& peer = *r.gemini_peer;
  const std::string key = gemini_cert_key(peer.host, peer.port);
  GeminiSiteCert& c = app.gemini_certs[key];
  const bool first = peer.first_contact ||
                     (c.peer.first_contact && c.peer.sha256 == peer.sha256);
  c.peer = peer;
  c.peer.first_contact = first;
  c.change.reset();  // the server is back on a trusted certificate
  if (peer.first_contact) {
    std::cerr << "gemini: trusted first certificate of " << key << " "
              << peer.sha256 << "\n";
    if (app.certs.open) refresh_cert_pins(app);
  }
}

GeminiSiteCert* pane_gemini_cert(App& app, int pi) {
  const std::string key = pane_cert_key(app, pi);
  if (key.empty()) return nullptr;
  auto it = app.gemini_certs.find(key);
  return it == app.gemini_certs.end() ? nullptr : &it->second;
}

void show_certificates(App& app, const std::string& key) {
  app.certs.open = true;
  focus_panel(app, WindowRole::Certificates);
  app.certs.selected = key.empty() ? pane_cert_key(app, app.focus) : key;
  refresh_cert_pins(app);
}

void accept_gemini_cert(App& app, const std::string& key) {
  auto it = app.gemini_certs.find(key);
  if (it == app.gemini_certs.end() || !it->second.change) return;
  GeminiSiteCert& c = it->second;
  std::string err;
  if (!xanatoo::gemini_tofu_replace(xanatoo::state_dir(), !xanatoo::no_save(),
                                    *c.change, &err)) {
    app.certs.error = err;
    flash_status(app, "Certificate not trusted: " + err);
    return;
  }
  c.change.reset();
  refresh_cert_pins(app);
  flash_status(app, "Trusted the new certificate of " + key);
  // Reload the page that met the change wherever it is still shown. The
  // cached copy predates the change; the new pin is checked again.
  const std::string url = c.url;
  if (url.empty()) return;
  forget_cached_page(app, url);
  for (int pi : live_views(app))
    if (path_of(app, app.panes[pi]) == url)
      start_fetch(app, FetchPurpose::Navigate, pi, url,
                  /*record_history=*/false);
}

void reject_gemini_cert(App& app, const std::string& key) {
  auto it = app.gemini_certs.find(key);
  if (it == app.gemini_certs.end() || !it->second.change) return;
  const std::string url = it->second.url;
  it->second.change.reset();
  flash_status(app, "Kept the old certificate of " + key +
                        "; the new one stays refused");
  // Leave the refused page like a browser's "Go back".
  if (!url.empty() && path_of(app, focused_pane(app)) == url &&
      can_nav_step(app, -1))
    nav_step(app, -1);
}

void forget_gemini_cert(App& app, const std::string& key) {
  std::string host;
  int port = 0;
  if (!split_cert_key(key, host, port)) return;
  std::string err;
  if (!xanatoo::gemini_tofu_forget(xanatoo::state_dir(), !xanatoo::no_save(),
                                   host, port, &err)) {
    app.certs.error = err;
    flash_status(app, "Certificate not removed: " + err);
    refresh_cert_pins(app);
    return;
  }
  if (auto it = app.gemini_certs.find(key); it != app.gemini_certs.end()) {
    it->second.change.reset();
    it->second.peer.first_contact = false;
  }
  refresh_cert_pins(app);
  flash_status(app, "Forgot the certificate of " + key +
                        "; the next visit trusts what it presents");
}

void cert_ui_state(App& app, xanatoo::UiState& st) {
  st.site_identity = xanatoo::UiState::SiteIdentity::None;
  st.site_identity_key = pane_cert_key(app, app.focus);
  st.site_identity_tip.clear();
  if (!st.site_identity_key.empty()) {
    const GeminiSiteCert* c = pane_gemini_cert(app, app.focus);
    if (c && c->change) {
      st.site_identity = xanatoo::UiState::SiteIdentity::GeminiChanged;
      st.site_identity_tip = "Certificate CHANGED for " + st.site_identity_key +
                             " — nothing was sent. Click to review.";
    } else {
      st.site_identity = xanatoo::UiState::SiteIdentity::Gemini;
      st.site_identity_tip = "Gemini · " + st.site_identity_key +
          (c && c->peer.first_contact ? " · certificate trusted on this first visit"
                                      : c ? " · certificate matches the pin"
                                          : "") +
          " · click for certificate details";
    }
  }
  st.certs_open = app.certs.open;
  if (!app.certs.open) return;
  auto [x, y, w, h] = float_client_rect(app, app.certs_win);
  st.certs_x = x;
  st.certs_y = y;
  st.certs_w = w;
  st.certs_h = h;
  st.cert_selected = app.certs.selected;
  st.cert_error = app.certs.error;
  st.cert_no_save = xanatoo::no_save();
  std::map<std::string, xanatoo::UiState::CertRow> rows;  // sorted by key
  for (const auto& pin : app.certs.pins) {
    auto& row = rows[gemini_cert_key(pin.host, pin.port)];
    row.host = pin.host;
    row.port = pin.port;
    row.sha256 = pin.sha256;
    row.saved = pin.saved;
  }
  for (const auto& [key, c] : app.gemini_certs) {
    auto& row = rows[key];
    row.host = c.peer.host;
    row.port = c.peer.port;
    row.seen = true;
    row.presented = c.peer.sha256;
    row.first_contact = c.peer.first_contact;
    row.changed = c.change.has_value();
    if (c.change) row.previous = c.change->previous_sha256;
    if (c.peer.details) {
      row.subject = c.peer.details->subject;
      row.issuer = c.peer.details->issuer;
      row.not_before = c.peer.details->not_before;
      row.not_after = c.peer.details->not_after;
      row.self_signed = self_signed_text(*c.peer.details);
    }
  }
  st.cert_rows.clear();
  for (auto& [key, row] : rows) {
    row.key = key;
    row.current = key == st.site_identity_key;
    st.cert_rows.push_back(std::move(row));
  }
  // The focused page's server first, then changed ones, then by name.
  std::stable_sort(st.cert_rows.begin(), st.cert_rows.end(),
                   [](const auto& a, const auto& b) {
                     if (a.current != b.current) return a.current;
                     return a.changed && !b.changed;
                   });
}

}  // namespace xanatoo::app
