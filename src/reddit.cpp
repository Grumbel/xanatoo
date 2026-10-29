// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only Reddit client via the public HTTPS .json endpoints.
// Maps listings / posts into Thread + Message document nodes.

#include "xanatoo/reddit.hpp"

#include "xanatoo/https.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
// HTTPS via xanatoo/https.hpp (TlsStream)
#endif

namespace xanatoo {

namespace {



// Strip scheme and leading www./old. → host path for host matching.
bool host_is_reddit(std::string_view host) {
  std::string h = ascii_lower(host);
  if (h.rfind("www.", 0) == 0) h = h.substr(4);
  if (h.rfind("old.", 0) == 0) h = h.substr(4);
  if (h.rfind("np.", 0) == 0) h = h.substr(3);
  return h == "reddit.com" || h == "redd.it";
}

}  // namespace

bool is_reddit_url(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  if (s.empty()) return false;

  std::string_view rest = s;
  if (starts_with_ci(rest, "https://")) rest.remove_prefix(8);
  else if (starts_with_ci(rest, "http://")) rest.remove_prefix(7);

  // host[/path…]
  size_t slash = rest.find('/');
  std::string_view host =
      (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
  // drop userinfo / port
  size_t at = host.rfind('@');
  if (at != std::string_view::npos) host = host.substr(at + 1);
  size_t colon = host.rfind(':');
  if (colon != std::string_view::npos) host = host.substr(0, colon);
  if (!host_is_reddit(host)) return false;

  // Accept listing/post paths; bare reddit.com alone is ok (front page).
  return true;
}

std::string reddit_json_url(std::string_view s) {
  if (!is_reddit_url(s)) return {};
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);

  std::string url(s);
  if (!starts_with_ci(url, "http://") && !starts_with_ci(url, "https://"))
    url = "https://" + url;
  // Force https
  if (starts_with_ci(url, "http://"))
    url = "https://" + url.substr(7);

  // Prefer www.reddit.com host
  {
    // https://HOST/path
    size_t scheme = url.find("://");
    if (scheme != std::string::npos) {
      size_t host_begin = scheme + 3;
      size_t host_end = url.find('/', host_begin);
      std::string host = (host_end == std::string::npos)
                             ? url.substr(host_begin)
                             : url.substr(host_begin, host_end - host_begin);
      std::string path = (host_end == std::string::npos) ? "/"
                                                         : url.substr(host_end);
      // drop port from host for rebuild
      size_t colon = host.rfind(':');
      if (colon != std::string::npos) host = host.substr(0, colon);
      std::string hlow = ascii_lower(host);
      if (hlow == "redd.it" || hlow == "www.redd.it") {
        // short links stay; API still works via redirect sometimes — keep path
        url = "https://www.reddit.com" + path;
      } else if (hlow.rfind("old.", 0) == 0 || hlow.rfind("np.", 0) == 0 ||
                 hlow == "reddit.com" || hlow == "www.reddit.com") {
        url = "https://www.reddit.com" + path;
      }
    }
  }

  // Split query / fragment
  std::string path_q = url;
  std::string query;
  size_t hash = path_q.find('#');
  if (hash != std::string::npos) path_q = path_q.substr(0, hash);
  size_t qmark = path_q.find('?');
  if (qmark != std::string::npos) {
    query = path_q.substr(qmark);  // includes '?'
    path_q = path_q.substr(0, qmark);
  }

  // Ensure .json before query
  auto ends_with = [](const std::string& a, const char* suf) {
    size_t n = std::strlen(suf);
    return a.size() >= n && a.compare(a.size() - n, n, suf) == 0;
  };
  if (!ends_with(path_q, ".json")) {
    if (!path_q.empty() && path_q.back() == '/')
      path_q += ".json";
    else
      path_q += "/.json";
  }

  // Limit + raw_json for cleaner text
  if (query.empty())
    query = "?raw_json=1&limit=25";
  else if (query.find("raw_json=") == std::string::npos)
    query += "&raw_json=1";

  return path_q + query;
}

#if defined(XANATOO_REDDIT) && XANATOO_REDDIT

namespace {

std::string reddit_user_agent() {
  const char* env = std::getenv("XANATOO_REDDIT_USER_AGENT");
  if (env && env[0]) return env;
  // Reddit's preferred form: platform:app_id:version (by /u/name)
  return std::string("linux:xanatoo:") + version_string() +
         " (by /u/grumbel; read-only; +https://github.com/Grumbel/xanatoo)";
}

// App-only OAuth token (client credentials). Empty if env not set or fails.
std::string reddit_oauth_token(std::string* err) {
  const char* id = std::getenv("XANATOO_REDDIT_CLIENT_ID");
  const char* sec = std::getenv("XANATOO_REDDIT_CLIENT_SECRET");
  if (!id || !id[0] || !sec || !sec[0]) return {};

  // Basic base64(id:secret) — minimal encoder
  auto b64 = [](const std::string& in) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        o.push_back(T[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    if (valb > -6) o.push_back(T[((val << 8) >> (valb + 8)) & 0x3F]);
    while (o.size() % 4) o.push_back('=');
    return o;
  };
  std::string auth = b64(std::string(id) + ":" + sec);
  std::string extra = "Authorization: Basic " + auth + "\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n";
  auto resp = https_request("POST", "https://www.reddit.com/api/v1/access_token",
                            reddit_user_agent(), extra,
                            "grant_type=client_credentials", err);
  if (!resp || resp->status != 200) {
    if (err && err->empty())
      *err = "OAuth token request failed (HTTP " +
             std::to_string(resp ? resp->status : 0) + ")";
    return {};
  }
  // "access_token":"..."
  {
    const std::string key = "\"access_token\"";
    size_t p = resp->body.find(key);
    if (p != std::string::npos) {
      p = resp->body.find(':', p + key.size());
      if (p != std::string::npos) {
        p = resp->body.find('"', p + 1);
        if (p != std::string::npos) {
          size_t endq = resp->body.find('"', p + 1);
          if (endq != std::string::npos)
            return resp->body.substr(p + 1, endq - p - 1);
        }
      }
    }
  }
  if (err) *err = "OAuth response missing access_token";
  return {};
}

std::string format_time(int64_t ts) {
  if (ts <= 0) return {};
  time_t t = static_cast<time_t>(ts);
  char buf[64];
  struct tm tm {};
  gmtime_r(&t, &tm);
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm) == 0)
    return {};
  return buf;
}

struct RedditPost {
  std::string id;
  std::string name;  // t3_xxx or t1_xxx
  std::string title;
  std::string author;
  std::string selftext;
  std::string url;
  std::string permalink;
  std::string subreddit;
  std::string flair;
  std::string body;  // comments: body markdown
  int64_t created = 0;
  int64_t score = 0;
  int64_t num_comments = 0;
  bool is_self = false;
  std::string kind;  // t3 / t1
  std::string parent_id;
};

RedditPost parse_thing_data(std::string_view thing_json) {
  RedditPost p;
  p.kind = json_str_field(thing_json, "kind");
  std::string_view data = json_object_after_key(thing_json, "data");
  if (data.empty()) data = thing_json;  // already a data object
  p.id = json_str_field(data, "id");
  p.name = json_str_field(data, "name");
  p.title = json_str_field(data, "title");
  p.author = json_str_field(data, "author");
  p.selftext = json_str_field(data, "selftext");
  p.body = json_str_field(data, "body");
  p.url = json_str_field(data, "url");
  p.permalink = json_str_field(data, "permalink");
  p.subreddit = json_str_field(data, "subreddit");
  p.flair = json_str_field(data, "link_flair_text");
  if (p.flair.empty()) p.flair = json_str_field(data, "author_flair_text");
  p.created = json_int_field(data, "created_utc");
  p.score = json_int_field(data, "score");
  p.num_comments = json_int_field(data, "num_comments");
  p.is_self = json_bool_field(data, "is_self");
  p.parent_id = json_str_field(data, "parent_id");
  return p;
}

void append_post_message(Document& doc, NodeId thread, const RedditPost& post,
                         bool is_comment) {
  NodeId msg = doc.create(NodeKind::Message, thread);
  Node& n = doc.get(msg);
  n.label = post.name.empty() ? post.id : post.name;
  n.src = post.author.empty() ? "[deleted]" : post.author;
  std::string meta = format_time(post.created);
  if (post.score != 0) {
    if (!meta.empty()) meta += " · ";
    meta += std::to_string(post.score) + " pts";
  }
  if (!is_comment && post.num_comments > 0) {
    if (!meta.empty()) meta += " · ";
    meta += std::to_string(post.num_comments) + " comments";
  }
  if (!post.flair.empty()) {
    if (!meta.empty()) meta += " · ";
    meta += post.flair;
  }
  n.summary = meta;
  if (is_comment && !post.parent_id.empty())
    n.alt = post.parent_id;

  // Title line for link posts / submissions
  if (!is_comment && !post.title.empty()) {
    NodeId title = doc.create(NodeKind::Paragraph, msg);
    InlineSpan hs;
    hs.kind = InlineKind::Strong;
    hs.text = post.title;
    doc.get(title).spans.push_back(std::move(hs));
  }

  std::string body = is_comment ? post.body : post.selftext;
  // Strip a few common HTML entities Reddit leaves in selftext
  auto replace_all = [](std::string& s, const char* a, const char* b) {
    size_t pos = 0;
    size_t na = std::strlen(a);
    while ((pos = s.find(a, pos)) != std::string::npos) {
      s.replace(pos, na, b);
      pos += std::strlen(b);
    }
  };
  replace_all(body, "&amp;", "&");
  replace_all(body, "&lt;", "<");
  replace_all(body, "&gt;", ">");
  replace_all(body, "&#39;", "'");
  replace_all(body, "&quot;", "\"");

  if (!body.empty()) {
    NodeId para = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(para, body);
  } else if (!is_comment && !post.is_self && !post.url.empty()) {
    NodeId para = doc.create(NodeKind::Paragraph, msg);
    InlineSpan link;
    link.kind = InlineKind::Link;
    link.text = post.url;
    link.href = post.url;
    doc.get(para).spans.push_back(std::move(link));
  }

  if (!is_comment && !post.permalink.empty()) {
    NodeId foot = doc.create(NodeKind::Paragraph, msg);
    InlineSpan link;
    link.kind = InlineKind::Link;
    std::string href = post.permalink;
    if (href.rfind("http", 0) != 0)
      href = "https://www.reddit.com" + href;
    link.text = "comments";
    link.href = href;
    doc.get(foot).spans.push_back(std::move(link));
  }
}

Document posts_to_document(const std::string& title,
                           const std::vector<RedditPost>& posts,
                           const std::vector<RedditPost>& comments) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title.empty() ? "Reddit" : title;
  doc.get(thread).label = "reddit";

