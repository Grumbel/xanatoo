// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/math.hpp"
#include "xanatoo/style.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "xanatoo/view_settings.hpp"

namespace xanatoo {

struct PositionedGlyph {
  uint32_t codepoint = 0;
  float x = 0.f;
  float y = 0.f;   // baseline
  float advance = 0.f;  // g.x → next pen position (caret after the glyph)
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  size_t char_index = 0;
  // 1 + index into LayoutResult::auto_links: a URL found in plain text, laid
  // out as a Link although its span is not one. 0 = the span decides.
  uint32_t auto_link = 0;
  InlineKind kind = InlineKind::Text;
  FontRole font = FontRole::Body;
  Rgba color;  // computed from the style sheet (block role + inline kind)
};

struct LayoutBox {
  NodeId node = INVALID_NODE;
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
};

struct PositionedImage {
  NodeId node = INVALID_NODE;
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
  std::string src;
  std::string base;  // directory for relative path resolution
  int thumb = 0;     // > 0: drawn from a thumbnail with this max edge (px)
  NodeId group = INVALID_NODE;  // Collection the card belongs to
  // Inline image in running text: `node` is the text block, `span_index`
  // its InlineKind::Image span. Not resizable; a click follows its link.
  bool in_text = false;
  size_t span_index = 0;
};

// A TeX formula set in running text (InlineKind::Math): `y` is its baseline,
// `px` the size of the em its box is measured in.
struct PositionedMath {
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  float x = 0.f;
  float y = 0.f;
  float px = 0.f;
  bool display = false;
  std::shared_ptr<const MathBox> box;
  Rgba color;
};

struct MessageCard {
  LayoutBox box;
  bool alt = false;  // odd reply depth
};

// Draggable gap between two Column children under a Columns node.
struct ColumnGap {
  NodeId columns = INVALID_NODE;
  NodeId left = INVALID_NODE;
  NodeId right = INVALID_NODE;
  float x = 0.f;       // center of the gutter
  float y = 0.f;
  float height = 0.f;
  float left_w = 0.f;   // pixel widths of the two neighbours (drag scale)
  float right_w = 0.f;
};

// Band of a block filled / underlined with its role's colours (header and
// thread title backgrounds, the rule under a header).
struct RoleBox {
  LayoutBox box;
  BlockRole role = BlockRole::Paragraph;
};

struct LayoutResult {
  // Resolved lexical style scopes, indexed by node rather than inherited
  // through renderer state. Index 0 means use the renderer's base sheet.
  std::vector<DocStyle> scoped_styles;
  std::unordered_map<NodeId, size_t> node_style_index;
  const DocStyle& style_for(NodeId id, const DocStyle& base) const {
    auto it = node_style_index.find(id);
    return it == node_style_index.end() ? base : scoped_styles[it->second - 1];
  }
  std::vector<PositionedGlyph> glyphs;
  std::vector<std::string> auto_links;  // targets of PositionedGlyph::auto_link
  std::vector<LayoutBox> boxes;
  std::vector<PositionedImage> images;
  std::vector<PositionedMath> maths;
  // Vertical bars for block quotes (content coordinates).
  std::vector<LayoutBox> quote_bars;
  // Vertical bars for notes (distinct colour from quotes).
  std::vector<LayoutBox> note_bars;
  // Vertical bars for messages / comments (thread depth uses x indent).
  std::vector<LayoutBox> message_bars;
  // Message card fills; replies at odd depth use the `message-alt` fill.
  std::vector<MessageCard> message_cards;
  // Soft background rectangles for fenced code blocks.
  std::vector<LayoutBox> code_blocks;
  // Table grid lines. height <= 2 → horizontal; width <= 2 → vertical.
  std::vector<LayoutBox> table_lines;
  // Soft fill under header cells (TableCell level > 0).
  std::vector<LayoutBox> table_header_fills;
  // Horizontal rules (x, y = center line, width, height = stroke thickness).
  std::vector<LayoutBox> hr_lines;
  // Filled with the role's `background` (drawn under the text).
  std::vector<RoleBox> role_fills;
  // Filled with the role's `border` (1 px rules under headers).
  std::vector<RoleBox> role_rules;
  // Outlined with the role's `border` (aside frames).
  std::vector<RoleBox> role_frames;
  std::vector<ColumnGap> column_gaps;
  // The page sheet (style `margin-background` / `margin-image`): the text
  // column plus a little padding, in content x; width 0 = no separate
  // margin. `page_gaps` (y, height) are bands across the sheet where the
  // margin shows through: the space around `article` blocks.
  float page_x = 0.f;
  float page_w = 0.f;
  std::vector<LayoutBox> page_gaps;
  float content_height = 0.f;
  // Reader mode: the "… hidden · Show all" notice at the top (click shows the
  // page chrome again); height 0 when nothing is hidden.
  LayoutBox chrome_notice;
  // True when a viewport was active and some sequential blocks were skipped
  // via height hints (glyphs only materialised near the view).
  bool virtualized = false;
  // Document Y band that was fully materialised (top − overscan … bottom + overscan).
  float materialised_top = 0.f;
  float materialised_bottom = 0.f;
  // Unique per LayoutEngine::layout call (0 = empty/default). Lets the pane
  // raster cache tell a fresh layout from the one it last drew.
  uint64_t generation = 0;
};

// View-local presentation of one Collection (not stored in the document;
// the node's `summary` only gives the default layout).
enum class CollectionLayout : uint8_t { Grid, List, Full };
enum class CollectionSort : uint8_t { Document, Name, NameReverse };

struct CollectionView {
  CollectionLayout layout = CollectionLayout::Grid;
  CollectionSort sort = CollectionSort::Document;
  float thumb_px = 160.f;  // card size before px_scale
};

// One card of a Collection: the Image node (direct child or inside a
// Figure), the item node itself and its caption text.
struct CollectionItem {
  NodeId item = INVALID_NODE;   // Image or Figure child of the collection
  NodeId image = INVALID_NODE;  // the Image node (INVALID when none)
  std::string src;
  std::string caption;          // Figure caption, else image alt
};

// Default view from the node's hint ("grid" / "list" / "full").
CollectionView default_collection_view(const Node& collection);
// Items in display order (view sort applied). Non-item children skipped.
std::vector<CollectionItem> collection_items(const Document& doc,
                                             NodeId collection,
                                             CollectionSort sort);

// Top-level Chrome nodes (page chrome of a web page, not nested in another
// Chrome node), in document order. Reader mode hides these.
std::vector<NodeId> chrome_regions(const Document& doc);
// Page chrome the Page panel shows instead of the document: navigation and
// side bars without wide content (tables, code).
bool margin_chrome(const Document& doc, const Node& chrome);
// "3 parts of the page hidden (navigation, sidebar, page footer)"; empty
// for none.
std::string chrome_hidden_text(const Document& doc,
                               const std::vector<NodeId>& regions);

// Optional viewport for large-document virtualization.
// When bottom > top, sequential block children outside
// [top − overscan, bottom + overscan] may be skipped if a height hint exists.
struct LayoutViewport {
  float top = 0.f;
  float bottom = 0.f;
  float overscan = 1200.f;
  bool active() const { return bottom > top; }
};

// Drop cached external documents used by :::include path.md[…].
// Call after reload so changed includes are picked up.
void clear_external_doc_cache();

// Per-layout rendering options (from ViewSettings). Geometry only.
struct LayoutOptions {
  float margin_x = 32.f;     // minimum left/right inset in px
  float margin_pct = 0.f;    // inset as a fraction of the width (each side)
  float margin_y = 24.f;
  float line_spacing = 1.30f;
  int measure_chars = 0;     // column cap in body-font characters; 0 = fill
  bool justify = false;
  // Plain text URLs are laid out as links (ViewSettings::auto_links).
  bool auto_links = false;
  // Display scale for px lengths (style sheet px, margins).
  float px_scale = 1.f;

