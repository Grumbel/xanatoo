# glyphtoo

Interactive document composition engine.

A lightweight, realtime, editable document system built with **C++20**, **SDL2**,
and **FreeType**. Inspired by Markdown, classical typesetting, Ted Nelson's
hypertext ideas, HyperCard, and the Canon Cat.

See [PROPOSAL.md](PROPOSAL.md) for the full vision. Local-only for now.

## Quick start

```bash
nix build
./result/bin/glyphtoo                  # built-in sample
./result/bin/glyphtoo notes.md         # open a file
./result/bin/glyphtoo a.md b.md        # dual-pane, two documents
```

Or with system packages (CMake ≥ 3.16, SDL2, FreeType):

```bash
mkdir build && cd build && cmake .. && make -j
./glyphtoo --font /path/to/font.ttf notes.md
```

Font resolution: `--font` → `GLYPHTOO_FONT` env → common system paths.
`nix build` wraps the binary with DejaVu Sans.

## Markup

Supported dialect (intentionally small):

| Syntax | Meaning |
|--------|---------|
| `#` … `######` | Headings |
| blank-line separated | Paragraphs |
| `- ` / `* ` / `+ ` | Unordered list items |
| `1. ` `2. ` … | Ordered list items |
| `> …` | Block quote |
| `*em*` `**strong**` `` `code` `` | Inline styles |
| `[text](#anchor)` / `[text](url)` | Links |
| `![alt](path.png){scale=1.2}` | Images (PNG/JPEG/BMP/GIF) |
| `:::stretch summary="…"` … `:::` | Collapsed stretchtext |
| `:::columns` / `:::column` | Side-by-side columns |

## Keybindings

### Files
| Key | Action |
|-----|--------|
| **Ctrl+S** | Save (untitled → `untitled.md`) |
| **Ctrl+Shift+S** | Save As (path in status bar) |
| **Ctrl+Q** | Quit (warns once if dirty) |

### Edit
| Key | Action |
|-----|--------|
| Type / Backspace / Delete | Insert / delete |
| **Enter** | Split paragraph / list item (empty list item exits list) |
| Backspace at block start | Merge with previous block (drops empty lists) |
| **Ctrl+Z** / **Ctrl+Y** | Undo / redo |
| **Ctrl+C** / **X** / **V** | Clipboard |
| **Ctrl+B** / **I** / **\`** / **K** | Bold / italic / code / link |
| **Ctrl+D** | Duplicate current block |
| **Alt+↑ / Alt+↓** | Move block among siblings |
| **Ctrl+E** | Wrap block in stretchtext |
| **Ctrl+Shift+E** | Edit stretch summary |
| **Ctrl+Shift+U** | Unwrap stretchtext |
| **Ctrl+0** | Demote to paragraph |
| **Ctrl+1…6** | Set heading level |
| **Ctrl+Shift+Q** | Toggle block quote |
| **Ctrl+Shift+[** / **]** | Collapse / expand all stretchtext |
| Double-click | Select word |

### Navigate
| Key | Action |
|-----|--------|
| Arrows | Move caret (scrolls into view) |
| **Ctrl+←/→** | Word motion |
| **Home** / **End** | Line start / end |
| **Ctrl+A** | Select current block |
| **Ctrl+Shift+A** | Select whole document |
| Click blue link | Follow `#anchor` or open external `http(s)` / `mailto:` |
| Hover `#anchor` link | Preview tooltip of the target passage |
| **Ctrl+F** | Leap search |
| **Ctrl+H** | Leap replace (Tab switches fields) |
| **F3** / **Ctrl+G** | Next match |
| **Esc** | Cancel mode / clear selection |

### View
| Key | Action |
|-----|--------|
| **Ctrl+\\** | Toggle dual pane |
| **Tab** | Focus other pane (or switch replace field) |
| Drag gutter | Resize panes |
| Drag scrollbar | Scroll |
| Drag image corner | Resize image |
| **Ctrl++** / **Ctrl+-** | Zoom text |
| Click ▸/▾ | Expand / collapse stretchtext |

## Architecture notes

- **Document** — arena of nodes with stable `NodeId`s; layout is a pure function
  of document state (data-oriented, Casey Muratori–style).
- **Selection** — two carets (anchor + focus); highlights derived from glyphs.
- **Undo** — per-document snapshot stacks.
- **Vendored** — `external/stb_image.h` (public domain).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
