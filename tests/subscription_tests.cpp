// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "xanatoo/subscriptions.hpp"

namespace fs = std::filesystem;

namespace {
std::string test_dir;
bool saving_disabled = false;
}

// Isolate the storage format from the platform directory/SDL implementation:
// the production persistence code is linked and exercised with a temp state dir.
namespace xanatoo {
std::string state_dir() { return test_dir; }
bool no_save() { return saving_disabled; }
bool write_file_atomic(const std::string& path, std::string_view data) {
  const std::string tmp = path + ".tmp";
  { std::ofstream out(tmp, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }
  fs::rename(tmp, path);
  return true;
}
}  // namespace xanatoo

int main() {
  using namespace xanatoo;
  Subscription s;
  s.address = "lemmy:lemmy.world/c/technology";
  assert(subscription_due(s, 100));
  assert(subscription_update(s, {"id:101", "id:100"}, 100) == 0);
  assert(s.initialized && s.unread == 0);
  assert(!subscription_due(s, 100));
  assert(!subscription_due(s, 999));
  assert(subscription_due(s, 1000));
  assert(subscription_update(s, {"id:102", "id:101"}, 200) == 1);
  assert(s.unread == 1);
  assert(subscription_update(s, {"id:102", "id:101"}, 300) == 0);
  assert(s.unread == 1);
  assert(subscription_update(s, {"id:103", "id:103", "id:102"}, 301) == 1);
  assert(s.recent_ids.size() == 2 && s.unread == 2);
  s.pending_job = 4;
  s.refresh_requested = true;
  assert(!subscription_due(s, 10000));
  s.pending_job = 0;
  assert(subscription_due(s, 301));
  s.refresh_requested = false;

  Document posts;
  NodeId root = posts.create(NodeKind::Document);
  NodeId thread = posts.create(NodeKind::Thread, root);
  NodeId m1 = posts.create(NodeKind::Message, thread);
  posts.get(m1).label = "1234";
  posts.get(m1).summary = "2 pts";
  NodeId m2 = posts.create(NodeKind::Message, thread);
  posts.get(m2).label = "5678";
  auto ids = subscription_item_keys(posts);
  assert((ids == std::vector<std::string>{"id:1234", "id:5678"}));
  posts.get(m1).summary = "300 pts";
  assert(subscription_item_keys(posts) == ids); // score is not a new item
  NodeId story = posts.create(NodeKind::Article, thread);
  posts.get(story).label = "hn-345";
  assert(subscription_item_keys(posts).size() == 3);

  Document rss;
  root = rss.create(NodeKind::Document);
  thread = rss.create(NodeKind::Thread, root);
  NodeId a = rss.create(NodeKind::Message, thread);
  rss.get(a).label = "e1";
  NodeId p = rss.create(NodeKind::Paragraph, a);
  InlineSpan link;
  link.kind = InlineKind::Link;
  link.href = "https://example.org/post-one";
  rss.get(p).spans.push_back(link);
  NodeId b = rss.create(NodeKind::Message, thread);
  rss.get(b).label = "e2";
  p = rss.create(NodeKind::Paragraph, b);
  InlineSpan second_link;
  second_link.kind = InlineKind::Link;
  second_link.href = "https://example.org/post-two";
  rss.get(p).spans.push_back(std::move(second_link));
  auto rss_ids = subscription_item_keys(rss);
  assert((rss_ids == std::vector<std::string>{
      "link:https://example.org/post-one", "link:https://example.org/post-two"}));
  rss.get(a).label = "e2"; // reorder must not matter
  rss.get(b).label = "e1";
  assert(subscription_item_keys(rss) == rss_ids);

  test_dir = (fs::temp_directory_path() /
      ("xanatoo-subscriptions-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()))).string();
  assert(fs::create_directory(test_dir));
  s.title = "A \"quoted\" community";
  s.recent_ids = {"id:102", "id:101"};
  std::vector<Subscription> all{s};
  assert(save_subscriptions(all));
  auto restored = load_subscriptions();
  assert(restored.size() == 1);
  assert(restored[0].address == s.address);
  assert(restored[0].title == s.title);
  assert(restored[0].interval_minutes == s.interval_minutes);
  assert(restored[0].recent_ids == s.recent_ids);
  assert(restored[0].unread == s.unread);
  assert(restored[0].initialized);
  saving_disabled = true;
  all.clear();
  assert(save_subscriptions(all));
  assert(load_subscriptions().size() == 1);  // --no-save does not erase
  fs::remove(fs::path(test_dir) / "subscriptions.txt");
  fs::remove(test_dir);
}
