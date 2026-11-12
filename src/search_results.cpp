// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/search_results.hpp"

#include "xanatoo/html_min.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/version.hpp"

#include <algorithm>

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
#include "xanatoo/https.hpp"
#endif

namespace xanatoo {

namespace {

// Running text on one line: blanks collapsed, cut after `max` code points.
std::string one_line(std::string_view s, size_t max = 0) {
  std::string out;
  bool blank = false;
  size_t points = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      blank = true;
      continue;
    }
    if ((c & 0xC0) != 0x80) {  // start of a code point
      if (max && points >= max) {
        out += "…";
        return out;
      }
      ++points;
      if (blank && !out.empty()) out += ' ';
      blank = false;
    }
    out += static_cast<char>(c);
  }
  return out;
}

// Destination of a Markdown link: parentheses and blanks percent-encoded.
std::string md_url(std::string_view url) {
  std::string out;
  for (char c : url) {
    if (c == '(') out += "%28";
    else if (c == ')') out += "%29";
    else if (c == ' ') out += "%20";
    else if (c == '<') out += "%3C";
    else if (c == '>') out += "%3E";
    else out += c;
  }
  return out;
}

std::string host_of(std::string_view url) {
  const size_t s = url.find("://");
  if (s == std::string_view::npos) return {};
  const size_t b = s + 3;
  const size_t e = url.find_first_of("/?#", b);
  return std::string(url.substr(b, e == std::string_view::npos ? e : e - b));
}

std::string short_host(std::string_view url) {
  std::string h = host_of(url);
  if (starts_with(h, "www.")) h.erase(0, 4);
  return h;
}

std::string date_of(std::string_view iso) {
  return std::string(iso.substr(0, std::min<size_t>(iso.size(), 10)));
}

// Link text is taken literally by the parser (no escapes, no emphasis):
// only brackets would end it early, so they become parentheses.
std::string link(std::string_view text, std::string_view url) {
  std::string t = one_line(text);
  for (char& c : t)
    if (c == '[') c = '(';
    else if (c == ']') c = ')';
  return "[" + t + "](" + md_url(url) + ")";
}

