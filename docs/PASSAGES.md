# Passages: bookmarks, selections, transclusion

Design note. Slices 1 and 2 below are implemented (`locator.hpp`,
`app_passage.cpp`), the rest is the plan. Bookmarks, selections, the
"part of a document as a document" view and image crops are one concept with
different lifetimes and presentations, so they should share one address
format and one data path.

## Terms

- **Passage**: a part of a document: a point, a text range, a node, a section,
  a rectangle of an image.
- **Selector**: how a passage is named, durable and independent of the session
  (`id:intro`, `text:…`, `rect:…`).
- **Locator**: an address: base document + selectors. A plain string, so it
  fits everywhere a URL does (location bar, history, bookmarks, links).
- **Extent**: a selector resolved against one loaded document (`NodeId`,
  `Caret`s, a rect). Session data, rebuilt when the document changes.
- **Cut**: showing a passage *as* the document (what "transclusion" meant in
  the request). **Embed**: the existing `Transclude` node, a cut placed inside
  another document.

| Thing | What it is |
|-------|------------|
| Selection | an Extent held by a pane, live, not stored |
| Bookmark | a stored Locator + a record (title, quote, revision) |
| Link | a Locator in a document |
| Cut | a pane showing a Locator |
| Mark / overlay | a pane showing an Extent on top of the whole document |
| Embed | a Locator in a `Transclude` node or an image `src` |

The verbs convert between them: selection → "Copy address", "Bookmark",
"Show only this"; bookmark → open as cut or as mark.

## Address syntax: pipes *and* fragments

Decision: both, with different meanings and the same selectors.

- `//selector` **cuts**: the result is the passage, as a document of its own.
- `#selector` **points**: the result is the whole thing, scrolled to the
  passage with the passage marked. This is what a fragment means on the web
  anyway.

```text
book.md//id:intro                section as a document
book.md#id:intro                 whole book, at the section, marked
book.md#intro                    same; a bare fragment is a label (as today)
book.md//id:ch3//text:the%20best,of%20times    range inside a section, cut
book.md//id:ch3#text:the%20best,of%20times     section cut, range marked
map.png//rect:0.2,0.1,0.3,0.3    crop, usable as an image anywhere
map.png#rect:0.2,0.1,0.3,0.3     whole image, region framed
doc.md//id:fig1//rect:0,0,.5,.5  crop of an image inside a document
book.pdf//page:12#rect:…         thumtoo pipes keep working in the chain
```

Rules:

- Pipes apply left to right, each to the result of the one before. Selectors
  are typed by what they apply to (document or image), so an image node
  selected from a document takes `rect:`.
- At most one `#`, and it comes last.
- Payloads are percent-encoded (`/`, `#`, `?`, `|`, `%`, spaces). `text:`
  stays encoded in `Selector::value`, since its grammar splits on `,` and `-`
  first.
- Only known selector names count as pipes. The parser strips
  `//<known>:…` segments from the right end, because plain URLs contain `//`
  (`https://web.archive.org/web/2020/https://x.org/`). Pipes are never sent to
  a server.
- `#foo:bar` with an unknown name, or matching a label exactly, stays a label.
  HTML ids may contain colons.
- Spelling follows thumtoo's Location model (`//page:12`). xanatoo needs its
  own small always-built parser: `XANATOO_THUMTOO` is optional and thumtoo's
  pipe kinds are a closed set. Thumtoo pipes are no selectors, so they stay in
  the base: `book.pdf//page:12#rect:…` has the base `book.pdf//page:12`.
- HTML compatibility is input and output aliasing. `#heading`, `#:~:text=…`
  and media fragments `#xywh=` are accepted, as thumtoo accepts `#page=` and
  rewrites it. A `#text:` address can be emitted as `#:~:text=` so that a
  copied link works in other browsers.

## Selectors

