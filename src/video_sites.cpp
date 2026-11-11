// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only PeerTube, Odysee and Dailymotion clients (docs/VIDEO_SITES.md).
// All three speak plain JSON, so the parsers are small and pure; listings and
// video pages become Thread + Article / Message documents like YouTube's.

#include "xanatoo/video_sites.hpp"

#include "xanatoo/html_min.hpp"   // html_to_plain
#include "xanatoo/html_page.hpp"  // kExternalPrefix
#include "xanatoo/search.hpp"     // search_encode / search_decode
#include "xanatoo/util.hpp"

#if defined(XANATOO_VIDEO) && XANATOO_VIDEO
#include "xanatoo/https.hpp"
#include "xanatoo/version.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <string_view>

namespace xanatoo {

namespace {

constexpr int kMaxPage = 200;
constexpr int kPerPage = 20;

struct SiteInfo {
  VideoSite site;
  std::string_view prefix;
  const char* name;
};
constexpr SiteInfo kSites[] = {
    {VideoSite::PeerTube, "peertube:", "PeerTube"},
    {VideoSite::Odysee, "odysee:", "Odysee"},
    {VideoSite::Dailymotion, "dailymotion:", "Dailymotion"},
};

const SiteInfo& info_of(VideoSite s) {
  for (const SiteInfo& i : kSites)
    if (i.site == s) return i;
  return kSites[0];
}

std::string query_param(std::string_view query, std::string_view name) {
  while (!query.empty()) {
    const size_t amp = query.find('&');
    std::string_view kv = query.substr(0, amp);
    query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
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

// Ids may hold anything but blanks and the characters that end a path.
bool id_ok(std::string_view s) {
  if (s.empty() || s.size() > 300) return false;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x21 || u == 0x7f || c == '?' || c == '&' || c == '#') return false;
  }
  return true;
}

bool host_ok(std::string_view h) {
  if (h.size() < 3 || h.size() > 253 || h.find('.') == std::string_view::npos) return false;
  for (char c : h)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':'))
      return false;
  return true;
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string pct_decode(std::string_view s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && hex_val(s[i + 1]) >= 0 && hex_val(s[i + 2]) >= 0) {
      o.push_back(static_cast<char>(hex_val(s[i + 1]) * 16 + hex_val(s[i + 2])));
      i += 2;
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

// Keeps what a ref path can carry as is; everything else (non-ASCII, blanks,
// '?', '&', '#', '%') becomes %XX, so a ref is one clean token.
std::string pct_encode_id(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '-' || c == '.' || c == '_' || c == '~' || c == '@' ||
        c == ':' || c == '/' || c == '!')
      o.push_back(c);
    else
      o += {'%', hex[u >> 4], hex[u & 15]};
  }
  return o;
}

std::string with_commas(long long n) {
  std::string d = std::to_string(n < 0 ? -n : n), o;
  for (size_t i = 0; i < d.size(); ++i) {
    if (i && (d.size() - i) % 3 == 0) o.push_back(',');
    o.push_back(d[i]);
  }
  return (n < 0 ? "-" : "") + o;
}

std::string count_label(long long n, const char* one, const char* many) {
  return with_commas(n) + " " + (n == 1 ? one : many);
}

std::string fmt_duration(long long s) {
  if (s <= 0) return {};
  char buf[32];
  if (s >= 3600)
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
  else
    std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
  return buf;
}

std::string date_unix(long long t) {
  if (t <= 0) return {};
  const std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
  return buf;
}

std::string date_iso(const std::string& s) { return s.size() >= 10 ? s.substr(0, 10) : s; }

std::string url_host(std::string_view url) {
  const size_t s = url.find("://");
  if (s == std::string_view::npos) return {};
  std::string_view rest = url.substr(s + 3);
  return std::string(rest.substr(0, rest.find_first_of("/?#")));
}

// Plain text of an API field that may carry HTML or markup.
std::string text_of(std::string s, bool html) {
  if (html) s = html_to_plain(s);
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

const char* kDot = " \xC2\xB7 ";

void append_plain(Document& doc, NodeId parent, std::string_view text) {
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

std::string external(const std::string& url) { return std::string(kExternalPrefix) + url; }

std::string join_dots(std::initializer_list<const std::string*> parts) {
  std::string line;
  for (const std::string* p : parts)
    if (!p->empty()) line += (line.empty() ? "" : kDot) + *p;
  return line;
}

const Json* list_of(const Json& reply, std::string_view key) {
  const Json* l = json_get(reply, key);
  return l && l->type == Json::Type::Array ? l : nullptr;
}

}  // namespace

// ---- refs -----------------------------------------------------------------

namespace {

// PeerTube video ids: 36-character uuid or 22-character short uuid.
bool pt_video_id(std::string_view id) {
  if (id.size() != 22 && id.size() != 36) return false;
  for (char c : id)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || (id.size() == 36 && c == '-'))) return false;
  return true;
}

}  // namespace

