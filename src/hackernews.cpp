// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only Hacker News client. Ranked story lists come from the official
// Firebase API (ids only); titles / scores and whole comment trees come
// from the Algolia HN API, which returns a nested thread in one request.
// Maps stories/comments into Thread + Message document nodes.

#include "xanatoo/hackernews.hpp"
#include "xanatoo/util.hpp"

#include "xanatoo/https.hpp"
#include "xanatoo/json_min.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xanatoo {

namespace {

constexpr int kStoriesPerPage = 30;

struct HnUrl {
  std::string list;  // "top" / "new" / "best" / "ask" / "show"; empty = item
  int64_t item = 0;
  int page = 1;
};

// Value of `key=` in a query string ("id=123&p=2"), or empty.
std::string query_param(std::string_view query, std::string_view key) {
  size_t p = 0;
  while (p < query.size()) {
    size_t amp = query.find('&', p);
    std::string_view kv = query.substr(
        p, amp == std::string_view::npos ? std::string_view::npos : amp - p);
    if (kv.size() > key.size() && kv.substr(0, key.size()) == key &&
        kv[key.size()] == '=')
      return std::string(kv.substr(key.size() + 1));
    if (amp == std::string_view::npos) break;
    p = amp + 1;
  }
  return {};
}

std::optional<HnUrl> parse_hn_url(std::string_view s) {
  trim_ascii(s);
  if (starts_with_ci(s, "https://")) s.remove_prefix(8);
  else if (starts_with_ci(s, "http://")) s.remove_prefix(7);
  else return std::nullopt;
  size_t slash = s.find_first_of("/?#");
  std::string host = ascii_lower(s.substr(0, slash));
  if (host != "news.ycombinator.com") return std::nullopt;
  std::string_view path =
      slash == std::string_view::npos ? std::string_view{} : s.substr(slash);
  if (size_t h = path.find('#'); h != std::string_view::npos)
    path = path.substr(0, h);
  std::string_view query;
  if (size_t q = path.find('?'); q != std::string_view::npos) {
    query = path.substr(q + 1);
    path = path.substr(0, q);
  }
  HnUrl out;
  if (path.empty() || path == "/" || path == "/news") out.list = "top";
  else if (path == "/newest") out.list = "new";
  else if (path == "/best") out.list = "best";
  else if (path == "/ask") out.list = "ask";
  else if (path == "/show") out.list = "show";
  else if (path == "/item") {
    out.item = std::strtoll(query_param(query, "id").c_str(), nullptr, 10);
    if (out.item <= 0) return std::nullopt;
    return out;
  } else {
    return std::nullopt;
  }
  out.page = std::max(1, std::atoi(query_param(query, "p").c_str()));
  return out;
}

// --- comment HTML ----------------------------------------------------------

// Attribute value from the inside of a tag ("a href=\"x\" rel=…").
std::string tag_attr(std::string_view tag, std::string_view name) {
  std::string pat = std::string(name) + "=\"";
  size_t p = tag.find(pat);
  if (p == std::string_view::npos) return {};
  p += pat.size();
  size_t e = tag.find('"', p);
  if (e == std::string_view::npos) return {};
  return html_decode_entities(tag.substr(p, e - p));
}

void push_span(std::vector<InlineSpan>& spans, InlineKind kind,
               std::string text, const std::string& href = {}) {
  if (text.empty()) return;
  if (!spans.empty() && spans.back().kind == kind && spans.back().href == href) {
    spans.back().text += text;
    return;
  }
  InlineSpan s;
  s.kind = kind;
  s.text = std::move(text);
  s.href = href;
  spans.push_back(std::move(s));
}

// One paragraph of inline HTML → spans (links, italics; other tags dropped).
std::vector<InlineSpan> inline_html(std::string_view html) {
  std::vector<InlineSpan> spans;
  bool italic = false;
  std::string href;  // inside <a>
  std::string text;
  auto flush = [&] {
    std::string t = html_decode_entities(text);
    text.clear();
    if (!href.empty())
      push_span(spans, InlineKind::Link, std::move(t), href);
    else
      push_span(spans, italic ? InlineKind::Emphasis : InlineKind::Text,
                std::move(t));
  };
  for (size_t i = 0; i < html.size(); ++i) {
    if (html[i] != '<') {
      text.push_back(html[i] == '\n' ? ' ' : html[i]);
      continue;
    }
    size_t e = html.find('>', i);
    if (e == std::string_view::npos) {
      text.append(html.substr(i));
      break;
    }
    std::string_view tag = html.substr(i + 1, e - i - 1);
    std::string name = ascii_lower(tag.substr(0, tag.find(' ')));
    flush();
    if (name == "a") href = tag_attr(tag, "href");
    else if (name == "/a") href.clear();
    else if (name == "i" || name == "em") italic = true;
    else if (name == "/i" || name == "/em") italic = false;
    i = e;
  }
  flush();
  return spans;
}

void append_paragraph(Document& doc, NodeId parent, std::string_view html) {
  trim_ascii(html);
  if (html.empty()) return;
  // HN has no quote markup; "> text" (or "&gt; text") is the convention.
  NodeKind kind = NodeKind::Paragraph;
  if (starts_with(html, "&gt;") || starts_with(html, ">")) {
    kind = NodeKind::BlockQuote;
    html.remove_prefix(html[0] == '>' ? 1 : 4);
    trim_ascii(html);
  }
  NodeId n = doc.create(kind, parent);
  doc.get(n).spans = inline_html(html);
}

std::string format_time_iso(std::string_view iso) {
  // "2024-01-15T12:34:56.000Z" → "2024-01-15 12:34 UTC"
  if (iso.size() < 16) return std::string(iso);
  return std::string(iso.substr(0, 10)) + " " + std::string(iso.substr(11, 5)) +
         " UTC";
}

std::string item_url(int64_t id) {
  return "https://news.ycombinator.com/item?id=" + std::to_string(id);
}

}  // namespace

