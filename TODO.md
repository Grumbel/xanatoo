# TODO — glyphtoo

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number after this work: **002** (this delivery is 001)

## Status (2026-10-10)

Implemented stage **01** skeleton:

- [x] Project scaffolding (CMake, flake.nix, LICENSE, REUSE-style SPDX)
- [x] Document model (`Document` / `Node` / `InlineSpan`) with stable `NodeId`
- [x] FreeType font loading + glyph cache → SDL textures
- [x] Basic layout engine (paragraphs, headings, simple word-wrap)
- [x] SDL2 window + paper-like rendering of a hard-coded sample document
- [x] Scroll with mouse wheel / arrow keys
- [x] Window resize triggers re-layout

The binary should open a resizable window showing a short sample text
inspired by the proposal. No editing yet.

## Open work / next steps

1. **Verify build** on a real machine (`nix build` or system CMake).
2. Improve layout:
   - Proper heading size scaling (currently only spacing is affected)
   - Margins / padding as data
   - Nested blocks and simple columns
3. Tiny Markdown-ish parser so the sample can be loaded from a string
   matching the example in PROPOSAL.md.
4. Stage 02 foundations:
   - Caret + selection model (data, not pixels)
   - Hit-testing from screen → (node, span, char)
   - Keyboard insertion / deletion of text
5. Stretchtext prototype (expand / collapse a block in place + reflow).

## Design notes (current)

- Document nodes live in a contiguous vector; IDs are 1-based indices.
  Later we can introduce free-lists / generational IDs if deletion becomes
  frequent.
- Layout produces a flat list of `PositionedGlyph` + `LayoutBox`. The
  document itself never stores screen coordinates as identity.
- Renderer is deliberately dumb: it just draws the layout result.
- Font discovery is a simple hard-coded list of common system paths;
  packaging a default font inside the flake would be nicer later.

## Bundle history

- `glyphtoo-001.5-stage01-scaffold-568a91b.bundle` — this delivery