std::optional<VsRef> parse_video_site_ref(std::string_view ref) {
  const SiteInfo* si = nullptr;
  for (const SiteInfo& i : kSites)
    if (starts_with(ref, i.prefix)) si = &i;
  if (!si) return std::nullopt;
  std::string_view rest = ref.substr(si->prefix.size());
  const size_t qm = rest.find('?');
  const std::string_view query = qm == std::string_view::npos ? std::string_view() : rest.substr(qm + 1);
  std::string_view path = rest.substr(0, qm);
  VsRef r;
  r.site = si->site;
  r.thread = query_param(query, "thread");
  if (!r.thread.empty() && !id_ok(r.thread)) return std::nullopt;

  std::string_view sub = path;  // what follows the host (PeerTube) or the prefix
  if (r.site == VideoSite::PeerTube) {
    const size_t slash = path.find('/');
    r.host = std::string(path.substr(0, slash));
    if (!host_ok(r.host)) return std::nullopt;
    sub = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
  }
  const std::string_view search_key = r.site == VideoSite::Odysee ? "s" : "search";
  const size_t slash = sub.find('/');
  const std::string_view head = sub.substr(0, slash);
  const std::string_view tail =
      slash == std::string_view::npos ? std::string_view() : sub.substr(slash + 1);

  switch (r.site) {
    case VideoSite::PeerTube:
      if (sub.empty()) {
        r.kind = VsRef::Kind::Home;
      } else if (sub == "search") {
        r.kind = VsRef::Kind::Search;
        r.id = query_param(query, search_key);
      } else if (head == "c" || head == "a" || head == "w") {
        r.kind = head == "c" ? VsRef::Kind::Channel
                 : head == "a" ? VsRef::Kind::Account : VsRef::Kind::Video;
        r.id = std::string(tail);
      } else {
        return std::nullopt;
      }
      break;
    case VideoSite::Odysee:
      if (sub == "search") {
        r.kind = VsRef::Kind::Search;
        r.id = query_param(query, search_key);
      } else if (!sub.empty()) {
        r.id = std::string(sub);
        r.kind = starts_with(sub, "@") && sub.find('/') == std::string_view::npos
                     ? VsRef::Kind::Channel : VsRef::Kind::Video;
      } else {
        return std::nullopt;
      }
      break;
    case VideoSite::Dailymotion:
      if (sub == "search") {
        r.kind = VsRef::Kind::Search;
        r.id = query_param(query, search_key);
      } else if (head == "user") {
        r.kind = VsRef::Kind::Channel;
        r.id = std::string(tail);
      } else if (!sub.empty() && sub.find('/') == std::string_view::npos) {
        r.kind = VsRef::Kind::Video;
        r.id = std::string(sub);
      } else {
        return std::nullopt;
      }
      break;
  }
  if (r.kind != VsRef::Kind::Home && !id_ok(r.id)) return std::nullopt;
  r.page = parse_page(query_param(query, r.kind == VsRef::Kind::Video ? "comments" : "page"));
  return r;
}

bool is_video_site_ref(std::string_view ref) { return parse_video_site_ref(ref).has_value(); }

const char* video_site_name(VideoSite s) { return info_of(s).name; }

std::string video_site_ref_string(const VsRef& r) {
  std::string out(info_of(r.site).prefix);
  std::string query;
  auto add = [&](const std::string& kv) { query += (query.empty() ? "?" : "&") + kv; };
  if (r.site == VideoSite::PeerTube) out += r.host;
  auto join = [&](const std::string& p) { out += (out.back() == ':' ? "" : "/") + p; };
  switch (r.kind) {
    case VsRef::Kind::Home: break;
    case VsRef::Kind::Search:
      if (r.site == VideoSite::Odysee) out += "search";
      else join("search");
      add(std::string(r.site == VideoSite::Odysee ? "s=" : "search=") + r.id);
      break;
    case VsRef::Kind::Channel:
      if (r.site == VideoSite::PeerTube) join("c/" + r.id);
      else if (r.site == VideoSite::Dailymotion) out += "user/" + r.id;
      else out += r.id;
      break;
    case VsRef::Kind::Account: join("a/" + r.id); break;
    case VsRef::Kind::Video:
      if (r.site == VideoSite::PeerTube) join("w/" + r.id);
      else out += r.id;
      break;
  }
  if (r.kind == VsRef::Kind::Video) {
    if (r.page > 1) add("comments=" + std::to_string(r.page));
    if (!r.thread.empty()) add("thread=" + r.thread);
  } else if (r.page > 1) {
    add("page=" + std::to_string(r.page));
  }
  return out + query;
}

std::string video_site_web_url(const VsRef& r) {
  switch (r.site) {
    case VideoSite::PeerTube: {
      const std::string base = "https://" + r.host;
      switch (r.kind) {
        case VsRef::Kind::Home: return base + "/";
        case VsRef::Kind::Search: return base + "/search?search=" + r.id;
        case VsRef::Kind::Channel: return base + "/c/" + r.id;
        case VsRef::Kind::Account: return base + "/a/" + r.id;
        case VsRef::Kind::Video: return base + "/w/" + r.id;
      }
      break;
    }
    case VideoSite::Odysee:
      return r.kind == VsRef::Kind::Search ? "https://odysee.com/$/search?q=" + r.id
                                           : "https://odysee.com/" + r.id;
    case VideoSite::Dailymotion:
      if (r.kind == VsRef::Kind::Video) return "https://www.dailymotion.com/video/" + r.id;
      if (r.kind == VsRef::Kind::Search) return "https://www.dailymotion.com/search/" + r.id;
      return {};
  }
  return {};
}

std::string video_site_ref_from_web_url(std::string_view url) {
  const size_t s = url.find("://");
  if (s == std::string_view::npos) return {};
  std::string_view rest = url.substr(s + 3);
  const size_t pe = rest.find_first_of("?#");
  const std::string_view query = pe == std::string_view::npos ? std::string_view()
                                 : rest.substr(pe + 1, rest.find('#', pe) == std::string_view::npos
                                                           ? std::string_view::npos : rest.find('#', pe) - pe - 1);
  rest = rest.substr(0, pe);
  const size_t hs = rest.find('/');
  std::string host = ascii_lower(rest.substr(0, hs));
  std::string_view path = hs == std::string_view::npos ? std::string_view() : rest.substr(hs + 1);
  while (!path.empty() && path.back() == '/') path.remove_suffix(1);
  VsRef r;
  if (host == "odysee.com" || host == "www.odysee.com") {
    r.site = VideoSite::Odysee;
    if (!starts_with(path, "@") || path.find("//") != std::string_view::npos) return {};
    r.id = pct_encode_id(pct_decode(path));
    r.kind = r.id.find('/') == std::string::npos ? VsRef::Kind::Channel : VsRef::Kind::Video;
    if (r.id.find('/') != r.id.rfind('/')) return {};
  } else if (host == "dailymotion.com" || host == "www.dailymotion.com" || host == "dai.ly") {
    r.site = VideoSite::Dailymotion;
    r.kind = VsRef::Kind::Video;
    if (host == "dai.ly") r.id = std::string(path);
    else if (starts_with(path, "video/")) r.id = std::string(path.substr(6));
    else return {};
    if (r.id.find('/') != std::string::npos) return {};
  } else {
    // PeerTube lives on any host: /w/ID, /videos/watch/ID, /c/NAME, /a/NAME.
    r.site = VideoSite::PeerTube;
    r.host = host;
    const size_t sl = path.find('/');
    const std::string_view head = path.substr(0, sl);
    std::string_view tail = sl == std::string_view::npos ? std::string_view() : path.substr(sl + 1);
    tail = tail.substr(0, tail.find('/'));
    if (head == "w") r.kind = VsRef::Kind::Video;
    else if (head == "videos" && starts_with(path, "videos/watch/")) {
      r.kind = VsRef::Kind::Video;
      tail = path.substr(13);
      tail = tail.substr(0, tail.find('/'));
    } else if (head == "video-channels") r.kind = VsRef::Kind::Channel;
    else if (head == "accounts") r.kind = VsRef::Kind::Account;
    else if (path == "search") {
      r.kind = VsRef::Kind::Search;
      tail = query_param(query, "search");
    } else return {};
    r.id = std::string(tail);
    // /w/ is too common a path (wikis): only a short or full video uuid counts.
    if (r.kind == VsRef::Kind::Video && !pt_video_id(r.id)) return {};
  }
  if ((r.site == VideoSite::PeerTube && !host_ok(r.host)) ||
      (r.kind != VsRef::Kind::Home && !id_ok(r.id)))
    return {};
  return video_site_ref_string(r);
}

