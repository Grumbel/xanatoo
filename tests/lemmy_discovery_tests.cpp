// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/lemmy_directory.hpp"

#include <cassert>
#include <string_view>

int main() {
  using namespace xanatoo;
  assert(lemmy_instance_host("https://LeMMy.World/c/technology") == "lemmy.world");
  assert(lemmy_instance_host("programming.dev") == "programming.dev");
  assert(lemmy_instance_host("https://foo@lemmy.world").empty());
  assert(lemmy_instance_host("localhost").empty());
  assert(lemmy_instance_host("127.0.0.1").empty());
  assert(lemmy_instance_host("http://example.com:8080").empty());
  assert(lemmy_instance_host("https://-bad.example").empty());
  assert(lemmy_query_escape("hello world &ü") == "hello%20world%20%26%C3%BC");

  constexpr std::string_view response = R"({
    "posts":[{"post":{"id":42,"name":"Lemmy release","published":"2026-01-01"},
              "creator":{"name":"alice","actor_id":"https://elsewhere.example/u/alice"},
              "community":{"name":"linux","title":"GNU/Linux","actor_id":"https://lemmy.world/c/linux"},
              "counts":{"score":13}}],
    "comments":[{"comment":{"id":99,"content":"hello"},
                 "creator":{"name":"bob","actor_id":"https://lemmy.world/u/bob"}}],
    "communities":[{"community":{"name":"technology","title":"Tech"},
                    "counts":{"subscribers":123}}],
    "users":[{"person":{"name":"charlie","display_name":"Charlie"},
              "counts":{"post_count":5}}]
  })";
  auto entries = lemmy_discover_payload("lemmy.world", response);
  const auto contains = [&](LemmyEntryKind kind, std::string_view name,
                            std::string_view ref, int64_t score) {
    for (const auto& e : entries)
      if (e.kind == kind && e.name == name && e.ref == ref && e.score == score)
        return true;
    return false;
  };
  assert(contains(LemmyEntryKind::Instance, "lemmy.world",
                  "lemmy:lemmy.world/api/v3/post/list?limit=25&sort=Hot", 0));
  assert(contains(LemmyEntryKind::Post, "42", "lemmy:lemmy.world/post/42", 13));
  assert(contains(LemmyEntryKind::Person, "alice@elsewhere.example",
                  "lemmy:lemmy.world/u/alice@elsewhere.example", 0));
  assert(contains(LemmyEntryKind::Person, "bob", "lemmy:lemmy.world/u/bob", 0));
  assert(contains(LemmyEntryKind::Community, "technology",
                  "lemmy:lemmy.world/c/technology", 123));
  assert(contains(LemmyEntryKind::Person, "charlie",
                  "lemmy:lemmy.world/u/charlie", 5));

  constexpr std::string_view peers = R"({"federated_instances":{
    "linked":[{"domain":"feddit.org"},{"domain":"other.example"}],
    "allowed":[{"domain":"programming.dev"}],
    "blocked":[{"domain":"blocked.example"}]}})";
  auto discovered = lemmy_discover_payload("lemmy.world", peers);
  bool linked = false, allowed = false, blocked = false;
  for (const auto& e : discovered) {
    if (e.name == "other.example") linked = true;
    if (e.name == "programming.dev") allowed = true;
    if (e.name == "blocked.example") blocked = true;
  }
  assert(linked && allowed && !blocked);
  assert(lemmy_discover_payload("localhost", response).empty());
}
