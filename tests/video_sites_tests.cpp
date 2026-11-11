// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Offline tests for the PeerTube / Odysee / Dailymotion parsers: replies are
// cut-down copies of what the real APIs send.
#include "xanatoo/video_sites.hpp"

#include <cassert>
#include <string>
#include <string_view>

using namespace xanatoo;

static Json parse(std::string_view text) {
  auto j = json_parse(text);
  assert(j);
  return *j;
}

static std::string all_text(const Document& d) {
  std::string out;
  for (const Node& n : d.nodes) {
    for (const InlineSpan& s : n.spans) out += s.text + "|";
    out += n.src + "|" + n.summary + "|";
  }
  return out;
}

static bool has_href(const Document& d, std::string_view href) {
  for (const Node& n : d.nodes)
    for (const InlineSpan& s : n.spans)
      if (s.href == href) return true;
  return false;
}

static bool has(const std::string& s, std::string_view part) {
  return s.find(part) != std::string::npos;
}

static void test_refs() {
  auto r = parse_video_site_ref("peertube:video.blender.org/c/blender_channel?page=2");
  assert(r && r->site == VideoSite::PeerTube && r->kind == VsRef::Kind::Channel &&
         r->host == "video.blender.org" && r->id == "blender_channel" && r->page == 2);
  assert(video_site_ref_string(*r) == "peertube:video.blender.org/c/blender_channel?page=2");
  assert(video_site_web_url(*r) == "https://video.blender.org/c/blender_channel");
  r = parse_video_site_ref("peertube:framatube.org/w/9c9de5e8-0a1e?comments=3&thread=12");
  assert(r && r->kind == VsRef::Kind::Video && r->page == 3 && r->thread == "12");
  assert(video_site_ref_string(*r) == "peertube:framatube.org/w/9c9de5e8-0a1e?comments=3&thread=12");
  r = parse_video_site_ref("peertube:sepiasearch.org/search?search=free+software&page=2");
  assert(r && r->kind == VsRef::Kind::Search && r->id == "free+software");
  assert(video_site_ref_string(*r) == "peertube:sepiasearch.org/search?search=free+software&page=2");
  r = parse_video_site_ref("peertube:peertube.tv");
  assert(r && r->kind == VsRef::Kind::Home && video_site_ref_string(*r) == "peertube:peertube.tv");
  assert(!parse_video_site_ref("peertube:nohost/w/x"));
  assert(!parse_video_site_ref("peertube:a.b/bogus/x"));

  r = parse_video_site_ref("odysee:@Odysee:8");
  assert(r && r->site == VideoSite::Odysee && r->kind == VsRef::Kind::Channel);
  assert(video_site_web_url(*r) == "https://odysee.com/@Odysee:8");
  r = parse_video_site_ref("odysee:@Odysee:8/channel-roundup:e?comments=2");
  assert(r && r->kind == VsRef::Kind::Video && r->page == 2);
  assert(video_site_ref_string(*r) == "odysee:@Odysee:8/channel-roundup:e?comments=2");
  assert(od_lbry_url(r->id) == "lbry://@Odysee#8/channel-roundup#e");
  assert(od_lbry_url("@a:1/caf%C3%A9:b") == "lbry://@a#1/caf\xC3\xA9#b");
  r = parse_video_site_ref("odysee:search?s=blender");
  assert(r && r->kind == VsRef::Kind::Search && r->id == "blender");
  assert(video_site_ref_string(*r) == "odysee:search?s=blender");

  r = parse_video_site_ref("dailymotion:x6gnvwh");
  assert(r && r->kind == VsRef::Kind::Video);
  assert(video_site_web_url(*r) == "https://www.dailymotion.com/video/x6gnvwh");
  r = parse_video_site_ref("dailymotion:user/x241t0t?page=3");
  assert(r && r->kind == VsRef::Kind::Channel && r->page == 3);
  r = parse_video_site_ref("dailymotion:search?search=a+b");
  assert(r && r->kind == VsRef::Kind::Search && r->id == "a+b");
  assert(!parse_video_site_ref("youtube:@x"));

  assert(video_site_ref_from_web_url("https://odysee.com/@Odysee:8/x-y:e?r=abc") ==
         "odysee:@Odysee:8/x-y:e");
  assert(video_site_ref_from_web_url("https://odysee.com/@Odysee:8") == "odysee:@Odysee:8");
  assert(video_site_ref_from_web_url("https://www.dailymotion.com/video/x6gnvwh") ==
         "dailymotion:x6gnvwh");
  assert(video_site_ref_from_web_url("https://dai.ly/x6gnvwh") == "dailymotion:x6gnvwh");
  assert(video_site_ref_from_web_url("https://framatube.org/w/vFLb8p18NUT94kekgu7aUW") ==
         "peertube:framatube.org/w/vFLb8p18NUT94kekgu7aUW");
  assert(video_site_ref_from_web_url("https://en.wikipedia.org/w/index.php?title=X").empty());
  assert(video_site_ref_from_web_url("https://framatube.org/c/chan").empty());
  assert(video_site_ref_from_web_url("https://framatube.org/video-channels/chan") ==
         "peertube:framatube.org/c/chan");
  assert(video_site_ref_from_web_url("https://framatube.org/videos/watch/9c9de5e8-0a1e-484a-b099-e80766180a6d?start=3") ==
         "peertube:framatube.org/w/9c9de5e8-0a1e-484a-b099-e80766180a6d");
  assert(video_site_ref_from_web_url("https://example.com/about").empty());
}

