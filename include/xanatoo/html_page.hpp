// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Whole HTML pages (web links, local .html files) → documents through the
// html_min subset: the main content only (<main>, a lone <article>, else
// <body>), page chrome dropped, headings as nested Sections. No CSS, no
// scripts, no layout — a reader view, not a browser.

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// .html / .htm / .xhtml
bool path_uses_html(std::string_view path);

// http(s) URL that is worth fetching as a page: not an obvious download
// (.pdf, .zip, images, audio, video, …), which stays with the system browser.
bool is_web_url(std::string_view s);

// "external:https://…" links open in the system browser.
inline constexpr std::string_view kExternalPrefix = "external:";

// og:title, else <title>; plain text, empty when absent.
std::string html_page_title(std::string_view html);

// Page → Document: a title Section (unless the content has its own <h1>)
// holding the main content. Remote pages (`page_url` http/https) start
// with a "host · Open in browser" line; relative links resolve against
// <base href> or `page_url`.
Document parse_html_page(std::string_view html, std::string_view page_url = {});

// text/plain body: blank-line separated chunks as code blocks.
Document plain_text_document(std::string_view text);

#if defined(XANATOO_WEB) && XANATOO_WEB
// GET (redirects followed) and convert by Content-Type: HTML pages, feeds,
// text/plain, text/markdown, text/gemini. `final_url` receives the URL
// after redirects (fragment dropped).
std::optional<Document> web_fetch(std::string_view url, std::string* err,
                                  std::string* final_url = nullptr);
#endif

}  // namespace xanatoo
