// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/search.hpp"

#include "xanatoo/paths.hpp"
#include "xanatoo/sexpr.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace xanatoo {

namespace {

bool valid_id(std::string_view id) {
  if (id.empty()) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  return true;
}

bool unreserved(unsigned char c) {
  return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string encode(std::string_view raw, std::string_view space) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : raw) {
    if (unreserved(c))
      out += static_cast<char>(c);
    else if (c == ' ')
      out += space;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string collapse_blanks(std::string_view raw) {
  std::string out;
  bool blank = true;  // also trims the front
  for (char c : raw) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      blank = true;
      continue;
    }
    if (blank && !out.empty()) out += ' ';
    blank = false;
    out += c;
  }
  return out;
}

bool read_text(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// Lower-case host of an http(s) URL, "" for anything else.
std::string http_host(std::string_view url) {
  const size_t p = url.find("://");
  if (p == std::string_view::npos) return {};
  const std::string scheme = ascii_lower(url.substr(0, p));
  if (scheme != "http" && scheme != "https") return {};
  std::string_view h = url.substr(p + 3);
  h = h.substr(0, h.find_first_of("/?#"));
  if (const size_t at = h.rfind('@'); at != std::string_view::npos)
    h.remove_prefix(at + 1);
  h = h.substr(0, h.find(':'));
  return ascii_lower(h);
}

const SearchEngine* find_engine(const std::vector<SearchEngine>& engines,
                                std::string_view id) {
  for (const SearchEngine& e : engines)
    if (e.id == id) return &e;
  return nullptr;
}

}  // namespace

const std::vector<SearchEngine>& builtin_engines() {
  // id, name, keyword, badge, url, kind, space, sites
  static const std::vector<SearchEngine> k = {
      {"wikipedia", "Wikipedia", "w", "W",
       "https://en.wikipedia.org/w/api.php?action=query&list=search"
       "&srsearch=%s&srlimit=30&format=json&formatversion=2",
       "wikipedia", "+",
       {"wikipedia:", "wikipedia.org"}},
      {"wikipedia-de", "Wikipedia (Deutsch)", "wd", "Wd",
       "https://de.wikipedia.org/w/api.php?action=query&list=search"
       "&srsearch=%s&srlimit=30&format=json&formatversion=2",
       "wikipedia", "+",
       {"wikipedia:de:", "de.wikipedia.org"}},
      {"hn", "Hacker News", "hn", "HN",
       "https://hn.algolia.com/api/v1/search?query=%s&tags=story"
       "&hitsPerPage=30",
       "hn", "+",
       {"hn:", "news.ycombinator.com", "hn.algolia.com"}},
      {"arxiv", "arXiv", "ax", "arX",
       "https://export.arxiv.org/api/query?search_query=all:%s&max_results=50",
       "feed", "+AND+all:",
       {"arxiv.org"}},  // every word must match (a bare blank is OR)
      {"archive", "Internet Archive", "ia", "IA",
       "https://archive.org/advancedsearch.php?q=%s&fl%5B%5D=identifier"
       "&fl%5B%5D=title&fl%5B%5D=description&fl%5B%5D=mediatype"
       "&fl%5B%5D=creator&sort%5B%5D=downloads+desc&rows=40&output=json",
       "archive", "+",
       {"ia:", "archive.org"}},
      {"standard-ebooks", "Standard Ebooks", "se", "SE",
       "https://standardebooks.org/ebooks?query=%s", "page", "+",
       {"standardebooks.org"}},
      {"gutenberg", "Project Gutenberg", "pg", "PG",
       "https://gutendex.com/books/?search=%s", "gutendex", "+",
       {"gutenberg.org", "gutendex.com"}},
      {"reddit", "Reddit", "r", "R", "https://www.reddit.com/search.rss?q=%s",
       "feed", "+",
       {"reddit:", "reddit.com"}},
      {"lemmy", "Lemmy (lemmy.world)", "l", "Lm",
       "https://lemmy.world/api/v3/search?q=%s&type_=Posts&limit=30", "lemmy",
       "+",
       {"lemmy:", "lemmy.world"}},
      {"bluesky", "Bluesky", "bs", "Bs",
       "https://api.bsky.app/xrpc/app.bsky.feed.searchPosts?q=%s&limit=30",
       "bluesky", "+",
       {"bsky.app", "bsky.social"}},
      {"mastodon-tag", "Mastodon hashtag", "mt", "#",
       "https://mastodon.social/tags/%s.rss", "feed", "",
       {"mastodon.social"}},
      {"flickr", "Flickr tags", "fl", "Fl",
       "https://www.flickr.com/services/feeds/photos_public.gne?tags=%s"
       "&format=rss2",
       "feed", ",",
       {"flickr.com"}},
      {"marginalia", "Marginalia (small web)", "mg", "Mg",
       "https://marginalia-search.com/search?query=%s", "page", "+",
       {"marginalia-search.com", "search.marginalia.nu"}},
      {"wiby", "Wiby (old web)", "wb", "Wb", "https://wiby.me/?q=%s", "page",
       "+",
       {"wiby.me"}},
      {"youtube", "YouTube", "yt", "YT",
       "youtube:results?search_query=%s", "page", "+",
       {"youtube:", "youtube.com", "youtu.be"}},
      {"peertube", "PeerTube (SepiaSearch)", "pt", "PT",
       "peertube:sepiasearch.org/search?search=%s", "page", "+",
       {"peertube:", "sepiasearch.org"}},
      {"odysee", "Odysee", "od", "Od", "odysee:search?s=%s", "page", "+",
       {"odysee:", "odysee.com"}},
      {"dailymotion", "Dailymotion", "dm", "DM",
       "dailymotion:search?search=%s", "page", "+",
       {"dailymotion:", "dailymotion.com"}},
      {"duckduckgo", "DuckDuckGo", "dd", "DDG",
       "https://html.duckduckgo.com/html/?q=%s", "page", "+",
       {"duckduckgo.com"}},
  };
  return k;
}

