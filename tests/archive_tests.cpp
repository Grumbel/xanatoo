// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Offline tests for the Internet Archive refs and Markdown mapping; replies
// are cut-down copies of what archive.org sends.
#include "xanatoo/archive.hpp"

#include <cassert>
#include <string>

using namespace xanatoo;

static bool has(const std::string& s, const char* part) {
  return s.find(part) != std::string::npos;
}

int main() {
  // Refs.
  assert(parse_ia_ref("ia:prelinger")->id == "prelinger");
  assert(parse_ia_ref("ia:details/foo_bar-1.2")->id == "foo_bar-1.2");
  assert(parse_ia_ref("ia:foo?page=3")->page == 3);
  assert(!parse_ia_ref("ia:"));
  assert(!parse_ia_ref("ia:a b"));
  assert(!parse_ia_ref("https://archive.org/details/x"));
  assert(ia_ref_string({"x", 2}) == "ia:x?page=2");
  assert(ia_ref_from_web_url("https://archive.org/details/foo?q=1") == "ia:foo");
  assert(ia_ref_from_web_url("https://archive.org/download/foo/a.pdf") == "ia:foo");
  assert(ia_ref_from_web_url("https://archive.org/search?query=x").empty());
  assert(ia_ref_from_web_url("https://example.com/details/foo").empty());
  assert(ia_web_url({"foo", 1}) == "https://archive.org/details/foo");

  // An item.
  const char* item = R"({"files":[
    {"name":"book.pdf","format":"Text PDF","source":"original","size":"2048"},
    {"name":"book_meta.xml","format":"Metadata","source":"original"},
    {"name":"book.torrent","format":"Archive BitTorrent"},
    {"name":"my book.epub","format":"EPUB","source":"derivative","size":"3145728"}],
   "metadata":{"identifier":"book","title":"A *Book* [1]","mediatype":"texts",
     "creator":["Doe, Jane","Roe, R."],"date":"1901-02-03","collection":["gutenberg"],
     "subject":["Travel","Maps"],"description":"<p>First.</p><p>Second</p>",
     "licenseurl":"http://creativecommons.org/publicdomain/zero/1.0/"}})";
  assert(ia_mediatype(item) == "texts");
  const std::string md = ia_markdown(item, "", {"book", 1});
  assert(has(md, "# A \\*Book\\* \\[1\\]"));
  assert(has(md, "Creator: Doe, Jane; Roe, R."));
  assert(has(md, "[gutenberg](ia:gutenberg)"));
  assert(has(md, "First."));
  assert(has(md, "Second"));
  assert(has(md, "[book.pdf](https://archive.org/download/book/book.pdf)"));
  assert(has(md, "my%20book.epub"));
  assert(!has(md, "book_meta.xml") && !has(md, "torrent"));
  assert(has(md, "external:https://archive.org/details/book"));
  assert(!has(md, "mpv"));  // texts are not played

  // Media gets a mpv link.
  const char* movie = R"({"files":[{"name":"a.mp4","format":"MPEG4","source":"original"}],
    "metadata":{"identifier":"m","title":"Film","mediatype":"movies"}})";
  assert(has(ia_markdown(movie, "", {"m", 1}), "external:mpv:https://archive.org/download/m/a.mp4"));

  // A collection lists its members and pages.
  const char* coll = R"({"metadata":{"identifier":"c","title":"Coll","mediatype":"collection"}})";
  const char* members = R"({"response":{"numFound":120,"start":0,"docs":[
    {"identifier":"one","title":"First item","mediatype":"texts","creator":"Ann"},
    {"identifier":"bad id","title":"x"}]}})";
  const std::string cm = ia_markdown(coll, members, {"c", 1});
  assert(has(cm, "[First item](ia:one) — texts · Ann"));
  assert(!has(cm, "bad id"));
  assert(has(cm, "[Next page](ia:c?page=2)"));
  assert(!has(cm, "Previous page"));
  assert(has(ia_markdown(coll, members, {"c", 2}), "[Previous page](ia:c)"));
  assert(has(ia_members_url({"c", 2}), "q=collection%3Ac"));

  // Unknown / malformed replies.
  assert(has(ia_markdown("{}", "", {"nope", 1}), "# Not found"));
  assert(has(ia_markdown("garbage", "", {"nope", 1}), "# Not found"));
  assert(ia_mediatype("{}").empty());
  return 0;
}
