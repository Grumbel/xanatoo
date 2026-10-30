// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/epub_import.hpp"

#include "xanatoo/paths.hpp"
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

namespace {

// Extension → pandoc reader. Markdown, Org, gemtext and HTML are read
// natively and stay out of this table.
struct PandocFormat {
  std::string_view ext;
  std::string_view reader;
};
constexpr PandocFormat kPandocFormats[] = {
    {".epub", "epub"},         {".docx", "docx"},       {".odt", "odt"},
    {".rtf", "rtf"},           {".rst", "rst"},         {".tex", "latex"},
    {".latex", "latex"},       {".typ", "typst"},       {".textile", "textile"},
    {".wiki", "mediawiki"},    {".mediawiki", "mediawiki"},
    {".dokuwiki", "dokuwiki"}, {".dbk", "docbook"},     {".docbook", "docbook"},
    {".fb2", "fb2"},           {".ipynb", "ipynb"},     {".opml", "opml"},
    {".man", "man"},           {".pod", "pod"},         {".t2t", "t2t"},
    {".muse", "muse"},         {".creole", "creole"},   {".dj", "djot"},
    {".djot", "djot"},         {".csv", "csv"},         {".tsv", "tsv"},
    {".bib", "bibtex"},        {".ris", "ris"},         {".jira", "jira"},
};

}  // namespace

std::string pandoc_input_format(std::string_view path) {
  for (const PandocFormat& f : kPandocFormats)
    if (path_ends_with_ci(path, f.ext)) return std::string(f.reader);
  return {};
}

bool is_pandoc_path(std::string_view path) {
  return !pandoc_input_format(path).empty();
}

std::vector<std::string> pandoc_extensions() {
  std::vector<std::string> out;
  for (const PandocFormat& f : kPandocFormats) out.emplace_back(f.ext);
  return out;
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
  // ~/.cache/xanatoo/epub-import/<stem>-<hash-of-abspath>/ (EPUB; other
  // formats under pandoc-import/)
  const char* sub = is_epub_path(epub_path) ? "epub-import" : "pandoc-import";
  const std::string cache = cache_dir();
  if (cache.empty()) return {};
  const fs::path base = fs::path(cache) / sub;

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

bool pandoc_available() {
  int rc = std::system("command -v pandoc >/dev/null 2>&1");
  return rc == 0;
}

// Lua filter for every pandoc import: drop raw HTML blocks / inlines.
// The EPUB reader keeps elements it does not map (<hgroup>, <footer>,
// <article>, empty <svg> cover wrappers, …) as RawBlock "html", and the
// gfm writer prints those even with -raw_html. Their content is already
// separate blocks, so only the tag lines go. See docs/PANDOC.md.
static constexpr const char* kStripRawHtmlLua =
    "function RawBlock(b) if b.format == 'html' then return {} end end\n"
    "function RawInline(b) if b.format == 'html' then return {} end end\n";

static bool is_ascii_space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// True if s[i..] starts with a UTF-8 U+00A0 (C2 A0).
static bool is_nbsp_utf8(std::string_view s, size_t i) {
  return i + 1 < s.size() &&
         static_cast<unsigned char>(s[i]) == 0xC2 &&
         static_cast<unsigned char>(s[i + 1]) == 0xA0;
}

// Content of a span is only whitespace / NBSP / empty HTML entities.
static bool is_ws_only_html_content(std::string_view inner) {
  size_t i = 0;
  while (i < inner.size()) {
    if (is_ascii_space(static_cast<unsigned char>(inner[i]))) {
      ++i;
      continue;
    }
    if (is_nbsp_utf8(inner, i)) {
      i += 2;
      continue;
    }
    // &nbsp; &#160; &#xa0; &#xA0;
    if (inner[i] == '&') {
      auto rest = inner.substr(i);
      if (rest.size() >= 6 &&
          (rest.compare(0, 6, "&nbsp;") == 0)) {
        i += 6;
        continue;
      }
      if (rest.size() >= 6 &&
          (rest.compare(0, 6, "&#160;") == 0)) {
        i += 6;
        continue;
      }
      if (rest.size() >= 6 &&
          (rest.compare(0, 6, "&#xa0;") == 0 ||
           rest.compare(0, 6, "&#xA0;") == 0)) {
        i += 6;
        continue;
      }
      if (rest.size() >= 7 &&
          (rest.compare(0, 7, "&#x00a0;") == 0 ||
           rest.compare(0, 7, "&#x00A0;") == 0)) {
        i += 7;
        continue;
      }
    }
    return false;
  }
  return true;
}

// Case-insensitive match of tag name at s[pos] (pos points past '<').
static bool tag_name_is(std::string_view s, size_t pos, const char* name) {
  for (size_t k = 0; name[k]; ++k) {
    if (pos + k >= s.size()) return false;
    char c = s[pos + k];
    char n = name[k];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != n) return false;
  }
  size_t after = pos + std::char_traits<char>::length(name);
  if (after >= s.size()) return false;
  char c = s[after];
  return is_ascii_space(static_cast<unsigned char>(c)) || c == '>' ||
         c == '/';
}

