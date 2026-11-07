// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/reddit_credentials.hpp"

#include "xanatoo/paths.hpp"

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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace xanatoo {

std::string reddit_credentials_path() {
  const std::string dir = config_dir();
  return dir.empty() ? std::string{} : (fs::path(dir) / "reddit.conf").string();
}

RedditCredentials load_reddit_credentials() {
  RedditCredentials result;
  const std::string path = reddit_credentials_path();
  if (path.empty()) return result;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("client_id=", 0) == 0)
      result.client_id = line.substr(10);
    else if (line.rfind("client_secret=", 0) == 0)
      result.client_secret = line.substr(14);
  }
  return result;
}

namespace {

bool valid_value(const std::string& value) {
  // Disallow config injection and overlong/mistyped credentials.
  if (value.size() > 255) return false;
  for (unsigned char c : value)
    if (c <= ' ' || c == 127) return false;
  return true;
}

bool write_private_file(const fs::path& target, const std::string& content) {
#ifdef _WIN32
  // Windows access control follows the user's private config directory.
  const fs::path tmp = target.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) return false;
  }
  // std::filesystem::rename does not replace existing files on Windows.
  if (!MoveFileExW(tmp.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ec;
    fs::remove(tmp, ec);
    return false;
  }
  return true;
#else
  // mkstemp creates a new 0600 file, avoiding a predictable .tmp symlink.
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

bool save_reddit_credentials(const RedditCredentials& credentials) {
  if (no_save()) return false;  // No persistent secrets in a no-save session.
  if (!valid_value(credentials.client_id) ||
      !valid_value(credentials.client_secret) ||
      (credentials.client_id.empty() != credentials.client_secret.empty()))
    return false;
  const std::string path = reddit_credentials_path();
  if (path.empty()) return false;
  if (!credentials.complete()) {
    std::error_code ec;
    fs::remove(path, ec);
    return !ec;
  }
  if (!ensure_dir(fs::path(path).parent_path().string())) return false;
  return write_private_file(path, "client_id=" + credentials.client_id +
                                  "\nclient_secret=" + credentials.client_secret + "\n");
}

bool reddit_credentials_from_environment() {
  const char* id = std::getenv("XANATOO_REDDIT_CLIENT_ID");
  const char* secret = std::getenv("XANATOO_REDDIT_CLIENT_SECRET");
  return id && *id && secret && *secret;
}

RedditCredentials effective_reddit_credentials() {
  if (reddit_credentials_from_environment())
    return {std::getenv("XANATOO_REDDIT_CLIENT_ID"),
            std::getenv("XANATOO_REDDIT_CLIENT_SECRET")};
  return load_reddit_credentials();
}

}  // namespace xanatoo
