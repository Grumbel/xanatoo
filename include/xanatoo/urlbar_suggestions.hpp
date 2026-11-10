// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace xanatoo {

enum class UrlBarSource { Tab, Bookmark, History };

struct UrlBarSuggestion {
  std::string address;
  std::string title;
  UrlBarSource source = UrlBarSource::History;
  int tab_index = -1;  // only meaningful for Tab entries
};

// Sources are supplied newest-first within each category, with Tabs and
// Bookmarks before History. Matches are case-insensitive for ASCII URLs and
// titles; no network request is made while typing.
inline std::vector<UrlBarSuggestion> urlbar_suggestions(
    std::string_view query, const std::vector<UrlBarSuggestion>& sources,
    size_t limit = 8) {
  auto lower = [](std::string_view text) {
    std::string out(text);
    for (char& c : out)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
  };
  const auto first = query.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) query = {};
  else {
    const auto last = query.find_last_not_of(" \t\r\n");
    query = query.substr(first, last - first + 1);
  }
  const std::string needle = lower(query);
  struct Match { UrlBarSuggestion suggestion; int score; };
  std::vector<Match> matches;
  std::unordered_set<std::string> seen;
  for (size_t i = 0; i < sources.size(); ++i) {
    const UrlBarSuggestion& s = sources[i];
    if (s.address.empty()) continue;
    int score = 0;
    if (!needle.empty()) {
      const std::string address = lower(s.address);
      const std::string title = lower(s.title);
      const size_t in_addr = address.find(needle);
      const size_t in_title = title.find(needle);
      if (in_addr == std::string::npos && in_title == std::string::npos)
        continue;
      if (in_addr == 0) score = 700;
      else if (in_addr != std::string::npos) {
        const size_t scheme = address.find("://");
        score = scheme != std::string::npos && in_addr == scheme + 3
                    ? 650 : 400;
      }
      if (in_title == 0) score = std::max(score, 680);
      else if (in_title != std::string::npos) score = std::max(score, 500);
    }
    if (s.source == UrlBarSource::Tab) score += 60;
    else if (s.source == UrlBarSource::Bookmark) score += 35;
    if (!seen.insert(s.address).second) continue;
    matches.push_back({s, score});
  }
  std::stable_sort(matches.begin(), matches.end(),
                   [](const Match& a, const Match& b) {
                     return a.score > b.score;
                   });
  std::vector<UrlBarSuggestion> out;
  out.reserve(std::min(limit, matches.size()));
  for (const Match& match : matches) {
    if (out.size() == limit) break;
    out.push_back(match.suggestion);
  }
  return out;
}

}  // namespace xanatoo
