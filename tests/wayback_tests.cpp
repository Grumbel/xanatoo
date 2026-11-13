// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
// Offline tests for the Wayback Machine refs.
#include "xanatoo/wayback.hpp"

#include <cassert>

using namespace xanatoo;

int main() {
  auto a = parse_wayback_ref("wayback:https://example.com/a?b=1");
  assert(a && a->timestamp.empty() && a->url == "https://example.com/a?b=1");
  auto b = parse_wayback_ref("wayback:20150301/http://example.com/");
  assert(b && b->timestamp == "20150301" && b->url == "http://example.com/");
  assert(!parse_wayback_ref("wayback:"));
  assert(!parse_wayback_ref("wayback:example.com"));
  assert(!parse_wayback_ref("wayback:123/https://x.org/"));  // too short
  assert(!parse_wayback_ref("https://example.com/"));
  assert(wayback_ref_string(*b) == "wayback:20150301/http://example.com/");
  assert(wayback_ref_string(*a) == "wayback:https://example.com/a?b=1");
  assert(wayback_snapshot_url(*a) ==
         "https://web.archive.org/web/2id_/https://example.com/a?b=1");
  assert(wayback_snapshot_url(*b) ==
         "https://web.archive.org/web/20150301id_/http://example.com/");

  auto s = wayback_ref_from_snapshot_url(
      "https://web.archive.org/web/20210101000012id_/http://example.com/x");
  assert(s && s->timestamp == "20210101000012" && s->url == "http://example.com/x");
  s = wayback_ref_from_snapshot_url("http://web.archive.org/web/2021/https://e.org/");
  assert(s && s->timestamp == "2021" && s->url == "https://e.org/");
  assert(!wayback_ref_from_snapshot_url("https://web.archive.org/save/https://e.org/"));
  assert(!wayback_ref_from_snapshot_url("https://example.com/web/2021/https://e.org/"));

  assert(wayback_lookup_ref("https://example.com/p#frag") ==
         "wayback:https://example.com/p");
  assert(wayback_lookup_ref("https://web.archive.org/web/2021/https://e.org/").empty());
  assert(wayback_lookup_ref("https://archive.org/details/x").empty());
  assert(wayback_lookup_ref("/home/user/a.md").empty());
  assert(wayback_lookup_ref("hn:item?id=1").empty());

  assert(wayback_timestamp_label("20150301123456") == "2015-03-01 12:34:56");
  assert(wayback_timestamp_label("2015") == "2015-01-01 00:00:00");
  assert(wayback_timestamp_label("20x5").empty());
  return 0;
}
