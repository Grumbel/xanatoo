# glyphtoo

Interactive document composition engine.

A lightweight, realtime, editable document system built with C++ and SDL2.
Inspired by Markdown, classical typesetting, Ted Nelson's hypertext ideas,
HyperCard, and the Canon Cat.

See [PROPOSAL.md](PROPOSAL.md) for the full vision.

## Status

Stage 01 scaffolding:

- Data-oriented document model with stable node IDs
- Simple paragraph / heading layout with line breaking
- FreeType glyph rendering via SDL2
- Nix flake for reproducible builds

Editing, caret, stretchtext, multiple views and interactive layout
are planned for later stages.

## Build (Nix)

```bash
nix build
./result/bin/glyphtoo
```

Or enter a development shell:

```bash
nix develop
mkdir build && cd build
cmake ..
make -j
./glyphtoo
```

## Build (system packages)

Requires: CMake ≥ 3.16, SDL2, FreeType, a C++20 compiler.

```bash
mkdir build && cd build
cmake ..
make -j
./glyphtoo
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