bool is_hn_url(std::string_view s) { return parse_hn_url(s).has_value(); }

void hn_append_html(Document& doc, NodeId parent, std::string_view html) {
  size_t i = 0;
  size_t block = 0;  // start of the current paragraph
  while (i < html.size()) {
    if (html[i] != '<') {
      ++i;
      continue;
    }
    if (starts_with_ci(html.substr(i), "<p>")) {
      append_paragraph(doc, parent, html.substr(block, i - block));
      i += 3;
      block = i;
    } else if (starts_with_ci(html.substr(i), "<pre>")) {
      append_paragraph(doc, parent, html.substr(block, i - block));
      size_t end = html.find("</pre>", i);
      if (end == std::string_view::npos) end = html.size();
      std::string code;
      for (size_t k = i + 5; k < end; ++k) {  // drop <code> wrappers
        if (html[k] == '<') {
          size_t g = html.find('>', k);
          if (g == std::string_view::npos || g > end) break;
          k = g;
          continue;
        }
        code.push_back(html[k]);
      }
      code = html_decode_entities(code);
      while (!code.empty() && (code.back() == '\n' || code.back() == ' '))
        code.pop_back();
      if (!code.empty()) {
        NodeId n = doc.create(NodeKind::CodeBlock, parent);
        doc.append_text(n, code);
      }
      i = std::min(html.size(), end + 6);
      block = i;
    } else {
      ++i;
    }
  }
  append_paragraph(doc, parent, html.substr(block));
}

#if defined(XANATOO_HN) && XANATOO_HN

