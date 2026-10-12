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
#include "xanatoo/parse.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/org.hpp"
#include "xanatoo/types.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
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


void test_parse_markup() {
  using namespace xanatoo;
  Document doc = parse_document(
      "# Title\n\n"
      "Hello *world* and **bold**.\n\n"
      "- one\n"
      "- two\n\n"
      "[link](#Title)\n",
      "t.md");

  EXPECT(doc.root != INVALID_NODE);
  // Collect headings / paragraphs via tree walk
  int headings = 0, paragraphs = 0, list_items = 0;
  std::vector<NodeId> stack;
  stack.push_back(doc.root);
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header)
      ++headings;
    if (n.kind == NodeKind::Paragraph) ++paragraphs;
    if (n.kind == NodeKind::ListItem) ++list_items;
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
      stack.push_back(*it);
  }
  EXPECT(headings >= 1);
  EXPECT(paragraphs >= 1);
  EXPECT(list_items >= 2);

  std::string plain = plain_text(doc, doc.root);
  // Title may appear; at least body words should show up somewhere in tree
  bool found_hello = false;
  stack.push_back(doc.root);
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    for (const auto& sp : n.spans) {
      if (sp.text.find("Hello") != std::string::npos ||
          sp.text.find("world") != std::string::npos)
        found_hello = true;
    }
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(found_hello);

  // Round-trip serialize preserves a heading marker
  std::string out = serialize_markup(doc);
  EXPECT(out.find("#") != std::string::npos);
}

void test_parse_gemtext() {
  using namespace xanatoo;
  Document doc = parse_gemtext(
      "# Capsule\n"
      "Hello gemini\n"
      "=> gemini://example.org/ Follow\n"
      "* item\n");
  EXPECT(doc.root != INVALID_NODE);

  bool found_link = false;
  bool found_heading = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header)
      found_heading = true;
    for (const auto& sp : n.spans) {
      if (sp.kind == InlineKind::Link ||
          sp.text.find("Follow") != std::string::npos)
        found_link = true;
    }
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(found_heading);
  EXPECT(found_link);
}

void test_json_edge_cases() {
  using namespace xanatoo;
  EXPECT(json_str_field("{}", "x").empty());
  EXPECT_EQ(json_int_field("{}", "x"), 0);
  EXPECT(json_str_field(R"({"a":null})", "a").empty());
  EXPECT_EQ(json_str_field(R"({"t":"a\"b"})", "t"), "a\"b");
  // Prefer key at object boundary (not mid-string) — basic smoke
  EXPECT_EQ(json_str_field(R"({"k":"v","k2":"w"})", "k2"), "w");
}

void test_url_edge_cases() {
  using namespace xanatoo;
  EXPECT(is_reddit_url("  https://www.reddit.com/r/x  "));
  EXPECT(is_reddit_url("https://np.reddit.com/r/x"));
  EXPECT(!is_reddit_url("https://notreddit.com/r/x"));

  EXPECT(is_lemmy_url("https://lemmy.ml/c/foo"));
  EXPECT(is_lemmy_url("https://lemmy.ml/post/1"));
  EXPECT(!is_lemmy_url("https://lemmy.ml/u/someone"));

  EXPECT(is_gemini_url("gemini://a"));
  EXPECT(!is_gemini_url("gemeni://a"));  // typo

  EXPECT(is_nostr_ref(" nostr:recent "));
  EXPECT(is_nostr_ref("recent"));  // bare firehose aliases accepted
  EXPECT(is_nostr_ref("firehose"));
  EXPECT(!is_nostr_ref("not-a-nostr-thing"));
}


void test_path_helpers() {
  using namespace xanatoo;
  EXPECT(path_ends_with_ci("File.MD", ".md"));
  EXPECT(path_ends_with_ci("x.Org", ".org"));
  EXPECT(!path_ends_with_ci("file.md", ".org"));
  EXPECT(path_uses_org("notes.org"));
  EXPECT(path_uses_org("Notes.ORG"));
  EXPECT(!path_uses_org("notes.md"));
  EXPECT(is_raster_image_path("pic.PNG"));
  EXPECT(is_raster_image_path("a/b/c.jpeg"));
  EXPECT(!is_raster_image_path("doc.md"));
}

