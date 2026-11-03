# Paged reading

An alternative to scrolling for long form text: the page is cut into
ebook style **pages**, shown one **spread** at a time (1–3 text columns side
by side), with a bar at the bottom for where you are.

Like reader mode it is **view state** (`Pane::paged`, unset =
`ViewSettings::paged`), never part of the address: bookmarks, links and
passages (`docs/PASSAGES.md`) are the same in both modes, and a bookmark made
while reading pages opens at the right text at any font size or window size.

## Using it

- **F8** / View → **Paged Reading** switches the focused view; View → **Page
  columns** picks 1 / 2 / 3 or "fit the width" (as many columns of ~34 em as
  fit). Edit → Preferences → *Paged reading by default* sets it for views not
  switched themselves (`paged=` / `page_columns=` in view.conf).
- Turn the page: **PgDn / PgUp**, mouse wheel (one notch = one spread),
  click the left / right **edge** of the page (a faint arrow shows when the
  pointer is over one), the bar's ‹ › buttons, swipe on a touch screen; in
  browse mode also **← / →**. **Ctrl+Home / End** go to the first / last
  page, caret moves and jumps (outline, search, links) turn to the page that
  holds their target.
- The bar: `Page 7–8 of 78` · slider (click / drag to seek) ·
  `Loc 42 of 539 · 10%` · ‹ ›.
- Pages (and their count) follow font size, window size and the column
  count. **Locations** do not: one per 128 glyphs of text (Kindle style), so
  "Loc 42" names the same text at any zoom. Page / location are for the
  reader's orientation; storage still uses the address, not these numbers.

## How it works

Pagination is a system between layout and rendering, not a new layout:

1. `layout_panes` lays the whole page out **one column wide** (width of the
   pane / columns; no virtualization, the page count needs all of it).
2. `paginate` (`paginate.hpp`, pure data, unit tested) cuts that
   `LayoutResult` into pages: text lines and block images are *atoms* a break
   never runs through; a page ends at the latest safe y that fits (page
   height minus top / bottom padding), a heading is not left at the foot of a
   page, an image taller than a page is scaled down, anything taller (a long
   code block) is sliced. Page k lands in column `k % columns` of spread
   `k / columns`; spread s sits at content y `s * page_h`.
3. The result is again a plain `LayoutResult` (glyphs, boxes, bars, fills,
   images split / moved per page), so hit-testing, carets, selection,
   overlays and the raster cache work unchanged. A paged view only differs
   in that `scroll_y` is always a multiple of the spread height
   (`PageView::stride`, `spread_scroll` / `spread_at` / `page_turn`).

Consequences worth knowing:

- `Pane::h` is the page area; the bar lives below it (`page_bar_h`, carved
  out in `place_centre_rows` / `place_windows`, so every layout, overlay and
  clip sees the smaller view). `sb_reserve` / `sb_gutter` are 0, the classic
  scrollbar is not drawn or hit.
- The view anchor mechanism (`keep_views`) keeps the reading position through
  zoom, resize and a column change: after the new layout the spread that
  holds the old top line is shown.
- Selection across a page break works (document order is kept in the glyph
  list); caret Up / Down at the end of a column moves by geometry.

## Open

- The whole page is laid out and cut on every layout (typing in a paged edit
  view, zoom): fine for books of a few hundred pages, slow for huge pages.
  An incremental / cached pagination would need per-section page maps.
- No chapter ticks on the slider, no "first page of the chapter" heading in
  the bar, no page margins sheet (`page_x` / `page_w` are 0 while paged).
- Views copied into a window / tab (`pop_out`, `tear_tab`) take `scroll_y`
  along, which means a different spot between a paged and a scrolling view.
- Footnote overlay still draws over the bottom of the page.
