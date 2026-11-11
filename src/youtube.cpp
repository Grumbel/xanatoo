// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only YouTube client over InnerTube (docs/YOUTUBE.md). Channel uploads,
// playlists, and video pages with comments become Thread + Article / Message
// documents. InnerTube is unofficial and changes shape without notice, so the
// parsers look for a few named renderers anywhere in the reply instead of
// following fixed paths, and accept the old and the new layout.

#include "xanatoo/youtube.hpp"

#include "xanatoo/html_page.hpp"  // kExternalPrefix
#include "xanatoo/util.hpp"

#if defined(XANATOO_YOUTUBE) && XANATOO_YOUTUBE
#include "xanatoo/https.hpp"
#include "xanatoo/version.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace xanatoo {

namespace {

constexpr std::string_view kPrefix = "youtube:";
constexpr int kMaxPage = 200;

bool id_chars(std::string_view s, bool allow_path) {
  if (s.empty() || s.size() > 200) return false;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '_' || c == '-' || c == '.' || c == '%' ||
        c == '@' || (allow_path && c == '/'))
      continue;
    return false;
  }
  return true;
}

std::string query_param(std::string_view query, std::string_view name) {
  while (!query.empty()) {
    const size_t amp = query.find('&');
    std::string_view kv = query.substr(0, amp);
    query = amp == std::string_view::npos ? std::string_view()
                                          : query.substr(amp + 1);
    const size_t eq = kv.find('=');
    if (kv.substr(0, eq) == name)
      return eq == std::string_view::npos ? std::string() : std::string(kv.substr(eq + 1));
  }
  return {};
}

int parse_page(std::string_view s) {
  if (s.empty() || s.size() > 4 || s.find_first_not_of("0123456789") != std::string_view::npos)
    return 1;
  return std::clamp(std::atoi(std::string(s).c_str()), 1, kMaxPage);
}

// simpleText / content / runs[].text of a YouTube text object.
std::string yt_text(const Json* v) {
  if (!v) return {};
  if (v->type == Json::Type::String) return v->s;
  if (const Json* s = json_get(*v, "simpleText")) return json_string(s);
  if (const Json* c = json_get(*v, "content"); c && c->type == Json::Type::String)
    return c->s;
  std::string out;
  if (const Json* runs = json_get(*v, "runs"); runs && runs->type == Json::Type::Array)
    for (const Json& r : runs->items) out += json_string(json_get(r, "text"));
  return out;
}

