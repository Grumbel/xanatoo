// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/cli.hpp"
#include "xanatoo/paths.hpp"

#ifdef __ANDROID__
#include <SDL.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef XANATOO_VERSION
#define XANATOO_VERSION "unknown"
#endif

namespace xanatoo {
namespace fs = std::filesystem;

const char* fallback_document() {
  return "# Untitled\n\n"
         "Open a document with `xanatoo file.md`, or point `--data-dir` "
         "at a directory with `examples/welcome.md`.\n\n"
         "Press **F1** for the keyboard reference.\n";
}

std::string find_default_document() {
  return data_file("examples/welcome.md");
}

void print_usage(std::ostream& out, const char* argv0) {
  out << "Usage: " << argv0 << " [options] [file] [file2]\n"
      << "\n"
      << "Interactive document composition engine.\n"
      << "\n"
      << "One path (or gemini://, https://, … URL) opens that document; two\n"
      << "open side by side. With none, examples/welcome.md from the data\n"
      << "directory opens. .html opens as a read-only reader view; .epub,\n"
      << ".docx, .odt, .rst, .tex, … are imported to Markdown via pandoc.\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help            Show this help\n"
      << "  -v, --version         Show the version\n"
      << "  -f, --fullscreen      Start fullscreen\n"
      << "  -w, --windowed        Start windowed\n"
      << "  -g, --geometry WxH    Window size (default 1024x768)\n"
      << "  -o, --open PATH       Open PATH (same as a positional path)\n"
      << "      --font PATH       TTF/OTF font for body text (else XANATOO_FONT,\n"
      << "                        then FontConfig)\n"
      << "      --ui-scale N      Chrome and default text scale (default 1;\n"
      << "                        Android: dpi/160)\n"
      << "\n"
      << "Directories:\n"
      << "      --data-dir DIR    Installed data: examples, help, style sheets,\n"
      << "                        icons (default <exe>/../share/xanatoo)\n"
      << "      --config-dir DIR  Settings (view.conf)\n"
      << "      --state-dir DIR   Drafts, revisions, bookmarks, history\n"
      << "      --user-data-dir DIR  Your own style sheets (styles/)\n"
      << "      --cache-dir DIR   Thumbnails, downloads, imports\n"
      << "      --portable        Keep everything in <exe dir>/userdata/ (also\n"
      << "                        when portable.txt is next to the executable)\n"
      << "      --no-save         Write no settings or state this run\n"
      << "      --show-paths      Print every directory and where it came from\n";
}

namespace {

// --name VALUE / --name=VALUE / -x VALUE. Returns true when argv[i] is this
// option (value in *out, i advanced past it); sets err on a missing value.
bool value_opt(int argc, char** argv, int& i, const char* shrt,
               const char* lng, std::string* out, bool& err) {
  const std::string a = argv[i];
  const std::string eq = std::string(lng) + "=";
  if (a.rfind(eq, 0) == 0) {
    *out = a.substr(eq.size());
    return true;
  }
  if (a != lng && !(shrt && a == shrt)) return false;
  if (i + 1 >= argc) {
    std::cerr << "xanatoo: " << a << " requires a value\n";
    err = true;
    return true;
  }
  *out = argv[++i];
  return true;
}

bool parse_geometry(const std::string& v, int& w, int& h) {
  char* end = nullptr;
  const long pw = std::strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || (*end != 'x' && *end != 'X')) return false;
  const char* hs = end + 1;
  const long ph = std::strtol(hs, &end, 10);
  if (end == hs || *end != '\0' || pw <= 0 || ph <= 0 || pw > 32768 ||
      ph > 32768)
    return false;
  w = static_cast<int>(pw);
  h = static_cast<int>(ph);
  return true;
}

}  // namespace

