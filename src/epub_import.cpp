// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/epub_import.hpp"

#include "xanatoo/util.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace xanatoo {

bool is_epub_path(std::string_view path) {
  return path_ends_with_ci(path, ".epub");
}

static std::string shell_quote(const std::string& s) {
  // POSIX single-quote escaping: ' → '\''
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

static fs::path import_cache_dir(const std::string& epub_path) {
  // ~/.cache/xanatoo/epub-import/<stem>-<hash-of-abspath>/
  fs::path base;
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0]) {
    base = fs::path(xdg) / "xanatoo" / "epub-import";
  } else if (const char* home = std::getenv("HOME"); home && home[0]) {
    base = fs::path(home) / ".cache" / "xanatoo" / "epub-import";
  } else {
    base = fs::temp_directory_path() / "xanatoo-epub-import";
  }

  std::string abs = epub_path;
  try {
    abs = fs::weakly_canonical(epub_path).string();
  } catch (...) {
  }
  // Short stable-ish key from path (not cryptographic).
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : abs) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::ostringstream key;
  std::string stem = fs::path(epub_path).stem().string();
  if (stem.empty()) stem = "book";
  // Keep stem filesystem-safe and short.
  for (char& c : stem) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
      c = '_';
  }
  if (stem.size() > 40) stem.resize(40);
  key << stem << "-" << std::hex << h;
  return base / key.str();
}

static bool pandoc_available() {
  // portable-ish: command -v via shell
  int rc = std::system("command -v pandoc >/dev/null 2>&1");
  return rc == 0;
}

EpubImportResult import_epub_via_pandoc(const std::string& epub_path) {
  EpubImportResult out;
  if (epub_path.empty() || !is_epub_path(epub_path)) {
    out.error = "Not an EPUB path";
    return out;
  }
  if (!fs::exists(epub_path)) {
    out.error = "EPUB not found: " + epub_path;
    return out;
  }
  if (!pandoc_available()) {
    out.error =
        "pandoc not found on PATH (install pandoc to import EPUB → Markdown)";
    return out;
  }

  fs::path dir = import_cache_dir(epub_path);
  fs::path md = dir / "content.md";
  fs::path media = dir / "media";

  try {
    fs::create_directories(dir);
  } catch (const std::exception& e) {
    out.error = std::string("Cannot create import cache: ") + e.what();
    return out;
  }

  // Reuse derived Markdown when it is at least as new as the EPUB.
  bool reuse = false;
  try {
    if (fs::exists(md)) {
      auto epub_t = fs::last_write_time(epub_path);
      auto md_t = fs::last_write_time(md);
      if (md_t >= epub_t) reuse = true;
    }
  } catch (...) {
  }

  if (!reuse) {
    // Remove stale media so paths stay consistent.
    try {
      if (fs::exists(media)) fs::remove_all(media);
    } catch (...) {
    }

    // pandoc book.epub -f epub -t gfm -o content.md --extract-media=media
    // Run with cwd = cache dir so relative media paths in the MD resolve.
    std::ostringstream cmd;
    cmd << "cd " << shell_quote(dir.string()) << " && "
        << "pandoc " << shell_quote(fs::absolute(epub_path).string())
        << " -f epub -t gfm"
        << " -o " << shell_quote(md.filename().string())
        << " --extract-media=" << shell_quote(media.filename().string())
        << " 2>/tmp/xanatoo-pandoc-epub.err";

    std::cout << "Importing EPUB via pandoc → " << md.string() << "\n";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0 || !fs::exists(md)) {
      std::string detail;
      {
        std::ifstream err("/tmp/xanatoo-pandoc-epub.err");
        if (err) {
          std::ostringstream ss;
          ss << err.rdbuf();
          detail = ss.str();
          if (detail.size() > 400) detail.resize(400);
        }
      }
      out.error = "pandoc failed";
      if (!detail.empty()) out.error += ": " + detail;
      return out;
    }
  } else {
    std::cout << "Reusing EPUB import cache: " << md.string() << "\n";
  }

  out.ok = true;
  out.markdown_path = md.string();
  out.media_dir = media.string();
  return out;
}

}  // namespace xanatoo