static void test_peertube() {
  Json list = parse(R"({"total":45,"data":[{"uuid":"b292","url":"https://video.blender.org/videos/watch/b292",
    "name":"OVERGROWN","duration":3725,"views":12770,"publishedAt":"2026-07-10T14:00:16.560Z",
    "thumbnailUrl":"https://video.blender.org/lazy-static/t.jpg",
    "channel":{"name":"blender_channel","displayName":"Blender","host":"video.blender.org"},
    "account":{"name":"blender","displayName":"B","host":"video.blender.org"}}]})");
  VsListing l = pt_parse_videos(list, "peertube.tv", 1, 20);
  assert(l.has_next && l.videos.size() == 1);
  const VsVideo& v = l.videos[0];
  assert(v.ref == "peertube:video.blender.org/w/b292");  // the host the video lives on
  assert(v.duration == "1:02:05" && v.views == "12,770 views" && v.age == "2026-07-10");
  assert(v.channel == "Blender" && v.channel_ref == "peertube:video.blender.org/c/blender_channel");
  assert(!pt_parse_videos(list, "peertube.tv", 3, 20).has_next);

  VsRef ref;
  ref.host = "peertube.tv";
  ref.kind = VsRef::Kind::Channel;
  ref.id = "blender_channel";
  Document d = vs_listing_document(l, ref);
  const std::string t = all_text(d);
  assert(has(t, "OVERGROWN") && has(t, "Blender"));
  assert(has_href(d, "peertube:video.blender.org/w/b292"));
  assert(has_href(d, "peertube:peertube.tv/c/blender_channel?page=2"));  // next page

  VsListing head;
  pt_parse_channel_header(parse(R"({"name":"c1","host":"x.org","displayName":"Chan","followersCount":3,"description":"About"})"), head);
  assert(head.title == "Chan" && has(head.subtitle, "c1@x.org") && has(head.subtitle, "3 followers"));

  Json video = parse(R"({"uuid":"u1","name":"Title","url":"https://a.org/videos/watch/u1","views":1,"likes":4,
    "publishedAt":"2026-01-02T00:00:00Z","duration":65,"comments":2,"thumbnailPath":"/t.jpg",
    "channel":{"name":"c","displayName":"Chan","host":"a.org"}})");
  VsVideoInfo info = pt_parse_video(video, "b.org");
  assert(info.title == "Title" && info.views == "1 view" && info.duration == "1:05");
  assert(info.web_url == "https://a.org/videos/watch/u1" && info.owner_ref == "peertube:a.org/c/c");
  assert(info.thumb == "https://b.org/t.jpg" && info.comment_count == 2);
  assert(pt_parse_description(parse(R"({"description":"Full\r\ntext"})")) == "Full\ntext");

  Json threads = parse(R"({"total":30,"data":[
    {"id":7,"text":"<p>Hello <b>you</b></p>","createdAt":"2026-06-24T17:23:41.000Z","totalReplies":2,
     "account":{"name":"ann","displayName":"Ann","host":"mastodon.social"}},
    {"id":8,"text":"x","isDeleted":true,"createdAt":"2026-06-25T00:00:00Z","totalReplies":0}]})");
  VsRef vref;
  vref.host = "peertube.tv";
  vref.kind = VsRef::Kind::Video;
  vref.id = "u1";
  VsComments c = pt_parse_comments(threads, "peertube.tv", vref, 1, 20);
  assert(c.comments.size() == 2 && c.has_next && c.total == "30");
  assert(c.comments[0].author == "Ann" && has(c.comments[0].text, "Hello") && !has(c.comments[0].text, "<p>"));
  assert(c.comments[0].author_ref == "peertube:peertube.tv/a/ann@mastodon.social");
  assert(c.comments[0].replies == 2 && c.comments[1].text == "[deleted]");
  Document vd = vs_video_document(info, c, vref);
  const std::string vt = all_text(vd);
  assert(has(vt, "Watch with mpv") && has(vt, "Comments (30)") && has(vt, "2 replies"));
  assert(has_href(vd, "external:mpv:https://a.org/videos/watch/u1"));
  assert(has_href(vd, "peertube:peertube.tv/w/u1?thread=7"));
  assert(has_href(vd, "peertube:peertube.tv/w/u1?comments=2"));

  Json thread = parse(R"({"comment":{"id":7,"text":"root","createdAt":"2026-06-24T00:00:00Z","account":{"name":"a","host":"x.org"}},
    "children":[{"comment":{"id":9,"text":"kid","createdAt":"2026-06-24T00:00:00Z"},"children":[
      {"comment":{"id":10,"text":"grandkid","createdAt":"2026-06-24T00:00:00Z"},"children":[]}]}]})");
  VsComments tc = pt_parse_comments(thread, "peertube.tv", vref, 1, 20);
  assert(tc.comments.size() == 3 && tc.comments[2].text == "grandkid" && !tc.has_next);
}

