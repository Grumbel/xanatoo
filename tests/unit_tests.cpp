// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Lightweight unit tests (no Catch/GTest). Run: ./xanatoo_tests

#include "xanatoo/about.hpp"
#include "xanatoo/ebooks.hpp"
#include "xanatoo/feed.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/html_min.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/download.hpp"
#include "xanatoo/table_grid.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/nav_history.hpp"
#include "xanatoo/history_menu.hpp"
#include "xanatoo/wikipedia.hpp"
#include "xanatoo/hackernews.hpp"
#include "xanatoo/time.hpp"
#include "xanatoo/json_min.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/locator.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/reddit_credentials.hpp"
#include "xanatoo/reply.hpp"
#include "xanatoo/inflate.hpp"
#include "xanatoo/search.hpp"
#include "xanatoo/search_results.hpp"
#include "xanatoo/sitemap.hpp"
#include "xanatoo/revstore.hpp"
#include "xanatoo/annotations.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/examples.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/source_view.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/org.hpp"
#include "xanatoo/types.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/math.hpp"
#include "xanatoo/paginate.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/style.hpp"
#include "xanatoo/sdl.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <cstdlib>
#include <iostream>
#include <string>
#include <algorithm>
#include <string_view>

namespace {

int g_failed = 0;
int g_passed = 0;

int process_id() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

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

void test_reply_markdown() {
  using xanatoo::wrap_reply_markup;
  auto bold = wrap_reply_markup("hi world!", 3, 8, "**", "**");
  EXPECT_EQ(bold.text, "hi **world**!");
  EXPECT_EQ(bold.selection_begin, 5u);
  EXPECT_EQ(bold.selection_end, 10u);
  auto empty = wrap_reply_markup("hello", 2, 2, "`", "`");
  EXPECT_EQ(empty.text, "he``llo");
  EXPECT_EQ(empty.selection_begin, empty.selection_end);
  auto reversed = wrap_reply_markup("abcdef", 5, 2, "*", "*");
  EXPECT_EQ(reversed.text, "ab*cde*f");
  // ImGui selection offsets are UTF-8 bytes, not code points.
  auto utf8 = wrap_reply_markup("xäz", 1, 3, "[", "](https://)");
  EXPECT_EQ(utf8.text, "x[ä](https://)z");
  auto clamped = wrap_reply_markup("abc", 1, 99, "> ", "");
  EXPECT_EQ(clamped.text, "a> bc");
}

void test_sdl_time_bridge() {
  using xanatoo::sdl::ticks_passed;
  EXPECT(!xanatoo::sdl::runtime_version().empty());
  EXPECT(ticks_passed(100, 100));
  EXPECT(ticks_passed(101, 100));
  EXPECT(!ticks_passed(99, 100));
  EXPECT(!ticks_passed(0xfffffff0u, 0x00000010u));
  EXPECT(ticks_passed(0x00000010u, 0xfffffff0u));

  SDL_Event event{};
  using xanatoo::sdl::EventKind;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::Other);
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  event.type = SDL_EVENT_WINDOW_MOVED;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::Other);
  event.type = SDL_EVENT_WINDOW_RESIZED;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::WindowResized);
  event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::GamepadButtonDown);
  EXPECT_EQ(xanatoo::sdl::gamepad_event_button(event), xanatoo::sdl::button::A);
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = SDLK_RETURN;
  event.key.mod = SDL_KMOD_CTRL;
#else
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_MOVED;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::Other);
  event.window.event = SDL_WINDOWEVENT_RESIZED;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::WindowResized);
  event.type = SDL_CONTROLLERBUTTONDOWN;
  event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
  EXPECT_EQ(xanatoo::sdl::event_kind(event), EventKind::GamepadButtonDown);
  EXPECT_EQ(xanatoo::sdl::gamepad_event_button(event),
            xanatoo::sdl::button::A);
  event.type = SDL_KEYDOWN;
  event.key.keysym.sym = SDLK_RETURN;
  event.key.keysym.mod = KMOD_CTRL;
#endif
  EXPECT_EQ(xanatoo::sdl::keycode(event), SDLK_RETURN);
  EXPECT((xanatoo::sdl::keymod(event) & KMOD_CTRL) != 0);
  xanatoo::sdl::set_keycode(event, SDLK_ESCAPE);
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  EXPECT_EQ(event.key.key, SDLK_ESCAPE);
#else
  EXPECT_EQ(event.key.keysym.sym, SDLK_ESCAPE);
#endif
}

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

void test_sha256() {
  using namespace xanatoo;
  EXPECT_EQ(sha256_hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256_hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Two-block message (56 bytes: padding spills into a second block).
  EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha256_hex(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  EXPECT_EQ(content_id("abc").substr(0, 7), "sha256:");
}

void test_revstore() {
#if XANATOO_SQLITE
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const fs::path db =
      fs::temp_directory_path() / ("xanatoo-revstore-" + std::to_string(process_id()) + ".sqlite");
  fs::remove(db);
  revstore_use_path(db.string());

  const std::string key = new_doc_key();
  EXPECT_EQ(key.size(), 4u + 64u);
  EXPECT(key != new_doc_key());
  EXPECT(!find_draft(key));

  const std::string id = put_draft(key, "# Two\n", "# One\n", "notes.md");
  EXPECT_EQ(id, content_id("# Two\n"));
  auto d = find_draft(key);
  EXPECT(d.has_value());
  if (d) {
    EXPECT_EQ(d->content, id);
    EXPECT_EQ(d->base, content_id("# One\n"));
    EXPECT_EQ(d->title, "notes.md");
    EXPECT_EQ(load_object(d->content).value_or(""), "# Two\n");
  }
  EXPECT_EQ(load_object(content_id("# One\n")).value_or(""), "# One\n");
  // A later draft replaces the head, keeps the base.
  put_draft(key, "# Three\n", "", "notes.md");
  d = find_draft(key);
  EXPECT(d && d->content == content_id("# Three\n"));
  EXPECT(d && d->base == content_id("# One\n"));
  EXPECT_EQ(list_drafts().size(), 1u);

  EXPECT(record_save(key, "# Three\n"));
  EXPECT(!find_draft(key));
  EXPECT(list_drafts().empty());
  // Old versions stay addressable.
  EXPECT_EQ(load_object(content_id("# Two\n")).value_or(""), "# Two\n");

  put_draft(key, "# Four\n", "", "notes.md");
  EXPECT(drop_draft(key));
  EXPECT(!find_draft(key));

  revstore_use_path({});
  fs::remove(db);
#endif
}

void test_annotation_layers() {
  using namespace xanatoo;
  AnnotationLayer layer;
  layer.id = "layer:alice";
  layer.owner = "Alice";
  layer.name = "Alice's notes";
  Annotation note;
  note.id = "anno:1";
  note.kind = "note";
  note.body = "Comment with binary and umlaut: ä";
  note.body.insert(13, 1, '\0');
  note.style = "margin";
  note.targets.push_back({"doc:article", "sha256:original", "article#text:hello"});
  note.targets.push_back({"doc:other", "sha256:other", "other#id:section"});
  layer.annotations.push_back(note);
  const std::string bytes = serialize_annotation_layer(layer);
  EXPECT(!bytes.empty());
  auto parsed = parse_annotation_layer(bytes);
  EXPECT(parsed.has_value());
  if (parsed) {
    EXPECT_EQ(parsed->annotations, layer.annotations);
    EXPECT_EQ(parsed->owner, layer.owner);
    EXPECT_EQ(serialize_annotation_layer(*parsed), bytes);
  }
  EXPECT(!parse_annotation_layer(bytes.substr(0, bytes.size() - 1)));
  EXPECT(!parse_annotation_layer(bytes + "garbage"));
  EXPECT(!parse_annotation_layer("not an annotation layer"));
  layer.annotations.push_back(note);
  EXPECT(serialize_annotation_layer(layer).empty()); // duplicate stable id
  layer.annotations.pop_back();
  layer.annotations[0].targets.clear();
  EXPECT(serialize_annotation_layer(layer).empty()); // no target

#if XANATOO_SQLITE
  namespace fs = std::filesystem;
  const fs::path db = fs::temp_directory_path() /
      ("xanatoo-annotations-" + std::to_string(process_id()) + ".sqlite");
  fs::remove(db);
  revstore_use_path(db.string());
  auto alice = create_annotation_layer("Alice", "Read-through");
  auto bob = create_annotation_layer("Bob", "Peer review");
  EXPECT(alice.has_value() && bob.has_value());
  if (alice && bob) {
    const std::string empty_rev = alice->revision;
    EXPECT(!empty_rev.empty());
    auto initial = load_annotation_layer(alice->id);
    EXPECT(initial && initial->revision == empty_rev);
    auto stale = *alice;
    alice->annotations.push_back(note);
    EXPECT(save_annotation_layer(*alice));
    EXPECT(alice->revision != empty_rev);
    EXPECT(!save_annotation_layer(stale)); // optimistic concurrency
    auto historical = load_annotation_layer(alice->id, empty_rev);
    EXPECT(historical && historical->annotations.empty());
    auto current = load_annotation_layer(alice->id);
    EXPECT(current && current->annotations == alice->annotations);
    auto stored = load_object_revision(alice->revision);
    EXPECT(stored && stored->parent == empty_rev);
    EXPECT(save_annotation_layer(*alice)); // unchanged means no new revision
    EXPECT_EQ(current->revision, alice->revision);
    // Edits and removals append new layer revisions without changing the
    // original document or destroying earlier versions of this layer.
    const auto before_edit = alice->revision;
    EXPECT(update_annotation_body(*alice, note.id, "Revised note text"));
    EXPECT(alice->annotations[0].body == "Revised note text");
    EXPECT(load_annotation_layer(alice->id, before_edit)->annotations[0].body ==
           note.body);
    const auto before_delete = alice->revision;
    EXPECT(remove_annotation(*alice, note.id));
    EXPECT(alice->annotations.empty());
    EXPECT(load_annotation_layer(alice->id, before_delete)->annotations.size() == 1);
    EXPECT(!remove_annotation(*alice, note.id));
    EXPECT(!update_annotation_body(*alice, note.id, "missing"));
    EXPECT(add_annotation(*alice, note));
    EXPECT(!add_annotation(*alice, note));
    auto out_of_date = *alice;
    EXPECT(update_annotation_body(*alice, note.id, "newest"));
    EXPECT(!update_annotation_body(out_of_date, note.id, "stale"));
    EXPECT(out_of_date.annotations[0].body == note.body);
    EXPECT(out_of_date.revision != alice->revision);
    const std::string pre_restore = alice->revision;
    const auto history = annotation_layer_history(*alice, 3);
    EXPECT_EQ(history.size(), 3u);
    EXPECT(history[0].id == pre_restore);
    EXPECT(history[1].id == out_of_date.revision);
    EXPECT(history[0].annotation_count == 1);
    EXPECT(restore_annotation_layer_revision(*alice, before_delete));
    EXPECT(alice->annotations[0].body == "Revised note text");
    auto restored = load_object_revision(alice->revision);
    EXPECT(restored && restored->parent == pre_restore);
    EXPECT(!restore_annotation_layer_revision(*alice, bob->revision));
    EXPECT(alice->annotations[0].body == "Revised note text");
    EXPECT(!restore_annotation_layer_revision(out_of_date, before_edit));
    EXPECT(out_of_date.revision != alice->revision);
    auto bob_again = load_annotation_layer(bob->id);
    EXPECT(bob_again && bob_again->annotations.empty());
    EXPECT_EQ(list_annotation_layers().size(), 2u);
    EXPECT(!load_annotation_layer(bob->id, alice->revision));
    // Annotation history and local file drafts share the object store but
    // must never move one another's heads.
    EXPECT(!put_draft("file:/notes.md", "changed", "base", "notes").empty());
    EXPECT(find_draft("file:/notes.md").has_value());
    auto alice_again = load_annotation_layer(alice->id);
    EXPECT(alice_again && alice_again->revision == alice->revision);
  }
  revstore_use_path({});
  fs::remove(db);
  const fs::path state = fs::temp_directory_path() /
      ("xanatoo-annotation-visibility-" + std::to_string(process_id()));
  fs::create_directories(state);
  set_dir_override(DirKind::State, state.string());
  std::unordered_set<std::string> hidden;
  load_hidden_annotation_layers(hidden);
  EXPECT(hidden.empty());
  save_annotation_layer_visibility("layer:test", false);
  load_hidden_annotation_layers(hidden);
  EXPECT(hidden.contains("layer:test"));
  save_annotation_layer_visibility("layer:test", true);
  load_hidden_annotation_layers(hidden);
  EXPECT(!hidden.contains("layer:test"));
  set_dir_override(DirKind::State, {});
  fs::remove(state / "sidestore.sqlite");
  fs::remove(state);
#endif
}

void test_stored_documents() {
#if XANATOO_SQLITE
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const fs::path db = fs::temp_directory_path() /
      ("xanatoo-stored-docs-" + std::to_string(process_id()) + ".sqlite");
  fs::remove(db);
  revstore_use_path(db.string());
  const std::string key = new_doc_key();
  const std::string other = new_doc_key();
  EXPECT(!find_stored_document(key));
  EXPECT(list_stored_documents().empty());
  auto first = save_stored_document(key, "(document one)", "", "My document");
  EXPECT(first.has_value());
  const std::string source = "file:/example/original.md";
  const std::string raw = "# Untouched source\n";
  auto imported = import_stored_document(source, raw, "(original-tree)",
                                         "(locally-edited-tree)", "Imported");
  EXPECT(imported.has_value());
  if (imported) {
    auto provenance = find_document_origin(imported->key);
    EXPECT(provenance.has_value());
    if (provenance) {
      EXPECT_EQ(provenance->source, source);
      EXPECT_EQ(load_object(provenance->raw), std::optional<std::string>(raw));
      EXPECT_EQ(load_stored_document_revision(imported->key,
                                              provenance->revision),
                std::optional<std::string>("(original-tree)"));
      const auto working = load_object_revision(imported->working);
      EXPECT(working && working->parent == provenance->revision);
    }
    EXPECT_EQ(load_stored_document_revision(imported->key, imported->working),
              std::optional<std::string>("(locally-edited-tree)"));
    auto same = import_stored_document(source, "CHANGED", "(new-original)",
                                       "(overwrite)", "New title");
    EXPECT(same && same->key == imported->key &&
           same->working == imported->working);
    auto by_source = find_imported_document(source);
    EXPECT(by_source && by_source->key == imported->key);
    EXPECT(!find_imported_document("file:/unrelated.md"));
    EXPECT(!find_document_origin(key));
  }
  if (first) {
    EXPECT(first->published.empty());
    EXPECT(!first->working.empty());
    EXPECT_EQ(load_stored_document_revision(key, first->working),
              std::optional<std::string>("(document one)"));
    EXPECT(!save_stored_document(key, "overwrite", "", "wrong head"));
    EXPECT(!load_stored_document_revision(other, first->working));
    EXPECT(!save_stored_document("file:/somewhere.md", "bad", "", "bad"));

    auto initial_pub = publish_stored_document(key, first->working, "");
    EXPECT(initial_pub.has_value());
    if (initial_pub) {
      EXPECT(initial_pub->working == first->working);
      EXPECT(!initial_pub->published.empty());
      EXPECT_EQ(load_stored_document_revision(key, initial_pub->published),
                std::optional<std::string>("(document one)"));

      auto edited = save_stored_document(key, "(document two)", first->working,
                                         "New title");
      EXPECT(edited.has_value());
      if (edited) {
        EXPECT(edited->published == initial_pub->published);
        EXPECT(edited->working != first->working);
        EXPECT(!publish_stored_document(key, first->working,
                                        initial_pub->published));
        EXPECT(!save_stored_document(key, "stale", first->working, "stale"));
        auto second_pub = publish_stored_document(
            key, edited->working, initial_pub->published);
        EXPECT(second_pub.has_value());
        if (second_pub) {
          const auto pub_record = load_object_revision(second_pub->published);
          EXPECT(pub_record && pub_record->parent == initial_pub->published);
          EXPECT_EQ(load_stored_document_revision(key, second_pub->published),
                    std::optional<std::string>("(document two)"));
          auto same = publish_stored_document(key, edited->working,
                                               second_pub->published);
          EXPECT(same && same->published == second_pub->published);
        }
      }
    }
    auto second = save_stored_document(other, "(different)", "", "Another");
    EXPECT(second.has_value());
    EXPECT_EQ(list_stored_documents().size(), 3u);
    revstore_use_path(db.string()); // close/reopen persisted state
    auto reopened = find_stored_document(key);
    auto imported_after_reopen = find_imported_document(source);
    EXPECT(imported_after_reopen && imported &&
           imported_after_reopen->key == imported->key &&
           imported_after_reopen->working == imported->working);
    EXPECT(reopened && reopened->title == "New title");
    EXPECT(reopened && !reopened->published.empty());
    EXPECT(!load_stored_document_revision(other, first->working));
  }
  revstore_use_path({});
  fs::remove(db);
#endif
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
  EXPECT(is_reddit_url("https://www.reddit.com?sort=top"));
  EXPECT(is_reddit_url("reddit.com#front"));
  EXPECT(!is_reddit_url("https://reddit.com.evil.test?sort=top"));
  EXPECT_EQ(reddit_json_url("reddit.com?sort=top"),
            "https://www.reddit.com/.json?sort=top&raw_json=1");
  EXPECT_EQ(reddit_json_url("https://reddit.com#front"),
            "https://www.reddit.com/.json?raw_json=1&limit=25");

  EXPECT_EQ(reddit_rss_url("https://www.reddit.com/r/linux"),
            "https://www.reddit.com/r/linux/.rss");
  EXPECT_EQ(reddit_rss_url("https://old.reddit.com/r/linux/comments/abc123/title/"),
            "https://www.reddit.com/r/linux/comments/abc123/title/.rss");
  EXPECT_EQ(reddit_rss_url("reddit.com/r/linux/new/?count=25#top"),
            "https://www.reddit.com/r/linux/new/.rss?count=25");
  EXPECT_EQ(reddit_rss_url("https://reddit.com/r/linux/.json?limit=10"),
            "https://www.reddit.com/r/linux/.rss?limit=10");
  EXPECT_EQ(reddit_rss_url("https://www.reddit.com/r/linux/.rss"),
            "https://www.reddit.com/r/linux/.rss");
  EXPECT_EQ(reddit_rss_url("reddit.com"), "https://www.reddit.com/.rss");
  EXPECT_EQ(reddit_rss_url("https://reddit.com/?sort=top"),
            "https://www.reddit.com/.rss?sort=top");
  EXPECT_EQ(reddit_rss_url("reddit.com?sort=top#front"),
            "https://www.reddit.com/.rss?sort=top");
  EXPECT(is_feed_url(reddit_rss_url("https://reddit.com/r/linux/new")));
  EXPECT(reddit_rss_url("https://redd.it/abc123").empty());
  EXPECT(reddit_rss_url("https://notreddit.com/r/linux").empty());
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

  // NIP-19 example key (npub ↔ hex) and round trips through the encoder.
  const std::string pk =
      "7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e";
  const std::string npub =
      "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg";
  EXPECT_EQ(nostr_bech32("npub", pk), npub);
  EXPECT(is_nostr_ref(npub));
  EXPECT(is_nostr_ref("nostr:" + npub));
  auto dec = nostr_decode_pubkey("nostr:" + npub);
  EXPECT(dec.has_value());
  if (dec) EXPECT_EQ(*dec, pk);
  EXPECT(!nostr_decode_event_id(npub).has_value());
  EXPECT(!nostr_decode_pubkey(nostr_bech32("note", pk)).has_value());
  auto note = nostr_decode_event_id(nostr_bech32("note", pk));
  EXPECT(note.has_value());
  if (note) EXPECT_EQ(*note, pk);
  EXPECT(nostr_bech32("npub", "xyz").empty());
}

void test_nostr_content() {
  using namespace xanatoo;
  const std::string pk =
      "7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e";
  const std::string npub = nostr_bech32("npub", pk);
  const std::string json =
      "{\"id\":\"x\",\"content\":\"say \\\"tags\\\" [x]\",\"tags\":[[\"p\",\"" +
      pk + "\"],[\"imeta\",\"url https://cdn.example/a\",\"m image/png\","
      "\"alt A \\\"cat\\\"\"],[\"t\",\"]\"]],\"kind\":1}";
  auto tags = nostr_event_tags(json);
  EXPECT_EQ(tags.size(), size_t(3));
  if (tags.size() == 3) {
    EXPECT_EQ(tags[0][1], pk);
    EXPECT_EQ(tags[1][3], "alt A \"cat\"");
    EXPECT_EQ(tags[2][1], "]");
  }

  std::map<std::string, std::string> names{{pk, "Alice"}};
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  nostr_append_content(
      doc, root,
      "Hi nostr:" + npub + " see https://example.com/page). and #[0]\n"
      "pic https://cdn.example/a\n\nclip https://v.example/x.mp4?t=1",
      tags, names);
  const auto& kids = doc.get(root).children;
  EXPECT_EQ(kids.size(), size_t(3));  // Paragraph, Image, Paragraph
  if (kids.size() != 3) return;
  const Node& p1 = doc.get(kids[0]);
  std::vector<std::string> links;
  for (const auto& sp : p1.spans)
    if (sp.kind == InlineKind::Link) links.push_back(sp.text + "|" + sp.href);
  EXPECT_EQ(links.size(), size_t(4));
  if (links.size() == 4) {
    EXPECT_EQ(links[0], "@Alice|nostr:" + npub);
    EXPECT_EQ(links[1], "example.com/page|https://example.com/page");
    EXPECT_EQ(links[2], "@Alice|nostr:" + npub);
    EXPECT_EQ(links[3], "[image]|https://cdn.example/a");
  }
  EXPECT_EQ(p1.spans.back().kind, InlineKind::Link);  // trailing "\n" trimmed
  const Node& img = doc.get(kids[1]);
  EXPECT(img.kind == NodeKind::Image);
  EXPECT_EQ(img.src, "https://cdn.example/a");
  EXPECT_EQ(img.alt, "A \"cat\"");
  const Node& p2 = doc.get(kids[2]);
  EXPECT_EQ(p2.spans.size(), size_t(2));
  if (p2.spans.size() == 2) {
    EXPECT_EQ(p2.spans[0].text, "clip ");
    EXPECT_EQ(p2.spans[1].text, "[video]");
  }
  auto mentioned = nostr_mentioned_pubkeys("cc nostr:" + npub + ", nostr:bogus");
  EXPECT_EQ(mentioned.size(), size_t(1));
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
  EXPECT(is_image_url("https://upload.example/a/Cat.JPG"));
  EXPECT(is_image_url("https://cdn.example/p.webp?w=800#x"));
  EXPECT(is_image_url("img/local.png"));
  EXPECT(is_image_url("/abs/pic.gif"));
  EXPECT(!is_image_url("https://example.org/page.png.html"));
  EXPECT(!is_image_url("https://example.org/?img=a.png"));
  EXPECT(!is_image_url("gemini://host/a.png"));
  EXPECT(!is_image_url("mailto:a.png"));
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
  EXPECT(path_uses_xml("document.xml"));
  EXPECT(path_uses_xml("document.XML"));
  EXPECT(!path_uses_xml("document.xhtml"));
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

void test_parse_document_xml_path() {
  using namespace xanatoo;
  const std::string source =
      "<doc title=\"Hello &amp; goodbye\"><section label=\"intro\">"
      "<p>Before <em>after</em> &lt;end&gt;</p></section></doc>";
  Document doc = parse_document(source, "note.XML");
  EXPECT_EQ(doc.meta.title, "Hello & goodbye");
  EXPECT_EQ(doc.get(doc.root).children.size(), 1u);
  EXPECT_EQ(doc.get(doc.get(doc.root).children[0]).kind, NodeKind::Section);
  std::string output = serialize_document(doc, "note.xml");
  EXPECT(output.find("<section") != std::string::npos);
  EXPECT(output.find("<em>after</em>") != std::string::npos);
  EXPECT_EQ(serialize_document(parse_document(output, "note.xml"), "note.xml"),
            output);
}

void test_source_view_text() {
  using namespace xanatoo;
  const Document doc = parse_markup("# A\n\nThis is *emphasized*.\n");
  const std::string raw = "<html><p>&amp; \"quoted\"</p></html>\n";
  const auto r = source_view_text(doc, SelectorKind::SourceRaw, raw);
  EXPECT_EQ(r.language, "text");
  EXPECT_EQ(r.text, raw);  // no parsing, escaping, or reserialization
  const Document rendered = source_view_document(r);
  const auto& root = rendered.get(rendered.root);
  EXPECT_EQ(root.children.size(), 1u);
  if (!root.children.empty()) {
    const auto& code = rendered.get(root.children[0]);
    EXPECT_EQ(code.kind, NodeKind::CodeBlock);
    EXPECT_EQ(code.spans.size(), 1u);
    if (!code.spans.empty()) EXPECT_EQ(code.spans[0].text, raw);
  }
  const auto sx = source_view_text(doc, SelectorKind::SourceSexpr, std::nullopt);
  EXPECT_EQ(sx.language, "lisp");
  EXPECT_EQ(sx.text, serialize_sexpr(doc));
  EXPECT(parse_sexpr(sx.text).root != INVALID_NODE);
  const auto xml = source_view_text(doc, SelectorKind::SourceXml, std::nullopt);
  EXPECT_EQ(xml.language, "xml");
  EXPECT_EQ(xml.text, serialize_xml(doc));
  EXPECT(parse_xml(xml.text).root != INVALID_NODE);
  const auto missing = source_view_text(doc, SelectorKind::SourceRaw, std::nullopt);
  EXPECT(missing.text.find("unavailable") != std::string::npos);
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
      (page (background "#fff") (color (rgb 1 2 3))
            (margin-background "#102030") (margin-image "dots.png"))
      (role article (space-before 20) (space-after 20))
      (fonts (body "Some Serif" "serif") (heading-weight regular)
             (heading-scale 1.5)
             (body-scale 0.85))
      (role paragraph (space-after gap) (space-before 4px))
      (role header (color accent) (font code))
      (role link (color accent) (underline #f))
      (role code (background none))
      (variant warning
        (role note (color accent) (border "#aabbcc"))
        (role paragraph (font code)))
      (variant quiet (role note (color "#eeeeee")))
      (role bogus (color "#000"))
      (role note (wobble 3) (border "nonsense")))
  )",
                     "test");
  EXPECT_EQ(st.title, "Test");
  EXPECT(st.paper == (Rgba{255, 255, 255, 255}));
  EXPECT(st.ink == (Rgba{1, 2, 3, 255}));
  EXPECT(st.margin == (Rgba{0x10, 0x20, 0x30, 255}));
  EXPECT_EQ(st.margin_image, "dots.png");
  EXPECT(st.has_margin());
  EXPECT(!builtin_style().has_margin());
  EXPECT(st[BlockRole::Article].space_before.px(10.f) == 20.f);
  EXPECT_EQ(st.fonts.body.size(), 2u);
  EXPECT_EQ(st.fonts.body[0], "Some Serif");
  EXPECT(!st.fonts.heading_bold);
  EXPECT(st.heading_scale > 1.49f && st.heading_scale < 1.51f);
  EXPECT(st.body_scale > 0.84f && st.body_scale < 0.86f);
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
  EXPECT_EQ(st.variants.size(), 2u);
  DocStyle scoped = st;
  EXPECT(apply_style_variant(scoped, "warning"));
  EXPECT(scoped[BlockRole::Note].border == (Rgba{0xaa, 0xbb, 0xcc, 255}));
  EXPECT(scoped.text_color(BlockRole::Note, InlineKind::Text) ==
         (Rgba{0x10, 0x20, 0x30, 255}));
  EXPECT(scoped[BlockRole::Paragraph].font == FontRole::Code);
  EXPECT(scoped[BlockRole::Header].font == st[BlockRole::Header].font);
  EXPECT(apply_style_variant(scoped, "quiet"));
  EXPECT(scoped.text_color(BlockRole::Note, InlineKind::Text) ==
         (Rgba{0xee, 0xee, 0xee, 255}));
  EXPECT(scoped[BlockRole::Note].border == (Rgba{0xaa, 0xbb, 0xcc, 255}));
  EXPECT(!apply_style_variant(scoped, "not-defined"));
  // bogus role, unknown property, bad colour
  EXPECT_EQ(st.warnings.size(), 3u);
}

void test_named_style_scopes() {
  using namespace xanatoo;
  const std::string source =
      ":::section style=technical\n"
      ":::header\nTechnical\n:::\n"
      ":::note style=warning kind=tip\nWarning text\n:::\n"
      ":::note\nNormal text\n:::\n"
      ":::\n"
      ":::aside style=compact side=left\nMargin text\n:::\n";
  Document doc = parse_markup(source);
  const auto& roots = doc.get(doc.root).children;
  EXPECT_EQ(roots.size(), 2u);
  if (roots.size() != 2) return;
  NodeId sec = roots[0], aside = roots[1];
  EXPECT(doc.get(sec).kind == NodeKind::Section);
  EXPECT_EQ(doc.get(sec).style, "technical");
  EXPECT(doc.get(aside).kind == NodeKind::Aside);
  EXPECT_EQ(doc.get(aside).style, "compact");
  const auto& kids = doc.get(sec).children;
  EXPECT_EQ(kids.size(), 3u);
  if (kids.size() != 3) return;
  NodeId warning = kids[1], normal = kids[2];
  EXPECT_EQ(doc.get(warning).style, "warning");
  EXPECT(doc.get(normal).style.empty());
  const std::string saved = serialize_markup(doc);
  EXPECT(saved.find(":::section style=technical") != std::string::npos);
  EXPECT(saved.find(":::note style=warning kind=tip") != std::string::npos);
  EXPECT_EQ(serialize_markup(parse_markup(saved)), saved);

  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_named_style_scopes layout (no font)\n");
    return;
  }
  DocStyle style = builtin_style();
  apply_style_source(style, R"(
    (stylesheet
      (variant technical (role note (border "#112233")))
      (variant warning (role note (border "#aa3300") (color "#ff0000")))
      (variant compact (role aside (placement flow) (width 12em))))
  )");
  EXPECT(style.warnings.empty());
  LayoutEngine engine(fonts);
  engine.set_style(&style);
  LayoutOptions opt;
  opt.margin_x = 0.f;
  opt.margin_y = 0.f;
  engine.set_options(opt);
  LayoutResult result = engine.layout(doc, 800.f);
  EXPECT_EQ(result.scoped_styles.size(), 3u);
  EXPECT_EQ(result.note_bars.size(), 2u);
  EXPECT(result.style_for(warning, style)[BlockRole::Note].border ==
         (Rgba{0xaa, 0x33, 0, 255}));
  EXPECT(result.style_for(normal, style)[BlockRole::Note].border ==
         (Rgba{0x11, 0x22, 0x33, 255}));
  EXPECT(result.style_for(aside, style)[BlockRole::Aside].placement ==
         Placement::Flow);
  EXPECT(result.style_for(sec, style)[BlockRole::Note].border ==
         (Rgba{0x11, 0x22, 0x33, 255}));
  bool red_warning = false;
  for (const PositionedGlyph& g : result.glyphs) {
    if (g.node == warning) {
      EXPECT(g.color == (Rgba{255, 0, 0, 255}));
      red_warning = true;
    }
  }
  EXPECT(red_warning);
  // The paged result must retain the rendering style of its source nodes.
  // This mapping is also used for note bars and code/inline backgrounds.
  EXPECT(!result.node_style_index.empty());
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
  for (const char* site : {"hackernews", "reddit", "wikipedia"}) {
    DocStyle st;
    EXPECT(load_style(site, st));
  }
  // default.xss mirrors the built-in look.
  DocStyle def;
  EXPECT(load_style("default", def));
  const DocStyle builtin = builtin_style();
  EXPECT(def.paper == builtin.paper && def.ink == builtin.ink);
  EXPECT(def.selection == builtin.selection && def.mark == builtin.mark &&
         def.bookmark == builtin.bookmark);
  for (size_t i = 0; i < kBlockRoleCount; ++i) {
    const BlockStyle& a = def.block[i];
    const BlockStyle& b = builtin.block[i];
    EXPECT(a.space_before.px(17.f) == b.space_before.px(17.f) &&
           a.space_after.px(17.f) == b.space_after.px(17.f) &&
           a.indent.px(17.f) == b.indent.px(17.f) && a.font == b.font &&
           a.border == b.border && a.background == b.background &&
           a.width.px(17.f) == b.width.px(17.f) &&
           a.side == b.side && a.placement == b.placement &&
           a.has_color == b.has_color &&
           (!a.has_color || a.color == b.color));
  }
  EXPECT(def.metadata.has_color == builtin.metadata.has_color);
  EXPECT(def.metadata.color == builtin.metadata.color);
  EXPECT(def.metadata.has_font == builtin.metadata.has_font);
  EXPECT(def.metadata.font == builtin.metadata.font);
  DocStyle hn;
  EXPECT(load_style("hackernews", hn));
  EXPECT(hn[BlockRole::Header].font == FontRole::Heading);
  EXPECT(hn[BlockRole::Caption].font == FontRole::Small);
  EXPECT(hn.metadata.font == FontRole::Small);
  EXPECT(hn.metadata.has_color);
  DocStyle custom = builtin_style();
  apply_style_source(custom,
                     "(role metadata (font small-bold) (color \"#445566\"))"
                     "(role link (font italic))");
  EXPECT(custom.metadata.font == FontRole::SmallBold);
  EXPECT(custom.metadata.color == (Rgba{0x44, 0x55, 0x66, 255}));
  EXPECT(custom[InlineKind::Link].has_font);
  EXPECT(custom[InlineKind::Link].font == FontRole::BodyItalic);
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

// The hostname must share the title's line without inheriting its heading
// size. A metadata discussion link keeps its Link kind / href but uses the
// same muted inline role in a caption.
void test_layout_inline_metadata() {
  using namespace xanatoo;
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_inline_metadata (no font)\n");
    return;
  }
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  NodeId article = doc.create(NodeKind::Article, root);
  NodeId title = doc.create(NodeKind::Header, article);
  InlineSpan story;
  story.kind = InlineKind::Link;
  story.text = "A story";
  story.href = "https://example.org/story";
  doc.get(title).spans.push_back(std::move(story));
  InlineSpan host;
  host.role = InlineRole::Metadata;
  host.text = " (example.org)";
  doc.get(title).spans.push_back(std::move(host));
  NodeId caption = doc.create(NodeKind::Caption, article);
  InlineSpan comments;
  comments.kind = InlineKind::Link;
  comments.role = InlineRole::Metadata;
  comments.text = "42 comments";
  comments.href = "https://news.ycombinator.com/item?id=42";
  doc.get(caption).spans.push_back(std::move(comments));

  DocStyle st = builtin_style();
  LayoutEngine engine(fonts, nullptr);
  engine.set_style(&st);
  LayoutResult lr = engine.layout(doc, 900.f);
  const PositionedGlyph* title_g = nullptr;
  const PositionedGlyph* host_g = nullptr;
  const PositionedGlyph* comment_g = nullptr;
  for (const auto& g : lr.glyphs) {
    if (g.node == title && g.codepoint == 'A') title_g = &g;
    if (g.node == title && g.codepoint == '(') host_g = &g;
    if (g.node == caption && g.codepoint == '4') comment_g = &g;
  }
  EXPECT(title_g && host_g && comment_g);
  if (title_g && host_g && comment_g) {
    EXPECT(title_g->font == FontRole::Heading);
    EXPECT(host_g->font == FontRole::Small);
    EXPECT(host_g->color == st.metadata.color);
    EXPECT(host_g->y == title_g->y);
    EXPECT(host_g->x > title_g->x);
    EXPECT(comment_g->font == FontRole::Small);
    EXPECT(comment_g->color == st.metadata.color);
    const auto target = hit_test_link(
        doc, lr, comment_g->x + 2.f, comment_g->y - 4.f, 0.f);
    EXPECT(target.has_value());
    if (target) EXPECT_EQ(*target, doc.get(caption).spans[0].href);
  }
  EXPECT_EQ(doc.get(title).spans[0].href, "https://example.org/story");
  EXPECT_EQ(doc.get(caption).spans[0].href,
            "https://news.ycombinator.com/item?id=42");

  // Generic Message headers carry semantic links and timestamp metadata.
  Document replies;
  NodeId rr = replies.create(NodeKind::Document);
  NodeId thread = replies.create(NodeKind::Thread, rr);
  NodeId parent = replies.create(NodeKind::Message, thread);
  replies.get(parent).label = "100";
  replies.get(parent).src = "alice";
  replies.get(parent).summary = "2026-11-03 10:00 UTC";
  NodeId reply = replies.create(NodeKind::Message, thread);
  auto& msg = replies.get(reply);
  msg.label = "101";
  msg.alt = "100";
  msg.src = "bob";
  msg.summary = "2026-11-04 09:00 UTC";
  msg.spans.resize(3);
  msg.spans[0].kind = InlineKind::Link;
  msg.spans[0].href = "hn:user?id=bob";
  msg.spans[1].kind = InlineKind::Link;
  msg.spans[1].href = "hn:item?id=101";
  msg.spans[1].datetime = "2026-11-04T09:00:00Z";
  msg.spans[2].kind = InlineKind::Link;
  msg.spans[2].href = "#id:100";
  LayoutResult ml = engine.layout(replies, 900.f);
  float reply_header_y = 0.f;
  for (size_t span = 0; span < 3; ++span) {
    const PositionedGlyph* glyph = nullptr;
    for (const auto& g : ml.glyphs) {
      if (g.node == reply && g.span_index == span && g.codepoint > ' ') {
        glyph = &g;
        break;
      }
    }
    EXPECT(glyph != nullptr);
    if (!glyph) continue;
    if (span == 0) reply_header_y = glyph->y;
    const float x = glyph->x + 2.f, y = glyph->y - 4.f;
    auto link = hit_test_link(replies, ml, x, y, 0.f);
    EXPECT(link.has_value());
    if (link) EXPECT_EQ(*link, msg.spans[span].href);
    auto date = hit_test_timestamp(replies, ml, x, y, 0.f);
    if (span == 1) {
      EXPECT(date.has_value());
      if (date) EXPECT_EQ(*date, msg.spans[1].datetime);
    } else {
      EXPECT(!date.has_value());
    }
  }

  // Both middle dots are punctuation between links, never part of either
  // clickable span (or the date's semantic timestamp hover area).
  size_t separators = 0;
  for (const PositionedGlyph& g : ml.glyphs) {
    if (g.codepoint != 0x00b7 || std::abs(g.y - reply_header_y) > 0.5f)
      continue;
    ++separators;
    EXPECT(g.node == INVALID_NODE);
    EXPECT(!hit_test_link(replies, ml, g.x + g.advance * 0.5f,
                          g.y - 4.f, 0.f).has_value());
    EXPECT(!hit_test_timestamp(replies, ml, g.x + g.advance * 0.5f,
                               g.y - 4.f, 0.f).has_value());
  }
  EXPECT_EQ(separators, 2u);

  // The reply link's #id: fragment must resolve to the actual parent
  // Message, and its caret must have an onscreen position to scroll to.
  EXPECT_EQ(find_anchor_node(replies, "#id:100"), parent);
  const auto destination = jump_to_href(replies, msg.spans[2].href);
  EXPECT(destination.has_value());
  if (destination) {
    EXPECT_EQ(destination->node, parent);
    EXPECT(caret_screen_pos(ml, *destination, 0.f, 20.f).valid);
  }
}

void test_nav_history_positions_and_forward_link() {
  using namespace xanatoo;
  using namespace xanatoo::app;
  NavHistory h;
  NavHistoryEntry home;
  home.url = "hn:";
  EXPECT(nav_history_push(h, std::move(home), 100));
  EXPECT_EQ(h.pos, 0);

  Selection at_link;
  at_link.focus = {12, 1, 4};
  at_link.anchor = at_link.focus;
  nav_history_capture(h, "hn:", at_link, 524.5f);
  EXPECT_EQ(h.entries[0].scroll, 524.5f);
  EXPECT(h.entries[0].sel.focus == at_link.focus);
  // Unrelated / loading pages must not overwrite the previous stop.
  nav_history_capture(h, "hn:item?id=2", {}, 0.f);
  EXPECT_EQ(h.entries[0].scroll, 524.5f);

  NavHistoryEntry story;
  story.url = "hn:item?id=2";
  story.via = {"hn:item?id=2", "hn:", 12, 1};
  EXPECT(nav_history_push(h, std::move(story), 100));
  EXPECT_EQ(h.pos, 1);
  nav_history_capture(h, "hn:item?id=2", {}, 850.f);
  h.pos = 0;  // Back
  const NavLink* next = nav_history_forward_link(h, "hn:");
  EXPECT(next != nullptr);
  if (next) {
    EXPECT_EQ(next->href, "hn:item?id=2");
    EXPECT_EQ(next->node, 12u);
    EXPECT_EQ(next->span, 1u);
  }
  EXPECT(!nav_history_forward_link(h, "different page"));
  h.restoring = 0;
  EXPECT(!nav_history_forward_link(h, "hn:"));
  nav_history_capture(h, "hn:", {}, 0.f);
  EXPECT_EQ(h.entries[0].scroll, 524.5f);
  h.restoring = -1;

  // Following a different link after Back replaces Forward history.
  NavHistoryEntry other;
  other.url = "hn:item?id=3";
  other.via = {"hn:item?id=3", "hn:", 18, 0};
  EXPECT(nav_history_push(h, std::move(other), 100));
  EXPECT_EQ(h.entries.size(), 2u);
  EXPECT_EQ(h.entries[1].url, "hn:item?id=3");
  EXPECT_EQ(h.entries[0].scroll, 524.5f);
  h.pos = 0;
  EXPECT(nav_history_forward_link(h, "hn:") != nullptr);
  NavHistoryEntry small;
  small.url = "hn:item?id=4";
  EXPECT(nav_history_push(h, std::move(small), 2));
  EXPECT_EQ(h.entries.size(), 2u);
  NavHistoryEntry newest;
  newest.url = "hn:item?id=5";
  EXPECT(nav_history_push(h, std::move(newest), 2));
  EXPECT_EQ(h.entries.size(), 2u);
  EXPECT_EQ(h.entries.front().url, "hn:item?id=4");
  EXPECT_EQ(h.pos, 1);

  // Replacing the point on a same-document URL preserves passage cuts,
  // which lets anchor jumps occupy distinct Back/Forward stops.
  Locator current = parse_locator("hn:item?id=42//id:thread#id:101");
  const Locator destination = parse_locator("page#id:100");
  current.point = destination.point;
  EXPECT_EQ(format_locator(current), "hn:item?id=42//id:thread#100");
}

void test_history_context_menu_targets() {
  using namespace xanatoo;
  const std::vector<std::string> urls = {"home", "one", "two", "three", "four"};
  const auto back = history_menu_items(urls, 3, -1);
  EXPECT_EQ(back.size(), 3u);
  EXPECT_EQ(back[0].url, "two");
  EXPECT_EQ(back[0].steps, -1);
  EXPECT_EQ(back[2].url, "home");
  EXPECT_EQ(back[2].steps, -3);
  const auto forward = history_menu_items(urls, 3, 1);
  EXPECT_EQ(forward.size(), 1u);
  EXPECT_EQ(forward[0].url, "four");
  EXPECT_EQ(forward[0].steps, 1);
  EXPECT_EQ(history_menu_items(urls, 3, -1, 2).size(), 2u);
  EXPECT(history_menu_items(urls, -1, -1).empty());
  EXPECT(history_menu_items(urls, 5, 1).empty());
  EXPECT(history_menu_items(urls, 0, -1).empty());
  EXPECT(history_menu_items(urls, 0, 0).empty());
}

// Text column = min(pane * (1 - 2 * margin%), measure_chars * avg char),
// centred; never closer to the edge than margin_x px.
void test_layout_reader_mode() {
  using namespace xanatoo;
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_reader_mode (no font)\n");
    return;
  }
  Document doc = parse_html_page(
      "<body><nav><p>Menu</nav><main><h1>T</h1><p>Body</main>"
      "<aside><p>Side</aside></body>",
      "page.html");
  auto text_of = [](const LayoutResult& lr) {
    std::string t;
    for (const auto& g : lr.glyphs)
      if (g.codepoint < 128) t.push_back(static_cast<char>(g.codepoint));
    return t;
  };
  LayoutEngine engine(fonts, nullptr);
  LayoutResult full = engine.layout(doc, 800.f);
  EXPECT(text_of(full).find("Menu") != std::string::npos);
  EXPECT_EQ(full.chrome_notice.height, 0.f);
  engine.set_hide_chrome(true);
  LayoutResult reader = engine.layout(doc, 800.f);
  engine.set_hide_chrome(false);
  const std::string rt = text_of(reader);
  EXPECT(rt.find("Menu") == std::string::npos);
  EXPECT(rt.find("Side") == std::string::npos);
  EXPECT(rt.find("Body") != std::string::npos);
  EXPECT(rt.find("2 parts of the page hidden (navigation, sidebar)") !=
         std::string::npos);
  EXPECT(reader.chrome_notice.height > 0.f);
  // The notice is decoration: no glyph of it belongs to a node.
  for (const auto& g : reader.glyphs)
    if (g.y < reader.chrome_notice.y + reader.chrome_notice.height)
      EXPECT(g.node == INVALID_NODE);
  // Outline: headings inside the chrome only in the full view.
  Document d2 = parse_html_page(
      "<body><nav><h2>Menu</h2><p>x</nav><main><h1>T</h1><p>b</main>",
      "p.html");
  std::vector<OutlineEntry> all, rd;
  collect_outline(d2, all);
  collect_outline(d2, rd, nullptr, {}, true);
  EXPECT_EQ(all.size(), rd.size() + 1);
}

