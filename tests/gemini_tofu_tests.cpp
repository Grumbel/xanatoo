// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Pure file-backed Gemini trust-on-first-use test; no SDL/network needed.

#include "xanatoo/gemini_tofu.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("xanatoo-gemini-tofu-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(fs::create_directory(dir), "create isolated Gemini trust directory");
  const fs::path file = dir / "gemini-known-hosts";
  const std::string a(64, 'a');
  const std::string b(64, 'b');
  const std::string c(64, 'c');
  std::string err;
  xanatoo::GeminiCertificateChange changed;
  auto tofu = [&](bool persist, std::string host, int port,
                  const std::string& hash) {
    err.clear();
    return xanatoo::gemini_tofu_check(dir.string(), persist, host, port,
                                      hash, &err, &changed);
  };
  auto approve = [&](bool persist, const xanatoo::GeminiCertificateChange& change) {
    err.clear();
    return xanatoo::gemini_tofu_replace(dir.string(), persist, change, &err);
  };

  check(tofu(true, "example.org", 1965, a), "first certificate is pinned");
  check(fs::exists(file), "persist a Gemini known-hosts file");
  check(tofu(true, "EXAMPLE.ORG", 1965, a), "hostnames are case-insensitive");
  check(!tofu(true, "example.org", 1965, b) &&
        err.find("CHANGED") != std::string::npos,
        "changed certificate must fail before making a request");
  check(changed.host == "example.org" && changed.port == 1965 &&
        changed.previous_sha256 == a && changed.received_sha256 == b,
        "pin mismatch provides structured old/new fingerprints for review");
  auto replacement = changed;
  check(!approve(true, {"example.org", 1965, c, b}) &&
        err.find("changed since review") != std::string::npos,
        "cannot approve a forged or stale previous fingerprint");
  check(!tofu(true, "example.org", 1965, b),
        "a rejected approval leaves the saved pin untouched");
  check(approve(true, replacement), "explicit approval replaces the persisted pin");
  check(tofu(true, "EXAMPLE.ORG", 1965, b) && changed.host.empty(),
        "approved fingerprint is now trusted and mismatch output is reset");
  check(!tofu(true, "example.org", 1965, a) &&
        changed.previous_sha256 == b,
        "old certificate becomes untrusted after replacement");
  check(tofu(true, "example.org", 1966, b), "different port has separate pin");
  check(tofu(true, "other.org", 1965, b), "different host has separate pin");
  check(!tofu(false, "example.org", 1965, a),
        "--no-save still enforces existing persisted pins");

  std::ifstream saved(file);
  const std::string initial((std::istreambuf_iterator<char>(saved)), {});
  check(approve(false, changed),
        "--no-save allows an explicitly reviewed session-only replacement");
  check(tofu(false, "example.org", 1965, a),
        "session-only replacement can temporarily trust the old certificate");
  check(!tofu(false, "example.org", 1965, c) &&
        changed.previous_sha256 == a,
        "unapproved third fingerprint is not trusted after session approval");
  check(approve(false, changed), "allow second session-only replacement");
  check(tofu(false, "example.org", 1965, c),
        "session-only replacement can be updated after fresh review");
  check(tofu(false, "ephemeral.org", 1965, a),
        "--no-save permits first use in memory");
  check(tofu(false, "EPHEMERAL.ORG", 1965, a),
        "--no-save remembers the certificate in the session");
  check(!tofu(false, "ephemeral.org", 1965, b),
        "--no-save refuses a certificate change within the session");
  check(changed.previous_sha256 == a && changed.received_sha256 == b,
        "in-memory certificate mismatch also provides review data");
  check(approve(false, changed) && tofu(false, "ephemeral.org", 1965, b),
        "explicit session-only approval does not require a saved pin");
  std::ifstream reread(file);
  check(std::string((std::istreambuf_iterator<char>(reread)), {}) == initial,
        "--no-save must not modify the stored pins");

  check(!xanatoo::gemini_tofu_check("", true, "example.org", 1965, a, &err),
        "no state directory must fail closed for persistent first use");
  check(!tofu(true, "new.org", 1965, "not-a-hash"),
        "malformed fingerprints are rejected");
  check(changed.host.empty(), "invalid fingerprint is not an approvable mismatch");

  // Another process may have updated the pin file while the user was
  // inspecting a warning. Never apply a stale confirmation over that change.
  check(!tofu(true, "other.org", 1965, a), "other host reports a mismatch");
  auto stale = changed;
  {
    std::ofstream updated(file, std::ios::trunc);
    const std::string host_hex = "6f746865722e6f7267";
    updated << "sha256 " << host_hex << ":1965 " << c << '\n';
    updated << "sha256 6578616d706c652e6f7267:1965 " << a << '\n';
  }
  check(!approve(true, stale) &&
        err.find("changed since review") != std::string::npos,
        "stale confirmation refuses an independently updated stored pin");
  check(!tofu(false, "example.org", 1965, c),
        "changing the saved pin invalidates an existing session override");
  {
    std::ofstream corrupt(file, std::ios::trunc);
    corrupt << "this is not a trusted pin\n";
  }
  check(!tofu(true, "first.org", 1965, a) &&
        err.find("invalid Gemini pin store") != std::string::npos,
        "malformed pin files must fail closed instead of resetting trust");

  fs::remove(file);
  fs::remove(dir);
  std::cout << "Gemini TOFU persistence and mismatch checks: PASS\n";
  return 0;
}
