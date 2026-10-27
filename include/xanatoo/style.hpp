// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Document style sheets: S-expression files kept apart from the markup
// (styles/*.xss). A sheet styles semantic roles — paragraph, header, note,
// link, … — never markup structure. Every sheet starts from the built-in
// default, so a sheet only states what it changes. See docs/STYLESHEETS.md.
//
// Precedence: reader view prefs (line spacing, margins, measure, zoom,
// --font) > style sheet > built-in default.

#include "xanatoo/font.hpp"
#include "xanatoo/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  bool visible() const { return a > 0; }
  bool operator==(const Rgba&) const = default;
};

// px (logical: × ui scale, like CSS px on high-density screens), or em
// relative to the body font size (follows zoom).
struct Length {
  float value = 0.f;
  bool em = false;
  float px(float em_px, float px_scale = 1.f) const {
    return em ? value * em_px : value * px_scale;
  }
};

enum class BlockRole : uint8_t {
  Paragraph,
  Header,
  Section,
  List,
  ListItem,
  BlockQuote,
  Note,
  CodeBlock,
  Term,
  Definition,
  Figure,
  Caption,
  BiblioEntry,
  Footnote,
  Thread,
  Message,
  Table,
  Rule,
  Transclusion,
  Count
};
constexpr size_t kBlockRoleCount = static_cast<size_t>(BlockRole::Count);
constexpr size_t kInlineKindCount = static_cast<size_t>(InlineKind::FootnoteRef) + 1;

struct BlockStyle {
  Length space_before;
  Length space_after;
  Length indent;          // text inset (after a bar / list marker / hanging)
  FontRole font = FontRole::Body;
  bool has_color = false;
  Rgba color;             // text colour (else page ink)
  Rgba background{0, 0, 0, 0};  // code block fill, table header fill
  Rgba border{0, 0, 0, 0};      // side bars, rules, grid lines, frames
};

struct InlineStyle {
  bool has_color = false;
  Rgba color;
  Rgba background{0, 0, 0, 0};
  bool underline = false;
};

struct DocStyle {
  std::string name = "default";  // file stem; key for view.conf
  std::string title = "Default";
  std::string description;
  Rgba paper{248, 246, 240, 255};
  Rgba ink{28, 28, 32, 255};
  Rgba selection{160, 190, 230, 120};
  FontFamilies fonts;
  float heading_scale = 0.f;  // heading px = body px × scale; 0 = app default
  BlockStyle block[kBlockRoleCount];
  InlineStyle inl[kInlineKindCount];
  // Unknown roles / properties / values met while loading (with sheet name).
  std::vector<std::string> warnings;

  const BlockStyle& operator[](BlockRole r) const {
    return block[static_cast<size_t>(r)];
  }
  const InlineStyle& operator[](InlineKind k) const {
    return inl[static_cast<size_t>(k)];
  }
  // Glyph colour for inline `k` inside a block of role `r`.
  Rgba text_color(BlockRole r, InlineKind k) const {
    const InlineStyle& is = (*this)[k];
    if (is.has_color) return is.color;
    const BlockStyle& bs = (*this)[r];
    return bs.has_color ? bs.color : ink;
  }
};

// The hard-coded look (what xanatoo rendered before style sheets).
DocStyle builtin_style();

// Apply the forms of one sheet source onto `style` (no inherit handling).
// Exposed for tests; load_style() is the normal entry point.
void apply_style_source(DocStyle& style, std::string_view source,
                        const std::string& sheet_name = {});

struct StyleEntry {
  std::string name;   // file stem
  std::string title;  // (name "…") from the sheet, else the stem
  std::string path;
};

// Style directories, lowest priority first (later ones override by name):
// build-tree styles/, <exe>/../share/xanatoo/styles, $XANATOO_DATA_DIR/styles,
// ./styles, ~/.config/xanatoo/styles.
std::vector<std::string> style_dirs();
// "default" always first (built in even without a file), rest by title.
std::vector<StyleEntry> list_styles();
// Built-in default + (inherit …) chain + the named sheet. False (and `out`
// left as the built-in default) if the sheet is missing.
bool load_style(const std::string& name, DocStyle& out,
                std::string* error = nullptr);

}  // namespace xanatoo
