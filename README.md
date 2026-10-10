# glyphtoo

Interactive document composition engine.

A lightweight, realtime, editable document system built with C++ and SDL2.
Inspired by Markdown, classical typesetting, Ted Nelson's hypertext ideas,
HyperCard, and the Canon Cat.

See [PROPOSAL.md](PROPOSAL.md) for the full vision.

## Multi-view

- **Ctrl+\\** — toggle a second side-by-side view
- **Tab** — switch keyboard focus between panes
- Drag the centre gutter to resize panes
- `glyphtoo a.md b.md` — open two independent documents side by side
- With one file (or Ctrl+\\ alone), both panes share the same live document

## Navigation

- **Ctrl+← / Ctrl+→** — move by word
- Click blue underlined links to follow `#anchor` targets in-document

## Search

- **Ctrl+F** — leap search: type to jump to the next match
- **F3** / **Ctrl+G** — next match
- **Esc** — leave search mode
- Query is shown in a bottom status bar; matches scroll into view

Untitled buffers: **Ctrl+S** writes `untitled.md` (or `untitled-N.md`) and
binds that path for further saves.

## Images

Markup `![alt](path.png)` loads PNG/JPEG/BMP/GIF relative to the
document directory (via stb_image). Missing files show a placeholder.

## Status

Stage 01 (document model + layout) is working.
Early stage 02 (caret, hit-testing, basic editing) is in place.

- Data-oriented document model with stable node IDs
- Minimal Markdown-ish parser
- FreeType glyph rendering (body + heading sizes) via SDL2
- Click to place caret, type to edit, Backspace/Delete, arrows
- Immediate reflow after every change

## Build (Nix)

```bash
nix build
./result/bin/glyphtoo
```

Or:

```bash
nix develop
mkdir build && cd build
cmake .. && make -j
./glyphtoo
```

## Build (system packages)

Requires: CMake ≥ 3.16, SDL2, FreeType, C++20 compiler.

```bash
mkdir build && cd build
cmake .. && make -j
./glyphtoo
./glyphtoo --font /path/to/SomeFont.ttf
```

### Command-line options

```
glyphtoo [options] [file.md]

-f, --font <path>     TTF/OTF font file (body + headings)
-o, --open <path>     Open a markup document
-h, --help            Show help
```

A positional path opens that file. **Ctrl+S** saves it back (round-trips
through the lightweight markup serializer).

Font resolution order:

1. `--font` / `-f`
2. `GLYPHTOO_FONT` environment variable
3. Common system paths (DejaVu, Liberation, FreeSans, Arial)

`nix build` wraps the binary so `GLYPHTOO_FONT` points at DejaVu Sans
from nixpkgs — no system font required.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
