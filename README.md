# glyphtoo

Interactive document composition engine.

A lightweight, realtime, editable document system built with C++ and SDL2.
Inspired by Markdown, classical typesetting, Ted Nelson's hypertext ideas,
HyperCard, and the Canon Cat.

See [PROPOSAL.md](PROPOSAL.md) for the full vision.

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

Requires: CMake ≥ 3.16, SDL2, FreeType, C++20 compiler, a system font
(DejaVu or Liberation).

```bash
mkdir build && cd build
cmake .. && make -j
./glyphtoo
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
