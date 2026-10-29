// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/html_page.hpp"

#include "xanatoo/feed.hpp"
#include "xanatoo/html_min.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"

#if defined(XANATOO_WEB) && XANATOO_WEB
#include "xanatoo/https.hpp"
#include "xanatoo/version.hpp"
#endif

#include <array>
#include <cctype>
#include <iostream>

namespace xanatoo {

namespace {

// One opening tag found by scan_tags.
struct TagRef {
  size_t lt = 0;   // '<'
  size_t gt = 0;   // '>'
  std::string name;
  std::string_view attrs;
};

std::string lower_name(std::string_view s) {
  size_t e = 0;
  while (e < s.size() &&
         (std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '-' ||
          s[e] == ':'))
    ++e;
  return ascii_lower(std::string(s.substr(0, e)));
}

// Visit opening tags in order (comments and script / style bodies
// skipped) until `fn` returns true; returns that tag.
template <typename Fn>
std::optional<TagRef> scan_tags(std::string_view html, Fn fn) {
  size_t i = 0;
  while ((i = html.find('<', i)) != std::string_view::npos) {
    if (html.compare(i, 4, "<!--") == 0) {
      size_t e = html.find("-->", i + 4);
      if (e == std::string_view::npos) break;
      i = e + 3;
      continue;
    }
    if (i + 1 >= html.size() ||
        !std::isalpha(static_cast<unsigned char>(html[i + 1]))) {
      ++i;
      continue;
    }
    size_t gt = html_tag_end(html, i);
    if (gt == std::string_view::npos) break;
    TagRef t;
    t.lt = i;
    t.gt = gt;
    t.name = lower_name(html.substr(i + 1));
    t.attrs = html.substr(i + 1 + t.name.size(), gt - i - 1 - t.name.size());
    if (fn(t)) return t;
    if (t.name == "script" || t.name == "style") {
      i = html_element_end(html, gt + 1, t.name);
      continue;
    }
    i = gt + 1;
  }
  return std::nullopt;
}

std::string_view element_inner(std::string_view html, const TagRef& t) {
  size_t end = html_element_end(html, t.gt + 1, t.name);
  return html.substr(t.gt + 1, end - t.gt - 1);
}

// <main> or role="main", else the only <article>, else <body>.
std::string_view main_content(std::string_view html) {
  if (auto m = scan_tags(html, [](const TagRef& t) {
        return t.name == "main" ||
               ascii_lower(html_attr(t.attrs, "role")) == "main";
      }))
    return element_inner(html, *m);
  int articles = 0;
  std::optional<TagRef> first;
  scan_tags(html, [&](const TagRef& t) {
    if (t.name == "article" && articles++ == 0) first = t;
    return false;
  });
  if (articles == 1) return element_inner(html, *first);
  if (auto b = scan_tags(html, [](const TagRef& t) { return t.name == "body"; }))
    return element_inner(html, *b);
  return html;
}

bool is_remote(std::string_view url) {
  return starts_with_ci(url, "https://") || starts_with_ci(url, "http://");
}

std::string url_host(std::string_view url) {
  size_t s = url.find("://");
  if (s == std::string_view::npos) return {};
  s += 3;
  size_t e = url.find_first_of("/?#", s);
  std::string host(url.substr(s, e == std::string_view::npos ? e : e - s));
  if (starts_with(host, "www.")) host.erase(0, 4);
  return host;
}

InlineSpan span(InlineKind kind, std::string text, std::string href = {}) {
  InlineSpan s;
  s.kind = kind;
  s.text = std::move(text);
  s.href = std::move(href);
  return s;
}

}  // namespace

bool path_uses_html(std::string_view path) {
  return path_ends_with_ci(path, ".html") || path_ends_with_ci(path, ".htm") ||
         path_ends_with_ci(path, ".xhtml");
}

bool is_web_url(std::string_view s) {
  if (!is_remote(s)) return false;
  // Path without query / fragment, for the extension test.
  size_t host = s.find("://") + 3;
  size_t slash = s.find('/', host);
  if (slash == std::string_view::npos) return true;
  std::string_view path = s.substr(slash);
  path = path.substr(0, path.find_first_of("?#"));
  static constexpr std::array<std::string_view, 40> kDownloads = {
      ".pdf",  ".zip",  ".gz",   ".tgz",  ".xz",   ".bz2",  ".7z",   ".rar",
      ".tar",  ".exe",  ".dmg",  ".iso",  ".apk",  ".deb",  ".rpm",  ".msi",
      ".mp3",  ".mp4",  ".m4a",  ".ogg",  ".opus", ".flac", ".wav",  ".webm",
      ".mkv",  ".avi",  ".mov",  ".png",  ".jpg",  ".jpeg", ".gif",  ".webp",
      ".svg",  ".ico",  ".bmp",  ".tiff", ".docx", ".xlsx", ".pptx", ".odt"};
  for (std::string_view ext : kDownloads)
    if (path_ends_with_ci(path, ext)) return false;
  return true;
}

std::string html_page_title(std::string_view html) {
  std::string title;
  scan_tags(html, [&](const TagRef& t) {
    if (t.name == "meta" &&
        ascii_lower(html_attr(t.attrs, "property")) == "og:title") {
      title = html_to_plain(html_attr(t.attrs, "content"));
      return !title.empty();
    }
    if (t.name == "title" && title.empty()) {
      size_t e = html.find("</", t.gt + 1);
      title = html_to_plain(html.substr(t.gt + 1, e == std::string_view::npos
                                                      ? 0
                                                      : e - t.gt - 1));
    }
    return t.name == "body";  // og:title lives in <head>
  });
  return title;
}

Document parse_html_page(std::string_view html, std::string_view page_url) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);

  HtmlOptions opt;
  opt.base_url = std::string(page_url);
  opt.page_url = std::string(page_url);
  if (auto b = scan_tags(html, [](const TagRef& t) {
        return t.name == "base" || t.name == "body";
      });
      b && b->name == "base") {
    std::string href = html_attr(b->attrs, "href");
    if (!href.empty()) opt.base_url = resolve_url(page_url, href);
  }
  opt.sections = true;
  opt.anchors = true;
  opt.drop_chrome = true;

  if (is_remote(page_url)) {
    NodeId p = doc.create(NodeKind::Paragraph, root);
    auto& spans = doc.get(p).spans;
    spans.push_back(span(InlineKind::Emphasis, url_host(page_url) + " · "));
    spans.push_back(span(InlineKind::Link, "Open in browser",
                         std::string(kExternalPrefix) + std::string(page_url)));
  }

  std::string_view body = main_content(html);
  const bool has_h1 = scan_tags(body, [](const TagRef& t) {
                        return t.name == "h1";
                      }).has_value();
  NodeId parent = root;
  doc.meta.title = html_page_title(html);
  if (std::string title = doc.meta.title; !title.empty() && !has_h1) {
    parent = doc.create(NodeKind::Section, root);
    doc.get(parent).level = 1;
    NodeId h = doc.create(NodeKind::Header, parent);
    doc.get(h).level = 1;
    doc.get(h).spans.push_back(span(InlineKind::Text, std::move(title)));
  }
  const size_t before = doc.nodes.size();
  html_append_blocks(doc, parent, body, opt);
  if (doc.nodes.size() == before) {
    NodeId p = doc.create(NodeKind::Paragraph, parent);
    doc.get(p).spans.push_back(span(
        InlineKind::Emphasis,
        "No readable content (the page may need JavaScript)."));
  }
  return doc;
}