std::string plural(int64_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// First string of "key": ["…", …] or the plain string value.
std::string first_string(std::string_view obj, std::string_view key) {
  std::string v = json_str_field(obj, key);
  if (!v.empty()) return v;
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t p = obj.find(pat);
  if (p == std::string_view::npos) return {};
  p = obj.find_first_not_of(" \t\r\n", p + pat.size() + 1);
  if (p == std::string_view::npos || obj[p] != '[') return {};
  p = obj.find_first_not_of(" \t\r\n", p + 1);
  if (p == std::string_view::npos || obj[p] != '"') return {};
  return json_str_field(std::string("{\"v\":") + std::string(obj.substr(p, 4096)) + "}",
                        "v");
}

std::string header(std::string_view what, const SearchPlan& plan) {
  return "# " + std::string(what) + ": " + search_md_escape(plan.query) + "\n\n";
}

std::string no_results(std::string_view what, const SearchPlan& plan) {
  return header(what, plan) + "No results.\n";
}

// ---- Wikipedia: list=search, formatversion=2 -----------------------------

std::string ref_title(std::string_view title) {
  std::string out;
  for (char c : title) {
    if (c == ' ') out += '_';
    else if (c == '(') out += "%28";
    else if (c == ')') out += "%29";
    else if (c == '%') out += "%25";
    else if (c == '#') out += "%23";
    else out += c;
  }
  return out;
}

std::string map_wikipedia(const SearchPlan& plan, std::string_view json) {
  const std::string host = host_of(plan.url);  // xx.wikipedia.org
  const size_t dot = host.find('.');
  const std::string lang = dot == std::string::npos ? "en" : host.substr(0, dot);
  const auto hits = json_object_array_after_key(json, "search");
  if (hits.empty()) return no_results("Wikipedia", plan);

  std::string md = header(lang == "en" ? "Wikipedia" : "Wikipedia (" + lang + ")",
                          plan);
  const int64_t total = json_int_field(json, "totalhits");
  if (total > 0)
    md += plural(total, "article matches", "articles match") + ".\n\n";
  for (const std::string& h : hits) {
    const std::string top = json_top_level(h);
    const std::string title = json_str_field(top, "title");
    if (title.empty()) continue;
    const std::string ref =
        "wikipedia:" + (lang == "en" ? std::string() : lang + ":") + ref_title(title);
    // The matched words come as <span class="searchmatch">: bold them.
    std::string snip = json_str_field(top, "snippet");
    size_t at;
    static const std::string kMatch = "<span class=\"searchmatch\">";
    while ((at = snip.find(kMatch)) != std::string::npos)
      snip.replace(at, kMatch.size(), "\x01");
    while ((at = snip.find("</span>")) != std::string::npos)
      snip.replace(at, 7, "\x02");
    snip = one_line(html_to_plain(snip), 260);
    std::string esc = search_md_escape(snip);
    for (size_t i = 0; i < esc.size(); ++i) {
      if (esc[i] == '\x01') esc.replace(i, 1, "**");
      else if (esc[i] == '\x02') esc.replace(i, 1, "**");
    }
    md += "- " + link(title, ref);
    if (!esc.empty()) md += " — " + esc;
    md += "\n";
  }
  md += "\n[The same search on the website](https://" + host +
        "/w/index.php?search=" + search_encode(plan.query) + ")\n";
  return md;
}

// ---- Hacker News: Algolia search -----------------------------------------

std::string map_hn(const SearchPlan& plan, std::string_view json) {
  const auto hits = json_object_array_after_key(json, "hits");
  if (hits.empty()) return no_results("Hacker News", plan);
  std::string md = header("Hacker News", plan);
  for (const std::string& h : hits) {
    const std::string top = json_top_level(h);
    const std::string id = json_str_field(top, "objectID");
    std::string title = json_str_field(top, "title");
    if (id.empty() || title.empty()) continue;
    const std::string thread = "hn:item?id=" + id;
    const std::string url = json_str_field(top, "url");
    md += "- " + link(title, url.empty() ? thread : url);
    if (!url.empty()) md += " (" + search_md_escape(short_host(url)) + ")";
    md += " — " + plural(json_int_field(top, "points"), "point", "points") +
          " · " + link(plural(json_int_field(top, "num_comments"), "comment",
                              "comments"),
                       thread);
    const std::string by = json_str_field(top, "author");
    if (!by.empty()) md += " · " + search_md_escape(by);
    const std::string when = date_of(json_str_field(top, "created_at"));
    if (!when.empty()) md += " · " + when;
    md += "\n";
  }
  return md;
}

// ---- Internet Archive: advancedsearch, output=json -----------------------

std::string map_archive(const SearchPlan& plan, std::string_view json) {
  const auto docs = json_object_array_after_key(json, "docs");
  if (docs.empty()) return no_results("Internet Archive", plan);
  std::string md = header("Internet Archive", plan);
  const int64_t total = json_int_field(json, "numFound");
  if (total > 0)
    md += plural(total, "item matches", "items match") +
          ", the most downloaded first.\n\n";
  for (const std::string& d : docs) {
    const std::string top = json_top_level(d);
    const std::string id = json_str_field(top, "identifier");
    if (id.empty()) continue;
    std::string title = first_string(d, "title");
    if (title.empty()) title = id;
    md += "- " + link(title, "ia:" + id);
    std::string meta;
    const std::string type = json_str_field(top, "mediatype");
    if (!type.empty()) meta += type;
    const std::string by = first_string(d, "creator");
    if (!by.empty()) meta += (meta.empty() ? "" : " · ") + one_line(by, 60);
    if (!meta.empty()) md += " — " + search_md_escape(meta);
    const std::string desc = one_line(html_to_plain(first_string(d, "description")), 200);
    if (!desc.empty()) md += ". " + search_md_escape(desc);
    md += "\n";
  }
  return md;
}

// ---- Lemmy: /api/v3/search, type_=Posts ----------------------------------

std::string map_lemmy(const SearchPlan& plan, std::string_view json) {
  const auto posts = json_object_array_after_key(json, "posts");
  const std::string host = host_of(plan.url);
  if (posts.empty() || host.empty()) return no_results("Lemmy", plan);
  std::string md = header("Lemmy (" + host + ")", plan);
  for (const std::string& p : posts) {
    const std::string post = std::string(json_object_after_key(p, "post"));
    const std::string top = json_top_level(post);
    const int64_t id = json_int_field(top, "id");
    const std::string name = json_str_field(top, "name");
    if (id == 0 || name.empty()) continue;
    md += "- " + link(name, "lemmy:" + host + "/post/" + std::to_string(id));
    const std::string comm =
        json_str_field(json_top_level(std::string(json_object_after_key(p, "community"))),
                       "name");
    if (!comm.empty()) md += " — c/" + search_md_escape(comm);
    const std::string counts = std::string(json_object_after_key(p, "counts"));
    md += " · " + plural(json_int_field(counts, "score"), "point", "points") +
          " · " + plural(json_int_field(counts, "comments"), "comment", "comments");
    const std::string by =
        json_str_field(json_top_level(std::string(json_object_after_key(p, "creator"))),
                       "name");
    if (!by.empty()) md += " · " + search_md_escape(by);
    md += "\n";
  }
  return md;
}

// ---- Bluesky: app.bsky.feed.searchPosts ----------------------------------

std::string map_bluesky(const SearchPlan& plan, std::string_view json) {
  const auto posts = json_object_array_after_key(json, "posts");
  if (posts.empty()) return no_results("Bluesky", plan);
  std::string md = header("Bluesky", plan);
  for (const std::string& p : posts) {
    const std::string top = json_top_level(p);
    const std::string uri = json_str_field(top, "uri");
    const std::string author = std::string(json_object_after_key(p, "author"));
    const std::string handle = json_str_field(json_top_level(author), "handle");
    const std::string record = json_top_level(std::string(json_object_after_key(p, "record")));
    const std::string text = json_str_field(record, "text");
    if (handle.empty() || text.empty()) continue;
    std::string name = json_str_field(json_top_level(author), "displayName");
    if (name.empty()) name = handle;
    const size_t slash = uri.rfind('/');
    md += "## " + search_md_escape(one_line(name)) + " @" + search_md_escape(handle) +
          " · " + date_of(json_str_field(record, "createdAt")) + "\n\n";
    md += search_md_escape(text) + "\n\n";
    if (slash != std::string::npos)
      md += link("Open on Bluesky", "https://bsky.app/profile/" + handle +
                                        "/post/" + uri.substr(slash + 1));
    md += " · " + plural(json_int_field(top, "replyCount"), "reply", "replies") +
          " · " + plural(json_int_field(top, "likeCount"), "like", "likes") + "\n\n";
  }
  return md;
}

// ---- Project Gutenberg: Gutendex -----------------------------------------

std::string map_gutendex(const SearchPlan& plan, std::string_view json) {
  const auto books = json_object_array_after_key(json, "results");
  if (books.empty()) return no_results("Project Gutenberg", plan);
  std::string md = header("Project Gutenberg", plan);
  const int64_t total = json_int_field(json, "count");
  if (total > 0)
    md += plural(total, "book matches", "books match") +
          ", the most downloaded first. A click downloads and opens the book.\n\n";
  for (const std::string& b : books) {
    const std::string top = json_top_level(b);
    const int64_t id = json_int_field(top, "id");
    const std::string title = json_str_field(top, "title");
    if (id == 0 || title.empty()) continue;
    md += "- " + link(title, "https://www.gutenberg.org/ebooks/" + std::to_string(id));
    const auto authors = json_object_array_after_key(b, "authors");
    if (!authors.empty()) {
      const std::string a = json_str_field(authors[0], "name");
      if (!a.empty()) md += " — " + search_md_escape(a);
    }
    const int64_t dl = json_int_field(top, "download_count");
    if (dl > 0) md += " · " + plural(dl, "download", "downloads");
    md += "\n";
  }
  return md;
}

// ---- Published-media catalogs --------------------------------------------

std::string metadata(std::string_view title, std::string_view details) {
  if (details.empty()) return {};
  return " — " + search_md_escape(one_line(details, 230));
}

std::string media_link(std::string_view title, std::string_view source,
                       std::string_view id) {
  return link(title, "media:" + std::string(source) + ":" + std::string(id));
}

std::string map_openlibrary(const SearchPlan& plan, std::string_view json) {
  auto hits = json_object_array_after_key(json, "docs");
  std::string md = header("Open Library", plan);
  if (hits.empty()) return md + "No results.\n";
  for (const auto& hit : hits) {
    const std::string top = json_top_level(hit);
    const std::string key = json_str_field(top, "key");
    const std::string title = json_str_field(top, "title");
    if (key.empty() || title.empty()) continue;
    const std::string id = key.substr(key.find_last_of('/') + 1);
    md += "- " + media_link(title, "openlibrary", id);
    md += metadata("", first_string(hit, "author_name"));
    const int64_t year = json_int_field(top, "first_publish_year");
    if (year > 0) md += " · " + std::to_string(year);
    md += "\n";
  }
  return md;
}

std::string map_tvmaze(const SearchPlan& plan, std::string_view json) {
  // The API returns an array of {score, show}; unlike named arrays in other APIs.
  std::string md = header("TVmaze", plan);
  const auto shows = json_object_array_after_key(
      std::string("{\"items\":") + std::string(json) + "}", "items");
  for (const auto& item : shows) {
    const std::string show = std::string(json_object_after_key(item, "show"));
    const std::string top = json_top_level(show);
    const int64_t id = json_int_field(top, "id");
    const std::string name = json_str_field(top, "name");
    if (id <= 0 || name.empty()) continue;
    md += "- " + media_link(name, "tvmaze", std::to_string(id));
    md += metadata("", json_str_field(top, "premiered"));
    md += "\n";
  }
  return md;
}

std::string map_musicbrainz(const SearchPlan& plan, std::string_view json) {
  std::string md = header("MusicBrainz", plan);
  for (const auto& hit : json_object_array_after_key(json, "release-groups")) {
    const std::string top = json_top_level(hit);
    const std::string id = json_str_field(top, "id");
    const std::string title = json_str_field(top, "title");
    if (id.empty() || title.empty()) continue;
    md += "- " + media_link(title, "musicbrainz", id);
    md += metadata("", json_str_field(top, "first-release-date"));
    md += "\n";
  }
  return md;
}

std::string map_wikidata(const SearchPlan& plan, std::string_view json) {
  std::string md = header("Wikidata", plan);
  for (const auto& hit : json_object_array_after_key(json, "search")) {
    const std::string top = json_top_level(hit);
    const std::string id = json_str_field(top, "id");
    const std::string title = json_str_field(top, "label");
    if (id.empty() || title.empty()) continue;
    md += "- " + media_link(title, "wikidata", id);
    md += metadata("", json_str_field(top, "description"));
    md += "\n";
  }
  return md;
}

std::string map_openalex(const SearchPlan& plan, std::string_view json) {
  std::string md = header("OpenAlex", plan);
  for (const auto& hit : json_object_array_after_key(json, "results")) {
    const std::string top = json_top_level(hit);
    const std::string id = json_str_field(top, "id");
    const std::string title = json_str_field(top, "display_name");
    if (id.empty() || title.empty()) continue;
    // The returned URL is an API identifier, not an arbitrary URL.
    const std::string key = id.substr(id.find_last_of('/') + 1);
    md += "- " + media_link(title, "openalex", key);
    const int64_t year = json_int_field(top, "publication_year");
    if (year > 0) md += " — " + std::to_string(year);
    md += "\n";
  }
  return md;
}

std::string map_crossref(const SearchPlan& plan, std::string_view json) {
  std::string md = header("Crossref", plan);
  const std::string message = std::string(json_object_after_key(json, "message"));
  for (const auto& hit : json_object_array_after_key(message, "items")) {
    const std::string top = json_top_level(hit);
    const std::string doi = json_str_field(top, "DOI");
    const std::string title = first_string(hit, "title");
    if (doi.empty() || title.empty()) continue;
    md += "- " + link(title, "https://doi.org/" + doi);
    md += metadata("", first_string(hit, "publisher"));
    md += "\n";
  }
  return md;
}

}  // namespace

