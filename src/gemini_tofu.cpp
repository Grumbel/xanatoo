// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/gemini_tofu.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

namespace xanatoo {
namespace {

namespace fs = std::filesystem;
using Pins = std::map<std::string, std::string>;

std::mutex& pin_mutex() {
  static std::mutex mutex;
  return mutex;
}

Pins& session_pins() {
  static Pins pins;
  return pins;
}

bool fail(std::string* err, std::string msg) {
  if (err) *err = std::move(msg);
  return false;
}

bool is_hex(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

std::string make_key(std::string_view host, int port) {
  // Hex encoding makes the on-disk representation unambiguous, even for
  // bracket-free IPv6 addresses, Unicode hostnames and control characters.
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(host.size() * 2 + 6);
  for (unsigned char c : host) {
    c = static_cast<unsigned char>(std::tolower(c));
    result += hex[c >> 4];
    result += hex[c & 15];
  }
  return result + ':' + std::to_string(port);
}

bool valid_key(std::string_view key) {
  const size_t colon = key.rfind(':');
  if (colon == std::string_view::npos || colon == 0 || colon % 2 != 0 ||
      !is_hex(key.substr(0, colon))) return false;
  const auto port = key.substr(colon + 1);
  if (port.empty() || port.size() > 5 ||
      !std::all_of(port.begin(), port.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
      })) return false;
  const int value = std::stoi(std::string(port));
  return value >= 1 && value <= 65535;
}

bool load(const fs::path& path, Pins& pins, std::string* err) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    if (ec) return fail(err, "cannot inspect Gemini pin store: " + ec.message());
    return true;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) return fail(err, "cannot read Gemini pin store: " + path.string());
  std::string line;
  size_t number = 0;
  while (std::getline(file, line)) {
    ++number;
    if (line.empty() || line.front() == '#') continue;
    std::istringstream row(line);
    std::string algorithm, key, fingerprint, extra;
    if (!(row >> algorithm >> key >> fingerprint) || (row >> extra) ||
        algorithm != "sha256" || !valid_key(key) ||
        fingerprint.size() != 64 || !is_hex(fingerprint) ||
        !pins.emplace(key, fingerprint).second) {
      return fail(err, "invalid Gemini pin store at " + path.string() +
                       " line " + std::to_string(number));
    }
  }
  if (file.bad()) return fail(err, "error reading Gemini pin store: " + path.string());
  return true;
}

bool save(const fs::path& path, const Pins& pins, std::string* err) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return fail(err, "cannot create Gemini pin directory: " + ec.message());

  static std::atomic<unsigned long> counter{0};
  fs::path temporary = path;
  temporary += ".tmp-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(++counter);
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return fail(err, "cannot create temporary Gemini pin store");
    out << "# Xanatoo Gemini TOFU SHA-256 certificate fingerprints\n";
    for (const auto& [key, fingerprint] : pins)
      out << "sha256 " << key << ' ' << fingerprint << '\n';
    out.close();
    if (!out) {
      fs::remove(temporary, ec);
      return fail(err, "cannot write Gemini pin store");
    }
  }
#ifndef _WIN32
  // Windows user-profile ACLs control access; POSIX owner-only permissions
  // do not map portably to that platform's filesystem permissions API.
  fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);
  if (ec) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return fail(err, "cannot protect Gemini pin store: " + ec.message());
  }
#endif
  fs::rename(temporary, path, ec);
  if (ec) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return fail(err, "cannot update Gemini pin store: " + ec.message());
  }
  return true;
}

}  // namespace

bool gemini_tofu_check(std::string_view directory, bool persist,
                       std::string_view host, int port,
                       std::string_view fingerprint_sha256, std::string* err) {
  if (host.empty() || host.size() > 255 || port < 1 || port > 65535 ||
      fingerprint_sha256.size() != 64 || !is_hex(fingerprint_sha256))
    return fail(err, "invalid Gemini host, port or certificate fingerprint");

  const std::lock_guard guard(pin_mutex());
  const std::string key = make_key(host, port);
  const fs::path path = directory.empty() ? fs::path{} :
      fs::path(directory) / "gemini-known-hosts";
  Pins saved;
  if (!path.empty() && !load(path, saved, err)) return false;

  const auto from_disk = saved.find(key);
  if (from_disk != saved.end()) {
    if (from_disk->second != fingerprint_sha256)
      return fail(err, "Gemini certificate CHANGED for " + std::string(host) +
          ':' + std::to_string(port) + "; saved SHA-256 " + from_disk->second +
          ", received " + std::string(fingerprint_sha256) +
          ". Inspect the certificate before removing its entry in " +
          path.string());
    return true;
  }

  Pins& session = session_pins();
  const std::string session_key = std::string(directory) + '\n' + key;
  if (const auto it = session.find(session_key); it != session.end()) {
    if (it->second != fingerprint_sha256)
      return fail(err, "Gemini certificate CHANGED during this session for " +
          std::string(host) + ':' + std::to_string(port));
    return true;
  }

  if (persist) {
    if (path.empty()) return fail(err, "no state directory for Gemini TOFU pins");
    saved.emplace(key, fingerprint_sha256);
    if (!save(path, saved, err)) return false;
  }
  session.emplace(session_key, fingerprint_sha256);
  return true;
}

}  // namespace xanatoo
