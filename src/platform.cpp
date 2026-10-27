// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/platform.hpp"

#include <SDL.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#endif

namespace xanatoo {

#ifdef __ANDROID__

namespace {

namespace fs = std::filesystem;

// std::cout / std::cerr → logcat (tag "xanatoo"), one entry per line.
class LogcatBuf : public std::streambuf {
 public:
  explicit LogcatBuf(int prio) : prio_(prio) {}

 protected:
  int overflow(int c) override {
    if (c == EOF) return 0;
    if (c == '\n') {
      flush_line();
    } else {
      line_.push_back(static_cast<char>(c));
    }
    return c;
  }
  // std::cerr is unit-buffered (flush after every <<): keep collecting until
  // the newline so one message stays one logcat entry.
  int sync() override { return 0; }

 private:
  void flush_line() {
    __android_log_write(prio_, "xanatoo", line_.c_str());
    line_.clear();
  }
  int prio_;
  std::string line_;
};

// Whole asset (or any SDL_RWops path) as a string; empty when missing.
std::string read_asset(const std::string& path) {
  SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
  if (!rw) return {};
  std::string data;
  char buf[16384];
  size_t n;
  while ((n = SDL_RWread(rw, buf, 1, sizeof(buf))) > 0) data.append(buf, n);
  SDL_RWclose(rw);
  return data;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// assets/xanatoo/MANIFEST: first line a stamp (version + content hash),
// then one relative path per line. Copy everything when the stamp differs
// from the last unpacked one (user edits to shipped files are overwritten
// on upgrade; their own files are left alone).
void unpack_assets(const fs::path& dest) {
  const std::string manifest = read_asset("xanatoo/MANIFEST");
  if (manifest.empty()) {
    SDL_Log("xanatoo: no assets/xanatoo/MANIFEST in the APK");
    return;
  }
  const fs::path stamp_file = dest / "MANIFEST";
  if (read_file(stamp_file) == manifest) return;
  std::istringstream lines(manifest);
  std::string rel;
  std::getline(lines, rel);  // stamp
  int count = 0;
  while (std::getline(lines, rel)) {
    if (rel.empty()) continue;
    const std::string data = read_asset("xanatoo/" + rel);
    const fs::path out = dest / rel;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (f) ++count;
  }
  std::ofstream(stamp_file, std::ios::binary | std::ios::trunc) << manifest;
  SDL_Log("xanatoo: unpacked %d data files to %s", count, dest.c_str());
}

}  // namespace

void platform_pre_init() {
  // Back arrives as SDLK_AC_BACK instead of finishing the activity (which
  // would drop unsaved edits); on_keydown treats it like Escape.
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
}

void platform_init() {
  static LogcatBuf out_buf(ANDROID_LOG_INFO), err_buf(ANDROID_LOG_WARN);
  std::cout.rdbuf(&out_buf);
  std::cerr.rdbuf(&err_buf);

  // No HOME / writable /tmp on Android: point them at the app's private
  // storage so config_dir(), view.conf, history and caches just work.
  const char* internal = SDL_AndroidGetInternalStoragePath();
  if (!internal) return;
  const fs::path home = internal;
  setenv("HOME", home.c_str(), 1);
  std::error_code ec;
  fs::create_directories(home / "tmp", ec);
  setenv("TMPDIR", (home / "tmp").c_str(), 1);

  const fs::path data = home / "data";
  fs::create_directories(data, ec);
  unpack_assets(data);
  setenv("XANATOO_DATA_DIR", data.c_str(), 1);
  setenv("XANATOO_ICON_DIR", (data / "icons").c_str(), 1);
  // Relative lookups (examples/…, styles/, icons/) and new files land here.
  if (chdir(data.c_str()) != 0)
    SDL_Log("xanatoo: chdir %s failed", data.c_str());
}

void platform_request_keyboard() { SDL_StartTextInput(); }
void platform_release_keyboard() { SDL_StopTextInput(); }

bool platform_text_input_at_start() { return false; }

#else

void platform_pre_init() {}
void platform_init() {}
void platform_request_keyboard() {}
void platform_release_keyboard() {}
bool platform_text_input_at_start() { return true; }

#endif

}  // namespace xanatoo
