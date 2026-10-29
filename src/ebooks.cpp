// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Public-domain ebooks: Standard Ebooks catalogue pages are scraped into a
// book list; book links download the EPUB into the cache and import it
// through the existing pandoc EPUB path. Project Gutenberg book pages and
// any direct .epub link take the download path as well.

#include "xanatoo/ebooks.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include "xanatoo/epub_import.hpp"
#include "xanatoo/https.hpp"
#include "xanatoo/parse.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace xanatoo {

namespace {

constexpr std::string_view kSeOrigin = "https://standardebooks.org";

struct SplitUrl {
  std::string host;  // lower case, without "www."
  std::string path;  // "/…" (may be empty)
  std::string query;
};

std::optional<SplitUrl> split_url(std::string_view s) {
  trim_ascii(s);
  if (starts_with_ci(s, "https://")) s.remove_prefix(8);
  else if (starts_with_ci(s, "http://")) s.remove_prefix(7);
  else return std::nullopt;
  if (size_t h = s.find('#'); h != std::string_view::npos) s = s.substr(0, h);
  SplitUrl u;
  size_t slash = s.find_first_of("/?");
  u.host = ascii_lower(s.substr(0, slash));
  if (starts_with(u.host, "www.")) u.host.erase(0, 4);
  if (slash != std::string_view::npos) {
    std::string_view rest = s.substr(slash);
    size_t q = rest.find('?');
    u.path = std::string(rest.substr(0, q));
    if (q != std::string_view::npos) u.query = std::string(rest.substr(q + 1));
  }
  return u;
}

std::vector<std::string> path_segments(std::string_view path) {
  std::vector<std::string> segs;
  size_t p = 0;
  while (p < path.size()) {
    size_t e = path.find('/', p);
    if (e == std::string_view::npos) e = path.size();
    if (e > p) segs.emplace_back(path.substr(p, e - p));
    p = e + 1;
  }
  return segs;
}

bool all_digits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

// Text between `<tag…>` and the next '<' after `from`, decoded.
std::string text_after(std::string_view html, size_t from) {
  size_t gt = html.find('>', from);
  if (gt == std::string_view::npos) return {};
  size_t lt = html.find('<', gt + 1);
  std::string t = html_decode_entities(html.substr(gt + 1, lt - gt - 1));
  std::string_view v = t;
  trim_ascii(v);
  return std::string(v);
}

std::string attr_value(std::string_view html, size_t from,
                       std::string_view name) {
  std::string pat = std::string(name) + "=\"";
  size_t p = html.find(pat, from);
  if (p == std::string_view::npos) return {};
  p += pat.size();
  size_t e = html.find('"', p);
  return html_decode_entities(html.substr(p, e - p));
}

// Markdown link label (taken raw by the parser): only "](" would end it early.
std::string md_label(std::string_view s) {
  std::string o(s);
  for (size_t p = o.find("]("); p != std::string::npos; p = o.find("](", p))
    o.insert(p + 1, " ");
  return o;
}

std::string absolute_se(const std::string& href) {
  if (starts_with(href, "http")) return href;
  return std::string(kSeOrigin) + (starts_with(href, "/") ? "" : "/") + href;
}

}  // namespace

bool is_standard_ebooks_url(std::string_view s) {
  auto u = split_url(s);
  if (!u || u->host != "standardebooks.org") return false;
  return u->path == "/ebooks" || starts_with(u->path, "/ebooks/") ||
         starts_with(u->path, "/subjects/");
}