// ---- PeerTube ---------------------------------------------------------------

namespace {

std::string pt_ref(std::string_view host, VsRef::Kind kind, std::string id) {
  VsRef r;
  r.site = VideoSite::PeerTube;
  r.host = std::string(host);
  r.kind = kind;
  r.id = std::move(id);
  return video_site_ref_string(r);
}

std::string pt_account_name(const Json* a) {
  std::string n = json_string(json_get(*a, "displayName"));
  return n.empty() ? json_string(json_get(*a, "name")) : n;
}

std::string pt_abs(std::string_view host, std::string path) {
  if (path.empty() || starts_with(path, "http")) return path;
  return "https://" + std::string(host) + path;
}

}  // namespace

VsListing pt_parse_videos(const Json& reply, std::string_view host, int page, int per_page) {
  VsListing out;
  const long long total = json_number(json_get(reply, "total"));
  out.has_next = static_cast<long long>(page) * per_page < total;
  if (total > 0) out.subtitle = count_label(total, "video", "videos");
  const Json* data = list_of(reply, "data");
  if (!data) return out;
  for (const Json& v : data->items) {
    VsVideo o;
    o.id = json_string(json_get(v, "uuid"));
    if (o.id.empty()) continue;
    std::string vhost = url_host(json_string(json_get(v, "url")));
    if (vhost.empty()) vhost = std::string(host);
    o.ref = pt_ref(vhost, VsRef::Kind::Video, o.id);
    o.title = json_string(json_get(v, "name"));
    if (const Json* ch = json_get(v, "channel"); ch && ch->type == Json::Type::Object) {
      o.channel = pt_account_name(ch);
      const std::string name = json_string(json_get(*ch, "name"));
      std::string chost = json_string(json_get(*ch, "host"));
      if (!name.empty()) o.channel_ref = pt_ref(chost.empty() ? vhost : chost, VsRef::Kind::Channel, name);
    } else if (const Json* ac = json_get(v, "account"); ac && ac->type == Json::Type::Object) {
      o.channel = pt_account_name(ac);
    }
    const long long views = json_number(json_get(v, "views"));
    o.views = count_label(views, "view", "views");
    o.age = date_iso(json_string(json_get(v, "publishedAt")));
    o.duration = fmt_duration(json_number(json_get(v, "duration")));
    o.thumb = json_string(json_get(v, "thumbnailUrl"));
    if (o.thumb.empty()) o.thumb = pt_abs(host, json_string(json_get(v, "thumbnailPath")));
    out.videos.push_back(std::move(o));
  }
  return out;
}

VsVideoInfo pt_parse_video(const Json& v, std::string_view host) {
  VsVideoInfo o;
  o.title = json_string(json_get(v, "name"));
  std::string vhost = url_host(json_string(json_get(v, "url")));
  if (vhost.empty()) vhost = std::string(host);
  const std::string uuid = json_string(json_get(v, "uuid"));
  o.web_url = json_string(json_get(v, "url"));
  if (o.web_url.empty() && !uuid.empty()) o.web_url = "https://" + vhost + "/w/" + uuid;
  o.play_url = o.web_url;
  if (const Json* ch = json_get(v, "channel"); ch && ch->type == Json::Type::Object) {
    o.owner = pt_account_name(ch);
    const std::string name = json_string(json_get(*ch, "name"));
    std::string chost = json_string(json_get(*ch, "host"));
    if (!name.empty()) o.owner_ref = pt_ref(chost.empty() ? vhost : chost, VsRef::Kind::Channel, name);
  }
  if (o.owner.empty())
    if (const Json* ac = json_get(v, "account")) o.owner = pt_account_name(ac);
  o.views = count_label(json_number(json_get(v, "views")), "view", "views");
  o.likes = json_number(json_get(v, "likes")) > 0
                ? "\xE2\x96\xB2 " + with_commas(json_number(json_get(v, "likes"))) : std::string();
  o.date = date_iso(json_string(json_get(v, "publishedAt")));
  o.duration = fmt_duration(json_number(json_get(v, "duration")));
  o.thumb = json_string(json_get(v, "thumbnailUrl"));
  if (o.thumb.empty()) o.thumb = pt_abs(host, json_string(json_get(v, "previewPath")));
  if (o.thumb.empty()) o.thumb = pt_abs(host, json_string(json_get(v, "thumbnailPath")));
  o.description = text_of(json_string(json_get(v, "truncatedDescription")), false);
  o.comment_count = static_cast<int>(json_number(json_get(v, "comments")));
  return o;
}

std::string pt_parse_description(const Json& reply) {
  return text_of(json_string(json_get(reply, "description")), false);
}