Document plain_text_document(std::string_view text) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  // Blank-line separated chunks: small blocks keep layout virtualised.
  std::string chunk;
  auto flush = [&] {
    while (!chunk.empty() && chunk.back() == '\n') chunk.pop_back();
    if (!chunk.empty()) {
      NodeId n = doc.create(NodeKind::CodeBlock, root);
      doc.get(n).spans.push_back(span(InlineKind::Text, std::move(chunk)));
    }
    chunk.clear();
  };
  size_t i = 0;
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(i, eol - i);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\f'))
      line.remove_suffix(1);
    if (line.find_first_not_of(" \t\f") == std::string_view::npos) {
      flush();
    } else {
      chunk.append(line);
      chunk.push_back('\n');
    }
    i = eol + 1;
  }
  flush();
  return doc;
}

#if defined(XANATOO_WEB) && XANATOO_WEB

std::optional<Document> web_fetch(std::string_view url_in, std::string* err,
                                  std::string* final_url) {
  static const std::string kUserAgent =
      std::string("Mozilla/5.0 (compatible; xanatoo/") + version_string() +
      "; +https://github.com/Grumbel/xanatoo)";
  std::string url(url_in.substr(0, url_in.find('#')));
  std::cerr << "Web: GET " << url << "\n";
  std::string fin;
  auto resp = https_get_follow(url, kUserAgent, err, &fin);
  if (!resp) return std::nullopt;
  if (final_url) *final_url = fin;
  if (resp->status != 200) {
    if (err) *err = "HTTP " + std::to_string(resp->status);
    return std::nullopt;
  }
  std::string type = ascii_lower(resp->content_type);
  type = type.substr(0, type.find(';'));
  std::string_view tv = type;
  trim_ascii(tv);
  type = std::string(tv);
  const std::string charset = detect_charset(resp->content_type, resp->body);
  std::string body = to_utf8(std::move(resp->body), charset);

  if (looks_like_feed(body)) return parse_feed(body, fin, err);
  if (type.empty() || type == "text/html" || type == "application/xhtml+xml")
    return parse_html_page(body, fin);
  if (type == "text/markdown" || type == "text/x-markdown")
    return parse_document(body, "remote.md");
  if (type == "text/gemini") return parse_gemtext(body);
  if (starts_with(type, "text/") || type == "application/json")
    return plain_text_document(body);
  if (err) *err = "cannot show " + type + " (" + std::to_string(body.size()) + " bytes)";
  return std::nullopt;
}

#endif  // XANATOO_WEB

}  // namespace xanatoo
