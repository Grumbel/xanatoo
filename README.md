# glyphtoo

Interactive document composition engine.

A lightweight, realtime, editable document system built with **C++20**, **SDL2**,
and **FreeType**. Inspired by Markdown, classical typesetting, Ted Nelson's
hypertext ideas, HyperCard, and the Canon Cat.

See [PROPOSAL.md](PROPOSAL.md) for the full vision. Local-only for now.

## Quick start

```bash
nix build
./result/bin/glyphtoo                  # opens examples/welcome.md when found
./result/bin/glyphtoo notes.md         # open a file
./result/bin/glyphtoo a.md b.md        # dual-pane, two documents
./glyphtoo examples/welcome.md         # sample with images (from repo root)
```

Or with system packages (CMake ≥ 3.16, SDL2, FreeType):

```bash
mkdir build && cd build && cmake .. && make -j
./glyphtoo --font /path/to/font.ttf ../examples/welcome.md
```

Font resolution: `--font` → `GLYPHTOO_FONT` env → common system paths.
`nix build` wraps the binary with DejaVu Sans.

The demo document lives in **`examples/welcome.md`** (with PNGs alongside).
With no path argument, glyphtoo searches for that file relative to the
working directory or the executable.

## Markup

Supported dialect (intentionally small):

| Syntax | Meaning |
|--------|---------|
| `#` … `######` | Headings |
| blank-line separated | Paragraphs |
| `- ` / `* ` / `+ ` | Unordered list items |
| `1. ` `2. ` … | Ordered list items |
| `> …` | Block quote |
| `---` / `***` / `___` | Horizontal rule |
| `*em*` `**strong**` `` `code` `` | Inline styles |
| `[text](#anchor)` / `[text](url)` | Links |
| `![alt](path.png){scale=1.2}` | Images (PNG/JPEG/BMP/GIF) |
| `:::stretch summary="…"` … `:::` | Collapsed stretchtext |
| `:::columns` / `:::column weight=1.5` | Side-by-side columns (draggable widths) |
| `:::include #Heading` | Transclude (live embed of a passage) |

## Keybindings

### Files
| Key | Action |
|-----|--------|
| **Ctrl+S** | Save (untitled → `untitled.md`) |
| **Ctrl+Shift+S** | Save As (path in status bar) |
| **Ctrl+Q** | Quit (warns once if dirty) |
| **F1** | Keyboard reference overlay |

### Edit
| Key | Action |
|-----|--------|
| Type / Backspace / Delete | Insert / delete |
| **Enter** | Split paragraph / list item (empty list item exits list) |
| **Ctrl+Enter** | Insert empty paragraph after current block |
| **Ctrl+Shift+-** | Insert horizontal rule (then a new paragraph) |
| Backspace at block start | Merge with previous block (drops empty lists) |
| **Ctrl+Z** / **Ctrl+Y** | Undo / redo |
| **Ctrl+C** / **X** / **V** | Clipboard (paste: blank lines → paragraphs) |
| **Ctrl+B** / **I** / **\`** / **K** | Bold / italic / code / link |
| **Ctrl+D** | Duplicate current block |
| **Alt+↑ / Alt+↓** | Move block among siblings |
| Drag left margin of block | Reorder / reparent block (columns & lists) |
| Drag column gutter | Adjust relative column widths |
| **Ctrl+E** | Wrap block in stretchtext |
| **Ctrl+Shift+E** | Edit stretch summary |
| **Ctrl+Shift+U** | Unwrap stretchtext |
| **Ctrl+0** | Demote to paragraph |
| **Ctrl+1…6** | Set heading level |
| **Ctrl+Shift+Q** | Toggle block quote |
| **Ctrl+Shift+8** | Toggle unordered list |
| **Ctrl+Shift+7** | Toggle ordered list |
| **Tab** / **Shift+Tab** | Indent / outdent list item (nested lists) |
| **Ctrl+Shift+[** / **]** | Collapse / expand all stretchtext |
| Double-click | Select word |

### Navigate
| Key | Action |
|-----|--------|
| Arrows | Move caret (scrolls into view) |
| **Ctrl+←/→** | Word motion |
| **Home** / **End** | Line start / end |
| **Ctrl+Home** / **Ctrl+End** | Document start / end |
| **Page Up** / **Page Down** | Scroll view and move caret |
| **Ctrl+A** | Select current block |
| **Ctrl+Shift+A** | Select whole document |
| Click blue link | Follow `#anchor` or open external `http(s)` / `mailto:` |
| **Ctrl+Click** link | Jump to `#anchor` in the other pane (enables split) |
| Hover `#anchor` link | Preview tooltip of the target passage |
| Click transclusion | Jump to the source passage |
| **Ctrl+F** | Leap search |
| **Ctrl+Shift+O** | Heading outline (jump to section) |
| **Ctrl+H** | Leap replace (Tab switches fields) |
| **F3** / **Ctrl+G** | Next match |
| **Esc** | Cancel mode / clear selection |

### View
| Key | Action |
|-----|--------|
| **Ctrl+\\** | Toggle dual pane |
| **Ctrl+Shift+D** | Diff marks between two open files (orange bars) |
| **Tab** | List indent, else focus other pane (or replace field) |
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
- **Status bar** — idle right side shows word / character / heading counts.
- **Vendored** — `external/stb_image.h` (public domain).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
