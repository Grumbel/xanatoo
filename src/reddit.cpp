// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only Reddit client via the public HTTPS .json endpoints.
// Maps listings / posts into Thread + Message document nodes.

#include "xanatoo/reddit.hpp"

#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace xanatoo {

namespace {

std::string ascii_lower_copy(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (unsigned char c : s)
    o.push_back(static_cast<char>(std::tolower(c)));
  return o;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i])))
      return false;
  }
  return true;
}

// Strip scheme and leading www./old. → host path for host matching.
bool host_is_reddit(std::string_view host) {
  std::string h = ascii_lower_copy(host);
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
      std::string hlow = ascii_lower_copy(host);
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

void ssl_once_init() {
  static bool done = false;
  if (done) return;
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  done = true;
}

int tcp_connect(const std::string& host, int port, std::string* err) {
  addrinfo hints {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  std::string port_s = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) {
    if (err) *err = "DNS failed for " + host;
    return -1;
  }
  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv;
    tv.tv_sec = 12;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0 && err) *err = "connect failed to " + host;
  return fd;
}

struct HttpResult {
  int status = 0;
  std::string body;
  std::string location;
};

std::optional<HttpResult> https_get(const std::string& url, std::string* err) {
  // Parse https://host[:port]/path
  if (!starts_with_ci(url, "https://")) {
    if (err) *err = "only https supported for Reddit";
    return std::nullopt;
  }
  std::string_view rest = std::string_view(url).substr(8);
  size_t slash = rest.find('/');
  std::string host = std::string(
      slash == std::string_view::npos ? rest : rest.substr(0, slash));
  std::string path = std::string(
      slash == std::string_view::npos ? "/" : rest.substr(slash));
  int port = 443;
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) {
    port = std::atoi(host.c_str() + colon + 1);
    if (port <= 0) port = 443;
    host = host.substr(0, colon);
  }

  ssl_once_init();
  int fd = tcp_connect(host, port, err);
  if (fd < 0) return std::nullopt;

  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx) {
    ::close(fd);
    if (err) *err = "SSL_CTX_new failed";
    return std::nullopt;
  }
  SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  SSL* ssl = SSL_new(ctx);
  if (!ssl) {
    SSL_CTX_free(ctx);
    ::close(fd);
    if (err) *err = "SSL_new failed";
    return std::nullopt;
  }
  SSL_set_fd(ssl, fd);
  SSL_set_tlsext_host_name(ssl, host.c_str());
  if (SSL_connect(ssl) != 1) {
    if (err) *err = "TLS handshake failed";
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return std::nullopt;
  }

  // Reddit requires a descriptive User-Agent; generic clients get 403/429.
  std::ostringstream req;
  req << "GET " << path << " HTTP/1.0\r\n"
      << "Host: " << host << "\r\n"
      << "User-Agent: xanatoo/0.1.0 (read-only document viewer; by grumbel)\r\n"
      << "Accept: application/json\r\n"
      << "Connection: close\r\n"
      << "\r\n";
  std::string req_s = req.str();
  if (SSL_write(ssl, req_s.data(), static_cast<int>(req_s.size())) <= 0) {
    if (err) *err = "TLS write failed";
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return std::nullopt;
  }

  std::string data;
  char buf[4096];
  for (;;) {
    int n = SSL_read(ssl, buf, sizeof(buf));
    if (n <= 0) break;
    data.append(buf, static_cast<size_t>(n));
    if (data.size() > 12 * 1024 * 1024) break;
  }
  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(ctx);
  ::close(fd);

  size_t hdr_end = data.find("\r\n\r\n");
  if (hdr_end == std::string::npos) {
    if (err) *err = "malformed HTTP response";
    return std::nullopt;
  }
  std::string headers = data.substr(0, hdr_end);
  HttpResult out;
  out.body = data.substr(hdr_end + 4);

  // Status line
  {
    size_t sp1 = headers.find(' ');
    if (sp1 != std::string::npos) {
      size_t sp2 = headers.find(' ', sp1 + 1);
      std::string code = (sp2 == std::string::npos)
                             ? headers.substr(sp1 + 1, 3)
                             : headers.substr(sp1 + 1, sp2 - sp1 - 1);
      out.status = std::atoi(code.c_str());
    }
  }
  // Location header (case-insensitive)
  {
    std::string hlow = ascii_lower_copy(headers);
    size_t loc = hlow.find("\nlocation:");
    if (loc != std::string::npos) {
      size_t v0 = headers.find(':', loc);
      size_t v1 = headers.find("\r\n", v0);
      if (v0 != std::string::npos && v1 != std::string::npos) {
        std::string val = headers.substr(v0 + 1, v1 - v0 - 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
          val.erase(val.begin());
        out.location = val;
      }
    }
  }
  return out;
}

// --- minimal JSON helpers (string / number / object slices) ---

