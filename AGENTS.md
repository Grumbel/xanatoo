# xanatoo — Agent Notes

## Project goal

Interactive document composition engine (C++ / SDL2).
See `PROPOSAL.md` for the full vision (stretchtext, live editing,
addressable passages, Hypercard/Xanadu/Canon-Cat spirit).

Keep the system **minimalist and portable**. Prefer small, well-scoped
libraries (SDL2, FreeType, FontConfig, optionally HarfBuzz).
Dear ImGui is allowed for chrome (menus/toolbars). No Qt/GTK/KDE.

## Architecture principles

- Data-oriented / ECS-friendly: stable IDs for content, separate layout
  geometry from document identity, systems that transform data.
- Content model is the source of truth; layout and rendering are derived.
- Immediate visual feedback after every edit.
- Local-only for now; network is a later optional layer.

## Source layout (app = world + systems)

`main()` only creates the SDL window/renderer and loops `run_frame`.
All state is the plain-data `xanatoo::app::App` (`include/xanatoo/app.hpp`);
behaviour is free functions ("systems") taking `App&`:

| File | System |
|------|--------|
| `app_init.cpp` | build initial world, shutdown |
| `app_events.cpp` | `handle_event` → one `on_<sdl_event>` per event type |
| `app_frame.cpp` | frame stages: sync size, relayout, `run_chrome` (UiState + `execute_command`), draw panes, overlays |
| `app_chrome.cpp` | status line (`compute_status` is pure data → `draw_status_bar`), floating window frame, client scrollbar |
| `app_windows.cpp` | floating windows (`FloatWindow`, role = Help/Outline/Backlinks/Document): one z-stack, shared hit-test / drag / resize / scroll / draw |
| `app_softwin.cpp` | window placement: `Dock` (None / Left / Right / Max) for every role — `dock_window` / `undock_window` / `toggle_maximize`, `place_windows` derives docked geometry, `dock_column_w` is what `layout_panes` leaves out |
| `app_touch.cpp` | touchscreen gestures: tap → click, drag/fling scroll, long-press select, pinch zoom |
| `app_buffers.cpp` / `app_view.cpp` / `app_nav.cpp` / `app_softwin.cpp` / `app_prompts.cpp` / `app_pointer.cpp` | document buffers, scroll/fold/outline, URL history + bookmarks, floating windows, minibuffer, cursor/gamepad |
| `app_fetch.cpp` | background page fetches (`FetchJob` on a worker thread → `poll_fetches`), page cache, loading indicator |
| `app_images.cpp` | remote (http/https) images: worker downloads of what `ImageCache` queued → `cache_dir()/images/`, relayout on completion and on finished thumbnails (`poll_images`); cache pruning at startup |
| `images_thumtoo.cpp` | `image_backend.hpp` over vendored thumtoo (`XANATOO_THUMTOO`): libvips decode, thumbnails composed from the shared tile cache |
| `app_collection.cpp` | collection view state (layout / size / sort), card clicks, fullscreen `Lightbox` viewer |
| `app_drafts.cpp` | unsaved edits → drafts in the revision store (`revstore.hpp`): autosave, restore on load (`attach_draft`), save / revert, `drafts:` page |
| `app_about.cpp` | `about:` page (Help → About): services, document formats, images (thumtoo / libvips loaders, MuPDF / DjVu) and libraries of this build, gathered from the feature switches and runtime versions; `about_markdown` (`about.hpp`) formats it. New services and user-visible optional deps get an entry there |
| `app_footnotes.cpp` | footnote overlay: notes of on-screen `FootnoteRef` marks laid out in a strip at the pane bottom (`FootnoteOverlay` per pane), click jumps to the note |
| `app_sitemap.cpp` | Sitemap panel (`WindowRole::Sitemap`, `SitemapPanel`): site tree of the focused page's host, one `SiteCache` per host loaded on a worker thread; "Page" opens `sitemap:host` |
| `app_split.cpp` | split browsing: left links open right, right links shift left, hover previews (`SplitPreview`, only with `ViewSettings::hover_preview`, off by default) |
| `theme.cpp` | classic palette (`Palette`) + stateless bevel/gradient/window primitives shared with the ImGui theme |
| `style.cpp` | document style sheets (`styles/*.xss`, S-expressions): `DocStyle` roles → layout spacing/fonts/glyph colours + renderer decoration colours; `apply_style` in `app_view.cpp` |

