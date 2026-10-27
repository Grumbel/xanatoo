// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/cli.hpp"

#ifdef __ANDROID__
#include <SDL.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace xanatoo {
namespace fs = std::filesystem;

const char* fallback_document() {
  return "# Untitled\n\n"
         "Open a document with `xanatoo file.md`, or place "
         "`examples/welcome.md` next to the binary.\n\n"
         "Press **F1** for the keyboard reference.\n";
}

std::string find_default_document() {
  // Prefer a real file so relative image paths resolve.
  const char* candidates[] = {
      "examples/welcome.md",
      "../examples/welcome.md",
      "../../examples/welcome.md",
      nullptr,
  };
  for (const char** p = candidates; *p; ++p) {
    if (fs::exists(*p)) return fs::absolute(*p).string();
  }
#ifdef __linux__
  try {
    fs::path exe = fs::read_symlink("/proc/self/exe");
    fs::path dir = exe.parent_path();
    const fs::path near[] = {
        dir / "examples" / "welcome.md",
        dir / ".." / "examples" / "welcome.md",
        dir / ".." / "share" / "xanatoo" / "welcome.md",
        dir / ".." / "share" / "xanatoo" / "examples" / "welcome.md",
    };
    for (const auto& c : near) {
      std::error_code ec;
      auto canon = fs::weakly_canonical(c, ec);
      if (!ec && fs::exists(canon)) return canon.string();
    }
  } catch (...) {
  }
#endif
  return {};
}

void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options] [file] [file2]\n"
      << "\n"
      << "Options:\n"
      << "  -f, --font <path>     TTF/OTF font file (body + headings)\n"
      << "  -o, --open <path>     Open a markup document\n"
      << "  -h, --help            Show this help\n"
      << "\n"
      << "With no file, opens examples/welcome.md when found.\n"
      << "One positional path opens that document (or gemini:// URL).\n"
      << ".html opens as a read-only reader view; .epub, .docx, .odt, .rtf,\n"
      << ".rst, .tex, .typ, .wiki, .ipynb, .csv, … are imported via pandoc to\n"
      << "Markdown (original unchanged).\n"
      << "Two paths open dual-pane: left=file1, right=file2.\n"
      << "Ctrl+S saves the focused pane's document.\n"
      << "Ctrl+\\ toggles split; drag the gutter to resize panes.\n"
      << "\n"
      << "  -F, --fullscreen      Borderless fullscreen\n"
      << "  --ui-scale <n>        chrome + default text scale (default: 1; Android: dpi/160)\n"
      << "\n"
      << "Font resolution: --font / XANATOO_FONT, then FontConfig\n"
      << "(regular/bold/italic/mono faces for styled text).\n";
}

bool parse_cli(int argc, char** argv, CliOptions& opt) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--fullscreen" || a == "-F") {
      opt.fullscreen = true;
      continue;
    }
    if (a == "--ui-scale") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a number\n";
        return false;
      }
      opt.ui_scale = std::strtof(argv[++i], nullptr);
      continue;
    }
    if (a.rfind("--ui-scale=", 0) == 0) {
      opt.ui_scale = std::strtof(a.c_str() + 11, nullptr);
      continue;
    }
    if (a == "-h" || a == "--help") {
      print_usage(argv[0]);
      return false;
    }
    if (a == "-f" || a == "--font") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.font_path = argv[++i];
      continue;
    }
    if (a.rfind("--font=", 0) == 0) {
      opt.font_path = a.substr(7);
      continue;
    }
    if (a == "-o" || a == "--open" || a == "--file") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.file_path = argv[++i];
      continue;
    }
    if (!a.empty() && a[0] != '-') {
      if (opt.file_path.empty()) {
        opt.file_path = a;
        continue;
      }
      if (opt.file_path2.empty()) {
        opt.file_path2 = a;
        continue;
      }
      std::cerr << "error: unexpected argument: " << a << "\n";
      print_usage(argv[0]);
      return false;
    }
    std::cerr << "error: unknown argument: " << a << "\n";
    print_usage(argv[0]);
    return false;
  }
  return true;
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