namespace {

const char* kUserAgent =
    "xanatoo/0.1.0 (read-only Hacker News client; +https://github.com/Grumbel/xanatoo)";

struct HnItem {
  int64_t id = 0;
  int64_t parent = 0;  // 0 for the story
  std::string author;
  std::string created;
  std::string title;
  std::string url;
  std::string text;  // HTML
  int64_t points = 0;
  int64_t comments = 0;
  std::vector<HnItem> children;
};

std::optional<std::string> get_json(const std::string& url, std::string* err) {
  std::cerr << "HN: GET " << url << "\n";
  auto resp = https_get_follow(url, kUserAgent, err);
  if (!resp) return std::nullopt;
  if (resp->status != 200) {
    if (err) *err = "HTTP " + std::to_string(resp->status) + " from " + url;
    return std::nullopt;
  }
  return std::move(resp->body);
}

// "[1,2,3]" → ids.
std::vector<int64_t> parse_id_list(std::string_view json) {
  std::vector<int64_t> ids;
  std::string s(json);
  const char* p = s.c_str();
  while (*p) {
    if (*p >= '0' && *p <= '9') {
      char* end = nullptr;
      ids.push_back(std::strtoll(p, &end, 10));
      p = end;
    } else {
      ++p;
    }
  }
  return ids;
}

// The "children" array of `obj` itself (not of a nested object).
std::vector<std::string> own_children(std::string_view obj) {
  int depth = 0;
  for (size_t i = 0; i < obj.size(); ++i) {
    char c = obj[i];
    if (c == '"') {
      size_t start = i++;
      while (i < obj.size() && obj[i] != '"') i += (obj[i] == '\\') ? 2 : 1;
      if (depth == 1 && obj.substr(start, i - start + 1) == "\"children\"")
        return json_object_array_after_key(obj.substr(start), "children");
      continue;
    }
    if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') --depth;
  }
  return {};
}

HnItem parse_item(std::string_view obj) {
  const std::string top = json_top_level(obj);
  HnItem it;
  it.id = json_int_field(top, "id");
  if (it.id == 0) it.id = std::atoll(json_str_field(top, "objectID").c_str());
  it.parent = json_int_field(top, "parent_id");
  it.author = json_str_field(top, "author");
  it.created = json_str_field(top, "created_at");
  it.title = json_str_field(top, "title");
  it.url = json_str_field(top, "url");
  it.text = json_str_field(top, "text");
  if (it.text.empty()) it.text = json_str_field(top, "story_text");
  it.points = json_int_field(top, "points");
  it.comments = json_int_field(top, "num_comments");
  for (const auto& c : own_children(obj)) it.children.push_back(parse_item(c));
  return it;
}

int64_t count_comments(const HnItem& it) {
  int64_t n = 0;
  for (const auto& c : it.children) n += 1 + count_comments(c);
  return n;
}

std::string url_host(const std::string& url) {
  size_t s = url.find("://");
  if (s == std::string::npos) return {};
  s += 3;
  size_t e = url.find_first_of("/?#", s);
  std::string h = url.substr(s, e == std::string::npos ? std::string::npos : e - s);
  if (starts_with(h, "www.")) h.erase(0, 4);
  return h;
}

void append_link(Document& doc, NodeId para, std::string text, std::string href) {
  InlineSpan s;
  s.kind = InlineKind::Link;
  s.text = std::move(text);
  s.href = std::move(href);
  doc.get(para).spans.push_back(std::move(s));
}

// Story card: title (→ article), optional text, footer (host · comments).
NodeId append_story(Document& doc, NodeId thread, const HnItem& st,
                    const std::string& rank, bool with_text) {
  NodeId msg = doc.create(NodeKind::Message, thread);
  Node& n = doc.get(msg);
  n.label = std::to_string(st.id);
  n.src = st.author.empty() ? "[deleted]" : st.author;
  std::string meta = rank;
  if (st.points != 0) {
    if (!meta.empty()) meta += " · ";
    meta += std::to_string(st.points) + " points";
  }
  if (!st.created.empty()) {
    if (!meta.empty()) meta += " · ";
    meta += format_time_iso(st.created);
  }
  n.summary = meta;

  NodeId title = doc.create(NodeKind::Paragraph, msg);
  if (!st.url.empty()) {
    append_link(doc, title, st.title, st.url);
  } else {
    InlineSpan s;
    s.kind = with_text ? InlineKind::Strong : InlineKind::Link;
    s.text = st.title.empty() ? "(untitled)" : st.title;
    if (!with_text) s.href = item_url(st.id);
    doc.get(title).spans.push_back(std::move(s));
  }
  if (with_text && !st.text.empty()) hn_append_html(doc, msg, st.text);

  NodeId foot = doc.create(NodeKind::Paragraph, msg);
  if (std::string host = url_host(st.url); !host.empty())
    doc.append_text(foot, host + "  ·  ");
  append_link(doc, foot,
              st.comments > 0 ? std::to_string(st.comments) + " comments"
                              : "discuss",
              item_url(st.id));
  return msg;
}

// Comments depth-first; threading via alt → parent Message.label.
void append_comments(Document& doc, NodeId thread, const HnItem& parent) {
  for (const HnItem& c : parent.children) {
    const bool gone = c.author.empty() && c.text.empty();
    if (gone && c.children.empty()) continue;
    NodeId msg = doc.create(NodeKind::Message, thread);
    Node& n = doc.get(msg);
    n.label = std::to_string(c.id);
    n.alt = std::to_string(parent.id);
    n.src = c.author.empty() ? "[deleted]" : c.author;
    n.summary = format_time_iso(c.created);
    if (gone) {
      NodeId p = doc.create(NodeKind::Paragraph, msg);
      doc.append_text(p, "[deleted]", InlineKind::Emphasis);
    } else {
      hn_append_html(doc, msg, c.text);
    }
    append_comments(doc, thread, c);
  }
}

const char* list_title(const std::string& list) {
  if (list == "new") return "New";
  if (list == "best") return "Best";
  if (list == "ask") return "Ask HN";
  if (list == "show") return "Show HN";
  return "Top";
}

// "Top · New · Best · Ask · Show" header line.
void append_nav(Document& doc, NodeId root, const std::string& current) {
  NodeId nav = doc.create(NodeKind::Paragraph, root);
  static const char* kLists[][2] = {{"top", "news"},
                                    {"new", "newest"},
                                    {"best", "best"},
                                    {"ask", "ask"},
                                    {"show", "show"}};
  doc.append_text(nav, "Hacker News: ", InlineKind::Strong);
  bool first = true;
  for (const auto& l : kLists) {
    if (!first) doc.append_text(nav, " · ");
    first = false;
    if (current == l[0])
      doc.append_text(nav, list_title(l[0]), InlineKind::Strong);
    else
      append_link(doc, nav, list_title(l[0]),
                  std::string("https://news.ycombinator.com/") + l[1]);
  }
}

std::optional<Document> fetch_list(const HnUrl& u, std::string* err) {
  auto ids_json = get_json(
      "https://hacker-news.firebaseio.com/v0/" + u.list + "stories.json", err);
  if (!ids_json) return std::nullopt;
  std::vector<int64_t> ids = parse_id_list(*ids_json);
  const size_t first = static_cast<size_t>(u.page - 1) * kStoriesPerPage;
  if (first < ids.size())
    ids = std::vector<int64_t>(
        ids.begin() + static_cast<std::ptrdiff_t>(first),
        ids.begin() + static_cast<std::ptrdiff_t>(
                          std::min(ids.size(), first + kStoriesPerPage)));
  else
    ids.clear();

  std::unordered_map<int64_t, HnItem> by_id;
  if (!ids.empty()) {
    std::string tags = "(story,job,poll),(";
    for (size_t i = 0; i < ids.size(); ++i) {
      if (i) tags += ",";
      tags += "story_" + std::to_string(ids[i]);
    }
    tags += ")";
    auto hits_json = get_json(
        "https://hn.algolia.com/api/v1/search?tags=" + tags +
            "&hitsPerPage=" + std::to_string(ids.size()) +
            "&attributesToRetrieve=title,url,author,points,num_comments,"
            "created_at&attributesToHighlight=none",
        err);
    if (!hits_json) return std::nullopt;
    for (const auto& h : json_object_array_after_key(*hits_json, "hits")) {
      HnItem it = parse_item(h);
      by_id[it.id] = std::move(it);
    }
  }

  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  append_nav(doc, root, u.list);
  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = std::string("Hacker News — ") + list_title(u.list);
  doc.get(thread).label = "hn";
  for (size_t i = 0; i < ids.size(); ++i) {
    auto it = by_id.find(ids[i]);
    if (it == by_id.end()) continue;  // not indexed by Algolia yet
    append_story(doc, thread, it->second,
                 "#" + std::to_string(first + i + 1), false);
  }
  if (doc.get(thread).children.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    doc.append_text(doc.create(NodeKind::Paragraph, msg),
                    "No stories returned from Hacker News.");
  }
  NodeId more = doc.create(NodeKind::Paragraph, root);
  static const std::unordered_map<std::string, std::string> kPath = {
      {"top", "news"}, {"new", "newest"}, {"best", "best"},
      {"ask", "ask"},  {"show", "show"}};
  append_link(doc, more, "More →",
              "https://news.ycombinator.com/" + kPath.at(u.list) +
                  "?p=" + std::to_string(u.page + 1));
  std::cerr << "HN: " << by_id.size() << " stories\n";
  return doc;
}

std::optional<Document> fetch_item(const HnUrl& u, std::string* err) {
  auto json = get_json(
      "https://hn.algolia.com/api/v1/items/" + std::to_string(u.item), err);
  if (!json) return std::nullopt;
  HnItem root_item = parse_item(*json);
  if (root_item.id == 0) {
    if (err) *err = "item " + std::to_string(u.item) + " not found";
    return std::nullopt;
  }

  // Algolia returns children by age; HN ranks them. The official item's
  // "kids" are in display order — apply that to the top level (best effort).
  std::string kids_err;
  if (auto fb = get_json("https://hacker-news.firebaseio.com/v0/item/" +
                             std::to_string(u.item) + ".json",
                         &kids_err)) {
    std::string_view kv = *fb;
    if (size_t k = kv.find("\"kids\""); k != std::string_view::npos) {
      size_t e = kv.find(']', k);
      std::vector<int64_t> kids = parse_id_list(kv.substr(k + 6, e - k - 6));
      std::unordered_map<int64_t, size_t> rank;
      for (size_t i = 0; i < kids.size(); ++i) rank[kids[i]] = i;
      std::stable_sort(root_item.children.begin(), root_item.children.end(),
                       [&](const HnItem& a, const HnItem& b) {
                         auto ra = rank.find(a.id), rb = rank.find(b.id);
                         size_t xa = ra == rank.end() ? kids.size() : ra->second;
                         size_t xb = rb == rank.end() ? kids.size() : rb->second;
                         return xa < xb;
                       });
    }
  }

  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  append_nav(doc, root, {});
  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).label = "hn";
  if (root_item.title.empty()) {
    // A comment permalink: show it as the head of its own subtree.
    doc.get(thread).summary = "Hacker News comment";
    NodeId msg = doc.create(NodeKind::Message, thread);
    Node& n = doc.get(msg);
    n.label = std::to_string(root_item.id);
    n.src = root_item.author.empty() ? "[deleted]" : root_item.author;
    n.summary = format_time_iso(root_item.created);
    hn_append_html(doc, msg, root_item.text);
    if (root_item.parent > 0) {
      NodeId up = doc.create(NodeKind::Paragraph, msg);
      append_link(doc, up, "parent", item_url(root_item.parent));
    }
  } else {
    root_item.comments = count_comments(root_item);
    doc.get(thread).summary = root_item.title;
    doc.meta.title = root_item.title;
    append_story(doc, thread, root_item, {}, true);
  }
  append_comments(doc, thread, root_item);
  std::cerr << "HN: item " << root_item.id << ", "
            << count_comments(root_item) << " comment(s)\n";
  return doc;
}

}  // namespace

std::optional<Document> hn_fetch(std::string_view url, std::string* err) {
  auto u = parse_hn_url(url);
  if (!u) {
    if (err) *err = "not a Hacker News list or item URL";
    return std::nullopt;
  }
  return u->list.empty() ? fetch_item(*u, err) : fetch_list(*u, err);
}

#endif  // XANATOO_HN

}  // namespace xanatoo