void pt_parse_channel_header(const Json& r, VsListing& l) {
  l.title = json_string(json_get(r, "displayName"));
  if (l.title.empty()) l.title = json_string(json_get(r, "name"));
  const std::string name = json_string(json_get(r, "name"));
  const std::string host = json_string(json_get(r, "host"));
  std::string sub = name.empty() ? std::string() : name + "@" + host;
  const long long fol = json_number(json_get(r, "followersCount"));
  if (fol > 0) sub += (sub.empty() ? "" : kDot) + count_label(fol, "follower", "followers");
  l.subtitle = sub;
  l.description = text_of(json_string(json_get(r, "description")), false);
}

namespace {

VsComment pt_comment(const Json& c, std::string_view host) {
  VsComment o;
  o.id = std::to_string(json_number(json_get(c, "id")));
  const Json* ac = json_get(c, "account");
  if (json_get(c, "isDeleted") && json_get(c, "isDeleted")->b) {
    o.author = "[deleted]";
    o.text = "[deleted]";
  } else {
    if (ac && ac->type == Json::Type::Object) {
      o.author = pt_account_name(ac);
      const std::string name = json_string(json_get(*ac, "name"));
      const std::string ahost = json_string(json_get(*ac, "host"));
      if (!name.empty())
        o.author_ref = pt_ref(host, VsRef::Kind::Account,
                              ahost.empty() || ahost == host ? name : name + "@" + ahost);
    }
    o.text = text_of(json_string(json_get(c, "text")), true);
  }
  o.published = date_iso(json_string(json_get(c, "createdAt")));
  o.replies = static_cast<int>(json_number(json_get(c, "totalReplies")));
  return o;
}

void pt_flatten(const Json& node, std::string_view host, VsComments& out) {
  if (const Json* c = json_get(node, "comment")) {
    VsComment cm = pt_comment(*c, host);
    cm.replies = 0;
    out.comments.push_back(std::move(cm));
  }
  if (const Json* kids = list_of(node, "children"))
    for (const Json& k : kids->items) pt_flatten(k, host, out);
}

}  // namespace

VsComments pt_parse_comments(const Json& reply, std::string_view host, const VsRef&,
                             int page, int per_page) {
  VsComments out;
  if (json_get(reply, "comment")) {  // one thread: the root and everything below
    pt_flatten(reply, host, out);
    return out;
  }
  const long long total = json_number(json_get(reply, "total"));
  if (total > 0) out.total = with_commas(total);
  out.has_next = static_cast<long long>(page) * per_page < total;
  if (const Json* data = list_of(reply, "data"))
    for (const Json& c : data->items) out.comments.push_back(pt_comment(c, host));
  return out;
}

// ---- Odysee -----------------------------------------------------------------

std::string od_lbry_url(std::string_view id) {
  std::string s = pct_decode(id);
  std::replace(s.begin(), s.end(), ':', '#');
  return "lbry://" + s;
}

namespace {

// "lbry://@chan#8/slug#e" → ref path "@chan:8/slug:e" (percent-encoded).
std::string od_path_of(std::string url) {
  if (starts_with(url, "lbry://")) url.erase(0, 7);
  std::replace(url.begin(), url.end(), '#', ':');
  return pct_encode_id(url);
}

std::string od_ref(const std::string& path) {
  VsRef r;
  r.site = VideoSite::Odysee;
  r.id = path;
  r.kind = path.find('/') == std::string::npos && starts_with(path, "@") ? VsRef::Kind::Channel
                                                                           : VsRef::Kind::Video;
  return video_site_ref_string(r);
}

long long od_time(const Json& claim) {
  long long t = json_number(json_path(claim, "value/release_time"));
  if (t <= 0) t = json_number(json_get(claim, "timestamp"));
  if (t <= 0) t = json_number(json_path(claim, "meta/creation_timestamp"));
  return t;
}

std::string od_channel_title(const Json& claim) {
  const Json* sc = json_get(claim, "signing_channel");
  if (!sc) return {};
  std::string t = json_string(json_path(*sc, "value/title"));
  return t.empty() ? json_string(json_get(*sc, "name")) : t;
}

std::string od_channel_ref(const Json& claim) {
  const Json* sc = json_get(claim, "signing_channel");
  if (!sc) return {};
  const std::string u = json_string(json_get(*sc, "canonical_url"));
  return u.empty() ? std::string() : od_ref(od_path_of(u));
}

}  // namespace

const Json* od_first_claim(const Json& reply, std::string_view lbry_url) {
  const Json* res = json_get(reply, "result");
  if (!res) return nullptr;
  if (const Json* items = list_of(*res, "items")) return json_at(*items, 0);
  const Json* c = json_get(*res, lbry_url);
  if (c && json_get(*c, "error")) return nullptr;
  if (c && json_get(*c, "claim_id")) return c;
  return nullptr;
}

std::vector<std::string> od_parse_search_ids(const Json& reply) {
  std::vector<std::string> ids;
  if (reply.type == Json::Type::Array)
    for (const Json& i : reply.items)
      if (std::string id = json_string(json_get(i, "claimId")); !id.empty()) ids.push_back(id);
  return ids;
}

VsListing od_parse_claims(const Json& reply, int page) {
  VsListing out;
  const Json* res = json_get(reply, "result");
  if (!res) return out;
  const long long pages = json_number(json_get(*res, "total_pages"));
  const long long total = json_number(json_get(*res, "total_items"));
  out.has_next = page < pages;
  if (total > 0) out.subtitle = count_label(total, "video", "videos");
  const Json* items = list_of(*res, "items");
  if (!items) return out;
  for (const Json& c : items->items) {
    if (json_string(json_get(c, "value_type")) != "stream") continue;
    VsVideo o;
    o.id = json_string(json_get(c, "claim_id"));
    std::string url = json_string(json_get(c, "canonical_url"));
    if (url.empty()) url = json_string(json_get(c, "permanent_url"));
    if (url.empty()) continue;
    o.ref = od_ref(od_path_of(url));
    o.title = json_string(json_path(c, "value/title"));
    if (o.title.empty()) o.title = json_string(json_get(c, "name"));
    o.channel = od_channel_title(c);
    o.channel_ref = od_channel_ref(c);
    o.age = date_unix(od_time(c));
    o.duration = fmt_duration(json_number(json_path(c, "value/video/duration")));
    o.thumb = json_string(json_path(c, "value/thumbnail/url"));
    out.videos.push_back(std::move(o));
  }
  return out;
}

