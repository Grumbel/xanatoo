// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// RSS 2.0 / RSS 1.0 / Atom reader. A small tolerant XML reader builds an
// element tree (names lower-cased, prefixes kept: "content:encoded");
// entries become Thread + Message nodes and their HTML goes through
// html_min.

#include "xanatoo/feed.hpp"
#include "xanatoo/util.hpp"

#include "xanatoo/html_min.hpp"
#include "xanatoo/https.hpp"
#include "xanatoo/version.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

namespace {

// --- XML ------------------------------------------------------------------

struct XmlNode {
  std::string name;   // lower case, prefix kept
  std::string attrs;  // raw attribute text, read with html_attr
  std::vector<size_t> kids;
  std::string text;   // direct text + CDATA, entities decoded
  size_t inner_begin = 0;
  size_t inner_end = 0;  // raw markup between start and end tag
};

using XmlTree = std::vector<XmlNode>;  // [0] = document node

size_t xml_tag_end(std::string_view s, size_t lt) {
  char quote = 0;
  for (size_t i = lt + 1; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      return i;
    }
  }
  return std::string_view::npos;
}

std::string xml_name(std::string_view s) {
  size_t e = 0;
  while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e])) &&
         s[e] != '/' && s[e] != '>')
    ++e;
  return ascii_lower(s.substr(0, e));
}

// Skip "<!…>" (a DOCTYPE may carry a [ … ] internal subset).
size_t skip_decl(std::string_view s, size_t i) {
  int depth = 0;
  for (size_t k = i + 2; k < s.size(); ++k) {
    if (s[k] == '[') ++depth;
    else if (s[k] == ']') --depth;
    else if (s[k] == '>' && depth <= 0) return k + 1;
  }
  return s.size();
}

XmlTree parse_xml(std::string_view s) {
  XmlTree t(1);
  std::vector<size_t> stack{0};
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '<') {
      size_t lt = s.find('<', i);
      if (lt == std::string_view::npos) lt = s.size();
      t[stack.back()].text += html_decode_entities(s.substr(i, lt - i));
      i = lt;
      continue;
    }
    std::string_view rest = s.substr(i);
    if (starts_with(rest, "<!--")) {
      size_t e = s.find("-->", i + 4);
      i = e == std::string_view::npos ? s.size() : e + 3;
    } else if (starts_with(rest, "<![CDATA[")) {
      size_t e = s.find("]]>", i + 9);
      size_t end = e == std::string_view::npos ? s.size() : e;
      t[stack.back()].text.append(s.substr(i + 9, end - i - 9));
      i = e == std::string_view::npos ? s.size() : e + 3;
    } else if (starts_with(rest, "<!")) {
      i = skip_decl(s, i);
    } else if (starts_with(rest, "<?")) {
      size_t e = s.find("?>", i + 2);
      i = e == std::string_view::npos ? s.size() : e + 2;
    } else if (starts_with(rest, "</")) {
      size_t e = xml_tag_end(s, i);
      if (e == std::string_view::npos) break;
      std::string name = xml_name(s.substr(i + 2));
      for (size_t k = stack.size(); k-- > 1;) {
        if (t[stack[k]].name != name) continue;
        t[stack[k]].inner_end = i;
        while (stack.size() > k) {
          if (t[stack.back()].inner_end == 0) t[stack.back()].inner_end = i;
          stack.pop_back();
        }
        break;
      }
      i = e + 1;
    } else {
      size_t e = xml_tag_end(s, i);
      if (e == std::string_view::npos) break;
      std::string_view inside = s.substr(i + 1, e - i - 1);
      const bool self_close = !inside.empty() && inside.back() == '/';
      if (self_close) inside.remove_suffix(1);
      XmlNode n;
      n.name = xml_name(inside);
      n.attrs = std::string(inside.substr(std::min(inside.size(), n.name.size())));
      n.inner_begin = n.inner_end = e + 1;
      const size_t id = t.size();
      t[stack.back()].kids.push_back(id);
      t.push_back(std::move(n));
      if (!self_close) {
        t[id].inner_end = 0;
        stack.push_back(id);
      }
      i = e + 1;
    }
  }
  for (size_t k = 1; k < stack.size(); ++k)
    if (t[stack[k]].inner_end == 0) t[stack[k]].inner_end = s.size();
  return t;
}

