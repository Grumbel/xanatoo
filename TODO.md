# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **046** (this delivery is **045**)

## Status (2026-10-10)

Stage **01**–**04** core features in place; improved page / doc navigation.

- [x] Document model, layout, FreeType + SDL2
- [x] Minimal markup parser
- [x] Caret, hit-testing, insert / delete
- [x] **Enter splits paragraph** into two sibling blocks
- [x] **Selection** (shift-arrows, click-drag) with highlight
- [x] **Vertical caret movement** using layout baselines
- [x] **Stretchtext** — `:::stretch summary="…"` starts collapsed;
      click ▸ / ▾ marker to expand/collapse and reflow
- [x] Ctrl+A selects current block
- [x] `--font` / `-f` CLI option to choose the TTF
- [x] Clipboard (Ctrl+C / Ctrl+V / Ctrl+X)
- [x] Undo / redo via document snapshots (Ctrl+Z / Ctrl+Y)
- [x] Caret blink
- [x] Build fix: qualify NodeId in main.cpp
- [x] Open document from CLI (`file.md` / `--open`)
- [x] Save with Ctrl+S via `serialize_markup`
- [x] Default font via `GLYPHTOO_FONT` + flake wrapper (DejaVu)
- [x] Image loading (stb_image: PNG/JPEG/BMP/GIF) + layout/render
- [x] Dirty flag / asterisk in window title
- [x] Side-by-side `:::columns` / `:::column` layout
- [x] Multi-node selection copy and delete
- [x] Dual-pane multi-view (Ctrl+\\ toggle, Tab focus)
- [x] Two-file CLI opens independent docs in dual-pane
- [x] Draggable pane divider (split_ratio)
- [x] Per-document undo stacks and dirty flags
- [x] Leap search (Ctrl+F / F3) — Canon Cat–inspired
- [x] On-screen leap query status bar
- [x] Scroll-to-match after search
- [x] Untitled Ctrl+S → auto `untitled.md` (or untitled-N.md)
- [x] Build fix: serialize_markup inside namespace glyphtoo
- [x] Link underlines + click-to-follow `#anchor`
- [x] Ctrl+←/→ word movement
- [x] Scroll caret into view on arrow navigation
- [x] Image drag-resize (display_scale + corner handle)
- [x] Home/End line movement
- [x] Persist image `{scale=…}` in markup round-trip
- [x] Double-click selects word
- [x] Bottom status bar with common keyboard shortcuts
- [x] Ctrl+B / Ctrl+I / Ctrl+K bold, italic, link on selection
- [x] Ctrl+Shift+S Save As (path entry in status bar)
- [x] Ctrl+K link href edit mode in status bar
- [x] Mouse wheel scrolls pane under cursor
- [x] Ctrl+Shift+A select whole document
- [x] Rename third_party/ → external/
- [x] Ctrl+E wrap block in stretchtext
- [x] Multi-node bold/italic/link styling
- [x] Esc clears selection (no longer quits); Ctrl+Q quits with dirty warn
- [x] Backspace at block start merges with previous paragraph
- [x] Ctrl++ / Ctrl+- font zoom (clears glyph cache)
- [x] Scrollbar thumb per pane
- [x] Ctrl+` code span style
- [x] Ctrl+H leap-replace (Tab switches find/replace)
- [x] Draggable scrollbar
- [x] README keybinding reference
- [x] Ctrl+0–6 paragraph / heading levels
- [x] Ctrl+Shift+[ / ] collapse / expand all stretchtext
- [x] Ctrl+Shift+E edit stretch summary
- [x] Ctrl+Shift+U unwrap stretchtext
- [x] Alt+↑/↓ reorder blocks
- [x] Ctrl+D duplicate block
- [x] Open external http(s)/mailto links via xdg-open (Linux) / open (macOS)
- [x] Hovered link href shown in status bar
- [x] Unordered lists (`-`/`*`/`+`) and ordered lists (`1.`) — parse, layout bullets, serialize
- [x] Enter in list item splits to sibling ListItem; empty item exits list; ordered renumber
- [x] Hover preview tooltip for internal `#anchor` links (target passage snippet)
- [x] Block quotes (`> …`) — parse, layout bar, serialize; Ctrl+Shift+Q toggle
- [x] Horizontal rules (`---`/`***`/`___`) — parse, layout line, serialize
- [x] Ctrl+Shift+8 / Ctrl+Shift+7 toggle unordered / ordered list on block
- [x] Inline transclusion: `:::include #Target` live embed + click-to-source
- [x] Drag-and-drop block reorder (left-margin grab, sibling relocate)
- [x] Adjustable column widths by dragging gutters (`Column.display_scale` weights)
- [x] Cross-parent block drag (into/out of columns & lists; ListItem↔Paragraph)
- [x] Nested lists: Tab/Shift+Tab indent/outdent; indented markup parse/serialize
- [x] F1 keyboard reference overlay (Esc/F1 to close)
- [x] Sample document moved to `examples/welcome.md` + PNG images
- [x] Dual-pane visual links: sync jump, Ctrl+Click other pane, connector line
- [x] Side-by-side diff marks (Ctrl+Shift+D, two independent files)
- [x] Heading outline (Ctrl+Shift+O) — jump via Enter / 1-9 / click
- [x] Ctrl+Enter new paragraph after block; Ctrl+Shift+- insert HR
- [x] Status-bar document stats (words, chars, headings)
- [x] Page Up/Down move caret; Ctrl+Home/End document extremes

## Open work / next steps

