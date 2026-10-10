# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **065** (this delivery is **064**)

## Status (2026-10-10)

Stage **01**–**04** core features in place; ImGui without X11 syswm dependency.
LCS structural diff marks; cross-document `:::include`.

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
- [x] Paste plain text splits blank lines into sibling paragraphs
- [x] Insert image (Ctrl+Shift+I path prompt)
- [x] FontConfig: bold/italic/bold-italic/mono faces for styled text
- [x] Shift+F3 / find_prev; soft background under inline code
- [x] Ctrl+R reload from disk (Ctrl+Shift+R force)
- [x] Selection word/char counts in status bar
- [x] Dear ImGui menu bar / toolbar (no X11 syswm on Linux)
- [x] Structural LCS-based dual-pane diff marks (insertions no longer cascade)
- [x] Cross-document transclude: `:::include path.md#Heading` (and whole-file)
- [x] Experimental S-expression serialize/parse (`.sexp` / `.glyph` / `.ss`)
- [x] Sexpr polish: `:id`, merged text spans, compact floats; File→Export; cache flush on reload
- [x] Drag-and-drop image files into a pane (PNG/JPEG/GIF/BMP)
- [x] Refactor: `util.hpp` (ascii_lower, anchors, image path); -Wall; dead-code cleanup
- [x] Extract CLI/bootstrap to `cli.hpp`; multi-delete no longer leaves empty husks
- [x] Leap-search match highlight (amber); `advance_caret`; ascii_lower in find
- [x] Fix mouse/selection offset (`content_y` + hit-test); triple-click; select_all/node/word
- [x] Scrollbar / page / scroll-into-view account for toolbar height
- [x] Classic Gtk2/Clearlooks-ish ImGui theme + SVG→PNG toolbar icons
- [x] Help as ImGui software window (draggable/resizable, not OS window)

## Open work / next steps

1. Optional: HarfBuzz for complex scripts
2. Replace snapshot undo with a tighter operation log if memory becomes an issue
3. Editable transclusion mirror (edits on embed write through to source)
4. Word-level / intra-block diff highlighting

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
- Transclude: `src` holds `#anchor` or `path.md[#anchor]`. Layout snapshots
  the target's spans (glyphs tagged with the Transclude node). Local click
  jumps to the source. Cross-doc paths resolve relative to the host file's
  directory via a process-wide external document cache.
- Diff marks: LCS of plain-text block fingerprints; only non-common nodes
  are marked (insertions/deletions no longer cascade).
- Block drag: grab left 16px of a layout box; drop on a same-parent sibling
  (top/bottom half = before/after). `relocate_block` splices the child list.
- Column widths: `Column.display_scale` is a relative weight (default 1).
  Layout emits `column_gaps`; dragging a gap shifts weight between neighbours.
  Markup: `:::column weight=1.5`.
- Cross-parent `relocate_block`: reparents across Columns/Lists; converts
  ListItem↔Paragraph when crossing a List; drops emptied lists.
- Nested lists: ListItem may own a child List. Tab nests under the previous
  sibling; Shift+Tab promotes. Markup uses 2-space indent per level.
- Shared helpers live in `util.hpp` (ascii_lower, collect_text_nodes,
  find_anchor_node, is_raster_image_path) so layout/editor/main do not
  each reimplement anchor matching or extension checks.

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
- `glyphtoo-045.*-page-nav-…` — page / document navigation
- `glyphtoo-046.*-paste-paras-…` — structured multiline paste
- `glyphtoo-047.*-insert-image-…` — insert image by path
- `glyphtoo-048.*-fontconfig-…` — FontConfig styled faces
- `glyphtoo-049.*-find-prev-code-bg-…` — find prev + code background
- `glyphtoo-050.*-reload-…` — Ctrl+R reload from disk
- `glyphtoo-051.*-rebased-…` — rebased onto origin/master
- `glyphtoo-052.*-sel-stats-…` — selection status stats
- `glyphtoo-053.*-imgui-…` — Dear ImGui menu/toolbar
- `glyphtoo-054.*-imgui-no-x11-…` — ImGui without X11 syswm
- `glyphtoo-055.*-lcs-diff-xdoc-…` — LCS diff + cross-doc include
- `glyphtoo-056.*-sexpr-…` — S-expression format
- `glyphtoo-057.*-sexpr-polish-…` — sexpr polish
- `glyphtoo-058.*-dnd-image-…` — drag-and-drop images
- `glyphtoo-059.*-refactor-cleanup-…` — util refactor
- `glyphtoo-060.*-cli-delete-cleanup-…` — CLI extract + delete husks
- `glyphtoo-061.*-search-highlight-…` — search highlight
- `glyphtoo-062.*-mouse-select-…` — mouse offset + selection
- `glyphtoo-063.*-classic-ui-icons-…` — classic theme + icons
- `glyphtoo-064.*-help-window-…` — this delivery