const XmlNode* child(const XmlTree& t, const XmlNode& n, std::string_view name) {
  for (size_t k : n.kids)
    if (t[k].name == name) return &t[k];
  return nullptr;
}

std::vector<const XmlNode*> children(const XmlTree& t, const XmlNode& n,
                                     std::string_view name) {
  std::vector<const XmlNode*> out;
  for (size_t k : n.kids)
    if (t[k].name == name) out.push_back(&t[k]);
  return out;
}

// Whitespace runs (newlines and indentation from pretty-printed XML)
// collapsed to one space, ends trimmed.
std::string trimmed(std::string_view s) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

std::string child_text(const XmlTree& t, const XmlNode& n, std::string_view name) {
  const XmlNode* c = child(t, n, name);
  return c ? trimmed(c->text) : std::string{};
}

std::string_view inner_markup(std::string_view src, const XmlNode& n) {
  if (n.inner_end <= n.inner_begin || n.inner_end > src.size()) return {};
  return src.substr(n.inner_begin, n.inner_end - n.inner_begin);
}

// Root element of a document, skipping prolog / comments.
std::string root_name(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    size_t lt = s.find('<', i);
    if (lt == std::string_view::npos) return {};
    std::string_view rest = s.substr(lt);
    if (starts_with(rest, "<?")) {
      size_t e = s.find("?>", lt);
      if (e == std::string_view::npos) return {};
      i = e + 2;
    } else if (starts_with(rest, "<!--")) {
      size_t e = s.find("-->", lt);
      if (e == std::string_view::npos) return {};
      i = e + 3;
    } else if (starts_with(rest, "<!")) {
      i = skip_decl(s, lt);
    } else {
      return xml_name(rest.substr(1));
    }
  }
  return {};
}

// --- feed mapping ----------------------------------------------------------

// "2026-10-27T10:00:00Z" / "Mon, 27 Oct 2026 10:00:00 +0000" → "2026-10-27 10:00 UTC".
std::string format_date(std::string_view raw) {
  const std::string clean = trimmed(raw);
  std::string_view d = clean;
  if (d.size() >= 16 && d[4] == '-' && d[7] == '-' && (d[10] == 'T' || d[10] == ' ')) {
    std::string zone(d.substr(16));
    if (size_t z = zone.find_first_of("Z+-"); z != std::string::npos)
      zone = zone.substr(z);
    else
      zone.clear();
    std::string out = std::string(d.substr(0, 10)) + " " + std::string(d.substr(11, 5));
    if (zone == "Z" || zone == "+00:00") out += " UTC";
    else if (!zone.empty()) out += " " + zone;
    return out;
  }
  std::string_view r = d;
  if (size_t comma = r.find(','); comma != std::string_view::npos && comma < 5)
    r.remove_prefix(comma + 1);
  std::istringstream in{std::string(r)};
  std::string day, mon, year, time, zone;
  in >> day >> mon >> year >> time >> zone;
  static const char* kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                  "jul", "aug", "sep", "oct", "nov", "dec"};
  int m = 0;
  for (int k = 0; k < 12; ++k)
    if (ascii_lower(mon).rfind(kMonths[k], 0) == 0) m = k + 1;
  if (m == 0 || year.size() != 4 || day.empty() || day.size() > 2 ||
      time.size() < 5)
    return std::string(d);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s-%02d-%02d %s", year.c_str(), m,
                std::atoi(day.c_str()), time.substr(0, 5).c_str());
  std::string out = buf;
  if (zone == "+0000" || zone == "GMT" || zone == "UT" || zone == "UTC" || zone == "Z")
    out += " UTC";
  else if (!zone.empty())
    out += " " + zone;
  return out;
}

