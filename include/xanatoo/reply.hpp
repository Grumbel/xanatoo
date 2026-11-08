// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace xanatoo {

// Markdown formatting for the mock reply editor. Offsets are UTF-8 byte
// offsets as supplied by ImGui's text widget (not character indices).
struct ReplyMarkup {
  std::string text;
  size_t selection_begin = 0;
  size_t selection_end = 0;
};

inline ReplyMarkup wrap_reply_markup(std::string_view text, size_t begin,
                                      size_t end, std::string_view before,
                                      std::string_view after) {
  begin = std::min(begin, text.size());
  end = std::min(end, text.size());
  if (begin > end) std::swap(begin, end);
  ReplyMarkup result;
  result.text.reserve(text.size() + before.size() + after.size());
  result.text.append(text.substr(0, begin));
  result.text.append(before);
  result.text.append(text.substr(begin, end - begin));
  result.text.append(after);
  result.text.append(text.substr(end));
  result.selection_begin = begin + before.size();
  result.selection_end = end + before.size();
  return result;
}

}  // namespace xanatoo