| Selector | Applies to | Meaning |
|----------|------------|---------|
| `id:LABEL` | document | node by `Node.label` / heading anchor, as `jump_to_href` |
| `path:0.2.1` | document | child-index path, as `section_stable_key`; fallback for unlabelled nodes |
| `text:[prefix-,]start[,end][,-suffix]` | document | text range, grammar of the web's text fragments |
| `text:prefix-,,-suffix` | document | point between two contexts (empty start) |
| `rect:x,y,w,h` | image | fractions of the oriented image |
| `at:x,y` | image | point, same units |
| `item:N` | collection | one item (later) |
| `rev:ID` | anything | pin to a revision (later, `docs/REVISIONS.md` phase 5) |
| `page:N`, `archive:…` | thumtoo | part of the base |

Choices behind this:

- **No `NodeId`, no span index in addresses.** NodeIds last a session; span
  boundaries move when formatting changes. Text is addressed by content, which
  also survives re-fetches of foreign pages that we cannot give stable ids.
  This follows the W3C Web Annotation quote selector and text fragments.
- **`selector_for` picks the shortest robust form**: `id:` when the selection
  is exactly a labelled node, otherwise nearest enclosing label + `text:`
  (`//id:ch3//text:…`). This keeps addresses short and makes matches local.
- **Long ranges keep only start and end**, which is the reason for the
  text-fragment grammar over a full quote.
- **`rect` uses fractions, not pixels**, because the image is seen at many
  resolutions (tile scales, thumbnails) and the same content may exist in
  several sizes. Input alias `#xywh=` takes pixels and percent.

## Data

Plain data and free functions, pure parts unit-tested without SDL:

```cpp
enum class SelectorKind : uint8_t { Id, Path, Text, Rect, At };
struct Selector { SelectorKind kind; std::string value; };

struct Locator {
  std::string base;                // what buffers, caches and fetches see
  std::vector<Selector> pipes;
  std::optional<Selector> point;   // the `#` part
};
Locator parse_locator(std::string_view);   // total: no selectors → all base
std::string format_locator(const Locator&);
bool locator_up(Locator&);
Resolve resolve_node(const Document&, NodeId within, const Selector&, NodeId&);
Selector node_selector(const Document&, NodeId);
```

Planned with slice 3, when text and rects get resolved:

```cpp
struct Extent {
  enum class Kind : uint8_t { Whole, Node, Text, Rect } kind;
  NodeId node;           // Node, Rect (the image), Text (enclosing block)
  Selection text;        // Text
  FRect rect;            // Rect
};
Selector selector_for(const Document&, const Selection&);
```

**The pipes are view state, not buffer identity.** `Locator::base` is the only
thing buffers, doc keys, drafts, caches and fetches see. All pipe parsing
happens once, at the top of `navigate_pane`. Consequences:

- `book.md` and `book.md//id:intro` share one `DocBuffer`. A cut needs no
  reload, and edits in a cut are ordinary edits of the document.
- `is_*_url` dispatch never sees a pipe.
- `Pane` holds the selectors (`locator`, valid while `locator_buf == buf`),
  the resolved `cut` and the text nodes of the `#` passage (`mark`).
  `pane_address` gives the address for the location bar, history and
  bookmarks, `pane_cut` is what layout uses.

## Presentation

**Cut.** Section Focus filtered layout to one Section / Article
(`layout(..., focus_section)`, the outline stays complete). A node cut is the
same thing, generalised:

- Per `Pane`, for any block. Section Focus is a cut the outline picks
  (`Pane::section_focus`) and shows up in the location bar.
- A section cut still shows the headers of its parent sections, as Section
  Focus always did.
- Text ranges (slice 4): blocks from start to end, clipping the first and last block at
  the carets (layout input: `Extent`, not `NodeId`).
- Images (slice 5): a source rect at draw time, in the pane, on the Image node and in
  the `Lightbox`. Same decode and tile cache, no new image.

**Up.** `page_up_url` handles pipes before `up_url`:

```text
book.md//id:ch3//text:x  →  book.md//id:ch3#text:x  →  book.md//id:ch3
                         →  book.md#id:ch3  →  book.md  →  directory
```

So the last pipe turns into `#`: the first Up shows the surrounding context
with the passage still marked and in place, the next drops the mark. The
step from cut to whole document needs no fetch and no reload, as the buffer is
shared. It keeps the passage on its spot on screen with `Pane::view_anchor`.
Back returns to the cut through the normal per-view `nav` history.

