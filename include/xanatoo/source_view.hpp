// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/locator.hpp"
#include "xanatoo/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

struct SourceViewText {
  std::string text;
  std::string language;
};

// A source pipe projects a document into code text. Raw is the input text,
// when available; sexpr/XML serialize the *current* semantic tree. No parser
// or document content is modified. An absent raw input gives a visible notice.
SourceViewText source_view_text(const Document& doc, SelectorKind kind,
                                std::optional<std::string_view> raw);

// A code block (not reparsed as Markdown) keeps markup characters literal.
Document source_view_document(const SourceViewText& source);

}  // namespace xanatoo
