# xanatoo help

xanatoo is a **visual hypertext** editor — stretchtext, transclusion, dual-pane
views, and living links. This page is an ordinary markup document shown in an
in-app window.

Jump to: [Files](#Files) · [Edit](#Edit) · [Navigate](#Navigate) · [View](#View-&-layout) · [Concepts](#Concepts)

---

## Files

- **Ctrl+N** — New document (warns if unsaved; repeat to discard)
- **Ctrl+O** — Open file… (type a path in the status bar)
- **Ctrl+S** — Save
- **Ctrl+Shift+S** — Save As…
- **Ctrl+R** / **Ctrl+Shift+R** — Reload / force reload
- **Ctrl+Q** — Quit (warns if unsaved; repeat to exit)
- **Ctrl+T** — New tab
- **Ctrl+W** — Close floating window, help, outline, or document tab
- **Ctrl+Tab** / **Ctrl+Shift+Tab** — Next / previous tab
- **Ctrl+Shift+P** — Pop focused document out to a floating window
- **Ctrl+Shift+I** — Dock top floating window back as a tab
- **Alt+F / E / O / I / V / B / H** — File / Edit / Format / Insert / View / Bookmarks / Help menu
- **Middle-click** — Paste at caret
- **Drag .md / .sexp** — Open in a software window

Export via *File → Export as S-expression…* or *Export as Markdown…*.

## Edit

- **Enter** — Split paragraph or list item
- **Ctrl+Enter** — New paragraph after the current block
- **Ctrl+Shift+-** — Insert horizontal rule
- **Ctrl+Shift+I** — Insert image (type a path in the status bar)
- **Drag image file** — Drop PNG/JPEG/GIF/BMP into a pane
- **Backspace** / **Delete** — Delete (merges at block start)
- **Ctrl+Z** / **Ctrl+Y** — Undo / redo
- **Ctrl+C** / **X** / **V** — Copy / cut / paste
- **Ctrl+B** / **I** / **`** / **K** — Bold / italic / code / link
- **Ctrl+A** — Select current block
- **Ctrl+Shift+A** — Select all
- **Double-click** / **triple-click** — Select word / block
- **Ctrl+D** — Duplicate block
- **Alt+Up** / **Alt+Down** — Move block among siblings
- **Ctrl+E** — Wrap in stretchtext
- **Ctrl+Shift+E** — Edit stretch summary
- **Ctrl+Shift+U** — Unwrap stretchtext
- **Ctrl+0** — Demote to paragraph
- **Ctrl+1** — Header (no h1–h6 rank)
- **Ctrl+2**…**Ctrl+6** — Header with soft depth hint
- **Ctrl+Shift+1** — Wrap block(s) in a Section
- **Ctrl+Shift+2** — Wrap block(s) in an Article
- **Ctrl+Shift+Q** — Toggle block quote
- **Ctrl+Shift+8** / **7** — Unordered / ordered list
- **Tab** / **Shift+Tab** — Indent / outdent list item

## Navigate

- **Arrows** / **Home** / **End** — Move caret
- **Ctrl+Home** / **End** — Document start / end
- **Page Up** / **Down** — Scroll by page
- **Ctrl+←** / **→** — Word left / right
- **Ctrl+F** / **F3** — Leap search / next match
- **Ctrl+H** — Find and replace
- **Ctrl+Shift+O** — Outline (headers by section depth)
- **Click a link** — Follow `#anchor` or open http(s)

## View & layout

- **View → Examples** / **Help → Examples** — open sample documents in soft windows
- **Examples → Diff demo (v1 | v2)** — dual-pane load of `diff-v1.md` and `diff-v2.md` with diff marks on
- **Ctrl+Shift+D** — toggle diff marks (needs two independent files)

- **Ctrl+\\** — Toggle dual pane
- **Dual pane browsing** — hover a link in the left pane to preview it on the
  right, click to keep it; a link clicked on the right moves that page to the
  left and opens the target on the right (Ctrl+click keeps the old behaviour)
- **Esc** / toolbar **✕** — stop loading a page (remote pages load in the
  background; the pane shows a progress bar and timer)
- **Ctrl+Shift+D** — Diff marks (two independent files)
- **Ctrl+Shift+L** — Structure highlight (colour bars + kind tags on the right)
- **Drag left margin** — Reorder or reparent a block
- **Drag column gutter** — Resize column widths
- **Drag image corner** — Resize image
- **Ctrl++** / **Ctrl+-** — Zoom text
- **Click ▸/▾** — Expand / collapse stretchtext

:::stretch summary="About stretchtext"
Stretchtext collapses a passage behind a one-line summary. Click the marker to
expand or collapse. Use **Ctrl+E** to wrap the current block.
:::

## Concepts

xanatoo treats structure as data: headings, lists, columns, includes, and
images are nodes in a tree, not only visual decorations.

- **Transclusion** — `:::include #Target` or a path embeds another passage live
- **Dual pane** — a two-page trail: left links open on the right, right links shift left
- **S-expression format** — experimental `.sexp` serialization of the tree

Press **F1** or **Esc** to close this window.

## Sectioning (article / section / header)

Prefer nested **section** / **article** containers over ranked headings.
A **header** is the title of the enclosing unit. Outline depth follows
nesting. **Enter** on a header starts a body paragraph (not another title).
See **Examples → Article / section / header**.

## Browse mode and gamepad

**F2** toggles **Browse mode** (read-only). Typing, edits, paste, and
structure changes are blocked (menus/toolbar included); the status bar turns
green. Use this for reading and navigating.

### Keyboard (browse)
- Arrows / Page / Home / End — move and scroll
- **Space** or **Enter** — expand/collapse stretch or follow link under caret
- **Ctrl+[** / **Ctrl+]** — collapse / expand all stretches
- **F2** — return to edit mode

### Gamepad
| Control | Action |
|---------|--------|
| D-pad | Move by line; Left/Right toggles stretch under caret |
| **A** | Activate (stretch or link) |
| **B** | Collapse stretch (or collapse all) |
| **X** | Toggle outline |
| **Y** | Toggle structure highlight |
| **LB / RB** | Previous / next header |
| **LT / RT** | Page up / page down |
| Left stick | Scroll |
| Stick click L/R | Collapse all / expand all |
| **Back** | Toggle browse mode |
| **Start** | Help |

## Navigation (history & bookmarks)

- **Alt+Left** / **Alt+Right** — Back / Forward in session history
- **Browse toolbar** — Back, Forward, Reload, Native view (web page ↔ `wikipedia:` / `hn:` / `reddit:` / `lemmy:` form; pressed while native), location field, Bookmark, History, Browse mode
- **Edit toolbar** — file, clipboard, bold/italic/link; menus for more format, structure, lists, insert; Find, Split pane and Heading outline toggles, and view tools
- **Services menu** — front pages of the built-in clients: Wikipedia, Hacker News, Reddit, Lemmy, Nostr, Gemini, Standard Ebooks, books and feeds
- **Ctrl+Shift+H** / **View → History** — session history panel (click to open)
- **Ctrl+Shift+B** — Bookmark or un-bookmark the current page
- **Bookmarks** menu — add/remove, list, open; **Show Bookmarks…** for the manager
- Bookmarks persist in `~/.config/xanatoo/bookmarks` (one URL or path per line)
- Local files and `gemini://` pages share the same session history

## Org-mode (`.org`)

Files ending in `.org` parse as an Org-mode subset: outline headings
with optional `TODO`/`DONE` keywords and `:tags:`, lists and checkboxes,
pipe tables, `#+BEGIN_SRC` / `QUOTE` / `EXAMPLE`, `[[link][desc]]`,
and `#+TITLE` / `#+AUTHOR`. Tags live on the node (`Node.tags`);
TODO keywords use `Header.src`; `:CUSTOM_ID:` becomes the stable label.

## Gemini (gemtext + gemini://)

- **`.gmi` / `.gemini`** files parse as gemtext (headers, `=>` links, lists, quotes, pre).
- **Ctrl+L** / **File → Open URL…** — focuses the **location field** in the menu bar
  (path or `gemini://`); press **Enter** to open.
- With **XANATOO_GEMINI** (default ON), the client fetches over TLS (port 1965) and
  shows the page in **browse mode**. Relative `=>` links resolve against the
  current capsule URL. Status **10/11** input requests reopen the URL bar with a
  `?` query for you to complete. Build without the client: `-DXANATOO_GEMINI=OFF`.


## Handheld / gamepad

- `--fullscreen` / `-F` — borderless fullscreen (R36S launcher uses this)
- `--ui-scale N` or `XANATOO_UI_SCALE` — enlarge ImGui chrome (auto 1.35 when width ≤720)
- **Right stick** software cursor · **A** click · **B** right-click
- **LB+A** activate under caret · **LB+B** collapse stretch
- **D-pad** move · **Left stick** scroll · **Back** history/browse · **Y** forward · **Start** help

- **F3** — toggle on-screen virtual keyboard
