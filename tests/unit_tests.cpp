// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Lightweight unit tests (no Catch/GTest). Run: ./xanatoo_tests

#include "xanatoo/gemini.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/util.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

int g_failed = 0;
int g_passed = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
  if (cond) {
    ++g_passed;
    return;
  }
  ++g_failed;
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}

#define EXPECT(c) expect(!!(c), #c, __FILE__, __LINE__)
#define EXPECT_EQ(a, b) expect((a) == (b), #a " == " #b, __FILE__, __LINE__)

void test_string_helpers() {
  using namespace xanatoo;
  EXPECT(starts_with("hello", "he"));
  EXPECT(!starts_with("hello", "HE"));
  EXPECT(starts_with_ci("Hello", "he"));
  EXPECT(starts_with_ci("HELLO", "hel"));
  EXPECT(!starts_with_ci("hello", "hello!"));

  EXPECT(ends_with("file.md", ".md"));
  EXPECT(!ends_with("file.md", ".MD"));

  EXPECT_EQ(ascii_lower(std::string("AbC")), "abc");
  EXPECT_EQ(ascii_lower(std::string_view("XyZ")), "xyz");

  std::string_view t = "  hi\t";
  trim_ascii(t);
  EXPECT_EQ(std::string(t), "hi");

  std::string_view empty = "   ";
  trim_ascii(empty);
  EXPECT(empty.empty());
}

void test_json_min() {
  using namespace xanatoo;
  const char* j =
      R"({"name":"Ada","score":42,"ok":true,"nested":{"id":7},"list":[{"a":"1"},{"a":"2"}]})";

  EXPECT_EQ(json_str_field(j, "name"), "Ada");
  EXPECT_EQ(json_int_field(j, "score"), 42);
  EXPECT(json_bool_field(j, "ok"));
  EXPECT(!json_bool_field(j, "missing"));

  auto nest = json_object_after_key(j, "nested");
  EXPECT(!nest.empty());
  EXPECT_EQ(json_int_field(nest, "id"), 7);

  auto arr = json_object_array_after_key(j, "list");
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_EQ(json_str_field(arr[0], "a"), "1");
  EXPECT_EQ(json_str_field(arr[1], "a"), "2");

  EXPECT_EQ(json_unescape(R"(line\nand\t\"q\")"), "line\nand\t\"q\"");
}

void test_reddit_urls() {
  using namespace xanatoo;
  EXPECT(is_reddit_url("https://www.reddit.com/r/linux"));
  EXPECT(is_reddit_url("https://old.reddit.com/r/linux/"));
  EXPECT(is_reddit_url("https://reddit.com/r/GameDeals/.json"));
  EXPECT(is_reddit_url("http://www.reddit.com/r/foo/comments/abc123/title/"));
  EXPECT(is_reddit_url("reddit.com/r/linux"));
  EXPECT(!is_reddit_url("https://example.com/r/linux"));
  EXPECT(!is_reddit_url("https://lemmy.world/c/linux"));
  EXPECT(!is_reddit_url(""));

  std::string u = reddit_json_url("https://www.reddit.com/r/linux");
  EXPECT(u.find("https://www.reddit.com/r/linux") == 0);
  EXPECT(u.find(".json") != std::string::npos);
  EXPECT(u.find("raw_json=1") != std::string::npos);

  EXPECT(reddit_json_url("https://example.com/r/x").empty());
}

void test_lemmy_urls() {
  using namespace xanatoo;
  EXPECT(is_lemmy_url("https://lemmy.world/c/asklemmy"));
  EXPECT(is_lemmy_url("https://programming.dev/post/12345"));
  EXPECT(is_lemmy_url(
      "https://lemmy.world/api/v3/post/list?community_name=linux"));
  EXPECT(!is_lemmy_url("https://lemmy.world/"));
  EXPECT(!is_lemmy_url("https://www.reddit.com/r/linux"));
  EXPECT(!is_lemmy_url("lemmy.world/c/asklemmy"));  // scheme required
  EXPECT(!is_lemmy_url(""));
}

void test_gemini_urls() {
  using namespace xanatoo;
  EXPECT(is_gemini_url("gemini://geminiprotocol.net/"));
  EXPECT(is_gemini_url("GEMINI://example.org/path"));
  EXPECT(!is_gemini_url("https://example.org/"));
  EXPECT(!is_gemini_url(""));

  auto u = parse_gemini_url("gemini://example.org:1966/foo?bar");
  EXPECT(u.has_value());
  if (u) {
    EXPECT_EQ(u->host, "example.org");
    EXPECT_EQ(u->port, 1966);
    EXPECT(u->path_query.find("/foo") == 0);
  }

  auto abs = resolve_gemini_url("gemini://example.org/dir/page", "other");
  EXPECT(abs.find("gemini://example.org/") == 0);
  EXPECT(abs.find("other") != std::string::npos);

  auto keep = resolve_gemini_url("gemini://example.org/a", "gemini://other/x");
  EXPECT_EQ(keep, "gemini://other/x");
}

void test_nostr_refs() {
  using namespace xanatoo;
  EXPECT(is_nostr_ref("nostr:recent"));
  EXPECT(is_nostr_ref(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  EXPECT(is_nostr_ref(
      "nostr:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  EXPECT(!is_nostr_ref("https://example.com"));
  EXPECT(!is_nostr_ref(""));
  EXPECT(!is_nostr_ref("note"));  // too short / not valid bech32 payload

  auto hex = nostr_decode_event_id(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  EXPECT(hex.has_value());
  if (hex)
    EXPECT_EQ(*hex,
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

}  // namespace

int main() {
  test_string_helpers();
  test_json_min();
  test_reddit_urls();
  test_lemmy_urls();
  test_gemini_urls();
  test_nostr_refs();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
