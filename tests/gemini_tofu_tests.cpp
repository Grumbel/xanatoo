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
  std::string err;
  auto tofu = [&](bool persist, std::string host, int port,
                  const std::string& hash) {
    err.clear();
    return xanatoo::gemini_tofu_check(dir.string(), persist, host, port,
                                      hash, &err);
  };

  check(tofu(true, "example.org", 1965, a), "first certificate is pinned");
  check(fs::exists(file), "persist a Gemini known-hosts file");
  check(tofu(true, "EXAMPLE.ORG", 1965, a), "hostnames are case-insensitive");
  check(!tofu(true, "example.org", 1965, b) &&
        err.find("CHANGED") != std::string::npos,
        "changed certificate must fail before making a request");
  check(tofu(true, "example.org", 1966, b), "different port has separate pin");
  check(tofu(true, "other.org", 1965, b), "different host has separate pin");
  check(!tofu(false, "example.org", 1965, b),
        "--no-save still enforces existing persisted pins");

  std::ifstream saved(file);
  const std::string initial((std::istreambuf_iterator<char>(saved)), {});
  check(tofu(false, "ephemeral.org", 1965, a),
        "--no-save permits first use in memory");
  check(tofu(false, "EPHEMERAL.ORG", 1965, a),
        "--no-save remembers the certificate in the session");
  check(!tofu(false, "ephemeral.org", 1965, b),
        "--no-save refuses a certificate change within the session");
  std::ifstream reread(file);
  check(std::string((std::istreambuf_iterator<char>(reread)), {}) == initial,
        "--no-save must not modify the stored pins");

  check(!xanatoo::gemini_tofu_check("", true, "example.org", 1965, a, &err),
        "no state directory must fail closed for persistent first use");
  check(!tofu(true, "new.org", 1965, "not-a-hash"),
        "malformed fingerprints are rejected");
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
