// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>

namespace glyphtoo {

struct CliOptions {
  std::string font_path;
  std::string file_path;   // primary document
  std::string file_path2;  // optional second → dual-pane
};

// Returns false if the process should exit (help printed or bad args).
bool parse_cli(int argc, char** argv, CliOptions& opt);

void print_usage(const char* argv0);

// Prefer examples/welcome.md near cwd or the executable.
std::string find_default_document();

// Minimal built-in doc when nothing else is available.
const char* fallback_document();

// Open http(s)/mailto via the platform handler (non-blocking best-effort).
void open_external_url(const std::string& url);

}  // namespace glyphtoo
