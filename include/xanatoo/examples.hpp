// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

// Help → Examples: the pages under examples/ the menu opens. A test checks
// that every page in the directory is listed (or opened another way).

namespace xanatoo {

struct ExampleEntry {
  const char* label;  // nullptr: a separator
  const char* file;   // relative to examples/
};

inline constexpr ExampleEntry kExamples[] = {
    {"Welcome", "welcome.md"},
    {"Keyboard reference", "help.md"},
    {nullptr, nullptr},
    {"Feature tour (every block kind)", "tour.md"},
    {nullptr, nullptr},
    {"Directory (links and feeds)", "directory.md"},
    {"Books and long reads", "books.md"},
    {"Feeds (RSS / Atom)", "feeds.md"},
    {"Nostr discussions", "nostr-sample.md"},
    {nullptr, nullptr},
    {"Gemini gemtext", "gemini-sample.gmi"},
    {"Org-mode", "sample.org"},
    {"Semantic format (S-expression)", "semantic.sexp"},
    {"Sample (S-expression)", "sample.sexp"},
};

// Opened by the "Diff demo" item instead of a menu entry of their own.
inline constexpr const char* kExampleDiffPair[] = {"diff-v1.md", "diff-v2.md"};

}  // namespace xanatoo