bool contains(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

// ---- parsing --------------------------------------------------------------

void add_video(YtListing& out, YtVideo v) {
  if (v.id.empty()) return;
  for (const YtVideo& o : out.videos)
    if (o.id == v.id) return;
  out.videos.push_back(std::move(v));
}

void sort_meta_part(YtVideo& v, std::string text) {
  if (text.empty()) return;
  if (contains(text, " views") || contains(text, " view") ||
      contains(text, " watching") || contains(text, "No views"))
    v.views = std::move(text);
  else if (contains(text, " ago") || contains(text, "Streamed") ||
           contains(text, "Premiere"))
    v.age = std::move(text);
  else if (v.channel.empty())
    v.channel = std::move(text);
}

void read_lockup(const Json& l, YtListing& out) {
  const std::string type = json_string(json_get(l, "contentType"));
  if (!type.empty() && type != "LOCKUP_CONTENT_TYPE_VIDEO") return;
  YtVideo v;
  v.id = json_string(json_get(l, "contentId"));
  v.title = yt_text(json_path(l, "metadata/lockupMetadataViewModel/title"));
  if (const Json* rows = json_find(l, "metadataRows");
      rows && rows->type == Json::Type::Array)
    for (const Json& row : rows->items)
      if (const Json* parts = json_get(row, "metadataParts"))
        for (const Json& p : parts->items) sort_meta_part(v, yt_text(json_get(p, "text")));
  if (const Json* img = json_get(l, "contentImage"))
    if (const Json* badge = json_find(*img, "thumbnailBadgeViewModel"))
      v.duration = yt_text(json_get(*badge, "text"));
  add_video(out, std::move(v));
}

void read_video_renderer(const Json& r, YtListing& out) {
  YtVideo v;
  v.id = json_string(json_get(r, "videoId"));
  v.title = yt_text(json_get(r, "title"));
  v.duration = yt_text(json_get(r, "lengthText"));
  v.views = yt_text(json_get(r, "viewCountText"));
  v.age = yt_text(json_get(r, "publishedTimeText"));
  v.channel = yt_text(json_get(r, "shortBylineText"));
  if (v.channel.empty()) v.channel = yt_text(json_get(r, "ownerText"));
  if (const Json* info = json_get(r, "videoInfo")) {  // playlistVideoRenderer
    const std::string t = yt_text(info);
    const size_t dot = t.find(" \xE2\x80\xA2 ");
    if (dot == std::string::npos) v.views = t;
    else v.views = t.substr(0, dot), v.age = t.substr(dot + 5);
  }
  add_video(out, std::move(v));
}

void read_header(const Json& reply, YtListing& out) {
  if (const Json* m = json_find(reply, "channelMetadataRenderer")) {
    out.title = json_string(json_get(*m, "title"));
    out.description = json_string(json_get(*m, "description"));
    out.channel_id = json_string(json_get(*m, "externalId"));
    if (const Json* urls = json_get(*m, "ownerUrls"); urls && !urls->items.empty()) {
      const std::string u = json_string(&urls->items[0]);
      if (const size_t at = u.rfind("/@"); at != std::string::npos)
        out.handle = u.substr(at + 1);
    }
  } else if (const Json* m2 = json_find(reply, "playlistMetadataRenderer")) {
    out.title = json_string(json_get(*m2, "title"));
    out.description = json_string(json_get(*m2, "description"));
  }
  if (const Json* h = json_find(reply, "playlistHeaderRenderer")) {
    if (out.title.empty()) out.title = yt_text(json_get(*h, "title"));
    out.channel_id = out.channel_id.empty() ? std::string() : out.channel_id;
    out.subscribers.clear();
    out.video_count = yt_text(json_get(*h, "numVideosText"));
  }
  if (const Json* head = json_get(reply, "header"))
    if (const Json* rows = json_find(*head, "metadataRows");
        rows && rows->type == Json::Type::Array)
      for (const Json& row : rows->items)
        if (const Json* parts = json_get(row, "metadataParts"))
          for (const Json& p : parts->items) {
            const std::string t = yt_text(json_get(p, "text"));
            if (contains(t, "subscriber")) out.subscribers = t;
            else if (contains(t, "video")) out.video_count = t;
            else if (out.handle.empty() && !t.empty() && t[0] == '@') out.handle = t;
          }
}

}  // namespace

YtListing yt_parse_browse(const Json& reply) {
  YtListing out;
  read_header(reply, out);
  json_walk(reply, [&](std::string_view k, const Json& v) {
    if (k == "lockupViewModel") read_lockup(v, out);
    else if (k == "videoRenderer" || k == "gridVideoRenderer" ||
             k == "playlistVideoRenderer")
      read_video_renderer(v, out);
    else if (k == "continuationItemRenderer" && out.next.empty())
      // First one: the grid precedes the header, whose engagement panels
      // (description, attribution) carry unrelated continuations.
      if (const Json* t = json_find(v, "token")) out.next = json_string(t);
    return true;
  });
  return out;
}

YtVideoInfo yt_parse_video(const Json& reply, std::string_view video_id) {
  YtVideoInfo v;
  v.id = std::string(video_id);
  if (const Json* p = json_find(reply, "videoPrimaryInfoRenderer")) {
    v.title = yt_text(json_get(*p, "title"));
    v.views = yt_text(json_path(*p, "viewCount/videoViewCountRenderer/viewCount"));
    v.date = yt_text(json_get(*p, "dateText"));
  }
  if (const Json* o = json_find(reply, "videoOwnerRenderer")) {
    v.owner = yt_text(json_get(*o, "title"));
    if (const Json* id = json_find(*o, "browseId")) v.owner_id = json_string(id);
  }
  if (const Json* d = json_find(reply, "attributedDescription"))
    v.description = yt_text(d);
  if (v.title.empty())
    if (const Json* t = json_find(reply, "videoTitle")) v.title = yt_text(t);
  // Sort menu: [0] top, [1] newest. Without it the section's own token is top.
  if (const Json* menu = json_find(reply, "sortFilterSubMenuRenderer"))
    if (const Json* items = json_get(*menu, "subMenuItems")) {
      auto token = [&](size_t i) {
        const Json* it = json_at(*items, i);
        const Json* t = it ? json_find(*it, "token") : nullptr;
        return json_string(t);
      };
      v.comments_top = token(0);
      v.comments_new = token(1);
    }
  if (v.comments_top.empty()) {
    std::vector<const Json*> sections;
    json_find_all(reply, "itemSectionRenderer", sections);
    for (const Json* s : sections)
      if (json_string(json_get(*s, "sectionIdentifier")) == "comment-item-section")
        if (const Json* t = json_find(*s, "token")) {
          v.comments_top = json_string(t);
          break;
        }
  }
  return v;
}