static void test_odysee() {
  Json claims = parse(R"({"result":{"total_items":30,"total_pages":2,"items":[
    {"claim_id":"ee58","canonical_url":"lbry://@Odysee#8/channel-roundup-may-2026#e","value_type":"stream",
     "name":"channel-roundup-may-2026","timestamp":1780057821,
     "signing_channel":{"name":"@Odysee","canonical_url":"lbry://@Odysee#8","value":{"title":"Odysee"}},
     "value":{"title":"Channel Roundup","release_time":"1780063201","description":"Hi\n\nthere",
              "video":{"duration":413},"thumbnail":{"url":"https://thumbs.odycdn.com/a.webp"}}},
    {"claim_id":"c1","canonical_url":"lbry://@Odysee#8","value_type":"channel","name":"@Odysee"}]}})");
  VsListing l = od_parse_claims(claims, 1);
  assert(l.has_next && l.videos.size() == 1);  // the channel claim is not a video
  const VsVideo& v = l.videos[0];
  assert(v.ref == "odysee:@Odysee:8/channel-roundup-may-2026:e" && v.id == "ee58");
  assert(v.title == "Channel Roundup" && v.duration == "6:53" && v.age == "2026-05-29");
  assert(v.channel == "Odysee" && v.channel_ref == "odysee:@Odysee:8");
  assert(!od_parse_claims(claims, 2).has_next);

  const Json* first = od_first_claim(claims, "");
  assert(first);
  VsVideoInfo info = od_parse_claim(*first);
  assert(info.title == "Channel Roundup" && info.owner == "Odysee");
  assert(info.web_url == "https://odysee.com/@Odysee:8/channel-roundup-may-2026:e");
  assert(info.description == "Hi\n\nthere");

  Json resolved = parse(R"({"result":{"lbry://@Odysee#8":{"claim_id":"c1","name":"@Odysee","value":{"title":"Odysee","description":"D"}},
                            "lbry://nope":{"error":{"name":"NOT_FOUND"}}}})");
  assert(od_first_claim(resolved, "lbry://@Odysee#8"));
  assert(!od_first_claim(resolved, "lbry://nope"));
  VsListing head;
  od_parse_channel_header(*od_first_claim(resolved, "lbry://@Odysee#8"), head);
  assert(head.title == "Odysee" && head.description == "D");

  Json ids = parse(R"([{"claimId":"a1","name":"x"},{"claimId":"b2"}])");
  auto got = od_parse_search_ids(ids);
  assert(got.size() == 2 && got[0] == "a1" && got[1] == "b2");

  Json comments = parse(R"({"result":{"total_items":183,"total_pages":10,"items":[
    {"comment":"Enjoy","comment_id":"ac9d","timestamp":1780063253,"channel_name":"@Odysee",
     "channel_url":"lbry://@Odysee#80d2","replies":5},
    {"comment":"no reply key","comment_id":"15","timestamp":1780063253,"channel_name":"@x","channel_url":"lbry://@x#1"}]}})");
  VsComments c = od_parse_comments(comments, 1);
  assert(c.comments.size() == 2 && c.has_next && c.total == "183");
  assert(c.comments[0].author_ref == "odysee:@Odysee:80d2" && c.comments[0].replies == 5);
  assert(c.comments[1].replies == 0);
  VsRef ref = *parse_video_site_ref("odysee:@Odysee:8/channel-roundup-may-2026:e");
  Document d = vs_video_document(info, c, ref);
  assert(has_href(d, "odysee:@Odysee:8/channel-roundup-may-2026:e?thread=ac9d"));
  assert(has_href(d, "external:mpv:https://odysee.com/@Odysee:8/channel-roundup-may-2026:e"));
  ref.thread = "ac9d";
  Document rd = vs_video_document(info, c, ref);
  assert(has(all_text(rd), "Replies") && has_href(rd, "odysee:@Odysee:8/channel-roundup-may-2026:e"));
}

