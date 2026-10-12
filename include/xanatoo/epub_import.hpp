// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>
#include <string_view>

namespace xanatoo {

// EPUB is treated as an immutable source. Import derives Markdown + media
// via an external `pandoc` on PATH (EPUB → GFM). The editor opens the
// derived .md; the original .epub is never written back.

bool is_epub_path(std::string_view path);

struct EpubImportResult {
  bool ok = false;
  std::string markdown_path;  // derived content.md
  std::string media_dir;      // --extract-media target
  std::string error;
};

// Convert epub_path to Markdown under the cache dir (or reuse if up to date).
// Requires `pandoc` on PATH. On failure, ok=false and error is set.
// Always runs sanitize_epub_markdown on the derived Markdown (including
// cache hits) so older imports pick up cleaner HTML stripping.
EpubImportResult import_epub_via_pandoc(const std::string& epub_path);

// Strip Calibre / EPUB residual HTML that pandoc leaves in GFM output:
// empty or whitespace-only <span class="calibreN">…</span>, empty page
// anchors, &nbsp; / U+00A0, and unwrap simple span wrappers that only
// carry class attributes. Safe on already-clean Markdown.
std::string sanitize_epub_markdown(std::string md);

}  // namespace xanatoo
