// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Read-only YouTube client over InnerTube (the JSON API youtube.com's own
// pages use; unofficial, no key, no account). See docs/YOUTUBE.md.
//
//   youtube:@handle[?page=N]          channel uploads (also channel/UC…,
//   youtube:channel/UC…[?page=N]      c/name, user/name)
//   youtube:playlist?list=PL…[&page=N]
//   youtube:watch?v=ID[&comments=N][&sort=new]   description + comments
//   youtube:results?search_query=Q[&page=N]      video search (Q percent-encoded)
//
// The parsers are pure (JSON text in, structs / Document out) and unit-tested
// with recorded replies; only youtube_fetch touches the network.

#include "xanatoo/json_dom.hpp"
#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

struct YtRef {
  enum class Kind { Channel, Named, Playlist, Video, Search };
  Kind kind = Kind::Video;
  // Channel: UC… id. Named: "@handle", "c/name" or "user/name" (needs a
  // lookup). Playlist: list id. Video: 11-character video id. Search: the
  // query, percent-encoded with '+' for blanks (search_encode form).
  std::string id;
  int page = 1;      // Channel / Playlist: grid chunk; Video: comment chunk
  bool newest = false;  // Video: comments sorted newest first
};

// "youtube:…" → ref; nullopt for anything else or an empty / odd id.
std::optional<YtRef> parse_youtube_ref(std::string_view ref);
std::string youtube_ref_string(const YtRef& r);
// Equivalent https://www.youtube.com/… address.
std::string youtube_web_url(const YtRef& r);
// youtube.com / youtu.be page URL → ref string ("" when not a page we show).
std::string youtube_ref_from_web_url(std::string_view url);

struct YtVideo {
  std::string id, title, channel, views, age, duration;
};

struct YtListing {
  std::string title, handle, channel_id, description;
  std::string subscribers, video_count;
  std::vector<YtVideo> videos;
  std::string next;  // continuation token for the following chunk, or ""
};

struct YtComment {
  std::string id, author, channel_id, text, published, likes;
  int replies = 0;
};

struct YtComments {
  std::vector<YtComment> comments;
  std::string total;     // "1,234" when the header says so
  std::string next;      // continuation token for the following chunk
};

struct YtVideoInfo {
  std::string id, title, owner, owner_id, views, date, description;
  std::string comments_top, comments_new;  // continuation tokens
};

// A browse reply: channel / playlist header (first chunk only) and the video
// items of the chunk. A continuation reply carries items only.
YtListing yt_parse_browse(const Json& reply);
// A `next` reply for a video page.
YtVideoInfo yt_parse_video(const Json& reply, std::string_view video_id);
// A comment continuation reply (header chunk, thread chunk or both).
YtComments yt_parse_comments(const Json& reply);
// First `navigation/resolve_url` browse id ("" when unknown).
std::string yt_parse_resolved_id(const Json& reply);

Document yt_listing_document(const YtListing& l, const YtRef& ref,
                             bool has_prev);
Document yt_video_document(const YtVideoInfo& v, const YtComments& c,
                           const YtRef& ref);

#if defined(XANATOO_YOUTUBE) && XANATOO_YOUTUBE
std::optional<Document> youtube_fetch(std::string_view ref,
                                      std::string* err = nullptr);
#endif

}  // namespace xanatoo
