// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only Lemmy client via HTTP API v3 (JSON).
// Maps posts/comments into Thread + Message document nodes.

#include "xanatoo/lemmy.hpp"
#include "xanatoo/util.hpp"

#include "xanatoo/https.hpp"
#include "xanatoo/json_min.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(XANATOO_LEMMY) && XANATOO_LEMMY
// HTTPS via xanatoo/https.hpp (TlsStream)
#endif

namespace xanatoo {

namespace {




// Path looks like Lemmy UI or API.
bool path_looks_lemmy(std::string_view path) {
  if (path.empty()) return false;
  // strip query
  size_t q = path.find('?');
  if (q != std::string_view::npos) path = path.substr(0, q);
  size_t h = path.find('#');
  if (h != std::string_view::npos) path = path.substr(0, h);
  std::string low = ascii_lower(path);
  if (low.rfind("/c/", 0) == 0 || low.find("/c/") != std::string::npos)
    return true;
  if (low.rfind("/post/", 0) == 0 || low.find("/post/") != std::string::npos)
    return true;
  if (low.find("/api/v3/") != std::string::npos) return true;
  // /u/name is common but too generic — skip
  return false;
}

}  // namespace

bool is_lemmy_url(std::string_view s) {
  trim_ascii(s);
  if (s.empty()) return false;
  std::string_view rest = s;
  if (starts_with_ci(rest, "https://")) rest.remove_prefix(8);
  else if (starts_with_ci(rest, "http://")) rest.remove_prefix(7);
  else return false;  // require scheme to avoid eating random paths

  size_t slash = rest.find('/');
  if (slash == std::string_view::npos) return false;
  std::string_view path = rest.substr(slash);
  return path_looks_lemmy(path);
}

#if defined(XANATOO_LEMMY) && XANATOO_LEMMY

namespace {

std::string user_agent() {
  const char* env = std::getenv("XANATOO_LEMMY_USER_AGENT");
  if (env && env[0]) return env;
  return "xanatoo/0.1.0 (read-only Lemmy client; by grumbel; +https://github.com/Grumbel/xanatoo)";
}

std::string format_time_iso(std::string_view iso) {
  // "2024-01-15T12:34:56.789Z" → "2024-01-15 12:34 UTC"
  if (iso.size() < 16) return std::string(iso);
  std::string o;
  o.append(iso.substr(0, 10));
  o += " ";
  o.append(iso.substr(11, 5));
  o += " UTC";
  return o;
}

struct LemmyPost {
  int64_t id = 0;
  std::string title;
  std::string body;
  std::string url;           // external link URL (link posts)
  std::string ap_id;         // ActivityPub id, usually https://host/post/N
  std::string author;
  std::string community;
  std::string community_url; // https://host/c/name when known
  std::string published;
  int64_t score = 0;
  int64_t comments = 0;
  bool is_comment = false;
  int64_t parent_id = 0;  // parent comment id (0 = under post)
  std::string path;      // Lemmy comment path for sort/order
};

LemmyPost parse_post_view(std::string_view view_json) {
  LemmyPost p;
  std::string_view post = json_object_after_key(view_json, "post");
  std::string_view creator = json_object_after_key(view_json, "creator");
  std::string_view community = json_object_after_key(view_json, "community");
  std::string_view counts = json_object_after_key(view_json, "counts");
  if (post.empty()) post = view_json;
  p.id = json_int_field(post, "id");
  p.title = json_str_field(post, "name");
  p.body = json_str_field(post, "body");
  p.url = json_str_field(post, "url");
  p.ap_id = json_str_field(post, "ap_id");
  p.published = json_str_field(post, "published");
  p.author = json_str_field(creator, "name");
  p.community = json_str_field(community, "name");
  if (p.community.empty()) p.community = json_str_field(community, "title");
  p.community_url = json_str_field(community, "actor_id");
  p.score = json_int_field(counts, "score");
  p.comments = json_int_field(counts, "comments");
  return p;
}

LemmyPost parse_comment_view(std::string_view view_json) {
  LemmyPost p;
  p.is_comment = true;
  std::string_view comment = json_object_after_key(view_json, "comment");
  std::string_view creator = json_object_after_key(view_json, "creator");
  std::string_view counts = json_object_after_key(view_json, "counts");
  if (comment.empty()) comment = view_json;
  p.id = json_int_field(comment, "id");
  p.body = json_str_field(comment, "content");
  p.published = json_str_field(comment, "published");
  p.author = json_str_field(creator, "name");
  p.score = json_int_field(counts, "score");
  // Lemmy path: "0.<id>" (top-level) or "0.<parent>.….<id>" (nested).
  // parent_id = immediate parent comment id, or 0 if top-level under the post.
  p.path = json_str_field(comment, "path");
  if (!p.path.empty()) {
    std::vector<int64_t> segs;
    size_t i = 0;
    while (i < p.path.size()) {
      while (i < p.path.size() && p.path[i] == '.') ++i;
      if (i >= p.path.size()) break;
      char* end = nullptr;
      long long v = std::strtoll(p.path.c_str() + i, &end, 10);
      if (end == p.path.c_str() + i) break;
      segs.push_back(static_cast<int64_t>(v));
      i = static_cast<size_t>(end - p.path.c_str());
    }
    // segs[0] is usually 0 (root); last is this comment id.
    if (segs.size() >= 3)
      p.parent_id = segs[segs.size() - 2];
    else
      p.parent_id = 0;
  }
  return p;
}

// Prefer ActivityPub id; else synthesize https://host/post/id from origin.
std::string post_permalink(const LemmyPost& post, const std::string& origin) {
  if (!post.ap_id.empty() && post.ap_id.rfind("http", 0) == 0)
    return post.ap_id;
  if (post.id > 0 && !origin.empty())
    return origin + "/post/" + std::to_string(post.id);
  return {};
}

void append_message(Document& doc, NodeId thread, const LemmyPost& post,
                    const std::string& origin, int64_t root_post_id = 0) {
  NodeId msg = doc.create(NodeKind::Message, thread);
  Node& n = doc.get(msg);
  n.label = std::to_string(post.id);
  n.src = post.author.empty() ? "[deleted]" : post.author;
  std::string meta;
  if (!post.published.empty()) meta = format_time_iso(post.published);
  if (post.score != 0) {
    if (!meta.empty()) meta += " · ";
    meta += std::to_string(post.score) + " pts";
  }
  n.summary = meta;

  // Threading: Message layout walks alt → parent Message.label for indent.
  if (post.is_comment) {
    if (post.parent_id > 0)
      n.alt = std::to_string(post.parent_id);
    else if (root_post_id > 0)
      n.alt = std::to_string(root_post_id);
  }

  const std::string permalink = post_permalink(post, origin);

  if (!post.is_comment && !post.title.empty()) {
    NodeId title = doc.create(NodeKind::Paragraph, msg);
    InlineSpan hs;
    // Title is a link to the post page so a click loads comments in-pane.
    if (!permalink.empty()) {
      hs.kind = InlineKind::Link;
      hs.text = post.title;
      hs.href = permalink;
    } else {
      hs.kind = InlineKind::Strong;
      hs.text = post.title;
    }
    doc.get(title).spans.push_back(std::move(hs));
  }

  if (!post.body.empty()) {
    NodeId para = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(para, post.body);
  }
  // Link posts: show external URL (may open in browser if not Lemmy).
  if (!post.is_comment && !post.url.empty() && post.url != permalink) {
    NodeId para = doc.create(NodeKind::Paragraph, msg);
    InlineSpan link;
    link.kind = InlineKind::Link;
    link.text = post.url;
    link.href = post.url;
    doc.get(para).spans.push_back(std::move(link));
  }

  // Footer: community + comments links (in-app Lemmy navigation).
  if (!post.is_comment) {
    NodeId foot = doc.create(NodeKind::Paragraph, msg);
    bool any = false;
    if (!post.community.empty()) {
      InlineSpan c;
      c.kind = InlineKind::Link;
      c.text = "c/" + post.community;
      if (!post.community_url.empty())
        c.href = post.community_url;
      else if (!origin.empty())
        c.href = origin + "/c/" + post.community;
      else
        c.kind = InlineKind::Emphasis;
      doc.get(foot).spans.push_back(std::move(c));
      any = true;
    }
    if (!permalink.empty()) {
      if (any) {
        InlineSpan sep;
        sep.kind = InlineKind::Text;
        sep.text = "  ·  ";
        doc.get(foot).spans.push_back(std::move(sep));
      }
      InlineSpan cl;
      cl.kind = InlineKind::Link;
      cl.text = post.comments > 0
                    ? (std::to_string(post.comments) + " comments")
                    : "comments";
      cl.href = permalink;
      doc.get(foot).spans.push_back(std::move(cl));
    }
  }
}

Document build_document(const std::string& title,
                        const std::vector<LemmyPost>& posts,
                        std::vector<LemmyPost> comments,
                        const std::string& origin) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title.empty() ? "Lemmy" : title;
  doc.get(thread).label = "lemmy";