std::string search_md_escape(std::string_view text) {
  std::string out;
  bool line_start = true;
  for (char c : text) {
    switch (c) {
      case '\\': case '`': case '*': case '_': case '[': case ']':
        out += '\\';
        out += c;
        line_start = false;
        break;
      case '#': case '-': case '+':
        if (line_start) out += '\\';
        out += c;
        line_start = false;
        break;
      case '\n':
        out += c;
        line_start = true;
        break;
      case ' ':
        out += c;  // a line may start with blanks, still at its start
        break;
      default:
        out += c;
        line_start = false;
    }
  }
  return out;
}

bool is_media_ref(std::string_view ref) {
  return starts_with(ref, "media:");
}

std::optional<SearchPlan> media_plan(std::string_view ref) {
  if (!is_media_ref(ref)) return std::nullopt;
  ref.remove_prefix(6);
  const size_t sep = ref.find(':');
  if (sep == std::string_view::npos) return std::nullopt;
  const std::string source(ref.substr(0, sep));
  const std::string id(ref.substr(sep + 1));
  if (id.empty() || id.size() > 120) return std::nullopt;
  for (unsigned char c : id)
    if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') return std::nullopt;
  SearchPlan p;
  p.kind = source;
  p.query = id;
  if (source == "openlibrary" && id.size() > 3 && id.starts_with("OL") &&
      id.back() == 'W')
    p.url = "https://openlibrary.org/works/" + id + ".json";
  else if (source == "tvmaze" && id.find_first_not_of("0123456789") == std::string::npos)
    p.url = "https://api.tvmaze.com/shows/" + id;
  else if (source == "musicbrainz" && id.size() == 36)
    p.url = "https://musicbrainz.org/ws/2/release-group/" + id + "?fmt=json";
  else if (source == "wikidata" && id[0] == 'Q' && id.size() > 1 &&
           id.substr(1).find_first_not_of("0123456789") == std::string::npos)
    p.url = "https://www.wikidata.org/wiki/Special:EntityData/" + id + ".json";
  else if (source == "openalex" && id[0] == 'W' && id.size() > 1 &&
           id.substr(1).find_first_not_of("0123456789") == std::string::npos)
    p.url = "https://api.openalex.org/works/" + id;
  if (p.url.empty()) return std::nullopt;
  return p;
}

