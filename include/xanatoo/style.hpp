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
#include "xanatoo/sexpr.hpp"
#include "xanatoo/types.hpp"

#include <cstdint>
#include <map>
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
  MessageAlt,     // card fill of replies at odd depth (zebra nesting)
  MessageAuthor,  // author name in a message header
  MessageOp,      // author name when it is the thread starter's
  MessageMeta,    // date / score / reply-to in a message header
  Table,
  Rule,
  Transclusion,
  Aside,          // text box beside the flow (in the page margin)
  Article,        // gap (space-before / -after) between articles on one page
  Count
};
constexpr size_t kBlockRoleCount = static_cast<size_t>(BlockRole::Count);
constexpr size_t kInlineKindCount = static_cast<size_t>(InlineKind::Math) + 1;

// Which side of the page a box goes to.
enum class FloatSide : uint8_t { None, Left, Right };
// Where a side box goes: into the page margin (beside the text column, the
// column untouched), floating inside the column with text running past
// (HTML compatibility), or in the flow at full width. Margin and float fall
// back to the flow when there is no room.
enum class Placement : uint8_t { Flow, Float, Margin };

struct BlockStyle {
  Length space_before;
  Length space_after;
  Length indent;          // text inset (after a bar / list marker / hanging)
  FontRole font = FontRole::Body;
  bool has_color = false;
  Rgba color;             // text colour (else page ink)
  // Code block, table header, message card fill; header / thread title band.
  Rgba background{0, 0, 0, 0};
  // Side bars, rules, grid lines, frames; the rule under a header.
  Rgba border{0, 0, 0, 0};
  // Side boxes (aside, sidebar): preferred width, side and placement.
  Length width;
  FloatSide side = FloatSide::None;
  Placement placement = Placement::Flow;
};

struct InlineStyle {
  bool has_color = false;
  Rgba color;
  Rgba background{0, 0, 0, 0};
  bool underline = false;
  bool has_font = false;
  FontRole font = FontRole::Body;
};

// A named, explicitly applied group of role overrides. The parsed forms
// and late-bound tokens are reused when composing nested scopes, so only
// properties mentioned by the variant change in the child scope.
struct StyleVariant {
  std::vector<SExpr> forms;
  std::map<std::string, SExpr> defines;
};

struct DocStyle {
  std::string name = "default";  // file stem; key for view.conf
  std::string title = "Default";
  std::string description;
  Rgba paper{248, 246, 240, 255};  // the page: the text column's sheet
  // What lies around the page (left / right of the column) and in the gaps
  // between articles. Invisible = no separate margin: the paper fills the
  // whole view. An image is tiled over the colour.
  Rgba margin{0, 0, 0, 0};
  std::string margin_image;  // file name (style dirs) or absolute path
  std::string paper_image;   // tiled over the paper
  bool has_margin() const { return margin.visible() || !margin_image.empty(); }
  Rgba ink{28, 28, 32, 255};
  Rgba selection{160, 190, 230, 120};
  Rgba mark{230, 160, 30, 70};  // passage named by the address (`#…`)
  Rgba bookmark{90, 180, 110, 60};  // bookmarks inside the page
  FontFamilies fonts;
  float heading_scale = 0.f;  // heading px = body px × scale; 0 = app default
  float body_scale = 1.f;     // body px = zoom px × scale (small-print sites)
  BlockStyle block[kBlockRoleCount];
  InlineStyle inl[kInlineKindCount];
  InlineStyle metadata;  // semantic inline role, can also apply to links
  std::map<std::string, StyleVariant> variants;
  // Unknown roles / properties / values met while loading (with sheet name).
  std::vector<std::string> warnings;

  const BlockStyle& operator[](BlockRole r) const {
    return block[static_cast<size_t>(r)];
  }
  const InlineStyle& operator[](InlineKind k) const {
    return inl[static_cast<size_t>(k)];
  }
  // Glyph colour for inline `k` inside a block of role `r`.
  Rgba text_color(BlockRole r, InlineKind k,
                  InlineRole role = InlineRole::Normal) const {
    if (role == InlineRole::Metadata && metadata.has_color)
      return metadata.color;
    const InlineStyle& is = (*this)[k];
    if (is.has_color) return is.color;
    const BlockStyle& bs = (*this)[r];
    return bs.has_color ? bs.color : ink;
  }
};

// A file named by a sheet (pattern image): absolute paths stay, relative ones
// are looked up in the style directories (later ones win).
std::string style_asset_path(const std::string& name);

// The hard-coded look (what xanatoo rendered before style sheets).
DocStyle builtin_style();

// Apply the forms of one sheet source onto `style` (no inherit handling).
// Exposed for tests; load_style() is the normal entry point.
void apply_style_source(DocStyle& style, std::string_view source,
                        const std::string& sheet_name = {});

// Apply one named role-only variant over an already resolved style. Unspecified
// roles and properties remain unchanged. Returns false for an unknown name.
bool apply_style_variant(DocStyle& style, std::string_view name);

struct StyleEntry {
  std::string name;   // file stem
  std::string title;  // (name "…") from the sheet, else the stem
  std::string path;
};

// Style directories, lowest priority first (later ones override by name):
// <data dir>/styles, then the user's <user data dir>/styles (paths.hpp).
std::vector<std::string> style_dirs();
// "default" always first (built in even without a file), rest by title.
std::vector<StyleEntry> list_styles();
// Built-in default + (inherit …) chain + the named sheet. False (and `out`
// left as the built-in default) if the sheet is missing.
bool load_style(const std::string& name, DocStyle& out,
                std::string* error = nullptr);

// User-owned sheets live in the high-priority XDG user-data style directory.
// A shipped sheet must be duplicated before editing. Names are safe file
// stems, never paths. Source is checked before an atomic replacement.
bool valid_style_name(std::string_view name);
bool save_user_style(std::string_view name, std::string_view source,
                     std::string* error = nullptr);
bool create_user_style(std::string_view name, std::string_view parent,
                       std::string* error = nullptr);
bool delete_user_style(std::string_view name, std::string* error = nullptr);

// Reader-defined per-domain / per-service overrides. Keys are of the form
// domain:example.org or service:lemmy; matching domains take precedence over
// services. The unpinned default remains ViewSettings::style.
using StylePins = std::map<std::string, std::string>;
bool valid_style_pin_key(std::string_view key);
std::vector<std::string> style_pin_keys(std::string_view url);
std::string style_for_url(const StylePins& pins, std::string_view url,
                          std::string_view fallback);
StylePins load_style_pins();
bool save_style_pins(const StylePins& pins);

}  // namespace xanatoo