  static LayoutOptions from_view(const ViewSettings& v, float px_scale = 1.f) {
    LayoutOptions o;
    o.margin_x = ViewSettings::kMinMarginPx * px_scale;
    o.margin_pct = v.margin_pct / 100.f;
    o.margin_y = v.margin_y * px_scale;
    o.line_spacing = v.line_spacing;
    o.measure_chars = v.measure_chars;
    o.justify = v.justify;
    o.auto_links = v.auto_links;
    o.px_scale = px_scale;
    return o;
  }
};

class LayoutEngine {
public:
  LayoutEngine(FontSet& fonts, ImageCache* images = nullptr);

  void set_options(const LayoutOptions& o) { options_ = o; }
  const LayoutOptions& options() const { return options_; }
  // Style sheet for spacing / fonts / glyph colours; must outlive the
  // engine. nullptr = built-in default.
  void set_style(const DocStyle* style);
  // View-local Collection layouts keyed by node (nullptr / missing entry =
  // the node's default). Must outlive layout() calls.
  void set_collection_views(
      const std::unordered_map<NodeId, CollectionView>* views) {
    collection_views_ = views;
  }
  const DocStyle& style() const { return *style_; }
  // Reader mode for the next layout() calls: Chrome nodes are left out and a
  // notice at the top says what was hidden (LayoutResult::chrome_notice).
  void set_hide_chrome(bool hide) { hide_chrome_ = hide; }
  // Leave navigation / side bar chrome out (the Page panel lists it).
  void set_hide_panel_chrome(bool hide) { hide_panel_chrome_ = hide; }

  // height_hints: optional prior block heights (from a previous LayoutResult's
  // boxes). Used only when viewport is active to skip off-screen sequential
  // blocks without re-measuring them.
  // folded_sections: view-local set of Section/Article ids whose body is
  // hidden (header still laid out). Not stored in the document.
  // focus_section: when not INVALID_NODE, only that Section/Article (and its
  // descendants) is fully laid out; ancestors show header only; other
  // branches are omitted. View filter — document unchanged.
  LayoutResult layout(const Document& doc, float max_width,
                      const std::string& image_base = {},
                      const LayoutViewport& viewport = {},
                      const std::unordered_map<NodeId, float>* height_hints = nullptr,
                      const std::unordered_set<NodeId>* folded_sections = nullptr,
                      NodeId focus_section = INVALID_NODE);

private:
  FontSet& fonts_;
  ImageCache* images_;
  LayoutOptions options_{};
  const DocStyle* style_;
  size_t active_style_index_ = 0;
  const std::unordered_map<NodeId, CollectionView>* collection_views_ = nullptr;