std::string json_unescape(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      if (c == 'n') o.push_back('\n');
      else if (c == 'r') o.push_back('\r');
      else if (c == 't') o.push_back('\t');
      else if (c == '"' || c == '\\' || c == '/') o.push_back(c);
      else if (c == 'u' && i + 4 < s.size()) {
        i += 4;
        o.push_back('?');
      } else
        o.push_back(c);
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

std::string json_str_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = 0;
  while ((p = json.find(pat, p)) != std::string_view::npos) {
    // Ensure this looks like a key (preceded by { , or whitespace start)
    if (p > 0) {
      char prev = json[p - 1];
      if (prev != '{' && prev != ',' && prev != ' ' && prev != '\n' &&
          prev != '\r' && prev != '\t') {
        p += pat.size();
        continue;
      }
    }
    size_t c = json.find(':', p + pat.size());
    if (c == std::string_view::npos) return {};
    size_t v = c + 1;
    while (v < json.size() &&
           std::isspace(static_cast<unsigned char>(json[v])))
      ++v;
    if (v >= json.size()) return {};
    if (json[v] == 'n' && json.substr(v, 4) == "null") return {};
    if (json[v] != '"') return {};
    ++v;
    std::string out;
    while (v < json.size()) {
      if (json[v] == '\\' && v + 1 < json.size()) {
        out.push_back('\\');
        out.push_back(json[v + 1]);
        v += 2;
        continue;
      }
      if (json[v] == '"') break;
      out.push_back(json[v++]);
    }
    return json_unescape(out);
  }
  return {};
}

int64_t json_int_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return 0;
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return 0;
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  return std::strtoll(std::string(json.substr(p, 24)).c_str(), nullptr, 10);
}

bool json_bool_field(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return false;
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return false;
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  return p + 4 <= json.size() && json.substr(p, 4) == "true";
}

// Find the "data" object that is the value of a top-level (or listing) key,
// returning a view of the {...} body. Shallow brace matcher from first '{'.
std::string_view json_object_after_key(std::string_view json,
                                       std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  size_t p = json.find(pat);
  if (p == std::string_view::npos) return {};
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return {};
  ++p;
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
    ++p;
  if (p >= json.size() || json[p] != '{') return {};
  int depth = 0;
  size_t start = p;
  for (size_t i = p; i < json.size(); ++i) {
    char c = json[i];
    if (c == '"') {
      ++i;
      while (i < json.size()) {
        if (json[i] == '\\') {
          i += 2;
          continue;
        }
        if (json[i] == '"') break;
        ++i;
      }
      continue;
    }
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) return json.substr(start, i - start + 1);
    }
  }
  return {};
}

// Extract each element of the "children" array as a raw JSON object string.
std::vector<std::string> json_children_objects(std::string_view listing_obj) {
  std::vector<std::string> out;
  std::string pat = "\"children\"";
  size_t p = listing_obj.find(pat);
  if (p == std::string_view::npos) return out;
  p = listing_obj.find('[', p + pat.size());
  if (p == std::string_view::npos) return out;
  ++p;
  while (p < listing_obj.size()) {
    while (p < listing_obj.size() &&
           (std::isspace(static_cast<unsigned char>(listing_obj[p])) ||
            listing_obj[p] == ','))
      ++p;
    if (p >= listing_obj.size() || listing_obj[p] == ']') break;
    if (listing_obj[p] != '{') break;
    int depth = 0;
    size_t start = p;
    for (size_t i = p; i < listing_obj.size(); ++i) {
      char c = listing_obj[i];
      if (c == '"') {
        ++i;
        while (i < listing_obj.size()) {
          if (listing_obj[i] == '\\') {
            i += 2;
            continue;
          }
          if (listing_obj[i] == '"') break;
          ++i;
        }
        continue;
      }
      if (c == '{') ++depth;
      else if (c == '}') {
        --depth;
        if (depth == 0) {
          out.emplace_back(listing_obj.substr(start, i - start + 1));
          p = i + 1;
          break;
        }
      }
    }
    if (depth != 0) break;
  }
  return out;
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
  auto children = json_children_objects(data);
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

  std::string current = json_url;
  std::optional<HttpResult> resp;
  for (int hop = 0; hop < 5; ++hop) {
    std::cerr << "Reddit: GET " << current << "\n";
    resp = https_get(current, err);
    if (!resp) return std::nullopt;
    if (resp->status >= 300 && resp->status < 400 && !resp->location.empty()) {
      std::string loc = resp->location;
      if (loc.rfind("http", 0) != 0) {
        // relative redirect
        if (!loc.empty() && loc[0] == '/')
          loc = "https://www.reddit.com" + loc;
        else
          loc = "https://www.reddit.com/" + loc;
      }
      // Ensure .json on redirect target
      current = reddit_json_url(loc);
      if (current.empty()) current = loc;
      continue;
    }
    break;
  }
  if (!resp) return std::nullopt;
  if (resp->status != 200) {
    if (err)
      *err = "HTTP " + std::to_string(resp->status) +
             (resp->body.size() > 80
                  ? (": " + resp->body.substr(0, 80))
                  : "");
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
    auto pos = ascii_lower_copy(u).find("/r/");
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
