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
- **Ctrl+W** — Close top soft window / help / outline
- **Alt+F / E / O / I / V / H** — File / Edit / Format / Insert / View / Help menu
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
- **Ctrl+0** … **Ctrl+6** — Paragraph / heading level
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
- **Ctrl+Shift+O** — Heading outline
- **Click a link** — Follow `#anchor` or open http(s)

## View & layout

- **View → Examples** / **Help → Examples** — open sample documents in soft windows
- **Examples → Diff demo (v1 | v2)** — dual-pane load of `diff-v1.md` and `diff-v2.md` with diff marks on
- **Ctrl+Shift+D** — toggle diff marks (needs two independent files)

- **Ctrl+\\** — Toggle dual pane
- **Ctrl+Shift+D** — Diff marks (two independent files)
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
- **Dual pane** — same document or two files; links can sync both views
- **S-expression format** — experimental `.sexp` serialization of the tree

Press **F1** or **Esc** to close this window.
