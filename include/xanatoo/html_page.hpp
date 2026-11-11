// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Whole HTML pages (web links, local .html files) → documents through the
// html_min subset: the main content (<main>, a lone <article>, else
// <body>) with the page chrome around and inside it as Chrome nodes,
// headings as nested Sections. No CSS, no
// scripts, no layout — a reader, not a browser.

#include "xanatoo/types.hpp"
#include "xanatoo/download.hpp"

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// .html / .htm / .xhtml
bool path_uses_html(std::string_view path);

// http(s) URL to fetch in-app; unsupported response types become downloads.
// Recognized image URLs continue through the external/image-viewer route.
bool is_web_url(std::string_view s);

// "external:https://…" links open in the system browser.
inline constexpr std::string_view kExternalPrefix = "external:";

// og:title, else <title>; plain text, empty when absent.
std::string html_page_title(std::string_view html);

// Page → Document: a title Section (unless the content has its own <h1>)
// holding the main content. Remote pages (`page_url` http/https) start
// with a "host · Open in browser" line; relative links resolve against
// <base href> or `page_url`. Page chrome (navigation, sidebars, footers,
// what lies around <main>) is kept as Chrome nodes, for reader mode to hide;
// `keep_chrome = false` drops it instead (EPUB chapters).
Document parse_html_page(std::string_view html, std::string_view page_url = {},
                         bool keep_chrome = true);

// text/plain body: blank-line separated chunks as code blocks.
Document plain_text_document(std::string_view text);

// "HTTP 404 Not Found" (reason phrase only for the common codes).
std::string http_status_line(int status);

#if defined(XANATOO_WEB) && XANATOO_WEB
// GET (redirects followed) and convert by Content-Type: HTML pages, feeds,
// text/plain, text/markdown, text/gemini. `final_url` receives the URL
// after redirects (fragment dropped). A 4xx / 5xx answer with a readable
// body returns that body as the document *and* sets `err` and
// `http_status`, so the caller can show it under an error banner.
//
// `pages` (may be null): an answer that can be shown as pages (PDF, DjVu,
// comic archive; `fixed_layout_response_name`) streams into
// cache_dir()/documents/ instead of Downloads; web_fetch then returns
// nullopt with `pages->path` set and the caller opens that file.
// `force_download`: every 200 answer goes to Downloads ("Download Link").
struct WebPagesFile {
  std::string path;  // local copy, set once complete
  std::string name;  // file name the server gave (document title)
  const std::atomic<bool>* stop = nullptr;  // the view gave up waiting
};
std::optional<Document> web_fetch(std::string_view url, std::string* err,
                                  std::string* final_url = nullptr,
                                  int* http_status = nullptr,
                                  std::string* raw_source = nullptr,
                                  DownloadProgress* download = nullptr,
                                  WebPagesFile* pages = nullptr,
                                  bool force_download = false);
#endif

}  // namespace xanatoo