**Mark / overlay.** Today `Pane::mark`: the `#` passage, whole text nodes,
in the `mark` colour of the page style. Slice 3 turns it into `Pane::marks`, a
list of `{Extent, role}` drawn over the page. One draw path for `#` targets,
bookmarks of this document, search hits later, and images (a frame over the
image, in the pane and the Lightbox).

- Generalises the `SplitTrail` wash and the selection wash.
- Bookmark roles come from `DocStyle` as well.
- Scrollbar ticks like the chapter marks; click selects the passage.
- `#` targets clear on the next click or navigation; bookmark marks follow a
  View toggle.
- Remote pages get their `#fragment` when the fetch lands, marked as well.

**Embed.** `Transclude` targets become Locators. Image `src` accepts a
Locator, so `![](map.png//rect:…)` works. Since an embed always cuts, the
existing `:::include path.md#Heading` keeps working and means `//id:Heading`.
The `:excerpt` summary becomes a `text:` selector in the same way. A click on
an embed opens the cut; Up from there is the context.

## Bookmarks

The `bookmarks` file stays line-based and plain, with no SQLite dependency,
since Locators fit the existing URL-per-line format. Optional tab-separated
`key=value` fields follow the URL, so old files stay valid:

```text
book.md//id:ch3//text:the%20best,of%20times	title=Chapter 3	quote=…	rev=…	at=2026-10-31
```

`App::bookmarks` becomes `std::vector<Bookmark>`
(`loc`, `title`, `quote`, `rev`, `created`). `quote` keeps the passage text
for display and for re-anchoring, and `rev` is the content id of the revision
seen, where a store exists.

- Ctrl+Alt+B with a selection bookmarks the selection; with only a caret, a
  point (position in a book); without a document position, the page, as today.
- `is_bookmarked` stays "this exact address"; the toolbar star also lights up
  when the page has bookmarks inside it.
- Bookmarks panel: grouped by document, each row shows title + quote; the
  panel can show a miniature cut, a passage card from `docs/BRAINSTORM.md`.
- Opening picks mark by default (context first), with "Open as cut" beside it.

## When the document changes

Resolving returns:

- **Exact**: one match.
- **Moved** (with `text:`, slice 3): found elsewhere, several matches (the nearest to the old position
  wins), or found only with fuzzy matching.
- **Lost**: not found. Never delete the bookmark: open the whole document,
  flash the status, and show the stored quote. With a `rev` the old text can be
  shown from the revision store, for local documents.

Passages are found again in `refresh_views_of_buf`; slice 3 adds the
`mark_dirty` debounce for Extents, rather than kept up to date through edits. Later, persistent text
units (`docs/REVISIONS.md` phase 4) replace matching for local documents. The
address format does not change: `id:` can then hold a unit id.

## Slices

Each is useful on its own. 1 and 2 are done:

1. `parse_locator` / `format_locator` + unit tests, including the
   web.archive.org and `hn:item?id=1` cases. No behaviour change.
2. `navigate_pane` splits the Locator; `#id:` on local and remote pages;
   `//id:` as per-pane cut (Section Focus moved to `Pane`); `page_up_url`
   steps; location bar and history carry the full address. Sections can then
   be bookmarked through the existing file format.
3. `selector_for` / `resolve_selector` for `text:`; `Pane::marks`; bookmark a
   caret or selection; `Bookmark` record; panel with quotes.
4. Text range cuts (clipped first and last block). "Show only this" on a
   selection; Copy address.
5. Images: `rect:` / `at:` in pane and Lightbox, drag-select a region,
   `src` with a Locator.
6. Embeds through Locators; `rev:` pinning with the revision store; link index
   and backlinks over all Locators (`docs/BRAINSTORM.md` §6).

## Open questions

- Should a real cut drop the headers of parent sections? It would read more
  like a document of its own; Section Focus wants them as context.
- Should bookmark marks show by default on every page, or only on request?
- Editing inside a cut of a *foreign* document stays impossible (`browsing`),
  but should a cut across two sections be editable at all at the seams?
- `Transclude` of a `Transclude`: cuts go through the same depth limit and
  cycle check as today?
- Multiple marks in one address (`#a#b`)? Not planned; a set of passages is a
  collection of bookmarks, not an address.
