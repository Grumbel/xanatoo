// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/catalog_credentials.hpp"

#include "xanatoo/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace xanatoo {

bool CatalogCredentials::empty() const {
  return std::all_of(values.begin(), values.end(), [](const auto& v) {
    return v.empty();
  });
}

bool valid_catalog_credential(std::string_view value) {
  if (value.size() > 511) return false;
  for (unsigned char c : value)
    if (c <= 32 || c >= 127) return false;
  return true;
}

std::string catalog_credentials_path() {
  const std::string dir = config_dir();
  return dir.empty() ? std::string{} : (fs::path(dir) / "catalog-credentials.conf").string();
}

CatalogCredentials load_catalog_credentials() {
  CatalogCredentials result;
  const std::string path = catalog_credentials_path();
  if (path.empty()) return result;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    for (size_t i = 0; i < catalog_key_count; ++i) {
      const std::string prefix = std::string(catalog_key_info[i].setting) + "=";
      if (line.rfind(prefix, 0) != 0) continue;
      const std::string value = line.substr(prefix.size());
      if (valid_catalog_credential(value)) result.values[i] = value;
      break;
    }
  }
  return result;
}

namespace {

bool write_private_file(const fs::path& target, const std::string& content) {
#ifdef _WIN32
  // The private user config directory controls access on Windows.
  const fs::path tmp = target.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) return false;
  }
  if (!MoveFileExW(tmp.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ec;
    fs::remove(tmp, ec);
    return false;
  }
  return true;
#else
  // A unique 0600 temporary file avoids symlink races and loose umasks.
  std::string pattern = target.string() + ".tmp.XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  const int fd = ::mkstemp(name.data());
  if (fd < 0) return false;
  const fs::path tmp = name.data();
  bool ok = ::fchmod(fd, 0600) == 0;
  size_t pos = 0;
  while (ok && pos < content.size()) {
    const ssize_t n = ::write(fd, content.data() + pos, content.size() - pos);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ok = false; break; }
    pos += static_cast<size_t>(n);
  }
  if (::close(fd) != 0) ok = false;
  if (!ok) {
    std::error_code ec;
    fs::remove(tmp, ec);
    return false;
  }
  std::error_code ec;
  fs::rename(tmp, target, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
#endif
}

}  // namespace

bool save_catalog_credentials(const CatalogCredentials& credentials) {
  if (no_save()) return false;
  for (const std::string& value : credentials.values)
    if (!valid_catalog_credential(value)) return false;
  const std::string path = catalog_credentials_path();
  if (path.empty()) return false;
  if (credentials.empty()) {
    std::error_code ec;
    fs::remove(path, ec);
    return !ec;
  }
  if (!ensure_dir(fs::path(path).parent_path().string())) return false;
  std::string content;
  for (size_t i = 0; i < catalog_key_count; ++i) {
    if (!credentials.values[i].empty()) {
      content += catalog_key_info[i].setting;
      content += '=';
      content += credentials.values[i];
      content += '\n';
    }
  }
  return write_private_file(path, content);
}

bool catalog_credential_from_environment(CatalogKey key) {
  const size_t i = catalog_key_index(key);
  if (i >= catalog_key_count) return false;
  const char* v = std::getenv(catalog_key_info[i].environment);
  return v != nullptr && *v != '\0';
}

std::string effective_catalog_credential(CatalogKey key) {
  const size_t i = catalog_key_index(key);
  if (i >= catalog_key_count) return {};
  if (const char* v = std::getenv(catalog_key_info[i].environment); v && *v)
    return v;
  return load_catalog_credentials().values[i];
}

std::string catalog_credentials_command(const CatalogCredentials& credentials) {
  std::string result;
  for (size_t i = 0; i < catalog_key_count; ++i) {
    if (i) result += '\n';
    result += credentials.values[i];
  }
  return result;
}

std::optional<CatalogCredentials> parse_catalog_credentials_command(std::string_view text) {
  CatalogCredentials result;
  for (size_t i = 0; i < catalog_key_count; ++i) {
    const size_t split = text.find('\n');
    if (i + 1 == catalog_key_count) {
      if (split != std::string_view::npos || !valid_catalog_credential(text))
        return std::nullopt;
      result.values[i] = text;
      return result;
    }
    if (split == std::string_view::npos) return std::nullopt;
    const std::string_view part = text.substr(0, split);
    if (!valid_catalog_credential(part)) return std::nullopt;
    result.values[i] = part;
    text.remove_prefix(split + 1);
  }
  return std::nullopt;
}

}  // namespace xanatoo