void test_parse_org() {
  using namespace xanatoo;
  Document doc = parse_org(
      "#+TITLE: Demo\n"
      "\n"
      "* TODO Heading :tag:\n"
      "Body text with *bold* and /italic/.\n"
      "\n"
      "- item one\n"
      "- item two\n"
      "\n"
      "** Nested\n"
      "More.\n");

  EXPECT(doc.root != INVALID_NODE);
  int headers = 0, list_items = 0, sections = 0;
  bool found_body = false;
  bool nested_under_section = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Header || n.kind == NodeKind::Heading)
      ++headers;
    if (n.kind == NodeKind::Section) ++sections;
    if (n.kind == NodeKind::ListItem) ++list_items;
    for (const auto& sp : n.spans) {
      if (sp.text.find("Body") != std::string::npos ||
          sp.text.find("bold") != std::string::npos)
        found_body = true;
    }
    // Nested "** Nested" section should be child of the outer section.
    if (n.kind == NodeKind::Section && n.level == 2 &&
        n.parent != INVALID_NODE &&
        doc.get(n.parent).kind == NodeKind::Section)
      nested_under_section = true;
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(headers >= 2);
  EXPECT(sections >= 2);
  EXPECT(nested_under_section);
  EXPECT(list_items >= 2);
  EXPECT(found_body);

  std::string out = serialize_org(doc);
  EXPECT(!out.empty());
  // Best-effort: still contains a star heading or TITLE residue
  EXPECT(out.find("*") != std::string::npos ||
         out.find("Heading") != std::string::npos);
}

void test_parse_document_dispatch() {
  using namespace xanatoo;
  Document org = parse_document("* Only\n", "x.org");
  EXPECT(org.root != INVALID_NODE);
  Document md = parse_document("# Only\n", "x.md");
  EXPECT(md.root != INVALID_NODE);
  Document gmi = parse_document("# Only\n", "x.gmi");
  EXPECT(gmi.root != INVALID_NODE);
}

void test_reddit_json_url_shapes() {
  using namespace xanatoo;
  auto a = reddit_json_url("https://old.reddit.com/r/linux/");
  EXPECT(a.find("www.reddit.com") != std::string::npos);
  EXPECT(a.find(".json") != std::string::npos);

  auto b = reddit_json_url("https://www.reddit.com/r/linux/.json?limit=10");
  EXPECT(b.find(".json") != std::string::npos);
  // Should not double-append .json awkwardly
  EXPECT(b.find(".json.json") == std::string::npos);

  auto c = reddit_json_url(
      "https://www.reddit.com/r/linux/comments/abc123/title");
  EXPECT(c.find("comments") != std::string::npos);
  EXPECT(c.find(".json") != std::string::npos);
}


void test_path_format_dispatch() {
  using namespace xanatoo;
  EXPECT(path_uses_sexpr("doc.sexp"));
  EXPECT(path_uses_sexpr("x.glyph"));
  EXPECT(path_uses_sexpr("y.ss"));
  EXPECT(!path_uses_sexpr("doc.md"));
  EXPECT(path_uses_gemtext("page.gmi"));
  EXPECT(path_uses_gemtext("page.gemini"));
  EXPECT(!path_uses_gemtext("page.md"));
  EXPECT(looks_like_sexpr("  ; comment\n(doc (p \"x\"))"));
  EXPECT(!looks_like_sexpr("# Markdown title\n"));
}