YtComments yt_parse_comments(const Json& reply) {
  YtComments out;
  if (const Json* h = json_find(reply, "commentsHeaderRenderer")) {
    std::string t = yt_text(json_get(*h, "countText"));
    // "1,234 Comments" → keep the number
    if (const size_t sp = t.find(' '); sp != std::string::npos) t.resize(sp);
    out.total = std::move(t);
  }
  // Comment bodies live in entity mutations keyed by the thread's commentKey.
  std::vector<std::pair<std::string, const Json*>> entities;
  if (const Json* muts = json_path(reply, "frameworkUpdates/entityBatchUpdate/mutations"))
    for (const Json& m : muts->items)
      if (const Json* e = json_path(m, "payload/commentEntityPayload"))
        entities.emplace_back(json_string(json_get(*e, "key")), e);
  json_walk(reply, [&](std::string_view k, const Json& v) {
    if (k != "commentThreadRenderer") return true;
    const std::string key =
        json_string(json_path(v, "commentViewModel/commentViewModel/commentKey"));
    for (const auto& [ekey, e] : entities) {
      if (ekey != key) continue;
      YtComment c;
      c.id = json_string(json_path(*e, "properties/commentId"));
      c.text = yt_text(json_path(*e, "properties/content"));
      c.published = json_string(json_path(*e, "properties/publishedTime"));
      c.author = json_string(json_path(*e, "author/displayName"));
      c.channel_id = json_string(json_path(*e, "author/channelId"));
      c.likes = json_string(json_path(*e, "toolbar/likeCountNotliked"));
      c.replies = static_cast<int>(json_number(json_path(*e, "toolbar/replyCount")));
      out.comments.push_back(std::move(c));
      break;
    }
    return true;
  });
  // Next page: the last continuation item outside the threads (a reply token
  // hangs under its thread).
  std::vector<const Json*> threads, inside, conts;
  json_find_all(reply, "commentThreadRenderer", threads);
  for (const Json* t : threads) json_find_all(*t, "continuationItemRenderer", inside);
  json_walk(reply, [&](std::string_view k, const Json& v) {
    if (k == "continuationItemRenderer" &&
        std::find(inside.begin(), inside.end(), &v) == inside.end())
      conts.push_back(&v);
    return true;
  });
  for (auto it = conts.rbegin(); it != conts.rend() && out.next.empty(); ++it)
    out.next = json_string(json_find(**it, "token"));
  return out;
}

std::string yt_parse_resolved_id(const Json& reply) {
  return json_string(json_find(reply, "browseId"));
}

// ---- refs -----------------------------------------------------------------