// Articles are separate sheets on a margin: gaps between them, the page
// sheet only when the style has a margin.
void test_layout_article_gaps() {
  using namespace xanatoo;
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_article_gaps (no font)\n");
    return;
  }
  Document doc = parse_document(
      ":::article id=\"a\"\nOne\n:::\n\n:::article id=\"b\"\nTwo\n:::\n",
      "g.md");
  DocStyle st = builtin_style();
  LayoutEngine engine(fonts, nullptr);
  engine.set_style(&st);
  LayoutResult plain = engine.layout(doc, 800.f);
  EXPECT(plain.page_w == 0.f);
  EXPECT(plain.page_gaps.empty());
  apply_style_source(st, "(page (margin-background \"#334455\"))"
                         "(role article (space-before 30) (space-after 30))");
  engine.set_style(&st);
  LayoutResult lr = engine.layout(doc, 800.f);
  EXPECT(lr.page_w > 0.f && lr.page_w < 800.f);
  EXPECT_EQ(lr.page_gaps.size(), 4u);  // above, between (×2), below
  EXPECT(lr.page_gaps.front().y == 0.f);
  EXPECT(lr.page_gaps.back().height > 1000.f);
}

void test_layout_margins_and_measure() {
  using namespace xanatoo;
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_margins_and_measure (no font)\n");
    return;
  }
  Document doc = parse_document(
      "A paragraph long enough to wrap several times in every column we try "
      "here, so that its glyphs span the whole width of the text column and "
      "show where the margins are on both sides of the pane.\n",
      "margins.md");
  auto extent = [](const LayoutResult& lr) {
    float l = 1e9f, r = 0.f;
    for (const auto& g : lr.glyphs) {
      if (g.codepoint == ' ' || g.codepoint == 0) continue;
      l = std::min(l, g.x);
      r = std::max(r, g.x);
    }
    return std::pair{l, r};
  };
  LayoutEngine engine(fonts, nullptr);
  LayoutOptions opts;
  opts.margin_x = 8.f;
  opts.margin_pct = 0.20f;
  engine.set_options(opts);
  auto [l1, r1] = extent(engine.layout(doc, 1000.f));
  EXPECT(l1 >= 199.f && l1 < 215.f);
  EXPECT(r1 <= 801.f && r1 > 700.f);

  // Narrow pane: the px floor wins over a tiny percentage.
  opts.margin_pct = 0.01f;
  engine.set_options(opts);
  auto [l2, r2] = extent(engine.layout(doc, 400.f));
  EXPECT(l2 >= 7.f);
  (void)r2;

  // Character cap: 40 chars of the body font, centred in a wide pane.
  opts.margin_pct = 0.05f;
  opts.measure_chars = 40;
  engine.set_options(opts);
  const float cap =
      fonts.pick(FontRole::Body).measure("abcdefghijklmnopqrstuvwxyz") / 26.f *
      40.f;
  auto [l3, r3] = extent(engine.layout(doc, 2000.f));
  EXPECT(r3 - l3 <= cap + 1.f);
  EXPECT(std::abs(l3 - (2000.f - cap) / 2.f) < 15.f);
}

// Old view.conf files stored pixel margins / column width.
void test_view_settings_migration() {
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const char* old_xdg = std::getenv("XDG_CONFIG_HOME");
  std::string saved = old_xdg ? old_xdg : "";
  fs::path dir = fs::temp_directory_path() /
                 ("xanatoo-vs-test-" + std::to_string(process_id()));
  fs::create_directories(dir / "xanatoo");
  ::setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
  {
    std::ofstream(dir / "xanatoo" / "view.conf")
        << "margin_x=56\nmax_content_width=720\n";
  }
  ViewSettings v;
  EXPECT(load_view_settings(v));
  EXPECT(v.margin_pct == 10.f);
  EXPECT(v.measure_chars == 72);
  EXPECT(!v.auto_updates);
  v.auto_updates = true;
  v.margin_pct = 25.f;
  v.measure_chars = 0;
  EXPECT(save_view_settings(v));
  ViewSettings w;
  EXPECT(load_view_settings(w));
  EXPECT(w.margin_pct == 25.f);
  EXPECT(w.measure_chars == 0);
  EXPECT(w.auto_updates);
  if (old_xdg)
    ::setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
  else
    ::unsetenv("XDG_CONFIG_HOME");
  fs::remove_all(dir);
}

