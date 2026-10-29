// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/paths.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace xanatoo {
namespace fs = std::filesystem;
namespace {

constexpr size_t kKinds = 4;

struct Kind {
  const char* name;     // --show-paths label
  const char* xdg_var;  // XDG_*_HOME
  const char* xdg_def;  // default below $HOME
  const char* portable; // <exe dir>/userdata/<this>
};

constexpr std::array<Kind, kKinds> kKind = {{
    {"config", "XDG_CONFIG_HOME", ".config", "config"},
    {"state", "XDG_STATE_HOME", ".local/state", "state"},
    {"user-data", "XDG_DATA_HOME", ".local/share", "data"},
    {"cache", "XDG_CACHE_HOME", ".cache", "cache"},
}};

std::array<std::string, kKinds> g_override;
std::array<std::string, kKinds> g_platform;
std::array<bool, kKinds> g_reported{};
std::string g_portable_root;
bool g_no_save = false;
std::string g_data_dir;
std::string g_data_source;

size_t idx(DirKind k) { return static_cast<size_t>(k); }

bool absolute(const char* p) { return p && p[0] == '/'; }

}  // namespace

void set_dir_override(DirKind kind, std::string path) {
  g_override[idx(kind)] = std::move(path);
}

void set_platform_dir(DirKind kind, std::string path) {
  g_platform[idx(kind)] = std::move(path);
}

bool portable_marker_present() {
  const std::string exe = executable_dir();
  std::error_code ec;
  return !exe.empty() && fs::exists(fs::path(exe) / "portable.txt", ec);
}

bool enable_portable_mode() {
  const std::string exe = executable_dir();
  if (exe.empty()) {
    std::cerr << "xanatoo: portable mode: cannot determine the executable's "
                 "directory\n";
    return false;
  }
  const fs::path root = fs::path(exe) / "userdata";
  std::error_code ec;
  fs::create_directories(root, ec);
  const fs::path probe = root / ".write-test";
  bool ok = !ec && static_cast<bool>(std::ofstream(probe));
  fs::remove(probe, ec);
  if (!ok) {
    std::cerr << "xanatoo: portable mode: " << root.string()
              << " is not writable\n";
    return false;
  }
  g_portable_root = root.string();
  return true;
}

void set_no_save(bool on) { g_no_save = on; }
bool no_save() { return g_no_save; }

DirInfo dir_info(DirKind kind) {
  const size_t i = idx(kind);
  const Kind& k = kKind[i];
  if (!g_override[i].empty()) return {g_override[i], "option"};
  if (!g_portable_root.empty())
    return {(fs::path(g_portable_root) / k.portable).string(), "portable"};
  if (!g_platform[i].empty()) return {g_platform[i], "platform"};
  // An XDG variable counts only when it is an absolute path (DIR-6).
  if (const char* xdg = std::getenv(k.xdg_var); absolute(xdg))
    return {(fs::path(xdg) / "xanatoo").string(), k.xdg_var};
  if (const char* home = std::getenv("HOME"); absolute(home))
    return {(fs::path(home) / k.xdg_def / "xanatoo").string(), "default"};
  if (!g_reported[i]) {
    g_reported[i] = true;
    std::cerr << "xanatoo: no " << k.name << " directory: neither "
              << k.xdg_var << " nor HOME is an absolute path\n";
  }
  return {};
}

std::string config_dir() { return dir_info(DirKind::Config).path; }
std::string state_dir() { return dir_info(DirKind::State).path; }
std::string user_data_dir() { return dir_info(DirKind::UserData).path; }
std::string cache_dir() { return dir_info(DirKind::Cache).path; }

bool ensure_dir(const std::string& dir) {
  if (dir.empty()) return false;
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    std::cerr << "xanatoo: cannot create " << dir << ": " << ec.message()
              << "\n";
    return false;
  }
  return true;
}

void set_data_dir(std::string path, std::string source) {
  g_data_dir = std::move(path);
  g_data_source = std::move(source);
}

DirInfo data_dir_info() {
  if (!g_data_dir.empty()) return {g_data_dir, g_data_source};
  std::error_code ec;
  if (const std::string exe = executable_dir(); !exe.empty()) {
    // Installed (<prefix>/bin/xanatoo) or the build tree, which keeps the
    // binary at its root next to share/xanatoo.
    for (const char* rel : {"../share/xanatoo", "share/xanatoo"}) {
      const fs::path p = fs::weakly_canonical(fs::path(exe) / rel, ec);
      if (!ec && fs::is_directory(p, ec)) return {p.string(), "executable"};
    }
  }
#ifdef XANATOO_DATA_INSTALL_DIR
  if (fs::is_directory(XANATOO_DATA_INSTALL_DIR, ec))
    return {XANATOO_DATA_INSTALL_DIR, "install path"};
#endif
  return {};
}

std::string data_dir() { return data_dir_info().path; }

std::string data_file(std::string_view rel) {
  const std::string dir = data_dir();
  if (dir.empty()) return {};
  std::error_code ec;
  // Canonical, so the build tree's symlinks resolve to the source files.
  const fs::path p = fs::weakly_canonical(fs::path(dir) / rel, ec);
  return !ec && fs::exists(p, ec) ? p.string() : std::string();
}

std::string executable_dir() {
#if defined(__linux__) && !defined(__ANDROID__)
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return exe.parent_path().string();
#endif
  return {};
}

void print_paths(std::ostream& out) {
  auto line = [&](const char* name, const DirInfo& d) {
    out << name << std::string(11 - std::string_view(name).size(), ' ')
        << (d.path.empty() ? "(none)" : d.path);
    if (!d.source.empty()) out << "  (" << d.source << ")";
    out << "\n";
  };
  for (size_t i = 0; i < kKinds; ++i)
    line(kKind[i].name, dir_info(static_cast<DirKind>(i)));
  line("data", data_dir_info());
  if (g_no_save) out << "--no-save: no config or state is written\n";
}

}  // namespace xanatoo
