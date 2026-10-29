// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// XML sitemaps → path tree → `sitemap:host` page. The XML is regular
// enough (<url><loc>…</loc><lastmod>…</lastmod></url>) for a scanner;
// namespace prefixes on the element names are tolerated.

#include "xanatoo/sitemap.hpp"

#include "xanatoo/util.hpp"

#if defined(XANATOO_WEB) && XANATOO_WEB
#include "xanatoo/https.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <memory>
#include <unordered_set>

namespace xanatoo {

namespace {

// Next element named `name` (any "prefix:" allowed) at or after `from`:
// [start of its content, end of its content). npos when none.
std::pair<size_t, size_t> find_element(std::string_view s,
                                       std::string_view name, size_t from) {
  size_t i = from;
  while ((i = s.find('<', i)) != std::string_view::npos) {
    size_t n = i + 1;
    if (n < s.size() && (s[n] == '/' || s[n] == '?' || s[n] == '!')) {
      ++i;
      continue;
    }
    size_t e = n;
    while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e])) &&
           s[e] != '>' && s[e] != '/')
      ++e;
    std::string_view tag = s.substr(n, e - n);
    if (const size_t colon = tag.find(':'); colon != std::string_view::npos)
      tag.remove_prefix(colon + 1);
    const size_t gt = s.find('>', e);
    if (gt == std::string_view::npos) break;
    if (tag.size() == name.size() &&
        ascii_lower(tag) == ascii_lower(name)) {
      if (s[gt - 1] == '/') return {gt + 1, gt + 1};  // <x/>
      // Closing tag: "</" [prefix:] name ">".
      size_t c = gt + 1;
      while ((c = s.find("</", c)) != std::string_view::npos) {
        size_t ce = s.find('>', c);
        if (ce == std::string_view::npos) break;
        std::string_view ct = s.substr(c + 2, ce - c - 2);
        trim_ascii(ct);
        if (const size_t colon = ct.find(':'); colon != std::string_view::npos)
          ct.remove_prefix(colon + 1);
        if (ascii_lower(ct) == ascii_lower(name)) return {gt + 1, c};
        c = ce;
      }
      return {gt + 1, s.size()};
    }
    i = gt;
  }
  return {std::string_view::npos, std::string_view::npos};
}

// Element text: CDATA unwrapped, entities decoded, trimmed.
std::string element_text(std::string_view body, std::string_view name) {
  auto [b, e] = find_element(body, name, 0);
  if (b == std::string_view::npos) return {};
  std::string_view v = body.substr(b, e - b);
  trim_ascii(v);
  if (starts_with(v, "<![CDATA[") && ends_with(v, "]]>"))
    return std::string(v.substr(9, v.size() - 12));
  std::string out = html_decode_entities(v);
  std::string_view t = out;
  trim_ascii(t);
  return std::string(t);
}