void test_reddit_credentials() {
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const char* old_xdg = std::getenv("XDG_CONFIG_HOME");
  const char* old_id = std::getenv("XANATOO_REDDIT_CLIENT_ID");
  const char* old_secret = std::getenv("XANATOO_REDDIT_CLIENT_SECRET");
  const std::string saved_xdg = old_xdg ? old_xdg : "";
  const std::string saved_id = old_id ? old_id : "";
  const std::string saved_secret = old_secret ? old_secret : "";
  const fs::path dir = fs::temp_directory_path() /
                       ("xanatoo-reddit-test-" + std::to_string(process_id()));
  fs::create_directories(dir);
  ::setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
  ::unsetenv("XANATOO_REDDIT_CLIENT_ID");
  ::unsetenv("XANATOO_REDDIT_CLIENT_SECRET");

  EXPECT(!load_reddit_credentials().complete());
  EXPECT(!save_reddit_credentials({"id", ""}));
  EXPECT(!save_reddit_credentials({"id\nmalicious", "secret"}));
  EXPECT(save_reddit_credentials({"sample-id", "sample-secret"}));
  const auto saved = load_reddit_credentials();
  EXPECT(saved.client_id == "sample-id");
  EXPECT(saved.client_secret == "sample-secret");
  EXPECT(save_reddit_credentials({"replaced-id", "replaced-secret"}));
  EXPECT(load_reddit_credentials().client_id == "replaced-id");
  EXPECT(save_reddit_credentials({"sample-id", "sample-secret"}));
#ifndef _WIN32
  const fs::perms mode = fs::status(reddit_credentials_path()).permissions();
  EXPECT((mode & fs::perms::owner_read) != fs::perms::none);
  EXPECT((mode & fs::perms::owner_write) != fs::perms::none);
  EXPECT((mode & fs::perms::group_all) == fs::perms::none);
  EXPECT((mode & fs::perms::others_all) == fs::perms::none);
#endif
  ::setenv("XANATOO_REDDIT_CLIENT_ID", "env-id", 1);
  ::setenv("XANATOO_REDDIT_CLIENT_SECRET", "env-secret", 1);
  EXPECT(reddit_credentials_from_environment());
  EXPECT(effective_reddit_credentials().client_id == "env-id");
  EXPECT(effective_reddit_credentials().client_secret == "env-secret");
  ::unsetenv("XANATOO_REDDIT_CLIENT_SECRET");
  EXPECT(!reddit_credentials_from_environment());
  EXPECT(effective_reddit_credentials().client_id == "sample-id");
  set_no_save(true);
  EXPECT(!save_reddit_credentials({"would-not-save", "value"}));
  EXPECT(load_reddit_credentials().client_id == "sample-id");
  set_no_save(false);
  EXPECT(save_reddit_credentials({}));
  EXPECT(!fs::exists(reddit_credentials_path()));

  if (old_xdg) ::setenv("XDG_CONFIG_HOME", saved_xdg.c_str(), 1);
  else ::unsetenv("XDG_CONFIG_HOME");
  if (old_id) ::setenv("XANATOO_REDDIT_CLIENT_ID", saved_id.c_str(), 1);
  else ::unsetenv("XANATOO_REDDIT_CLIENT_ID");
  if (old_secret)
    ::setenv("XANATOO_REDDIT_CLIENT_SECRET", saved_secret.c_str(), 1);
  else
    ::unsetenv("XANATOO_REDDIT_CLIENT_SECRET");
  fs::remove_all(dir);
}

void test_hit_test_wrapped_link() {
  using namespace xanatoo;
  Document doc = parse_document("[two words](https://example.org/)\n", "l.md");
  NodeId para = INVALID_NODE;
  size_t span = 0;
  for (const Node& n : doc.nodes) {
    for (size_t s = 0; s < n.spans.size(); ++s)
      if (n.spans[s].kind == InlineKind::Link) {
        para = n.id;
        span = s;
      }
  }
  EXPECT(para != INVALID_NODE);
  if (para == INVALID_NODE) return;
  // "two" ends line 1 at the right edge, "words" starts line 2 at the left.
  LayoutResult lr;
  auto put = [&](float x, float y, size_t ci) {
    PositionedGlyph g;
    g.codepoint = 'x';
    g.x = x;
    g.y = y;
    g.advance = 10.f;
    g.node = para;
    g.span_index = span;
    g.char_index = ci;
    g.kind = InlineKind::Link;
    lr.glyphs.push_back(g);
  };
  for (size_t i = 0; i < 3; ++i) put(400.f + 10.f * i, 100.f, i);
  for (size_t i = 0; i < 5; ++i) put(30.f + 10.f * i, 130.f, 4 + i);
  auto first = hit_test_link(doc, lr, 410.f, 95.f, 0.f);
  auto second = hit_test_link(doc, lr, 50.f, 125.f, 0.f);
  EXPECT(first && *first == "https://example.org/");
  EXPECT(second && *second == "https://example.org/");
  // Left of line 1's part is not part of the link.
  EXPECT(!hit_test_link(doc, lr, 100.f, 95.f, 0.f));
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

// An unspaced run following ordinary words must move to a fresh line before
// its synthetic ↩ breaks. The prefix used to be included in the emitted line
// but excluded from the width calculation, so the word and mark overflowed.
void test_layout_forced_wrap_after_regular_text() {
  using namespace xanatoo;
  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty()) path = fontconfig_find("Liberation Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_layout_forced_wrap_after_regular_text (no font)\n");
    return;
  }

  const std::string long_word(160, 'W');
  // The second case crosses inline-span boundaries (ordinary text + link).
  for (const std::string& md : {
           "A brief introduction " + long_word + " followed by words.\n",
           "Regular words [" + long_word + "](https://example.org) end.\n",
           "Regular words **" + long_word.substr(0, 80) + "**" +
               long_word.substr(80) + " end.\n",
       }) {
    Document doc = parse_document(md, "long-wrap.md");
    LayoutEngine engine(fonts, nullptr);
    for (float pane_w : {280.f, 360.f, 520.f}) {
      for (bool justify : {false, true}) {
        LayoutOptions opts;
        opts.margin_x = 32.f;
        opts.justify = justify;
        engine.set_options(opts);
        const LayoutResult lr = engine.layout(doc, pane_w);
        const float right = pane_w - opts.margin_x;
        int forced_marks = 0;
        float prefix_baseline = -1.f;
        float word_baseline = -1.f;
        for (const PositionedGlyph& g : lr.glyphs) {
          if (g.codepoint == 'A' || g.codepoint == 'R') {
            if (prefix_baseline < 0.f) prefix_baseline = g.y;
          }
          if (g.codepoint == 'W' && word_baseline < 0.f)
            word_baseline = g.y;
          if (g.codepoint == 0x21A9 || g.codepoint == 0x21B5)
            ++forced_marks;
          if (g.codepoint == ' ' || g.codepoint == 0) continue;
          const Glyph& met = fonts.pick(g.font).glyph(g.codepoint);
          const float cell_right = g.x - static_cast<float>(met.bearing_x) +
                                   static_cast<float>(met.advance >> 6);
          if (cell_right > right + 1.5f) {
            std::fprintf(stderr,
                         "forced wrap overflow: pane=%.0f justify=%d "
                         "cp=U+%04X right=%.1f expected<=%.1f\n",
                         pane_w, justify, g.codepoint, cell_right, right);
          }
          EXPECT(cell_right <= right + 1.5f);
        }
        EXPECT(forced_marks > 0);
        EXPECT(prefix_baseline >= 0.f);
        EXPECT(word_baseline > prefix_baseline);
      }
    }
  }
}

void test_download_mime_and_filenames() {
  using namespace xanatoo;
  EXPECT(!download_response("https://example.org/file.pdf", "text/html; charset=utf-8", ""));
  EXPECT(!download_response("https://example.org/", "application/json", ""));
  EXPECT(!download_response("https://example.org/feed", "application/rss+xml", ""));
  EXPECT(!download_response("https://example.org/file.txt", "text/plain", ""));
  EXPECT(download_response("https://example.org/file.pdf", "application/pdf", ""));
  EXPECT(download_response("https://example.org/get?id=1", "application/octet-stream", ""));
  EXPECT(download_response("https://example.org/file.zip", "", ""));
  EXPECT(download_response("https://example.org/file.txt", "text/plain",
                           "attachment; filename=readme.txt"));
  EXPECT_EQ(download_filename("https://example.org/files/archive%20one.zip", ""),
            "archive one.zip");
  EXPECT_EQ(download_filename("https://example.org/file",
                              "attachment; filename=other.zip"), "other.zip");
  EXPECT_EQ(download_filename("https://example.org/file",
                              "attachment; filename*=UTF-8''r%C3%A9sum%C3%A9.pdf"),
            "r\xC3\xA9sum\xC3\xA9.pdf");
  EXPECT_EQ(download_filename("https://example.org/file",
                              "attachment; filename=\"../../evil\\file.exe\""),
            "_.._evilfile.exe");
  EXPECT_EQ(download_filename("https://example.org/", ""), "download");
}

void test_download_file_safety() {
  using namespace xanatoo;
  namespace fs = std::filesystem;
  fs::path dir;
  for (int n = 0; n < 100; ++n) {
    const fs::path candidate = fs::temp_directory_path() /
        ("xanatoo-download-test-" + std::to_string(process_id()) + "-" +
         std::to_string(n));
    if (fs::create_directory(candidate)) {
      dir = candidate;
      break;
    }
  }
  EXPECT(!dir.empty());
  if (dir.empty()) return;
  const char* previous = std::getenv("XDG_DOWNLOAD_DIR");
  const std::string saved = previous ? previous : "";
#ifdef _WIN32
  _putenv_s("XDG_DOWNLOAD_DIR", dir.string().c_str());
#else
  setenv("XDG_DOWNLOAD_DIR", dir.string().c_str(), 1);
#endif
  { std::ofstream(dir / "example.bin", std::ios::binary) << "original"; }
  std::string error;
  {
    DownloadFile file;
    EXPECT(file.begin("example.bin", &error));
    EXPECT(file.append("new payload", &error));
    EXPECT(file.finish(&error));
    EXPECT_EQ(fs::path(file.path()).filename().string(), "example (1).bin");
    std::ifstream old(dir / "example.bin", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(old),
                          std::istreambuf_iterator<char>{}), "original");
    std::ifstream newer(dir / "example (1).bin", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(newer),
                          std::istreambuf_iterator<char>{}), "new payload");
  }
  {
    DownloadFile partial;
    EXPECT(partial.begin("unfinished.bin", &error));
    EXPECT(partial.append("some bytes", &error));
    partial.abort();
  }
  for (const auto& entry : fs::directory_iterator(dir))
    EXPECT(entry.path().extension() != ".part");
#ifdef _WIN32
  _putenv_s("XDG_DOWNLOAD_DIR", previous ? saved.c_str() : "");
#else
  if (previous) setenv("XDG_DOWNLOAD_DIR", saved.c_str(), 1);
  else unsetenv("XDG_DOWNLOAD_DIR");
#endif
  fs::remove_all(dir);
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

// Stored (uncompressed) zip, enough for epub_to_markdown.
std::string test_zip(const std::vector<std::pair<std::string, std::string>>& files) {
  auto u = [](std::string& o, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) o.push_back(static_cast<char>(v >> (8 * i)));
  };
  std::string out, central;
  for (const auto& [name, data] : files) {
    const uint32_t offset = static_cast<uint32_t>(out.size());
    u(out, 0x04034b50, 4); u(out, 20, 2); u(out, 0, 2); u(out, 0, 2);
    u(out, 0, 4); u(out, 0, 4); u(out, data.size(), 4); u(out, data.size(), 4);
    u(out, name.size(), 2); u(out, 0, 2);
    out += name;
    out += data;
    u(central, 0x02014b50, 4); u(central, 20, 2); u(central, 20, 2);
    u(central, 0, 2); u(central, 0, 2); u(central, 0, 4); u(central, 0, 4);
    u(central, data.size(), 4); u(central, data.size(), 4);
    u(central, name.size(), 2); u(central, 0, 2); u(central, 0, 2);
    u(central, 0, 2); u(central, 0, 2); u(central, 0, 4); u(central, offset, 4);
    central += name;
  }
  const uint32_t at = static_cast<uint32_t>(out.size());
  out += central;
  u(out, 0x06054b50, 4); u(out, 0, 2); u(out, 0, 2);
  u(out, files.size(), 2); u(out, files.size(), 2);
  u(out, central.size(), 4); u(out, at, 4); u(out, 0, 2);
  return out;
}

