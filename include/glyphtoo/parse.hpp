// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "glyphtoo/types.hpp"
#include <string>
#include <string_view>

namespace glyphtoo {

// Minimal Markdown-ish parser for the proposal sample syntax.
// Supports:
//   # Heading
//   ## Heading 2
//   paragraphs (blank-line separated)
//   *emphasis*  **strong**
//   [link text](#target) or [link text](url)
//   :::stretch ... :::   (treated as a block container for now)
//   :::columns / :::column  (containers)
//   ![alt](src)
//
// Not a full CommonMark implementation — deliberately small.
Document parse_markup(std::string_view source);

}  // namespace glyphtoo