VsVideoInfo od_parse_claim(const Json& c) {
  VsVideoInfo o;
  o.title = json_string(json_path(c, "value/title"));
  if (o.title.empty()) o.title = json_string(json_get(c, "name"));
  o.owner = od_channel_title(c);
  o.owner_ref = od_channel_ref(c);
  o.date = date_unix(od_time(c));
  o.duration = fmt_duration(json_number(json_path(c, "value/video/duration")));
  o.description = text_of(json_string(json_path(c, "value/description")), false);
  o.thumb = json_string(json_path(c, "value/thumbnail/url"));
  std::string url = json_string(json_get(c, "canonical_url"));
  if (url.empty()) url = json_string(json_get(c, "permanent_url"));
  if (!url.empty()) o.web_url = "https://odysee.com/" + od_path_of(url);
  o.play_url = o.web_url;
  return o;
}

void od_parse_channel_header(const Json& c, VsListing& l) {
  l.title = json_string(json_path(c, "value/title"));
  if (l.title.empty()) l.title = json_string(json_get(c, "name"));
  l.subtitle = json_string(json_get(c, "name"));
  l.description = text_of(json_string(json_path(c, "value/description")), false);
}

VsComments od_parse_comments(const Json& reply, int page) {
  VsComments out;
  const Json* res = json_get(reply, "result");
  if (!res) return out;
  const long long total = json_number(json_get(*res, "total_items"));
  if (total > 0) out.total = with_commas(total);
  out.has_next = page < json_number(json_get(*res, "total_pages"));
  const Json* items = list_of(*res, "items");
  if (!items) return out;
  for (const Json& c : items->items) {
    VsComment o;
    o.id = json_string(json_get(c, "comment_id"));
    o.author = json_string(json_get(c, "channel_name"));
    const std::string cu = json_string(json_get(c, "channel_url"));
    if (!cu.empty()) o.author_ref = od_ref(od_path_of(cu));
    o.text = text_of(json_string(json_get(c, "comment")), false);
    o.published = date_unix(json_number(json_get(c, "timestamp")));
    o.replies = static_cast<int>(json_number(json_get(c, "replies")));
    out.comments.push_back(std::move(o));
  }
  return out;
}

// ---- Dailymotion ------------------------------------------------------------

namespace {

std::string dm_ref(std::string id) {
  VsRef r;
  r.site = VideoSite::Dailymotion;
  r.kind = VsRef::Kind::Video;
  r.id = std::move(id);
  return video_site_ref_string(r);
}

std::string dm_user_ref(const std::string& id) {
  if (id.empty()) return {};
  VsRef r;
  r.site = VideoSite::Dailymotion;
  r.kind = VsRef::Kind::Channel;
  r.id = id;
  return video_site_ref_string(r);
}

}  // namespace

VsListing dm_parse_videos(const Json& reply, int) {
  VsListing out;
  if (const Json* hm = json_get(reply, "has_more")) out.has_next = hm->b;
  const Json* list = list_of(reply, "list");
  if (!list) return out;
  for (const Json& v : list->items) {
    VsVideo o;
    o.id = json_string(json_get(v, "id"));
    if (o.id.empty()) continue;
    o.ref = dm_ref(o.id);
    o.title = json_string(json_get(v, "title"));
    o.channel = json_string(json_get(v, "owner.screenname"));
    o.channel_ref = dm_user_ref(json_string(json_get(v, "owner.id")));
    if (json_get(v, "views_total"))
      o.views = count_label(json_number(json_get(v, "views_total")), "view", "views");
    o.age = date_unix(json_number(json_get(v, "created_time")));
    o.duration = fmt_duration(json_number(json_get(v, "duration")));
    o.thumb = json_string(json_get(v, "thumbnail_240_url"));
    out.videos.push_back(std::move(o));
  }
  return out;
}

VsVideoInfo dm_parse_video(const Json& v) {
  VsVideoInfo o;
  o.title = json_string(json_get(v, "title"));
  o.owner = json_string(json_get(v, "owner.screenname"));
  o.owner_ref = dm_user_ref(json_string(json_get(v, "owner.id")));
  if (json_get(v, "views_total"))
    o.views = count_label(json_number(json_get(v, "views_total")), "view", "views");
  o.likes = json_number(json_get(v, "likes_total")) > 0
                ? "\xE2\x96\xB2 " + with_commas(json_number(json_get(v, "likes_total"))) : std::string();
  o.date = date_unix(json_number(json_get(v, "created_time")));
  o.duration = fmt_duration(json_number(json_get(v, "duration")));
  o.description = text_of(json_string(json_get(v, "description")), true);
  o.thumb = json_string(json_get(v, "thumbnail_480_url"));
  o.web_url = json_string(json_get(v, "url"));
  o.play_url = o.web_url;
  o.comment_count = static_cast<int>(json_number(json_get(v, "comments_total")));
  return o;
}

void dm_parse_channel_header(const Json& r, VsListing& l) {
  l.title = json_string(json_get(r, "screenname"));
  std::string sub = json_string(json_get(r, "username"));
  if (json_get(r, "videos_total"))
    sub += (sub.empty() ? "" : kDot) + count_label(json_number(json_get(r, "videos_total")), "video", "videos");
  if (json_get(r, "followers_total"))
    sub += (sub.empty() ? "" : kDot) + count_label(json_number(json_get(r, "followers_total")), "follower", "followers");
  l.subtitle = sub;
  l.description = text_of(json_string(json_get(r, "description")), true);
}

VsComments dm_parse_comments(const Json& reply, int) {
  VsComments out;
  if (const Json* hm = json_get(reply, "has_more")) out.has_next = hm->b;
  if (json_get(reply, "total")) out.total = with_commas(json_number(json_get(reply, "total")));
  const Json* list = list_of(reply, "list");
  if (!list) return out;
  for (const Json& c : list->items) {
    VsComment o;
    o.id = json_string(json_get(c, "id"));
    o.author = json_string(json_get(c, "owner.screenname"));
    o.author_ref = dm_user_ref(json_string(json_get(c, "owner.id")));
    o.text = text_of(json_string(json_get(c, "message")), true);
    o.published = date_unix(json_number(json_get(c, "created_time")));
    o.replies = static_cast<int>(json_number(json_get(c, "replies_total")));
    out.comments.push_back(std::move(o));
  }
  return out;
}