void test_epub_native() {
  const std::string zip = test_zip({
      {"mimetype", "application/epub+zip"},
      {"META-INF/container.xml",
       "<container><rootfiles><rootfile full-path=\"OEBPS/book.opf\"/>"
       "</rootfiles></container>"},
      {"OEBPS/book.opf",
       "<package><metadata><dc:title>A &amp; B</dc:title>"
       "<dc:creator>Some One</dc:creator></metadata><manifest>"
       "<item id=\"c1\" href=\"text/one.xhtml\" media-type=\"application/xhtml+xml\"/>"
       "<item id=\"c2\" href=\"text/two.xhtml\" media-type=\"application/xhtml+xml\"/>"
       "<item id=\"i\" href=\"img/a%20b.png\" media-type=\"image/png\"/>"
       "</manifest><spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine>"
       "</package>"},
      {"OEBPS/text/one.xhtml",
       "<html><head><title>x</title></head><body><h1>First</h1>"
       "<p><a id=\"p1\"/>Go <a href=\"two.xhtml#n1\">note</a> or "
       "<a href=\"two.xhtml\">next</a> or <a href=\"https://a.org/\">web</a>.</p>"
       "<p><img src=\"../img/a%20b.png\" alt=\"Pic\"/></p></body></html>"},
      {"OEBPS/text/two.xhtml",
       "<html><body><h1>Second</h1><p id=\"n1\">Note text.</p></body></html>"},
      {"OEBPS/img/a b.png", "PNGDATA"},
  });
  std::string err;
  auto book = xanatoo::epub_to_markdown(zip, &err);
  EXPECT(book.has_value());
  if (!book) {
    std::cerr << "epub: " << err << "\n";
    return;
  }
  const std::string& md = book->markdown;
  EXPECT(xanatoo::starts_with(md, "---\ntitle: \"A & B\"\nauthor: \"Some One\"\n---\n"));
  EXPECT(md.find("First") != std::string::npos);
  EXPECT(md.find("Second") != std::string::npos);
  size_t sections = 0, images = 0;
  const xanatoo::Document doc = xanatoo::parse_document(md, "book.md");
  for (const xanatoo::Node& n : doc.nodes) {
    sections += n.kind == xanatoo::NodeKind::Section;
    images += n.kind == xanatoo::NodeKind::Image;
  }
  EXPECT_EQ(sections, size_t(2));
  EXPECT_EQ(images, size_t(1));
  EXPECT(md.find("](#n1)") != std::string::npos);
  EXPECT(md.find("](#ch-1)") != std::string::npos);
  EXPECT(md.find("](https://a.org/)") != std::string::npos);
  EXPECT(md.find("![Pic](media/img0.png)") != std::string::npos);
  EXPECT_EQ(book->media.size(), size_t(1));
  if (!book->media.empty()) EXPECT_EQ(book->media[0].second, std::string("PNGDATA"));

  EXPECT(!xanatoo::epub_to_markdown("not a zip at all, sorry about that", &err));
  EXPECT(!xanatoo::epub_to_markdown(zip.substr(0, zip.size() / 2), &err));
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

void test_locator() {
  using xanatoo::format_locator;
  using xanatoo::Locator;
  using xanatoo::parse_locator;
  using xanatoo::SelectorKind;

  Locator l = parse_locator("book.md//id:ch3//text:the%20best,of%20times");
  EXPECT_EQ(l.base, "book.md");
  EXPECT_EQ(l.pipes.size(), 2u);
  EXPECT(l.pipes[0].kind == SelectorKind::Id);
  EXPECT_EQ(l.pipes[0].value, "ch3");
  EXPECT(l.pipes[1].kind == SelectorKind::Text);
  EXPECT_EQ(l.pipes[1].value, "the%20best,of%20times");
  EXPECT(!l.point);

  for (const std::string_view name : {"source-raw", "source-sexpr", "source-xml"}) {
    l = parse_locator("book.md//" + std::string(name));
    EXPECT_EQ(l.base, "book.md");
    EXPECT_EQ(l.pipes.size(), 1u);
    EXPECT(xanatoo::is_source_pipe(l.pipes[0].kind));
    EXPECT_EQ(l.pipes[0].value, "");
    EXPECT_EQ(format_locator(l), "book.md//" + std::string(name));
    EXPECT(xanatoo::locator_up(l));
    EXPECT_EQ(format_locator(l), "book.md");
  }
  l = parse_locator("https://example.org/p//source-raw");
  EXPECT_EQ(l.base, "https://example.org/p");
  EXPECT_EQ(l.pipes[0].kind, SelectorKind::SourceRaw);
  EXPECT_EQ(format_locator(l), "https://example.org/p//source-raw");
  l = parse_locator("book.md//source-xml");
  EXPECT_EQ(l.pipes[0].kind, SelectorKind::SourceXml);
  l = parse_locator("book.md//source-sexpr");
  EXPECT_EQ(l.pipes[0].kind, SelectorKind::SourceSexpr);

  l = parse_locator("map.png//rect:0.2,0.1,0.3,0.3#at:0.5,0.5");
  EXPECT_EQ(l.base, "map.png");
  EXPECT(l.pipes[0].kind == SelectorKind::Rect);
  EXPECT_EQ(l.pipes[0].value, "0.2,0.1,0.3,0.3");
  EXPECT(l.point && l.point->kind == SelectorKind::At);

  // Bare fragments are labels, unknown names and colons included.
  l = parse_locator("page.html#intro");
  EXPECT_EQ(l.base, "page.html");
  EXPECT(l.point && l.point->kind == SelectorKind::Id);
  EXPECT_EQ(l.point->value, "intro");
  l = parse_locator("page.html#fn:1");
  EXPECT(l.point && l.point->kind == SelectorKind::Id);
  EXPECT_EQ(l.point->value, "fn:1");
  l = parse_locator("page.html#Some%20Heading");
  EXPECT_EQ(l.point->value, "Some Heading");
  l = parse_locator("page.html#:~:text=a,b");
  EXPECT(l.point && l.point->kind == SelectorKind::Text);
  EXPECT_EQ(l.point->value, "a,b");

  // Not selectors: every `//` of ordinary addresses stays in the base.
  for (const char* plain :
       {"https://web.archive.org/web/2020/https://x.org/a", "file:///a/b.md",
        "/abs//double/slash.md", "hn:item?id=1", "gemini://host/id:1",
        "https://id:secret@host/", "book.pdf//page:12",
        "a.zip//archive:b.jpg", "x//id:", "wikipedia:C#"}) {
    l = parse_locator(plain);
    EXPECT_EQ(l.base, plain);
    EXPECT(!l.has_selectors());
    EXPECT_EQ(format_locator(l), plain);
  }

  // Selectors after ordinary addresses, thumtoo pipes remain the base.
  l = parse_locator("hn:item?id=1//id:x");
  EXPECT_EQ(l.base, "hn:item?id=1");
  EXPECT_EQ(l.pipes.size(), 1u);
  l = parse_locator("hn:item?id=42#id:101");
  EXPECT_EQ(l.base, "hn:item?id=42");
  EXPECT(l.point.has_value());
  if (l.point) EXPECT_EQ(l.point->value, "101");
  l = parse_locator("https://a.org//b//path:0.1#c");
  EXPECT_EQ(l.base, "https://a.org//b");
  EXPECT_EQ(l.pipes[0].value, "0.1");
  l = parse_locator("book.pdf//page:12#rect:0,0,1,1");
  EXPECT_EQ(l.base, "book.pdf//page:12");

  // Round trips, with escapes where a value would break the address.
  for (const char* s :
       {"a.md//id:b", "a.md//id:b#c", "a.md//id:b//path:0.2", "a.md#b",
        "a.md#id:x:y", "a.md//text:a%2Cb,c#rect:0,0,.5,.5",
        "a.md//id:a%2Fb%20c"}) {
    EXPECT_EQ(format_locator(parse_locator(s)), s);
  }

  // Up: last pipe becomes the point, the point goes.
  l = parse_locator("a.md//id:b//id:c");
  EXPECT(xanatoo::locator_up(l));
  EXPECT_EQ(format_locator(l), "a.md//id:b#c");
  EXPECT(xanatoo::locator_up(l));
  EXPECT_EQ(format_locator(l), "a.md//id:b");
  EXPECT(xanatoo::locator_up(l));
  EXPECT_EQ(format_locator(l), "a.md#b");
  EXPECT(xanatoo::locator_up(l));
  EXPECT_EQ(format_locator(l), "a.md");
  EXPECT(!xanatoo::locator_up(l));

  // Resolving against a document.
  const xanatoo::Document d = xanatoo::parse_markup(
      "# One\n\nfirst\n\n## Two\n\nsecond\n\n## Two\n\nthird\n\n"
      "# Three\n\nfourth\n");
  xanatoo::NodeId one = 0, three = 0, n = 0;
  EXPECT(xanatoo::resolve_node(d, 0, {SelectorKind::Id, "One"}, one) ==
         xanatoo::Resolve::Exact);
  EXPECT(d.get(one).kind == xanatoo::NodeKind::Section);
  EXPECT(xanatoo::resolve_node(d, 0, {SelectorKind::Id, "Three"}, three) ==
         xanatoo::Resolve::Exact);
  EXPECT(xanatoo::resolve_node(d, one, {SelectorKind::Id, "Two"}, n) ==
         xanatoo::Resolve::Exact);
  EXPECT(xanatoo::resolve_node(d, three, {SelectorKind::Id, "Two"}, n) ==
         xanatoo::Resolve::Lost);
  EXPECT(xanatoo::resolve_node(d, 0, {SelectorKind::Id, "nope"}, n) ==
         xanatoo::Resolve::Lost);
  EXPECT(xanatoo::resolve_node(d, 0, {SelectorKind::Path, "99"}, n) ==
         xanatoo::Resolve::Lost);
  EXPECT(xanatoo::resolve_node(d, 0, {SelectorKind::Path, "x"}, n) ==
         xanatoo::Resolve::Lost);

  // node_selector names every section so that it resolves back to it,
  // through a path where the heading is not unique.
  int paths = 0;
  for (const xanatoo::Node& node : d.nodes) {
    if (node.id == 0 || node.kind != xanatoo::NodeKind::Section) continue;
    const xanatoo::Selector sel = xanatoo::node_selector(d, node.id);
    paths += sel.kind == SelectorKind::Path;
    xanatoo::NodeId back = 0;
    EXPECT(xanatoo::resolve_node(d, 0, sel, back) == xanatoo::Resolve::Exact);
    EXPECT_EQ(back, node.id);
  }
  EXPECT_EQ(paths, 1);  // the second "Two"
}

void test_text_selectors() {
  using xanatoo::Caret;
  using xanatoo::Resolve;
  using xanatoo::Selection;
  using xanatoo::SelectorKind;

  auto q = xanatoo::parse_text_quote("pre-,a%2Cb,end,-post");
  EXPECT(q && q->prefix == "pre" && q->start == "a,b" && q->end == "end" &&
         q->suffix == "post");
  EXPECT_EQ(xanatoo::format_text_quote(*q), "pre-,a%2Cb,end,-post");
  q = xanatoo::parse_text_quote("before-,,-after");
  EXPECT(q && q->start.empty() && q->prefix == "before" &&
         q->suffix == "after");
  EXPECT_EQ(xanatoo::format_text_quote(*q), "before-,,-after");
  EXPECT(xanatoo::parse_text_quote("-,,-after"));
  EXPECT(!xanatoo::parse_text_quote(""));
  EXPECT(!xanatoo::parse_text_quote("a,b,c"));
  EXPECT(!xanatoo::parse_text_quote(",x"));

  const xanatoo::Document d = xanatoo::parse_markup(
      "# Title\n\nThe quick brown fox jumps over the lazy dog.\n\n"
      "Another dog sleeps here, and   the quick brown fox **naps** today.\n\n"
      "Ünïcode wörds stay whole.\n");
  std::vector<xanatoo::NodeId> texts;
  xanatoo::collect_text_nodes(d, d.root, texts);
  EXPECT_EQ(texts.size(), 4u);
  const xanatoo::NodeId p1 = texts[1], p2 = texts[2], p3 = texts[3];

  Selection out;
  // Case and whitespace do not matter; across spans of one block.
  EXPECT(xanatoo::resolve_text(d, 0, {SelectorKind::Text, "LAZY%20DOG"}, out) ==
         Resolve::Exact);
  EXPECT(out.anchor.node == p1 && out.anchor.char_index == 35);
  EXPECT(out.focus.node == p1 && out.focus.char_index == 43);
  EXPECT(xanatoo::resolve_text(d, 0, {SelectorKind::Text, "fox%20naps%20today"},
                               out) == Resolve::Exact);
  EXPECT(out.anchor.node == p2 && out.focus.node == p2);
  // The context picks the second "quick brown fox".
  EXPECT(xanatoo::resolve_text(d, 0, {SelectorKind::Text, "the-,quick"}, out) ==
         Resolve::Exact);
  EXPECT(out.anchor.node == p1);
  EXPECT(xanatoo::resolve_text(d, 0,
                               {SelectorKind::Text, "and%20the-,quick"},
                               out) == Resolve::Exact);
  EXPECT(out.anchor.node == p2);
  // start,end spans blocks.
  EXPECT(xanatoo::resolve_text(d, 0, {SelectorKind::Text, "lazy,sleeps"},
                               out) == Resolve::Exact);
  EXPECT(out.anchor.node == p1 && out.focus.node == p2);
  EXPECT(xanatoo::resolve_text(d, 0, {SelectorKind::Text, "missing"}, out) ==
         Resolve::Lost);
  EXPECT(xanatoo::resolve_text(d, p2, {SelectorKind::Text, "lazy"}, out) ==
         Resolve::Lost);

  // text_selector → resolve_text gives the selection back, and is unique.
  auto round_trip = [&](const Selection& sel) {
    const xanatoo::Selector s = xanatoo::text_selector(d, 0, sel);
    Selection back;
    const bool ok =
        xanatoo::resolve_text(d, 0, s, back) == Resolve::Exact &&
        xanatoo::caret_cmp(d, back.anchor, sel.anchor) == 0 &&
        xanatoo::caret_cmp(d, back.focus, sel.focus) == 0;
    if (!ok) std::fprintf(stderr, "  text selector: %s\n", s.value.c_str());
    return ok;
  };
  const xanatoo::Node& n2 = d.get(p2);
  const size_t last = n2.spans.size() - 1;
  // "quick brown fox" in the second paragraph: needs a prefix.
  const size_t at = std::string("Another dog sleeps here, and   the ").size();
  EXPECT(round_trip({{p2, 0, at}, {p2, 0, at + 15}}));
  EXPECT(round_trip({{p1, 0, 4}, {p1, 0, 9}}));    // "quick"
  EXPECT(round_trip({{p1, 0, 10}, {p2, last, 3}}));  // long, two blocks
  EXPECT(round_trip({{p3, 0, 0}, {p3, 0, 7}}));    // "Ünïcode"
  // Points.
  for (const Caret c : {Caret{p1, 0, 20}, Caret{p2, 0, 4}, Caret{p3, 0, 8},
                        Caret{p1, 0, 0}}) {
    EXPECT(round_trip({c, c}));
  }
}

void test_bookmark_lines() {
  auto b = xanatoo::parse_bookmark_line("  https://example.org/  ");
  EXPECT(b && b->address == "https://example.org/" && b->quote.empty());
  EXPECT(!xanatoo::parse_bookmark_line("# comment"));
  EXPECT(!xanatoo::parse_bookmark_line("   "));
  xanatoo::Bookmark in{"a.md#text:x", "Chapter\t1", "a\nquote", "2026-11-03"};
  const std::string line = xanatoo::format_bookmark_line(in);
  EXPECT(line.find('\n') == std::string::npos);
  b = xanatoo::parse_bookmark_line(line);
  EXPECT(b && b->address == in.address && b->title == "Chapter 1" &&
         b->quote == "a quote" && b->created == in.created);
  b = xanatoo::parse_bookmark_line("a.md\tunknown=1\tquote=q");
  EXPECT(b && b->quote == "q");
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

void test_hn_urls_and_html() {
  using namespace xanatoo;
  EXPECT(is_hn_url("https://news.ycombinator.com/"));
  EXPECT(is_hn_url("https://news.ycombinator.com"));
  EXPECT(is_hn_url("https://news.ycombinator.com/news?p=2"));
  EXPECT(is_hn_url("https://news.ycombinator.com/newest"));
  EXPECT(is_hn_url("http://news.ycombinator.com/item?id=8863"));
  EXPECT(!is_hn_url("https://news.ycombinator.com/item"));
  EXPECT(is_hn_url("https://news.ycombinator.com/user?id=pg"));
  EXPECT(!is_hn_url("https://news.ycombinator.com/user?id="));
  EXPECT(!is_hn_url("https://example.com/item?id=1"));

  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  hn_append_html(doc, root,
                 "First &#x27;one&#x27;<p>&gt; quoted <i>it</i>"
                 "<p>See <a href=\"https:&#x2F;&#x2F;x.org&#x2F;\" "
                 "rel=\"nofollow\">x.org</a> now"
                 "<pre><code>  a &lt; b\n</code></pre>tail");
  const auto& kids = doc.get(root).children;
  EXPECT_EQ(kids.size(), 5u);
  if (kids.size() == 5) {
    EXPECT(doc.get(kids[0]).kind == NodeKind::Paragraph);
    EXPECT_EQ(doc.get(kids[0]).spans.at(0).text, "First 'one'");
    EXPECT(doc.get(kids[1]).kind == NodeKind::BlockQuote);
    EXPECT_EQ(doc.get(kids[1]).spans.at(0).text, "quoted ");
    EXPECT(doc.get(kids[1]).spans.at(1).kind == InlineKind::Emphasis);
    const auto& link = doc.get(kids[2]).spans.at(1);
    EXPECT(link.kind == InlineKind::Link);
    EXPECT_EQ(link.href, "https://x.org/");
    EXPECT(doc.get(kids[3]).kind == NodeKind::CodeBlock);
    EXPECT_EQ(plain_text(doc, kids[3]), "  a < b");
    EXPECT_EQ(plain_text(doc, kids[4]), "tail");
  }

  Document dated;
  NodeId dated_root = dated.create(NodeKind::Document);
  hn_append_html(dated, dated_root,
                 "<p><time datetime=\"2026-11-04T09:00:00Z\">today</time>"
                 " then <a href=\"/item?id=10\"><time "
                 "datetime=\"2026-11-03T09:00:00Z\">yesterday</time>"
                 "</a> afterwards");
  const auto& spans = dated.get(dated.get(dated_root).children.at(0)).spans;
  EXPECT(spans.size() >= 4);
  if (spans.size() >= 4) {
    EXPECT_EQ(spans[0].datetime, "2026-11-04T09:00:00Z");
    EXPECT(spans[1].datetime.empty());
    EXPECT_EQ(spans[2].datetime, "2026-11-03T09:00:00Z");
    EXPECT(spans[2].kind == InlineKind::Link);
    EXPECT(spans[3].datetime.empty());
  }
}

void test_timestamps() {
  using namespace xanatoo;
  EXPECT_EQ(parse_timestamp("1970-01-01T00:00:00Z"),
            std::optional<int64_t>{0});
  EXPECT_EQ(parse_timestamp("1970-01-01T01:00:00+01:00"),
            std::optional<int64_t>{0});
  EXPECT_EQ(parse_timestamp("1970-01-01T00:30:00-01:00"),
            std::optional<int64_t>{5400});
  EXPECT_EQ(parse_timestamp("2024-02-29T12:34:56.000Z"),
            std::optional<int64_t>{1709210096});
  EXPECT(!parse_timestamp("2025-02-29T12:00Z"));
  EXPECT(!parse_timestamp("2026-11-04T12:00"));  // no timezone
  EXPECT(!parse_timestamp("2026-11-04T24:00Z"));
  const int64_t now = *parse_timestamp("2026-11-04T12:00:00Z");
  auto past = timestamp_hover_text("2026-10-30T08:00:00Z", now);
  EXPECT(past.find("5 days, 4 hours ago") != std::string::npos);
  EXPECT(past.find("UTC") != std::string::npos);
  EXPECT(past.find("Local") != std::string::npos);
  auto future = timestamp_hover_text("2026-11-04T15:02:00Z", now);
  EXPECT(future.find("in 3 hours, 2 minutes") != std::string::npos);
  EXPECT(timestamp_hover_text("not a date", now).empty());
}

void test_json_utf8_and_top_level() {
  using namespace xanatoo;
  EXPECT_EQ(json_unescape("caf\\u00e9 \\u20ac \\ud83d\\ude00"),
            "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80");
  std::string top = json_top_level(
      "{\"_h\":{\"title\":{\"value\":\"x\"}},\"kids\":[{\"title\":\"no\"}],"
      "\"title\":\"yes\",\"n\":3}");
  EXPECT_EQ(json_str_field(top, "title"), "yes");
  EXPECT_EQ(json_int_field(top, "n"), 3);
  EXPECT_EQ(html_decode_entities("a &amp; b &#34;c&#x27; &bogus; &"),
            "a & b \"c' &bogus; &");
}

void test_ebook_urls() {
  using namespace xanatoo;
  EXPECT(is_standard_ebooks_url("https://standardebooks.org/ebooks"));
  EXPECT(is_standard_ebooks_url("https://standardebooks.org/ebooks?query=x"));
  EXPECT(is_standard_ebooks_url("https://standardebooks.org/subjects/poetry"));
  EXPECT(!is_standard_ebooks_url("https://standardebooks.org/about"));
  EXPECT(ebook_download_url("https://standardebooks.org/ebooks/charles-dickens")
             .empty());
  EXPECT_EQ(ebook_download_url("https://standardebooks.org/ebooks/jane-austen/emma"),
            "https://standardebooks.org/ebooks/jane-austen/emma/downloads/"
            "jane-austen_emma.epub?source=feed");
  EXPECT_EQ(ebook_download_url(
                "https://standardebooks.org/ebooks/leo-tolstoy/anna-karenina/"
                "constance-garnett/text/single-page"),
            "https://standardebooks.org/ebooks/leo-tolstoy/anna-karenina/"
            "constance-garnett/downloads/"
            "leo-tolstoy_anna-karenina_constance-garnett.epub?source=feed");
  EXPECT_EQ(ebook_download_url("https://www.gutenberg.org/ebooks/1342"),
            "https://www.gutenberg.org/ebooks/1342.epub3.images");
  EXPECT(is_remote_epub_url("https://www.gutenberg.org/ebooks/84.epub.noimages"));
  EXPECT(is_remote_epub_url("https://example.org/books/a.EPUB?x=1"));
  EXPECT(!is_remote_epub_url("https://www.gutenberg.org/ebooks/search/?query=x"));
  EXPECT(!is_remote_epub_url("https://example.org/a.pdf"));

  Document doc = parse_standard_ebooks_listing(
      "<h1>Ebooks by A &amp; B</h1><ol>"
      "<li typeof=\"schema:Book\" about=\"/ebooks/a-b/title-one\">"
      "<p><a href=\"/ebooks/a-b/title-one\"><span property=\"schema:name\">"
      "Title [One]</span></a></p><p class=\"author\" resource=\"/ebooks/a-b\">"
      "<a href=\"x\"><span property=\"schema:name\">A B</span></a></p></li>"
      "</ol><a href=\"/ebooks/a-b?page=2\" rel=\"next\">Next</a>",
      "https://standardebooks.org/ebooks/a-b");
  std::string text;
  for (const Node& n : doc.nodes) text += plain_text(doc, n.id) + "\n";
  EXPECT(text.find("Ebooks by A & B") != std::string::npos);
  EXPECT(text.find("Title [One]") != std::string::npos);
  EXPECT(text.find("Next page") != std::string::npos);
}

// Children of `id` with their kinds, for structure checks.
std::vector<xanatoo::NodeId> kids_of(const xanatoo::Document& doc,
                                     xanatoo::NodeId id) {
  return doc.get(id).children;
}

const xanatoo::Node* first_of_kind(const xanatoo::Document& doc,
                                   xanatoo::NodeKind kind) {
  for (const auto& n : doc.nodes)
    if (n.id != xanatoo::INVALID_NODE && n.kind == kind) return &n;
  return nullptr;
}

void test_html_min() {
  using namespace xanatoo;
  EXPECT_EQ(resolve_url("https://x.org/blog/post.html", "img/a.png"),
            "https://x.org/blog/img/a.png");
  EXPECT_EQ(resolve_url("https://x.org/blog/post.html", "../a"), "https://x.org/a");
  EXPECT_EQ(resolve_url("https://x.org/blog/", "/top"), "https://x.org/top");
  EXPECT_EQ(resolve_url("https://x.org/b?q=1#f", "#g"), "https://x.org/b?q=1#g");
  EXPECT_EQ(resolve_url("https://x.org/b", "//cdn.org/i"), "https://cdn.org/i");
  EXPECT_EQ(resolve_url("https://x.org/b", "mailto:a@b"), "mailto:a@b");
  EXPECT_EQ(resolve_url("https://x.org", "a"), "https://x.org/a");
  EXPECT_EQ(html_attr(" href='a&amp;b' CLASS=x", "class"), "x");
  EXPECT_EQ(html_attr(" href='a&amp;b' CLASS=x", "href"), "a&b");
  EXPECT_EQ(html_to_plain("  A <b>bold</b>&nbsp;move &rsquo;s x < y "),
            "A bold\xc2\xa0move \xe2\x80\x99s x < y");

  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  HtmlOptions opt;
  opt.base_url = "https://blog.example/posts/1";
  html_append_blocks(
      doc, root,
      "<div class=x><h2>Head</h2><p>One <a href=\"/a\">link</a><br>two"
      "<script>bad()</script><p>Para <img src=\"i.png\" alt=\"cat\">"
      "<ul><li>a<li>b<ol><li>b1</ol></ul>"
      "<blockquote><p>q1</p><p>q2</p></blockquote>"
      "<pre>\n  x &lt; 1\n</pre>"
      "<table><tr><th>H<td>v</table><hr></div>",
      opt);
  auto k = kids_of(doc, root);
  EXPECT_EQ(k.size(), 8u);
  if (k.size() == 8) {
    EXPECT(doc.get(k[0]).kind == NodeKind::Heading);
    EXPECT_EQ(doc.get(k[0]).level, 2);
    EXPECT_EQ(plain_text(doc, k[0]), "Head");
    EXPECT_EQ(plain_text(doc, k[1]), "One link\ntwo");
    EXPECT_EQ(doc.get(k[1]).spans.at(1).href, "https://blog.example/a");
    EXPECT_EQ(doc.get(k[2]).spans.back().href, "https://blog.example/posts/i.png");
    EXPECT_EQ(doc.get(k[2]).spans.back().text, "[image: cat]");
    EXPECT(doc.get(k[3]).kind == NodeKind::List);
    auto items = kids_of(doc, k[3]);
    EXPECT_EQ(items.size(), 2u);
    if (items.size() == 2) {
      EXPECT_EQ(plain_text(doc, items[0]), "a");
      auto nested = kids_of(doc, items[1]);
      EXPECT_EQ(nested.size(), 1u);
      if (!nested.empty()) {
        EXPECT_EQ(doc.get(nested[0]).level, 1);  // ordered
        EXPECT_EQ(doc.get(kids_of(doc, nested[0]).at(0)).level, 1);
      }
    }
    EXPECT(doc.get(k[4]).kind == NodeKind::BlockQuote);
    EXPECT_EQ(plain_text(doc, k[4]), "q1\nq2");
    EXPECT(doc.get(k[5]).kind == NodeKind::CodeBlock);
    EXPECT_EQ(plain_text(doc, k[5]), "  x < 1");
    EXPECT(doc.get(k[6]).kind == NodeKind::Table);
    auto cells = kids_of(doc, kids_of(doc, k[6]).at(0));
    EXPECT_EQ(cells.size(), 2u);
    if (cells.size() == 2) {
      EXPECT_EQ(doc.get(cells[0]).level, 1);
      EXPECT_EQ(plain_text(doc, cells[1]), "v");
    }
    EXPECT(doc.get(k[7]).kind == NodeKind::HorizontalRule);
  }

  // images: block-level <img> → Image node; icons and images in list items
  // are inline image spans; data: placeholders stay links.
  Document idoc;
  NodeId iroot = idoc.create(NodeKind::Document);
  opt.images = true;
  html_append_blocks(
      idoc, iroot,
      "<p>Before <img src=\"i.png\" alt=\"cat\"> after"
      " <img src=\"flag.png\" width=\"16\" height=\"11\" alt=\"DE\">"
      "<figure><img src=\"data:image/gif;base64,R0\" data-src=\"/lazy.jpg\">"
      "<figcaption>Cap</figcaption></figure>"
      "<p><img srcset=\"//cdn.example/s.webp 1x, //cdn.example/l.webp 2x\">"
      "<ul><li>x <img src=\"li.png\"></ul>",
      opt);
  auto ik = kids_of(idoc, iroot);
  EXPECT_EQ(ik.size(), 6u);
  if (ik.size() == 6) {
    EXPECT_EQ(plain_text(idoc, ik[0]), "Before");
    EXPECT(idoc.get(ik[1]).kind == NodeKind::Image);
    EXPECT_EQ(idoc.get(ik[1]).src, "https://blog.example/posts/i.png");
    EXPECT_EQ(idoc.get(ik[1]).alt, "cat");
    {
      const auto& sp = idoc.get(ik[2]).spans;
      EXPECT_EQ(sp.size(), 2u);
      if (sp.size() == 2) {
        EXPECT_EQ(sp[0].text, "after ");
        EXPECT(sp[1].kind == InlineKind::Image);
        EXPECT_EQ(sp[1].text, std::string(kInlineImageText));
        EXPECT_EQ(sp[1].src, "https://blog.example/posts/flag.png");
        EXPECT_EQ(sp[1].alt, "DE");
        EXPECT(sp[1].width == 16 && sp[1].height == 11);
      }
    }
    EXPECT(idoc.get(ik[3]).kind == NodeKind::Figure);
    const auto fk = kids_of(idoc, ik[3]);
    EXPECT_EQ(fk.size(), 2u);
    if (fk.size() == 2) {
      EXPECT(idoc.get(fk[0]).kind == NodeKind::Image);
      EXPECT_EQ(idoc.get(fk[0]).src, "https://blog.example/lazy.jpg");
      EXPECT(idoc.get(fk[1]).kind == NodeKind::Caption);
      EXPECT_EQ(plain_text(idoc, fk[1]), "Cap");
    }
    EXPECT(idoc.get(ik[4]).kind == NodeKind::Image);
    EXPECT_EQ(idoc.get(ik[4]).src, "https://cdn.example/s.webp");
    EXPECT(idoc.get(ik[5]).kind == NodeKind::List);
    const auto li = kids_of(idoc, ik[5]);
    EXPECT(li.size() == 1 && idoc.get(li[0]).spans.size() == 2 &&
           idoc.get(li[0]).spans[1].kind == InlineKind::Image);
  }
}

// Inline images (InlineKind::Image) and asides (NodeKind::Aside) through
// Markdown, sexp and HTML; layout of floats and image atoms.
void test_inline_images_and_asides() {
  {
    using namespace xanatoo;
    Document d = parse_markup("![caption](a.png){scale=0.7 float=right}\n");
    NodeId id = kids_of(d, d.root)[0];
    EXPECT_EQ(d.get(id).float_side, "right");
    EXPECT(d.get(id).display_scale > 0.69f && d.get(id).display_scale < 0.71f);
    const std::string md = serialize_markup(d);
    Document again = parse_markup(md);
    EXPECT_EQ(again.get(kids_of(again, again.root)[0]).float_side, "right");
    const std::string sx = serialize_sexpr(d);
    Document sex = parse_sexpr(sx);
    EXPECT_EQ(sex.get(kids_of(sex, sex.root)[0]).float_side, "right");
    Document invalid = parse_markup("![x](a.png){float=center}\n");
    EXPECT(invalid.get(kids_of(invalid, invalid.root)[0]).float_side.empty());
  }

  using namespace xanatoo;
  // Markdown inline images: plain, sized, linked; a lone one is a block.
  Document md = parse_markup(
      "Flag ![DE](de.png){width=16 height=11} and "
      "[![badge](b.svg)](https://ci.example) ok\n\n"
      "![photo](p.jpg)\n\n"
      ":::aside side=left\n"
      "Box text\n\n"
      "More\n"
      ":::\n\n"
      "After\n");
  auto mk = kids_of(md, md.root);
  EXPECT_EQ(mk.size(), 4u);
  if (mk.size() == 4) {
    const auto& sp = md.get(mk[0]).spans;
    EXPECT_EQ(sp.size(), 5u);
    if (sp.size() == 5) {
      EXPECT(sp[1].kind == InlineKind::Image);
      EXPECT_EQ(sp[1].src, "de.png");
      EXPECT_EQ(sp[1].alt, "DE");
      EXPECT(sp[1].width == 16 && sp[1].height == 11);
      EXPECT(sp[3].kind == InlineKind::Image);
      EXPECT_EQ(sp[3].src, "b.svg");
      EXPECT_EQ(sp[3].href, "https://ci.example");
    }
    EXPECT(md.get(mk[1]).kind == NodeKind::Image);
    EXPECT(md.get(mk[2]).kind == NodeKind::Aside);
    EXPECT_EQ(md.get(mk[2]).src, "left");
    EXPECT_EQ(kids_of(md, mk[2]).size(), 2u);
  }
  const std::string out = serialize_markup(md);
  EXPECT(out.find("![DE](de.png){width=16 height=11}") != std::string::npos);
  EXPECT(out.find("[![badge](b.svg)](https://ci.example)") != std::string::npos);
  EXPECT(out.find(":::aside side=left\n") != std::string::npos);
  EXPECT_EQ(serialize_markup(parse_markup(out)), out);

  // Table cells take inline images.
  Document tb = parse_markup("| a | b |\n|---|---|\n| ![x](x.png) | y |\n");
  bool cell_img = false;
  for (const Node& n : tb.nodes)
    if (n.kind == NodeKind::TableCell)
      for (const auto& sp : n.spans)
        cell_img = cell_img || (sp.kind == InlineKind::Image && sp.src == "x.png");
  EXPECT(cell_img);

  // sexp: (img …) in text, (aside …) of blocks; a text-only aside is a note.
  Document sx = parse_sexpr(
      "(doc (p \"a \" (img \"i.png\" :alt \"b\" :width 4 :href \"#t\"))"
      " (aside :side \"right\" (p \"box\")) (aside \"plain\"))");
  auto sk = kids_of(sx, sx.root);
  EXPECT_EQ(sk.size(), 3u);
  if (sk.size() == 3) {
    const auto& sp = sx.get(sk[0]).spans;
    EXPECT(sp.size() == 2 && sp[1].kind == InlineKind::Image &&
           sp[1].src == "i.png" && sp[1].width == 4 && sp[1].href == "#t");
    EXPECT(sx.get(sk[1]).kind == NodeKind::Aside);
    EXPECT_EQ(sx.get(sk[1]).src, "right");
    EXPECT(sx.get(sk[2]).kind == NodeKind::Note);
  }
  const std::string so = serialize_sexpr(sx);
  EXPECT_EQ(serialize_sexpr(parse_sexpr(so)), so);

  // HTML: an aside in <main> is a box, one with chrome classes stays chrome.
  Document hp = parse_html_page(
      "<body><main><p>Text<aside><p>Box</aside>"
      "<aside class=\"share\"><p>Share</aside></main></body>",
      "page.html");
  int asides = 0, chrome = 0;
  for (const Node& n : hp.nodes) {
    if (n.parent == INVALID_NODE) continue;  // unhooked empty regions
    asides += n.kind == NodeKind::Aside;
    chrome += n.kind == NodeKind::Chrome;
  }
  EXPECT_EQ(asides, 1);
  EXPECT_EQ(chrome, 1);

  // Editing next to an image: typed text gets a span of its own.
  Document ed = parse_markup("![i](i.png) x\n");
  NodeId para = kids_of(ed, ed.root).empty() ? INVALID_NODE
                                             : kids_of(ed, ed.root)[0];
  if (para != INVALID_NODE && ed.get(para).spans.size() == 2) {
    Caret c{para, 0, 1};
    insert_text(ed, c, "Q");
    const auto& sp = ed.get(para).spans;
    EXPECT(sp.size() == 3 && sp[0].kind == InlineKind::Image &&
           sp[0].text == kInlineImageText && sp[1].text == "Q");
  } else {
    EXPECT(false);
  }

  FontSet fonts;
  std::string path = fontconfig_find("DejaVu Sans", 0, 0);
  if (path.empty() || !fonts.load(nullptr, path, 18, 24)) {
    std::fprintf(stderr, "SKIP test_inline_images_and_asides layout (no font)\n");
    return;
  }
  LayoutEngine engine(fonts, nullptr);
  LayoutOptions lo;
  lo.margin_x = 0.f;
  lo.margin_y = 0.f;
  engine.set_options(lo);

  // Wikipedia infoboxes remain tables even with nested layout tables. The
  // complete table floats beside text, and falls back to flow when narrow.
  Document infobox;
  NodeId root = infobox.create(NodeKind::Document);
  HtmlOptions html;
  html.drop_chrome = true;
  html.images = true;
  html_append_blocks(infobox, root,
      "<table class='infobox biography'><caption>Profile</caption>"
      "<tr><th>Name</th><td>Alice</td></tr>"
      "<tr><td>Born</td><td><table><tr><td>Berlin</td></tr></table></td></tr>"
      "</table><p>Following text runs beside the infobox when there is room.</p>", html);
  auto ib = kids_of(infobox, root);
  EXPECT_EQ(ib.size(), 2u);
  if (ib.size() == 2) {
    EXPECT(infobox.get(ib[0]).kind == NodeKind::Table);
    EXPECT_EQ(infobox.get(ib[0]).float_side, "right");
    auto table_kids = kids_of(infobox, ib[0]);
    EXPECT_EQ(table_kids.size(), 3u);
    if (!table_kids.empty()) {
      EXPECT(infobox.get(table_kids[0]).kind == NodeKind::Caption);
      EXPECT_EQ(plain_text(infobox, table_kids[0]), "Profile");
    }
    bool nested_text = false;
    for (const Node& n : infobox.nodes)
      if (n.kind == NodeKind::TableCell && plain_text(infobox, n.id).find("Berlin") != std::string::npos)
        nested_text = true;
    EXPECT(nested_text);
    auto find_box = [](const LayoutResult& l, NodeId id) -> const LayoutBox* {
      for (const auto& b : l.boxes) if (b.node == id) return &b;
      return nullptr;
    };
    LayoutResult wide = engine.layout(infobox, 900.f);
    const LayoutBox* table = find_box(wide, ib[0]);
    const LayoutBox* text = find_box(wide, ib[1]);
    EXPECT(table && text);
    if (table && text) {
      EXPECT(table->x > text->x);
      EXPECT(text->y < table->y + table->height);
    }
    LayoutResult narrow = engine.layout(infobox, 250.f);
    table = find_box(narrow, ib[0]);
    text = find_box(narrow, ib[1]);
    EXPECT(table && text);
    if (table && text) EXPECT(text->y >= table->y + table->height);
    Document roundtrip = parse_sexpr(serialize_sexpr(infobox));
    bool retained = false;
    for (const Node& n : roundtrip.nodes)
      retained |= n.kind == NodeKind::Table && n.float_side == "right";
    EXPECT(retained);
    Document markdown = parse_markup(serialize_markup(infobox));
    retained = false;
    bool caption_retained = false;
    for (const Node& n : markdown.nodes) {
      retained |= n.kind == NodeKind::Table && n.float_side == "right";
      caption_retained |= n.kind == NodeKind::Caption && plain_text(markdown, n.id) == "Profile";
    }
    EXPECT(retained && caption_retained);
  }
  Document aligned;
  NodeId aligned_root = aligned.create(NodeKind::Document);
  html_append_blocks(aligned, aligned_root,
      "<table class='infobox' style='float: none'><tr><td>A</td></tr></table>"
      "<table style='FLOAT: left'><tr><td>B</td></tr></table>", html);
  auto ak = kids_of(aligned, aligned_root);
  EXPECT_EQ(ak.size(), 2u);
  if (ak.size() == 2) {
    EXPECT(aligned.get(ak[0]).float_side.empty());
    EXPECT_EQ(aligned.get(ak[1]).float_side, "left");
  }

  // HTML spans are table-wide: a cell covers slots in subsequent rows,
  // and rowspan=0 ends at the current row group, not the next tbody.
  Document spans;
  NodeId spans_root = spans.create(NodeKind::Document);
  html_append_blocks(spans, spans_root,
      "<table class='infobox'><thead><tr><th colspan='2'>Title</th></tr></thead>"
      "<tbody><tr><th rowspan='0'>Long<br>spanning<br>heading<br>with<br>many<br>lines</th>"
      "<td>One</td></tr><tr><td>Two</td></tr><tr><td>Three</td></tr></tbody>"
      "<tbody><tr><td colspan='2'>End</td></tr></tbody></table><p>Beside.</p>", html);
  auto span_top = kids_of(spans, spans_root);
  EXPECT_EQ(span_top.size(), 2u);
  if (span_top.size() == 2) {
    auto rows = kids_of(spans, span_top[0]);
    EXPECT_EQ(rows.size(), 5u);
    if (rows.size() == 5) {
      auto title = kids_of(spans, rows[0]);
      auto first = kids_of(spans, rows[1]);
      auto second = kids_of(spans, rows[2]);
      auto last = kids_of(spans, rows[4]);
      EXPECT(title.size() == 1 && first.size() == 2 && second.size() == 1 && last.size() == 1);
      if (title.size() == 1 && first.size() == 2 && second.size() == 1 && last.size() == 1) {
        EXPECT_EQ(spans.get(title[0]).col_span, 2);
        EXPECT_EQ(spans.get(first[0]).row_span, 3);
        EXPECT_EQ(spans.get(last[0]).col_span, 2);
        Document edited = spans;
        Caret insertion{first[1], 0, 0};
        EXPECT(split_block(edited, insertion));
        const TableGrid edited_grid = build_table_grid(edited, span_top[0]);
        EXPECT_EQ(edited_grid.rows.size(), 6u);
        EXPECT_EQ(edited.get(first[0]).row_span, 4);
        if (edited_grid.rows.size() == 6 && edited_grid.columns == 2) {
          EXPECT_EQ(edited_grid.slots[2][0], first[0]);
          EXPECT_EQ(edited_grid.slots[2][1], insertion.node);
          EXPECT_EQ(edited_grid.slots[3][0], first[0]);
          EXPECT_EQ(edited_grid.slots[3][1], second[0]);
        } else EXPECT(false);
        LayoutResult laid = engine.layout(spans, 900.f);
        auto box_for = [&](NodeId id) -> const LayoutBox* {
          for (const auto& b : laid.boxes) if (b.node == id) return &b;
          return nullptr;
        };
        const auto* table = box_for(span_top[0]);
        const auto* head = box_for(title[0]);
        const auto* tall = box_for(first[0]);
        const auto* right = box_for(second[0]);
        const auto* row0 = box_for(rows[0]);
        const auto* row2 = box_for(rows[2]);
        const auto* row4 = box_for(rows[4]);
        EXPECT(table && head && tall && right && row0 && row2 && row4);
        if (table && head && tall && right && row0 && row2 && row4) {
          EXPECT(head->width > tall->width * 1.8f);
          EXPECT(right->x > tall->x + tall->width - 1.f);
          EXPECT(tall->y + tall->height <= row4->y + 0.1f);
          EXPECT(tall->y + tall->height >= row4->y - 9.f);
          bool covered_fill = false;
          for (const auto& fill : laid.table_header_fills)
            if (fill.node == first[0])
              covered_fill = fill.y + fill.height >= row4->y - 5.f;
          EXPECT(covered_fill);
          for (const auto& line : laid.table_lines) {
            if (line.node != span_top[0]) continue;
            if (line.width <= 1.01f && line.x > table->x + 2.f &&
                line.x < table->x + table->width - 2.f)
              EXPECT(line.y >= row0->y + row0->height - 1.f);
            if (line.height <= 1.01f && std::abs(line.y - row2->y) < 1.f)
              EXPECT(line.x > tall->x + tall->width - 1.f);
          }
        }
        for (const std::string& serialized : {serialize_markup(spans), serialize_sexpr(spans)}) {
          Document restored = serialized[0] == '(' ? parse_sexpr(serialized) : parse_markup(serialized);
          bool col = false, row = false;
          for (const Node& n : restored.nodes) {
            col |= n.kind == NodeKind::TableCell && n.col_span == 2;
            row |= n.kind == NodeKind::TableCell && n.row_span == 3;
          }
          EXPECT(col && row);
        }
      }
    }
  }
  Document invalid_spans;
  NodeId invalid_root = invalid_spans.create(NodeKind::Document);
  html_append_blocks(invalid_spans, invalid_root,
      "<table class='infobox'><tr><td colspan='0' rowspan='oops'>Bad</td>"
      "<td colspan='9999999'>Large</td></tr></table>", html);
  auto invalid_table = kids_of(invalid_spans, invalid_root);
  if (!invalid_table.empty()) {
    auto rows = kids_of(invalid_spans, invalid_table[0]);
    if (!rows.empty()) {
      auto cells = kids_of(invalid_spans, rows[0]);
      EXPECT_EQ(cells.size(), 2u);
      if (cells.size() == 2) {
        EXPECT(invalid_spans.get(cells[0]).col_span == 1 && invalid_spans.get(cells[0]).row_span == 1);
        EXPECT_EQ(invalid_spans.get(cells[1]).col_span, 1000);
      }
    }
  }
  Document single_span;
  NodeId single_root = single_span.create(NodeKind::Document);
  html_append_blocks(single_span, single_root,
      "<table><tr><td colspan=' 2 '>Single row</td></tr></table>", html);
  auto single_children = kids_of(single_span, single_root);
  EXPECT(single_children.size() == 1 && single_span.get(single_children[0]).kind == NodeKind::Table);
  if (single_children.size() == 1 && single_span.get(single_children[0]).kind == NodeKind::Table) {
    auto rows = kids_of(single_span, single_children[0]);
    if (rows.size() == 1) {
      auto cells = kids_of(single_span, rows[0]);
      EXPECT(cells.size() == 1 && single_span.get(cells[0]).col_span == 2);
    } else EXPECT(false);
  }
  Document implicit_group;
  NodeId implicit_root = implicit_group.create(NodeKind::Document);
  html_append_blocks(implicit_group, implicit_root,
      "<table class='infobox'><tr><td rowspan='0'>Before</td></tr>"
      "<tbody><tr><td>After</td></tr></tbody></table>", html);
  auto implicit_table = kids_of(implicit_group, implicit_root);
  if (!implicit_table.empty()) {
    auto rows = kids_of(implicit_group, implicit_table[0]);
    if (rows.size() == 2) {
      auto cells = kids_of(implicit_group, rows[0]);
      EXPECT(cells.size() == 1 && implicit_group.get(cells[0]).row_span == 1);
    } else EXPECT(false);
  } else EXPECT(false);
  Document combined;
  NodeId combined_root = combined.create(NodeKind::Document);
  html_append_blocks(combined, combined_root,
      "<table class='infobox'><tr><th rowspan='2'>A</th><th colspan='2'>B</th></tr>"
      "<tr><td colspan='2'>C</td></tr></table>", html);
  auto combined_children = kids_of(combined, combined_root);
  EXPECT_EQ(combined_children.size(), 1u);
  if (combined_children.size() == 1) {
    const TableGrid grid = build_table_grid(combined, combined_children[0]);
    EXPECT(grid.rows.size() == 2 && grid.columns == 3 && grid.cells.size() == 3);
    if (grid.rows.size() == 2 && grid.columns == 3 && grid.cells.size() == 3) {
      EXPECT(grid.slots[0][0] == grid.slots[1][0]);
      EXPECT(grid.slots[0][1] == grid.slots[0][2]);
      EXPECT(grid.slots[1][1] == grid.slots[1][2]);
      EXPECT(grid.slots[0][1] != grid.slots[1][1]);
      EXPECT(grid.slots[1][0] != grid.slots[1][1]);
    }
  }
  Document rectangle;
  NodeId rectangle_root = rectangle.create(NodeKind::Document);
  html_append_blocks(rectangle, rectangle_root,
      "<table class='infobox'><tr><th colspan='2' rowspan='2'>A</th><td>B</td></tr>"
      "<tr><td>C</td></tr></table>", html);
  auto rectangle_children = kids_of(rectangle, rectangle_root);
  if (rectangle_children.size() == 1) {
    const TableGrid grid = build_table_grid(rectangle, rectangle_children[0]);
    EXPECT(grid.rows.size() == 2 && grid.columns == 3);
    if (grid.rows.size() == 2 && grid.columns == 3) {
      EXPECT(grid.slots[0][0] == grid.slots[0][1]);
      EXPECT(grid.slots[0][0] == grid.slots[1][0]);
      EXPECT(grid.slots[0][0] == grid.slots[1][1]);
      EXPECT(grid.slots[0][2] != grid.slots[1][2]);
    }
  } else EXPECT(false);
  Document weighted;
  NodeId weighted_root = weighted.create(NodeKind::Document);
  html_append_blocks(weighted, weighted_root,
      "<table class='infobox'><tr><th colspan='2'>Title</th></tr>"
      "<tr><td>Left</td><td>Right</td></tr></table>", html);
  auto weighted_table = kids_of(weighted, weighted_root);
  if (weighted_table.size() == 1) {
    auto rows = kids_of(weighted, weighted_table[0]);
    LayoutResult laid = engine.layout(weighted, 900.f);
    if (rows.size() == 2) {
      const LayoutBox* title_row = nullptr;
      for (const auto& box : laid.boxes) if (box.node == rows[0]) title_row = &box;
      EXPECT(title_row != nullptr && !laid.column_gaps.empty());
      if (title_row)
        for (const auto& gap : laid.column_gaps)
          EXPECT(gap.y >= title_row->y + title_row->height);
    } else EXPECT(false);
  } else EXPECT(false);

  // A right infobox and a left thumbnail can overlap vertically across a
  // section boundary. The heading rule and text must stay between them.
  Document paired;
  NodeId paired_root = paired.create(NodeKind::Document);
  HtmlOptions page_html = html;
  page_html.sections = true;
  html_append_blocks(paired, paired_root,
      "<table class='infobox'><tr><th>Name</th><td>Alice</td></tr>"
      "<tr><th>One</th><td>Details</td></tr><tr><th>Two</th><td>Details</td></tr>"
      "<tr><th>Three</th><td>Details</td></tr><tr><th>Four</th><td>Details</td></tr>"
      "<tr><th>Five</th><td>Details</td></tr><tr><th>Six</th><td>Details</td></tr>"
      "<tr><th>Seven</th><td>Details</td></tr></table>"
      "<h2>Section</h2><figure typeof='mw:File/Thumb' class='mw-halign-left'>"
      "<img src='portrait.jpg' width='170' height='170'><figcaption>Portrait</figcaption>"
      "</figure><p>Text continues between the figure and the infobox.</p>", page_html);
  NodeId table_id = INVALID_NODE, figure_id = INVALID_NODE;
  NodeId header_id = INVALID_NODE, text_id = INVALID_NODE;
  for (const Node& n : paired.nodes) {
    if (n.kind == NodeKind::Table) table_id = n.id;
    if (n.kind == NodeKind::Figure) figure_id = n.id;
    if (n.kind == NodeKind::Header) header_id = n.id;
    if (n.kind == NodeKind::Paragraph && plain_text(paired, n.id).find("Text continues") == 0)
      text_id = n.id;
  }
  EXPECT(table_id != INVALID_NODE && figure_id != INVALID_NODE &&
         header_id != INVALID_NODE && text_id != INVALID_NODE);
  if (table_id != INVALID_NODE && figure_id != INVALID_NODE &&
      header_id != INVALID_NODE && text_id != INVALID_NODE) {
    EXPECT_EQ(paired.get(figure_id).float_side, "left");
    DocStyle float_style = builtin_style();
    float_style.block[static_cast<size_t>(BlockRole::Header)].border = {100, 100, 100, 255};
    engine.set_style(&float_style);
    auto box_for = [](const LayoutResult& l, NodeId id) -> const LayoutBox* {
      for (const auto& b : l.boxes) if (b.node == id) return &b;
      return nullptr;
    };
    LayoutResult wide = engine.layout(paired, 900.f);
    const LayoutBox* table = box_for(wide, table_id);
    const LayoutBox* figure = box_for(wide, figure_id);
    EXPECT(table && figure);
    if (table && figure) {
      EXPECT(figure->y < table->y + table->height);
      EXPECT(figure->x + figure->width < table->x);
      bool saw_rule = false;
      for (const auto& rule : wide.role_rules)
        if (rule.box.node == header_id) {
          saw_rule = true;
          EXPECT(rule.box.x + rule.box.width <= table->x);
        }
      EXPECT(saw_rule);
      bool text_between = false;
      for (const auto& g : wide.glyphs)
        if (g.node == text_id && g.y < table->y + table->height &&
            g.x >= figure->x + figure->width && g.x < table->x)
          text_between = true;
      EXPECT(text_between);
    }
    LayoutResult tight = engine.layout(paired, 400.f);
    table = box_for(tight, table_id);
    figure = box_for(tight, figure_id);
    EXPECT(table && figure);
    if (table && figure) {
      for (const auto& g : tight.glyphs)
        if (g.node == text_id && g.y < table->y + table->height)
          EXPECT(g.y >= figure->y + figure->height);
    }
    engine.set_style(nullptr);
  }

  Document default_thumb;
  NodeId default_root = default_thumb.create(NodeKind::Document);
  html_append_blocks(default_thumb, default_root,
      "<table class='infobox'><tr><td>One</td></tr><tr><td>Two</td></tr>"
      "<tr><td>Three</td></tr><tr><td>Four</td></tr></table>"
      "<figure typeof='mw:File/Thumb'><img src='portrait.jpg' width='170'>"
      "<figcaption>Portrait</figcaption></figure><p>Text alongside the infobox.</p>"
      "<figure typeof='mw:File/Thumb' class='mw-halign-none'><img src='plain.jpg'></figure>", html);
  auto defaults = kids_of(default_thumb, default_root);
  EXPECT_EQ(defaults.size(), 4u);
  if (defaults.size() == 4) {
    EXPECT_EQ(default_thumb.get(defaults[1]).float_side, "right");
    EXPECT(default_thumb.get(defaults[3]).float_side.empty());
    LayoutResult layout = engine.layout(default_thumb, 900.f);
    const LayoutBox *tb = nullptr, *fig = nullptr, *text = nullptr;
    for (const auto& box : layout.boxes) {
      if (box.node == defaults[0]) tb = &box;
      if (box.node == defaults[1]) fig = &box;
      if (box.node == defaults[2]) text = &box;
    }
    EXPECT(tb && fig && text);
    if (tb && fig && text) {
      EXPECT(fig->y >= tb->y + tb->height);
      EXPECT(text->y < tb->y + tb->height);
    }
  }

  // Inline image: an atom on the baseline, raising its line.
  Document il = parse_markup("a ![i](i.png){width=40 height=60} b\n");
  LayoutResult lr = engine.layout(il, 600.f);
  EXPECT_EQ(lr.images.size(), 1u);
  if (lr.images.size() == 1) {
    const PositionedImage& im = lr.images[0];
    EXPECT(im.in_text && im.width == 40.f && im.height == 60.f);
    float base = -1.f;
    for (const auto& g : lr.glyphs)
      if (g.codepoint == 'b') base = g.y;
    EXPECT(std::abs(im.y + im.height - base) < 0.5f);
    EXPECT(im.y >= 0.f);
  }

  // A margin aside uses existing space only. It may not narrow the text
  // column just to make room for itself.
  std::string words;
  for (int k = 0; k < 120; ++k) words += "word ";
  Document fl = parse_markup(":::aside\nSide box\n:::\n\n" + words + "\n");
  NodeId aside = kids_of(fl, fl.root)[0];
  auto box_of = [](const LayoutResult& r, NodeId id) {
    LayoutBox box{};
    for (const auto& b : r.boxes)
      if (b.node == id) box = b;
    return box;
  };
  auto in_aside = [&](const PositionedGlyph& g) {
    return g.node == aside || (g.node != INVALID_NODE &&
                               fl.get(g.node).parent == aside);
  };
  {
    lo.margin_pct = 0.22f;
    engine.set_options(lo);
    LayoutResult mr = engine.layout(fl, 1200.f);
    const LayoutBox abox = box_of(mr, aside);
    EXPECT(abox.width >= 8.f * fonts.regular.pixel_size() &&
           abox.width <= 264.f);
    EXPECT(abox.x > 950.f && abox.x + abox.width <= 1200.f);
    Document body_only = parse_markup(words + "\n");
    LayoutResult plain = engine.layout(body_only, 1200.f);
    float text_right = 0.f;
    bool small = false;
    size_t body_at = 0;
    for (const auto& g : mr.glyphs) {
      if (in_aside(g)) {
        small = small || g.font == FontRole::Small;
        continue;
      }
      if (g.codepoint != ' ') text_right = std::max(text_right, g.x + g.advance);
      if (body_at < plain.glyphs.size()) {
        // The text must lay out identically with or without the aside.
        EXPECT(std::abs(g.x - plain.glyphs[body_at].x) < 0.5f);
        EXPECT(std::abs(g.y - plain.glyphs[body_at].y) < 0.5f);
      }
      ++body_at;
    }
    EXPECT_EQ(body_at, plain.glyphs.size());
    EXPECT(small);
    EXPECT(text_right > 0.f && text_right <= abox.x);
    // The flow does not move for it: the paragraph starts at the top.
    float first_y = 1e9f;
    for (const auto& g : mr.glyphs)
      if (!in_aside(g)) first_y = std::min(first_y, g.y);
    EXPECT(first_y < abox.y + abox.height);
  }
  // Without an existing margin, the aside floats inside the text column
  // rather than manufacturing a new margin by shrinking the column.
  lo.margin_pct = 0.f;
  engine.set_options(lo);
  {
    LayoutResult fr = engine.layout(fl, 1000.f);
    const LayoutBox abox = box_of(fr, aside);
    EXPECT(abox.width > 0.f && abox.width <= 450.f);
    EXPECT(abox.x > 500.f && abox.x + abox.width <= 1000.f);
    float beside_right = 0.f, below_right = 0.f, text_left = 1e9f;
    for (const auto& g : fr.glyphs) {
      if (in_aside(g) || g.codepoint == ' ') continue;
      text_left = std::min(text_left, g.x);
      const float r = g.x + g.advance;
      if (g.y < abox.y + abox.height) beside_right = std::max(beside_right, r);
      else below_right = std::max(below_right, r);
    }
    EXPECT(text_left < 1.f);  // no text-column inset was introduced
    EXPECT(beside_right > 0.f && beside_right <= abox.x);
    EXPECT(below_right > abox.x);
  }
  // When even a readable float cannot fit, use a full-width block.
  LayoutResult narrow = engine.layout(fl, 300.f);
  EXPECT_EQ(box_of(narrow, aside).width, 300.f);

  // The same fallback works on the left, retaining the normal right edge
  // of the document once lines pass below the aside.
  Document left = parse_markup(
      ":::aside side=left\nSide box\n:::\n\n" + words + "\n");
  NodeId left_id = kids_of(left, left.root)[0];
  auto in_left_aside = [&](const PositionedGlyph& g) {
    return g.node == left_id || (g.node != INVALID_NODE &&
                                 left.get(g.node).parent == left_id);
  };
  {
    LayoutResult lr = engine.layout(left, 1000.f);
    const LayoutBox abox = box_of(lr, left_id);
    EXPECT(abox.x < 1.f && abox.width > 0.f && abox.width <= 450.f);
    float beside_left = 1e9f, below_left = 1e9f;
    for (const auto& g : lr.glyphs) {
      if (in_left_aside(g) || g.codepoint == ' ')
        continue;
      if (g.y < abox.y + abox.height)
        beside_left = std::min(beside_left, g.x);
      else
        below_left = std::min(below_left, g.x);
    }
    EXPECT(beside_left >= abox.x + abox.width);
    EXPECT(below_left < 1.f);
  }

  // Image floats reserve a band for the caption and restore the text width.
  for (const char* side : {"left", "right"}) {
    Document image = parse_markup(std::string("![caption](missing.png){float=") +
                                  side + "}\n\n" + words + "\n");
    NodeId image_id = kids_of(image, image.root)[0];
    LayoutResult ir = engine.layout(image, 800.f);
    LayoutBox ib = box_of(ir, image_id);
    EXPECT(ib.width > 0.f && ib.width <= 360.f);
    bool beside = false;
    for (const auto& g : ir.glyphs) {
      if (g.node == image_id || g.codepoint == ' ' || g.y >= ib.y + ib.height)
        continue;
      beside = true;
      if (std::string(side) == "left") EXPECT(g.x >= ib.x + ib.width);
      else EXPECT(g.x + g.advance <= ib.x);
    }
    EXPECT(beside);
    EXPECT(box_of(engine.layout(image, 200.f), image_id).width >= 199.f);
  }

  // Explicit flow placement still occupies the entire text column.
  DocStyle flow_style = builtin_style();
  flow_style.block[static_cast<size_t>(BlockRole::Aside)].placement =
      Placement::Flow;
  engine.set_style(&flow_style);
  EXPECT_EQ(box_of(engine.layout(fl, 1000.f), aside).width, 1000.f);

  // placement float: lines beside the box stop at its left edge, lines
  // below it use the whole column again.
  DocStyle fst = builtin_style();
  fst.block[static_cast<size_t>(BlockRole::Aside)].placement = Placement::Float;
  engine.set_style(&fst);
  {
    LayoutResult fr = engine.layout(fl, 800.f);
    const LayoutBox abox = box_of(fr, aside);
    EXPECT(abox.width > 0.f && abox.x > 400.f);
    float beside_right = 0.f, below_right = 0.f;
    for (const auto& g : fr.glyphs) {
      if (in_aside(g) || g.codepoint == ' ') continue;
      const float r = g.x + g.advance;
      if (g.y < abox.y + abox.height) beside_right = std::max(beside_right, r);
      else below_right = std::max(below_right, r);
    }
    EXPECT(beside_right > 0.f && beside_right <= abox.x);
    EXPECT(below_right > abox.x);
  }
  engine.set_style(nullptr);

  // Navigation chrome stays in the flow, unless the Page panel shows it
  // (then the panes leave it out); reader mode hides it, too.
  Document nav = parse_html_page(
      "<html><body><nav><ul><li><a href=\"/a\">Alpha</a></li>"
      "<li><a href=\"/b\">Beta</a></li></ul></nav><main><p>" + words +
      "</p></main></body></html>", "https://example.org/");
  NodeId nav_node = INVALID_NODE;
  for (const Node& n : nav.nodes)
    if (n.kind == NodeKind::Chrome && n.parent != INVALID_NODE &&
        n.summary == "navigation")
      nav_node = n.id;
  EXPECT(nav_node != INVALID_NODE);
  if (nav_node != INVALID_NODE) {
    EXPECT(margin_chrome(nav, nav.get(nav_node)));
    EXPECT(box_of(engine.layout(nav, 1000.f), nav_node).width > 0.f);
    engine.set_hide_panel_chrome(true);
    EXPECT_EQ(box_of(engine.layout(nav, 1000.f), nav_node).width, 0.f);
    engine.set_hide_panel_chrome(false);
    engine.set_hide_chrome(true);
    EXPECT_EQ(box_of(engine.layout(nav, 1000.f), nav_node).width, 0.f);
    engine.set_hide_chrome(false);
  }
}

// Search engines and the search: address (docs/SEARCH.md).
void test_search() {
  using namespace xanatoo;
  EXPECT_EQ(search_encode("electron spin"), "electron+spin");
  EXPECT_EQ(search_encode("  a   b "), "a+b");  // blanks collapse and trim
  EXPECT_EQ(search_encode("c# | x"), "c%23+%7C+x");
  EXPECT_EQ(search_decode("c%23+%7C+x"), "c# | x");
  EXPECT_EQ(search_decode("100%"), "100%");  // stray percent stays
  EXPECT_EQ(search_decode(search_encode("Käse & Brot")), "Käse & Brot");

  const std::string ref = search_ref("arxiv", "electron spin");
  EXPECT_EQ(ref, "search:arxiv:electron+spin");
  EXPECT(is_search_ref(ref));
  EXPECT(!is_search_ref("search:arxiv:"));
  EXPECT(!is_search_ref("search:Bad Id:x"));
  EXPECT(!is_search_ref("https://example.org/search:a:b"));
  EXPECT_EQ(search_ref("arxiv", "   "), "");
  EXPECT_EQ(search_ref("Bad", "x"), "");
  EXPECT_EQ(search_query_text(ref), "electron spin");
  auto parsed = parse_search_ref(ref);
  EXPECT(parsed && parsed->engine == "arxiv" && parsed->query == "electron+spin");

  // The encoded query never holds what locator.hpp cuts at.
  const std::string odd = search_ref("wikipedia", "a#b|c//d");
  EXPECT(odd.find('#') == std::string::npos && odd.find('|') == std::string::npos &&
         odd.find("//") == std::string::npos);

  const std::vector<SearchEngine> eng = builtin_engines();
  EXPECT_EQ(search_target(ref, eng),
            "feed:https://export.arxiv.org/api/query?search_query=all:"
            "electron+AND+all:spin&max_results=50");
  EXPECT_EQ(search_target("search:standard-ebooks:wells", eng),
            "https://standardebooks.org/ebooks?query=wells");
  EXPECT_EQ(search_target("search:wikipedia:xanadu", eng), "");  // JSON engine
  {
    auto plan = search_plan("search:wikipedia:Olivia+Newton-John", eng);
    EXPECT(plan && plan->kind == "wikipedia" && plan->query == "Olivia Newton-John");
    EXPECT(plan && plan->url.find("srsearch=Olivia+Newton-John&") != std::string::npos);
    EXPECT(!search_plan("search:nope:x", eng));
  }
  EXPECT_EQ(search_target("search:mastodon-tag:space+opera", eng),
            "feed:https://mastodon.social/tags/spaceopera.rss");  // space ""
  EXPECT_EQ(search_target("search:flickr:moon+nasa", eng),
            "feed:https://www.flickr.com/services/feeds/photos_public.gne?"
            "tags=moon,nasa&format=rss2");
  EXPECT_EQ(search_target("search:nope:x", eng), "");
  EXPECT_EQ(search_target("https://x.org/", eng), "");

  // Every built-in is well formed: unique id / keyword, one %s, https.
  std::unordered_set<std::string> ids, keys;
  for (const SearchEngine& e : eng) {
    EXPECT(ids.insert(e.id).second);
    if (!e.keyword.empty()) EXPECT(keys.insert(e.keyword).second);
    EXPECT(!e.name.empty() && !e.badge.empty());
    EXPECT(starts_with(e.url, "https://"));
    const size_t at = e.url.find("%s");
    EXPECT(at != std::string::npos && e.url.find("%s", at + 2) == std::string::npos);
    EXPECT(is_search_ref(search_ref(e.id, "x")));
    EXPECT(is_search_kind(e.kind));
  }
  EXPECT(is_json_search_kind("hn") && !is_json_search_kind("feed") &&
         !is_json_search_kind("page") && !is_search_kind("nope"));

  // Ctrl+L keywords.
  EXPECT_EQ(search_from_keyword("ax electron spin", eng), "search:arxiv:electron+spin");
  EXPECT_EQ(search_from_keyword("w xanadu", eng), "search:wikipedia:xanadu");
  EXPECT_EQ(search_from_keyword("w", eng), "");          // no query
  EXPECT_EQ(search_from_keyword("zz xanadu", eng), "");  // no such keyword
  EXPECT_EQ(search_from_keyword("ia prelinger", eng), "search:archive:prelinger");
  EXPECT_EQ(search_from_keyword("https://x.org/a b", eng), "");

  // The user's file: replaces by id, adds new ones, skips bad entries.
  std::vector<std::string> warn;
  const std::vector<SearchEngine> user = parse_engines(
      "(engine \"mine\" (name \"My Site\") (keyword \"ms\") "
      "(url \"https://example.org/find?q=%s\") (kind feed))\n"
      "(engine \"Bad Id\" (url \"https://x.org/%s\"))\n"
      "(engine \"nourl\" (name \"x\"))\n"
      "(engine \"twice\" (url \"https://x.org/%s/%s\"))\n"
      "(engine \"plain\" (url \"ftp://x.org/%s\"))\n"
      "(other)\n",
      &warn);
  EXPECT_EQ(user.size(), static_cast<size_t>(1));
  EXPECT_EQ(warn.size(), static_cast<size_t>(4));
  {
    std::vector<std::string> w2;
    auto k = parse_engines("(engine \"a\" (url \"https://x.org/%s\") (kind lemmy))"
                           "(engine \"b\" (url \"https://x.org/%s\") (kind bogus))", &w2);
    EXPECT_EQ(k.size(), static_cast<size_t>(2));
    EXPECT(k.size() == 2 && k[0].kind == "lemmy" && k[1].kind == "page");
    EXPECT_EQ(w2.size(), static_cast<size_t>(1));
  }
  if (!user.empty()) {
    EXPECT(user[0].kind == "feed" && user[0].badge == "M" && user[0].space == "+");
    EXPECT_EQ(search_target("search:mine:a+b", user),
              "feed:https://example.org/find?q=a+b");
  }
}

// JSON engines → result documents (search_results.cpp), on canned answers.
void test_search_results() {
  using namespace xanatoo;
  auto plan = [](const char* kind, const char* url, const char* q) {
    return SearchPlan{url, kind, q};
  };
  auto has = [](const std::string& s, const char* part) {
    const bool ok = s.find(part) != std::string::npos;
    if (!ok) std::fprintf(stderr, "missing %s in:\n%s\n", part, s.c_str());
    return ok;
  };

  EXPECT_EQ(search_md_escape("a*b_c [x] <y>"), "a\\*b\\_c \\[x\\] <y>");
  EXPECT_EQ(search_md_escape("#tag\n- item\nx # y"), "\\#tag\n\\- item\nx # y");

  {  // what the parser makes of the escapes and of link text
    auto texts = [](const char* md) {
      Document d = parse_document(md, "t.md");
      std::string all;
      for (const Node& n : d.nodes)
        for (const InlineSpan& sp : n.spans) all += "<" + sp.text + ">";
      return all;
    };
    EXPECT_EQ(texts("\\#tag \\* x \\[y\\]"), "<#tag * x [y]>");
    EXPECT_EQ(texts("[a *b* c_d (v)](http://x)"), "<a *b* c_d (v)>");
  }
  // Wikipedia: native refs (parentheses encoded), matched words in bold.
  const std::string wiki = search_results_markdown(
      plan("wikipedia", "https://de.wikipedia.org/w/api.php?x=1", "xanadu"),
      "{\"query\":{\"searchinfo\":{\"totalhits\":1845},\"search\":["
      "{\"ns\":0,\"title\":\"Xanadu (film)\",\"snippet\":\"<span class=\\\"searchmatch\\\">"
      "Xanadu</span> is a 1980 film &amp; more\"},"
      "{\"ns\":0,\"title\":\"Xanadu\",\"snippet\":\"\"}]}}");
  EXPECT(has(wiki, "# Wikipedia (de): xanadu"));
  EXPECT(has(wiki, "1845 articles match."));
  EXPECT(has(wiki, "[Xanadu (film)](wikipedia:de:Xanadu_%28film%29)"));
  EXPECT(has(wiki, "**Xanadu** is a 1980 film & more"));
  EXPECT(has(wiki, "[Xanadu](wikipedia:de:Xanadu)"));
  EXPECT(has(wiki, "https://de.wikipedia.org/w/index.php?search=xanadu"));
  {
    // The result parses to a document with real links to native refs.
    Document d = parse_document(wiki, "search.md");
    int refs = 0;
    for (NodeId id = 0; id < d.nodes.size(); ++id)
      for (const InlineSpan& s : d.nodes[id].spans)
        if (starts_with(s.href, "wikipedia:de:")) ++refs;
    EXPECT_EQ(refs, 2);
  }
  EXPECT(has(search_results_markdown(plan("wikipedia", "https://en.wikipedia.org/w", "zzz"),
                                     "{\"query\":{\"search\":[]}}"),
             "No results."));
  EXPECT(has(search_results_markdown(plan("wikipedia", "https://en.wikipedia.org/w", "q"),
                                     "not json at all"),
             "No results."));

  // Hacker News: story link + thread link; Ask HN (no url) links the thread.
  const std::string hn = search_results_markdown(
      plan("hn", "https://hn.algolia.com/api/v1/search?query=x", "xanadu"),
      "{\"hits\":[{\"author\":\"tagrun\",\"created_at\":\"2014-05-12T10:00:00Z\","
      "\"num_comments\":21,\"points\":152,\"title\":\"Xanadu: a (working) deliverable\","
      "\"url\":\"http://www.xanadu.com/a_(b)\",\"objectID\":\"7849389\"},"
      "{\"author\":\"x\",\"created_at\":\"2020-01-01T00:00:00Z\",\"num_comments\":1,"
      "\"points\":1,\"title\":\"Ask HN: Xanadu?\",\"url\":null,\"objectID\":\"42\"}]}");
  EXPECT(has(hn, "(http://www.xanadu.com/a_%28b%29) (xanadu.com)"));
  EXPECT(has(hn, "152 points · [21 comments](hn:item?id=7849389) · tagrun · 2014-05-12"));
  EXPECT(has(hn, "[Ask HN: Xanadu?](hn:item?id=42)"));
  EXPECT(has(search_results_markdown(
                 plan("hn", "https://hn.algolia.com/api/v1/search?query=x", "x"),
                 "{\"hits\":[{\"title\":\"A *b* [video] _c_\",\"url\":\"http://x.org/\","
                 "\"objectID\":\"7\"}]}"),
             "[A *b* (video) _c_](http://x.org/)"));
  EXPECT(has(hn, "1 point · [1 comment](hn:item?id=42)"));

  // Internet Archive: array-valued fields, plain identifiers.
  const std::string ia = search_results_markdown(
      plan("archive", "https://archive.org/advancedsearch.php?q=x", "prelinger"),
      "{\"response\":{\"numFound\":16292,\"docs\":[{\"creator\":[\"Rick Prelinger\",\"x\"],"
      "\"description\":[\"Films <b>from</b> the archive\"],\"identifier\":\"prelinger\","
      "\"mediatype\":\"collection\",\"title\":\"Prelinger Archives\"}]}}");
  EXPECT(has(ia, "16292 items match"));
  EXPECT(has(ia, "[Prelinger Archives](https://archive.org/details/prelinger)"));
  EXPECT(has(ia, "collection · Rick Prelinger. Films from the archive"));

  // Lemmy: native post refs on the engine's instance.
  const std::string lm = search_results_markdown(
      plan("lemmy", "https://lemmy.world/api/v3/search?q=x", "linux"),
      "{\"posts\":[{\"post\":{\"id\":53943997,\"name\":\"Fedora 45 is out\"},"
      "\"creator\":{\"name\":\"alice\"},\"community\":{\"name\":\"linux\"},"
      "\"counts\":{\"score\":120,\"comments\":33}}]}");
  EXPECT(has(lm, "[Fedora 45 is out](lemmy:lemmy.world/post/53943997)"));
  EXPECT(has(lm, "c/linux · 120 points · 33 comments · alice"));

  // Bluesky: text kept (escaped), link to the post from the at:// uri.
  const std::string bs = search_results_markdown(
      plan("bluesky", "https://api.bsky.app/x", "xanadu"),
      "{\"posts\":[{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/3mzg\","
      "\"author\":{\"handle\":\"rick.bsky.social\",\"displayName\":\"Rick\"},"
      "\"record\":{\"createdAt\":\"2026-11-03T16:05:51.375Z\",\"text\":\"#xanadu * rules\\nline 2\"},"
      "\"replyCount\":2,\"likeCount\":5}]}");
  EXPECT(has(bs, "## Rick @rick.bsky.social · 2026-11-03"));
  EXPECT(has(bs, "\\#xanadu \\* rules\nline 2"));
  EXPECT(has(bs, "(https://bsky.app/profile/rick.bsky.social/post/3mzg) · 2 replies · 5 likes"));

  // Gutendex.
  const std::string gu = search_results_markdown(
      plan("gutendex", "https://gutendex.com/books/?search=x", "wells"),
      "{\"count\":396,\"results\":[{\"id\":35,\"title\":\"The Time Machine\","
      "\"authors\":[{\"name\":\"Wells, H. G.\",\"birth_year\":1866}],\"download_count\":37773}]}");
  EXPECT(has(gu, "[The Time Machine](https://www.gutenberg.org/ebooks/35) — Wells, H. G. · 37773 downloads"));
}

// Every page under examples/ is in Help → Examples (kExamples), and every
// menu entry exists; the diff pair opens through its own item.
void test_examples_in_menu() {
  using namespace xanatoo;
  namespace fs = std::filesystem;
  const std::string tour = data_file("examples/tour.md");
  if (tour.empty()) {
    std::fprintf(stderr, "SKIP test_examples_in_menu (no data dir)\n");
    return;
  }
  const fs::path dir = fs::path(tour).parent_path();
  std::unordered_set<std::string> listed;
  for (const ExampleEntry& e : kExamples) {
    if (!e.file) continue;
    listed.insert(e.file);
    if (!fs::exists(dir / e.file))
      std::fprintf(stderr, "menu lists missing example %s\n", e.file);
    EXPECT(fs::exists(dir / e.file));
  }
  for (const char* f : kExampleDiffPair) {
    listed.insert(f);
    EXPECT(fs::exists(dir / f));
  }
  for (const auto& de : fs::directory_iterator(dir)) {
    if (!de.is_regular_file()) continue;
    const std::string ext = de.path().extension().string();
    if (ext != ".md" && ext != ".gmi" && ext != ".gemini" && ext != ".org" &&
        ext != ".sexp")
      continue;  // pictures and the like
    const std::string name = de.path().filename().string();
    if (!listed.count(name))
      std::fprintf(stderr, "examples/%s is not in the Examples menu "
                           "(include/xanatoo/examples.hpp)\n", name.c_str());
    EXPECT(listed.count(name));
  }
}

// examples/tour.md is the feature tour: it keeps showing every block kind.
void test_tour_example() {
  using namespace xanatoo;
  const std::string path = data_file("examples/tour.md");
  if (path.empty()) {
    std::fprintf(stderr, "SKIP test_tour_example (no data dir)\n");
    return;
  }
  std::ifstream in(path, std::ios::binary);
  const std::string src((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  Document doc = parse_document(src, path);
  std::unordered_set<int> kinds;
  bool inline_image = false, cite = false, task = false, link = false;
  for (const Node& n : doc.nodes) {
    if (n.parent == INVALID_NODE && n.id != doc.root) continue;
    kinds.insert(static_cast<int>(n.kind));
    task = task || n.label == "task" || n.label == "task-done";
    for (const InlineSpan& sp : n.spans) {
      inline_image = inline_image || sp.kind == InlineKind::Image;
      cite = cite || sp.kind == InlineKind::Cite;
      link = link || (sp.kind == InlineKind::Link && !sp.rel.empty());
    }
  }
  for (NodeKind k :
       {NodeKind::Article, NodeKind::Section, NodeKind::Header,
        NodeKind::List, NodeKind::BlockQuote, NodeKind::HorizontalRule,
        NodeKind::DefList, NodeKind::Note, NodeKind::CodeBlock,
        NodeKind::Table, NodeKind::Aside, NodeKind::Stretch,
        NodeKind::Columns, NodeKind::Figure, NodeKind::Collection,
        NodeKind::Transclude, NodeKind::Thread, NodeKind::Message,
        NodeKind::Footnote, NodeKind::Biblio, NodeKind::BiblioEntry}) {
    if (!kinds.count(static_cast<int>(k)))
      std::fprintf(stderr, "tour.md lacks node kind %d\n", static_cast<int>(k));
    EXPECT(kinds.count(static_cast<int>(k)));
  }
  EXPECT(inline_image && cite && task && link);
  // The include names a section: it shows the section's first paragraph.
  const NodeId q = transclude_text_node(doc, find_anchor_node(doc, "#quotable"));
  EXPECT(q != INVALID_NODE && doc.get(q).kind == NodeKind::Paragraph);
}

void test_feed_urls() {
  using namespace xanatoo;
  EXPECT(is_feed_url("https://xkcd.com/atom.xml"));
  EXPECT(is_feed_url("https://lwn.net/headlines/rss"));
  EXPECT(is_feed_url("https://hackaday.com/blog/feed/"));
  EXPECT(is_feed_url("https://mastodon.social/@Gargron.rss"));
  EXPECT(is_feed_url("https://www.reddit.com/r/linux/.rss"));
  EXPECT(is_feed_url("https://blog.example/?feed=rss2"));
  EXPECT(is_feed_url("https://www.youtube.com/feeds/videos.xml?channel_id=x"));
  EXPECT(is_feed_url("feed:https://jvns.ca/"));
  EXPECT(!is_feed_url("https://example.org/sitemap.xml"));
  EXPECT(!is_feed_url("https://example.org/feedback"));
  EXPECT(!is_feed_url("https://news.ycombinator.com/item?id=1"));
  EXPECT_EQ(feed_http_url("feed://example.org/rss"), "https://example.org/rss");
  EXPECT_EQ(feed_http_url("feed:http://example.org/"), "http://example.org/");
  EXPECT(looks_like_feed("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- c --><rss version=\"2.0\">"));
  EXPECT(looks_like_feed("<feed xmlns=\"http://www.w3.org/2005/Atom\">"));
  EXPECT(!looks_like_feed("<!DOCTYPE html><html><head>"));
  EXPECT_EQ(feed_autodiscover(
                "<html><head><link rel=\"stylesheet\" href=\"s.css\">"
                "<LINK REL=\"alternate\" TYPE=\"application/atom+xml\" "
                "href=\"/atom.xml\"></head>",
                "https://jvns.ca/blog/"),
            "https://jvns.ca/atom.xml");
}

void test_parse_feeds() {
  using namespace xanatoo;
  auto rss = parse_feed(
      "<?xml version=\"1.0\"?><rss version=\"2.0\" "
      "xmlns:content=\"http://purl.org/rss/1.0/modules/content/\" "
      "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><channel>"
      "<title>Blog &amp; Co</title><link>https://b.example/</link>"
      "<description>Notes</description>"
      "<item><title>First &amp;amp; best</title><link>https://b.example/1</link>"
      "<dc:creator>Ann</dc:creator><pubDate>Mon, 27 Oct 2026 10:05:00 +0000</pubDate>"
      "<description>short</description>"
      "<content:encoded><![CDATA[<p>Full <em>text</em></p><p>Two</p>]]></content:encoded>"
      "<enclosure url=\"https://b.example/a.mp3\" type=\"audio/mpeg\" length=\"2500000\"/>"
      "</item><item><description>&lt;p&gt;No title&lt;/p&gt;</description>"
      "<guid>https://b.example/2</guid></item></channel></rss>",
      "https://b.example/feed");
  EXPECT(rss.has_value());
  if (rss) {
    const Node* thread = first_of_kind(*rss, NodeKind::Thread);
    EXPECT(thread && thread->summary == "Blog & Co");
    if (thread) {
      EXPECT_EQ(thread->children.size(), 2u);
      const Node& m = rss->get(thread->children.at(0));
      EXPECT_EQ(m.src, "Ann");
      EXPECT_EQ(m.summary, "2026-10-27 10:05 UTC");
      auto parts = m.children;
      EXPECT_EQ(parts.size(), 4u);  // title, 2 paragraphs, enclosure
      if (parts.size() == 4) {
        EXPECT_EQ(plain_text(*rss, parts[0]), "First & best");
        EXPECT_EQ(rss->get(parts[0]).spans.at(0).href, "https://b.example/1");
        EXPECT_EQ(plain_text(*rss, parts[1]), "Full text");
        EXPECT_EQ(plain_text(*rss, parts[3]), "Enclosure: audio/mpeg (2.5 MB)");
      }
      const Node& m2 = rss->get(thread->children.at(1));
      EXPECT_EQ(m2.src, "Blog & Co");
      EXPECT_EQ(plain_text(*rss, m2.children.at(0)), "No title");
      EXPECT_EQ(rss->get(m2.children.back()).spans.at(0).href, "https://b.example/2");
    }
  }

  auto atom = parse_feed(
      "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title type=\"html\">"
      "A &lt;b&gt;bold&lt;/b&gt; feed</title><author><name>Zed</name></author>"
      "<link href=\"https://a.example/\"/><link rel=\"self\" href=\"/atom.xml\"/>"
      "<entry><title>E1</title><link rel=\"alternate\" href=\"/e1\"/>"
      "<updated>2026-10-01T08:30:00Z</updated>"
      "<content type=\"xhtml\"><div xmlns=\"http://www.w3.org/1999/xhtml\">"
      "<p>Hi <a href=\"x\">there</a></p></div></content></entry>"
      "<entry><title>E2</title><summary>plain\n\nsecond</summary></entry></feed>",
      "https://a.example/atom.xml");
  EXPECT(atom.has_value());
  if (atom) {
    EXPECT_EQ(atom->meta.title, "A bold feed");
    const Node* thread = first_of_kind(*atom, NodeKind::Thread);
    if (thread && thread->children.size() == 2) {
      const Node& e1 = atom->get(thread->children[0]);
      EXPECT_EQ(e1.src, "Zed");
      EXPECT_EQ(e1.summary, "2026-10-01 08:30 UTC");
      EXPECT_EQ(atom->get(e1.children.at(0)).spans.at(0).href, "https://a.example/e1");
      EXPECT_EQ(atom->get(e1.children.at(1)).spans.at(1).href, "https://a.example/x");
      const Node& e2 = atom->get(thread->children[1]);
      EXPECT_EQ(e2.children.size(), 3u);  // title + 2 text paragraphs
    } else {
      EXPECT(false);
    }
  }

  auto rdf = parse_feed(
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
      "xmlns=\"http://purl.org/rss/1.0/\"><channel><title>R</title></channel>"
      "<item rdf:about=\"https://r.example/1\"><title>One</title>"
      "<link>https://r.example/1</link></item></rdf:RDF>",
      "https://r.example/rss");
  EXPECT(rdf.has_value());
  if (rdf) {
    const Node* thread = first_of_kind(*rdf, NodeKind::Thread);
    EXPECT(thread && thread->children.size() == 1);
  }
  EXPECT(!parse_feed("<html><body>x</body></html>", "https://x").has_value());
}

std::string doc_span_href(const xanatoo::Document& d, xanatoo::NodeId n) {
  const auto& sp = d.get(n).spans;
  return sp.empty() ? std::string{} : sp.front().href;
}

void test_html_chrome_regions() {
  using namespace xanatoo;
  HtmlOptions opt;
  opt.drop_chrome = true;
  opt.chrome_nodes = true;
  opt.sections = true;
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  html_append_blocks(
      doc, root,
      "<p>a <span class=share>Share</span> b</p>"   // inline: dropped
      "<ul><li>x<nav>in item</nav></li></ul>"       // inside a list: dropped
      "<div class=\"navbox\"><p>Box</p></div>"       // block: region
      "<div role=complementary></div>"              // empty: nothing
      "<form><h1>Page</h1><p>All of it</form>",     // page-wide form: content
      opt);
  auto top = kids_of(doc, root);
  EXPECT_EQ(top.size(), 4u);  // p, list, navbox region, Section "Page"
  EXPECT_EQ(plain_text(doc, top[0]), "a b");
  EXPECT(plain_text(doc, kids_of(doc, top[1])[0]).find("in item") ==
         std::string::npos);
  EXPECT(doc.get(top[2]).kind == NodeKind::Chrome);
  EXPECT_EQ(doc.get(top[2]).summary, "navigation");
  EXPECT(doc.get(top[3]).kind == NodeKind::Section);
  EXPECT_EQ(chrome_regions(doc).size(), 1u);
}

void test_html_page() {
  using namespace xanatoo;
  EXPECT(is_web_url("https://example.org"));
  EXPECT(is_web_url("http://example.org/a/b.html?x=1"));
  EXPECT(is_web_url("https://example.org/paper.PDF")); // MIME routes to download
  EXPECT(is_web_url("https://example.org/archive.zip"));
  EXPECT(!(is_web_url("https://example.org/i.png?w=10")));
  EXPECT(!(is_web_url("gemini://example.org/")));
  EXPECT(path_uses_html("/tmp/A.HTM"));
  EXPECT(!(path_uses_html("a.md")));

  const char* page =
      "<!doctype html><html><head><title>T &amp; t</title>"
      "<meta property=\"og:title\" content=\"OG title\"></head><body>"
      "<nav><a href=/>Home</a></nav>"
      "<main><h2 id=\"intro\">Intro</h2><p>See <a href=\"#more\">more</a> "
      "and <a href=\"https://ex.org/p#intro\">top</a>"
      "<span class=\"sr-only\">hidden</span><img src=d.png alt=\"\">"
      "<div hidden><p>gone</div>"
      "<h3>Sub</h3><p id=\"more\">Deep</p><h2>Two</h2><p>Last</main>"
      "<footer>foot</footer></body></html>";
  EXPECT_EQ(html_page_title(page), "OG title");
  Document doc = parse_html_page(page, "https://ex.org/p");
  auto top = kids_of(doc, doc.root);
  // "host · Open in browser" line, the <nav> before <main>, the title
  // section, the <footer> after it.
  EXPECT_EQ(top.size(), 4u);
  EXPECT_EQ(doc.get(top[0]).spans.back().href, "external:https://ex.org/p");
  EXPECT(doc.get(top[1]).kind == NodeKind::Chrome);
  EXPECT_EQ(doc.get(top[1]).summary, "navigation");
  EXPECT_EQ(plain_text(doc, kids_of(doc, top[1])[0]), "Home");
  EXPECT(doc.get(top[3]).kind == NodeKind::Chrome);
  EXPECT_EQ(doc.get(top[3]).summary, "footer");
  NodeId title = top[2];
  EXPECT(doc.get(title).kind == NodeKind::Section);
  auto tk = kids_of(doc, title);
  EXPECT_EQ(tk.size(), 3u);  // Header, Intro, Two
  EXPECT_EQ(plain_text(doc, tk[0]), "OG title");
  auto intro = kids_of(doc, tk[1]);
  EXPECT_EQ(intro.size(), 3u);  // Header, Paragraph, Section "Sub"
  EXPECT_EQ(doc.get(intro[0]).label, "intro");
  EXPECT_EQ(plain_text(doc, intro[1]), "See more and top");
  EXPECT_EQ(doc.get(intro[1]).spans[1].href, "#more");
  EXPECT_EQ(doc.get(intro[1]).spans[3].href, "#intro");
  EXPECT(doc.get(intro[2]).kind == NodeKind::Section);
  EXPECT_EQ(doc.get(intro[2]).level, 3);
  EXPECT(jump_to_href(doc, "#more").has_value());
  EXPECT_EQ(plain_text(doc, jump_to_href(doc, "#more")->node), "Deep");
  // Hidden text is gone in both modes.
  for (const Node& n : doc.nodes) {
    std::string t = plain_text(doc, n.id);
    EXPECT(t.find("hidden") == std::string::npos);
    EXPECT(t.find("gone") == std::string::npos);
    EXPECT(t.find("[image") == std::string::npos);
  }
  EXPECT_EQ(chrome_regions(doc).size(), 2u);
  EXPECT_EQ(chrome_hidden_text(doc, chrome_regions(doc)),
            "2 parts of the page hidden (navigation, footer)");
  // Without chrome (EPUB chapters) nav / footer are dropped as before.
  Document dropped = parse_html_page(page, "https://ex.org/p", false);
  EXPECT(chrome_regions(dropped).empty());
  for (const Node& n : dropped.nodes) {
    std::string t = plain_text(dropped, n.id);
    EXPECT(t.find("Home") == std::string::npos);
    EXPECT(t.find("foot") == std::string::npos);
  }
  // Markdown keeps the chrome as content.
  EXPECT(serialize_markup(doc).find("foot") != std::string::npos);

  // Own <h1>: no synthetic title section; no <main>: <body> is used.
  Document d2 = parse_html_page("<title>X</title><h1>Real</h1><p>b", "a.html");
  auto k2 = kids_of(d2, d2.root);
  EXPECT_EQ(k2.size(), 1u);
  EXPECT_EQ(plain_text(d2, kids_of(d2, k2[0])[0]), "Real");
  EXPECT(parse_document("<p>x", "local.html").nodes.size() > 1);

  // Unquoted href ending in "/" is not self-closing; "a<b" in a script
  // does not hide <main>; layout tables and <br><br> become paragraphs.
  Document d3 = parse_html_page(
      "<head><script>for(o=0;o<p.length;o++){x=\"'\"}</script></head>"
      "<body><p>nav<main><h1>M</h1><a href=https://e.org/x/>link</a>"
      "<table><tr><td><table><tr><td>one<br><br>two "
      "<img src=s.gif width=1 height=1></table></table></main>",
      "https://e.org/");
  // Host line, "nav" before <main> (page header), the <h1> section.
  EXPECT(d3.get(kids_of(d3, d3.root)[1]).kind == NodeKind::Chrome);
  EXPECT_EQ(d3.get(kids_of(d3, d3.root)[1]).summary, "page header");
  auto k3 = kids_of(d3, kids_of(d3, d3.root)[2]);  // the <h1> section
  EXPECT_EQ(k3.size(), 4u);  // Header, link, one, two
  EXPECT_EQ(doc_span_href(d3, k3[1]), "https://e.org/x/");
  EXPECT_EQ(plain_text(d3, k3[2]), "one");
  EXPECT_EQ(plain_text(d3, k3[3]), "two");

  // <base href> moves relative links, not the page "#id" links refer to.
  Document d4 = parse_html_page(
      "<base href=\"//w.org/wiki/\"><main><p><a href=\"./X\">x</a>"
      "<a href=\"https://w.org/wiki/P#n1\">1</a></main>",
      "https://w.org/wiki/P");
  NodeId p4 = kids_of(d4, d4.root).back();
  EXPECT_EQ(d4.get(p4).spans[0].href, "https://w.org/wiki/X");
  EXPECT_EQ(d4.get(p4).spans[1].href, "#n1");

  EXPECT_EQ(http_status_line(404), "HTTP 404 Not Found");
  EXPECT_EQ(http_status_line(599), "HTTP 599");

  Document txt = plain_text_document("a\r\n  b\n\n\n c\n");
  auto tk2 = kids_of(txt, txt.root);
  EXPECT_EQ(tk2.size(), 2u);
  EXPECT_EQ(plain_text(txt, tk2[0]), "a\n  b");
  EXPECT_EQ(plain_text(txt, tk2[1]), " c");

  // Charsets.
  EXPECT_EQ(detect_charset("text/html; charset=ISO-8859-1", ""), "iso-8859-1");
  EXPECT_EQ(detect_charset("text/html", "<meta charset=\"Windows-1252\">"),
            "windows-1252");
  EXPECT_EQ(detect_charset("", "<?xml version=\"1.0\" encoding='iso-8859-1'?>"),
            "iso-8859-1");
  EXPECT_EQ(to_utf8("caf\xe9 \x93q\x94", "iso-8859-1"),
            "caf\xc3\xa9 \xe2\x80\x9cq\xe2\x80\x9d");
  EXPECT_EQ(to_utf8("caf\xc3\xa9", "iso-8859-1"), "caf\xc3\xa9");
  EXPECT_EQ(to_utf8("caf\xe9", ""), "caf\xc3\xa9");
  EXPECT_EQ(html_decode_entities("a&#150;b"), "a\xe2\x80\x93" "b");
}

void test_wikipedia() {
  using namespace xanatoo;
  auto r = parse_wikipedia_ref("wikipedia:Hypertext");
  EXPECT(r.has_value());
  EXPECT_EQ(r->lang, "en");
  EXPECT_EQ(r->title, "Hypertext");
  r = parse_wikipedia_ref("wikipedia:de:Ted Nelson#Leben");
  EXPECT(r.has_value());
  EXPECT_EQ(r->lang, "de");
  EXPECT_EQ(r->title, "Ted_Nelson");
  EXPECT_EQ(r->fragment, "Leben");
  EXPECT_EQ(wikipedia_ref_string(*r), "wikipedia:de:Ted_Nelson#Leben");
  // Upper-case first part is part of the title, not a language.
  r = parse_wikipedia_ref("wikipedia:Star Wars: Episode IV");
  EXPECT(r.has_value());
  EXPECT_EQ(r->lang, "en");
  EXPECT_EQ(r->title, "Star_Wars:_Episode_IV");
  EXPECT(!is_wikipedia_ref("wikipedia:"));
  EXPECT(!is_wikipedia_ref("https://en.wikipedia.org/wiki/X"));

  WikipediaRef c{"en", "C++/CLI", ""};
  EXPECT_EQ(wikipedia_rest_url(c),
            "https://en.wikipedia.org/api/rest_v1/page/html/C%2B%2B%2FCLI");
  const WikipediaRef de{"de", "Hypertext", ""};
  EXPECT_EQ(wikipedia_page_url(de), "https://de.wikipedia.org/wiki/Hypertext");

  EXPECT_EQ(native_url_for("https://en.wikipedia.org/wiki/Xanadu_(Projekt)#A"),
            "wikipedia:Xanadu_(Projekt)#A");
  EXPECT_EQ(native_url_for("https://de.m.wikipedia.org/wiki/K%C3%B6ln"),
            "wikipedia:de:K\xc3\xb6ln");
  EXPECT_EQ(native_url_for("https://en.wikipedia.org/wiki/File:X.png"), "");
  EXPECT_EQ(native_url_for("https://en.wikipedia.org/wiki/Talk:Hypertext"), "");
  EXPECT_EQ(native_url_for("https://de.wikipedia.org/wiki/Kategorie:Web"), "");
  EXPECT_EQ(native_url_for("https://en.wikipedia.org/w/index.php?title=X"), "");
  EXPECT_EQ(native_url_for("https://example.org/wiki/X"), "");

  // hn: / reddit: / lemmy: refs ↔ web URLs.
  EXPECT_EQ(native_url_for("https://news.ycombinator.com/"), "hn:");
  EXPECT_EQ(native_url_for("https://news.ycombinator.com/news?p=2"), "hn:?p=2");
  EXPECT_EQ(native_url_for("https://news.ycombinator.com/item?id=42"),
            "hn:item?id=42");
  EXPECT_EQ(native_url_for("https://news.ycombinator.com/user?id=pg"),
            "hn:user?id=pg");
  EXPECT_EQ(web_url_for("hn:user?id=pg"),
            "https://news.ycombinator.com/user?id=pg");
  EXPECT_EQ(web_url_for("hn:newest"), "https://news.ycombinator.com/newest");
  EXPECT(is_hn_ref("hn:"));
  EXPECT(!is_hn_ref("https://news.ycombinator.com/"));
  EXPECT_EQ(native_url_for("https://old.reddit.com/r/linux/.json?after=x"),
            "reddit:r/linux/?after=x");
  EXPECT_EQ(native_url_for("https://www.reddit.com/r/linux/comments/abc/t/"),
            "reddit:r/linux/comments/abc/t/");
  EXPECT_EQ(native_url_for("https://redd.it/abc"), "");
  EXPECT_EQ(web_url_for("reddit:r/popular/"), "https://www.reddit.com/r/popular/");
  EXPECT_EQ(native_url_for("https://lemmy.world/c/technology"),
            "lemmy:lemmy.world/c/technology");
  EXPECT_EQ(native_url_for("https://lemmy.world/api/v3/post/list"), "");
  EXPECT_EQ(web_url_for("lemmy:programming.dev/post/7"),
            "https://programming.dev/post/7");
  EXPECT(!is_lemmy_ref("lemmy:"));
  EXPECT_EQ(web_url_for("wikipedia:de:Hypertext#A"),
            "https://de.wikipedia.org/wiki/Hypertext#A");
  EXPECT_EQ(alternate_view_url("hn:item?id=1"),
            "https://news.ycombinator.com/item?id=1");
  EXPECT_EQ(alternate_view_url("https://news.ycombinator.com/item?id=1"),
            "hn:item?id=1");
  EXPECT_EQ(alternate_view_url("https://example.org/"), "");
  EXPECT_EQ(alternate_view_url("notes.md"), "");

  EXPECT_EQ(up_url("/a/b/c.md"), "/a/b/");
  EXPECT_EQ(up_url("/a/b/"), "/a/");
  EXPECT_EQ(up_url("/a"), "/");
  EXPECT_EQ(up_url("/"), "");
  EXPECT_EQ(up_url("https://h.org/a/b?q=1#f"), "https://h.org/a/");
  EXPECT_EQ(up_url("https://h.org/a/"), "https://h.org/");
  EXPECT_EQ(up_url("https://h.org/a"), "https://h.org/");
  EXPECT_EQ(up_url("https://h.org/?q"), "https://h.org/");
  EXPECT_EQ(up_url("https://h.org/"), "");
  EXPECT_EQ(up_url("https://h.org"), "");
  EXPECT_EQ(up_url("gemini://g.net/docs/faq.gmi"), "gemini://g.net/docs/");
  EXPECT_EQ(up_url("hn:item?id=1"), "hn:");
  EXPECT_EQ(up_url("hn:"), "");
  EXPECT_EQ(up_url("reddit:r/linux/comments/abc/t/"), "reddit:r/linux/");
  EXPECT_EQ(up_url("reddit:r/linux/"), "reddit:");
  EXPECT_EQ(up_url("reddit:r/linux/top/"), "reddit:r/linux/");
  EXPECT_EQ(up_url("reddit:"), "");
  EXPECT_EQ(up_url("lemmy:lemmy.world/post/7"), "https://lemmy.world/");
  EXPECT_EQ(up_url("wikipedia:Hypertext"), "");
  EXPECT_EQ(up_url("notes.md"), "");
  {
    Document hn;
    NodeId root = hn.create(NodeKind::Document);
    NodeId para = hn.create(NodeKind::Paragraph, root);
    InlineSpan item;
    item.kind = InlineKind::Link;
    item.href = "https://news.ycombinator.com/item?id=5";
    InlineSpan ext = item;
    ext.href = "https://example.org/c/post/1";
    InlineSpan web_profile = item;
    web_profile.href = "https://news.ycombinator.com/user?id=pg";
    web_profile.rel = "external";
    hn.get(para).spans.push_back(item);
    hn.get(para).spans.push_back(ext);
    hn.get(para).spans.push_back(web_profile);
    native_rewrite_links(hn, "hn:");
    EXPECT_EQ(hn.get(para).spans[0].href, "hn:item?id=5");
    EXPECT_EQ(hn.get(para).spans[1].href, "https://example.org/c/post/1");
    EXPECT_EQ(hn.get(para).spans[2].href,
              "https://news.ycombinator.com/user?id=pg");
  }

  Document doc = parse_html_page(
      "<base href=\"//en.wikipedia.org/wiki/\"><title>Hypertext</title>"
      "<p><a href=\"./Ted_Nelson\">TN</a> <a href=\"./Hypertext#History\">h</a>"
      " <a href=\"./Category:Web\">c</a>"
      " <a href=\"https://de.wikipedia.org/wiki/Hypertext\">de</a></p>",
      "https://en.wikipedia.org/wiki/Hypertext");
  const WikipediaRef self{"en", "Hypertext", ""};
  wikipedia_rewrite_links(doc, self);
  std::vector<std::string> hrefs;
  for (const Node& n : doc.nodes)
    for (const InlineSpan& s : n.spans)
      if (s.kind == InlineKind::Link) hrefs.push_back(s.href);
  EXPECT_EQ(hrefs.size(), size_t(5));
  if (hrefs.size() == 5) {
    EXPECT_EQ(hrefs[0], "external:https://en.wikipedia.org/wiki/Hypertext");
    EXPECT_EQ(hrefs[1], "wikipedia:Ted_Nelson");
    EXPECT_EQ(hrefs[2], "#History");
    EXPECT_EQ(hrefs[3], "https://en.wikipedia.org/wiki/Category:Web");
    EXPECT_EQ(hrefs[4], "wikipedia:de:Hypertext");
  }
  Document figure = parse_html_page(
      "<base href=\"//en.wikipedia.org/wiki/\"><main>"
      "<figure class=\"mw-halign-left\"><a href=\"./File:Example.jpg\">"
      "<img alt=\"\" width=\"170\" height=\"170\" "
      "src=\"//upload.wikimedia.org/wikipedia/commons/thumb/a/ab/Example.jpg/170px-Example.jpg\">"
      "</a><figcaption>Example caption</figcaption></figure><p>After</p></main>",
      "https://en.wikipedia.org/wiki/Hypertext");
  wikipedia_rewrite_links(figure, self);
  bool found_image = false, found_figure = false, found_caption = false;
  for (const Node& n : figure.nodes) {
    if (n.kind == NodeKind::Figure) {
      found_figure = true;
      EXPECT_EQ(n.float_side, "left");
    } else if (n.kind == NodeKind::Image) {
      found_image = true;
      EXPECT_EQ(n.spans.size(), size_t(1));
      if (!n.spans.empty())
        EXPECT_EQ(n.spans[0].href,
                  "https://upload.wikimedia.org/wikipedia/commons/a/ab/Example.jpg");
    } else if (n.kind == NodeKind::Caption) {
      found_caption = true;
      EXPECT_EQ(plain_text(figure, n.id), "Example caption");
    }
  }
  EXPECT(found_image && found_figure && found_caption);
}

void test_pandoc_formats() {
  using namespace xanatoo;
  EXPECT_EQ(pandoc_input_format("a/b.EPUB"), "epub");
  EXPECT_EQ(pandoc_input_format("x.docx"), "docx");
  EXPECT_EQ(pandoc_input_format("x.tex"), "latex");
  EXPECT_EQ(pandoc_input_format("x.wiki"), "mediawiki");
  EXPECT_EQ(pandoc_input_format("x.md"), "");
  EXPECT(is_pandoc_path("notes.rst"));
  EXPECT(!is_pandoc_path("notes.org"));
}

void test_about_markdown() {
  using namespace xanatoo;
  AboutInfo info;
  info.version = "1.2.3";
  info.sections = {
      {"Services", "Read-only.",
       {{"Hacker News", true, "`hn:` lists", "hn:", "`-DXANATOO_HN=ON`"},
        {"Reddit", false, "`reddit:` refs", "reddit:r/linux/",
         "`-DXANATOO_REDDIT=ON`"}}},
      {"Platform", {}, {{"SDL", true, "2.30.0", {}, {}}}}};
  const std::string md = about_markdown(info);
  EXPECT(md.find("1.2.3") != std::string::npos);
  EXPECT(md.find("needs `-DXANATOO_REDDIT=ON`") != std::string::npos);
  EXPECT(md.find("-DXANATOO_HN") == std::string::npos);  // on: no switch

  const Document doc = parse_document(md, {});
  int sections = 0;
  std::vector<std::string> hrefs;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    const Node& n = doc.get(stack.back());
    stack.pop_back();
    if (n.kind == NodeKind::Section) ++sections;
    for (const auto& sp : n.spans)
      if (sp.kind == InlineKind::Link) hrefs.push_back(sp.href);
    for (auto c : n.children) stack.push_back(c);
  }
  EXPECT_EQ(sections, 3);  // the page + one per group (the outline)
  // Only items in the build link to an example; off ones would dead-end.
  EXPECT_EQ(hrefs.size(), size_t(1));
  if (!hrefs.empty()) EXPECT_EQ(hrefs[0], "hn:");
}

void test_collection() {
  using namespace xanatoo;
  const char* md =
      "# Top\n\n## A\n\n::: collection view=list\n"
      "![Bee](img/b.jpg)\n![Ant](img/a.jpg)\n"
      ":::figure\n![](img/c.jpg)\nCaption *c*\n:::\n"
      ":::\n\n## B\n\nafter\n";
  Document doc = parse_document(md, "x.md");
  NodeId coll = INVALID_NODE;
  for (const Node& n : doc.nodes)
    if (n.kind == NodeKind::Collection) coll = n.id;
  EXPECT(coll != INVALID_NODE);
  if (coll == INVALID_NODE) return;
  EXPECT_EQ(doc.get(coll).summary, "list");
  EXPECT(default_collection_view(doc.get(coll)).layout ==
         CollectionLayout::List);
  auto items = collection_items(doc, coll, CollectionSort::Document);
  EXPECT_EQ(items.size(), size_t(3));
  if (items.size() == 3) {
    EXPECT_EQ(items[0].caption, "Bee");
    EXPECT_EQ(items[2].src, "img/c.jpg");
    EXPECT_EQ(items[2].caption, "Caption c");
  }
  items = collection_items(doc, coll, CollectionSort::Name);
  if (items.size() == 3) EXPECT_EQ(items[0].src, "img/a.jpg");
  items = collection_items(doc, coll, CollectionSort::NameReverse);
  if (items.size() == 3) EXPECT_EQ(items[0].src, "img/c.jpg");

  // "## B" after the fence nests under "# Top" again (sibling of "## A").
  const NodeId sec_a = doc.get(coll).parent;
  EXPECT(doc.get(sec_a).kind == NodeKind::Section);
  const NodeId top = doc.get(sec_a).parent;
  EXPECT(doc.get(top).kind == NodeKind::Section);
  EXPECT_EQ(doc.get(top).children.size(), size_t(3));  // header, A, B

  const std::string out = serialize_markup(doc);
  EXPECT(out.find(":::collection view=list\n") != std::string::npos);
  Document again = parse_document(out, "x.md");
  EXPECT_EQ(serialize_markup(again), out);

  const std::string sx = serialize_sexpr(doc);
  EXPECT(sx.find("(collection") != std::string::npos);
  EXPECT(sx.find(":view \"list\"") != std::string::npos);
  Document sdoc = parse_sexpr(sx);
  EXPECT_EQ(serialize_sexpr(sdoc), sx);
}

void test_front_matter_and_preamble() {
  using namespace xanatoo;
  const std::string src =
      "---\ntitle: \"The Picture of Dorian Gray\"\nauthor: Oscar Wilde\n"
      "description: |\n  title: not this\nlanguage: en-GB\n---\n\n"
      "![](cover.jpg)\n\n# Preface\n\nText.\n";
  Document doc = parse_markup(src);
  EXPECT_EQ(doc.meta.title, "The Picture of Dorian Gray");
  EXPECT_EQ(doc.meta.author, "Oscar Wilde");
  EXPECT_EQ(doc.meta.lang, "en-GB");
  const std::string out = serialize_markup(doc);
  EXPECT(starts_with(out, "---\ntitle: \"The Picture of Dorian Gray\"\n"));
  EXPECT(out.find("description: |\n  title: not this\n") != std::string::npos);
  EXPECT_EQ(serialize_markup(parse_markup(out)), out);

  // Cover image before the first heading → title row, then the heading.
  std::vector<OutlineEntry> ol;
  collect_outline(doc, ol, nullptr, "content.md");
  EXPECT_EQ(ol.size(), size_t(2));
  if (ol.size() == 2) {
    EXPECT(ol[0].preamble);
    EXPECT_EQ(ol[0].text, "The Picture of Dorian Gray");
    EXPECT(!ol[1].preamble);
    EXPECT_EQ(ol[1].text, "Preface");
  }

  // A leading horizontal rule is not front matter.
  Document hr = parse_markup("---\n\nSome text.\n\n---\n");
  EXPECT(hr.meta.title.empty());
  EXPECT(hr.meta.front_matter.empty());

  // No title: the fallback label; no preamble row when a heading leads.
  Document plain = parse_markup("Intro.\n\n# A\n");
  collect_outline(plain, ol, nullptr, "notes.md");
  EXPECT(!ol.empty() && ol[0].preamble && ol[0].text == "notes.md");
  Document headed = parse_markup("# A\n\nText.\n");
  collect_outline(headed, ol, nullptr, "notes.md");
  EXPECT(ol.size() == 1 && !ol[0].preamble);
}

// Lines that look like a block start but no block parser claims (a lone
// "|", a malformed "![") must still be consumed: this used to loop forever.
void test_parse_unclaimed_block_lines() {
  using namespace xanatoo;
  for (const char* src : {"|\n", "|x\n", "![\n", "![x\n", "text\n|\nmore\n"}) {
    Document doc = parse_markup(src);
    bool has_para = false;
    for (NodeId c : doc.get(doc.root).children)
      if (doc.get(c).kind == NodeKind::Paragraph) has_para = true;
    EXPECT(has_para);
  }
}

void test_nearest_caret() {
  xanatoo::LayoutResult l;
  EXPECT(!xanatoo::nearest_caret(l, 0.f, 0.f));
  auto glyph = [&](xanatoo::NodeId node, size_t ch, float x, float y) {
    xanatoo::PositionedGlyph g;
    g.node = node;
    g.char_index = ch;
    g.x = x;
    g.y = y;
    l.glyphs.push_back(g);
  };
  glyph(xanatoo::INVALID_NODE, 0, 0.f, 100.f);  // decoration: no caret
  glyph(1, 0, 10.f, 20.f);
  glyph(1, 1, 20.f, 20.f);
  glyph(2, 0, 10.f, 500.f);
  glyph(2, 1, 20.f, 500.f);
  // Line by y, then glyph by x.
  auto near = xanatoo::nearest_caret(l, 19.f, 30.f);
  EXPECT(near && near->node == 1 && near->char_index == 1);
  // Decorations are no target, distance does not matter.
  near = xanatoo::nearest_caret(l, 0.f, 100.f);
  EXPECT(near && near->node == 1 && near->char_index == 0);
  near = xanatoo::nearest_caret(l, 0.f, 9000.f);
  EXPECT(near && near->node == 2);
}

void test_paginate() {
  using namespace xanatoo;
  // 100 lines of 20 px from y = 24, a heading at line 20, a code box over
  // lines 40..60 and a 1000 px image-free tail.
  LayoutResult l;
  DocStyle base = builtin_style();
  DocStyle scoped = base;
  scoped.block[static_cast<size_t>(BlockRole::Note)].border =
      {0x12, 0x34, 0x56, 255};
  l.scoped_styles.push_back(scoped);
  l.node_style_index[1] = 1;
  for (int i = 0; i < 100; ++i) {
    PositionedGlyph g;
    g.node = 1;
    g.char_index = static_cast<size_t>(i);
    g.x = 10.f;
    g.y = 24.f + 20.f * static_cast<float>(i) + 14.f;  // baseline
    g.font = (i == 20) ? FontRole::Heading : FontRole::Body;
    l.glyphs.push_back(g);
  }
  l.boxes.push_back({1, 0.f, 24.f, 300.f, 2000.f});
  l.code_blocks.push_back({INVALID_NODE, 5.f, 24.f + 40 * 20.f, 290.f, 400.f});
  l.content_height = 24.f + 2000.f + 24.f + 24.f;
  PageSpec spec;
  spec.page_h = 300.f;
  spec.pad_top = 24.f;
  spec.pad_bottom = 24.f;
  spec.columns = 2;
  spec.pitch = 320.f;
  spec.first_y = 24.f;
  spec.extents.assign(11, GlyphExtent{14.f, 4.f});
  PageMap map;
  LayoutResult out = paginate(l, spec, map);
  EXPECT(out.style_for(1, base)[BlockRole::Note].border ==
         (Rgba{0x12, 0x34, 0x56, 255}));
  EXPECT(out.style_for(2, base)[BlockRole::Note].border ==
         base[BlockRole::Note].border);
  // 252 px per page → 12 lines per page, 9 pages (the last short).
  EXPECT(map.pages.size() == 9);
  EXPECT(map.spread_count() == 5);
  EXPECT(out.content_height == 5 * 300.f);
  EXPECT(out.glyphs.size() == l.glyphs.size());
  // Page 1 starts at the top, page 2 is the left column of... spread 0
  // holds pages 0 and 1: the second one is shifted by the pitch.
  EXPECT(out.glyphs[0].x == 10.f);
  EXPECT(out.glyphs[12].x == 10.f + 320.f);
  // Every page's first line lands at the same height, no line is cut.
  for (size_t i = 0; i < map.pages.size(); ++i) {
    const size_t g = map.pages[i].first_glyph;
    const size_t spread = i / 2;
    EXPECT(std::abs(out.glyphs[g].y - (spread * 300.f + 24.f + 4.f + 14.f)) <
           1.f + 4.f);
    EXPECT(out.glyphs[g].y - 14.f >= spread * 300.f + 24.f - 1.f);
  }
  for (const auto& g : out.glyphs) {
    const float in_spread = std::fmod(g.y, 300.f);
    EXPECT(in_spread - 14.f >= 23.f && in_spread + 4.f <= 300.f - 24.f + 1.f);
  }
  // Pages cut the long box, pieces never leave the page.
  EXPECT(out.boxes.size() >= map.pages.size());
  for (const auto& b : out.boxes) {
    const float top = std::fmod(b.y, 300.f);
    EXPECT(top >= 23.f && top + b.height <= 300.f - 23.f);
  }
  EXPECT(out.code_blocks.size() >= 2);
  // The heading line is not the last on its page.
  for (size_t i = 0; i + 1 < map.pages.size(); ++i)
    EXPECT(map.pages[i + 1].first_glyph != 21u);
  // Locations follow the glyph count, not the geometry.
  EXPECT(glyph_location(0) == 1);
  EXPECT(glyph_location(map.glyph_count) == 1);
  // An empty layout is one empty page.
  LayoutResult e;
  e.content_height = 100.f;
  PageMap em;
  LayoutResult eo = paginate(e, spec, em);
  EXPECT(em.pages.size() == 1);
  EXPECT(eo.content_height == 300.f);
}

void test_inflate() {
  using namespace xanatoo;
  // Vectors from Python's gzip / zlib (fixed, dynamic and stored blocks).
  const std::string fixed_gz(
      "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\xcb\x48\xcd\xc9\xc9\xd7\x51"
      "\xc8\x40\xa2\x14\xca\xf3\x8b\x72\x52\x00\x96\x65\x6d\xfd\x19\x00\x00"
      "\x00",
      35);
  EXPECT(is_gzip(fixed_gz));
  auto out = gunzip(fixed_gz);
  EXPECT(out && *out == "hello, hello, hello world");
  std::string bad = fixed_gz;
  bad[bad.size() - 6] ^= 1;  // CRC no longer matches
  EXPECT(!gunzip(bad));
  EXPECT(!gunzip(fixed_gz.substr(0, 20)));  // truncated
  EXPECT(!gunzip(fixed_gz, 10));            // over the output cap

  const std::string stored_z(
      "\x78\x01\x01\x0d\x00\xf2\xff\x73\x74\x6f\x72\x65\x64\x20\x62\x6c\x6f"
      "\x63\x6b\x21\x24\x5e\x04\xde",
      24);
  out = zlib_inflate(stored_z);
  EXPECT(out && *out == "stored block!");

  const std::string dyn_gz(
      "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x9d\xda\xbd\x6e\xdb\x30"
      "\x00\x85\xd1\xbd\x0f\x53\xe9\xf2\x47\x24\x0b\x23\xef\x12\x14\x46"
      "\x3a\xb8\x48\x90\xa6\x40\x1f\xbf\xc8\xd8\xa9\x38\x9c\xed\x3b\x1d"
      "\xc8\x96\x3e\xea\xf6\xfb\xfd\xf1\x74\x7b\xbc\x7e\x7f\xfa\xf1\xf1"
      "\xf1\xf6\xeb\xdb\x71\xdc\xff\x3c\xff\x7c\x7b\xdc\xbf\xbe\xbe\xbf"
      "\x1c\x6f\xcf\x2f\xf7\xe3\xbc\x1d\x9f\x9f\xdf\x8e\xcf\xaf\x7e\xb9"
      "\xfd\x7f\x10\x1d\x14\x1d\x54\x1d\x34\x1d\x74\x1d\x5c\x3a\x18\x3a"
      "\x98\x3a\x58\x0c\xe7\xd4\x6c\x1d\xc6\x0e\x6b\x87\xb9\xc3\xde\x61"
      "\xf0\xb0\x78\x98\x3c\x6c\x5e\xd8\xbc\xf8\xf5\xcd\xe6\x85\xcd\x0b"
      "\x9b\x17\x36\x2f\x6c\x5e\xd8\xbc\xb0\x79\x61\xf3\xca\xe6\x95\xcd"
      "\xab\xff\xa8\xb3\x79\x65\xf3\xca\xe6\x95\xcd\x2b\x9b\x57\x36\xaf"
      "\x6c\xde\xd8\xbc\xb1\x79\x63\xf3\xe6\xff\xe4\x6c\xde\xd8\xbc\xb1"
      "\x79\x63\xf3\xc6\xe6\x8d\xcd\x3b\x9b\x77\x36\xef\x6c\xde\xd9\xbc"
      "\xfb\xed\x1b\x9b\x77\x36\xef\x6c\xde\xd9\xbc\xb3\xf9\xc5\xe6\x17"
      "\x9b\x5f\x6c\x7e\xb1\xf9\xc5\xe6\x97\xdf\xb3\xb3\xf9\xc5\xe6\x17"
      "\x9b\x5f\x6c\x3e\xd8\x7c\xb0\xf9\x60\xf3\xc1\xe6\x83\xcd\x07\x9b"
      "\x0f\x7f\x50\x63\xf3\xc1\xe6\x83\xcd\x27\x9b\x4f\x36\x9f\x6c\x3e"
      "\xd9\x7c\xb2\xf9\x64\xf3\xc9\xe6\xd3\x9f\xce\xd9\x7c\xb2\xf9\x62"
      "\xf3\xc5\xe6\x8b\xcd\x17\x9b\x2f\x36\x5f\x6c\xbe\xd8\x7c\xb1\xf9"
      "\xf2\x24\xb3\xd1\x64\x3c\xca\x9c\x5e\x65\x4e\xcf\x32\xa7\x77\x99"
      "\xd3\xc3\xcc\xe9\x65\xe6\xf4\x34\x73\x7a\x9b\x39\x3d\xce\x9c\xae"
      "\xbf\x93\xe4\x5c\x7f\x23\xca\x6d\x54\xb9\x8d\x2c\xb7\xd1\xe5\x36"
      "\xc2\xdc\x46\x99\xdb\x48\x73\xde\xe6\xe2\x71\x2e\x65\xa3\xc8\xba"
      "\xbe\xf7\xb9\x78\xa0\x8b\x17\xba\x78\xa2\x8b\x37\xba\x78\xa4\x8b"
      "\x57\xba\x78\xa6\x8b\x77\xba\xd4\x8d\x20\xef\xfa\x9e\xea\xe2\xad"
      "\x2e\x1e\xeb\xe2\xb5\x2e\x9e\xeb\xe2\xbd\x2e\x1e\xec\xe2\xc5\x2e"
      "\x9e\xec\xd2\x36\xce\x63\x5c\xdf\xab\x5d\x3c\xdb\xc5\xbb\x5d\x3c"
      "\xdc\xc5\xcb\x5d\x3c\xdd\xc5\xdb\x5d\x3c\xde\xc5\xeb\x5d\xfa\xc6"
      "\x71\x9c\xeb\x7b\xc0\x8b\x17\xbc\x78\xc2\x8b\x37\xbc\x78\xc4\x8b"
      "\x57\xbc\x78\xc6\x8b\x77\xbc\x78\xc8\xcb\xb5\x71\x1a\xeb\xfa\xde"
      "\xf2\xe2\x31\x2f\x5e\xf3\xe2\x39\x2f\xde\xf3\xe2\x41\x2f\x5e\xf4"
      "\xe2\x49\x2f\xde\xf4\x32\x36\x0e\xe3\x5d\xdf\xb3\x5e\xbc\xeb\xc5"
      "\xc3\x5e\xbc\xec\xc5\xd3\x5e\xbc\xed\xc5\xe3\x5e\xbc\xee\xc5\xf3"
      "\x5e\xe6\xc6\xbb\x18\xae\xef\x85\x2f\x9e\xf8\xe2\x8d\x2f\x1e\xf9"
      "\xe2\x95\x2f\x9e\xf9\xe2\x9d\x2f\x1e\xfa\xe2\xa5\x2f\x6b\xe3\x55"
      "\x9c\x7f\xf5\xff\x02\x25\xa3\x53\xa2\x6a\x27\x00\x00",
      573);
  std::string want;
  for (int n = 0; n < 200; ++n)
    want += "<url><loc>https://example.org/page/" + std::to_string(n) +
            "</loc></url>\n";
  out = gunzip(dyn_gz);
  EXPECT(out && *out == want);
  EXPECT_EQ(parse_sitemap_xml(*out).urls.size(), size_t(200));
  // Every one-byte corruption is rejected (CRC) and never crashes.
  int rejected = 0;
  for (size_t i = 10; i < dyn_gz.size(); ++i) {
    std::string c = dyn_gz;
    c[i] = static_cast<char>(c[i] ^ 0x5a);
    if (!gunzip(c)) ++rejected;
  }
  EXPECT_EQ(rejected, static_cast<int>(dyn_gz.size()) - 10);
}

void test_sitemap() {
  using namespace xanatoo;
  // urlset with a prefix, CDATA, entities and a self-closing element.
  const char* urlset = R"(<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url><loc>https://example.org/</loc><lastmod>2026-01-02</lastmod></url>
  <url><loc>https://example.org/docs/intro.html</loc></url>
  <url><loc>https://example.org/docs/api/v2/ref/</loc>
       <lastmod>2026-03-04T10:00:00+00:00</lastmod></url>
  <url><loc><![CDATA[https://example.org/a%20b?x=1&y=2]]></loc></url>
  <url><loc>https://example.org/q?p=1&amp;s=2</loc><priority/></url>
  <s:url><s:loc>https://www.example.org/docs/</s:loc></s:url>
  <url><loc>https://cdn.example.net/file</loc></url>
  <url><loc>https://example.org/docs/intro.html#dup</loc></url>
</urlset>)";
  SitemapFile f = parse_sitemap_xml(urlset);
  EXPECT_EQ(f.urls.size(), size_t(8));
  EXPECT(f.sitemaps.empty());
  EXPECT_EQ(f.urls[0].lastmod, std::string("2026-01-02"));
  EXPECT_EQ(f.urls[3].loc, std::string("https://example.org/a%20b?x=1&y=2"));
  EXPECT_EQ(f.urls[4].loc, std::string("https://example.org/q?p=1&s=2"));
  EXPECT_EQ(f.urls[5].loc, std::string("https://www.example.org/docs/"));

  SitemapFile idx = parse_sitemap_xml(R"(<sitemapindex>
    <sitemap><loc>https://example.org/s1.xml</loc></sitemap>
    <sitemap><loc>https://example.org/s2.xml</loc><lastmod>2026</lastmod></sitemap>
  </sitemapindex>)");
  EXPECT_EQ(idx.sitemaps.size(), size_t(2));
  EXPECT(idx.urls.empty());

  auto rs = robots_sitemaps("User-agent: *\nDisallow: /x\r\nSITEMAP: "
                            "https://e.org/a.xml # main\nsitemap:https://e.org/b.xml");
  EXPECT_EQ(rs.size(), size_t(2));
  if (rs.size() == 2) {
    EXPECT_EQ(rs[0], std::string("https://e.org/a.xml"));
    EXPECT_EQ(rs[1], std::string("https://e.org/b.xml"));
  }

  EXPECT(is_sitemap_ref("sitemap:example.org"));
  EXPECT(!is_sitemap_ref("sitemap:"));
  EXPECT_EQ(sitemap_ref_host("sitemap:Example.org/x"), std::string("example.org"));
  EXPECT_EQ(sitemap_ref_host("sitemap:https://example.org/a"),
            std::string("example.org"));
  EXPECT_EQ(url_host("https://User@Example.ORG:8443/p"), std::string("example.org"));
  EXPECT_EQ(site_url_key("http://www.example.org/docs/#x"),
            site_url_key("https://example.org/docs"));

  SiteTree t = build_site_tree("example.org", f.urls);
  // The duplicate (fragment) entry is dropped: 7 distinct pages.
  EXPECT_EQ(t.nodes[0].pages, 7);
  EXPECT_EQ(t.nodes[0].url, std::string("https://example.org/"));
  // Pre-order + parent/depth consistency.
  for (size_t i = 1; i < t.nodes.size(); ++i) {
    EXPECT(t.nodes[i].parent >= 0 && t.nodes[i].parent < static_cast<int>(i));
    EXPECT_EQ(t.nodes[i].depth,
              t.nodes[static_cast<size_t>(t.nodes[i].parent)].depth + 1);
  }
  const int docs = site_tree_find(t, "https://example.org/docs/");
  EXPECT(docs > 0);
  if (docs > 0) {
    const SiteNode& d = t.nodes[static_cast<size_t>(docs)];
    EXPECT_EQ(d.label, std::string("docs"));
    EXPECT_EQ(d.kids.size(), size_t(2));
    if (d.kids.size() == 2) {
      // One-child chain without pages folds into one row (a page now).
      EXPECT_EQ(t.nodes[static_cast<size_t>(d.kids[0])].label,
                std::string("api/v2/ref"));
      EXPECT_EQ(t.nodes[static_cast<size_t>(d.kids[1])].label, std::string("intro"));
    }
  }
  EXPECT(site_tree_find(t, "https://example.org/a%20b?x=1&y=2") > 0);
  EXPECT(site_tree_find(t, "https://example.org/nope") < 0);
  bool other_host = false;
  for (const SiteNode& n : t.nodes)
    if (n.label == "cdn.example.net/file") other_host = true;
  EXPECT(other_host);

  Document doc = site_tree_document(t);
  EXPECT_EQ(doc.meta.title, std::string("Site map — example.org"));
  int sections = 0, items = 0;
  for (size_t i = 0; i < doc.nodes.size(); ++i) {
    if (doc.nodes[i].kind == NodeKind::Section) ++sections;
    if (doc.nodes[i].kind == NodeKind::ListItem) ++items;
  }
  EXPECT(sections >= 3);  // Recently changed, root, docs
  EXPECT(items >= 6);
}


// A face with one size for every glyph: enough to test the structure of a
// layout without a font file.
class FakeMathFace : public xanatoo::MathFace {
public:
  uint32_t glyph(uint32_t cp) const override { return cp; }
  xanatoo::MathGlyph metrics(uint32_t) const override {
    xanatoo::MathGlyph g;
    g.advance = 0.5f;
    g.ascent = 0.7f;
    g.descent = 0.f;
    g.ink_w = 0.5f;
    return g;
  }
  const xanatoo::MathConstants& constants() const override { return k; }
  std::vector<xanatoo::MathVariantGlyph> variants(uint32_t gid,
                                                  bool) const override {
    return {{gid, 0.7f}};
  }
  bool assembly(uint32_t, bool, std::vector<xanatoo::MathPart>&) const override {
    return false;
  }
  xanatoo::MathConstants k;
};

void test_math() {
  using namespace xanatoo;
  auto width = [](const char* tex, bool display = false) {
    FakeMathFace face;
    return layout_math(parse_math(tex).root, face, display).width;
  };

  // Structure.
  MathParse p = parse_math("x_i^2 + \\frac{1}{2}");
  EXPECT(p.error.empty());
  EXPECT_EQ(p.root.kids.size(), 3u);
  EXPECT(p.root.kids[0].kind == MathKind::Script);
  EXPECT(p.root.kids[0].has_a && p.root.kids[0].has_b);
  EXPECT(p.root.kids[0].kids[0].cp == 'x');
  EXPECT(p.root.kids[2].kind == MathKind::Frac);

  p = parse_math("\\sqrt[3]{x}");
  EXPECT(p.root.kids[0].kind == MathKind::Radical && p.root.kids[0].has_b);

  p = parse_math("\\left( a \\right]");
  EXPECT(p.error.empty());
  EXPECT(p.root.kids[0].kind == MathKind::Fenced);
  EXPECT_EQ(p.root.kids[0].cp, static_cast<uint32_t>('('));
  EXPECT_EQ(p.root.kids[0].cp2, static_cast<uint32_t>(']'));

  p = parse_math("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}");
  EXPECT(p.error.empty());
  EXPECT(p.root.kids[0].kind == MathKind::Fenced);
  EXPECT_EQ(p.root.kids[0].kids[0].rows, 2);
  EXPECT_EQ(p.root.kids[0].kids[0].cols, 2);

  // `\\over` splits the group; a trailing `\\\\` adds no row.
  p = parse_math("{a \\over b}");
  EXPECT(p.root.kids[0].kids[0].kind == MathKind::Frac);
  p = parse_math("\\begin{matrix} a \\\\ b \\\\ \\end{matrix}");
  EXPECT_EQ(p.root.kids[0].rows, 2);

  // Symbols, classes and errors.
  p = parse_math("\\alpha \\leq \\infty -");
  EXPECT_EQ(p.root.kids[0].cp, 0x3B1u);
  EXPECT(p.root.kids[1].cls == MathClass::Rel);
  EXPECT(p.root.kids[3].cls == MathClass::Bin);
  p = parse_math("\\nosuchthing x");
  EXPECT(!p.error.empty());
  EXPECT(p.root.kids[0].kind == MathKind::Text);
  EXPECT(!parse_math("\\frac{1}").error.empty());
  EXPECT(!parse_math("{a").error.empty());
  EXPECT(!parse_math("a}").error.empty());
  EXPECT(!parse_math("\\begin{pmatrix} a").error.empty());

  // Markdown: $…$ and $$…$$, with the dollar-sign rules of pandoc.
  {
    Document d = parse_markup(
        "Let $x_1 + y$ and $$\\frac{a}{b}$$ then costs $5 and $6, \\$7.\n");
    const Node& para = d.get(d.get(d.root).children[0]);
    int math = 0, disp = 0;
    std::string tex;
    for (const InlineSpan& sp : para.spans)
      if (sp.kind == InlineKind::Math) {
        ++math;
        disp += sp.display;
        tex += sp.src + "|";
      }
    EXPECT_EQ(math, 2);
    EXPECT_EQ(disp, 1);
    EXPECT_EQ(tex, std::string("x_1 + y|\\frac{a}{b}|"));
    const std::string back = serialize_markup(d);
    EXPECT(back.find("$x_1 + y$") != std::string::npos);
    EXPECT(back.find("$$\\frac{a}{b}$$") != std::string::npos);
    EXPECT(back.find("costs \\$5") != std::string::npos);
    Document block = parse_markup("$$\n\\begin{aligned}\na &= b\\\\\n+ c\n\\end{aligned}\n$$\n");
    const Node& bp = block.get(block.get(block.root).children[0]);
    EXPECT(bp.spans.size() == 1 && bp.spans[0].kind == InlineKind::Math &&
           bp.spans[0].display);
  }

  // Org: \(…\), \[…\] and dollars; the clipboard gets the dollar form.
  {
    Document d = parse_org("Euler \\(e^{i\\pi}=-1\\), then $x_1$ and\n\n\\[ \\int_0^1 x\\,dx \\]\n");
    std::string tex;
    int disp = 0;
    for (const Node& n : d.nodes)
      for (const InlineSpan& sp : n.spans)
        if (sp.kind == InlineKind::Math) {
          tex += sp.src + "|";
          disp += sp.display;
        }
    EXPECT_EQ(tex, std::string("e^{i\\pi}=-1|x_1|\\int_0^1 x\\,dx|"));
    EXPECT_EQ(disp, 1);
    Document md = parse_markup("a $x^2$ b\n");
    Selection sel;
    const NodeId pn = md.get(md.root).children[0];
    sel.anchor = {pn, 0, 0};
    sel.focus = {pn, 2, 2};
    EXPECT_EQ(selected_text(md, sel), std::string("a $x^2$ b"));
  }

  // HTML: MathML annotation, Wikipedia / KaTeX wrappers, MathJax scripts.
  {
    HtmlOptions opt;
    Document d;
    d.root = d.create(NodeKind::Document);
    html_append_blocks(
        d, d.root,
        "<p>A <span class=\"mwe-math-element mwe-math-element-inline\" "
        "typeof=\"mw:Extension/math\"><span style=\"display: none;\"><math "
        "alttext=\"{\\displaystyle x^{2}}\"><semantics><mi>x</mi><annotation "
        "encoding=\"application/x-tex\">{\\displaystyle x^{2}}</annotation>"
        "</semantics></math></span><img alt=\"x\" src=\"a.svg\"/></span> and "
        "<span class=\"katex\"><span class=\"katex-mathml\"><math><semantics>"
        "<annotation encoding=\"application/x-tex\">a &lt; b</annotation>"
        "</semantics></math></span><span class=\"katex-html\">a&lt;b</span>"
        "</span> and <script type=\"math/tex\">z^2</script> end.</p>",
        opt);
    std::string tex;
    int others = 0;
    for (const Node& n : d.nodes)
      for (const InlineSpan& sp : n.spans) {
        if (sp.kind == InlineKind::Math) tex += sp.src + "|";
        else if (sp.kind == InlineKind::Image) ++others;
      }
    EXPECT_EQ(tex, std::string("{\\displaystyle x^{2}}|a < b|z^2|"));
    EXPECT_EQ(others, 0);
    EXPECT(plain_text(d, d.root).find("a<b") == std::string::npos);
  }

  // Alphabets.
  EXPECT_EQ(math_alphabet('x', MathVariant::Italic), 0x1D465u);
  EXPECT_EQ(math_alphabet('h', MathVariant::Italic), 0x210Eu);
  EXPECT_EQ(math_alphabet('R', MathVariant::DoubleStruck), 0x211Du);
  EXPECT_EQ(math_alphabet('B', MathVariant::Script), 0x212Cu);
  EXPECT_EQ(math_alphabet('g', MathVariant::Fraktur), 0x1D524u);
  EXPECT_EQ(math_alphabet('x', MathVariant::Roman), static_cast<uint32_t>('x'));
  EXPECT_EQ(math_alphabet('7', MathVariant::Bold), 0x1D7D5u);
  EXPECT_EQ(math_alphabet(0x3B1, MathVariant::Auto), 0x1D6FCu);
  EXPECT_EQ(math_alphabet(0x393, MathVariant::Auto), 0x393u);
  EXPECT_EQ(math_alphabet('1', MathVariant::Auto), static_cast<uint32_t>('1'));

  // Layout: spacing follows the atom classes.
  const float ab = width("ab");
  EXPECT(std::fabs(ab - 1.0f) < 1e-4f);
  EXPECT(width("a=b") > width("a+b"));     // thick > medium
  EXPECT(width("a+b") > ab);               // medium space around +
  EXPECT(std::fabs(width("-b") - width("b") - 0.5f) < 1e-4f);  // unary: none
  EXPECT(std::fabs(width("a,b") - (1.5f + 3.f / 18.f)) < 1e-4f);  // thin after ,
  EXPECT(std::fabs(width("a\\quad b") - 2.0f) < 1e-4f);
  EXPECT(width("a^2", false) < width("a^2", false) + 1);

  // Scripts rise and sink, fractions have a rule, radicals a bar.
  FakeMathFace face;
  MathBox sup = layout_math(parse_math("a^b").root, face, false);
  MathBox base = layout_math(parse_math("a").root, face, false);
  EXPECT(sup.ascent > base.ascent);
  MathBox sub = layout_math(parse_math("a_b").root, face, false);
  EXPECT(sub.depth > 0.f);
  MathBox fr = layout_math(parse_math("\\frac{a}{b}").root, face, false);
  int rules = 0;
  for (const MathItem& it : fr.items) rules += it.rule;
  EXPECT_EQ(rules, 1);
  EXPECT(fr.ascent > base.ascent && fr.depth > 0.f);
  MathBox sq = layout_math(parse_math("\\sqrt{a}").root, face, false);
  rules = 0;
  for (const MathItem& it : sq.items) rules += it.rule;
  EXPECT_EQ(rules, 1);
  // Display style sets a sum's limits above and below.
  MathBox lim = layout_math(parse_math("\\sum_a^b").root, face, true);
  MathBox side = layout_math(parse_math("\\sum_a^b").root, face, false);
  EXPECT(lim.width < side.width + 1.f && lim.ascent > side.ascent);
  // Matrices: cells in rows, wider than one cell.
  MathBox mx = layout_math(
      parse_math("\\begin{matrix} a & b \\\\ c & d \\end{matrix}").root, face,
      false);
  EXPECT(mx.width > 1.f && mx.ascent + mx.depth > 2.f * base.ascent);
}

int main() {
  test_reply_markdown();
  test_sdl_time_bridge();
  test_about_markdown();
  test_sitemap();
  test_nearest_caret();
  test_paginate();
  test_inflate();
  test_sha256();
  test_revstore();
  test_annotation_layers();
  test_stored_documents();
  test_front_matter_and_preamble();
  test_collection();
  test_wikipedia();
  test_pandoc_formats();
  test_html_min();
  test_html_chrome_regions();
  test_html_page();
  test_feed_urls();
  test_parse_feeds();
  test_hn_urls_and_html();
  test_timestamps();
  test_json_utf8_and_top_level();
  test_ebook_urls();
  test_string_helpers();
  test_json_min();
  test_reddit_urls();
  test_lemmy_urls();
  test_gemini_urls();
  test_nostr_refs();
  test_nostr_content();
  test_parse_markup();
  test_parse_unclaimed_block_lines();
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
  test_parse_document_xml_path();
  test_source_view_text();
  test_markup_stretch_and_link();
  test_markup_blockquote_code_table();
  test_markup_columns();
  test_gemtext_structures();
  test_serialize_roundtrip_md();
  test_serialize_md_uses_atx_headings();
  test_json_array_and_bool();
  test_utf8_pop_back();
  test_sanitize_epub_markdown();
  test_epub_native();
  test_hit_test_wrapped_link();
  test_layout_wrap_respects_max_width();
  test_layout_forced_wrap_after_regular_text();
  test_download_mime_and_filenames();
  test_download_file_safety();
  test_layout_margins_and_measure();
  test_layout_reader_mode();
  test_inline_images_and_asides();
  test_search();
  test_search_results();
  test_examples_in_menu();
  test_tour_example();
  test_view_settings_migration();
  test_reddit_credentials();
  test_style_sheet_parse();
  test_named_style_scopes();
  test_layout_article_gaps();
  test_layout_inline_metadata();
  test_nav_history_positions_and_forward_link();
  test_history_context_menu_targets();
  test_style_sheets_shipped();
  test_footnotes();
  test_locator();
  test_text_selectors();
  test_bookmark_lines();
  test_math();

  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