std::string ebook_download_url(std::string_view s) {
  auto u = split_url(s);
  if (!u) return {};
  const auto segs = path_segments(u->path);
  if (u->host == "standardebooks.org") {
    // /ebooks/<author>/<title>[/<translator>…][/downloads/<file>.epub]
    if (segs.size() < 3 || segs[0] != "ebooks") return {};
    std::vector<std::string> book;
    for (size_t i = 1; i < segs.size(); ++i) {
      if (segs[i] == "downloads" || segs[i] == "text") break;
      book.push_back(segs[i]);
    }
    if (book.size() < 2) return {};
    std::string path, slug;
    for (const auto& b : book) {
      path += "/" + b;
      slug += (slug.empty() ? "" : "_") + b;
    }
    // ?source=feed gets the file itself instead of the "thank you" page.
    return std::string(kSeOrigin) + "/ebooks" + path + "/downloads/" + slug +
           ".epub?source=feed";
  }
  if (u->host == "gutenberg.org") {
    // /ebooks/1342, /ebooks/1342.epub3.images, /cache/epub/1342/….epub
    if (segs.size() == 2 && segs[0] == "ebooks") {
      std::string_view id = segs[1];
      id = id.substr(0, id.find('.'));
      if (all_digits(id))
        return "https://www.gutenberg.org/ebooks/" + std::string(id) +
               ".epub3.images";
    }
  }
  if (path_ends_with_ci(u->path, ".epub"))
    return "https://" + std::string(s.substr(s.find("://") + 3));
  return {};
}

bool is_remote_epub_url(std::string_view s) {
  if (is_standard_ebooks_url(s)) return false;
  return !ebook_download_url(s).empty();
}

Document parse_standard_ebooks_listing(std::string_view html,
                                       std::string_view page_url) {
  std::string md;
  std::string title;
  if (size_t h1 = html.find("<h1"); h1 != std::string_view::npos)
    title = text_after(html, h1);
  auto u = split_url(page_url);
  std::string query;
  if (u) {
    size_t q = u->query.find("query=");
    if (q != std::string::npos) {
      query = u->query.substr(q + 6, u->query.find('&', q) - q - 6);
      for (char& c : query)
        if (c == '+') c = ' ';
    }
    if (starts_with(u->path, "/subjects/"))
      title = "Subject: " + u->path.substr(10);
  }
  if (title.empty() || title == "Browse Ebooks") title = "Standard Ebooks";
  if (!query.empty()) title += " — search “" + query + "”";
  md += "# " + title + "\n\n";

  int count = 0;
  const std::string_view kItem = "<li typeof=\"schema:Book\"";
  for (size_t p = html.find(kItem); p != std::string_view::npos;
       p = html.find(kItem, p + kItem.size())) {
    size_t end = html.find("</li>", p);
    std::string_view item = html.substr(p, end - p);
    std::string about = attr_value(item, 0, "about");
    size_t name = item.find("property=\"schema:name\"");
    if (about.empty() || name == std::string_view::npos) continue;
    std::string book = text_after(item, name);
    md += "- [" + md_label(book) + "](" + absolute_se(about) + ")";
    if (size_t a = item.find("class=\"author\""); a != std::string_view::npos) {
      std::string author_url = attr_value(item, a, "resource");
      size_t an = item.find("property=\"schema:name\"", a);
      if (an != std::string_view::npos) {
        std::string author = text_after(item, an);
        if (!author_url.empty())
          md += " — [" + md_label(author) + "](" + absolute_se(author_url) + ")";
        else
          md += " — " + md_label(author);
      }
    }
    md += "\n";
    ++count;
  }
  if (count == 0) md += "No books found on this page.\n";
  md += "\n";

  if (size_t next = html.find("rel=\"next\""); next != std::string_view::npos) {
    size_t a = html.rfind("<a ", next);
    if (a != std::string_view::npos) {
      std::string href = attr_value(html, a, "href");
      if (!href.empty()) md += "[Next page →](" + absolute_se(href) + ")\n\n";
    }
  }
  md += "Subjects: ";
  static const char* kSubjects[] = {"adventure", "fantasy",  "fiction",
                                    "horror",    "mystery",  "philosophy",
                                    "poetry",    "science-fiction", "shorts"};
  bool first = true;
  for (const char* s : kSubjects) {
    md += std::string(first ? "" : " · ") + "[" + s + "](" +
          std::string(kSeOrigin) + "/subjects/" + s + ")";
    first = false;
  }
  md += "\n\nClick a title to download its EPUB (imported via pandoc). "
        "Search with **Ctrl+L** `https://standardebooks.org/ebooks?query=…`\n";
  return parse_document(md, {});
}