  std::string image_base_;
  LayoutViewport viewport_;
  const std::unordered_map<NodeId, float>* height_hints_ = nullptr;
  const std::unordered_set<NodeId>* folded_sections_ = nullptr;
  NodeId focus_section_ = INVALID_NODE;
  bool hide_chrome_ = false;
  bool hide_panel_chrome_ = false;
  // Boxes beside the flow (floating asides) of the current layout() call:
  // text lines next to them get narrower, other blocks move below them.
  struct FloatBox {
    float top = 0.f, bottom = 0.f;  // incl. the gap below
    float left = 0.f, right = 0.f;  // incl. the gap towards the text
    FloatSide side = FloatSide::Right;
  };
  std::vector<FloatBox> floats_;
  // Narrow [x, x + w) to what the floats leave free between top and bottom.
  void float_band(float top, float bottom, float& x, float& w) const;
  // y below every float that reaches into [x, x + w) at y.
  float clear_floats(float x, float w, float y) const;
  void layout_aside(const Document& doc, const Node& node, float x, float& y,
                    float max_width, LayoutResult& out);
  // Page margins of the current layout() call: [x, x + w) left of and
  // right of the text column (w == 0: no room), and how far the boxes
  // already set there reach down (stacking).
  struct MarginArea {
    float x = 0.f, w = 0.f;
    float bottom = 0.f;
  };
  MarginArea margins_[2];  // [0] left, [1] right
  bool in_image_float_ = false;
  bool in_figure_float_ = false;
  bool in_table_float_ = false;
  int in_margin_ = 0;      // > 0 while laying out a margin box
  // Side box text: small faces and the box role's text colour
  // (BlockRole::Count = none) for the blocks inside.
  bool small_ = false;
  BlockRole ctx_role_ = BlockRole::Count;
  // A side box in the margin at flow position y (y itself stays); false
  // when that margin has no room. `label` heads the box ("Navigation").
  bool layout_margin_box(const Document& doc, const Node& node,
                         BlockRole role, FloatSide side,
                         const std::string& label, float y,
                         LayoutResult& out);
  void setup_margins(const Document& doc, float mx, float content_w,
                     float max_width);
  // Display size of an inline image span (CSS px × px_scale, fitted to
  // max_w); remote ones from their thumbnail, a 1 em box while pending.
  void inline_image_size(const InlineSpan& sp, float max_w, float& w,
                         float& h, bool& remote);

  // The set formula of a Math span (null without a math font or on a parse
  // error); cached by source and style.
  std::shared_ptr<const MathBox> math_box(const InlineSpan& sp);
  std::unordered_map<std::string, std::shared_ptr<const MathBox>> math_cache_;

  // Author of the Thread's first Message while laying out that Thread.
  std::string thread_op_;
  bool skipped_any_ = false;

  // If the block has a height hint and lies fully outside the materialised
  // band, advance y by that height, emit a stub box, and return true.
  bool try_skip_block(NodeId id, float x, float& y, float max_width,
                      LayoutResult& out);

  void layout_children_sequential(const Document& doc,
                                  const std::vector<NodeId>& children,
                                  float x, float& y, float max_width,
                                  LayoutResult& out);

  void layout_node(const Document& doc, NodeId id,
                   float x, float& y, float max_width,
                   LayoutResult& out);
  void layout_spanned_table(const Document& doc, NodeId table,
                            const std::vector<NodeId>& rows, float x, float& y,
                            float width, LayoutResult& out);
  void layout_paragraph(const Document& doc, const Node& node,
                        float x, float& y, float max_width,
                        LayoutResult& out, FontRole base_role,
                        BlockRole block = BlockRole::Paragraph);
  void layout_collection(const Document& doc, NodeId id, float x, float& y,
                         float max_width, LayoutResult& out);
  // http(s) Image: drawn from its kRemoteThumbEdge thumbnail; a placeholder
  // while it downloads, nothing once it failed.
  void layout_remote_image(const Document& doc, const Node& node, float x,
                           float& y, float max_width, LayoutResult& out);
  // Reader mode: "Reader mode: N parts of the page hidden · Show all".
  void layout_chrome_notice(const Document& doc, float x, float& y,
                            float max_width, LayoutResult& out);
  // Paragraph in the role's font.
  void layout_role_text(const Document& doc, const Node& node, BlockRole role,
                        float x, float& y, float max_width, LayoutResult& out);
  // Background band / underline of a title laid out from `top` to `bottom`,
  // when the role sets those colours.
  void add_role_band(BlockRole role, NodeId id, float x, float top,
                     float width, float bottom, LayoutResult& out);
};

}  // namespace xanatoo
