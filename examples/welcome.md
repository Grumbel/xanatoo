# The network

The web connects documents, but [hypertext](#hypertext) connects ideas.

![A small network of linked nodes](network.png){scale=0.9}

:::stretch summary="What is hypertext?"
Hypertext is a system of linked information that can be explored
non-linearly. Passages can expand in place (*stretchtext*), images are
first-class addressable content, and layout is part of the living
document rather than a fixed page prescription.
:::

:::columns
:::column weight=1.1
## Original

A passage from the original document, shown in a column.

![Stretchtext expands in place](stretchtext.png){scale=0.85}
:::
:::column
## Commentary

An editable annotation linked in spirit to the original passage.

![Side-by-side columns](columns.png){scale=0.85}
:::
:::

## Living documents

This is a live document. Click to place the caret, type to insert text,
Backspace to delete. **Enter** splits a paragraph (or list item; empty item exits the list). Shift+arrows select.
Ctrl+C / Ctrl+V / Ctrl+X for clipboard, Ctrl+Z / Ctrl+Y for undo/redo.
**Ctrl+\\** toggles a second view pane; **Tab** switches focus.
Drag the gutter to resize. Open two files for independent documents.
**Ctrl+F** leap search (type to find, F3 next, Esc cancel).
**Ctrl+←/→** move by word. Click blue links to jump to #anchors or
open external http(s) URLs in the system browser.
Drag the blue corner on images to resize. Home/End move on the line.
Click a stretchtext marker (▸ / ▾) to expand or collapse.
Press **F1** for the full keyboard reference.

Inspiration: Ted Nelson's *Xanadu*, Bill Atkinson's **HyperCard**, and
Jef Raskin's *Canon Cat* — a keyboard-driven work processor that treated
text as a continuous, searchable stream.

> "A document is not just something you render and read. It is a live
> workspace." — the glyphtoo proposal

---

A live embedding of another passage (click to jump to the source):

:::include #Living documents

Design pillars:

- Addressable passages and stable node IDs
- Stretchtext that reflows the living layout
  - Expand and collapse without losing place
  - Summary markers (▸ / ▾) stay interactive
- Side-by-side views of related material
- Direct manipulation of images and blocks

Ordered steps to explore:

1. Click a stretchtext marker to expand
2. Follow an internal or external link
3. Toggle dual-pane with Ctrl+\\

Try an internal link: [Living documents](#Living documents) — click to jump.
Or an external one: [Xanadu](https://en.wikipedia.org/wiki/Project_Xanadu).

Images load relative to the document directory (PNG, JPEG, BMP, GIF via
stb_image). Drag a corner handle to resize; the scale is saved as
`{scale=…}` in the markup.