#if defined(XANATOO_EBOOKS) && XANATOO_EBOOKS

namespace {

const char* kUserAgent =
    "xanatoo/0.1.0 (ebook reader; +https://github.com/Grumbel/xanatoo)";

fs::path download_dir() {
  const std::string cache = cache_dir();
  if (cache.empty()) return {};
  return fs::path(cache) / "downloads";
}

// "<host>_<last segment>.epub", filesystem-safe.
std::string cache_name(const std::string& url) {
  auto u = split_url(url);
  std::string name = u ? u->host : "book";
  auto segs = u ? path_segments(u->path) : std::vector<std::string>{};
  if (!segs.empty()) name += "_" + segs.back();
  for (char& c : name)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.'))
      c = '_';
  if (!path_ends_with_ci(name, ".epub")) name += ".epub";
  return name;
}

std::optional<Document> fetch_listing(const std::string& url, std::string* err) {
  std::cerr << "Ebooks: GET " << url << "\n";
  auto resp = https_get_follow(url, kUserAgent, err);
  if (!resp) return std::nullopt;
  if (resp->status != 200) {
    if (err) *err = "HTTP " + std::to_string(resp->status);
    return std::nullopt;
  }
  return parse_standard_ebooks_listing(resp->body, url);
}

std::optional<Document> fetch_book(const std::string& epub_url,
                                   std::string* err, std::string* local_path) {
  const fs::path dir = download_dir();
  if (dir.empty()) {
    if (err) *err = "no cache directory for downloads";
    return std::nullopt;
  }
  fs::path file = dir / cache_name(epub_url);
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    std::cerr << "Ebooks: GET " << epub_url << "\n";
    auto resp = https_get_follow(epub_url, kUserAgent, err);
    if (!resp) return std::nullopt;
    if (resp->status != 200) {
      if (err) *err = "HTTP " + std::to_string(resp->status);
      return std::nullopt;
    }
    if (!starts_with(resp->body, "PK")) {
      if (err) *err = "download is not an EPUB (" + resp->content_type + ")";
      return std::nullopt;
    }
    fs::create_directories(file.parent_path(), ec);
    // Write under a temp name so a half-written file is never reused.
    fs::path tmp = file;
    tmp += ".part";
    {
      std::ofstream out(tmp, std::ios::binary);
      out.write(resp->body.data(),
                static_cast<std::streamsize>(resp->body.size()));
      if (!out) {
        if (err) *err = "cannot write " + tmp.string();
        return std::nullopt;
      }
    }
    fs::rename(tmp, file, ec);
    std::cerr << "Ebooks: saved " << file << " (" << resp->body.size()
              << " bytes)\n";
  }
  auto imp = import_epub_via_pandoc(file.string());
  if (!imp.ok) {
    if (err) *err = imp.error;
    return std::nullopt;
  }
  std::ifstream in(imp.markdown_path);
  std::string source((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  if (local_path) *local_path = imp.markdown_path;
  return parse_document(source, imp.markdown_path);
}

}  // namespace

std::optional<Document> ebook_fetch(std::string_view url, std::string* err,
                                    std::string* local_path) {
  std::string epub = ebook_download_url(url);
  if (!epub.empty()) return fetch_book(epub, err, local_path);
  if (is_standard_ebooks_url(url)) {
    std::string u(url);
    if (starts_with_ci(u, "http://")) u = "https://" + u.substr(7);
    return fetch_listing(u, err);
  }
  if (err) *err = "not an ebook URL";
  return std::nullopt;
}

#endif  // XANATOO_EBOOKS

}  // namespace xanatoo
