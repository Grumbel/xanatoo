// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"
#include "xanatoo/layout.hpp"
#include <string>
#include <optional>
#include <vector>

namespace xanatoo {

// Caret is pure document data — never stores pixels.
struct Caret {
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
  size_t char_index = 0;  // codepoint index within the span

  bool operator==(const Caret& o) const {
    return node == o.node && span_index == o.span_index &&
           char_index == o.char_index;
  }
  bool operator!=(const Caret& o) const { return !(*this == o); }
};

struct CaretScreenPos {
  float x = 0.f;
  float y = 0.f;      // baseline
  float height = 0.f;
  bool valid = false;
};

// Selection is two carets. When anchor == focus the selection is empty.
struct Selection {
  Caret anchor;
  Caret focus;

  bool empty() const { return anchor == focus; }
};

// Map a screen point to a caret location using the current layout.
std::optional<Caret> hit_test(const LayoutResult& layout,
                              float sx, float sy, float scroll_y);

// Hit-test for stretchtext toggle boxes.
std::optional<NodeId> hit_test_stretch(const LayoutResult& layout,
                                       float sx, float sy, float scroll_y);

// Compute the on-screen position of a caret for drawing.
CaretScreenPos caret_screen_pos(const LayoutResult& layout,
                                const Caret& caret,
                                float scroll_y,
                                float line_height);

// Glyph ranges that form the selection highlight (screen-space, already
// adjusted for scroll).
struct SelRect {
  float x, y, w, h;
};
std::vector<SelRect> selection_rects(const LayoutResult& layout,
                                     const Selection& sel,
                                     float scroll_y,
                                     float line_height);

// If caret sits on a same-document Transclude, move it to the source
// passage so subsequent edits write through. Returns true when redirected.
// External includes are left alone (read-only mirror).
bool redirect_transclude_caret(Document& doc, Caret& caret);

// Editing operations (mutate Document, caller re-layouts afterwards).
void insert_text(Document& doc, Caret& caret, const std::string& utf8);

// Insert styled run at caret (splits the current span as needed).
void insert_styled(Document& doc, Caret& caret, const std::string& utf8,
                   InlineKind kind, const std::string& href = {});

// Apply style to the current selection (single-node selections).
// If the selection is already entirely that style, reverts to plain Text.
void apply_style_to_selection(Document& doc, Selection& sel, InlineKind kind,
                              const std::string& href = {});
void delete_backward(Document& doc, Caret& caret);
void delete_forward(Document& doc, Caret& caret);

// Split the current paragraph/heading at the caret into two sibling blocks.
// Caret moves to the start of the new block. Returns true on success.
bool split_block(Document& doc, Caret& caret);

// Wrap a text block in a collapsed Stretch node. Summary defaults to a
// short prefix of the block text when empty.
bool wrap_block_in_stretch(Document& doc, NodeId block,
                           const std::string& summary = {});

// If `block` is inside a Stretch, unwrap it: promote stretch children to the
// stretch's parent and remove the stretch node. Returns the first child id.
std::optional<NodeId> unwrap_stretch(Document& doc, NodeId block_or_stretch);

// Walk parents to find enclosing Stretch node (or INVALID_NODE).
NodeId enclosing_stretch(const Document& doc, NodeId id);

// Reorder a block among its siblings. Returns false if already at edge.
bool move_block_up(Document& doc, NodeId block);
bool move_block_down(Document& doc, NodeId block);

// Move `block` to just before or after `target`. Same-parent reorder, or
// cross-parent reparent (into/out of columns and lists). ListItem↔Paragraph
// conversion happens when crossing a List boundary. Empty lists are dropped.
bool relocate_block(Document& doc, NodeId block, NodeId target, bool before);

// Duplicate a block as the next sibling; returns the new node id.
std::optional<NodeId> duplicate_block(Document& doc, NodeId block);

// Insert a new sibling after the caret's block. Returns the new node id.
// For HorizontalRule, also inserts an empty Paragraph after the rule and
// moves the caret into that paragraph (so typing can continue).
std::optional<NodeId> insert_sibling_after(Document& doc, Caret& caret,
                                          NodeKind kind);
// Semantic structure helpers (table / deflist skeletons).
std::optional<NodeId> insert_table(Document& doc, Caret& caret);
std::optional<NodeId> insert_deflist(Document& doc, Caret& caret);
// Two-column Columns block with an empty paragraph in each column.
std::optional<NodeId> insert_columns(Document& doc, Caret& caret, int n = 2);
// Insert a bibliography entry (creates a Biblio container if needed).
// `key` becomes the entry label (cite target); body is placeholder text.
std::optional<NodeId> insert_biblio_entry(Document& doc, Caret& caret,
                                         const std::string& key = "key");
// Insert a Transclude (:::include) after the caret's block.
// `src` is `#Heading`, `file.md`, or `file.md#Heading`.
std::optional<NodeId> insert_transclude(Document& doc, Caret& caret,
                                        const std::string& src);

// Insert an Image node after the caret's block. Follows with an empty
// paragraph and moves the caret there so typing can continue.
std::optional<NodeId> insert_image(Document& doc, Caret& caret,
                                   const std::string& path,
                                   const std::string& alt = {});
// Figure containing an Image + Caption; caret lands in the caption.
std::optional<NodeId> insert_figure(Document& doc, Caret& caret,
                                    const std::string& path,
                                    const std::string& alt = {},
                                    const std::string& caption = "Caption");
// Set heading level 1–6, or 0 to demote to paragraph.
bool set_heading_level(Document& doc, Caret& caret, int level);

// Toggle list membership of the caret's text block.
// - Paragraph/Heading/BlockQuote → become a ListItem under a new List
// - ListItem → unwrap to Paragraph (list dropped if emptied)
// `ordered` selects ul vs ol when creating a list.
bool toggle_list(Document& doc, Caret& caret, bool ordered);

// Nest the current ListItem under the previous sibling (Tab).
bool indent_list_item(Document& doc, Caret& caret);
// Promote the current ListItem out of a nested list (Shift+Tab).
bool outdent_list_item(Document& doc, Caret& caret);

// Delete the current selection (or nothing if empty). Caret becomes the start.
void delete_selection(Document& doc, Selection& sel);

// Plain-text extraction of the current selection (single-node for now).
std::string selected_text(const Document& doc, const Selection& sel);

// Concatenate all text of a node (for paste targets / fallbacks).
std::string node_text(const Document& doc, NodeId id);

// Paste plain text at the caret. Blank-line separated chunks become new
// sibling paragraphs; single newlines within a chunk become spaces.
void paste_plain_text(Document& doc, Caret& caret, const std::string& text);

// Plain-text word / character / block counts for status display.
struct DocStats {
  size_t words = 0;
  size_t chars = 0;
  size_t text_blocks = 0;
  size_t headings = 0;
};
DocStats document_stats(const Document& doc);

// Move caret by one codepoint (within the node). Returns false if at boundary.
bool move_left(Document& doc, Caret& caret);
bool move_right(Document& doc, Caret& caret);
bool move_word_left(Document& doc, Caret& caret);
bool move_word_right(Document& doc, Caret& caret);

// Vertical movement using layout geometry (preferred x preserved by caller).
bool move_up(const LayoutResult& layout, Caret& caret, float preferred_x);
bool move_down(const LayoutResult& layout, Caret& caret, float preferred_x);

// Move to previous / next text-bearing node.
bool move_to_prev_node(Document& doc, Caret& caret);
bool move_to_next_node(Document& doc, Caret& caret);

// Document extremes (first / last text node).
bool move_to_document_start(Document& doc, Caret& caret);
bool move_to_document_end(Document& doc, Caret& caret);

void ensure_valid_caret(Document& doc, Caret& caret);

// Document-order comparison of two carets (-1, 0, +1).
int caret_cmp(const Document& doc, const Caret& a, const Caret& b);

// Case-insensitive search starting after `from` (wraps around).
// Returns the start caret of the match and sets match_len to codepoint length.
// Move caret forward by up to n codepoints within the current text node.
// Returns false if the node ended early.
bool advance_caret(Document& doc, Caret& caret, size_t n);

// Select the entire text node (paragraph / heading / list item / quote).
void select_node(Document& doc, Selection& sel, NodeId node);
// Select the whole document (first → last text node).
void select_all(Document& doc, Selection& sel);
// Expand selection to the word under the focus caret.
void select_word_at(Document& doc, Selection& sel);

std::optional<Caret> find_next(const Document& doc, const std::string& query,
                               const Caret& from, size_t& match_len);
// Case-insensitive search starting before `from` (wraps around).
std::optional<Caret> find_prev(const Document& doc, const std::string& query,
                               const Caret& from, size_t& match_len);
// Replace every case-insensitive match of `query` with `replacement`.
// Returns the number of substitutions performed.
size_t replace_all(Document& doc, const std::string& query,
                   const std::string& replacement);

// Internal link: href "#Title" → caret at matching heading/paragraph.
std::optional<Caret> jump_to_href(const Document& doc, const std::string& href);

// Short preview of an internal #anchor target (first ~80 chars of the node).
// Empty optional for external or unresolved hrefs.
std::optional<std::string> link_preview_text(const Document& doc,
                                            const std::string& href);

// Link/cite under pointer.
struct LinkHit {
  std::string href;
  std::string rel;   // typed relation (glossary, source, …); empty if none
  InlineKind kind = InlineKind::Link;
  NodeId node = INVALID_NODE;
  size_t span_index = 0;
};
std::optional<LinkHit> hit_test_link_ex(const Document& doc,
                                        const LayoutResult& layout,
                                        float mx, float my, float scroll_y);
// Convenience: href only (Cite keys returned as "#key").
std::optional<std::string> hit_test_link(const Document& doc,
                                         const LayoutResult& layout,
                                         float mx, float my, float scroll_y);

// Structural block diff for side-by-side version viewing.
// Aligns text-bearing nodes via LCS of plain-text fingerprints, then
// marks nodes that are not part of the common subsequence (insertions,
// deletions, or content changes). Survives mid-document inserts without
// cascading false positives from a pure index-aligned scan.
//
// When optional word_a / word_b are provided, pairs of "modified" blocks
// (similar but not identical text) also get intra-block word-range marks
// so the view can highlight only the changed runs.
struct DiffWordMark {
  NodeId node = INVALID_NODE;
  size_t char_begin = 0;  // inclusive, UTF-8 character index in node plain text
  size_t char_end = 0;    // exclusive
};
void compute_diff_marks(const Document& a, const Document& b,
                        std::vector<NodeId>& changed_a,
                        std::vector<NodeId>& changed_b,
                        std::vector<DiffWordMark>* word_a = nullptr,
                        std::vector<DiffWordMark>* word_b = nullptr);

// Tab / Shift+Tab inside a table: move to next / previous cell (wraps by row).
bool table_move_cell(Document& doc, Caret& caret, bool forward);

// Heading outline for navigation (document order).
struct OutlineEntry {
  NodeId node = INVALID_NODE;
  int level = 1;
  std::string text;
};
void collect_outline(const Document& doc, std::vector<OutlineEntry>& out);

// ---------------------------------------------------------------------------
// Undo / redo via document snapshots (simple, correct at current scale).
// ---------------------------------------------------------------------------
struct EditorSnapshot {
  Document doc;
  Selection sel;
};

class History {
public:
  explicit History(size_t max_depth = 128);

  // Call *before* a mutating edit. Clears the redo side.
  void push(const Document& doc, const Selection& sel);

  bool can_undo() const { return undo_.size() > 1; }  // keep baseline
  bool can_redo() const { return !redo_.empty(); }

  // Restores previous state into doc/sel. Returns false if nothing to undo.
  bool undo(Document& doc, Selection& sel);
  bool redo(Document& doc, Selection& sel);

  // Seed the stack with the initial document (call once after load).
  void reset(const Document& doc, const Selection& sel);

private:
  size_t max_depth_;
  std::vector<EditorSnapshot> undo_;
  std::vector<EditorSnapshot> redo_;
};

}  // namespace xanatoo