// ---- documents --------------------------------------------------------------

namespace {

std::string site_title(const VsRef& ref, const std::string& title) {
  return title + " \xE2\x80\x93 " + (ref.site == VideoSite::PeerTube ? ref.host : video_site_name(ref.site));
}

}  // namespace

Document vs_listing_document(const VsListing& l, const VsRef& ref) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  const char* site = video_site_name(ref.site);
  std::string title = l.title;
  if (title.empty())
    title = ref.kind == VsRef::Kind::Search ? "Search: " + search_decode(ref.id)
            : ref.kind == VsRef::Kind::Home ? "Recent videos" : ref.id;
  const bool search = ref.kind == VsRef::Kind::Search;
  doc.meta.title = site_title(ref, title);

  doc.append_text(doc.create(NodeKind::Header, root), title, InlineKind::Strong);
  if (!l.subtitle.empty()) add_meta(doc, doc.create(NodeKind::Paragraph, root), l.subtitle);
  if (ref.page == 1 && !l.description.empty()) append_plain(doc, root, l.description);

  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title;
  doc.get(thread).label = ascii_lower(std::string_view(site));
  for (const VsVideo& v : l.videos) {
    NodeId art = doc.create(NodeKind::Article, thread);
    doc.get(art).label = v.id;
    add_link(doc, doc.create(NodeKind::Header, art), v.title.empty() ? v.id : v.title, v.ref);
    if (!v.thumb.empty()) {
      NodeId im = doc.create(NodeKind::Image, art);
      doc.get(im).src = v.thumb;
      doc.get(im).float_side = "left";
    }
    NodeId cap = doc.create(NodeKind::Caption, art);
    const std::string line = join_dots({&v.duration, &v.views, &v.age});
    doc.append_text(cap, line);
    if (!v.channel.empty()) {
      if (!line.empty()) add_meta(doc, cap, kDot);
      if (v.channel_ref.empty()) add_meta(doc, cap, v.channel);
      else add_link(doc, cap, v.channel, v.channel_ref, InlineRole::Metadata);
    }
  }
  if (l.videos.empty())
    doc.append_text(doc.create(NodeKind::Paragraph, root),
                    search ? "No videos match." : "No videos found.", InlineKind::Emphasis);

  NodeId nav = doc.create(NodeKind::Paragraph, root);
  VsRef other = ref;
  if (ref.page > 1) {
    other.page = ref.page - 1;
    add_link(doc, nav, search ? "\xE2\x86\x90 Previous" : "\xE2\x86\x90 Newer", video_site_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  if (l.has_next) {
    other.page = ref.page + 1;
    add_link(doc, nav, search ? "More \xE2\x86\x92" : "Older \xE2\x86\x92", video_site_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  add_meta(doc, nav, "page " + std::to_string(ref.page) + kDot);
  if (const std::string web = video_site_web_url(ref); !web.empty())
    add_link(doc, nav, std::string("Open on ") + site, external(web), InlineRole::Metadata);
  return doc;
}

Document vs_video_document(const VsVideoInfo& v, const VsComments& c, const VsRef& ref) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  const char* site = video_site_name(ref.site);
  const bool replies_page = !ref.thread.empty();
  const std::string title = replies_page ? "Replies" : v.title.empty() ? ref.id : v.title;
  doc.meta.title = site_title(ref, title);

  doc.append_text(doc.create(NodeKind::Header, root), title, InlineKind::Strong);
  NodeId by = doc.create(NodeKind::Paragraph, root);
  if (replies_page) {
    VsRef back = ref;
    back.thread.clear();
    back.page = 1;
    add_link(doc, by, "\xE2\x86\x90 Back to the video", video_site_ref_string(back));
  }
  if (!v.owner.empty()) {
    if (!doc.get(by).spans.empty()) add_meta(doc, by, kDot);
    if (v.owner_ref.empty()) add_meta(doc, by, v.owner);
    else add_link(doc, by, v.owner, v.owner_ref);
  }
  for (const std::string* part : {&v.views, &v.date, &v.duration, &v.likes})
    if (!part->empty() && !replies_page)
      add_meta(doc, by, (doc.get(by).spans.empty() ? "" : kDot) + *part);

  if (ref.page == 1 && !replies_page) {
    if (!v.thumb.empty()) {
      NodeId im = doc.create(NodeKind::Image, root);
      doc.get(im).src = v.thumb;
      doc.get(im).float_side = "left";
    }
    if (!v.play_url.empty()) {
      NodeId watch = doc.create(NodeKind::Paragraph, root);
      add_link(doc, watch, std::string("\xE2\x96\xB6 Watch on ") + site, external(v.play_url));
      add_meta(doc, watch, kDot);
      add_link(doc, watch, "\xE2\x96\xB6 Watch with mpv", external("mpv:" + v.play_url));
    }
    if (!v.description.empty()) append_plain(doc, root, v.description);
  }

  if (!replies_page) {
    NodeId label = doc.create(NodeKind::Paragraph, root);
    std::string total = c.total;
    if (total.empty() && v.comment_count > 0) total = with_commas(v.comment_count);
    doc.append_text(label, "Comments" + (total.empty() ? std::string() : " (" + total + ")"),
                    InlineKind::Strong);
  }

  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).summary = title;
  doc.get(thread).label = "comments";
  for (const VsComment& cm : c.comments) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    Node& n = doc.get(msg);
    n.label = cm.id;
    n.src = cm.author.empty() ? "[unknown]" : cm.author;
    n.summary = cm.published;
    if (!cm.author_ref.empty()) {
      n.spans.resize(1);
      n.spans[0].kind = InlineKind::Link;
      n.spans[0].href = cm.author_ref;
    }
    append_plain(doc, msg, cm.text);
    if (cm.replies > 0) {
      NodeId cap = doc.create(NodeKind::Caption, msg);
      const std::string label = std::to_string(cm.replies) + (cm.replies == 1 ? " reply" : " replies");
      if (ref.site == VideoSite::Dailymotion) {
        add_meta(doc, cap, label);
      } else {
        VsRef rr = ref;
        rr.page = 1;
        rr.thread = cm.id;
        add_link(doc, cap, label + " \xE2\x86\x92", video_site_ref_string(rr), InlineRole::Metadata);
      }
    }
  }
  if (c.comments.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    doc.append_text(doc.create(NodeKind::Paragraph, msg),
                    ref.page > 1 ? "No more comments."
                    : replies_page ? "No replies found."
                                   : "No comments (they may be turned off).");
  }

  NodeId nav = doc.create(NodeKind::Paragraph, root);
  VsRef other = ref;
  if (ref.page > 1) {
    other.page = ref.page - 1;
    add_link(doc, nav, "\xE2\x86\x90 Previous comments", video_site_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  if (c.has_next) {
    other.page = ref.page + 1;
    add_link(doc, nav, "More comments \xE2\x86\x92", video_site_ref_string(other));
    add_meta(doc, nav, kDot);
  }
  if (!v.web_url.empty())
    add_link(doc, nav, std::string("Open on ") + site, external(v.web_url), InlineRole::Metadata);
  return doc;
}

// ---- network ----------------------------------------------------------------

#if defined(XANATOO_VIDEO) && XANATOO_VIDEO

namespace {

std::optional<Json> get_json(const std::string& url, std::string* err) {
  static const std::string ua = user_agent("read-only video client");
  std::string e;
  auto resp = https_get_follow(url, ua, &e);
  if (!resp) {
    if (err) *err = e;
    return std::nullopt;
  }
  if (resp->status != 200) {
    if (err) {
      *err = url_host(url) + " answered HTTP " + std::to_string(resp->status);
      if (resp->status == 404) *err += " (not found)";
      if (resp->status == 429) *err += " (rate limited, try again later)";
    }
    return std::nullopt;
  }
  auto json = json_parse(resp->body);
  if (!json && err) *err = url_host(url) + " sent a reply that is not JSON";
  return json;
}

std::optional<Json> post_json(const std::string& url, const std::string& body, std::string* err) {
  static const std::string ua = user_agent("read-only video client");
  auto resp = https_request("POST", url, ua, "Content-Type: application/json\r\n", body, err);
  if (!resp) return std::nullopt;
  if (resp->status != 200) {
    if (err) *err = url_host(url) + " answered HTTP " + std::to_string(resp->status);
    return std::nullopt;
  }
  auto json = json_parse(resp->body);
  if (!json && err) *err = url_host(url) + " sent a reply that is not JSON";
  return json;
}

std::string json_quote(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      default: o += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
  }
  return o + "\"";
}

std::string start_of(int page) { return std::to_string((page - 1) * kPerPage); }

// ---- PeerTube

std::optional<Document> pt_fetch_listing(const VsRef& ref, std::string* err) {
  const std::string base = "https://" + ref.host + "/api/v1/";
  const std::string paging = "start=" + start_of(ref.page) + "&count=" + std::to_string(kPerPage);
  std::string url;
  switch (ref.kind) {
    case VsRef::Kind::Channel: url = base + "video-channels/" + ref.id + "/videos?sort=-publishedAt&" + paging; break;
    case VsRef::Kind::Account: url = base + "accounts/" + ref.id + "/videos?sort=-publishedAt&" + paging; break;
    case VsRef::Kind::Search: url = base + "search/videos?search=" + ref.id + "&" + paging; break;
    default: url = base + "videos?sort=-publishedAt&" + paging; break;
  }
  auto reply = get_json(url, err);
  if (!reply) return std::nullopt;
  VsListing l = pt_parse_videos(*reply, ref.host, ref.page, kPerPage);
  if (ref.kind == VsRef::Kind::Channel || ref.kind == VsRef::Kind::Account) {
    const char* what = ref.kind == VsRef::Kind::Channel ? "video-channels/" : "accounts/";
    if (auto head = get_json(base + what + ref.id, nullptr)) {
      std::string sub = l.subtitle;
      pt_parse_channel_header(*head, l);
      if (!sub.empty()) l.subtitle += (l.subtitle.empty() ? "" : kDot) + sub;
    }
  }
  return vs_listing_document(l, ref);
}

std::optional<Document> pt_fetch_video(const VsRef& ref, std::string* err) {
  const std::string base = "https://" + ref.host + "/api/v1/videos/" + ref.id;
  VsVideoInfo info;
  if (ref.thread.empty()) {
    auto reply = get_json(base, err);
    if (!reply) return std::nullopt;
    info = pt_parse_video(*reply, ref.host);
    if (ref.page == 1)
      if (auto d = get_json(base + "/description", nullptr)) {
        std::string full = pt_parse_description(*d);
        if (!full.empty()) info.description = std::move(full);
      }
  }
  VsComments comments;
  const std::string curl =
      ref.thread.empty()
          ? base + "/comment-threads?sort=-createdAt&start=" + start_of(ref.page) + "&count=" + std::to_string(kPerPage)
          : base + "/comment-threads/" + ref.thread;
  if (auto reply = get_json(curl, nullptr))
    comments = pt_parse_comments(*reply, ref.host, ref, ref.page, kPerPage);
  return vs_video_document(info, comments, ref);
}

// ---- Odysee

constexpr const char* kOdyApi = "https://api.na-backend.odysee.com/api/v1/proxy?m=";

std::optional<Json> od_rpc(const char* method, const std::string& params, std::string* err) {
  return post_json(kOdyApi + std::string(method),
                   std::string(R"({"jsonrpc":"2.0","method":")") + method + R"(","params":)" + params + "}", err);
}

std::optional<Document> od_fetch_listing(const VsRef& ref, std::string* err) {
  const std::string common = R"("page_size":20,"claim_type":["stream"],"stream_types":["video"],"page":)" +
                             std::to_string(ref.page);
  VsListing l;
  if (ref.kind == VsRef::Kind::Search) {
    auto ids = get_json("https://lighthouse.odysee.tv/search?s=" + ref.id + "&mediaType=video&size=" +
                            std::to_string(kPerPage) + "&from=" + start_of(ref.page), err);
    if (!ids) return std::nullopt;
    const std::vector<std::string> order = od_parse_search_ids(*ids);
    if (!order.empty()) {
      std::string list;
      for (const std::string& id : order) list += (list.empty() ? "" : ",") + json_quote(id);
      auto claims = od_rpc("claim_search",
                           R"({"page_size":20,"page":1,"claim_ids":[)" + list + "]}", err);
      if (!claims) return std::nullopt;
      l = od_parse_claims(*claims, 1);
      std::vector<VsVideo> sorted;  // lighthouse's ranking, not the chain's
      for (const std::string& id : order)
        for (VsVideo& v : l.videos)
          if (v.id == id) sorted.push_back(v);
      l.videos = std::move(sorted);
    }
    l.subtitle.clear();
    l.has_next = static_cast<int>(order.size()) >= kPerPage;
    return vs_listing_document(l, ref);
  }
  const std::string channel = pct_decode(ref.id);
  auto claims = od_rpc("claim_search",
                       "{" + common + R"(,"order_by":["release_time"],"channel":)" + json_quote(channel) + "}", err);
  if (!claims) return std::nullopt;
  l = od_parse_claims(*claims, ref.page);
  if (auto head = od_rpc("resolve", R"({"urls":[)" + json_quote(od_lbry_url(ref.id)) + "]}", nullptr))
    if (const Json* c = od_first_claim(*head, od_lbry_url(ref.id))) {
      const std::string sub = l.subtitle;
      od_parse_channel_header(*c, l);
      l.subtitle += (sub.empty() ? "" : kDot) + sub;
    }
  if (l.videos.empty() && l.title.empty()) {
    if (err) *err = "No Odysee channel " + channel;
    return std::nullopt;
  }
  return vs_listing_document(l, ref);
}

std::optional<Document> od_fetch_video(const VsRef& ref, std::string* err) {
  const std::string url = od_lbry_url(ref.id);
  auto reply = od_rpc("resolve", R"({"urls":[)" + json_quote(url) + "]}", err);
  if (!reply) return std::nullopt;
  const Json* claim = od_first_claim(*reply, url);
  if (!claim) {
    if (err) *err = "No Odysee video " + pct_decode(ref.id);
    return std::nullopt;
  }
  VsVideoInfo info = od_parse_claim(*claim);
  const std::string claim_id = json_string(json_get(*claim, "claim_id"));
  std::string params = R"({"claim_id":)" + json_quote(claim_id) + R"(,"page":)" + std::to_string(ref.page) +
                       R"(,"page_size":20,"sort_by":3,"top_level":)" +
                       (ref.thread.empty() ? "true" : "false");
  if (!ref.thread.empty()) params += R"(,"parent_id":)" + json_quote(ref.thread);
  params += "}";
  VsComments comments;
  if (auto cr = post_json("https://comments.odysee.tv/api/v2?m=comment.List",
                          R"({"jsonrpc":"2.0","id":1,"method":"comment.List","params":)" + params + "}", nullptr))
    comments = od_parse_comments(*cr, ref.page);
  return vs_video_document(info, comments, ref);
}

// ---- Dailymotion

constexpr const char* kDmVideoFields =
    "id,title,owner.screenname,owner.id,views_total,duration,created_time,thumbnail_240_url";

std::optional<Document> dm_fetch_listing(const VsRef& ref, std::string* err) {
  const std::string paging = "&limit=" + std::to_string(kPerPage) + "&page=" + std::to_string(ref.page);
  std::string url;
  if (ref.kind == VsRef::Kind::Search)
    url = "https://api.dailymotion.com/videos?search=" + ref.id + "&fields=" + kDmVideoFields + paging;
  else
    url = "https://api.dailymotion.com/user/" + ref.id + "/videos?sort=recent&fields=" + kDmVideoFields + paging;
  auto reply = get_json(url, err);
  if (!reply) return std::nullopt;
  VsListing l = dm_parse_videos(*reply, ref.page);
  if (ref.kind == VsRef::Kind::Channel)
    if (auto head = get_json("https://api.dailymotion.com/user/" + ref.id +
                                 "?fields=screenname,username,description,videos_total,followers_total",
                             nullptr))
      dm_parse_channel_header(*head, l);
  return vs_listing_document(l, ref);
}

std::optional<Document> dm_fetch_video(const VsRef& ref, std::string* err) {
  auto reply = get_json("https://api.dailymotion.com/video/" + ref.id +
                            "?fields=id,title,owner.screenname,owner.id,views_total,likes_total,duration,"
                            "created_time,thumbnail_480_url,description,url,comments_total",
                        err);
  if (!reply) return std::nullopt;
  VsVideoInfo info = dm_parse_video(*reply);
  VsComments comments;
  if (info.comment_count > 0)
    if (auto cr = get_json("https://api.dailymotion.com/video/" + ref.id +
                               "/comments?fields=id,message,owner.screenname,owner.id,created_time,replies_total&limit=" +
                               std::to_string(kPerPage) + "&page=" + std::to_string(ref.page),
                           nullptr))
      comments = dm_parse_comments(*cr, ref.page);
  return vs_video_document(info, comments, ref);
}

}  // namespace

std::optional<Document> video_site_fetch(std::string_view ref, std::string* err) {
  auto r = parse_video_site_ref(ref);
  if (!r) {
    if (err) *err = "Not a video site address: " + std::string(ref);
    return std::nullopt;
  }
  const bool video = r->kind == VsRef::Kind::Video;
  switch (r->site) {
    case VideoSite::PeerTube: return video ? pt_fetch_video(*r, err) : pt_fetch_listing(*r, err);
    case VideoSite::Odysee: return video ? od_fetch_video(*r, err) : od_fetch_listing(*r, err);
    case VideoSite::Dailymotion: return video ? dm_fetch_video(*r, err) : dm_fetch_listing(*r, err);
  }
  return std::nullopt;
}

#endif

}  // namespace xanatoo
