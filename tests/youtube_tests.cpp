// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Offline tests for the YouTube (InnerTube) parsers: replies are cut-down
// copies of what youtubei/v1 sends.
#include "xanatoo/youtube.hpp"

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

static void test_json() {
  Json j = parse(R"({"a":[1,{"b":"xé\n"},true,null],"c":{"b":"deep"}, "n":-2.5e1})");
  assert(json_string(json_path(j, "a/1/b")) == "x\xC3\xA9\n");
  assert(json_string(json_find(j, "b")) == "x\xC3\xA9\n");  // first in order
  assert(json_number(json_get(j, "n")) == -25);
  assert(json_path(j, "a/9") == nullptr);
  std::vector<const Json*> all;
  json_find_all(j, "b", all);
  assert(all.size() == 2);
  assert(!json_parse("{\"a\":"));
  assert(!json_parse("[1,]"));
}

static void test_refs() {
  auto r = parse_youtube_ref("youtube:@GoogleDevelopers?page=3");
  assert(r && r->kind == YtRef::Kind::Named && r->id == "@GoogleDevelopers" &&
         r->page == 3);
  assert(youtube_ref_string(*r) == "youtube:@GoogleDevelopers?page=3");
  r = parse_youtube_ref("youtube:watch?v=rsOiZE19RVo&comments=2&sort=new");
  assert(r && r->kind == YtRef::Kind::Video && r->page == 2 && r->newest);
  assert(youtube_ref_string(*r) == "youtube:watch?v=rsOiZE19RVo&comments=2&sort=new");
  r = parse_youtube_ref("youtube:playlist?list=PLabc_-1&page=2");
  assert(r && r->kind == YtRef::Kind::Playlist && r->id == "PLabc_-1");
  assert(youtube_ref_string(*r) == "youtube:playlist?list=PLabc_-1&page=2");
  assert(parse_youtube_ref("youtube:channel/UC_x5XG1OV2P6uZZ5FSM9Ttw"));
  assert(!parse_youtube_ref("youtube:channel/xyz"));
  r = parse_youtube_ref("youtube:results?search_query=rust+lang%C3%BC&page=2");
  assert(r && r->kind == YtRef::Kind::Search && r->page == 2);
  assert(youtube_ref_string(*r) == "youtube:results?search_query=rust+lang%C3%BC&page=2");
  assert(parse_youtube_ref("youtube:results?search_query=a%20b")->id == "a+b");
  assert(!parse_youtube_ref("youtube:results?search_query="));
  assert(youtube_ref_from_web_url("https://www.youtube.com/results?search_query=a+b") ==
         "youtube:results?search_query=a+b");
  assert(!parse_youtube_ref("youtube:watch?v="));
  assert(!parse_youtube_ref("youtube:watch?v=a\"b"));
  assert(!parse_youtube_ref("youtube:"));
  assert(!parse_youtube_ref("https://www.youtube.com/watch?v=rsOiZE19RVo"));
  assert(parse_youtube_ref("youtube:@x?page=999999")->page == 1);

  assert(youtube_ref_from_web_url("https://www.youtube.com/watch?v=rsOiZE19RVo&t=5s#x") ==
         "youtube:watch?v=rsOiZE19RVo");
  assert(youtube_ref_from_web_url("https://youtu.be/rsOiZE19RVo?si=1") ==
         "youtube:watch?v=rsOiZE19RVo");
  assert(youtube_ref_from_web_url("https://m.youtube.com/shorts/rsOiZE19RVo") ==
         "youtube:watch?v=rsOiZE19RVo");
  assert(youtube_ref_from_web_url("https://www.youtube.com/@GoogleDevelopers/videos") ==
         "youtube:@GoogleDevelopers");
  assert(youtube_ref_from_web_url("https://www.youtube.com/channel/UCabc/featured") ==
         "youtube:channel/UCabc");
  assert(youtube_ref_from_web_url("https://www.youtube.com/c/Name") == "youtube:c/Name");
  assert(youtube_ref_from_web_url("https://www.youtube.com/playlist?list=PL1") ==
         "youtube:playlist?list=PL1");
  assert(youtube_ref_from_web_url("https://www.youtube.com/feed/subscriptions").empty());
  assert(youtube_ref_from_web_url("https://example.com/watch?v=rsOiZE19RVo").empty());
  assert(youtube_web_url(*parse_youtube_ref("youtube:@x")) == "https://www.youtube.com/@x");
}

