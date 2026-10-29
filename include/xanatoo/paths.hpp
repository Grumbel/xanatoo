// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Where xanatoo reads and writes files (gamepkgstd DIRECTORIES.md). One
// place resolves every directory, from (highest first) the command line
// (--config-dir, …), portable mode, the platform layer (Android internal
// storage) and the XDG variables. Lookups are not cached, so a changed
// environment applies at once.
//
//   config     view.conf                         $XDG_CONFIG_HOME/xanatoo
//   state      revisions, sidestore, bookmarks,  $XDG_STATE_HOME/xanatoo
//              minibuffer history
//   user data  user style sheets (styles/)       $XDG_DATA_HOME/xanatoo
//   cache      thumbnails, imports, downloads    $XDG_CACHE_HOME/xanatoo
//
// Installed data (examples, help, style sheets, icons, fonts) is separate:
// --data-dir, else <exe>/../share/xanatoo (or <exe>/share/xanatoo in the
// build tree), else the compiled-in install path. Never the current working directory (DIR-3).

#include <iosfwd>
#include <string>
#include <string_view>

namespace xanatoo {

enum class DirKind { Config, State, UserData, Cache };

struct DirInfo {
  std::string path;    // empty: not usable (error printed)
  std::string source;  // "option", "portable", "XDG_CONFIG_HOME", "default", …
};

// --config-dir / --state-dir / --user-data-dir / --cache-dir. Overriding
// one never moves another.
void set_dir_override(DirKind kind, std::string path);
// Platform default (Android), used when no option / portable mode is set.
void set_platform_dir(DirKind kind, std::string path);

// Portable mode (--portable or portable.txt next to the executable):
// <exe dir>/userdata/{config,state,data,cache}. False (error printed) when
// that is not writable or the executable's directory is unknown.
bool enable_portable_mode();
bool portable_marker_present();

// --no-save: no config or state is written this run.
void set_no_save(bool on);
bool no_save();

DirInfo dir_info(DirKind kind);
std::string config_dir();
std::string state_dir();
std::string user_data_dir();
std::string cache_dir();

// Create a directory before the first write into it. False, with an error
// naming the path on stderr, when that fails.
bool ensure_dir(const std::string& dir);

// Installed data: --data-dir (or the platform layer), then
// <exe>/../share/xanatoo (build tree: <exe>/share/xanatoo), then the
// compiled-in install path.
void set_data_dir(std::string path, std::string source);
DirInfo data_dir_info();
std::string data_dir();
// <data dir>/<rel> when it exists, else empty.
std::string data_file(std::string_view rel);

// Directory of the running executable; empty where unknown.
std::string executable_dir();

// --show-paths: every resolved directory and where it came from.
void print_paths(std::ostream& out);

}  // namespace xanatoo
