// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <iosfwd>
#include <string>

namespace xanatoo {

// Command line (gamepkgstd OPTIONS.md); also documented in docs/xanatoo.1.
struct CliOptions {
  std::string font_path;
  std::string file_path;   // primary document
  std::string file_path2;  // optional second → dual-pane
  bool fullscreen = false;
  int width = 1024;
  int height = 768;
  float ui_scale = 0.f;  // 0 = auto (display density on Android, else 1)
  // Directories (paths.hpp); empty = default.
  std::string data_dir;
  std::string config_dir;
  std::string state_dir;
  std::string user_data_dir;
  std::string cache_dir;
  bool portable = false;
  bool no_save = false;
  bool show_paths = false;
};

enum class CliResult {
  Run,
  Exit,   // --help / --version printed: exit 0
  Error,  // bad arguments, reported on stderr: exit non-zero
};

CliResult parse_cli(int argc, char** argv, CliOptions& opt);

// Hand the directory options to paths.hpp (overrides, portable mode,
// --no-save); --show-paths prints them and returns Exit.
CliResult apply_path_options(const CliOptions& opt);

void print_usage(std::ostream& out, const char* argv0);

// <data dir>/examples/welcome.md, empty when missing.
std::string find_default_document();

// Minimal built-in doc when nothing else is available.
const char* fallback_document();

// Open http(s)/mailto via the platform handler (non-blocking best-effort).
void open_external_url(const std::string& url);

}  // namespace xanatoo