std::vector<SitemapEntry> read_entries(std::string_view xml,
                                       std::string_view element) {
  std::vector<SitemapEntry> out;
  size_t pos = 0;
  for (;;) {
    auto [b, e] = find_element(xml, element, pos);
    if (b == std::string_view::npos) break;
    std::string_view body = xml.substr(b, e - b);
    SitemapEntry en;
    en.loc = element_text(body, "loc");
    en.lastmod = element_text(body, "lastmod");
    if (!en.loc.empty()) out.push_back(std::move(en));
    pos = std::max(e, b + 1);
  }
  return out;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percent_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hex_digit(s[i + 1]) >= 0 &&
        hex_digit(s[i + 2]) >= 0) {
      out += static_cast<char>(hex_digit(s[i + 1]) * 16 + hex_digit(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string strip_www(std::string h) {
  if (starts_with(h, "www.")) h.erase(0, 4);
  return h;
}

// http(s)://host/rest → {host (lower), "/rest" without fragment}.
bool split_url(std::string_view url, std::string& host, std::string& path) {
  size_t p = url.find("://");
  if (p == std::string_view::npos) return false;
  const std::string scheme = ascii_lower(url.substr(0, p));
  if (scheme != "http" && scheme != "https") return false;
  url.remove_prefix(p + 3);
  size_t slash = url.find_first_of("/?#");
  std::string_view h = url.substr(0, slash);
  if (const size_t at = h.rfind('@'); at != std::string_view::npos)
    h.remove_prefix(at + 1);
  host = ascii_lower(h);
  if (const size_t colon = host.find(':'); colon != std::string::npos)
    host.erase(colon);
  std::string_view rest =
      slash == std::string_view::npos ? std::string_view{} : url.substr(slash);
  if (const size_t hash = rest.find('#'); hash != std::string_view::npos)
    rest = rest.substr(0, hash);
  path = rest.empty() || rest[0] != '/' ? "/" + std::string(rest)
                                        : std::string(rest);
  return !host.empty();
}

std::string page_label(std::string seg, bool leaf) {
  seg = percent_decode(seg);
  if (leaf) {
    for (std::string_view ext : {".html", ".htm", ".php", ".aspx", ".shtml"}) {
      if (seg.size() > ext.size() && ends_with(ascii_lower(seg), ext)) {
        seg.erase(seg.size() - ext.size());
        break;
      }
    }
  }
  return seg;
}

// Build-time node: children by segment.
struct TmpNode {
  std::string label;
  std::string url;
  std::string lastmod;
  std::map<std::string, std::unique_ptr<TmpNode>> kids;
};

void collapse(TmpNode& n, bool is_root) {
  for (auto& [k, c] : n.kids) collapse(*c, false);
  // "a" (no page) → only "b": one row "a/b".
  while (!is_root && n.url.empty() && n.kids.size() == 1) {
    std::unique_ptr<TmpNode> c = std::move(n.kids.begin()->second);
    n.kids.clear();
    n.label += "/" + c->label;
    n.url = std::move(c->url);
    n.lastmod = std::move(c->lastmod);
    n.kids = std::move(c->kids);
  }
}

int flatten(SiteTree& t, TmpNode& n, int parent, int depth) {
  const int id = static_cast<int>(t.nodes.size());
  t.nodes.emplace_back();
  {
    SiteNode& s = t.nodes.back();
    s.label = n.label;
    s.url = n.url;
    s.lastmod = n.lastmod;
    s.parent = parent;
    s.depth = depth;
  }
  std::vector<TmpNode*> order;
  for (auto& [k, c] : n.kids) order.push_back(c.get());
  std::stable_sort(order.begin(), order.end(),
                   [](const TmpNode* a, const TmpNode* b) {
                     const bool da = !a->kids.empty(), db = !b->kids.empty();
                     if (da != db) return db;  // pages before directories
                     return ascii_lower(a->label) < ascii_lower(b->label);
                   });
  int pages = n.url.empty() ? 0 : 1;
  std::vector<int> kids;
  for (TmpNode* c : order) {
    const int k = flatten(t, *c, id, depth + 1);
    kids.push_back(k);
    pages += t.nodes[static_cast<size_t>(k)].pages;
  }
  t.nodes[static_cast<size_t>(id)].kids = std::move(kids);
  t.nodes[static_cast<size_t>(id)].pages = pages;
  return id;
}

std::string short_date(const std::string& lastmod) {
  return lastmod.substr(0, std::min<size_t>(10, lastmod.size()));
}

void link_span(Document& doc, NodeId n, const std::string& text,
               const std::string& href) {
  InlineSpan s;
  s.kind = href.empty() ? InlineKind::Text : InlineKind::Link;
  s.text = text;
  s.href = href;
  doc.get(n).spans.push_back(std::move(s));
}

void emit_dir(const SiteTree& t, Document& doc, NodeId parent, int id,
              int level) {
  const SiteNode& n = t.nodes[static_cast<size_t>(id)];
  NodeId sec = doc.create(NodeKind::Section, parent);
  doc.get(sec).level = level;
  NodeId h = doc.create(NodeKind::Header, sec);
  doc.get(h).level = level;
  link_span(doc, h, id == 0 ? t.host : n.label, n.url);
  doc.append_text(h, "  (" + std::to_string(n.pages) + ")",
                  InlineKind::Emphasis);
  NodeId list = INVALID_NODE;
  for (int k : n.kids) {
    const SiteNode& c = t.nodes[static_cast<size_t>(k)];
    if (!c.kids.empty()) continue;
    if (list == INVALID_NODE) list = doc.create(NodeKind::List, sec);
    NodeId li = doc.create(NodeKind::ListItem, list);
    link_span(doc, li, c.label, c.url);
    if (!c.lastmod.empty())
      doc.append_text(li, "  " + short_date(c.lastmod), InlineKind::Emphasis);
  }
  for (int k : n.kids)
    if (!t.nodes[static_cast<size_t>(k)].kids.empty())
      emit_dir(t, doc, sec, k, std::min(level + 1, 6));
}

}  // namespace

SitemapFile parse_sitemap_xml(std::string_view xml) {
  SitemapFile f;
  f.sitemaps = read_entries(xml, "sitemap");
  f.urls = read_entries(xml, "url");
  return f;
}

std::vector<std::string> robots_sitemaps(std::string_view robots_txt) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= robots_txt.size()) {
    size_t nl = robots_txt.find('\n', pos);
    std::string_view line = robots_txt.substr(
        pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    if (const size_t hash = line.find('#'); hash != std::string_view::npos)
      line = line.substr(0, hash);
    trim_ascii(line);
    if (starts_with_ci(line, "sitemap:")) {
      std::string_view v = line.substr(8);
      trim_ascii(v);
      if (!v.empty()) out.emplace_back(v);
    }
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  return out;
}

bool is_sitemap_ref(std::string_view s) {
  return starts_with(s, kSitemapPrefix) && s.size() > kSitemapPrefix.size();
}

std::string sitemap_ref_host(std::string_view ref) {
  if (!is_sitemap_ref(ref)) return {};
  std::string_view rest = ref.substr(kSitemapPrefix.size());
  std::string host, path;
  if (split_url(rest, host, path)) return host;
  rest = rest.substr(0, rest.find_first_of("/?#"));
  return ascii_lower(rest);
}

std::string url_host(std::string_view url) {
  std::string host, path;
  return split_url(url, host, path) ? host : std::string{};
}

std::string site_url_key(std::string_view url) {
  std::string host, path;
  if (!split_url(url, host, path)) return std::string(url);
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  if (path == "/") path.clear();
  return strip_www(host) + percent_decode(path);
}

SiteTree build_site_tree(std::string_view host,
                         const std::vector<SitemapEntry>& urls) {
  SiteTree t;
  t.host = ascii_lower(host);
  const std::string self = strip_www(t.host);
  TmpNode root;
  root.label = "/";
  std::unordered_set<std::string> seen;
  for (const SitemapEntry& e : urls) {
    std::string h, path;
    if (!split_url(e.loc, h, path)) continue;
    if (!seen.insert(site_url_key(e.loc)).second) continue;
    std::vector<std::string> segs;
    // Other hosts (a CDN, a subdomain) get their own top-level branch.
    if (strip_www(h) != self) segs.push_back(h);
    std::string_view rest = path;
    std::string query;
    if (const size_t q = rest.find('?'); q != std::string_view::npos) {
      query = std::string(rest.substr(q));
      rest = rest.substr(0, q);
    }
    size_t i = 0;
    while (i < rest.size()) {
      size_t j = rest.find('/', i);
      if (j == std::string_view::npos) j = rest.size();
      if (j > i) segs.emplace_back(rest.substr(i, j - i));
      i = j + 1;
    }
    if (!query.empty()) {
      if (segs.empty()) segs.push_back(query);
      else segs.back() += query;
    }
    TmpNode* n = &root;
    for (size_t k = 0; k < segs.size(); ++k) {
      auto& slot = n->kids[segs[k]];
      if (!slot) {
        slot = std::make_unique<TmpNode>();
        slot->label = page_label(segs[k], k + 1 == segs.size());
      }
      n = slot.get();
    }
    n->url = e.loc;
    n->lastmod = e.lastmod;
  }
  collapse(root, true);
  flatten(t, root, -1, 0);
  return t;
}

int site_tree_find(const SiteTree& t, std::string_view url) {
  const std::string key = site_url_key(url);
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (!t.nodes[i].url.empty() && site_url_key(t.nodes[i].url) == key)
      return static_cast<int>(i);
  return -1;
}

Document site_tree_document(const SiteTree& t) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  doc.meta.title = "Site map — " + t.host;
  const SiteNode& top = t.nodes.front();

  NodeId intro = doc.create(NodeKind::Paragraph, root);
  doc.append_text(intro, std::to_string(top.pages) + " pages on ");
  link_span(doc, intro, t.host, "https://" + t.host + "/");
  if (!t.sources.empty()) {
    doc.append_text(intro, " · from ");
    for (size_t i = 0; i < t.sources.size() && i < 5; ++i) {
      if (i) doc.append_text(intro, ", ");
      std::string name = t.sources[i];
      if (const size_t s = name.rfind('/'); s != std::string::npos)
        name.erase(0, s + 1);
      link_span(doc, intro, name, t.sources[i]);
    }
    if (t.sources.size() > 5)
      doc.append_text(intro,
                      " and " + std::to_string(t.sources.size() - 5) + " more");
  }
  for (const std::string& note : t.notes)
    doc.append_text(doc.create(NodeKind::Paragraph, root), note,
                    InlineKind::Emphasis);

  // Newest first: a "what changed" view for sites without a feed.
  std::vector<int> dated;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (!t.nodes[i].url.empty() && !t.nodes[i].lastmod.empty())
      dated.push_back(static_cast<int>(i));
  if (!dated.empty()) {
    std::stable_sort(dated.begin(), dated.end(), [&](int a, int b) {
      return t.nodes[static_cast<size_t>(a)].lastmod >
             t.nodes[static_cast<size_t>(b)].lastmod;
    });
    NodeId sec = doc.create(NodeKind::Section, root);
    doc.get(sec).level = 1;
    NodeId h = doc.create(NodeKind::Header, sec);
    doc.get(h).level = 1;
    doc.append_text(h, "Recently changed");
    NodeId list = doc.create(NodeKind::List, sec);
    for (size_t i = 0; i < dated.size() && i < 30; ++i) {
      const SiteNode& n = t.nodes[static_cast<size_t>(dated[i])];
      NodeId li = doc.create(NodeKind::ListItem, list);
      doc.append_text(li, short_date(n.lastmod) + "  ");
      // Path for context: "docs/api/intro".
      std::string path = n.label;
      for (int p = n.parent; p > 0; p = t.nodes[static_cast<size_t>(p)].parent)
        path = t.nodes[static_cast<size_t>(p)].label + "/" + path;
      link_span(doc, li, path, n.url);
    }
  }

  emit_dir(t, doc, root, 0, 1);
  return doc;
}

#if defined(XANATOO_WEB) && XANATOO_WEB

namespace {

constexpr const char* kUserAgent =
    "xanatoo/0.1 (sitemap reader; https://github.com/Grumbel/xanatoo)";
constexpr size_t kMaxFiles = 25;
constexpr size_t kMaxUrls = 50000;

}  // namespace

std::optional<SiteTree> sitemap_fetch_tree(std::string_view host_in,
                                           std::string* err) {
  const std::string host = ascii_lower(host_in);
  if (host.empty()) {
    if (err) *err = "sitemap: needs a host";
    return std::nullopt;
  }
  const std::string base = "https://" + host;
  std::deque<std::string> queue;
  {
    std::string e;
    if (auto r = https_get_follow(base + "/robots.txt", kUserAgent, &e);
        r && r->status == 200)
      for (std::string& s : robots_sitemaps(r->body)) queue.push_back(s);
  }
  const bool from_robots = !queue.empty();
  if (!from_robots)
    for (const char* p : {"/sitemap.xml", "/sitemap_index.xml",
                          "/wp-sitemap.xml"})
      queue.push_back(base + p);

  std::vector<SitemapEntry> urls;
  std::vector<std::string> sources, notes;
  std::unordered_set<std::string> visited;
  size_t files = 0;
  std::string last_err;
  while (!queue.empty()) {
    std::string url = std::move(queue.front());
    queue.pop_front();
    if (starts_with_ci(url, "http://")) url = "https://" + url.substr(7);
    if (!visited.insert(url).second) continue;
    if (files >= kMaxFiles) {
      notes.push_back("Stopped after " + std::to_string(kMaxFiles) +
                      " sitemap files; " + std::to_string(queue.size() + 1) +
                      " not read.");
      break;
    }
    ++files;
    std::string e;
    auto r = https_get_follow(url, kUserAgent, &e);
    if (!r || r->status != 200) {
      last_err = r ? url + ": HTTP " + std::to_string(r->status) : e;
      continue;
    }
    if (r->body.size() >= 2 && static_cast<unsigned char>(r->body[0]) == 0x1f &&
        static_cast<unsigned char>(r->body[1]) == 0x8b) {
      notes.push_back("Skipped gzip-compressed " + url + " (not supported).");
      continue;
    }
    SitemapFile f = parse_sitemap_xml(r->body);
    if (f.urls.empty() && f.sitemaps.empty()) continue;
    // Without robots.txt the first well-known path that answers is the
    // site's sitemap; the other guesses would mostly repeat it.
    if (!from_robots && sources.empty()) queue.clear();
    sources.push_back(url);
    for (SitemapEntry& s : f.sitemaps) queue.push_back(std::move(s.loc));
    for (SitemapEntry& u : f.urls) {
      if (urls.size() >= kMaxUrls) break;
      urls.push_back(std::move(u));
    }
    if (urls.size() >= kMaxUrls) {
      notes.push_back("Showing the first " + std::to_string(kMaxUrls) +
                      " pages.");
      break;
    }
  }
  if (sources.empty()) {
    if (err)
      *err = "no sitemap found for " + host +
             (last_err.empty() ? "" : " (" + last_err + ")");
    return std::nullopt;
  }
  SiteTree t = build_site_tree(host, urls);
  t.sources = std::move(sources);
  t.notes = std::move(notes);
  return t;
}

std::optional<Document> sitemap_fetch(std::string_view ref, std::string* err) {
  auto t = sitemap_fetch_tree(sitemap_ref_host(ref), err);
  if (!t) return std::nullopt;
  return site_tree_document(*t);
}

#endif

}  // namespace xanatoo
