// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/native.hpp"

#include "xanatoo/hackernews.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/wikipedia.hpp"

namespace xanatoo {

namespace {

constexpr std::string_view kHnPrefix = "hn:";
constexpr std::string_view kRedditPrefix = "reddit:";
constexpr std::string_view kLemmyPrefix = "lemmy:";

// "https://Host/path?q#f" → host (lower case, no port) and "path?q#f"
// (no leading '/'). False without an http(s) scheme.
bool split_web(std::string_view url, std::string& host, std::string& rest) {
  trim_ascii(url);
  if (starts_with_ci(url, "https://")) url.remove_prefix(8);
  else if (starts_with_ci(url, "http://")) url.remove_prefix(7);
  else return false;
  size_t end = url.find_first_of("/?#");
  host = ascii_lower(url.substr(0, end));
  if (size_t colon = host.rfind(':'); colon != std::string::npos)
    host.erase(colon);
  rest = end == std::string_view::npos ? std::string() : std::string(url.substr(end));
  if (starts_with(rest, "/")) rest.erase(0, 1);
  return !host.empty();
}

std::string_view after_prefix(std::string_view s, std::string_view prefix) {
  trim_ascii(s);
  s.remove_prefix(prefix.size());
  return s;
}

}  // namespace

bool is_hn_ref(std::string_view s) {
  trim_ascii(s);
  return starts_with_ci(s, kHnPrefix);
}

bool is_reddit_ref(std::string_view s) {
  trim_ascii(s);
  return starts_with_ci(s, kRedditPrefix);
}

bool is_lemmy_ref(std::string_view s) {
  trim_ascii(s);
  // Needs a host: "lemmy:" alone has no instance to ask.
  return starts_with_ci(s, kLemmyPrefix) && s.size() > kLemmyPrefix.size() &&
         s[kLemmyPrefix.size()] != '/';
}

std::string hn_ref_from_url(std::string_view url) {
  if (!is_hn_url(url)) return {};
  std::string host, rest;
  if (!split_web(url, host, rest)) return {};
  if (rest == "news" || starts_with(rest, "news?") || starts_with(rest, "news#"))
    rest.erase(0, 4);
  return std::string(kHnPrefix) + rest;
}

std::string reddit_ref_from_url(std::string_view url) {
  std::string host, rest;
  if (!split_web(url, host, rest) || !is_reddit_url(url)) return {};
  // redd.it short links name a post id, not a path on reddit.com.
  if (host == "redd.it" || host == "www.redd.it") return {};
  size_t tail = rest.find_first_of("?#");
  std::string path = rest.substr(0, tail);
  const std::string after =
      tail == std::string::npos ? std::string() : rest.substr(tail);
  if (ends_with(path, ".json")) path.resize(path.size() - 5);
  return std::string(kRedditPrefix) + path + after;
}

std::string lemmy_ref_from_url(std::string_view url) {
  std::string host, rest;
  if (!split_web(url, host, rest) || !is_lemmy_url(url)) return {};
  if (rest.find("api/v3/") != std::string::npos) return {};
  return std::string(kLemmyPrefix) + host + "/" + rest;
}

std::string web_url_for(std::string_view ref) {
  if (is_hn_ref(ref))
    return "https://news.ycombinator.com/" +
           std::string(after_prefix(ref, kHnPrefix));
  if (is_reddit_ref(ref))
    return "https://www.reddit.com/" +
           std::string(after_prefix(ref, kRedditPrefix));
  if (is_lemmy_ref(ref))
    return "https://" + std::string(after_prefix(ref, kLemmyPrefix));
  if (auto r = parse_wikipedia_ref(ref)) {
    std::string url = wikipedia_page_url(*r);
    if (!r->fragment.empty()) url += "#" + r->fragment;
    return url;
  }
  return {};
}

std::string native_url_for(std::string_view url) {
  if (auto r = wikipedia_ref_from_url(url)) return wikipedia_ref_string(*r);
  if (std::string r = hn_ref_from_url(url); !r.empty()) return r;
  if (std::string r = reddit_ref_from_url(url); !r.empty()) return r;
  if (std::string r = lemmy_ref_from_url(url); !r.empty()) return r;
  return {};
}

std::string alternate_view_url(std::string_view url) {
  if (std::string native = native_url_for(url); !native.empty()) return native;
  return web_url_for(url);
}

void native_rewrite_links(Document& doc, std::string_view ref) {
  std::string (*to_ref)(std::string_view) = nullptr;
  // Lemmy's path test is too loose for arbitrary links ("/c/" anywhere);
  // lemmy.cpp emits refs for its own post / community links instead.
  if (is_hn_ref(ref)) to_ref = hn_ref_from_url;
  else if (is_reddit_ref(ref)) to_ref = reddit_ref_from_url;
  if (!to_ref) return;
  for (Node& n : doc.nodes)
    for (InlineSpan& s : n.spans) {
      if (s.kind != InlineKind::Link || s.href.empty()) continue;
      if (std::string r = to_ref(s.href); !r.empty()) s.href = std::move(r);
    }
}

}  // namespace xanatoo