// Atom text construct (title / subtitle): type text, html or xhtml.
std::string text_construct(std::string_view src, const XmlNode* n) {
  if (!n) return {};
  std::string type = ascii_lower(html_attr(n->attrs, "type"));
  if (type == "xhtml") return html_to_plain(inner_markup(src, *n));
  // RSS titles are often HTML-escaped as well; decoding them is harmless.
  return html_to_plain(n->text);
}

void append_plain_paragraphs(Document& doc, NodeId parent, std::string_view text) {
  std::string para;
  auto flush = [&] {
    std::string_view p = para;
    trim_ascii(p);
    if (!p.empty()) doc.append_text(doc.create(NodeKind::Paragraph, parent), std::string(p));
    para.clear();
  };
  size_t i = 0;
  while (i <= text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string_view::npos) e = text.size();
    std::string_view line = text.substr(i, e - i);
    trim_ascii(line);
    if (line.empty()) {
      flush();
    } else {
      if (!para.empty()) para.push_back('\n');
      para.append(line);
    }
    i = e + 1;
  }
  flush();
}

// Entry body: html / xhtml / text content, or a link for out-of-line content.
void append_content(Document& doc, NodeId msg, std::string_view src,
                    const XmlNode& n, const std::string& base, bool default_html) {
  std::string type = ascii_lower(html_attr(n.attrs, "type"));
  std::string out_of_line = html_attr(n.attrs, "src");
  HtmlOptions opt;
  opt.base_url = base;
  if (!out_of_line.empty()) {
    NodeId p = doc.create(NodeKind::Paragraph, msg);
    InlineSpan s;
    s.kind = InlineKind::Link;
    s.text = "Full content";
    s.href = resolve_url(base, out_of_line);
    doc.get(p).spans.push_back(std::move(s));
  } else if (type == "xhtml") {
    html_append_blocks(doc, msg, inner_markup(src, n), opt);
  } else if (type == "html" || type == "text/html" ||
             (type.empty() && default_html)) {
    html_append_blocks(doc, msg, n.text, opt);
  } else if (type.empty() || type == "text" || type == "text/plain") {
    append_plain_paragraphs(doc, msg, n.text);
  }
}

void append_link_para(Document& doc, NodeId parent, std::string text,
                      std::string href, InlineKind fallback = InlineKind::Text) {
  NodeId p = doc.create(NodeKind::Paragraph, parent);
  InlineSpan s;
  s.kind = href.empty() ? fallback : InlineKind::Link;
  s.text = std::move(text);
  s.href = std::move(href);
  doc.get(p).spans.push_back(std::move(s));
}

std::string human_size(const std::string& bytes) {
  double n = std::atof(bytes.c_str());
  if (n <= 0) return {};
  char buf[32];
  if (n >= 1e6) std::snprintf(buf, sizeof(buf), "%.1f MB", n / 1e6);
  else std::snprintf(buf, sizeof(buf), "%.0f kB", n / 1e3);
  return buf;
}

void append_enclosure(Document& doc, NodeId msg, const std::string& url,
                      const std::string& type, const std::string& length) {
  if (url.empty()) return;
  // Lemmy and others mark the linked article itself as an enclosure.
  if (starts_with(type, "text/html")) {
    append_link_para(doc, msg, "Link", url);
    return;
  }
  std::string label = "Enclosure";
  if (!type.empty()) label += ": " + type;
  if (std::string sz = human_size(length); !sz.empty()) label += " (" + sz + ")";
  append_link_para(doc, msg, label, url);
}

// "jane@example.org (Jane Doe)" → "Jane Doe".
std::string rss_author(std::string a) {
  size_t open = a.find('(');
  size_t close = a.rfind(')');
  if (open != std::string::npos && close != std::string::npos && close > open)
    return a.substr(open + 1, close - open - 1);
  return a;
}