static void test_dailymotion() {
  Json list = parse(R"({"page":1,"has_more":true,"list":[{"id":"x6gnvwh","title":"Curvas",
    "owner.screenname":"Alberto","owner.id":"x241t0t","views_total":4,"duration":571,
    "created_time":1521670205,"thumbnail_240_url":"https://s1.dmcdn.net/v/x240"}]})");
  VsListing l = dm_parse_videos(list, 1);
  assert(l.has_next && l.videos.size() == 1);
  assert(l.videos[0].ref == "dailymotion:x6gnvwh" && l.videos[0].duration == "9:31");
  assert(l.videos[0].age == "2018-03-21" && l.videos[0].channel_ref == "dailymotion:user/x241t0t");
  VsRef ref = *parse_video_site_ref("dailymotion:search?search=blender");
  Document d = vs_listing_document(l, ref);
  assert(has_href(d, "dailymotion:x6gnvwh") && has_href(d, "dailymotion:search?search=blender&page=2"));

  VsVideoInfo info = dm_parse_video(parse(R"({"id":"x6gnvwh","title":"T","owner.screenname":"Al","owner.id":"x2",
    "views_total":1234,"likes_total":2,"duration":60,"created_time":1521670205,
    "description":"a<br />b","url":"https://www.dailymotion.com/video/x6gnvwh","comments_total":1})"));
  assert(info.views == "1,234 views" && !has(info.description, "<") && has(info.description, "a") &&
         has(info.description, "b") && info.comment_count == 1);
  VsComments c = dm_parse_comments(parse(R"({"has_more":false,"list":[{"id":"c1","message":"nice",
    "owner.screenname":"Bo","owner.id":"x3","created_time":1521670205,"replies_total":1}]})"), 1);
  assert(c.comments.size() == 1 && c.comments[0].author == "Bo");
  Document vd = vs_video_document(info, c, *parse_video_site_ref("dailymotion:x6gnvwh"));
  assert(has(all_text(vd), "nice") && has(all_text(vd), "1 reply") &&
         has_href(vd, "external:mpv:https://www.dailymotion.com/video/x6gnvwh"));
}

int main() {
  test_refs();
  test_peertube();
  test_odysee();
  test_dailymotion();
  return 0;
}
