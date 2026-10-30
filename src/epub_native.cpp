// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Built-in EPUB reader for systems without pandoc (Android): zip container
// → OPF spine → chapter XHTML joined into one page → parse_html_page →
// Markdown, so the result takes the same route as a pandoc import.

#include "xanatoo/epub_import.hpp"

#include "xanatoo/html_min.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/inflate.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

namespace {

struct ZipEntry {
  size_t local_offset = 0;
  size_t comp_size = 0;
  int method = 0;
};
using ZipDir = std::map<std::string, ZipEntry, std::less<>>;

uint32_t le(std::string_view s, size_t pos, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i)
    v |= static_cast<uint32_t>(static_cast<unsigned char>(s[pos + i])) << (8 * i);
  return v;
}

// Central directory of a zip; nullopt when there is none. Zip64 is not
// read: a book's container stays far below its limits.
std::optional<ZipDir> zip_directory(std::string_view zip) {
  constexpr size_t kEocd = 22, kCentral = 46;
  if (zip.size() < kEocd) return std::nullopt;
  size_t eocd = zip.size() - kEocd;
  const size_t lowest = zip.size() > kEocd + 0xFFFF ? zip.size() - kEocd - 0xFFFF : 0;
  while (le(zip, eocd, 4) != 0x06054b50) {
    if (eocd == lowest) return std::nullopt;
    --eocd;
  }
  const uint32_t count = le(zip, eocd + 10, 2);
  size_t pos = le(zip, eocd + 16, 4);
  ZipDir dir;
  for (uint32_t i = 0; i < count; ++i) {
    if (pos > zip.size() || zip.size() - pos < kCentral ||
        le(zip, pos, 4) != 0x02014b50)
      return std::nullopt;
    const size_t name_len = le(zip, pos + 28, 2);
    const size_t extra_len = le(zip, pos + 30, 2);
    const size_t comment_len = le(zip, pos + 32, 2);
    if (zip.size() - pos - kCentral < name_len) return std::nullopt;
    ZipEntry e;
    e.method = static_cast<int>(le(zip, pos + 10, 2));
    e.comp_size = le(zip, pos + 20, 4);
    e.local_offset = le(zip, pos + 42, 4);
    dir.emplace(std::string(zip.substr(pos + kCentral, name_len)), e);
    pos += kCentral + name_len + extra_len + comment_len;
  }
  return dir;
}

std::optional<std::string> zip_read(std::string_view zip, const ZipDir& dir,
                                    std::string_view name) {
  auto it = dir.find(name);
  if (it == dir.end()) return std::nullopt;
  const ZipEntry& e = it->second;
  constexpr size_t kLocal = 30;
  if (e.local_offset > zip.size() || zip.size() - e.local_offset < kLocal ||
      le(zip, e.local_offset, 4) != 0x04034b50)
    return std::nullopt;
  const size_t data = e.local_offset + kLocal + le(zip, e.local_offset + 26, 2) +
                      le(zip, e.local_offset + 28, 2);
  if (data > zip.size() || zip.size() - data < e.comp_size) return std::nullopt;
  std::string_view body = zip.substr(data, e.comp_size);
  if (e.method == 0) return std::string(body);
  if (e.method == 8) return inflate_raw(body);
  return std::nullopt;
}

bool is_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
         c == ':' || c == '.';
}

// Calls fn(attrs) for each <name …> or <prefix:name …> in an XML document.
template <class Fn>
void for_each_tag(std::string_view xml, std::string_view name, Fn fn) {
  for (size_t lt = xml.find('<'); lt != std::string_view::npos;
       lt = xml.find('<', lt + 1)) {
    size_t end = lt + 1;
    while (end < xml.size() && is_name_char(xml[end])) ++end;
    std::string_view tag = xml.substr(lt + 1, end - lt - 1);
    if (size_t colon = tag.rfind(':'); colon != std::string_view::npos)
      tag.remove_prefix(colon + 1);
    if (tag != name) continue;
    const size_t gt = xml.find('>', end);
    if (gt == std::string_view::npos) return;
    fn(xml.substr(end, gt - end));
  }
}

// Text of the first <dc:name> element of the OPF.
std::string dc_text(std::string_view opf, std::string_view name) {
  const std::string open = "<dc:" + std::string(name);
  size_t start = opf.find(open);
  if (start == std::string_view::npos) return {};
  start = opf.find('>', start);
  if (start == std::string_view::npos) return {};
  const size_t end = opf.find("</dc:" + std::string(name), start);
  if (end == std::string_view::npos) return {};
  return html_to_plain(opf.substr(start + 1, end - start - 1));
}

