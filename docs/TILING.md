<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# Tiling, trails, and comparisons

## Separate arrangement from relationships

A **tile** is a document view with independent selection, scroll, and
navigation state. A **split** is a geometric arrangement of tiles; it does
not imply that following a link in one tile should navigate another tile.

Relationships belong to a specific pair of tiles, with the divider as their
visible control:

| Divider mode | Behaviour | Indicator |
| --- | --- | --- |
| Independent | Two unrelated document views. Links stay in their own tile; each has its own Back/Forward. | Neutral `TILES` |
| Trail | A link from tile one opens in tile two; a link from tile two advances the shared browsing trail. Both views share Trail Back/Forward. | Blue `TRAIL` |
| Compare | Independent views plus structural diff marks. Neither tile navigates the other. | Wide amber `DIFF` |

These relationships are not an alternative to tiling. A tile pair may
switch from Independent to Trail or Compare without being recreated; scroll
and selection stay with their views. Leaving Trail suspends its history,
which may be resumed if the same two documents are still showing.

## First implementation (#313)

- Split and Trail have separate toolbar buttons; Ctrl+Backslash selects
  independent tiles.
- The divider has a mode-coloured background and a labelled button; click
  the button or right-click the divider for Independent / Trail / Compare.
- The same menu changes the arrangement between **Side by side** (columns)
  and **Stacked** (rows). The divider is draggable on either axis.
- Compare uses a wider divider. This reserves room for a future diff overview
  without changing the document viewport APIs now.
- Session save/restore records orientation and relationship; existing
  sessions with `split=1` but without `tile_mode` restore as Trail.

For now, the centre layout still has **two leaves** (`panes[0]` and
`panes[1]`); the UI does not yet support three or more tiled panes. Also,
the Trail history in a session is reseeded from the saved pages, rather than
serializing its complete stack.

## Next: arbitrary tile trees

The pure geometry model is now implemented in `tile_tree.hpp` (#315), and
the existing two centre panes already use it for their computed rectangles.
It supports nested split/close, retaining stable leaf view IDs. The runtime
still needs to host more than two centre views.

Replace the remaining fixed centre-slot assumptions with the tree of nodes:

```
TileNode = Leaf(ViewId)
         | Split { axis: Columns | Rows,
                   ratio: float,
                   first: TileNode,
                   second: TileNode,
                   relation: Independent | Trail | Compare }
```

Each split node draws and hit-tests its own divider; its relation addresses
its immediate two children (initially individual leaves). For a split
containing more than one leaf on either side, explicit source/target view
selection is needed before enabling a linked relation. There must be no
implicit arbitrary choice of which grandchild is the comparison target.

Work incrementally:

1. Introduce stable `ViewId` handles for centre tiles, reusing the existing
   `Pane` view state. Decouple navigation from fixed indices `0` / `1`.
2. ~~Move layout to a recursive `TileNode` model with a two-leaf adapter.~~
   Done in #315. Next, move pointer hit-testing and divider drag ratios to
   the tree, then permit new leaf view IDs in the live centre set.
3. Add Split Horizontally / Split Vertically / Close Tile / Move Tile actions.
   Closing a leaf collapses its parent split without invalidating surviving
   views' navigation state.
4. Move Trail and Compare data into explicit pair relationships (`ViewId`
   endpoints), including preview, diff, synced scrolling, and cross-pane
   connector overlays. Each internal divider exposes its own relation menu.
5. Extend the initial **diff overview** in Compare dividers (added in #314):
   the current two lanes show approximate document-order changed blocks and
   current viewports; clicks/drags seek to a normalized position in both
   documents. Next: source-offset mapping for exact changed-range seeking,
   next/previous hunk actions, and an explicit synchronized-scroll option.
   Ordinary scrolling stays independent unless sync is requested.

Every internal node must respect minimum tile size, screen bounds, pointer
capture, toolbars, scrollbars, and docked side panels. A tiny screen may need
an overflow strategy (collapse or tabbed leaves) instead of forcing
unreadably narrow nested splits.

## Later: save workspaces, not only bookmarks

A bookmark identifies content and a position within it. A **workspace**
snapshot is different: it identifies which buffers are open, the entire
tiling tree, tile-specific scroll/selection/navigation, relationships and
their options, and which tile has focus. These snapshots can reuse session
serialization, but should have a separate user-facing command and persistence
identity. Do not make regular document bookmarks implicitly restore all
application windows or tabs.

The data model should distinguish a live, mutable session from a named,
restorable workspace. Account-specific or sensitive document state should
be handled explicitly when snapshot export is introduced.