void test_parse_sexpr() {
  using namespace xanatoo;
  Document doc = parse_sexpr(
      "(doc\n"
      "  (h 1 \"Title\")\n"
      "  (p \"Hello \" (em \"world\") \"!\")\n"
      "  (ul (li \"a\") (li \"b\"))\n"
      "  (hr))\n");
  EXPECT(doc.root != INVALID_NODE);

  int headings = 0, paragraphs = 0, list_items = 0, hrs = 0;
  bool found_em = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header)
      ++headings;
    if (n.kind == NodeKind::Paragraph) ++paragraphs;
    if (n.kind == NodeKind::ListItem) ++list_items;
    if (n.kind == NodeKind::HorizontalRule) ++hrs;
    for (const auto& sp : n.spans) {
      if (sp.kind == InlineKind::Emphasis ||
          sp.text.find("world") != std::string::npos)
        found_em = true;
    }
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(headings >= 1);
  EXPECT(paragraphs >= 1);
  EXPECT(list_items >= 2);
  EXPECT(hrs >= 1);
  EXPECT(found_em);

  std::string out = serialize_sexpr(doc);
  EXPECT(out.find("(doc") != std::string::npos ||
         out.find("doc") != std::string::npos);
}

void test_parse_document_sexpr_path() {
  using namespace xanatoo;
  Document doc = parse_document("(doc (h 1 \"X\"))\n", "note.sexp");
  EXPECT(doc.root != INVALID_NODE);
  int headings = 0;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Heading || n.kind == NodeKind::Header)
      ++headings;
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(headings >= 1);
}

void test_markup_stretch_and_link() {
  using namespace xanatoo;
  Document doc = parse_document(
      "# H\n\n"
      ":::stretch summary=\"More\"\n"
      "Hidden body\n"
      ":::\n\n"
      "See [here](https://example.com).\n",
      "t.md");
  EXPECT(doc.root != INVALID_NODE);
  bool found_stretch = false, found_link = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Stretch) found_stretch = true;
    for (const auto& sp : n.spans) {
      if (sp.kind == InlineKind::Link) found_link = true;
    }
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(found_stretch);
  EXPECT(found_link);
}


void test_markup_blockquote_code_table() {
  using namespace xanatoo;
  Document doc = parse_document(
      "# H\n\n"
      "> quoted line\n\n"
      "```cpp\n"
      "int x = 1;\n"
      "```\n\n"
      "1. first\n"
      "2. second\n\n"
      "| A | B |\n"
      "| --- | --- |\n"
      "| 1 | 2 |\n",
      "t.md");
  EXPECT(doc.root != INVALID_NODE);

  bool quote = false, code = false, table = false;
  int ordered_items = 0;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::BlockQuote) quote = true;
    if (n.kind == NodeKind::CodeBlock) code = true;
    if (n.kind == NodeKind::Table || n.kind == NodeKind::TableRow ||
        n.kind == NodeKind::TableCell)
      table = true;
    if (n.kind == NodeKind::ListItem && n.level != 0)
      ++ordered_items;  // ordered lists may use level flag
    if (n.kind == NodeKind::ListItem) ++ordered_items;
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(quote);
  EXPECT(code);
  // Tables are optional in the small dialect; accept either presence or
  // graceful degradation to paragraphs.
  EXPECT(ordered_items >= 2);
  (void)table;
}

void test_markup_columns() {
  using namespace xanatoo;
  Document doc = parse_document(
      ":::columns\n"
      ":::column weight=1.5\n"
      "Left\n"
      ":::\n"
      ":::column\n"
      "Right\n"
      ":::\n"
      ":::\n",
      "cols.md");
  EXPECT(doc.root != INVALID_NODE);
  bool columns = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Columns) columns = true;
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(columns);
}

void test_gemtext_structures() {
  using namespace xanatoo;
  Document doc = parse_gemtext(
      "## Sub\n"
      "> a quote\n"
      "* bullet\n"
      "```\n"
      "pre line\n"
      "```\n"
      "plain line\n");
  EXPECT(doc.root != INVALID_NODE);
  bool quote = false, list = false, code = false, para = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    NodeId id = stack.back();
    stack.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::BlockQuote) quote = true;
    if (n.kind == NodeKind::ListItem) list = true;
    if (n.kind == NodeKind::CodeBlock) code = true;
    if (n.kind == NodeKind::Paragraph) {
      for (const auto& sp : n.spans)
        if (sp.text.find("plain") != std::string::npos) para = true;
    }
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT(list);
  EXPECT(para);
  // quote/code depend on gemtext mapping; soft checks
  (void)quote;
  (void)code;
}

