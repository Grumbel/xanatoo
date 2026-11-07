// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/download.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace xanatoo {
namespace fs = std::filesystem;
namespace {

std::string decode_percent(std::string_view s) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const int a = hex(s[i + 1]), b = hex(s[i + 2]);
      if (a >= 0 && b >= 0) {
        out.push_back(static_cast<char>((a << 4) | b));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string_view trimmed(std::string_view s) {
  trim_ascii(s);
  return s;
}

std::string disposition_name(std::string_view d, std::string_view key) {
  // Parameters are delimited by semicolons outside quoted strings.
  for (size_t i = 0; i < d.size();) {
    while (i < d.size() && (d[i] == ';' || d[i] == ' ' || d[i] == '\t')) ++i;
    const size_t start = i;
    while (i < d.size() && d[i] != '=' && d[i] != ';') ++i;
    std::string name = ascii_lower(std::string(trimmed(d.substr(start, i-start))));
    if (i == d.size() || d[i] != '=') {
      while (i < d.size() && d[i] != ';') ++i;
      continue;
    }
    ++i;
    std::string value;
    if (i < d.size() && d[i] == '"') {
      ++i;
      while (i < d.size() && d[i] != '"') {
        if (d[i] == '\\' && i + 1 < d.size()) ++i;
        value.push_back(d[i++]);
      }
      if (i < d.size()) ++i;
    } else {
      const size_t vstart = i;
      while (i < d.size() && d[i] != ';') ++i;
      value = std::string(trimmed(d.substr(vstart, i - vstart)));
    }
    if (name == key) return value;
    while (i < d.size() && d[i] != ';') ++i;
  }
  return {};
}

std::string safe_name(std::string name) {
  for (char& c : name)
    if (static_cast<unsigned char>(c) < 32 || c == '/' || c == '\\' ||
        c == ':' || c == 127) c = '_';
  while (!name.empty() && (name.front() == '.' || name.front() == ' '))
    name.erase(name.begin());
  while (!name.empty() && (name.back() == '.' || name.back() == ' '))
    name.pop_back();
  if (name.empty()) return "download";
  if (name.size() > 180) name.resize(180);
  return name;
}

std::string numbered_name(const std::string& name, unsigned number) {
  if (number == 0) return name;
  const size_t dot = name.rfind('.');
  const size_t split = dot != std::string::npos && dot > 0 ? dot : name.size();
  return name.substr(0, split) + " (" + std::to_string(number) + ")" +
         name.substr(split);
}

}  // namespace

bool download_response(std::string_view url, std::string_view content_type,
                       std::string_view disposition) {
  const std::string cd = ascii_lower(std::string(disposition));
  if (trimmed(cd).starts_with("attachment")) return true;
  std::string type = ascii_lower(std::string(content_type));
  if (const size_t semi = type.find(';'); semi != std::string::npos)
    type.resize(semi);
  type = std::string(trimmed(type));
  if (type.empty()) {
    // Missing Content-Type: conservatively keep known text paths readable,
    // but download obvious binaries instead of parsing them as HTML.
    std::string path(url.substr(0, url.find_first_of("?#")));
    for (const char* ext : {".pdf", ".zip", ".gz", ".tgz", ".xz", ".bz2",
                            ".7z", ".rar", ".tar", ".exe", ".dmg", ".iso",
                            ".apk", ".deb", ".rpm", ".msi", ".mp3", ".mp4",
                            ".m4a", ".ogg", ".opus", ".flac", ".wav",
                            ".webm", ".mkv", ".avi", ".mov", ".docx",
                            ".xlsx", ".pptx", ".odt", ".bin"})
      if (path_ends_with_ci(path, ext)) return true;
    return false;
  }
  if (type == "text/html" || type == "application/xhtml+xml" ||
      type == "application/json" || type == "application/rss+xml" ||
      type == "application/atom+xml")
    return false;
  if (type.starts_with("text/")) return false;
  return true;
}

std::string download_filename(std::string_view url,
                              std::string_view disposition) {
  std::string value = disposition_name(disposition, "filename*");
  if (ascii_lower(value).starts_with("utf-8''"))
    value = decode_percent(std::string_view(value).substr(7));
  else
    value.clear();  // unknown filename* encoding
  if (value.empty()) value = disposition_name(disposition, "filename");
  if (value.empty()) {
    std::string_view path = url.substr(0, url.find_first_of("?#"));
    if (const size_t scheme = path.find("://"); scheme != std::string_view::npos) {
      const size_t slash = path.find('/', scheme + 3);
      path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash);
    }
    const size_t slash = path.rfind('/');
    value = decode_percent(path.substr(slash == std::string_view::npos
                                          ? 0 : slash + 1));
  }
  return safe_name(std::move(value));
}

DownloadFile::~DownloadFile() { abort(); }

bool DownloadFile::begin(std::string filename, std::string* error) {
  abort();
  filename_ = safe_name(std::move(filename));
  const std::string dir = downloads_dir();
  if (!ensure_dir(dir)) {
    if (error) *error = "cannot create Downloads directory";
    return false;
  }
  static std::atomic<uint64_t> sequence{0};
  const uint64_t serial = ++sequence;
  const uint64_t now = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path folder(dir);
  std::error_code ec;
  for (unsigned n = 0; n < 100; ++n) {
    const fs::path temp = folder / (".xanatoo-" + std::to_string(now) + "-" +
        std::to_string(serial) + "-" + std::to_string(n) + ".part");
    if (fs::exists(temp, ec)) continue;
    file_.open(temp, std::ios::binary | std::ios::out | std::ios::trunc);
    if (file_) {
      temporary_ = temp.string();
      return true;
    }
  }
  if (error) *error = "cannot create temporary download file";
  return false;
}

bool DownloadFile::append(std::string_view bytes, std::string* error) {
  if (!file_.is_open()) return false;
  file_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (file_) return true;
  if (error) *error = "could not write downloaded data";
  return false;
}

bool DownloadFile::finish(std::string* error) {
  if (!file_.is_open()) return false;
  file_.flush();
  const bool ok = static_cast<bool>(file_);
  file_.close();
  if (!ok) {
    if (error) *error = "could not flush downloaded data";
    abort();
    return false;
  }
  const fs::path dir = fs::path(temporary_).parent_path();
  for (unsigned n = 0; n < 10000; ++n) {
    const fs::path dest = dir / numbered_name(filename_, n);
    std::error_code ec;
    // Hard-link creation is atomic and fails if a file already exists.
    fs::create_hard_link(temporary_, dest, ec);
    if (ec) {
      if (fs::exists(dest)) continue;
      ec.clear();
      if (!fs::copy_file(temporary_, dest, fs::copy_options::none, ec)) {
        if (fs::exists(dest)) continue;
        if (error) *error = "could not save download: " + ec.message();
        abort();
        return false;
      }
    }
    path_ = dest.string();
    filename_ = dest.filename().string();
    fs::remove(temporary_, ec);
    temporary_.clear();
    return true;
  }
  if (error) *error = "too many files with the same name";
  abort();
  return false;
}

void DownloadFile::abort() {
  if (file_.is_open()) file_.close();
  if (!temporary_.empty()) {
    std::error_code ec;
    fs::remove(temporary_, ec);
    temporary_.clear();
  }
}

}  // namespace xanatoo