static const char* kChannel = R"({
 "metadata":{"channelMetadataRenderer":{"title":"Dev Channel","description":"About us.\n\nMore.",
   "externalId":"UCabc","ownerUrls":["http://www.youtube.com/@DevChan"]}},
 "header":{"pageHeaderRenderer":{"content":{"pageHeaderViewModel":{"metadata":{"contentMetadataViewModel":{"metadataRows":[
   {"metadataParts":[{"text":{"content":"@DevChan"}}]},
   {"metadataParts":[{"text":{"content":"2.68M subscribers"}},{"text":{"content":"6.1K videos"}}]}]}}}}}},
 "contents":{"twoColumnBrowseResultsRenderer":{"tabs":[{"tabRenderer":{"selected":true,"content":{"richGridRenderer":{"contents":[
  {"richItemRenderer":{"content":{"lockupViewModel":{"contentId":"vid00000001","contentType":"LOCKUP_CONTENT_TYPE_VIDEO",
    "contentImage":{"thumbnailViewModel":{"overlays":[{"thumbnailBottomOverlayViewModel":{"badges":[{"thumbnailBadgeViewModel":{"text":"2:35"}}]}}]}},
    "metadata":{"lockupMetadataViewModel":{"title":{"content":"First video"},"metadata":{"contentMetadataViewModel":{"metadataRows":[
      {"metadataParts":[{"text":{"content":"Dev Channel"}}]},
      {"metadataParts":[{"text":{"content":"14K views"}},{"text":{"content":"12 days ago"}}]}]}}}}}}}},
  {"richItemRenderer":{"content":{"lockupViewModel":{"contentId":"PLplaylist","contentType":"LOCKUP_CONTENT_TYPE_PLAYLIST","metadata":{}}}}},
  {"richItemRenderer":{"content":{"videoRenderer":{"videoId":"vid00000002","title":{"runs":[{"text":"Second "},{"text":"video"}]},
    "lengthText":{"simpleText":"10:00"},"viewCountText":{"simpleText":"5 views"},"publishedTimeText":{"simpleText":"1 year ago"}}}}},
  {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"TOKEN-1"}}}}]}}}}]}}
})";

static void test_listing() {
  YtListing l = yt_parse_browse(parse(kChannel));
  assert(l.title == "Dev Channel" && l.channel_id == "UCabc" && l.handle == "@DevChan");
  assert(l.subscribers == "2.68M subscribers" && l.video_count == "6.1K videos");
  assert(l.next == "TOKEN-1");
  assert(l.videos.size() == 2);  // the playlist lockup is not a video
  assert(l.videos[0].id == "vid00000001" && l.videos[0].title == "First video");
  assert(l.videos[0].views == "14K views" && l.videos[0].age == "12 days ago");
  assert(l.videos[0].duration == "2:35" && l.videos[0].channel == "Dev Channel");
  assert(l.videos[1].title == "Second video" && l.videos[1].duration == "10:00");

  YtRef ref = *parse_youtube_ref("youtube:@DevChan");
  Document d = yt_listing_document(l, ref, false);
  const std::string t = all_text(d);
  assert(t.find("Dev Channel") != std::string::npos);
  assert(has_href(d, "youtube:watch?v=vid00000001"));
  assert(has_href(d, "youtube:@DevChan?page=2"));   // Older
  assert(!has_href(d, "youtube:@DevChan"));          // no Newer on page 1
  YtRef p2 = ref;
  p2.page = 2;
  l.next.clear();
  d = yt_listing_document(l, p2, true);
  assert(has_href(d, "youtube:@DevChan"));           // Newer is page 1
  assert(!has_href(d, "youtube:@DevChan?page=3"));
}