Document views: `App::panes[kMaxViews]` — slots 0 / 1 are the centre
(split), document windows own a slot from `kCentreViews` up
(`FloatWindow::view`, `alloc_view` / `release_view` / `close_doc_window`).
Every view has its own rect (`Pane::x/y/w/h`, centre rows from
`place_centre_rows`, windows from `place_windows`), so the whole pane path
(layout, raster, caret, editing, overlays, footnotes) serves windows too.
`app.focus` may be any live slot; tabs act on `app.centre_focus`. Use
`focus_view`, `live_views` (never `pi < 2`), `other_pane` (never
`1 - app.focus`), `pane_at(app, x, y)` and `content_y(app, y, pi)`;
after replacing a buffer's document call `refresh_views_of_buf`.
Back / Forward are per view (`Pane::nav`): record a shown page with
`push_nav_history(app, pi, url)`, step with `nav_step`; `App::visited`
is only the session log for the History panel. While split browsing, the
centre panes share one trail instead (`App::split_history`, levels = left
page + pages opened from it on the right): `push_nav_history` forwards to
it and `nav_step` / `can_nav_step` walk it (`app_split.cpp`).

Buffers are addressed by stable `BufferId` (`DocBuffer::id`, never reused,
`kNoBuffer` = 0): use `buf(app, id)` / `find_buf` / `add_buffer` /
`remove_buffer` / `close_tab` (`app_buffers.cpp`); never index
`app.buffers` directly and never hold a `DocBuffer&` across add/remove.

Status-bar prompts are one `Prompt` record (`App::prompt`: `PromptKind`,
`text`, `text2` for Replace, `node`/`span` for Edit*): open with
`open_prompt` / `open_search`, close with `close_prompt`, Enter dispatches
through `commit_prompt` (`app_prompts.cpp`). At most one prompt is active;
path/URL kinds edit through `App::minibuf` (`prompt_uses_minibuf`), and
`last_search`/`last_replace` keep a displaced find for F3 / Replace All.

Unsaved edits are never discarded behind a confirmation: before a buffer
is replaced or dropped call `stash_draft` / `stash_or_confirm` (the latter
only asks when no store is available). After putting a local file into a
buffer call `attach_draft` (`add_buffer` / `show_doc_in_pane` do); after
writing it, `note_saved`. Mark edits with `mark_dirty` / `note_edit`, not
`dirty = true`. Design and roadmap: `docs/REVISIONS.md`.

Keyboard focus: `App::key_focus` (`KeyFocus::Document/Outline/Backlinks`).
Panels handle plain keys only while `panel_has_keys(app, …)`; opening or
clicking a panel focuses it, a pane click / jump / unused plain key returns
focus to the document. Never let a panel consume keys just because it is open.

Windows place one way regardless of role: Left / Right docks are side
columns that take width from the panes (stacked by `dock_seq`, shared
`App::dock_w`), Max overlays the client area. Never add a per-panel
"docked" flag; set `FloatWindow::dock` through the helpers above.
See `docs/BRAINSTORM.md` "Views, arrangement, documents" for where tabs /
split / windows are heading.

Rules: no behaviour on data types; new modal state goes in `App` (group
related fields into small records like `FloatPanel` / `Hover`); menus,
toolbar and shortcuts should route through `execute_command`.

## Tech stack (current)

- C++20
- SDL2 (window, events, renderer)
- FreeType (glyph rasterization)
- FontConfig (styled faces)
- Dear ImGui (menu bar / toolbar; vendored under external/imgui)
- CMake + Nix flake

## Shared network helpers

- `include/xanatoo/tls.hpp` — `TlsStream` (connect over a socket fd with SNI, write_all / read / pending; owns the fd). Backends `src/tls_openssl.cpp` (desktop default) and `src/tls_mbedtls.cpp` (Android; `-DXANATOO_TLS=mbedtls`). Never call OpenSSL / mbedTLS directly elsewhere.
- `include/xanatoo/https.hpp` + `src/https.cpp` — HTTPS GET/POST over `TlsStream` (chunked bodies decoded; `https_get_follow` follows redirects) plus `tcp_connect_host` for Gemini / Nostr (built when any network feature is ON).
- `include/xanatoo/json_min.hpp` — tiny JSON field/object helpers for read-only API clients (header-only). The `*_field` lookups are flat (first match anywhere): use `json_top_level(obj)` when nested objects repeat a key.

