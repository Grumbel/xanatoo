// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/urlbar_suggestions.hpp"

#include <cassert>
#include <vector>

int main() {
  using xanatoo::UrlBarSource;
  using xanatoo::UrlBarSuggestion;
  using xanatoo::urlbar_suggestions;
  const std::vector<UrlBarSuggestion> sources = {
      {"https://example.org/docs", "Documentation", UrlBarSource::Tab, 3},
      {"https://example.org/docs", "My docs", UrlBarSource::Bookmark},
      {"https://docs.example.net", "Reference", UrlBarSource::Bookmark},
      {"https://y.example.net", "Test entry", UrlBarSource::History},
      {"https://example.org/new", "Newest page", UrlBarSource::History},
  };
  const auto all = urlbar_suggestions("", sources);
  assert(all.size() == 4);  // the tab wins over the duplicate bookmark
  assert(all.front().source == UrlBarSource::Tab);
  assert(all.front().tab_index == 3);
  assert(urlbar_suggestions("  DOCS ", sources).size() == 2);
  assert(urlbar_suggestions("REFERENCE", sources).front().address ==
         "https://docs.example.net");
  const auto titled = urlbar_suggestions("My docs", sources);
  assert(titled.size() == 1);
  assert(titled.front().source == UrlBarSource::Bookmark);
  assert(urlbar_suggestions("missing", sources).empty());
  assert(urlbar_suggestions("example", sources, 2).size() == 2);
  assert(urlbar_suggestions("  ", sources).size() == all.size());
  assert(urlbar_suggestions("", sources, 0).empty());
}
