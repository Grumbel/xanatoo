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
//   :::stretch summary="..." ... :::
//   :::columns / :::column  (containers)
//   :::include #Anchor | path.md#Anchor
//   ![alt](src)
//
// Not a full CommonMark implementation — deliberately small.
Document parse_markup(std::string_view source);

// Serialize a document back to the same lightweight markup dialect.
// Round-trips the structures we parse; unknown/richer structure is
// flattened conservatively.
std::string serialize_markup(const Document& doc);

// --- Experimental S-expression format ---------------------------------
//
// Unambiguous nested form of the same document model, e.g.:
//
//   (doc
//     (h 1 "Title")
//     (p "plain " (em "emph") " " (strong "bold") " "
//        (code "x") " " (a "#anchor" "link"))
//     (stretch :summary "What?" :expanded #f
//       (p "body"))
//     (columns
//       (column :weight 1.5 (p "left"))
//       (column (p "right")))
//     (img "pic.png" :alt "Alt" :scale 1.2)
//     (include "#Heading")
//     (include "other.md#Section")
//     (ul (li "bullet") (li "parent" (ul (li "nested"))))
//     (ol (li 1 "one") (li 2 "two"))
//     (quote "quoted")
//     (hr))
//
// Keywords: :summary :expanded :weight :scale :alt
// Booleans: #t #f   Strings: "…" with \\ \" \n \t
// Line comments: ; …
//
// File extensions that prefer sexpr: .sexp .glyph .ss
// Content that starts with '(' (after whitespace/comments) also parses
// as sexpr when loaded.

bool path_uses_sexpr(std::string_view path);
bool looks_like_sexpr(std::string_view source);

Document parse_sexpr(std::string_view source);
std::string serialize_sexpr(const Document& doc);

// Dispatch: sexpr if path extension says so, else if source looks like
// sexpr, else Markdown-ish markup.
Document parse_document(std::string_view source, std::string_view path = {});
std::string serialize_document(const Document& doc, std::string_view path = {});

}  // namespace glyphtoo
