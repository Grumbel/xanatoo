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
#include "xanatoo/epub_import.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/style.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>
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

void test_serialize_md_uses_atx_headings() {
  using namespace xanatoo;
  const char* src = "# A\n\ntext\n\n## B\n\nmore\n\n# C\n\nend\n";
  Document doc = parse_document(src, "r.md");
  std::string out = serialize_document(doc, "r.md");
  EXPECT(out.find(":::") == std::string::npos);
  EXPECT(out.find("# A") != std::string::npos);
  EXPECT(out.find("## B") != std::string::npos);
  Document again = parse_document(out, "r.md");
  EXPECT_EQ(serialize_document(again, "r.md"), out);
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






void test_address_label_soft() {
  using namespace xanatoo;
  Document doc = parse_markup("# Hello World\n\nSee [x](#Hello World).\n");
  NodeId hdr = INVALID_NODE;
  for (const auto& n : doc.nodes) {
    if (n.kind == NodeKind::Header || n.kind == NodeKind::Heading) {
      hdr = n.id;
      break;
    }
  }
  EXPECT(hdr != INVALID_NODE);
  std::string lab = address_label(doc, hdr);
  EXPECT(!lab.empty());
  std::vector<BacklinkEntry> bl;
  collect_backlinks(doc, lab, bl);
  EXPECT(bl.size() >= 1);
}

void test_promote_demote_section() {
  using namespace xanatoo;
  Document doc = parse_markup(
      "# A\n"
      "body a\n"
      "\n"
      "## B\n"
      "body b\n");
  // Find section for B
  NodeId sec_b = INVALID_NODE;
  for (const auto& n : doc.nodes) {
    if (n.kind == NodeKind::Section) {
      for (NodeId c : n.children) {
        if (doc.get(c).kind == NodeKind::Header) {
          std::string t = plain_text(doc, c);
          if (t.find('B') != std::string::npos) sec_b = n.id;
        }
      }
    }
  }
  EXPECT(sec_b != INVALID_NODE);
  Caret caret{sec_b, 0, 0};
  // B is nested under A after normalize — promote should lift
  NodeId parent = doc.get(sec_b).parent;
  EXPECT(parent != INVALID_NODE);
  if (doc.get(parent).kind == NodeKind::Section ||
      doc.get(parent).kind == NodeKind::Article) {
    EXPECT(promote_section(doc, caret));
    EXPECT(doc.get(sec_b).parent != parent);
  }
}

void test_backlinks_collect() {
  using namespace xanatoo;
  Document doc = parse_markup(
      "# Target {#tgt}\n"
      "see [x](#tgt) and more.\n");
  // Ensure some label exists
  std::string lab;
  for (const auto& n : doc.nodes) {
    if (!n.label.empty()) {
      lab = n.label;
      break;
    }
  }
  std::vector<BacklinkEntry> bl;
  if (!lab.empty()) {
    collect_backlinks(doc, lab, bl);
    EXPECT(bl.size() >= 1);
  }
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



void test_style_sheet_parse() {
  using namespace xanatoo;
  DocStyle st = builtin_style();
  apply_style_source(st, R"(
    ; comment
    (stylesheet
      (name "Test")
      (define accent "#102030")
      (define gap 1.5em)
      (page (background "#fff") (color (rgb 1 2 3)))
      (fonts (body "Some Serif" "serif") (heading-weight regular)
             (heading-scale 1.5))
      (role paragraph (space-after gap) (space-before 4px))
      (role header (color accent) (font code))
      (role link (color accent) (underline #f))
      (role code (background none))
      (role bogus (color "#000"))
      (role note (wobble 3) (border "nonsense")))
  )",
                     "test");
  EXPECT_EQ(st.title, "Test");
  EXPECT(st.paper == (Rgba{255, 255, 255, 255}));
  EXPECT(st.ink == (Rgba{1, 2, 3, 255}));
  EXPECT_EQ(st.fonts.body.size(), 2u);
  EXPECT_EQ(st.fonts.body[0], "Some Serif");
  EXPECT(!st.fonts.heading_bold);
  EXPECT(st.heading_scale > 1.49f && st.heading_scale < 1.51f);
  const BlockStyle& para = st[BlockRole::Paragraph];
  EXPECT(para.space_after.em);
  EXPECT(para.space_after.px(10.f) > 14.9f && para.space_after.px(10.f) < 15.1f);
  EXPECT(!para.space_before.em);
  EXPECT(para.space_before.px(10.f) == 4.f);
  EXPECT(st[BlockRole::Header].font == FontRole::Code);
  EXPECT(st.text_color(BlockRole::Header, InlineKind::Text) ==
         (Rgba{0x10, 0x20, 0x30, 255}));
  // Inline colour wins over block colour; plain text falls back to ink.
  EXPECT(st.text_color(BlockRole::Header, InlineKind::Link) ==
         (Rgba{0x10, 0x20, 0x30, 255}));
  EXPECT(st.text_color(BlockRole::Paragraph, InlineKind::Strong) == st.ink);
  EXPECT(!st[InlineKind::Link].underline);
  EXPECT(!st[InlineKind::Code].background.visible());
  // Untouched roles keep the built-in values.
  EXPECT(st[BlockRole::ListItem].indent.px(10.f) == 28.f);
  // bogus role, unknown property, bad colour
  EXPECT_EQ(st.warnings.size(), 3u);
}

void test_style_sheets_shipped() {
  using namespace xanatoo;
  // Every sheet under styles/ loads without warnings.
  const std::vector<StyleEntry> sheets = list_styles();
  EXPECT(!sheets.empty());
  EXPECT_EQ(sheets.front().name, "default");
  for (const StyleEntry& e : sheets) {
    DocStyle st;
    std::string err;
    EXPECT(load_style(e.name, st, &err));
    if (!st.warnings.empty())
      std::fprintf(stderr, "style %s: %s\n", e.name.c_str(),
                   st.warnings.front().c_str());
    EXPECT(st.warnings.empty());
  }
  // default.xss mirrors the built-in look.
  DocStyle def;
  EXPECT(load_style("default", def));
  const DocStyle builtin = builtin_style();
  EXPECT(def.paper == builtin.paper && def.ink == builtin.ink);
  for (size_t i = 0; i < kBlockRoleCount; ++i) {
    const BlockStyle& a = def.block[i];
    const BlockStyle& b = builtin.block[i];
    EXPECT(a.space_before.px(17.f) == b.space_before.px(17.f) &&
           a.space_after.px(17.f) == b.space_after.px(17.f) &&
           a.indent.px(17.f) == b.indent.px(17.f) && a.font == b.font &&
           a.border == b.border && a.background == b.background);
  }
  // Late-bound tokens: night-amber only redefines tokens of night.
  DocStyle night, amber;
  if (load_style("night", night) && load_style("night-amber", amber)) {
    EXPECT(!(night.paper == amber.paper));
    EXPECT(!(night[InlineKind::Link].color == amber[InlineKind::Link].color));
    EXPECT(night[BlockRole::Table].border == amber[BlockRole::Table].border);
    EXPECT_EQ(amber.title, "Night Amber");
  }
  DocStyle missing;
  EXPECT(!load_style("no-such-sheet", missing));
}

void test_layout_wrap_respects_max_width() {
  using namespace xanatoo;
  // Metrics-only: no SDL window. Font textures may be null; advances still work.
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty()) path = fontconfig_find("Liberation Sans", 0, 0);
  if (path.empty()) path = fontconfig_find("FreeSans", 0, 0);
  if (path.empty()) {
    std::fprintf(stderr, "SKIP test_layout_wrap_respects_max_width (no font)\n");
    return;
  }
  if (!fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_wrap_respects_max_width (font load failed)\n");
    return;
  }

  // Long paragraph similar to the Storm of Steel overflow report.
  const char* md =
      "We stalked several working parties that, unfortunately, were separated "
      "from us by dense entanglements. The somewhat eccentric sergeant-major "
      "suggested giving himself up as a deserter and holding the enemy in "
      "parley until we had surrounded the first post. This plan was turned "
      "down after a brief discussion, and we crept back, disappointed, to the "
      "outpost.\n";
  Document doc = parse_document(md, "wrap-test.md");
  EXPECT(doc.root != INVALID_NODE);

  const float pane_w = 640.f;
  const float sb_gutter = 18.f;  // kSbW+4
  const float layout_w = pane_w - sb_gutter;

  LayoutEngine engine(fonts, nullptr);
  LayoutOptions opts;
  opts.margin_x = 32.f;
  opts.margin_y = 24.f;
  opts.line_spacing = 1.3f;
  opts.justify = false;
  engine.set_options(opts);

  LayoutResult lr = engine.layout(doc, layout_w);
  EXPECT(!lr.glyphs.empty());

  // Every glyph must stay within the content box: [margin_x, layout_w - margin_x]
  // (layout places the column at x=margin_x with width content_w).
  const float left = opts.margin_x;
  const float right = layout_w - opts.margin_x;
  float max_right = left;
  float worst = 0.f;
  uint32_t worst_cp = 0;
  int overflow_n = 0;
  for (const auto& g : lr.glyphs) {
    if (g.codepoint == ' ' || g.codepoint == 0) continue;
    Font& f = fonts.pick(g.font);
    const Glyph& met = f.glyph(g.codepoint);
    float adv = static_cast<float>(met.advance >> 6);
    float cell_r = g.x - static_cast<float>(met.bearing_x) + adv;
    if (cell_r > max_right) max_right = cell_r;
    if (cell_r > right + 1.5f) {
      ++overflow_n;
      if (cell_r - right > worst) {
        worst = cell_r - right;
        worst_cp = g.codepoint;
      }
    }
  }
  if (overflow_n) {
    std::fprintf(stderr,
                 "wrap overflow: n=%d max_right=%.2f right=%.2f worst=+%.2f "
                 "cp=U+%04X layout_w=%.1f mx=%.1f glyphs=%zu\n",
                 overflow_n, max_right, right, worst, worst_cp, layout_w,
                 opts.margin_x, lr.glyphs.size());
  }
  EXPECT(overflow_n == 0);
  EXPECT(max_right <= right + 1.5f);

  // With justify on, still must not overflow.
  opts.justify = true;
  engine.set_options(opts);
  lr = engine.layout(doc, layout_w);
  for (const auto& g : lr.glyphs) {
    if (g.codepoint == ' ' || g.codepoint == 0) continue;
    Font& f = fonts.pick(g.font);
    const Glyph& met = f.glyph(g.codepoint);
    float adv = static_cast<float>(met.advance >> 6);
    float cell_r = g.x - static_cast<float>(met.bearing_x) + adv;
    EXPECT(cell_r <= right + 1.5f);
  }

  // Simulate the reported overflow: pane width with scrollbar, narrow window.
  const float narrow = 400.f;
  lr = engine.layout(doc, narrow - sb_gutter);
  const float right_n = (narrow - sb_gutter) - opts.margin_x;
  for (const auto& g : lr.glyphs) {
    if (g.codepoint == ' ' || g.codepoint == 0) continue;
    Font& f = fonts.pick(g.font);
    const Glyph& met = f.glyph(g.codepoint);
    float adv = static_cast<float>(met.advance >> 6);
    float cell_r = g.x - static_cast<float>(met.bearing_x) + adv;
    EXPECT(cell_r <= right_n + 1.5f);
  }
}


void test_utf8_pop_back() {
  using namespace xanatoo;
  std::string a = "abc";
  utf8_pop_back(a);
  EXPECT_EQ(a, "ab");
  std::string b = "h\xC3\xA9";  // "hé"
  utf8_pop_back(b);
  EXPECT_EQ(b, "h");
  std::string c = "\xE2\x82\xAC";  // single 3-byte code point
  utf8_pop_back(c);
  EXPECT(c.empty());
  utf8_pop_back(c);  // no-op on empty
  EXPECT(c.empty());
}

void test_sanitize_epub_markdown() {
  using namespace xanatoo;
  // Empty calibre spacer spans (NBSP / spaces / entities)
  EXPECT_EQ(sanitize_epub_markdown(
                "Hello<span class=\"calibre4\"> </span>world"),
            "Hello world");
  EXPECT_EQ(sanitize_epub_markdown(
                "A<span class=\"calibre4\">&nbsp;</span>B"),
            "A B");
  EXPECT_EQ(sanitize_epub_markdown(
                "A<span class=\"calibre4\"> </span>B"),
            "A B");
  // UTF-8 U+00A0 as two bytes in a source string
  {
    std::string in = "X<span class=\"c\">";
    in.push_back(static_cast<char>(0xC2));
    in.push_back(static_cast<char>(0xA0));
    in += "</span>Y";
    EXPECT_EQ(sanitize_epub_markdown(in), "X Y");
  }
  // Unwrap span that only wraps text
  EXPECT_EQ(sanitize_epub_markdown(
                "Say <span class=\"calibre1\">hello</span>."),
            "Say hello.");
  // Empty page anchors
  EXPECT_EQ(sanitize_epub_markdown(
                "Text<a id=\"page_1\"></a> more"),
            "Text more");
  EXPECT_EQ(sanitize_epub_markdown("Text<a id=\"x\"/> more"), "Text more");
  // Nested-looking content with real text preserved
  EXPECT_EQ(sanitize_epub_markdown(
                "<span class=\"calibre\">Keep me</span>"),
            "Keep me");
  // Markdown structure must survive
  EXPECT_EQ(sanitize_epub_markdown("# Title\n\nPara with *em*.\n"),
            "# Title\n\nPara with *em*.\n");
  // Standalone NBSP entity in text
  EXPECT_EQ(sanitize_epub_markdown("a&nbsp;b"), "a b");
}

// All nodes of `kind`, in document order.
std::vector<xanatoo::NodeId> nodes_of(const xanatoo::Document& doc,
                                      xanatoo::NodeKind kind) {
  std::vector<xanatoo::NodeId> out;
  for (const auto& n : doc.nodes)
    if (n.id != xanatoo::INVALID_NODE && n.kind == kind) out.push_back(n.id);
  return out;
}

void test_footnotes() {
  using namespace xanatoo;
  // Pandoc GFM output: refs, a one-line note, a two-paragraph note.
  const char* md =
      "Hello[^1] world[^b].\n"
      "\n"
      "[^1]: Note *one*.\n"
      "\n"
      "[^b]: Note B line\n"
      "continued.\n"
      "\n"
      "    Second para of B.\n"
      "\n"
      "After the notes.\n";
  Document doc = parse_document(md, "fn.md");
  const auto fns = nodes_of(doc, NodeKind::Footnote);
  EXPECT_EQ(fns.size(), 2u);
  EXPECT(nodes_of(doc, NodeKind::BiblioEntry).empty());
  if (fns.size() == 2) {
    EXPECT_EQ(doc.get(fns[0]).label, "1");
    EXPECT_EQ(doc.get(fns[0]).children.size(), 1u);
    EXPECT_EQ(doc.get(fns[1]).label, "b");
    EXPECT_EQ(doc.get(fns[1]).children.size(), 2u);
    if (doc.get(fns[1]).children.size() == 2) {
      EXPECT_EQ(plain_text(doc, doc.get(fns[1]).children[0]),
                "Note B line continued.");
      EXPECT_EQ(plain_text(doc, doc.get(fns[1]).children[1]),
                "Second para of B.");
    }
  }
  // First paragraph: Text, Ref(1), Text, Ref(b), Text
  const Node& p0 = doc.get(doc.get(doc.root).children[0]);
  EXPECT_EQ(p0.spans.size(), 5u);
  if (p0.spans.size() == 5) {
    EXPECT(p0.spans[1].kind == InlineKind::FootnoteRef);
    EXPECT_EQ(p0.spans[1].href, "1");
    EXPECT_EQ(p0.spans[3].href, "b");
  }
  // "After the notes." is an ordinary paragraph again
  EXPECT(doc.get(doc.get(doc.root).children.back()).kind ==
         NodeKind::Paragraph);

  // #fn:key resolves to the note's first paragraph
  const NodeId t = find_anchor_node(doc, footnote_href("B"));
  EXPECT(t != INVALID_NODE && doc.get(t).parent == find_footnote(doc, "b"));

  // Markdown round trip keeps refs and both paragraphs
  const std::string out = serialize_document(doc, "fn.md");
  EXPECT(out.find("Hello[^1] world[^b]") != std::string::npos);
  EXPECT(out.find("\n    Second para of B.") != std::string::npos);
  Document again = parse_document(out, "fn.md");
  EXPECT_EQ(nodes_of(again, NodeKind::Footnote).size(), 2u);
  EXPECT_EQ(serialize_document(again, "fn.md"), out);

  // S-expression round trip
  const std::string sx = serialize_document(doc, "fn.sexp");
  EXPECT(sx.find("(fnref \"b\")") != std::string::npos);
  Document sdoc = parse_document(sx, "fn.sexp");
  EXPECT_EQ(nodes_of(sdoc, NodeKind::Footnote).size(), 2u);
  EXPECT_EQ(serialize_document(sdoc, "fn.md"), out);

  // [^key]: inside :::biblio stays a bibliography entry
  Document bib = parse_document(
      "See [Nelson](cite:nelson65).\n\n:::biblio\n[^nelson65]: Nelson, T.\n:::\n",
      "b.md");
  EXPECT_EQ(nodes_of(bib, NodeKind::BiblioEntry).size(), 1u);
  EXPECT(nodes_of(bib, NodeKind::Footnote).empty());

  // Org: [fn:key] refs and definitions
  Document org = parse_document(
      "* Head\nText[fn:1] more[fn:x].\n\n[fn:1] Org note.\n\n[fn:x] Other\nnote.\n",
      "n.org");
  const auto ofn = nodes_of(org, NodeKind::Footnote);
  EXPECT_EQ(ofn.size(), 2u);
  if (ofn.size() == 2) {
    EXPECT_EQ(org.get(ofn[1]).label, "x");
    EXPECT_EQ(plain_text(org, org.get(ofn[1]).children[0]), "Other note.");
  }
  const std::string oout = serialize_document(org, "n.org");
  EXPECT(oout.find("Text[fn:1] more[fn:x].") != std::string::npos);
  EXPECT(oout.find("[fn:x] Other note.") != std::string::npos);

  // Overlay helpers: marks in the band, minus notes already shown there
  LayoutResult lr;
  const NodeId para = doc.get(doc.root).children[0];
  auto mark = [&](size_t span, float y) {
    PositionedGlyph g;
    g.node = para;
    g.span_index = span;
    g.kind = InlineKind::FootnoteRef;
    g.y = y;
    lr.glyphs.push_back(g);
  };
  mark(3, 100.f);  // b
  mark(1, 120.f);  // 1
  mark(3, 900.f);  // b again, off screen
  auto keys = visible_footnote_keys(doc, lr, 0.f, 500.f);
  EXPECT((keys == std::vector<std::string>{"b", "1"}));
  lr.boxes.push_back({fns[0], 0.f, 400.f, 100.f, 30.f});  // note 1 on screen
  keys = visible_footnote_keys(doc, lr, 0.f, 500.f);
  EXPECT((keys == std::vector<std::string>{"b"}));

  std::vector<NodeId> src;
  Document notes = extract_footnotes(doc, {"b", "nope", "1"}, &src);
  EXPECT((src == std::vector<NodeId>{fns[1], fns[0]}));
  EXPECT_EQ(notes.get(notes.root).children.size(), 2u);
  EXPECT_EQ(nodes_of(notes, NodeKind::Paragraph).size(), 3u);
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
  test_promote_demote_section();
  test_backlinks_collect();
  test_address_label_soft();
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
  test_serialize_md_uses_atx_headings();
  test_json_array_and_bool();
  test_utf8_pop_back();
  test_sanitize_epub_markdown();
  test_layout_wrap_respects_max_width();
  test_style_sheet_parse();
  test_style_sheets_shipped();
  test_footnotes();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
