// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/autolink.hpp"

#include <cassert>
#include <string>

static std::vector<std::string> urls(const std::string& s) {
  std::vector<uint32_t> cps(s.begin(), s.end());  // ASCII only here
  std::vector<std::string> out;
  for (auto [b, e] : xanatoo::find_urls(cps)) out.push_back(s.substr(b, e - b));
  return out;
}

int main() {
  using V = std::vector<std::string>;
  assert(urls("see https://example.org/a?q=1, then") ==
         V{"https://example.org/a?q=1"});
  assert(urls("(http://example.org/x)") == V{"http://example.org/x"});
  assert(urls("https://en.wikipedia.org/wiki/Foo_(bar).") ==
         V{"https://en.wikipedia.org/wiki/Foo_(bar)"});
  assert(urls("HTTP://EXAMPLE.ORG and http://b.example/") ==
         (V{"HTTP://EXAMPLE.ORG", "http://b.example/"}));
  assert(urls("xhttps://nope.example").empty());
  assert(urls("https:// alone").empty());
  assert(urls("no links here").empty());
  assert(urls("<https://a.example/>") == V{"https://a.example/"});
}