std::string media_results_markdown(const SearchPlan& plan, std::string_view json) {
  const std::string top = json_top_level(json);
  std::string title;
  std::string desc;
  std::string external;
  if (plan.kind == "openlibrary") {
    title = json_str_field(top, "title");
    desc = json_str_field(top, "description");
    if (desc.empty()) desc = json_str_field(
        json_top_level(std::string(json_object_after_key(json, "description"))), "value");
    external = "https://openlibrary.org/works/" + plan.query;
  } else if (plan.kind == "tvmaze") {
    title = json_str_field(top, "name");
    desc = one_line(html_to_plain(json_str_field(top, "summary")));
    external = "https://www.tvmaze.com/shows/" + plan.query;
  } else if (plan.kind == "musicbrainz") {
    title = json_str_field(top, "title");
    desc = json_str_field(top, "first-release-date");
    external = "https://musicbrainz.org/release-group/" + plan.query;
  } else if (plan.kind == "wikidata") {
    const std::string entities = std::string(json_object_after_key(json, "entities"));
    const std::string entity = std::string(json_object_after_key(entities, plan.query));
    const std::string labels = std::string(json_object_after_key(entity, "labels"));
    const std::string en = std::string(json_object_after_key(labels, "en"));
    title = json_str_field(en, "value");
    const std::string descriptions = std::string(json_object_after_key(entity, "descriptions"));
    desc = json_str_field(std::string(json_object_after_key(descriptions, "en")), "value");
    external = "https://www.wikidata.org/wiki/" + plan.query;
  } else if (plan.kind == "openalex") {
    title = json_str_field(top, "display_name");
    const int64_t year = json_int_field(top, "publication_year");
    if (year > 0) desc = std::to_string(year);
    external = "https://openalex.org/" + plan.query;
  }
  if (title.empty()) title = plan.query;
  std::string md = "# " + search_md_escape(title) + "\n\n";
  if (!desc.empty()) md += search_md_escape(desc) + "\n\n";
  md += "**Source:** " + search_md_escape(plan.kind) + " · " +
        link("View original record", external) + "\n";
  return md;
}

