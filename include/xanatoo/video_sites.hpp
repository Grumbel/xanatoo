// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Read-only clients for more video platforms, next to youtube.hpp. Same shape:
// listings (channel / account / search / recent), video pages with comments and
// "Watch with mpv" links. Public JSON APIs only; no account, no posting.
// See docs/VIDEO_SITES.md.
//
//   peertube:HOST[?page=N]                    recent videos of an instance
//   peertube:HOST/c/NAME[?page=N]             channel (also /a/NAME for accounts)
//   peertube:HOST/w/ID[?comments=N][&thread=ID]   video, comments / one thread
//   peertube:HOST/search?search=Q[&page=N]    search (sepiasearch.org spans the fediverse)
//   odysee:@chan:8[?page=N]                   channel (lbry name with short claim id)
//   odysee:@chan:8/slug:e[?comments=N][&thread=ID]   video, comments / replies
//   odysee:search?s=Q[&page=N]                search
//   dailymotion:xID[?comments=N]              video
//   dailymotion:user/xID[?page=N]             channel uploads
//   dailymotion:search?search=Q[&page=N]      search
//
// Parsers are pure (JSON text in, structs / Document out) and tested with cut
// down recorded replies; only video_site_fetch touches the network.

#include "xanatoo/json_dom.hpp"
#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

enum class VideoSite { PeerTube, Odysee, Dailymotion };

struct VsRef {
  VideoSite site = VideoSite::PeerTube;
  enum class Kind { Home, Channel, Account, Video, Search } kind = Kind::Home;
  std::string host;  // PeerTube instance host
  // Channel / Account / Video: the site's id (PeerTube name or uuid, Odysee
  // "@chan:8" or "@chan:8/slug:e", Dailymotion xID or user id).
  // Search: the query in search_encode form.
  std::string id;
  int page = 1;         // listing page, or comment page of a video
  std::string thread;   // comment thread / parent comment id (replies page)
};

std::optional<VsRef> parse_video_site_ref(std::string_view ref);
bool is_video_site_ref(std::string_view ref);
std::string video_site_ref_string(const VsRef& r);
const char* video_site_name(VideoSite s);
// Equivalent web address ("" when there is none).
std::string video_site_web_url(const VsRef& r);
// Web page URL of a supported site → ref string ("" when not one we show).
std::string video_site_ref_from_web_url(std::string_view url);

struct VsVideo {
  std::string id;    // the site's own id (Odysee: claim id)
  std::string ref;   // native ref of the video page
  std::string title, channel, channel_ref, views, age, duration, thumb;
};

struct VsListing {
  std::string title, subtitle, description;
  std::vector<VsVideo> videos;
  bool has_next = false;
};

struct VsComment {
  std::string id, author, author_ref, text, published, likes;
  int replies = 0;
};

struct VsVideoInfo {
  std::string title, owner, owner_ref, views, date, likes, duration;
  std::string description, thumb, web_url, play_url;
  int comment_count = 0;
};

struct VsComments {
  std::vector<VsComment> comments;
  std::string total;
  bool has_next = false;
};

// ---- PeerTube (REST, /api/v1) ----
// A `{total, data:[video…]}` list; videos link to the instance they live on.
VsListing pt_parse_videos(const Json& reply, std::string_view host, int page,
                          int per_page);
VsVideoInfo pt_parse_video(const Json& reply, std::string_view host);
// Header of a video-channels/NAME or accounts/NAME reply into `l`.
void pt_parse_channel_header(const Json& reply, VsListing& l);
// `description` reply of /videos/ID/description.
std::string pt_parse_description(const Json& reply);
// comment-threads (`{total,data}`) or a thread (`{comment, children:[…]}`).
VsComments pt_parse_comments(const Json& reply, std::string_view host,
                             const VsRef& video, int page, int per_page);

// ---- Odysee (LBRY proxy + comment server) ----
VsListing od_parse_claims(const Json& reply, int page);
VsVideoInfo od_parse_claim(const Json& claim);
// Claim ids of a lighthouse search reply (an array of {claimId}).
std::vector<std::string> od_parse_search_ids(const Json& reply);
VsComments od_parse_comments(const Json& reply, int page);
// `result.items` of a claim_search reply, or the claim of a `resolve` reply.
const Json* od_first_claim(const Json& reply, std::string_view lbry_url);
// Header of a resolved channel claim into `l`.
void od_parse_channel_header(const Json& claim, VsListing& l);
// "@chan:8/slug:e" (possibly percent-encoded) → "lbry://@chan#8/slug#e".
std::string od_lbry_url(std::string_view id);

// ---- Dailymotion (public REST) ----
VsListing dm_parse_videos(const Json& reply, int page);
VsVideoInfo dm_parse_video(const Json& reply);
void dm_parse_channel_header(const Json& reply, VsListing& l);
VsComments dm_parse_comments(const Json& reply, int page);

// ---- documents ----
Document vs_listing_document(const VsListing& l, const VsRef& ref);
Document vs_video_document(const VsVideoInfo& v, const VsComments& c,
                           const VsRef& ref);

#if defined(XANATOO_VIDEO) && XANATOO_VIDEO
std::optional<Document> video_site_fetch(std::string_view ref,
                                         std::string* err = nullptr);
#endif

}  // namespace xanatoo