std::optional<YtRef> parse_youtube_ref(std::string_view s) {
  trim_ascii(s);
  if (!starts_with_ci(s, kPrefix)) return std::nullopt;
  s.remove_prefix(kPrefix.size());
  if (const size_t hash = s.find('#'); hash != std::string_view::npos)
    s = s.substr(0, hash);
  const size_t q = s.find('?');
  std::string_view path = s.substr(0, q);
  const std::string_view query =
      q == std::string_view::npos ? std::string_view() : s.substr(q + 1);
  YtRef r;
  if (starts_with(path, "channel/")) {
    r.kind = YtRef::Kind::Channel;
    r.id = std::string(path.substr(8));
    if (!starts_with(r.id, "UC") || !id_chars(r.id, false)) return std::nullopt;
    r.page = parse_page(query_param(query, "page"));
  } else if (starts_with(path, "@") || starts_with(path, "c/") ||
             starts_with(path, "user/")) {
    r.kind = YtRef::Kind::Named;
    r.id = std::string(path);
    if (!id_chars(r.id, true) || ends_with(r.id, "/")) return std::nullopt;
    r.page = parse_page(query_param(query, "page"));
  } else if (path == "playlist") {
    r.kind = YtRef::Kind::Playlist;
    r.id = query_param(query, "list");
    if (!id_chars(r.id, false)) return std::nullopt;
    r.page = parse_page(query_param(query, "page"));
  } else if (path == "watch") {
    r.kind = YtRef::Kind::Video;
    r.id = query_param(query, "v");
    if (r.id.size() < 6 || !id_chars(r.id, false)) return std::nullopt;
    r.page = parse_page(query_param(query, "comments"));
    r.newest = query_param(query, "sort") == "new";
  } else {
    return std::nullopt;
  }
  return r;
}

std::string youtube_ref_string(const YtRef& r) {
  std::string s(kPrefix);
  switch (r.kind) {
    case YtRef::Kind::Channel: s += "channel/" + r.id; break;
    case YtRef::Kind::Named: s += r.id; break;
    case YtRef::Kind::Playlist: s += "playlist?list=" + r.id; break;
    case YtRef::Kind::Video: s += "watch?v=" + r.id; break;
  }
  const bool video = r.kind == YtRef::Kind::Video;
  char sep = video || r.kind == YtRef::Kind::Playlist ? '&' : '?';
  if (r.page > 1) {
    s += sep;
    s += (video ? "comments=" : "page=") + std::to_string(r.page);
    sep = '&';
  }
  if (video && r.newest) s += std::string(1, sep) + "sort=new";
  return s;
}

std::string youtube_web_url(const YtRef& r) {
  switch (r.kind) {
    case YtRef::Kind::Channel: return "https://www.youtube.com/channel/" + r.id;
    case YtRef::Kind::Named: return "https://www.youtube.com/" + r.id;
    case YtRef::Kind::Playlist: return "https://www.youtube.com/playlist?list=" + r.id;
    case YtRef::Kind::Video: return "https://www.youtube.com/watch?v=" + r.id;
  }
  return {};
}

std::string youtube_ref_from_web_url(std::string_view url) {
  trim_ascii(url);
  if (starts_with_ci(url, "https://")) url.remove_prefix(8);
  else if (starts_with_ci(url, "http://")) url.remove_prefix(7);
  else return {};
  const size_t end = url.find_first_of("/?#");
  std::string host = ascii_lower(url.substr(0, end));
  std::string_view rest =
      end == std::string_view::npos ? std::string_view() : url.substr(end);
  if (const size_t hash = rest.find('#'); hash != std::string_view::npos)
    rest = rest.substr(0, hash);
  const size_t q = rest.find('?');
  std::string_view path = rest.substr(0, q);
  const std::string_view query =
      q == std::string_view::npos ? std::string_view() : rest.substr(q + 1);
  if (starts_with(path, "/")) path.remove_prefix(1);
  while (ends_with(path, "/")) path.remove_suffix(1);

  YtRef r;
  auto video = [&](std::string_view id) -> std::string {
    r.kind = YtRef::Kind::Video;
    r.id = std::string(id.substr(0, id.find('/')));
    return r.id.size() >= 6 && id_chars(r.id, false) ? youtube_ref_string(r)
                                                      : std::string();
  };
  if (host == "youtu.be") return video(path);
  if (host != "youtube.com" && host != "www.youtube.com" &&
      host != "m.youtube.com")
    return {};
  if (path == "watch") return video(query_param(query, "v"));
  for (std::string_view p : {"shorts/", "live/", "embed/"})
    if (starts_with(path, p)) return video(path.substr(p.size()));
  if (path == "playlist") {
    r.kind = YtRef::Kind::Playlist;
    r.id = query_param(query, "list");
    return id_chars(r.id, false) ? youtube_ref_string(r) : std::string();
  }
  // Channel pages, with or without a tab: /@x, /@x/videos, /channel/UC…/featured
  std::string_view head = path.substr(0, path.find('/'));
  std::string_view tail =
      path.find('/') == std::string_view::npos ? std::string_view() : path.substr(path.find('/') + 1);
  if (starts_with(head, "@")) {
    r.kind = YtRef::Kind::Named;
    r.id = std::string(head);
  } else if (head == "channel" || head == "c" || head == "user") {
    const std::string_view name = tail.substr(0, tail.find('/'));
    if (head == "channel") {
      r.kind = YtRef::Kind::Channel;
      r.id = std::string(name);
      if (!starts_with(r.id, "UC")) return {};
    } else {
      r.kind = YtRef::Kind::Named;
      r.id = std::string(head) + "/" + std::string(name);
    }
  } else {
    return {};
  }
  return id_chars(r.id, true) ? youtube_ref_string(r) : std::string();
}