std::string percent_decode(std::string_view s) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

// href relative to the zip member `from` → zip member name.
std::string zip_resolve(std::string_view from, std::string_view href) {
  const std::string decoded = percent_decode(html_decode_entities(std::string(href)));
  std::vector<std::string> parts;
  auto push = [&parts](std::string_view path) {
    size_t pos = 0;
    while (pos <= path.size()) {
      size_t slash = std::min(path.find('/', pos), path.size());
      std::string_view seg = path.substr(pos, slash - pos);
      if (seg == "..") {
        if (!parts.empty()) parts.pop_back();
      } else if (!seg.empty() && seg != ".") {
        parts.emplace_back(seg);
      }
      pos = slash + 1;
    }
  };
  if (size_t slash = from.rfind('/'); slash != std::string_view::npos)
    push(from.substr(0, slash));
  push(decoded);
  std::string out;
  for (const std::string& p : parts) {
    if (!out.empty()) out.push_back('/');
    out += p;
  }
  return out;
}

bool has_scheme(std::string_view href) {
  const size_t colon = href.find(':');
  return colon != std::string_view::npos &&
         href.find_first_of("/#?") > colon;
}

struct Book {
  std::map<std::string, std::string> media;    // zip member → media/ path
  std::map<std::string, size_t> chapters;      // zip member → spine index
};

// In a tag's attributes, the quoted value of ` name=`: [first, last).
std::optional<std::pair<size_t, size_t>> attr_value(std::string_view attrs,
                                                    std::string_view name) {
  const std::string key = std::string(name) + "=";
  for (size_t p = attrs.find(key); p != std::string_view::npos;
       p = attrs.find(key, p + 1)) {
    if (p == 0 || !std::isspace(static_cast<unsigned char>(attrs[p - 1])))
      continue;
    const size_t q = p + key.size();
    if (q >= attrs.size() || (attrs[q] != '"' && attrs[q] != '\'')) continue;
    const size_t end = attrs.find(attrs[q], q + 1);
    if (end == std::string_view::npos) return std::nullopt;
    return std::make_pair(q + 1, end);
  }
  return std::nullopt;
}

// A chapter's <body> content, made fit for one shared page: images point
// into media/, links between chapters become "#id" / "#ch-N", and XML
// self-closing tags (which HTML does not have) are written out.
std::string chapter_body(std::string_view xhtml, const std::string& member,
                         const Book& book) {
  size_t start = xhtml.find("<body");
  if (start != std::string_view::npos) start = xhtml.find('>', start);
  std::string_view body =
      start == std::string_view::npos ? xhtml : xhtml.substr(start + 1);
  if (size_t end = body.rfind("</body"); end != std::string_view::npos)
    body = body.substr(0, end);

  std::string out;
  out.reserve(body.size());
  size_t pos = 0;
  while (pos < body.size()) {
    const size_t lt = body.find('<', pos);
    if (lt == std::string_view::npos) break;
    const size_t gt = body.find('>', lt);
    if (gt == std::string_view::npos) break;
    out.append(body.substr(pos, lt - pos));
    pos = gt + 1;

    size_t name_end = lt + 1;
    while (name_end < gt && is_name_char(body[name_end])) ++name_end;
    const std::string tag(body.substr(lt + 1, name_end - lt - 1));
    const bool closed = body[gt - 1] == '/';
    std::string attrs(body.substr(name_end, gt - name_end - (closed ? 1 : 0)));

    const char* attr = tag == "img" ? "src" : tag == "a" ? "href" : nullptr;
    if (attr) {
      if (auto v = attr_value(attrs, attr)) {
        const std::string href = attrs.substr(v->first, v->second - v->first);
        std::string repl = href;
        if (tag == "img") {
          auto it = book.media.find(zip_resolve(member, href));
          if (it != book.media.end()) repl = it->second;
        } else if (!has_scheme(href) && !href.empty() && href[0] != '#') {
          const size_t hash = href.find('#');
          auto it = book.chapters.find(
              zip_resolve(member, std::string_view(href).substr(0, hash)));
          if (it != book.chapters.end())
            repl = hash != std::string::npos
                       ? href.substr(hash)
                       : "#ch-" + std::to_string(it->second);
        }
        attrs.replace(v->first, v->second - v->first, repl);
      }
    }
    out += '<';
    out += tag;
    out += attrs;
    const bool is_void = tag == "img" || tag == "br" || tag == "hr";
    if (closed && !is_void && !tag.empty()) {
      out += "></";
      out += tag;
      out += '>';
    } else {
      out += closed ? "/>" : ">";
    }
    if (tag.empty()) {  // comment, closing tag, …: leave as it was
      out.resize(out.size() - (attrs.size() + 2 + (closed ? 1 : 0)));
      out.append(body.substr(lt, gt + 1 - lt));
    }
  }
  if (pos < body.size()) out.append(body.substr(pos));
  return out;
}

