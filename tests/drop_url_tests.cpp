// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/drop_url.hpp"

#include <cassert>
#include <string>

int main() {
  using xanatoo::dropped_navigation_target;
  assert(dropped_navigation_target(" https://example.org/a?q=1#part\r\n") ==
         "https://example.org/a?q=1#part");
  assert(dropped_navigation_target("# from browser\r\n\r\n"
                                   "https://example.org/one\r\n"
                                   "https://example.org/two\r\n") ==
         "https://example.org/one");
  assert(dropped_navigation_target("Title of page\nhttps://example.org/") ==
         "https://example.org/");
  assert(dropped_navigation_target("gemini://geminiprotocol.net/") ==
         "gemini://geminiprotocol.net/");
  assert(dropped_navigation_target("hn:item?id=42") == "hn:item?id=42");
  assert(dropped_navigation_target("file:///tmp/a%20file.md") ==
         "/tmp/a file.md");
  assert(dropped_navigation_target("file://localhost/tmp/a%23b.md") ==
         "/tmp/a#b.md");
  for (const std::string s : {"just some prose", "javascript:alert(1)",
                              "data:text/html,a", "https://", "file://remote/x",
                              "file:///tmp/%GG.md", "http:///missing-host"})
    assert(dropped_navigation_target(s).empty());
}
