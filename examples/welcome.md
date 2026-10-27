# Welcome to xanatoo

An interactive document workspace — edit, stretch, link, and browse in one place.
Inspired by *Xanadu*, **HyperCard**, and the *Canon Cat*.

![A small network of linked nodes](network.png){scale=0.85}

## Quick start

1. Click anywhere and type — the document is live.
2. **Enter** splits a paragraph; **Ctrl+Z** undoes.
3. Click a stretchtext marker (▸ / ▾) to expand or collapse.
4. Follow blue links: internal `#anchors`, other example files, or `gemini://`.
5. **F1** opens the full keyboard reference · **F2** toggles browse (read-only) mode.

:::stretch summary="Essential keys (expand for more)"
| Key | Action |
|-----|--------|
| **Ctrl+S** | Save |
| **Ctrl+O** / **Ctrl+L** | Open file / focus location bar |
| **Ctrl+F** | Leap search (F3 next) |
| **Ctrl+\\** | Dual-pane split |
| **Ctrl+T** / **Ctrl+W** | New tab / close tab |
| **Ctrl+Shift+P** | Pop out to floating window |
| **Alt+Left** / **Alt+Right** | History back / forward |
| **Ctrl+Shift+H** | Session history panel |
| **Ctrl+Shift+B** | Bookmark this page |
| **Ctrl+B** / **I** / **K** | Bold / italic / link |
| **F1** | Keyboard help document |

Menus: **Alt+F** File · **E** Edit · **V** View · **B** Bookmarks · **H** Help.
:::

## Example documents

Open any of these from this page (links load in the same pane and join session history):

### Guides

- [Keyboard reference](help.md) — complete keybinding list (**F1**)
- [Article / Section / Header](sectioning.md) — semantic sectioning model

### Demos

- [Thread / RFC discussion](thread-rfc.md) — Thread and Message nodes
- [Gemini gemtext sample](gemini-sample.gmi) — native gemtext layout
- [Nostr discussions](nostr-sample.md) — relay-backed Thread/Message view
- [Recent Nostr notes](nostr:recent) — live firehose sample from public relays
- [r/GameDeals on Reddit](https://www.reddit.com/r/GameDeals/.json) — subreddit listing via `.json`
- [r/linux on Reddit](https://www.reddit.com/r/linux) — another public listing
- [Ask Lemmy](https://lemmy.world/c/asklemmy) — Lemmy community via API v3
- [programming.dev /c/rust](https://programming.dev/c/rust) — federated Lemmy community
- [Hacker News](https://news.ycombinator.com/) — front page and comment trees
- [Books and long reads](books.md) — Standard Ebooks / Project Gutenberg EPUBs
- [Feeds](feeds.md) — RSS / Atom: blogs, news, Mastodon, Reddit, arXiv
- [Collections](collection.md) — image grids / lists with a fullscreen viewer
- [Org-mode sample](sample.org) — headings, TODO, tags, source blocks
- [Semantic format (S-expression)](semantic.sexp) — experimental sexpr form
- [Sample (S-expression)](sample.sexp) — compact sexpr demo

### Diff pair (open both for side-by-side marks)

- [Diff demo — version 1](diff-v1.md)
- [Diff demo — version 2](diff-v2.md)

Or use **View → Examples → Diff demo (v1 \| v2, marks on)** to load both panes at once.

:::columns
:::column weight=1.1
### Stretchtext

![Stretchtext expands in place](stretchtext.png){scale=0.8}

Collapsed passages keep a one-line summary. Click the marker to expand; the layout reflows immediately.
:::
:::column
### Columns & images

![Side-by-side columns](columns.png){scale=0.8}

Drag column gutters to reweight widths. Drag an image’s blue corner to resize (`{scale=…}` is saved).
:::
:::

## Reddit

Subreddit listings and posts open in-pane via Reddit's JSON API
(same Thread/Message layout as Nostr). **Ctrl+L** accepts URLs with or without `.json`.

- [r/GameDeals](https://www.reddit.com/r/GameDeals/.json)
- [r/linux](https://www.reddit.com/r/linux)
- Or paste any `https://www.reddit.com/r/…` or post URL

If requests return **HTTP 403**, Reddit is blocking the client/IP. Set a script app's credentials:

`XANATOO_REDDIT_CLIENT_ID` / `XANATOO_REDDIT_CLIENT_SECRET`

(create at https://www.reddit.com/prefs/apps — type **script**).


## Lemmy

Community listings and posts open in-pane via **Lemmy API v3** (Thread/Message layout). **Ctrl+L** accepts any instance URL:

- [lemmy.world/c/asklemmy](https://lemmy.world/c/asklemmy)
- [programming.dev/c/rust](https://programming.dev/c/rust)
- Or paste `https://<instance>/c/<name>` or `/post/<id>`

## Hacker News

[Front page](https://news.ycombinator.com/), [New](https://news.ycombinator.com/newest),
[Ask](https://news.ycombinator.com/ask) and [Show](https://news.ycombinator.com/show)
lists plus whole comment trees (`/item?id=…`) open in the Thread/Message layout.
Comment HTML maps to paragraphs, `> quotes`, links and code blocks.

## Books

[Books and long reads](books.md) lists public-domain EPUBs from
[Standard Ebooks](https://standardebooks.org/ebooks) (catalogue, subjects,
search) and Project Gutenberg. A click downloads the book and imports it via
**pandoc**.

## Feeds

[Feeds](feeds.md) lists RSS and Atom feeds; entries render as message cards
with their HTML mapped to paragraphs, lists, quotes, code and tables.
**Ctrl+L** takes any feed URL, or `feed:https://site/` to autodiscover.

## Web pages

Any other web link opens here too, as a reader view: the page's main
content mapped to sections, paragraphs, lists, quotes, code and tables —
no CSS, no scripts. Headings fill the outline, and the first line offers
**Open in browser** for pages that need the real thing. Try
[Hypertext on Wikipedia](https://en.wikipedia.org/wiki/Hypertext) or
[How to Do Great Work](https://www.paulgraham.com/greatwork.html).
Local `.html` files open the same way.

`wikipedia:Hypertext` (or `wikipedia:de:Hypertext` for other languages)
opens an article natively from the Wikipedia API: references intact, and
article links stay native. On a Wikipedia web page the **Native view**
button next to Reload switches to that form — try
[wikipedia:Project Xanadu](wikipedia:Project_Xanadu).

Documents in other formats — Word (.docx), OpenDocument (.odt), RTF,
reStructuredText, LaTeX, Typst, MediaWiki, Jupyter notebooks, CSV, … —
open read-only through **pandoc**.

## Nostr

Open [Recent Nostr notes](nostr:recent) or [Nostr discussions](nostr-sample.md).
**Ctrl+L** accepts `nostr:recent`, `note1…`, or `nevent1…`.

## Living relationships

Try an internal jump: [Quick start](#Quick start).

External web links open in your system browser: [Project Xanadu](https://en.wikipedia.org/wiki/Project_Xanadu).

With Gemini enabled, type a capsule in the location bar (**Ctrl+L**), for example:

`gemini://geminiprotocol.net/`

A live embedding of another passage on this page:

:::include #Quick start

## Design pillars

- Addressable passages and stable node IDs
- Stretchtext that reflows the living layout
- Side-by-side views of related material
- Direct manipulation of images and blocks
- Session history and bookmarks for local files and capsules

> “A document is not just something you render and read. It is a live workspace.” — the xanatoo proposal

---

Open **View → Examples** any time, or return here by opening `welcome.md` from the Examples menu.