// parse_html_page leaves images as "[image: alt]" links; a paragraph of
// nothing else becomes a real Image node.
void promote_images(Document& doc) {
  for (Node& n : doc.nodes) {
    if (n.kind != NodeKind::Paragraph || n.spans.size() != 1) continue;
    const InlineSpan& s = n.spans[0];
    if (s.kind != InlineKind::Link || !starts_with(s.href, "media/") ||
        !starts_with(s.text, "[image"))
      continue;
    std::string alt;
    if (starts_with(s.text, "[image: "))
      alt = s.text.substr(8, s.text.size() - 9);
    n.kind = NodeKind::Image;
    n.src = s.href;
    n.alt = std::move(alt);
    n.spans.clear();
  }
}

std::string yaml_quoted(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (c != '\n') out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

std::optional<EpubBook> epub_to_markdown(std::string_view zip, std::string* err) {
  auto fail = [err](const char* msg) -> std::optional<EpubBook> {
    if (err) *err = msg;
    return std::nullopt;
  };
  auto dir = zip_directory(zip);
  if (!dir) return fail("not a zip container");
  auto container = zip_read(zip, *dir, "META-INF/container.xml");
  if (!container) return fail("no META-INF/container.xml");

  std::string opf_path;
  for_each_tag(*container, "rootfile", [&](std::string_view attrs) {
    if (opf_path.empty()) opf_path = html_attr(attrs, "full-path");
  });
  auto opf = zip_read(zip, *dir, opf_path);
  if (opf_path.empty() || !opf) return fail("no package document");

  struct Item {
    std::string member, type;
  };
  std::map<std::string, Item> manifest;
  EpubBook out;
  Book book;
  for_each_tag(*opf, "item", [&](std::string_view attrs) {
    Item item{zip_resolve(opf_path, html_attr(attrs, "href")),
              html_attr(attrs, "media-type")};
    if (starts_with(item.type, "image/") && !book.media.count(item.member)) {
      if (auto bytes = zip_read(zip, *dir, item.member)) {
        std::string ext;
        const size_t slash = item.member.rfind('/');
        const size_t dot = item.member.rfind('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
          ext = ascii_lower(item.member.substr(dot));
        std::string name =
            "media/img" + std::to_string(book.media.size()) + ext;
        book.media.emplace(item.member, name);
        out.media.emplace_back(std::move(name), std::move(*bytes));
      }
    }
    manifest.emplace(html_attr(attrs, "id"), std::move(item));
  });

  std::vector<std::string> spine;
  for_each_tag(*opf, "itemref", [&](std::string_view attrs) {
    auto it = manifest.find(html_attr(attrs, "idref"));
    if (it == manifest.end()) return;
    const std::string& type = it->second.type;
    if (type != "application/xhtml+xml" && type != "text/html") return;
    book.chapters.emplace(it->second.member, spine.size());
    spine.push_back(it->second.member);
  });
  if (spine.empty()) return fail("empty spine");

  // Publisher font obfuscation is listed here too, but only chapters
  // matter: a listed chapter is DRM.
  if (auto enc = zip_read(zip, *dir, "META-INF/encryption.xml")) {
    bool drm = false;
    for_each_tag(*enc, "CipherReference", [&](std::string_view attrs) {
      if (book.chapters.count(html_attr(attrs, "URI"))) drm = true;
    });
    if (drm) return fail("the book is DRM protected");
  }

  std::string html = "<html><body><main>";
  for (size_t i = 0; i < spine.size(); ++i) {
    auto xhtml = zip_read(zip, *dir, spine[i]);
    if (!xhtml) continue;
    html += "<a id=\"ch-" + std::to_string(i) + "\"></a>";
    html += chapter_body(*xhtml, spine[i], book);
    html += '\n';
  }
  html += "</main></body></html>";

  Document doc = parse_html_page(html);
  promote_images(doc);

  const std::string title = dc_text(*opf, "title");
  const std::string author = dc_text(*opf, "creator");
  if (!title.empty() || !author.empty()) {
    out.markdown = "---\n";
    if (!title.empty()) out.markdown += "title: " + yaml_quoted(title) + "\n";
    if (!author.empty()) out.markdown += "author: " + yaml_quoted(author) + "\n";
    out.markdown += "---\n\n";
  }
  out.markdown += serialize_markup(doc);
  return out;
}

}  // namespace xanatoo