  int64_t root_post_id = posts.empty() ? 0 : posts.front().id;

  for (const auto& p : posts) append_message(doc, thread, p, origin, 0);

  // Tree order: Lemmy path sorts parents before children.
  std::sort(comments.begin(), comments.end(),
            [](const LemmyPost& a, const LemmyPost& b) {
              if (a.path != b.path) return a.path < b.path;
              return a.id < b.id;
            });
  for (const auto& c : comments)
    append_message(doc, thread, c, origin, root_post_id);

  if (doc.get(thread).children.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    NodeId body = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(body, "No posts returned from Lemmy.");
  }

  NodeId foot = doc.create(NodeKind::Paragraph, root);
  doc.append_text(foot,
                  "Lemmy · read-only · Ctrl+L to open another community or post");
  return doc;
}

struct ParsedUrl {
  std::string origin;       // https://host
  std::string community;    // from /c/name
  int64_t post_id = 0;      // from /post/id
  std::string api_path;     // if already /api/v3/...
};

ParsedUrl parse_lemmy_url(std::string_view s) {
  ParsedUrl out;
  trim_ascii(s);
  std::string url(s);
  if (starts_with_ci(url, "http://"))
    url = "https://" + url.substr(7);
  size_t scheme = url.find("://");
  if (scheme == std::string::npos) return out;
  size_t host_begin = scheme + 3;
  size_t path_start = url.find('/', host_begin);
  std::string host = (path_start == std::string::npos)
                         ? url.substr(host_begin)
                         : url.substr(host_begin, path_start - host_begin);
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) host = host.substr(0, colon);
  out.origin = "https://" + host;
  std::string path = (path_start == std::string::npos) ? "/" : url.substr(path_start);
  size_t hash = path.find('#');
  if (hash != std::string::npos) path = path.substr(0, hash);
  size_t qmark = path.find('?');
  std::string query;
  if (qmark != std::string::npos) {
    query = path.substr(qmark);
    path = path.substr(0, qmark);
  }

