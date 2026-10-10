# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **003** (this delivery is **002**)

## Status (2026-10-10)

Stage **01** complete + early **stage 02** foundations:

- [x] Project scaffolding (CMake, flake.nix, LICENSE, SPDX)
- [x] Document model with stable NodeIds
- [x] FreeType + SDL2 rendering (body + heading sizes)
- [x] Layout engine (paragraphs, headings, word-wrap, dual fonts)
- [x] Minimal Markdown-ish parser (`parse_markup`) covering the
      proposal sample (headings, paragraphs, *em*, **strong**, links,
      basic `:::stretch` / `:::columns` containers, images)
- [x] Caret as pure document data (node / span / char)
- [x] Hit-testing (click to place caret)
- [x] Basic keyboard editing: insert, backspace, delete, arrows,
      Home, PageUp/Down, scroll
- [x] Visual caret
- [x] Link colouring

The binary now shows a live, editable sample document loaded from
markup. Type, click, delete — layout reflows immediately.

## Open work / next steps

1. **Verify build** on a real machine.
2. Proper block split on Enter (new paragraph) instead of inserting `\n`
   into the current span.
3. Selection (shift-arrows / drag) and clipboard.
4. Better vertical caret movement (true line-up / line-down using layout).
5. Stretchtext: click a summary to expand/collapse and reflow.
6. Multi-view / side-by-side panes (stage 03).
7. Undo/redo as operation log.
8. Package a default font inside the flake so it runs without system fonts.
9. Optional: HarfBuzz for complex scripts later.

## Design notes

- Caret never stores pixels; `caret_screen_pos` derives geometry from
  the current layout result.
- Editing mutates the document; layout is recalculated from scratch
  (fine at this scale; incremental layout can come later if needed).
- Parser is intentionally tiny — not CommonMark. Extend only when a
  feature needs it.
- Document nodes remain the source of truth; presentation is derived.

## Bundle history

- `glyphtoo-001.*-stage01-scaffold-568a91b.bundle` — initial scaffold
- `glyphtoo-002.1-markup-caret-edit-568a91b.bundle` — this delivery