bool is_json_search_kind(std::string_view kind) {
  return kind == "wikipedia" || kind == "hn" || kind == "archive" ||
         kind == "lemmy" || kind == "bluesky" || kind == "gutendex";
}

bool is_search_kind(std::string_view kind) {
  return kind == "page" || kind == "feed" || is_json_search_kind(kind);
}

std::vector<SearchEngine> parse_engines(std::string_view source,
                                        std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& m) {
    if (warnings) warnings->push_back(m);
  };
  std::vector<SearchEngine> out;
  for (const SExpr& f : SExprParser(source).parse_all()) {
    if (!is_form(f, "engine")) continue;
    SearchEngine e;
    if (f.kids.size() > 1) e.id = sexpr_text(f.kids[1]);
    if (!valid_id(e.id)) {
      warn("engine id must be lower-case letters, digits and '-': \"" + e.id +
           "\"");
      continue;
    }
    e.name = e.id;
    for (size_t i = 2; i < f.kids.size(); ++i) {
      const SExpr& p = f.kids[i];
      if (p.kind != SExpr::Kind::List || p.kids.size() < 2) continue;
      const std::string key = sexpr_text(p.kids[0]);
      const std::string val = sexpr_text(p.kids[1]);
      if (key == "name") e.name = val;
      else if (key == "keyword") e.keyword = val;
      else if (key == "badge") e.badge = val;
      else if (key == "url") e.url = val;
      else if (key == "kind") {
        if (is_search_kind(val))
          e.kind = val;
        else
          warn("engine " + e.id + ": unknown kind " + val);
      }
      else if (key == "space") e.space = val;
      else if (key == "site") {
        for (size_t j = 1; j < p.kids.size(); ++j) {
          std::string site = sexpr_text(p.kids[j]);
          if (!site.empty()) e.sites.push_back(std::move(site));
        }
      }
      else warn("engine " + e.id + ": unknown property " + key);
    }
    const size_t pct = e.url.find("%s");
    if (pct == std::string::npos || e.url.find("%s", pct + 2) != std::string::npos ||
        !(starts_with(e.url, "http://") || starts_with(e.url, "https://"))) {
      warn("engine " + e.id + ": url needs an http(s) address with one %s");
      continue;
    }
    if (e.badge.empty()) e.badge = e.name.substr(0, 1);
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<SearchEngine> load_engines() {
  std::vector<SearchEngine> all = builtin_engines();
  const std::string dir = user_data_dir();
  if (dir.empty()) return all;
  std::string src;
  if (!read_text((std::filesystem::path(dir) / "search.sexp").string(), src))
    return all;
  for (SearchEngine& e : parse_engines(src)) {
    auto it = std::find_if(all.begin(), all.end(), [&](const SearchEngine& b) {
      return b.id == e.id;
    });
    if (it != all.end())
      *it = std::move(e);
    else
      all.push_back(std::move(e));
  }
  return all;
}

std::string search_encode(std::string_view raw) {
  return encode(collapse_blanks(raw), "+");
}

std::string search_decode(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(
                   static_cast<unsigned char>(s[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

bool is_search_ref(std::string_view s) { return parse_search_ref(s).has_value(); }

std::optional<SearchRef> parse_search_ref(std::string_view s) {
  trim_ascii(s);
  if (!starts_with(s, kSearchPrefix)) return std::nullopt;
  s.remove_prefix(kSearchPrefix.size());
  const size_t colon = s.find(':');
  if (colon == std::string_view::npos) return std::nullopt;
  SearchRef r;
  r.engine = std::string(s.substr(0, colon));
  r.query = std::string(s.substr(colon + 1));
  if (!valid_id(r.engine) || r.query.empty()) return std::nullopt;
  return r;
}

std::string search_ref(std::string_view engine, std::string_view raw) {
  const std::string q = search_encode(raw);
  if (q.empty() || !valid_id(engine)) return {};
  return std::string(kSearchPrefix) + std::string(engine) + ":" + q;
}

std::optional<SearchPlan> search_plan(std::string_view ref,
                                      const std::vector<SearchEngine>& engines) {
  auto r = parse_search_ref(ref);
  if (!r) return std::nullopt;
  const SearchEngine* e = find_engine(engines, r->engine);
  if (!e) return std::nullopt;
  std::string url = e->url;
  const size_t at = url.find("%s");
  if (at == std::string::npos) return std::nullopt;
  SearchPlan p;
  p.query = search_decode(r->query);
  url.replace(at, 2, encode(p.query, e->space));
  p.url = std::move(url);
  p.kind = e->kind;
  return p;
}

std::string search_target(std::string_view ref,
                          const std::vector<SearchEngine>& engines) {
  auto p = search_plan(ref, engines);
  if (!p || is_json_search_kind(p->kind)) return {};
  return p->kind == "feed" ? "feed:" + p->url : p->url;
}

std::string search_from_keyword(std::string_view text,
                                const std::vector<SearchEngine>& engines) {
  trim_ascii(text);
  const size_t sp = text.find(' ');
  if (sp == std::string_view::npos) return {};
  const std::string_view word = text.substr(0, sp);
  for (const SearchEngine& e : engines)
    if (!e.keyword.empty() && e.keyword == word)
      return search_ref(e.id, text.substr(sp + 1));
  return {};
}

std::string engine_for_address(std::string_view address,
                               const std::vector<SearchEngine>& engines) {
  trim_ascii(address);
  if (auto r = parse_search_ref(address))
    return find_engine(engines, r->engine) ? r->engine : std::string();
  const std::string host = http_host(address);
  const std::string lower = ascii_lower(address);
  std::string best;
  size_t best_len = 0;
  for (const SearchEngine& e : engines) {
    for (const std::string& site : e.sites) {
      const std::string s = ascii_lower(site);
      bool hit;
      if (s.find(':') != std::string::npos)
        hit = starts_with(lower, s);
      else
        hit = !host.empty() &&
              (host == s || (host.size() > s.size() &&
                             ends_with(host, s) &&
                             host[host.size() - s.size() - 1] == '.'));
      if (hit && s.size() > best_len) {
        best = e.id;
        best_len = s.size();
      }
    }
  }
  return best;
}

std::string search_query_text(std::string_view ref) {
  auto r = parse_search_ref(ref);
  return r ? search_decode(r->query) : std::string();
}

}  // namespace xanatoo