## Style sheets

- `styles/*.xss` — S-expression sheets kept apart from the markup; they style
  semantic roles (no selectors). See `docs/STYLESHEETS.md`.
- Every sheet starts from `builtin_style()`; `styles/default.xss` mirrors it
  (a unit test keeps them equal). `(inherit "x")` + late-bound `(define …)`.
- Never hard-code document colours/spacing in layout/render; add a role or
  property to `DocStyle` instead. UI chrome keeps using `theme.hpp`.
- Canvas chrome text (status line, window titles, outline, backlinks,
  tooltips) uses `app.ui_font` / `app.ui_font_bold` (`ui_font_path()`, the
  ImGui face at `kUiFontPx`), never `app.fonts` — those follow the style
  sheet and zoom. Cut long labels with `fit_text`.
- Sheets may name faces with poor coverage (Verdana has no arrows):
  `Font::glyph` draws codepoints the face lacks from fallback faces
  (`fallback_font_paths`: FontConfig coverage sort, fixed list on Android);
  line metrics stay the primary face's.
- Selected sheet = `ViewSettings::style` (view.conf); toolbar style button and
  View → Style sheet issue `UiCommand::SelectStyle` / `ReloadStyles`.

## Shared S-expression reader (`sexpr.hpp`)

- Header-only `SExprParser` (`parse_top` / `parse_all`) + `is_atom` /
  `is_form` / `sexpr_*` helpers; used by the `.sexp` format and style sheets.

## Shared string helpers (`util.hpp`)

- `starts_with` / `ends_with` / `starts_with_ci`
- `ascii_lower` (string or string_view)
- `trim_ascii` for string_view edges
- `html_decode_entities` (named basics + numeric → UTF-8)

## Coding rules

- GPLv3+ with short SPDX headers on every source file.
- Author: Ingo Ruhnke <grumbel@gmail.com>
- Commit directly to git; add a `Co-authored-by:` trailer for the AI agent that did the work.
- Prefer clear data layouts over clever abstractions.
- No drive-by feature removal; discuss first.

## Tests

- `tests/unit_tests.cpp` — offline unit tests (string helpers, json_min, URL detection for Reddit/Lemmy/Gemini/Nostr).
- Build target: `xanatoo_tests` (`ctest` or run the binary).
- Does not exercise live network fetches.

## Android

- `nix build .#xanatoo-android` (ndk-build pipeline from Kurvenrausch;
  `nix/android.nix`, `mk/android/`). See `docs/ANDROID.md`.
- Built with `XANATOO_FONTCONFIG=0`, SQLite (amalgamation) and the network
  clients on Mbed TLS; keep code compiling with `-DXANATOO_FONTCONFIG=OFF`
  and `-DXANATOO_TLS=mbedtls` (desktop CMake accepts both). New `src/*.cpp`
  files are picked up by `mk/android/app/jni/Android.mk` automatically;
  OpenSSL-only files must be excluded in `build-apk.sh` like
  `tls_openssl.cpp`.
- Images: thumtoo is an ndk-build module (`mk/android/thumtoo/Android.mk`,
  sources from `external/thumtoo`, no MuPDF / DjVu / unarr / curl) over
  prebuilt C libraries (libvips, glib, libarchive, codecs) that
  `nix/android-imagelibs.nix` cross-builds per ABI with nixpkgs' Android
  set (NDK 27, plain C, so they link into the NDK 29 build) and merges into
  one `libimagedeps.a`; `nix build .#android-imagelibs`.
- Test on the device: `adb logcat -s xanatoo` shows std::cout / std::cerr.

## Develop shell (`nix develop`)

- PATH scripts: `xanatoo-configure`, `xanatoo-build`, `xanatoo-run`, `xanatoo-run-gdb`, `xanatoo-test`.
- Requires `XANATOO_SOURCE` (set in shellHook). Default build dir `/tmp/xanatoo-build`.
- See `docs/ENVIRONMENT.md`.