static void test_video() {
  Json next = parse(R"({"contents":{"twoColumnWatchNextResults":{"results":{"results":{"contents":[
   {"videoPrimaryInfoRenderer":{"title":{"runs":[{"text":"A talk"}]},
     "viewCount":{"videoViewCountRenderer":{"viewCount":{"simpleText":"14,115 views"}}},
     "dateText":{"simpleText":"Oct 29, 2026"}}},
   {"videoSecondaryInfoRenderer":{"owner":{"videoOwnerRenderer":{"title":{"runs":[{"text":"Dev Channel"}]},
     "navigationEndpoint":{"browseEndpoint":{"browseId":"UCabc"}}}},
     "attributedDescription":{"content":"Line one.\n\nLine two."}}},
   {"itemSectionRenderer":{"sectionIdentifier":"comment-item-section","contents":[
     {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"CTOP"}}}}]}}]}}}},
   "engagementPanels":[{"engagementPanelSectionListRenderer":{"header":{"engagementPanelTitleHeaderRenderer":{"menu":{
     "sortFilterSubMenuRenderer":{"subMenuItems":[
       {"serviceEndpoint":{"continuationCommand":{"token":"SORT-TOP"}}},
       {"serviceEndpoint":{"continuationCommand":{"token":"SORT-NEW"}}}]}}}}}}]})");
  YtVideoInfo v = yt_parse_video(next, "rsOiZE19RVo");
  assert(v.title == "A talk" && v.owner == "Dev Channel" && v.owner_id == "UCabc");
  assert(v.views == "14,115 views" && v.date == "Oct 29, 2026");
  assert(v.description == "Line one.\n\nLine two.");
  assert(v.comments_top == "SORT-TOP" && v.comments_new == "SORT-NEW");

  // Without the sort menu the comment section's token is the top token.
  Json bare = parse(R"({"x":{"itemSectionRenderer":{"sectionIdentifier":"comment-item-section","contents":[
    {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"CTOP"}}}}]}}})");
  assert(yt_parse_video(bare, "x").comments_top == "CTOP");

  Json cj = parse(R"({
   "onResponseReceivedEndpoints":[
    {"reloadContinuationItemsCommand":{"continuationItems":[{"commentsHeaderRenderer":{"countText":{"runs":[{"text":"1,234"},{"text":" Comments"}]}}}]}},
    {"reloadContinuationItemsCommand":{"continuationItems":[
      {"commentThreadRenderer":{"commentViewModel":{"commentViewModel":{"commentKey":"K1"}},
        "replies":{"commentRepliesRenderer":{"contents":[{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"REPLY-TOKEN"}}}}]}}}},
      {"commentThreadRenderer":{"commentViewModel":{"commentViewModel":{"commentKey":"K2"}}}},
      {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"NEXT-PAGE"}}}}]}}],
   "frameworkUpdates":{"entityBatchUpdate":{"mutations":[
    {"payload":{"commentSharedEntityPayload":{"key":"zz"}}},
    {"payload":{"commentEntityPayload":{"key":"K2","properties":{"commentId":"c2","content":{"content":"Second"},"publishedTime":"1 day ago"},
      "author":{"displayName":"@bob","channelId":"UCbob"},"toolbar":{"likeCountNotliked":"","replyCount":""}}}},
    {"payload":{"commentEntityPayload":{"key":"K1","properties":{"commentId":"c1","content":{"content":"First\nline two"},"publishedTime":"2 days ago"},
      "author":{"displayName":"@alice","channelId":"UCalice"},"toolbar":{"likeCountNotliked":"5","replyCount":"3"}}}}]}}})");
  YtComments c = yt_parse_comments(cj);
  assert(c.total == "1,234");
  assert(c.next == "NEXT-PAGE");  // not the reply token
  assert(c.comments.size() == 2);
  assert(c.comments[0].id == "c1" && c.comments[0].author == "@alice");
  assert(c.comments[0].text == "First\nline two" && c.comments[0].likes == "5");
  assert(c.comments[0].replies == 3 && c.comments[1].replies == 0);
  assert(c.comments[1].channel_id == "UCbob");

  YtRef ref = *parse_youtube_ref("youtube:watch?v=rsOiZE19RVo");
  Document d = yt_video_document(v, c, ref);
  const std::string t = all_text(d);
  assert(t.find("A talk") != std::string::npos);
  assert(t.find("@alice|2 days ago") != std::string::npos);
  assert(t.find("3 replies") != std::string::npos);
  assert(c.comments[0].reply_token == "REPLY-TOKEN");
  assert(has_href(d, "youtube:watch?v=rsOiZE19RVo&replies=REPLY-TOKEN"));
  assert(has_href(d, "youtube:channel/UCabc"));
  assert(has_href(d, "youtube:channel/UCalice"));
  assert(has_href(d, "youtube:watch?v=rsOiZE19RVo&comments=2"));
  assert(has_href(d, "youtube:watch?v=rsOiZE19RVo&sort=new"));
  assert(has_href(d, "external:https://www.youtube.com/watch?v=rsOiZE19RVo"));

  // Reply continuation: bare comment view models, no thread wrapper.
  Json rj = parse(R"({"onResponseReceivedEndpoints":[{"appendContinuationItemsAction":{"continuationItems":[
      {"commentViewModel":{"commentViewModel":{"commentKey":"R1"}}},
      {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"MORE-REPLIES"}}}}]}}],
    "frameworkUpdates":{"entityBatchUpdate":{"mutations":[
    {"payload":{"commentEntityPayload":{"key":"R1","properties":{"commentId":"c1.r1","content":{"content":"A reply"},"publishedTime":"1 hour ago"},
      "author":{"displayName":"@carol","channelId":"UCcarol"},"toolbar":{"likeCountNotliked":"","replyCount":""}}}}]}}})");
  YtComments rc = yt_parse_comments(rj);
  assert(rc.comments.size() == 1 && rc.comments[0].text == "A reply");
  assert(rc.next == "MORE-REPLIES");
  YtRef rref = *parse_youtube_ref("youtube:watch?v=rsOiZE19RVo&replies=REPLY-TOKEN%3D");
  assert(rref.replies == "REPLY-TOKEN%3D");
  assert(youtube_ref_string(rref) == "youtube:watch?v=rsOiZE19RVo&replies=REPLY-TOKEN%3D");
  Document rd = yt_video_document(v, rc, rref);
  assert(all_text(rd).find("A reply") != std::string::npos);
  assert(has_href(rd, "youtube:watch?v=rsOiZE19RVo"));

  assert(yt_parse_resolved_id(parse(R"({"endpoint":{"browseEndpoint":{"browseId":"UCxyz"}}})")) == "UCxyz");
}

int main() {
  test_json();
  test_refs();
  test_listing();
  test_video();
}
