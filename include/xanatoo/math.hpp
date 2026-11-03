// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// TeX math notation (the subset Wikipedia's <math> and Markdown `$…$` use):
// `parse_math` turns TeX source into a tree, `layout_math` sets it into a
// box of glyphs and rules following TeXbook Appendix G, with the metrics of
// an OpenType MATH font behind the abstract `MathFace` (math_font.hpp has the
// FreeType one; the unit tests use a fake). Everything here is pure data.
// Coordinates are in em of the base font size, y grows upwards.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// ---------------------------------------------------------------------------
// Syntax tree
// ---------------------------------------------------------------------------

// TeX atom classes (spacing between neighbours depends on them).
enum class MathClass : uint8_t { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

enum class MathKind : uint8_t {
  Atom,     // one symbol `cp` (or an operator name in `text`)
  Text,     // upright running text (\text)
  List,     // kids set side by side
  Frac,     // kids[0] over kids[1]; `bar` = false for \binom / \atop
  Radical,  // kids[0] radicand, kids[1] index when `has_b`
  Script,   // kids[0] nucleus, kids[1] subscript (`has_a`), kids[2] superscript (`has_b`)
  Fenced,   // kids[0] body between `cp` and `cp2` (0 = none), auto-sized
  Accent,   // kids[0] under / over the accent `cp` (`above`, `stretchy`)
  Rule,     // kids[0] with an over / under bar (\overline, \underline)
  OverUnder,  // kids[0] with kids[1] over (`above`) or under it (\overset)
  Matrix,   // kids = cells row-major, `rows` × `cols`, `align` per column
  Space,    // horizontal space of `amount` em
  Style,    // kids[0] set in the style `style`
  Phantom,  // kids[0]'s size without ink
};

// Which alphabet an atom's letters come from.
enum class MathVariant : uint8_t {
  Auto,  // italic Latin / lower-case Greek, the rest upright
  Roman,
  Bold,
  BoldItalic,
  Italic,
  DoubleStruck,
  Script,
  Fraktur,
  SansSerif,
  Monospace,
};

struct MathNode {
  MathKind kind = MathKind::List;
  MathClass cls = MathClass::Ord;
  MathVariant variant = MathVariant::Auto;
  uint32_t cp = 0;   // Atom: the character; Fenced / Accent: delimiter
  uint32_t cp2 = 0;  // Fenced: right delimiter
  std::string text;  // Text, or the name of an operator (\sin)
  float amount = 0.f;     // Space: em
  int8_t limits = 0;      // Op: 0 default, 1 \limits, 2 \nolimits
  bool dlimits = false;   // Op: scripts above / below in display style
  int8_t style = -1;      // Style / Frac: forced style (0 D, 2 T, 4 S, 6 SS)
  int8_t size = 0;        // \big … \Bigg: 1…4 fixed delimiter size
  bool bar = true;        // Frac: draw the rule
  bool above = true;      // Accent / Rule / OverUnder
  bool stretchy = false;  // Accent grows with its base
  bool has_a = false;     // Script: has a subscript
  bool has_b = false;     // Script: has a superscript; Radical: has an index
  int rows = 0, cols = 0;
  std::string align;      // Matrix: one of c / l / r per column
  std::vector<MathNode> kids;
};

struct MathParse {
  MathNode root;          // a List
  std::string error;      // first problem ("" = none); unknown commands are
                          // kept as upright text so the page stays readable
};

// Parses TeX math source (without the `$` delimiters).
MathParse parse_math(std::string_view tex);

// A letter in an alphabet (Unicode Mathematical Alphanumeric Symbols with
// their holes filled from Letterlike Symbols); `cp` itself when it has none.
uint32_t math_alphabet(uint32_t cp, MathVariant v);

// ---------------------------------------------------------------------------
// Font metrics
// ---------------------------------------------------------------------------

// Metrics of one glyph, em units (1.0 = the font size).
struct MathGlyph {
  float advance = 0.f;
  float ascent = 0.f;   // ink above the baseline
  float descent = 0.f;  // ink below the baseline
  float lsb = 0.f;      // ink starts here
  float ink_w = 0.f;    // width of the ink
  float italic = 0.f;   // italic correction
  float accent = -1.f;  // top accent attachment x (-1 = none, use the middle)
};

// The MATH table constants that layout uses (em; the font's own defaults of
// a typical text face when the font has no table).
struct MathConstants {
  float script_scale = 0.7f;
  float script_script_scale = 0.5f;
  float display_op_min_height = 1.3f;
  float axis_height = 0.25f;
  float accent_base_height = 0.45f;
  float subscript_shift_down = 0.15f;
  float subscript_top_max = 0.4f;
  float subscript_baseline_drop_min = 0.05f;
  float superscript_shift_up = 0.4f;
  float superscript_shift_up_cramped = 0.3f;
  float superscript_bottom_min = 0.12f;
  float superscript_baseline_drop_max = 0.2f;
  float sub_superscript_gap_min = 0.15f;
  float superscript_bottom_max_with_subscript = 0.4f;
  float space_after_script = 0.05f;
  float upper_limit_gap_min = 0.12f;
  float upper_limit_baseline_rise_min = 0.2f;
  float lower_limit_gap_min = 0.15f;
  float lower_limit_baseline_drop_min = 0.6f;
  float stack_top_shift_up = 0.4f;
  float stack_top_display_shift_up = 0.6f;
  float stack_bottom_shift_down = 0.35f;
  float stack_bottom_display_shift_down = 0.7f;
  float stack_gap_min = 0.12f;
  float stack_display_gap_min = 0.35f;
  float fraction_numerator_shift_up = 0.4f;
  float fraction_numerator_display_shift_up = 0.68f;
  float fraction_denominator_shift_down = 0.35f;
  float fraction_denominator_display_shift_down = 0.7f;
  float fraction_numerator_gap_min = 0.05f;
  float fraction_numerator_display_gap_min = 0.12f;
  float fraction_rule_thickness = 0.05f;
  float fraction_denominator_gap_min = 0.05f;
  float fraction_denominator_display_gap_min = 0.12f;
  float overbar_vertical_gap = 0.15f;
  float overbar_rule_thickness = 0.05f;
  float overbar_extra_ascender = 0.05f;
  float underbar_vertical_gap = 0.15f;
  float underbar_rule_thickness = 0.05f;
  float underbar_extra_descender = 0.05f;
  float radical_vertical_gap = 0.06f;
  float radical_display_vertical_gap = 0.12f;
  float radical_rule_thickness = 0.05f;
  float radical_extra_ascender = 0.05f;
  float radical_kern_before_degree = 0.28f;
  float radical_kern_after_degree = -0.55f;
  float radical_degree_bottom_raise = 0.6f;  // fraction of the radical height
  float min_connector_overlap = 0.02f;
};

// One size of a stretchable glyph.
struct MathVariantGlyph {
  uint32_t gid = 0;
  float size = 0.f;  // extent along the stretch direction (em)
};

// One piece of a glyph assembled from parts, first part at the start.
struct MathPart {
  uint32_t gid = 0;
  float start_overlap = 0.f;
  float end_overlap = 0.f;
  float advance = 0.f;
  bool extender = false;
};

class MathFace {
public:
  virtual ~MathFace() = default;
  // Glyph for a character, 0 when the font has none.
  virtual uint32_t glyph(uint32_t cp) const = 0;
  virtual MathGlyph metrics(uint32_t gid) const = 0;
  virtual const MathConstants& constants() const = 0;
  // Sizes of a stretchable glyph, smallest first (the glyph itself too).
  virtual std::vector<MathVariantGlyph> variants(uint32_t gid,
                                                 bool vertical) const = 0;
  // Parts to build the glyph from when no variant is large enough.
  virtual bool assembly(uint32_t gid, bool vertical,
                        std::vector<MathPart>& parts) const = 0;
};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct MathItem {
  bool rule = false;   // a filled rectangle, else a glyph
  uint32_t gid = 0;    // glyph id of the MathFace
  float x = 0.f;       // glyph: origin; rule: left edge
  float y = 0.f;       // glyph: baseline; rule: bottom edge (y up)
  float size = 1.f;    // glyph: multiple of the base size
  float w = 0.f;       // rule only
  float h = 0.f;
};

struct MathBox {
  float width = 0.f;
  float ascent = 0.f;
  float depth = 0.f;
  std::vector<MathItem> items;
  bool ok = true;      // false: parse error, the source is shown instead
};

// Sets `root` in the display (own line) or text (inline) style.
MathBox layout_math(const MathNode& root, const MathFace& face, bool display);

}  // namespace xanatoo