## Layout virtualization

- `LayoutEngine::layout` accepts optional `LayoutViewport` + height-hint map.
- Sequential containers (Document, Section, Article, Thread, List, Column,
  DefList, Biblio) skip off-screen children when a prior box height is known.
- Main keeps per-pane `height_cache`
- Preferences (`ViewSettings` / `view.conf`): line spacing, margins, measure, plus behaviour switches (footnote overlay, justify, hover preview) shown in Edit → Preferences (ImGui window, `UiCommand::TogglePreferences`); never document format
- Section Focus mode: `LayoutEngine::layout(..., focus_section)` filters body to one Section/Article (outline still full tree); scroll outside the materialised band
  triggers re-layout. Width changes clear the cache.
- History still snapshots whole documents (depth capped for large trees).

## Section tree (structure)

- Org stars and Markdown ATX/setext always normalize into nested `Section`
  nodes with a `Header` title child. Body blocks are children of the section.
- `Article` is never synthesised automatically.
- Soft interop: `Section.level` / `Header.level` may still hold depth for export.
- View fold state will live outside the document (future SQLite); do not rely
  on dirtying `.md`/`.org` for collapse alone.
- Structure highlight shows `headerN` for header depth.
- View-local fold: `folded_sections` set passed into layout; Section/
  Article show header only when folded. Outline twisties / ←→ / Ctrl+Shift+F.

## Continuity

Read `TODO.md` first. Record plans and outcomes there.
Longer design notes: `docs/BRAINSTORM.md`.
Work is committed directly to git (bundles were only a Grok Web handover
mechanism; historical bundle numbers in TODO.md are archival).

## Hacker News

- Read-only (`src/hackernews.cpp`), gated by `XANATOO_HN` (default ON).
- Lists (`/`, `/news`, `/newest`, `/best`, `/ask`, `/show`, `?p=N`): ranked
  ids from the Firebase API + one Algolia search for titles / scores.
- Items (`/item?id=N`): whole tree in one Algolia `/items/N` request; top
  level re-ordered by the Firebase item's `kids` (HN rank).
- `hn_append_html` maps comment HTML → Paragraph / BlockQuote (`> …`) /
  CodeBlock with Link / Emphasis spans.

## Feeds (RSS / Atom)

- `src/feed.cpp`, gated by `XANATOO_FEEDS` (default ON). Small tolerant XML
  reader (names lower-cased, prefixes kept: `content:encoded`), RSS 2.0,
  RSS 1.0 (RDF) and Atom (text / html / xhtml content) → Thread + Message.
- `is_feed_url`: `feed:` prefix (HTML pages autodiscover via
  `<link rel=alternate>`), *.rss / .atom / .rdf / .xml, /feed, /rss,
  /feeds/…, ?feed=rss2. Checked before the other services in dispatch.
- Entry HTML goes through `html_min` (below).

## HTML fragments (`html_min.hpp`)

- `html_append_blocks`: tag soup → Paragraph / Heading / List / ListItem /
  BlockQuote / CodeBlock / Table / HorizontalRule with Link / Emphasis /
  Strong / Code / Strike spans; script / style / iframe dropped, unknown
  wrappers unwrapped. BlockQuote, ListItem and TableCell are leaves, so
  nested blocks inside them flatten to line breaks / "• " lines.