  for (const auto& p : posts)
    append_post_message(doc, thread, p, /*is_comment=*/false);
  for (const auto& c : comments)
    append_post_message(doc, thread, c, /*is_comment=*/true);

  if (doc.get(thread).children.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    NodeId body = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(body, "No posts returned from Reddit.");
  }

  NodeId foot = doc.create(NodeKind::Paragraph, root);
  doc.append_text(foot,
                  "Reddit · read-only · Ctrl+L to open another listing or post");
  return doc;
}

void collect_from_listing(std::string_view listing_json,
                          std::vector<RedditPost>& posts,
                          std::vector<RedditPost>& comments) {
  std::string_view data = json_object_after_key(listing_json, "data");
  if (data.empty()) data = listing_json;
  auto children = json_object_array_after_key(data, "children");
  for (const auto& ch : children) {
    RedditPost p = parse_thing_data(ch);
    if (p.kind == "t1" || (!p.body.empty() && p.title.empty()))
      comments.push_back(std::move(p));
    else if (p.kind == "t3" || !p.title.empty())
      posts.push_back(std::move(p));
    else if (!p.body.empty())
      comments.push_back(std::move(p));
  }
}

}  // namespace

std::optional<Document> reddit_fetch(std::string_view url_in, std::string* err) {
  std::string json_url = reddit_json_url(url_in);
  if (json_url.empty()) {
    if (err) *err = "not a Reddit URL";
    return std::nullopt;
  }

  // Candidate request URLs: normalized .json, old.reddit, bare .json path.
  std::vector<std::string> candidates;
  auto add_cand = [&](std::string u) {
    if (u.empty()) return;
    if (std::find(candidates.begin(), candidates.end(), u) == candidates.end())
      candidates.push_back(std::move(u));
  };
  add_cand(json_url);
  {
    std::string alt = json_url;
    auto rep = [&](const char* a, const char* b) {
      size_t p = alt.find(a);
      if (p != std::string::npos) alt.replace(p, std::strlen(a), b);
    };
    rep("://www.reddit.com", "://old.reddit.com");
    add_cand(alt);
    alt = json_url;
    // /r/foo/.json → /r/foo.json
    size_t p = alt.find("/.json");
    if (p != std::string::npos) {
      alt.replace(p, 6, ".json");
      add_cand(alt);
    }
  }

  std::string oauth_err;
  std::string token = reddit_oauth_token(&oauth_err);
  if (!token.empty())
    std::cerr << "Reddit: using OAuth client credentials\n";
  else if (!oauth_err.empty())
    std::cerr << "Reddit: OAuth skipped/failed: " << oauth_err << "\n";

  std::optional<HttpResult> resp;
  std::string last_err;
  for (const std::string& cand : candidates) {
    std::string current = cand;
    // OAuth API host prefers no .json suffix on path — map listing path.
    if (!token.empty()) {
      // https://www.reddit.com/r/X/.json?… → https://oauth.reddit.com/r/X?…
      std::string o = current;
      size_t host = o.find("://");
      if (host != std::string::npos) {
        size_t path_start = o.find('/', host + 3);
        std::string pathq = (path_start == std::string::npos) ? "/"
                                                              : o.substr(path_start);
        size_t dotjson = pathq.find(".json");
        if (dotjson != std::string::npos) pathq.erase(dotjson, 5);
        // collapse /r/foo/ → /r/foo
        if (pathq.size() > 1 && pathq.back() == '/' &&
            pathq.find('?') == std::string::npos)
          pathq.pop_back();
        current = "https://oauth.reddit.com" + pathq;
      }
    }

    for (int hop = 0; hop < 4; ++hop) {
      std::cerr << "Reddit: GET " << current << "\n";
      std::string req_err;
      std::optional<HttpResult> one;
      if (!token.empty()) {
        std::string extra = "Authorization: Bearer " + token + "\r\n";
        one = https_request("GET", current, reddit_user_agent(), extra, {},
                             &req_err);
      } else {
        one = https_get(current, reddit_user_agent(), &req_err);
      }
      if (!one) {
        last_err = req_err.empty() ? "request failed" : req_err;
        break;
      }
      if (one->status >= 300 && one->status < 400 && !one->location.empty()) {
        std::string loc = one->location;
        if (loc.rfind("http", 0) != 0) {
          if (!loc.empty() && loc[0] == '/')
            loc = std::string("https://www.reddit.com") + loc;
          else
            loc = "https://www.reddit.com/" + loc;
        }
        std::string next = reddit_json_url(loc);
        current = next.empty() ? loc : next;
        continue;
      }
      resp = std::move(one);
      break;
    }
    if (resp && resp->status == 200 && !resp->body.empty() &&
        resp->body.find("\"children\"") != std::string::npos)
      break;
    if (resp && resp->status == 200 && !resp->body.empty() &&
        (resp->body[0] == '{' || resp->body[0] == '['))
      break;
    if (resp) {
      last_err = "HTTP " + std::to_string(resp->status);
      if (resp->status == 403)
        last_err +=
            " (Reddit blocked this client/IP — try residential network, or set "
            "XANATOO_REDDIT_CLIENT_ID + XANATOO_REDDIT_CLIENT_SECRET for OAuth)";
      else if (resp->status == 429)
        last_err += " (rate limited — wait and retry)";
      else if (resp->body.size() > 0 && resp->body.size() < 120 &&
               resp->body.find('<') == std::string::npos)
        last_err += ": " + resp->body;
      resp.reset();
    }
  }

  if (!resp) {
    if (err) *err = last_err.empty() ? "no response from Reddit" : last_err;
    return std::nullopt;
  }
  if (resp->status != 200) {
    if (err) *err = last_err.empty()
                        ? ("HTTP " + std::to_string(resp->status))
                        : last_err;
    return std::nullopt;
  }

  std::string& body = resp->body;
  // Trim BOM / leading whitespace
  size_t start = 0;
  while (start < body.size() &&
         std::isspace(static_cast<unsigned char>(body[start])))
    ++start;

  std::vector<RedditPost> posts;
  std::vector<RedditPost> comments;
  std::string title;

  if (start < body.size() && body[start] == '[') {
    // Post page: [ listing(post), listing(comments) ]
    // Split top-level array elements with brace depth.
    size_t p = start + 1;
    while (p < body.size()) {
      while (p < body.size() &&
             (std::isspace(static_cast<unsigned char>(body[p])) ||
              body[p] == ','))
        ++p;
      if (p >= body.size() || body[p] == ']') break;
      if (body[p] != '{') break;
      int depth = 0;
      size_t s0 = p;
      for (size_t i = p; i < body.size(); ++i) {
        char c = body[i];
        if (c == '"') {
          ++i;
          while (i < body.size()) {
            if (body[i] == '\\') {
              i += 2;
              continue;
            }
            if (body[i] == '"') break;
            ++i;
          }
          continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') {
          --depth;
          if (depth == 0) {
            std::string_view slice(body.data() + s0, i - s0 + 1);
            collect_from_listing(slice, posts, comments);
            p = i + 1;
            break;
          }
        }
      }
      if (depth != 0) break;
    }
  } else {
    collect_from_listing(std::string_view(body).substr(start), posts, comments);
  }

  if (!posts.empty() && !posts.front().subreddit.empty())
    title = "r/" + posts.front().subreddit;
  else {
    // Try path: /r/NAME/
    std::string u(url_in);
    auto pos = ascii_lower(u).find("/r/");
    if (pos != std::string::npos) {
      size_t s0 = pos + 3;
      size_t s1 = s0;
      while (s1 < u.size() && (std::isalnum(static_cast<unsigned char>(u[s1])) ||
                               u[s1] == '_'))
        ++s1;
      if (s1 > s0) title = "r/" + u.substr(s0, s1 - s0);
    }
  }
  if (title.empty()) title = "Reddit";
  if (!posts.empty() && posts.size() == 1 && !posts.front().title.empty())
    title = posts.front().title;

  std::cerr << "Reddit: " << posts.size() << " post(s), " << comments.size()
            << " comment(s)\n";
  return posts_to_document(title, posts, comments);
}

#endif  // XANATOO_REDDIT

}  // namespace xanatoo
