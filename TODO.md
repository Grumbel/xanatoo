# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **017** (this delivery is **016**)

## Status (2026-10-10)

Stage **01** complete. Stage **02** largely working. Early stretchtext.

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

## Open work / next steps

1. Optional: HarfBuzz for complex scripts
2. Replace snapshot undo with a tighter operation log if memory becomes an issue
3. Explicit Save As to a chosen path (untitled auto-names for now)
4. Open external http(s) links (currently logged only)

## Design notes

- Stretch nodes carry `summary` + `expanded`. Layout synthesises a
  one-line marker when collapsed; children are skipped.
- Selection is two Carets (anchor + focus). Highlight is derived from
  the glyph list, not stored on the document.
- Block split creates a new sibling via `Document::insert_after` and
  moves the trailing spans across.

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
- `glyphtoo-016.*-status-bar-…` — this delivery