// Find end of an HTML open/close tag starting at '<' (returns index of '>').
static size_t find_tag_close(std::string_view s, size_t lt) {
  bool quote = false;
  char q = 0;
  for (size_t i = lt + 1; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == q) quote = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = true;
      q = c;
      continue;
    }
    if (c == '>') return i;
  }
  return std::string::npos;
}

std::string sanitize_epub_markdown(std::string md) {
  // 1) Decode common NBSP forms to ordinary space (keeps word separation).
  {
    std::string out;
    out.reserve(md.size());
    for (size_t i = 0; i < md.size();) {
      if (is_nbsp_utf8(md, i)) {
        out.push_back(' ');
        i += 2;
        continue;
      }
      if (md[i] == '&') {
        auto rest = std::string_view(md).substr(i);
        if (rest.size() >= 6 && rest.compare(0, 6, "&nbsp;") == 0) {
          out.push_back(' ');
          i += 6;
          continue;
        }
        if (rest.size() >= 6 && rest.compare(0, 6, "&#160;") == 0) {
          out.push_back(' ');
          i += 6;
          continue;
        }
        if (rest.size() >= 6 &&
            (rest.compare(0, 6, "&#xa0;") == 0 ||
             rest.compare(0, 6, "&#xA0;") == 0)) {
          out.push_back(' ');
          i += 6;
          continue;
        }
      }
      out.push_back(md[i]);
      ++i;
    }
    md.swap(out);
  }

  // 2) Repeatedly strip empty / whitespace-only <span …>…</span> and empty
  //    <a id=…></a> / <a …/> page markers. Also unwrap simple <span>text</span>
  //    with no nested tags (drops class="calibreN" wrappers, keeps text).
  //    Limit passes so pathological nesting cannot loop forever.
  for (int pass = 0; pass < 32; ++pass) {
    std::string out;
    out.reserve(md.size());
    bool changed = false;
    size_t i = 0;
    while (i < md.size()) {
      if (md[i] != '<') {
        out.push_back(md[i]);
        ++i;
        continue;
      }

      // Closing tags we do not handle alone — copy through unless part of pair.
      size_t gt = find_tag_close(md, i);
      if (gt == std::string::npos) {
        out.append(md, i, std::string::npos);
        break;
      }

      // Self-closing empty anchor: <a id="…" /> or <a …/>
      if (tag_name_is(md, i + 1, "a")) {
        std::string_view tag(md.data() + i, gt - i + 1);
        bool self_close = false;
        if (tag.size() >= 2 && tag[tag.size() - 2] == '/') self_close = true;
        // Empty pair: <a …></a>
        if (!self_close) {
          size_t after = gt + 1;
          // skip whitespace between
          size_t j = after;
          while (j < md.size() && is_ascii_space(static_cast<unsigned char>(md[j])))
            ++j;
          if (j + 3 < md.size() && md[j] == '<' && md[j + 1] == '/' &&
              tag_name_is(md, j + 2, "a")) {
            size_t gt2 = find_tag_close(md, j);
            if (gt2 != std::string::npos) {
              // drop the whole empty anchor pair
              i = gt2 + 1;
              changed = true;
              continue;
            }
          }
        } else {
          // self-closing <a …/>
          i = gt + 1;
          changed = true;
          continue;
        }
      }

      if (tag_name_is(md, i + 1, "span")) {
        // Find matching </span> with no nested '<' inside (simple unwrap / drop).
        size_t after_open = gt + 1;
        size_t close = after_open;
        bool nested = false;
        while (close < md.size()) {
          if (md[close] == '<') {
            if (close + 6 < md.size() && md[close + 1] == '/' &&
                tag_name_is(md, close + 2, "span")) {
              break;
            }
            nested = true;
            break;
          }
          ++close;
        }
        if (!nested && close < md.size()) {
          size_t gt2 = find_tag_close(md, close);
          if (gt2 != std::string::npos) {
            std::string_view inner(md.data() + after_open, close - after_open);
            if (is_ws_only_html_content(inner)) {
              // Calibre often uses empty/nbsp spans as word separators —
              // keep a single space so "word<span>&nbsp;</span>word" stays
              // two words. Purely empty <span></span> yields no space.
              if (!inner.empty())
                out.push_back(' ');
              i = gt2 + 1;
              changed = true;
              continue;
            }
            // Unwrap: keep inner text, drop the span tags.
            out.append(inner);
            i = gt2 + 1;
            changed = true;
            continue;
          }
        }
      }

      // Default: copy the open tag through unchanged.
      out.append(md, i, gt - i + 1);
      i = gt + 1;
    }
    md.swap(out);
    if (!changed) break;
  }

  return md;
}

