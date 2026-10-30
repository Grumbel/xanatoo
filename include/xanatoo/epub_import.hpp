// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xanatoo {

// EPUB is treated as an immutable source. Import derives Markdown + media
// via an external `pandoc` on PATH (EPUB → GFM), or the built-in reader
// when pandoc is missing. The editor opens the
// derived .md; the original .epub is never written back.

bool is_epub_path(std::string_view path);

// Other formats pandoc reads (docx, odt, rtf, rst, LaTeX, typst, textile,
// MediaWiki, DokuWiki, DocBook, FB2, ipynb, OPML, man, CSV / TSV, …) take
// the same route: pandoc_input_format gives the reader for a path's
// extension, empty for formats xanatoo reads itself (md, org, gmi, html).
std::string pandoc_input_format(std::string_view path);
bool is_pandoc_path(std::string_view path);
// Every extension routed through pandoc (".epub", ".docx", …).
std::vector<std::string> pandoc_extensions();
// `pandoc` found on PATH (runs a shell; not for every frame).
bool pandoc_available();

struct ImportResult {
  bool ok = false;
  std::string markdown_path;  // derived content.md
  std::string media_dir;      // --extract-media target
  std::string error;
};
using EpubImportResult = ImportResult;

// Convert epub_path to Markdown under the cache dir (or reuse if up to date).
// EPUB falls back to the built-in reader, the rest requires `pandoc` on
// PATH. On failure, ok=false and error is set.
// Always runs sanitize_epub_markdown on the derived Markdown (including
// cache hits) so older imports pick up cleaner HTML stripping.
EpubImportResult import_epub_via_pandoc(const std::string& epub_path);
// Any is_pandoc_path file → cached Markdown (same rules as EPUB).
ImportResult import_via_pandoc(const std::string& path);

// Built-in EPUB reader, used by the imports when pandoc is missing (Android).
// `media` holds (path relative to the Markdown, bytes) of the book's images.
struct EpubBook {
  std::string markdown;
  std::vector<std::pair<std::string, std::string>> media;
};
std::optional<EpubBook> epub_to_markdown(std::string_view zip, std::string* err);

// Strip Calibre / EPUB residual HTML that pandoc leaves in GFM output:
// empty or whitespace-only <span class="calibreN">…</span>, empty page
// anchors, &nbsp; / U+00A0, and unwrap simple span wrappers that only
// carry class attributes. Safe on already-clean Markdown.
std::string sanitize_epub_markdown(std::string md);

}  // namespace xanatoo
