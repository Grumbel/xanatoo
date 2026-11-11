// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Fixed-layout documents (PDF, DjVu, comic archives) as ordinary xanatoo
// documents of Page nodes, see docs/FIXED_LAYOUT.md. The builder is pure;
// `read_fixed_layout` asks thumtoo (XANATOO_THUMTOO) for the pages.

#include "xanatoo/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {
class Client;
}

namespace xanatoo {

// .pdf .djvu .djv .cbz .cbr .cb7 .cbt (case-insensitive) — local files only.
bool is_fixed_layout_path(std::string_view path);

// "page2" < "page10": digit runs compare by value, the rest by byte
// (case-insensitive). Comic archives list members in archive order.
bool natural_less(std::string_view a, std::string_view b);

// An http(s) answer that can be shown as pages: the file name to keep it
// under (with a fixed-layout extension), else empty. A page type (PDF,
// DjVu, comic book MIME) decides by itself; a generic binary type (empty,
// octet-stream, zip, rar, …) needs a fixed-layout file name. A text or
// HTML answer never counts, whatever its URL says. `Content-Disposition:
// attachment` does not stop it: viewing is the default, saving is the
// explicit "Download Link".
std::string fixed_layout_response_name(std::string_view url,
                                       std::string_view content_type,
                                       std::string_view disposition);

// This build can show `name` as pages (thumtoo with the needed backend:
// MuPDF for PDF, DjVuLibre for DjVu; archives always).
bool fixed_layout_supported(std::string_view name);

struct FixedPage {
  std::string uri;       // thumtoo location of the page
  std::string label;     // page number or member name
  int width = 0;         // 0 = thumtoo could not size it (see `error`)
  int height = 0;
  std::string error;
};

struct FixedOutlineEntry {
  int level = 1;
  std::string title;
  int page = 0;  // 1-based index into pages; 0 = no target
};

struct FixedLayoutSource {
  std::string title;
  std::vector<FixedPage> pages;
  std::vector<FixedOutlineEntry> outline;
};

// Pages in order, grouped into nested Sections by the outline. A page
// without a size becomes a notice paragraph, never a page of guessed size.
Document fixed_layout_document(const FixedLayoutSource& src);

// Expands `path` through thumtoo and waits until every page has its real
// size (slow for big solid archives, by design). nullopt + error when the
// file has no pages or thumtoo is not built in.
std::optional<FixedLayoutSource> read_fixed_layout(const std::string& path,
                                                   std::string* error);

// The thumtoo client used for pages (shared by the reader and the tile
// views), opened on first use; null without thumtoo or a cache dir.
std::shared_ptr<thumtoo::Client> page_client();
// Shutdown: drop the shared reference (workers join with the last owner).
void release_page_client();

}  // namespace xanatoo