// ---- documents ------------------------------------------------------------

namespace {

void append_plain(Document& doc, NodeId parent, std::string_view text) {
  std::string para;
  auto flush = [&] {
    std::string_view p = para;
    trim_ascii(p);
    if (!p.empty())
      doc.append_text(doc.create(NodeKind::Paragraph, parent), std::string(p));
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

void add_link(Document& doc, NodeId para, std::string text, std::string href,
              InlineRole role = InlineRole::Normal) {
  InlineSpan s;
  s.kind = InlineKind::Link;
  s.text = std::move(text);
  s.href = std::move(href);
  s.role = role;
  doc.get(para).spans.push_back(std::move(s));
}

void add_meta(Document& doc, NodeId para, std::string text) {
  InlineSpan s;
  s.text = std::move(text);
  s.role = InlineRole::Metadata;
  doc.get(para).spans.push_back(std::move(s));
}

std::string thumb_url(const std::string& id) {
  return "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg";
}

std::string external(const std::string& url) {
  return std::string(kExternalPrefix) + url;
}

const char* kDot = " \xC2\xB7 ";

}  // namespace

Document yt_listing_document(const YtListing& l, const YtRef& ref, bool has_prev) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  const std::string title = l.title.empty() ? ref.id : l.title;
  doc.meta.title = title + " \xE2\x80\x93 YouTube";

  NodeId head = doc.create(NodeKind::Header, root);
  doc.append_text(head, title, InlineKind::Strong);
  std::string facts = l.handle;
  for (const std::string* part : {&l.subscribers, &l.video_count})
    if (!part->empty()) facts += (facts.empty() ? "" : kDot) + *part;
  if (!facts.empty()) {
    NodeId p = doc.create(NodeKind::Paragraph, root);
    add_meta(doc, p, facts);
  }
  if (ref.page == 1 && !l.description.empty()) append_plain(doc, root, l.description);

  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title;
  doc.get(thread).label = "youtube";
  for (const YtVideo& v : l.videos) {
    NodeId art = doc.create(NodeKind::Article, thread);
    doc.get(art).label = v.id;
    NodeId h = doc.create(NodeKind::Header, art);
    YtRef w;
    w.id = v.id;
    add_link(doc, h, v.title.empty() ? v.id : v.title, youtube_ref_string(w));
    NodeId im = doc.create(NodeKind::Image, art);
    doc.get(im).src = thumb_url(v.id);
    doc.get(im).float_side = "left";
    std::string line;
    for (const std::string* part : {&v.duration, &v.views, &v.age, &v.channel})
      if (!part->empty()) line += (line.empty() ? "" : kDot) + *part;
    NodeId cap = doc.create(NodeKind::Caption, art);
    doc.append_text(cap, line);
  }
  if (l.videos.empty()) {
    NodeId p = doc.create(NodeKind::Paragraph, root);
    doc.append_text(p, "No videos found.", InlineKind::Emphasis);
  }

  NodeId nav = doc.create(NodeKind::Paragraph, root);
  YtRef other = ref;
  if (has_prev) {
    other.page = ref.page - 1;
    add_link(doc, nav, "\xE2\x86\x90 Newer", youtube_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  if (!l.next.empty()) {
    other.page = ref.page + 1;
    add_link(doc, nav, "Older \xE2\x86\x92", youtube_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  add_meta(doc, nav, "page " + std::to_string(ref.page) + kDot);
  add_link(doc, nav, "Open on YouTube", external(youtube_web_url(ref)),
           InlineRole::Metadata);
  return doc;
}

Document yt_video_document(const YtVideoInfo& v, const YtComments& c,
                           const YtRef& ref) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  const std::string title = v.title.empty() ? v.id : v.title;
  doc.meta.title = title + " \xE2\x80\x93 YouTube";

  NodeId head = doc.create(NodeKind::Header, root);
  doc.append_text(head, title, InlineKind::Strong);
  NodeId by = doc.create(NodeKind::Paragraph, root);
  if (!v.owner.empty()) {
    if (v.owner_id.empty()) add_meta(doc, by, v.owner);
    else add_link(doc, by, v.owner, "youtube:channel/" + v.owner_id);
  }
  for (const std::string* part : {&v.views, &v.date})
    if (!part->empty()) add_meta(doc, by, (doc.get(by).spans.empty() ? "" : kDot) + *part);

  if (ref.page == 1) {
    NodeId im = doc.create(NodeKind::Image, root);
    doc.get(im).src = thumb_url(v.id);
    doc.get(im).float_side = "left";
    NodeId watch = doc.create(NodeKind::Paragraph, root);
    add_link(doc, watch, "\xE2\x96\xB6 Watch on YouTube",
             external("https://www.youtube.com/watch?v=" + v.id));
    if (!v.description.empty()) append_plain(doc, root, v.description);
  }

  NodeId label = doc.create(NodeKind::Paragraph, root);
  doc.append_text(label, "Comments" + (c.total.empty() ? std::string() : " (" + c.total + ")"),
                  InlineKind::Strong);
  YtRef sort = ref;
  sort.page = 1;
  sort.newest = !ref.newest;
  add_meta(doc, label, kDot);
  add_link(doc, label, ref.newest ? "Show top comments" : "Show newest first",
           youtube_ref_string(sort), InlineRole::Metadata);

  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title;
  doc.get(thread).label = "comments";
  for (const YtComment& cm : c.comments) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    Node& n = doc.get(msg);
    n.label = cm.id;
    n.src = cm.author.empty() ? "[unknown]" : cm.author;
    n.summary = cm.published;
    if (!cm.channel_id.empty()) {
      n.spans.resize(1);
      n.spans[0].kind = InlineKind::Link;
      n.spans[0].href = "youtube:channel/" + cm.channel_id;
    }
    append_plain(doc, msg, cm.text);
    std::string meta;
    if (!cm.likes.empty() && cm.likes != "0") meta = "\xE2\x96\xB2 " + cm.likes;
    if (cm.replies > 0)
      meta += (meta.empty() ? "" : kDot) + std::to_string(cm.replies) +
              (cm.replies == 1 ? " reply" : " replies");
    if (!meta.empty()) {
      NodeId cap = doc.create(NodeKind::Caption, msg);
      doc.append_text(cap, meta);
    }
  }
  if (c.comments.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    doc.append_text(doc.create(NodeKind::Paragraph, msg),
                    ref.page > 1 ? "No more comments."
                                 : "No comments (they may be turned off).");
  }

  NodeId nav = doc.create(NodeKind::Paragraph, root);
  YtRef other = ref;
  if (ref.page > 1) {
    other.page = ref.page - 1;
    add_link(doc, nav, "\xE2\x86\x90 Previous comments", youtube_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  if (!c.next.empty()) {
    other.page = ref.page + 1;
    add_link(doc, nav, "More comments \xE2\x86\x92", youtube_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  add_meta(doc, nav, "replies are not shown" + std::string(kDot));
  add_link(doc, nav, "Open on YouTube", external(youtube_web_url(ref)),
           InlineRole::Metadata);
  return doc;
}

// ---- network --------------------------------------------------------------

#if defined(XANATOO_YOUTUBE) && XANATOO_YOUTUBE

namespace {

constexpr const char* kVideosTab = "EgZ2aWRlb3PyBgQKAjoA";

std::string json_quote(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
  }
  return o + "\"";
}

std::optional<Json> innertube(std::string_view endpoint, const std::string& fields,
                              std::string* err) {
  static const std::string ua = user_agent("read-only YouTube client");
  const std::string body =
      R"({"context":{"client":{"clientName":"WEB","clientVersion":"2.20260101.00.00","hl":"en","gl":"US"}},)" +
      fields + "}";
  auto resp = https_request(
      "POST",
      "https://www.youtube.com/youtubei/v1/" + std::string(endpoint) +
          "?prettyPrint=false",
      ua, "Content-Type: application/json\r\nCookie: SOCS=CAI\r\n", body, err);
  if (!resp) return std::nullopt;
  if (resp->status != 200) {
    if (err)
      *err = "YouTube answered HTTP " + std::to_string(resp->status) +
             (resp->status == 429 ? " (rate limited, try again later)" : "");
    return std::nullopt;
  }
  auto json = json_parse(resp->body);
  if (!json && err) *err = "YouTube sent a reply that is not JSON";
  return json;
}

std::string continuation_fields(const std::string& token) {
  return "\"continuation\":" + json_quote(token);
}

std::optional<Document> fetch_listing(const YtRef& ref, std::string* err) {
  std::string browse_id;
  std::string params;
  switch (ref.kind) {
    case YtRef::Kind::Channel:
      browse_id = ref.id;
      params = kVideosTab;
      break;
    case YtRef::Kind::Playlist:
      browse_id = starts_with(ref.id, "VL") ? ref.id : "VL" + ref.id;
      break;
    default: {
      auto r = innertube("navigation/resolve_url",
                         "\"url\":" + json_quote("https://www.youtube.com/" + ref.id), err);
      if (!r) return std::nullopt;
      browse_id = yt_parse_resolved_id(*r);
      if (browse_id.empty()) {
        if (err) *err = "No YouTube channel named " + ref.id;
        return std::nullopt;
      }
      params = kVideosTab;
    }
  }
  std::string fields = "\"browseId\":" + json_quote(browse_id);
  if (!params.empty() && starts_with(browse_id, "UC"))
    fields += ",\"params\":" + json_quote(params);
  auto reply = innertube("browse", fields, err);
  if (!reply) return std::nullopt;
  YtListing head = yt_parse_browse(*reply);
  YtListing chunk = head;
  for (int page = 2; page <= ref.page; ++page) {
    if (chunk.next.empty()) {
      if (err) *err = "That page is past the last video.";
      return std::nullopt;
    }
    auto more = innertube("browse", continuation_fields(chunk.next), err);
    if (!more) return std::nullopt;
    chunk = yt_parse_browse(*more);
  }
  if (head.title.empty() && head.videos.empty()) {
    if (err) *err = "YouTube returned no channel or playlist data";
    return std::nullopt;
  }
  head.videos = std::move(chunk.videos);
  head.next = std::move(chunk.next);
  return yt_listing_document(head, ref, ref.page > 1);
}

std::optional<Document> fetch_video(const YtRef& ref, std::string* err) {
  auto reply = innertube("next", "\"videoId\":" + json_quote(ref.id), err);
  if (!reply) return std::nullopt;
  YtVideoInfo info = yt_parse_video(*reply, ref.id);
  if (info.title.empty() && info.owner.empty()) {
    if (err) *err = "YouTube returned no data for video " + ref.id;
    return std::nullopt;
  }
  YtComments comments;
  std::string token = ref.newest && !info.comments_new.empty() ? info.comments_new
                                                               : info.comments_top;
  for (int page = 1; page <= ref.page && !token.empty(); ++page) {
    auto more = innertube("next", continuation_fields(token), err);
    if (!more) {
      if (page == 1) return std::nullopt;
      break;
    }
    YtComments got = yt_parse_comments(*more);
    if (page == 1) comments.total = got.total;
    comments.comments = std::move(got.comments);
    comments.next = got.next;
    token = got.next;
  }
  if (ref.page > 1 && token.empty() && comments.comments.empty())
    comments.next.clear();
  return yt_video_document(info, comments, ref);
}

}  // namespace

std::optional<Document> youtube_fetch(std::string_view ref, std::string* err) {
  auto r = parse_youtube_ref(ref);
  if (!r) {
    if (err) *err = "Not a YouTube address: " + std::string(ref);
    return std::nullopt;
  }
  return r->kind == YtRef::Kind::Video ? fetch_video(*r, err) : fetch_listing(*r, err);
}

#endif

}  // namespace xanatoo