struct Entry {
  std::string title, link, author, date;
  const XmlNode* body = nullptr;
  bool body_html = false;  // untyped body is HTML (RSS) rather than text
  std::vector<const XmlNode*> enclosures;  // RSS <enclosure> / Atom rel=enclosure
};

struct FeedInfo {
  std::string title, subtitle, link, author;
  std::vector<Entry> entries;
};

void read_atom(const XmlTree& t, std::string_view src, const XmlNode& feed,
               const std::string& url, FeedInfo& out) {
  out.title = text_construct(src, child(t, feed, "title"));
  out.subtitle = text_construct(src, child(t, feed, "subtitle"));
  if (const XmlNode* a = child(t, feed, "author")) out.author = child_text(t, *a, "name");
  for (const XmlNode* l : children(t, feed, "link")) {
    std::string rel = html_attr(l->attrs, "rel");
    if (rel.empty() || rel == "alternate") {
      out.link = resolve_url(url, html_attr(l->attrs, "href"));
      break;
    }
  }
  for (const XmlNode* e : children(t, feed, "entry")) {
    Entry en;
    en.title = text_construct(src, child(t, *e, "title"));
    for (const XmlNode* l : children(t, *e, "link")) {
      std::string rel = html_attr(l->attrs, "rel");
      if (rel == "enclosure") en.enclosures.push_back(l);
      else if (en.link.empty() && (rel.empty() || rel == "alternate"))
        en.link = resolve_url(url, html_attr(l->attrs, "href"));
    }
    if (const XmlNode* a = child(t, *e, "author")) en.author = child_text(t, *a, "name");
    en.date = child_text(t, *e, "published");
    if (en.date.empty()) en.date = child_text(t, *e, "updated");
    en.body = child(t, *e, "content");
    if (!en.body) en.body = child(t, *e, "summary");
    if (!en.body)  // YouTube: <media:group><media:description>
      if (const XmlNode* g = child(t, *e, "media:group"))
        en.body = child(t, *g, "media:description");
    out.entries.push_back(std::move(en));
  }
}

void read_rss(const XmlTree& t, const XmlNode& channel,
              const std::vector<const XmlNode*>& items, const std::string& url,
              FeedInfo& out) {
  out.title = html_to_plain(child_text(t, channel, "title"));
  out.subtitle = html_to_plain(child_text(t, channel, "description"));
  out.link = resolve_url(url, child_text(t, channel, "link"));
  for (const XmlNode* it : items) {
    Entry en;
    en.title = html_to_plain(child_text(t, *it, "title"));
    en.link = child_text(t, *it, "link");
    if (en.link.empty()) {
      const XmlNode* g = child(t, *it, "guid");
      if (g && html_attr(g->attrs, "ispermalink") != "false" &&
          starts_with_ci(trimmed(g->text), "http"))
        en.link = trimmed(g->text);
    }
    if (en.link.empty()) en.link = html_attr(it->attrs, "rdf:about");
    en.link = resolve_url(url, en.link);
    en.author = child_text(t, *it, "dc:creator");
    if (en.author.empty()) en.author = rss_author(child_text(t, *it, "author"));
    en.date = child_text(t, *it, "pubdate");
    if (en.date.empty()) en.date = child_text(t, *it, "dc:date");
    en.body = child(t, *it, "content:encoded");
    if (!en.body || trimmed(en.body->text).empty()) en.body = child(t, *it, "description");
    en.body_html = true;
    en.enclosures = children(t, *it, "enclosure");
    out.entries.push_back(std::move(en));
  }
}