std::string search_results_markdown(const SearchPlan& plan, std::string_view json) {
  if (plan.kind == "wikipedia") return map_wikipedia(plan, json);
  if (plan.kind == "hn") return map_hn(plan, json);
  if (plan.kind == "archive") return map_archive(plan, json);
  if (plan.kind == "lemmy") return map_lemmy(plan, json);
  if (plan.kind == "bluesky") return map_bluesky(plan, json);
  if (plan.kind == "gutendex") return map_gutendex(plan, json);
  if (plan.kind == "openlibrary") return map_openlibrary(plan, json);
  if (plan.kind == "tvmaze") return map_tvmaze(plan, json);
  if (plan.kind == "musicbrainz") return map_musicbrainz(plan, json);
  if (plan.kind == "wikidata") return map_wikidata(plan, json);
  if (plan.kind == "openalex") return map_openalex(plan, json);
  if (plan.kind == "crossref") return map_crossref(plan, json);
  return "# " + search_md_escape(plan.query) + "\n\nNo reader for the engine kind " +
         search_md_escape(plan.kind) + ".\n";
}

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
std::optional<Document> media_fetch(const SearchPlan& plan, std::string* err) {
  static const std::string ua = user_agent("media");
  auto r = https_get_follow(plan.url, ua, err);
  if (!r) return std::nullopt;
  if (r->status != 200) {
    if (err) *err = "HTTP " + std::to_string(r->status) + " from " + host_of(plan.url);
    return std::nullopt;
  }
  return parse_document(media_results_markdown(plan, r->body), "media.md");
}

std::optional<Document> search_fetch(const SearchPlan& plan, std::string* err) {
  static const std::string ua = user_agent("search");
  auto r = https_get_follow(plan.url, ua, err);
  if (!r) return std::nullopt;
  if (r->status != 200) {
    if (err) *err = "HTTP " + std::to_string(r->status) + " from " + host_of(plan.url);
    return std::nullopt;
  }
  return parse_document(search_results_markdown(plan, r->body), "search.md");
}
#endif

}  // namespace xanatoo
