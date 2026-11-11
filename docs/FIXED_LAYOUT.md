# Fixed-layout documents (PDF, DjVu, comic archives)

PDF, DjVu, CBZ, CBR, CB7 and CBT files open as ordinary xanatoo documents.
Each page of the file is one `Page` node. Bookmarks, history, sessions,
the outline, passages and paged reading all work as they do on any other
page, so a PDF needs no viewer mode of its own.

thumtoo (`external/thumtoo`, see AGENTS.md "Images through thumtoo") does
all the file work: page lists, page sizes, outlines and rendered tiles. Its
cache (`$XDG_CACHE_HOME/thumtoo`) is shared with galapix and biltoo, so a
book opened in one of them is already tiled for the others.

## Document model

```
Document  (meta.title = file name)
  Page              pages before the first outline entry
  Section  level 1
    Header "Chapter 1"
    Page ...
    Section level 2
      Header "1.1"
      Page ...
```

- `NodeKind::Page`: `src` = thumtoo page URI (`file:///a.pdf//page:3`,
  `file:///a.cbz//archive:001.png`), `width` / `height` = the page's
  layout size in thumtoo's pixels (PDF at 144 dpi), `alt` = page label
  (number, or the member file name for archives), `label` = `page-N`.
- The outline (PDF and DjVu) becomes nested Sections. An outline entry
  opens its section in front of its target page. Archives have no outline.
- Archive members are sorted by name in natural order (`natural_less`:
  `p2` before `p10`). Packers store members in any order.
- Page nodes serialize everywhere:
  - Markdown: `:::page src="…" width=W height=H id="page-N" alt="…"`
  - S-expression: `(page :label … :src … :width … :height … :alt …)`
  - XML: `<page … width= height=>`

  A saved copy therefore stays a valid document that points at the file.

### Never guess a size

A page's size has to come from thumtoo. If thumtoo cannot size a page, the
page is replaced by a paragraph saying it could not be read. Nothing ever
lays it out as a square or assumes the size of its neighbours. For the same
reason, `read_fixed_layout` waits until every page is sized before the
document exists. A slow first open is better than a layout that later
jumps. The sizes are stored in thumtoo's Store, so the second open is fast.

Sizing per format:

- **PDF:** `ensure_pdf_page_sizes`, one pass over the file.
- **Archives:** one bulk `prepare_paths` pass, so a solid RAR is extracted
  once.
- **DjVu:** a `request_size` probe per page.
- Any page these passes miss is probed on its own.

## Layout and drawing

- **Layout** (`layout.cpp`, `case NodeKind::Page`): the page takes the
  full column width, and its height follows the aspect ratio. It is pushed
  as a `PositionedImage` with `page_w` / `page_h` set, so it never goes
  through `ImageCache`, has no resize handle and no hover preview.
- **Style role `page`:** `space-after`, `background` (the paper shown
  before tiles arrive) and `border`.
- **Raster** (`render.cpp`): the pane raster holds only the sheet and its
  frame.
- **Tiles** (`app_pagetiles.cpp`): tiles are drawn over the raster every
  frame, so an arriving tile never invalidates the raster.
  - thumtoo's LOD layer does the planning: one `TileLoader` per page URI,
    one `TileSession` per (pane, page) and the global `TileScheduler`.
  - The scheduler's wake hook only records when to pump.
    `pump_page_tiles` pumps it from the frame loop.
  - Coarser tiles are drawn first as stand-ins, then exact tiles over them.
  - Views, textures and loaders that go unused for a while are dropped.
- **Paged mode** (`paginate.cpp`): a `page` atom that has content above
  it on the current page starts a new page, so every sheet begins its own
  screen page. Sheets taller than the screen page are already scaled down
  to fit by the existing image handling.

## Opening

- `is_fixed_layout_path` covers local files only, by extension. They are
  foreign, so always read-only (`is_foreign_path`).
- Navigation (`open_page`) reads the file on a fetch worker (service
  "Pages", with the usual loading indicator).
- `load_doc` (tabs, session restore) reads synchronously.
- Without thumtoo (minimal ports) the file shows an error page, and
  `app_pagetiles.cpp` is a stub. Android's thumtoo has no MuPDF or DjVu,
  so there only archives work.

## Open items

- CBZ double-page spreads split into two pages (a thumtoo matter).
- `#page:N` selector, and `//page:N` cutting the view to a single page.
- In paged mode an outline Section header takes a screen page of its own.
- Fit width / fit page / zoom. Today the measure setting narrows pages.
- Progressive loading: show the sized prefix while the rest is probed.
- The synchronous `load_doc` path (tabs, session restore) blocks the UI
  while a large file is sized for the first time.
- PDF links, text selection and search from thumtoo's text / OCR layers.
- A page sorter (grid of pages through Collection).
- PDF zoom floor from the page profile (`decide_zoom_floor`) instead of
  the fixed `-2`.
- Coarser-tile `src_uv` is truncated to whole pixels.