static bool write_text_file(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(out);
}

static std::string read_text_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

EpubImportResult import_epub_via_pandoc(const std::string& epub_path) {
  if (!is_epub_path(epub_path)) {
    EpubImportResult out;
    out.error = "Not an EPUB path";
    return out;
  }
  return import_via_pandoc(epub_path);
}

ImportResult import_via_pandoc(const std::string& epub_path) {
  ImportResult out;
  const std::string reader = pandoc_input_format(epub_path);
  if (epub_path.empty() || reader.empty()) {
    out.error = "No pandoc reader for " + epub_path;
    return out;
  }
  if (!fs::exists(epub_path)) {
    out.error = "File not found: " + epub_path;
    return out;
  }
  const bool native = !pandoc_available() && is_epub_path(epub_path);
  if (!native && !pandoc_available()) {
    out.error = "pandoc not found on PATH (install pandoc to import " +
                reader + " → Markdown)";
    return out;
  }

  fs::path dir = import_cache_dir(epub_path);
  if (dir.empty()) {
    out.error = "no cache directory for the import";
    return out;
  }
  fs::path md = dir / "content.md";
  fs::path media = dir / "media";

  try {
    fs::create_directories(dir);
  } catch (const std::exception& e) {
    out.error = std::string("Cannot create import cache: ") + e.what();
    return out;
  }

  // Reuse derived Markdown when it is at least as new as the EPUB and was
  // made by the current conversion (bump kFormat when the pandoc call
  // changes; the stamp file holds it).
  const char* const kFormat = native ? "native 1" : "3 strip-raw-html";
  const fs::path stamp = dir / ".format";
  bool reuse = false;
  try {
    if (fs::exists(md) && read_text_file(stamp) == kFormat) {
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

    if (native) {
      std::string zip = read_text_file(epub_path);
      std::string err;
      auto book = epub_to_markdown(zip, &err);
      if (!book) {
        out.error = "EPUB import failed: " + err;
        return out;
      }
      std::error_code ec;
      fs::create_directories(media, ec);
      for (const auto& [name, bytes] : book->media) {
        if (!write_file_atomic((dir / name).string(), bytes)) {
          out.error = "Cannot write " + (dir / name).string();
          return out;
        }
      }
      if (!write_text_file(md, book->markdown)) {
        out.error = "Cannot write " + md.string();
        return out;
      }
      write_text_file(stamp, kFormat);
    } else {
      const fs::path filter = dir / "strip-raw-html.lua";
      if (!write_text_file(filter, kStripRawHtmlLua)) {
        out.error = "Cannot write pandoc filter: " + filter.string();
        return out;
      }

      // pandoc book.epub -f epub -t gfm-raw_html+yaml_metadata_block -s
      //   -L strip-raw-html.lua -o content.md --extract-media=media
      // (-s writes the title / author / … as YAML front matter.)
      // Run with cwd = cache dir so relative media paths in the MD resolve.
      std::ostringstream cmd;
      cmd << "cd " << shell_quote(dir.string()) << " && "
          << "pandoc " << shell_quote(fs::absolute(epub_path).string())
          // -raw_html: unwrap div/span/section markup instead of passing it
          // through (EPUB XHTML leaves attribute lines in the text).
          << " -f " << reader << " -t gfm-raw_html+yaml_metadata_block -s"
          << " -L " << shell_quote(filter.filename().string())
          << " -o " << shell_quote(md.filename().string())
          << " --extract-media=" << shell_quote(media.filename().string())
          << " 2>/tmp/xanatoo-pandoc-epub.err";

      std::cout << "Importing " << reader << " via pandoc → " << md.string()
                << "\n";
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
      write_text_file(stamp, kFormat);
    }
  } else {
    std::cout << "Reusing import cache: " << md.string() << "\n";
  }

  // Always sanitize (including cache hits) so older imports lose calibre HTML.
  {
    std::string raw = read_text_file(md);
    if (raw.empty() && !fs::exists(md)) {
      out.error = "Derived Markdown missing after import";
      return out;
    }
    std::string cleaned = sanitize_epub_markdown(std::move(raw));
    if (!write_text_file(md, cleaned)) {
      out.error = "Cannot write sanitized Markdown: " + md.string();
      return out;
    }
  }

  out.ok = true;
  out.markdown_path = md.string();
  out.media_dir = media.string();
  return out;
}

}  // namespace xanatoo