  std::string low = ascii_lower(path);
  if (low.find("/api/v3/") != std::string::npos) {
    out.api_path = path + query;
    return out;
  }

  // /c/community or /c/community@other
  size_t cpos = low.find("/c/");
  if (cpos != std::string::npos) {
    size_t s0 = cpos + 3;
    size_t s1 = s0;
    while (s1 < path.size() && path[s1] != '/' && path[s1] != '?' &&
           path[s1] != '#')
      ++s1;
    out.community = path.substr(s0, s1 - s0);
  }

  size_t ppos = low.find("/post/");
  if (ppos != std::string::npos) {
    size_t s0 = ppos + 6;
    out.post_id = std::strtoll(path.c_str() + s0, nullptr, 10);
  }
  return out;
}

}  // namespace

std::optional<Document> lemmy_fetch(std::string_view url_in, std::string* err) {
  if (!is_lemmy_url(url_in)) {
    if (err) *err = "not a Lemmy community/post URL";
    return std::nullopt;
  }
  ParsedUrl pu = parse_lemmy_url(url_in);
  if (pu.origin.empty()) {
    if (err) *err = "could not parse Lemmy URL";
    return std::nullopt;
  }

  std::string api_url;
  if (!pu.api_path.empty()) {
    api_url = pu.origin + pu.api_path;
  } else if (pu.post_id > 0) {
    api_url = pu.origin + "/api/v3/post?id=" + std::to_string(pu.post_id);
  } else if (!pu.community.empty()) {
    api_url = pu.origin + "/api/v3/post/list?community_name=" + pu.community +
              "&limit=25&sort=Hot";
  } else {
    api_url = pu.origin + "/api/v3/post/list?limit=25&sort=Hot";
  }

  std::string current = api_url;
  std::optional<HttpResult> resp;
  for (int hop = 0; hop < 4; ++hop) {
    std::cerr << "Lemmy: GET " << current << "\n";
    std::string req_err;
    resp = https_get(current, user_agent(), &req_err);
    if (!resp) {
      if (err) *err = req_err.empty() ? "request failed" : req_err;
      return std::nullopt;
    }
    if (resp->status >= 300 && resp->status < 400 && !resp->location.empty()) {
      std::string loc = resp->location;
      if (loc.rfind("http", 0) != 0) {
        if (!loc.empty() && loc[0] == '/')
          loc = pu.origin + loc;
        else
          loc = pu.origin + "/" + loc;
      }
      current = loc;
      continue;
    }
    break;
  }

  if (!resp || resp->status != 200) {
    if (err)
      *err = "HTTP " + std::to_string(resp ? resp->status : 0) +
             (resp && resp->status == 403
                  ? " (instance blocked this client)"
                  : "");
    return std::nullopt;
  }

  std::string& body = resp->body;
  size_t start = 0;
  while (start < body.size() &&
         std::isspace(static_cast<unsigned char>(body[start])))
    ++start;
  std::string_view json(body.data() + start, body.size() - start);

  std::vector<LemmyPost> posts;
  std::vector<LemmyPost> comments;
  std::string title;

  // Listing: { "posts": [ post_view, ... ] }
  // Single post (newer Lemmy): { "post_view": ... } without embedded comments —
  // comments come from GET /api/v3/comment/list?post_id=…
  std::string_view post_view = json_object_after_key(json, "post_view");
  if (!post_view.empty()) {
    LemmyPost p = parse_post_view(post_view);
    posts.push_back(p);
    if (!p.title.empty()) title = p.title;
    else if (!p.community.empty()) title = "c/" + p.community;
    // Legacy: comments array on the same payload (older instances).
    auto comment_views = json_object_array_after_key(json, "comments");
    for (const auto& cv : comment_views)
      comments.push_back(parse_comment_view(cv));
  } else {
    auto post_views = json_object_array_after_key(json, "posts");
    for (const auto& pv : post_views)
      posts.push_back(parse_post_view(pv));
    if (!pu.community.empty())
      title = "c/" + pu.community;
    else if (!posts.empty() && !posts.front().community.empty())
      title = "c/" + posts.front().community;
    else
      title = "Lemmy";
  }

  // Fetch comments for a single post when the post endpoint omitted them.
  int64_t need_comments_for = 0;
  if (comments.empty()) {
    if (pu.post_id > 0)
      need_comments_for = pu.post_id;
    else if (posts.size() == 1 && posts.front().id > 0)
      need_comments_for = posts.front().id;
  }
  if (need_comments_for > 0) {
    std::string curl =
        pu.origin + "/api/v3/comment/list?post_id=" +
        std::to_string(need_comments_for) +
        "&limit=80&max_depth=8&sort=Hot";
    std::cerr << "Lemmy: GET " << curl << "\n";
    std::string cerr;
    auto cresp = https_get(curl, user_agent(), &cerr);
    if (cresp && cresp->status == 200 && !cresp->body.empty()) {
      size_t cs = 0;
      while (cs < cresp->body.size() &&
             std::isspace(static_cast<unsigned char>(cresp->body[cs])))
        ++cs;
      std::string_view cjson(cresp->body.data() + cs,
                             cresp->body.size() - cs);
      auto comment_views = json_object_array_after_key(cjson, "comments");
      for (const auto& cv : comment_views)
        comments.push_back(parse_comment_view(cv));
    } else {
      std::cerr << "Lemmy: comment list failed"
                << (cerr.empty() ? "" : (": " + cerr)) << "\n";
    }
  }

  std::cerr << "Lemmy: " << posts.size() << " post(s), " << comments.size()
            << " comment(s)\n";
  return build_document(title, posts, comments, pu.origin);
}

#endif  // XANATOO_LEMMY

}  // namespace xanatoo
