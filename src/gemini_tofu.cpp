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

// A user may approve a replacement without allowing writes (--no-save).
// Keep the previously persisted fingerprint too: a separate update to the
// stored pin must not be masked by this temporary approval.
using Overrides = std::map<std::string, std::pair<std::string, std::string>>;
Overrides& session_overrides() {
  static Overrides pins;
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

bool valid_identity(std::string_view host, int port,
                    std::string_view fingerprint) {
  return !host.empty() && host.size() <= 255 && port >= 1 && port <= 65535 &&
         fingerprint.size() == 64 && is_hex(fingerprint);
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
                       std::string_view fingerprint_sha256, std::string* err,
                       GeminiCertificateChange* changed,
                       GeminiCertificateFirstUse* first_use) {
  if (changed) *changed = {};
  if (first_use) *first_use = {};
  if (!valid_identity(host, port, fingerprint_sha256))
    return fail(err, "invalid Gemini host, port or certificate fingerprint");

  const std::lock_guard guard(pin_mutex());
  const std::string key = make_key(host, port);
  const fs::path path = directory.empty() ? fs::path{} :
      fs::path(directory) / "gemini-known-hosts";
  Pins saved;
  if (!path.empty() && !load(path, saved, err)) return false;

  const std::string session_key = std::string(directory) + '\n' + key;
  const auto from_disk = saved.find(key);
  const auto override = session_overrides().find(session_key);
  if (override != session_overrides().end() &&
      (from_disk == saved.end() || override->second.first != from_disk->second))
    return fail(err, "Gemini pin store changed since session-only approval; "
                     "review the trust store before reconnecting");
  const auto reject_change = [&](const std::string& old_hash,
                                 bool on_disk) {
    if (changed)
      *changed = {std::string(host), port, old_hash,
                  std::string(fingerprint_sha256)};
    return fail(err, "Gemini certificate CHANGED for " + std::string(host) +
        ':' + std::to_string(port) + "; saved SHA-256 " + old_hash +
        ", received " + std::string(fingerprint_sha256) +
        (on_disk ? ". Review both fingerprints before approving a replacement."
                 : ". Review both fingerprints before approving a session replacement."));
  };

  if (from_disk != saved.end()) {
    if (override != session_overrides().end()) {
      if (override->second.second != fingerprint_sha256)
        return reject_change(override->second.second, true);
      return true;
    }
    if (from_disk->second != fingerprint_sha256)
      return reject_change(from_disk->second, true);
    return true;
  }

  Pins& session = session_pins();
  if (const auto it = session.find(session_key); it != session.end()) {
    if (it->second != fingerprint_sha256)
      return reject_change(it->second, false);
    return true;
  }

  if (persist && path.empty())
    return fail(err, "no state directory for Gemini TOFU pins");
  if (first_use) {
    *first_use = {std::string(host), port, std::string(fingerprint_sha256)};
    return fail(err, "Gemini FIRST CONTACT for " + std::string(host) + ':' +
        std::to_string(port) + "; presented SHA-256 " +
        std::string(fingerprint_sha256) +
        ". Verify this fingerprint before trusting the server.");
  }
  if (persist) {
    saved.emplace(key, fingerprint_sha256);
    if (!save(path, saved, err)) return false;
  }
  session.emplace(session_key, fingerprint_sha256);
  return true;
}

bool gemini_tofu_approve_first_use(std::string_view directory, bool persist,
                                  const GeminiCertificateFirstUse& first_use,
                                  std::string* err) {
  if (!valid_identity(first_use.host, first_use.port,
                      first_use.fingerprint_sha256))
    return fail(err, "invalid Gemini first-use certificate fingerprint");

  const std::lock_guard guard(pin_mutex());
  const std::string key = make_key(first_use.host, first_use.port);
  const std::string session_key = std::string(directory) + '\n' + key;
  const fs::path path = directory.empty() ? fs::path{} :
      fs::path(directory) / "gemini-known-hosts";
  Pins saved;
  if (!path.empty() && !load(path, saved, err)) return false;

  if (const auto it = saved.find(key); it != saved.end()) {
    if (it->second != first_use.fingerprint_sha256)
      return fail(err, "Gemini certificate was pinned differently since first-use review");
    return true;  // another fetch approved the same fingerprint
  }
  auto& session = session_pins();
  if (const auto it = session.find(session_key); it != session.end()) {
    if (it->second != first_use.fingerprint_sha256)
      return fail(err, "Gemini certificate was pinned differently since first-use review");
    return true;
  }

  if (persist) {
    if (path.empty()) return fail(err, "no state directory for Gemini TOFU pins");
    saved.emplace(key, first_use.fingerprint_sha256);
    if (!save(path, saved, err)) return false;
  }
  session.emplace(session_key, first_use.fingerprint_sha256);
  return true;
}

bool gemini_tofu_replace(std::string_view directory, bool persist,
                         const GeminiCertificateChange& change,
                         std::string* err) {
  if (change.host.empty() || change.host.size() > 255 ||
      change.port < 1 || change.port > 65535 ||
      change.previous_sha256.size() != 64 || !is_hex(change.previous_sha256) ||
      change.received_sha256.size() != 64 || !is_hex(change.received_sha256) ||
      change.previous_sha256 == change.received_sha256)
    return fail(err, "invalid Gemini certificate replacement");

  const std::lock_guard guard(pin_mutex());
  const std::string key = make_key(change.host, change.port);
  const std::string session_key = std::string(directory) + '\n' + key;
  const fs::path path = directory.empty() ? fs::path{} :
      fs::path(directory) / "gemini-known-hosts";
  Pins saved;
  if (!path.empty() && !load(path, saved, err)) return false;
  auto stored = saved.find(key);
  const auto existing_override = session_overrides().find(session_key);
  if (existing_override != session_overrides().end() &&
      (stored == saved.end() || existing_override->second.first != stored->second))
    return fail(err, "Gemini pin store changed since session-only approval; "
                     "review the trust store before reconnecting");
  if (stored != saved.end()) {
    // When replacing a second time after a session-only approval, the
    // expected hash is the approved one, not the unchanged on-disk hash.
    const std::string current = existing_override != session_overrides().end() ?
        existing_override->second.second : stored->second;
    if (current != change.previous_sha256)
      return fail(err, "Gemini certificate pin changed since review; reload the page");
    if (persist) {
      stored->second = change.received_sha256;
      if (!save(path, saved, err)) return false;
      session_overrides().erase(session_key);
      session_pins()[session_key] = change.received_sha256;
    } else {
      session_overrides()[session_key] = {stored->second, change.received_sha256};
    }
    return true;
  }

  auto session = session_pins().find(session_key);
  if (session == session_pins().end() ||
      session->second != change.previous_sha256)
    return fail(err, "Gemini certificate pin changed since review; reload the page");
  if (persist) {
    if (path.empty()) return fail(err, "no state directory for Gemini TOFU pins");
    saved.emplace(key, change.received_sha256);
    if (!save(path, saved, err)) return false;
  }
  session->second = change.received_sha256;
  return true;
}

}  // namespace xanatoo
