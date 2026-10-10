# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **009** (this delivery is **008**)

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

## Open work / next steps

1. Multi-view / side-by-side *document panes* (stage 03) — columns are
   in-document layout; separate view panes are still open
2. Optional: HarfBuzz for complex scripts
3. Replace snapshot undo with a tighter operation log if memory becomes an issue
4. Save-as / untitled buffer when no path was given
5. Image drag-resize / interactive placement

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
- `glyphtoo-008.*-columns-multisel-…` — this delivery
