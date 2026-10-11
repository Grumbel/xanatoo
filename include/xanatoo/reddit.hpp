// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// True for reddit.com / old.reddit.com listing or post URLs (with or without
// .json). Also accepts bare "reddit.com/…" without a scheme.
bool is_reddit_url(std::string_view s);

// Normalize to an https://www.reddit.com/…/.json request URL.
// Returns empty on failure.
std::string reddit_json_url(std::string_view s);

#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
// Fetch a subreddit listing or post+comments via the public .json API.
// Builds a Document with Thread + Message nodes (read-only).
// On failure returns nullopt and sets *err.
std::optional<Document> reddit_fetch(std::string_view url,
                                     std::string* err = nullptr);
#endif

}  // namespace xanatoo
