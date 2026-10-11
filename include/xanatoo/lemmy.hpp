// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// Lemmy community / post / API URLs, e.g.:
//   https://lemmy.world/c/asklemmy
//   https://programming.dev/post/12345
//   https://lemmy.world/api/v3/post/list?community_name=linux
// Any host is accepted when the path matches /c/, /post/, or /api/v3/.
bool is_lemmy_url(std::string_view s);

#if defined(XANATOO_LEMMY) && XANATOO_LEMMY
// Fetch a community listing or single post (+ comments) via Lemmy API v3.
// Builds a Document with Thread + Message nodes (read-only).
std::optional<Document> lemmy_fetch(std::string_view url,
                                    std::string* err = nullptr);
#endif

}  // namespace xanatoo