CliResult parse_cli(int argc, char** argv, CliOptions& opt) {
  bool options_done = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (options_done || a.empty() || a[0] != '-' || a == "-") {
      if (opt.file_path.empty()) {
        opt.file_path = a;
      } else if (opt.file_path2.empty()) {
        opt.file_path2 = a;
      } else {
        std::cerr << "xanatoo: unexpected argument: " << a << "\n";
        return CliResult::Error;
      }
      continue;
    }
    if (a == "--") {
      options_done = true;
      continue;
    }
    if (a == "-h" || a == "--help") {
      print_usage(std::cout, argv[0]);
      return CliResult::Exit;
    }
    if (a == "-v" || a == "--version") {
      std::cout << "xanatoo " << XANATOO_VERSION << "\n";
      return CliResult::Exit;
    }
    if (a == "-f" || a == "--fullscreen") {
      opt.fullscreen = true;
      continue;
    }
    if (a == "-w" || a == "--windowed") {
      opt.fullscreen = false;
      continue;
    }
    if (a == "--portable") {
      opt.portable = true;
      continue;
    }
    if (a == "--no-save") {
      opt.no_save = true;
      continue;
    }
    if (a == "--show-paths") {
      opt.show_paths = true;
      continue;
    }
    bool err = false;
    std::string v;
    if (value_opt(argc, argv, i, "-g", "--geometry", &v, err)) {
      if (!err && !parse_geometry(v, opt.width, opt.height)) {
        std::cerr << "xanatoo: --geometry: expected WxH, got '" << v << "'\n";
        err = true;
      }
    } else if (value_opt(argc, argv, i, nullptr, "--ui-scale", &v, err)) {
      char* end = nullptr;
      opt.ui_scale = err ? 0.f : std::strtof(v.c_str(), &end);
      if (!err && (end == v.c_str() || *end != '\0' || opt.ui_scale <= 0.f)) {
        std::cerr << "xanatoo: --ui-scale: expected a positive number, got '"
                  << v << "'\n";
        err = true;
      }
    } else if (value_opt(argc, argv, i, nullptr, "--font", &opt.font_path, err) ||
               value_opt(argc, argv, i, "-o", "--open", &opt.file_path, err) ||
               value_opt(argc, argv, i, nullptr, "--data-dir", &opt.data_dir, err) ||
               value_opt(argc, argv, i, nullptr, "--config-dir", &opt.config_dir, err) ||
               value_opt(argc, argv, i, nullptr, "--state-dir", &opt.state_dir, err) ||
               value_opt(argc, argv, i, nullptr, "--user-data-dir", &opt.user_data_dir, err) ||
               value_opt(argc, argv, i, nullptr, "--cache-dir", &opt.cache_dir, err)) {
    } else {
      std::cerr << "xanatoo: unknown option: " << a << " (see --help)\n";
      return CliResult::Error;
    }
    if (err) return CliResult::Error;
  }
  return CliResult::Run;
}

CliResult apply_path_options(const CliOptions& opt) {
  auto absolute = [](const std::string& p) {
    std::error_code ec;
    const fs::path a = fs::absolute(p, ec);
    return ec ? p : a.lexically_normal().string();
  };
  if (!opt.data_dir.empty()) set_data_dir(absolute(opt.data_dir), "option");
  const std::pair<DirKind, const std::string*> dirs[] = {
      {DirKind::Config, &opt.config_dir},
      {DirKind::State, &opt.state_dir},
      {DirKind::UserData, &opt.user_data_dir},
      {DirKind::Cache, &opt.cache_dir},
  };
  for (const auto& [kind, path] : dirs)
    if (!path->empty()) set_dir_override(kind, absolute(*path));
  if ((opt.portable || portable_marker_present()) && !enable_portable_mode())
    return CliResult::Error;
  set_no_save(opt.no_save);
  if (opt.show_paths) {
    print_paths(std::cout);
    return CliResult::Exit;
  }
  return CliResult::Run;
}

void open_external_url(const std::string& url_in) {
  // "external:https://…" forces the system browser for an in-app URL.
  const std::string url = url_in.rfind("external:", 0) == 0
                              ? url_in.substr(9)
                              : url_in;
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0 &&
      url.rfind("mailto:", 0) != 0) {
    std::cerr << "Refusing to open non-http(s)/mailto URL: " << url << "\n";
    return;
  }

#if defined(__ANDROID__)
  if (SDL_OpenURL(url.c_str()) != 0)
    std::cerr << "SDL_OpenURL failed: " << SDL_GetError() << "\n";
#elif defined(__linux__)
  pid_t pid = fork();
  if (pid == 0) {
    // Double-fork so the browser is reparented and does not leave a zombie.
    if (fork() > 0) _exit(0);
    setsid();
    execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  } else if (pid > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
  } else {
    std::cerr << "fork failed for xdg-open\n";
  }
#elif defined(__APPLE__)
  pid_t pid = fork();
  if (pid == 0) {
    if (fork() > 0) _exit(0);
    setsid();
    execlp("open", "open", url.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  } else if (pid > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
  }
#else
  std::cout << "Open URL not implemented on this platform: " << url << "\n";
#endif
}

}  // namespace xanatoo
