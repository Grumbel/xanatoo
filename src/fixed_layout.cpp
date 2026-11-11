// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Fixed-layout documents → Page nodes (pure part, docs/FIXED_LAYOUT.md).

#include "xanatoo/fixed_layout.hpp"

#include "xanatoo/download.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>

namespace xanatoo {

bool is_fixed_layout_path(std::string_view path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string_view::npos || path.find("://") != std::string_view::npos)
    return false;
  if (path.find('/', dot) != std::string_view::npos) return false;
  const std::string ext = ascii_lower(path.substr(dot));
  for (const char* e : {".pdf", ".djvu", ".djv", ".cbz", ".cbr", ".cb7", ".cbt"})
    if (ext == e) return true;
  return false;
}

bool natural_less(std::string_view a, std::string_view b) {
  size_t i = 0, j = 0;
  auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)); };
  while (i < a.size() && j < b.size()) {
    if (digit(a[i]) && digit(b[j])) {
      size_t ie = i, je = j;
      while (ie < a.size() && digit(a[ie])) ++ie;
      while (je < b.size() && digit(b[je])) ++je;
      // Compare by value: skip leading zeros, then length, then digits.
      size_t is = i, js = j;
      while (is + 1 < ie && a[is] == '0') ++is;
      while (js + 1 < je && b[js] == '0') ++js;
      if (ie - is != je - js) return ie - is < je - js;
      const int c = a.substr(is, ie - is).compare(b.substr(js, je - js));
      if (c != 0) return c < 0;
      if (ie - i != je - j) return ie - i < je - j;  // same value: "1" < "01"
      i = ie;
      j = je;
      continue;
    }
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j])));
    if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
    ++i;
    ++j;
  }
  if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
  return a < b;  // equal ignoring case: a stable, total order
}

std::string fixed_layout_response_name(std::string_view url,
                                       std::string_view content_type,
                                       std::string_view disposition) {
  std::string type = ascii_lower(content_type.substr(0, content_type.find(';')));
  std::string_view tv = type;
  trim_ascii(tv);
  type = std::string(tv);
  std::string name = download_filename(url, disposition);
  struct PageType {
    const char* mime;
    const char* ext;
  };
  static constexpr PageType kPageTypes[] = {
      {"application/pdf", ".pdf"},
      {"application/x-pdf", ".pdf"},
      {"image/vnd.djvu", ".djvu"},
      {"image/x-djvu", ".djvu"},
      {"image/djvu", ".djvu"},
      {"application/vnd.comicbook+zip", ".cbz"},
      {"application/x-cbz", ".cbz"},
      {"application/vnd.comicbook-rar", ".cbr"},
      {"application/x-cbr", ".cbr"},
      {"application/x-cb7", ".cb7"},
      {"application/x-cbt", ".cbt"},
  };
  const size_t dot = name.rfind('.');
  const std::string ext =
      dot == std::string::npos ? std::string() : ascii_lower(std::string_view(name).substr(dot));
  for (const PageType& pt : kPageTypes) {
    if (type != pt.mime) continue;
    // Keep the server's name when it already says what it is; the
    // extension is what thumtoo and is_fixed_layout_path go by.
    const bool named = ext == pt.ext || (ext == ".djv" && std::string_view(pt.ext) == ".djvu");
    if (!named) name += pt.ext;
    return name;
  }
  static constexpr const char* kGeneric[] = {
      "", "application/octet-stream", "binary/octet-stream",
      "application/download", "application/force-download",
      "application/x-download", "application/zip", "application/x-zip",
      "application/x-zip-compressed", "application/vnd.rar",
      "application/x-rar", "application/x-rar-compressed",
      "application/x-7z-compressed", "application/x-tar",
  };
  for (const char* g : kGeneric)
    if (type == g) return is_fixed_layout_path(name) ? name : std::string();
  return {};
}

namespace {

void add_paragraph(Document& doc, NodeId parent, const std::string& text) {
  NodeId p = doc.create(NodeKind::Paragraph, parent);
  doc.append_text(p, text);
}

void add_page(Document& doc, NodeId parent, const FixedPage& page, int number) {
  if (page.width <= 0 || page.height <= 0) {
    add_paragraph(doc, parent,
                  "Page " + std::to_string(number) + " (" + page.label +
                      ") could not be read" +
                      (page.error.empty() ? std::string(".") : ": " + page.error));
    return;
  }
  NodeId n = doc.create(NodeKind::Page, parent);
  Node& pg = doc.get(n);
  pg.src = page.uri;
  pg.width = page.width;
  pg.height = page.height;
  pg.alt = page.label;
  // Markdown attribute values are quoted with '"'.
  std::replace(pg.alt.begin(), pg.alt.end(), '"', '\'');
  pg.label = "page-" + std::to_string(number);
}

}  // namespace

Document fixed_layout_document(const FixedLayoutSource& src) {
  Document doc;
  const NodeId root = doc.create(NodeKind::Document);
  doc.meta.title = src.title;
  const int n = static_cast<int>(src.pages.size());

  struct Open {
    int level;
    NodeId section;
  };
  std::vector<Open> open;
  auto container = [&] { return open.empty() ? root : open.back().section; };
  int next = 1;  // next page to place
  auto place_until = [&](int last) {
    for (; next <= std::min(last, n); ++next)
      add_page(doc, container(), src.pages[static_cast<size_t>(next - 1)], next);
  };

  for (const FixedOutlineEntry& e : src.outline) {
    // Pages before the entry's target belong to the section still open.
    if (e.page > 0) place_until(e.page - 1);
    const int level = std::clamp(e.level, 1, 16);
    while (!open.empty() && open.back().level >= level) open.pop_back();
    const NodeId sec = doc.create(NodeKind::Section, container());
    doc.get(sec).level = level;
    const NodeId head = doc.create(NodeKind::Header, sec);
    doc.get(head).level = level;
    doc.append_text(head, e.title.empty() ? std::string("(untitled)") : e.title);
    open.push_back({level, sec});
  }
  place_until(n);
  if (n == 0) add_paragraph(doc, root, "This document has no pages.");
  return doc;
}

#if !(defined(XANATOO_THUMTOO) && XANATOO_THUMTOO)
std::optional<FixedLayoutSource> read_fixed_layout(const std::string&,
                                                   std::string* error) {
  if (error) *error = "PDF, DjVu and comic archives need a build with thumtoo";
  return std::nullopt;
}

bool fixed_layout_supported(std::string_view) { return false; }
std::shared_ptr<thumtoo::Client> page_client() { return nullptr; }
void release_page_client() {}
#endif

}  // namespace xanatoo