Document build_document(const FeedInfo& f, std::string_view src,
                        const std::string& url) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  const std::string title = f.title.empty() ? url : f.title;
  doc.meta.title = title;

  NodeId head = doc.create(NodeKind::Paragraph, root);
  InlineSpan ts;
  ts.kind = f.link.empty() ? InlineKind::Strong : InlineKind::Link;
  ts.text = title;
  ts.href = f.link;
  doc.get(head).spans.push_back(std::move(ts));
  if (!f.subtitle.empty() && f.subtitle != title)
    doc.append_text(head, "\n" + f.subtitle, InlineKind::Emphasis);

  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title;
  doc.get(thread).label = "feed";
  int n = 0;
  for (const Entry& e : f.entries) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    {
      Node& m = doc.get(msg);
      m.label = "e" + std::to_string(++n);
      m.src = !e.author.empty() ? e.author : !f.author.empty() ? f.author : title;
      m.summary = format_date(e.date);
    }
    if (!e.title.empty())
      append_link_para(doc, msg, e.title, e.link, InlineKind::Strong);
    if (e.body) {
      const std::string base = e.link.empty() ? url : e.link;
      append_content(doc, msg, src, *e.body, base, e.body_html);
    }
    for (const XmlNode* enc : e.enclosures) {
      std::string href = html_attr(enc->attrs, "url");
      if (href.empty()) href = html_attr(enc->attrs, "href");
      append_enclosure(doc, msg, resolve_url(url, href),
                       html_attr(enc->attrs, "type"),
                       html_attr(enc->attrs, "length"));
    }
    // Untitled entries (Mastodon, micro-blogs): keep a way to the original.
    if (e.title.empty() && !e.link.empty()) append_link_para(doc, msg, "Permalink", e.link);
  }
  if (f.entries.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    doc.append_text(doc.create(NodeKind::Paragraph, msg), "This feed has no entries.");
  }
  NodeId foot = doc.create(NodeKind::Paragraph, root);
  doc.append_text(foot, std::to_string(f.entries.size()) + " entries · ");
  InlineSpan fs;
  fs.kind = InlineKind::Link;
  fs.text = url;
  fs.href = url;
  doc.get(foot).spans.push_back(std::move(fs));
  return doc;
}

struct SplitPath {
  std::string path;   // lower case, no trailing '/'
  std::string query;  // lower case
};

std::optional<SplitPath> http_path(std::string_view s) {
  trim_ascii(s);
  if (starts_with_ci(s, "https://")) s.remove_prefix(8);
  else if (starts_with_ci(s, "http://")) s.remove_prefix(7);
  else return std::nullopt;
  s = s.substr(0, s.find('#'));
  size_t slash = s.find_first_of("/?");
  if (slash == std::string_view::npos) return SplitPath{};
  std::string rest = ascii_lower(s.substr(slash));
  SplitPath out;
  size_t q = rest.find('?');
  out.path = rest.substr(0, q);
  if (q != std::string::npos) out.query = rest.substr(q + 1);
  while (!out.path.empty() && out.path.back() == '/') out.path.pop_back();
  return out;
}

}  // namespace

bool is_feed_url(std::string_view s) {
  trim_ascii(s);
  if (starts_with_ci(s, "feed:")) return true;
  auto u = http_path(s);
  if (!u) return false;
  const std::string& p = u->path;
  if (ends_with(p, ".rss") || ends_with(p, ".atom") || ends_with(p, ".rdf"))
    return true;
  if (ends_with(p, ".xml")) return p.find("sitemap") == std::string::npos;
  std::string last = p.substr(p.rfind('/') + 1);
  if (last == "feed" || last == "rss" || last == "atom" || last == "rss2")
    return true;
  if (p.find("/feeds/") != std::string::npos || p.find("/feed/") != std::string::npos)
    return true;
  const std::string& q = u->query;
  return q.find("feed=rss") != std::string::npos ||
         q.find("feed=atom") != std::string::npos ||
         q.find("format=rss") != std::string::npos ||
         q.find("format=atom") != std::string::npos ||
         q.find("feedformat=") != std::string::npos;
}

std::string feed_http_url(std::string_view s) {
  trim_ascii(s);
  if (starts_with_ci(s, "feed:")) {
    s.remove_prefix(5);
    if (starts_with(s, "//")) s.remove_prefix(2);  // feed://host/…
  }
  if (starts_with_ci(s, "https://")) return std::string(s);
  if (starts_with_ci(s, "http://")) return "https://" + std::string(s.substr(7));
  return "https://" + std::string(s);
}