- Images become `[image: alt]` links (no remote image fetch yet).
- Used by feeds and Hacker News (`gt_quotes` for HN's "> " convention)
  and web pages (page-mode options below); also `html_to_plain`,
  `html_attr`, `resolve_url`, `html_element_end`.

## Web pages (`html_page.hpp`)

- `src/html_page.cpp`, gated by `XANATOO_WEB` (default ON). Any http(s)
  URL that no other service claims and that `is_web_url` accepts (not an
  obvious download) opens in-app; it is checked last in dispatch.
- `parse_html_page`: main content (`<main>`, lone `<article>`, `<body>`)
  through `html_min` page mode (`sections`, `anchors`, `drop_chrome`);
  also used for local `.html` files (`parse_document` dispatch). HTML is
  read-only: Save asks for a `.md` name.
- `web_fetch` dispatches on Content-Type; bodies go through
  `detect_charset` / `to_utf8` (util.hpp) first.
- `external:<url>` hrefs force the system browser (`open_external_url`).
- Foreign content is read-only: `is_foreign_path` (remote URL, `.html`,
  any `is_pandoc_path` file, anything under `cache_dir()`) makes `browsing(app)` true.
  Gate document edits on `browsing(app)`, never on `app.browse_mode`
  alone (that is only the user's global toggle).

## Collections

- `NodeKind::Collection` (`:::collection view=grid|list|full` or pandoc
  `::: collection`; `.sexp` `(collection :view "grid" …)`): ordered Image /
  Figure items rendered as cards. `summary` = default view hint only.
- Layout / sort / card size are view state (`App::collection_views`,
  `CollectionView`, passed via `LayoutEngine::set_collection_views`), never
  written to the document. `collection_items` gives items in view order.
- Card images are `PositionedImage` with `thumb > 0` (+ `group`): drawn
  from `ImageCache::thumb` (box-filtered, cached as BMP under
  `cache_dir()/thumbs/`, keyed by path + size + mtime); no resize handle.
- `app_collection.cpp`: Collection menus (`UiCommand::CollectionView` +
  arg, context menu and View menu), click on a card → `Lightbox` (full-
  window viewer drawn after ImGui; `lightbox_event` takes all input first).

## Site maps (`sitemap.hpp`)

- `src/sitemap.cpp`: XML sitemap scanner (`parse_sitemap_xml`, urlset +
  sitemapindex), `robots_sitemaps`, `build_site_tree` (flat URLs → path
  tree in pre-order, page-less one-child chains folded to "a/b") and
  `site_tree_document`; parsing / tree are always built and unit-tested,
  the fetch (`sitemap_fetch_tree`: robots.txt, else /sitemap.xml,
  /sitemap_index.xml, /wp-sitemap.xml; ≤ 25 files, ≤ 50k URLs; `.gz`
  through `inflate.hpp`) needs `XANATOO_WEB`.
- `sitemap:host` is a read-only page (one Section per directory, so the
  outline is the site structure). The panel is a tool view of the focused
  pane, not a document; its keys run before the browse-mode key filter.
- Shown pages land in `SitemapPanel::seen` (`note_page_seen`, called by
  `show_doc_in_pane`): titles and per-node coverage (`SiteCache::
  seen_count`, recomputed on `seen_gen`). `sitemap_step` = Alt+PgUp/PgDn.

## Wikipedia (`wikipedia.hpp`)

- `src/wikipedia.cpp`, gated by `XANATOO_WIKIPEDIA` (default ON).
  `wikipedia:Title` / `wikipedia:de:Title#frag` fetch the REST Parsoid
  HTML (`/api/rest_v1/page/html/…`, descriptive User-Agent required) and
  map it with `parse_html_page`; `wikipedia_rewrite_links` turns article
  links into `wikipedia:` refs (non-article namespaces stay web links).
- `https://*.wikipedia.org/wiki/…` stays an ordinary web page; the
  explicit "Native view" toggle / `UiCommand::ToggleNative` switches via
  `alternate_view_url` (`native.hpp`) — add new native mappings there,
  never reroute silently.

## Service refs (`native.hpp`)

- `hn:` / `reddit:` / `lemmy:` refs select the native Thread + Message
  clients; the part after the prefix mirrors the web path (`hn:item?id=1`,
  `reddit:r/linux/`, `lemmy:lemmy.world/c/technology`). `web_url_for`
  maps a ref back to https for the API clients, which still take web URLs.
- Plain https URLs of those sites open as web pages. Links inside native
  views stay native: `native_rewrite_links` (HN, Reddit) or refs emitted by
  `lemmy.cpp` (its path test is too loose for arbitrary links).

## Pandoc import (`epub_import.hpp`)

- `kPandocFormats` maps extensions to pandoc readers (epub, docx, odt, rst,
  tex, typ, wiki, ipynb, csv, …); `is_pandoc_path` / `import_via_pandoc`
  convert to cached GFM Markdown with `--extract-media`. The original file
  is never touched and the import is read-only (foreign).
- Pandoc misbehaviour and our workarounds (Lua filter, post-passes) are
  collected in `docs/PANDOC.md`; add new findings there.

## Ebooks

- `src/ebooks.cpp`, gated by `XANATOO_EBOOKS` (default ON).
- Standard Ebooks catalogue pages (`/ebooks`, `?query=`, author pages,
  `/subjects/x`) are scraped into a Markdown book list.
- Book URLs (Standard Ebooks book pages, Gutenberg `/ebooks/N`, any `.epub`
  link) download into `~/.cache/xanatoo/downloads/` and open through the
  pandoc EPUB import; the pane path is the derived `content.md` so images
  resolve. Never previewed on hover.
- `examples/books.md` is the curated entry page.

## Nostr

- WSS over `TlsStream` (`tls.hpp`).
- Read-only note/thread fetch gated by `XANATOO_NOSTR` (default ON; needs OpenSSL).
- Opens `note1` / `nevent1` / `nostr:…` / 64-hex via Ctrl+L → Thread+Message layout.
- `npub1` / `nprofile1` open the author's profile + recent notes. Message
  headers link the author (`nostr:npub1…`) and date (`nostr:note1…`) via
  Link spans on the Message node (`[0]` author, `[1]` date; see `types.hpp`).
- Note text goes through `nostr_append_content`: URLs / `nostr:` refs /
  NIP-08 `#[n]` → links (mentions as `@name`), media labelled `[image]` /
  `[video]` / `[audio]` (NIP-92 `imeta` type, else extension), images also
  as remote Image nodes.

## Remote images

- An Image whose `src` is http(s) is remote: `ImageCache` never fetches,
  it queues the URL (`take_requests`) and returns a `pending` placeholder;
  `poll_images` (app_images.cpp) downloads it and calls `remote_done`.
- Layout draws remote images from their `kRemoteThumbEdge` thumbnail
  (`layout_remote_image`); a click opens the lightbox over all document
  images.
- No posting or key management in v1.

## Images through thumtoo (`image_backend.hpp`)

- `external/thumtoo` is a git subtree of `~/projects/thumtoo/thumtoo.git`
  (`git subtree pull --prefix external/thumtoo … --squash`); change thumtoo
  upstream first, then pull.
- `XANATOO_THUMTOO` (default ON on desktop): `ImageCache` decodes through
  libvips (`image_backend_decode`, every format vips has) and thumbnails
  asynchronously: `thumb()` returns a pending image and queues the request,
  `ImageCache::poll()` (from `poll_images`) uploads finished ones. Thumbnails
  are durable thumtoo tiles (JPEG, WebP with alpha) of the coarse scales
  only, composed by `get_pixels_from_tiles`; the cache is
  `$XDG_CACHE_HOME/thumtoo`, shared with galapix / biltoo.
- Without it (unit tests) stb_image decodes (PNG / JPEG / GIF / BMP) and
  thumbnails are BMPs under `cache_dir()/thumbs/`.

## Gemini

- Gemtext parser is always built (`.gmi` / `.gemini`).
- Network client is gated by `XANATOO_GEMINI` (default ON; needs OpenSSL).
- URL entry is Ctrl+L / `UiCommand::OpenUrlBar` → `navigate_to_url`.

## Reddit

- Read-only client via public `https://www.reddit.com/…/.json` (OpenSSL HTTPS).
- Gated by `XANATOO_REDDIT` (default ON; needs OpenSSL).
- `reddit:r/<sub>/…` refs (the reddit.com path); https links open as web pages.
- Maps posts/comments to Thread + Message nodes.
- Optional OAuth: `XANATOO_REDDIT_CLIENT_ID` + `XANATOO_REDDIT_CLIENT_SECRET` (script app) when anonymous `.json` returns 403.
- Optional UA override: `XANATOO_REDDIT_USER_AGENT`.

## Lemmy

- Read-only client via Lemmy **API v3** JSON (`/api/v3/post/list`, `/api/v3/post`).
- Gated by `XANATOO_LEMMY` (default ON; needs OpenSSL).
- `lemmy:<instance>/c/<community>` and `lemmy:<instance>/post/<id>` refs.
- Maps posts/comments to Thread + Message nodes.
- Optional UA override: `XANATOO_LEMMY_USER_AGENT`.
