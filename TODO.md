# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **006** (this delivery is **005.2**)

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

## Open work / next steps

1. Multi-node selection delete / copy (currently single-node only)
2. Proper image loading and display
3. Multi-view / side-by-side panes (stage 03)
4. Package a default font in the flake
5. Optional: HarfBuzz for complex scripts
6. Replace snapshot undo with a tighter operation log if memory becomes an issue

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