1. Optional: HarfBuzz for complex scripts
2. Replace snapshot undo with a tighter operation log if memory becomes an issue
3. Richer transclusion (editable mirror, cross-document includes)
4. Finer-grained diff (word-level / structural tree diff)

## Design notes

- Stretch nodes carry `summary` + `expanded`. Layout synthesises a
  one-line marker when collapsed; children are skipped.
- Selection is two Carets (anchor + focus). Highlight is derived from
  the glyph list, not stored on the document.
- Block split creates a new sibling via `Document::insert_after` and
  moves the trailing spans across.
- List: `level` 0 = unordered, 1 = ordered. ListItem `level` holds the
  ordered display number (0 for bullets). Layout draws "•" or "N." as
  synthetic glyphs on the ListItem node.
- Enter on ListItem: same-kind sibling + ordered renumber. Enter on an
  empty ListItem exits the list (paragraph after the list; empty list
  removed). Backspace-merge drops empty List parents.
- BlockQuote: text node with left bar (`LayoutResult::quote_bars`).
  Ctrl+Shift+Q toggles; Ctrl+0 demotes to paragraph.
- HorizontalRule: non-text node; `hr_lines` in layout. Toggle list wraps
  a text block in a List or unwraps a ListItem back to Paragraph.
- Transclude: `src` holds `#anchor`. Layout snapshots the target's spans
  (glyphs tagged with the Transclude node). Click jumps to the source.
- Block drag: grab left 16px of a layout box; drop on a same-parent sibling
  (top/bottom half = before/after). `relocate_block` splices the child list.
- Column widths: `Column.display_scale` is a relative weight (default 1).
  Layout emits `column_gaps`; dragging a gap shifts weight between neighbours.
  Markup: `:::column weight=1.5`.
- Cross-parent `relocate_block`: reparents across Columns/Lists; converts
  ListItem↔Paragraph when crossing a List; drops emptied lists.
- Nested lists: ListItem may own a child List. Tab nests under the previous
  sibling; Shift+Tab promotes. Markup uses 2-space indent per level.

## Bundle history

- `glyphtoo-001.*` — stage 01 scaffold
- `glyphtoo-002.*` — markup, caret, basic edit
- `glyphtoo-003.*-selection-stretch-…` — selection, stretchtext
- `glyphtoo-004.*-font-cli-…` — `--font` CLI
- `glyphtoo-005.*-clipboard-undo-…` — clipboard, undo, blink + NodeId fix
- `glyphtoo-006.*-open-save-font-…` — open/save + default font
- `glyphtoo-007.*-images-dirty-…` — images + dirty flag
- `glyphtoo-008.*-columns-multisel-…` — columns + multi-sel
- `glyphtoo-009.*-dual-pane-…` — dual-pane multi-view
- `glyphtoo-010.*-two-docs-divider-…` — two docs + divider
- `glyphtoo-011.*-undo-leap-…` — per-doc undo + leap search
- `glyphtoo-012.*-search-ui-save-…` — search UI + untitled save + ns fix
- `glyphtoo-013.*-links-words-…` — links + word move + scroll fix
- `glyphtoo-014.*-image-resize-…` — image resize + Home/End
- `glyphtoo-015.*-scale-persist-…` — scale persist + dblclick
- `glyphtoo-016.*-status-bar-…` — shortcuts status bar
- `glyphtoo-017.*-style-saveas-…` — styles + save as
- `glyphtoo-018.*-link-edit-…` — link href edit + wheel + select-all
- `glyphtoo-019.*-external-stretch-…` — external/ + Ctrl+E stretch
- `glyphtoo-020.*-multistyle-quit-…` — multi-node style + quit safety
- `glyphtoo-021.*-merge-zoom-…` — merge + zoom
- `glyphtoo-022.*-scroll-replace-…` — scrollbar + replace + code style
- `glyphtoo-023.*-scroll-drag-docs-…` — scroll drag + README
- `glyphtoo-024.*-heading-stretch-…` — heading levels + expand-all
- `glyphtoo-025.*-stretch-edit-…` — stretch summary + unwrap
- `glyphtoo-026.*-block-move-…` — block reorder + duplicate
- `glyphtoo-027.*-external-links-…` — external links + hover
- `glyphtoo-028.*-lists-…` — list parse/layout/serialize
- `glyphtoo-029.*-list-edit-…` — list Enter/exit/renumber
- `glyphtoo-030.*-link-preview-…` — anchor hover preview
- `glyphtoo-031.*-blockquote-…` — block quotes
- `glyphtoo-032.*-hr-list-toggle-…` — HR + list toggle
- `glyphtoo-033.*-transclude-…` — :::include transclusion
- `glyphtoo-034.*-block-drag-…` — block drag reorder
- `glyphtoo-035.*-column-width-…` — column width drag
- `glyphtoo-036.*-cross-parent-drag-…` — cross-parent block drag
- `glyphtoo-037.*-nested-lists-…` — nested lists
- `glyphtoo-038.*-f1-help-…` — F1 help overlay
- `glyphtoo-039.*-example-doc-…` — examples/welcome.md + images
- `glyphtoo-040.*-pane-links-…` — dual-pane visual links
- `glyphtoo-041.*-diff-marks-…` — dual-pane diff marks
- `glyphtoo-042.*-outline-…` — heading outline
- `glyphtoo-043.*-insert-blocks-…` — Ctrl+Enter / HR insert
- `glyphtoo-044.*-doc-stats-…` — status-bar document stats
- `glyphtoo-045.*-page-nav-…` — this delivery