bool looks_like_feed(std::string_view body) {
  std::string r = root_name(body.substr(0, 4096));
  return r == "rss" || r == "feed" || r == "rdf:rdf" || r == "atom:feed";
}

std::string feed_autodiscover(std::string_view html, std::string_view page_url) {
  std::string low = ascii_lower(html);
  for (size_t p = low.find("<link"); p != std::string::npos;
       p = low.find("<link", p + 5)) {
    size_t e = xml_tag_end(html, p);
    if (e == std::string_view::npos) break;
    std::string_view attrs = html.substr(p + 5, e - p - 5);
    std::string rel = ascii_lower(html_attr(attrs, "rel"));
    std::string type = ascii_lower(html_attr(attrs, "type"));
    if (rel.find("alternate") == std::string::npos) continue;
    if (type.find("rss+xml") == std::string::npos &&
        type.find("atom+xml") == std::string::npos)
      continue;
    std::string href = html_attr(attrs, "href");
    if (!href.empty()) return resolve_url(page_url, href);
  }
  return {};
}

std::optional<Document> parse_feed(std::string_view xml, std::string_view feed_url,
                                   std::string* err) {
  if (starts_with(xml, "\xEF\xBB\xBF")) xml.remove_prefix(3);  // UTF-8 BOM
  XmlTree t = parse_xml(xml);
  const XmlNode* root = nullptr;
  for (size_t k : t[0].kids) {
    root = &t[k];
    break;
  }
  if (!root) {
    if (err) *err = "empty feed";
    return std::nullopt;
  }
  // "atom:feed" with a prefixed namespace: drop the prefix everywhere.
  if (root->name == "atom:feed")
    for (XmlNode& n : t)
      if (starts_with(n.name, "atom:")) n.name.erase(0, 5);
  root = &t[t[0].kids.front()];

  const std::string url(feed_url);
  FeedInfo info;
  if (root->name == "feed") {
    read_atom(t, xml, *root, url, info);
  } else if (root->name == "rss" || root->name == "rdf:rdf") {
    const XmlNode* channel = child(t, *root, "channel");
    std::vector<const XmlNode*> items = children(t, *root, "item");  // RSS 1.0
    if (channel)
      for (const XmlNode* it : children(t, *channel, "item")) items.push_back(it);
    read_rss(t, channel ? *channel : *root, items, url, info);
  } else {
    if (err) *err = "not an RSS / Atom feed (root <" + root->name + ">)";
    return std::nullopt;
  }
  return build_document(info, xml, url);
}

#if defined(XANATOO_FEEDS) && XANATOO_FEEDS

std::optional<Document> feed_fetch(std::string_view url_in, std::string* err) {
  static const std::string kUserAgent = user_agent("feed reader");
  std::string url = feed_http_url(url_in);
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::cerr << "Feed: GET " << url << "\n";
    std::string final_url;
    auto resp = https_get_follow(url, kUserAgent, err, &final_url);
    if (!resp) return std::nullopt;
    if (resp->status != 200) {
      if (err) *err = "HTTP " + std::to_string(resp->status);
      return std::nullopt;
    }
    const std::string charset = detect_charset(resp->content_type, resp->body);
    resp->body = to_utf8(std::move(resp->body), charset);
    if (looks_like_feed(resp->body)) return parse_feed(resp->body, final_url, err);
    // An HTML page: follow its <link rel="alternate"> feed once.
    std::string alt = attempt == 0 ? feed_autodiscover(resp->body, final_url)
                                   : std::string{};
    if (alt.empty()) {
      if (err)
        *err = "not an RSS / Atom feed" +
               (resp->content_type.empty() ? std::string{}
                                           : " (" + resp->content_type + ")");
      return std::nullopt;
    }
    url = feed_http_url(alt);
  }
  return std::nullopt;
}

#endif  // XANATOO_FEEDS

}  // namespace xanatoo