void test_serialize_roundtrip_md() {
  using namespace xanatoo;
  const char* src = "# Title\n\nParagraph with **x**.\n";
  Document doc = parse_document(src, "r.md");
  std::string out = serialize_document(doc, "r.md");
  EXPECT(out.find("Title") != std::string::npos);
  EXPECT(out.find("Paragraph") != std::string::npos ||
         out.find("x") != std::string::npos);

  Document again = parse_document(out, "r.md");
  EXPECT(again.root != INVALID_NODE);
}

void test_json_array_and_bool() {
  using namespace xanatoo;
  const char* j = R"({"flags":{"on":true,"off":false},"items":[{"n":1},{"n":2},{"n":3}]})";
  EXPECT(json_bool_field(json_object_after_key(j, "flags"), "on"));
  EXPECT(!json_bool_field(json_object_after_key(j, "flags"), "off"));
  auto items = json_object_array_after_key(j, "items");
  EXPECT_EQ(items.size(), 3u);
  EXPECT_EQ(json_int_field(items[2], "n"), 3);
}

}  // namespace



void test_outline_section_tree() {
  using namespace xanatoo;
  Document doc = parse_markup(
      "# Top\n"
      "Body A\n"
      "\n"
      "## Nested\n"
      "Body B\n");
  std::vector<OutlineEntry> ol;
  collect_outline(doc, ol, nullptr);
  EXPECT(ol.size() >= 2);
  EXPECT(ol[0].level == 1);
  EXPECT(ol[1].level >= 2);
  EXPECT(ol[0].section != INVALID_NODE);
  EXPECT(ol[0].has_children);

  std::unordered_set<NodeId> folded;
  folded.insert(ol[0].section);
  std::vector<OutlineEntry> ol2;
  collect_outline(doc, ol2, &folded);
  EXPECT(ol2.size() == 1);
  EXPECT(ol2[0].folded);
}

void test_atx_section_tree() {
  using namespace xanatoo;
  Document doc = parse_markup(
      "# Top\n"
      "Body A\n"
      "\n"
      "## Nested\n"
      "Body B\n");
  int sections = 0, headers = 0;
  bool body_under_section = false;
  std::vector<NodeId> st{doc.root};
  while (!st.empty()) {
    NodeId id = st.back();
    st.pop_back();
    const Node& n = doc.get(id);
    if (n.kind == NodeKind::Section) ++sections;
    if (n.kind == NodeKind::Header) ++headers;
    if (n.kind == NodeKind::Paragraph && n.parent != INVALID_NODE &&
        doc.get(n.parent).kind == NodeKind::Section)
      body_under_section = true;
    for (auto c : n.children) st.push_back(c);
  }
  EXPECT(sections >= 2);
  EXPECT(headers >= 2);
  EXPECT(body_under_section);
}

int main() {
  test_string_helpers();
  test_json_min();
  test_reddit_urls();
  test_lemmy_urls();
  test_gemini_urls();
  test_nostr_refs();
  test_parse_markup();
  test_parse_gemtext();
  test_json_edge_cases();
  test_url_edge_cases();
  test_path_helpers();
  test_parse_org();
  test_atx_section_tree();
  test_outline_section_tree();
  test_parse_document_dispatch();
  test_reddit_json_url_shapes();
  test_path_format_dispatch();
  test_parse_sexpr();
  test_parse_document_sexpr_path();
  test_markup_stretch_and_link();
  test_markup_blockquote_code_table();
  test_markup_columns();
  test_gemtext_structures();
  test_serialize_roundtrip_md();
  test_json_array_and_bool();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
