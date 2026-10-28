# TODO — xanatoo

> **Rename (2026-10-10):** project moved from `glyphtoo` to **xanatoo** (`https://github.com/Grumbel/xanatoo.git`). Namespace, includes, binary, env vars (`XANATOO_FONT`), and docs updated. Historical bundle names below still use the `glyphtoo-NNN` prefix from before the rename.

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **263** (this delivery is **262**)

## Status (2026-10-28) — drafts instead of "discard?" prompts

- Unsaved edits go to a local revision store
  (`~/.config/xanatoo/revisions.sqlite`, `revstore.hpp`): content-addressed
  objects (SHA-256), immutable revision records with parents, per-document
  heads. Autosave 2 s after an edit and whenever a buffer is left (navigate,
  close, New, Open, Quit, window close); no more "save first, or repeat to
  discard" (kept only for builds without SQLite).
- Reopening a file restores its draft (`*` tab + notice; warns when the
  file changed on disk since). Ctrl+S records a save revision; File →
  Revert to Saved drops the draft; File → Drafts (`drafts:`) lists all,
  untitled ones as `draft:doc:<hex>` (random 256-bit doc keys).
- Roadmap (revision browser, doc ids in files, overlays for remote pages,
  persistent text units, transclusion by revision, signatures, exchange):
  `docs/REVISIONS.md`.

## Status (2026-10-27) — margins in percent, text width in characters

- View → Margins: 2 / 5 / 10 / 15 / 20 / 25 / 30 % of the pane width per
  side (`ViewSettings::margin_pct`, floor `kMinMarginPx`), replacing the
  12…80 px steps that were barely visible on wide windows.
- View → Text width (was Measure): Full / 60 / 72 / 90 / 120 / 160 / 200 /
  300 characters
  (`measure_chars`), measured with the body font's average lowercase
  advance, so it follows zoom and the style sheet (the old 720 / 960 px cap
  shrank to ~45 chars at 150 % zoom). Column = min(pane × (1 − 2p), N ch).
- `UiCommand::ViewMargins` / `ViewMeasure` take the value as arg; old
  `margin_x` / `max_content_width` lines in view.conf migrate to the
  nearest step.

## Status (2026-10-27) — outline title row, Markdown front matter

- Outline: content before the first heading (cover image, byline, web
  page source line) gets a leading row labelled with the page title
  (`doc.meta.title`, else the tab label); it jumps to the very top.
  `OutlineEntry::preamble`, `collect_outline(…, fallback_title)`.
- Markdown YAML front matter (`---` … `---`): title / author / lang into
  `DocumentMeta`, block kept verbatim (`front_matter`) for round-trip; a
  leading `---` rule is not mistaken for it (line 2 must be `key:`).
- Pandoc imports run with `-s` + `yaml_metadata_block`, so EPUBs keep
  their title; `.format` stamp in the import dir forces a re-import of
  older caches. Web pages set `meta.title` from `<title>`.
- Menus: Open in Window under File; Examples only under Help (grouped,
  incl. Collections).

## Status (2026-10-27) — Services menu, split / outline buttons

- Services menu (between Bookmarks and Help): front pages of the in-app
  clients (wikipedia:Main_Page, de Hauptseite, HN, r/popular,
  lemmy.world/c/technology, nostr:recent, geminiprotocol.net, Standard
  Ebooks, books.md, feeds.md) + Open URL. List built in
  `build_ui_state` (`UiState::services`); entries whose client is not
  compiled in are greyed out with the reason as tooltip.
- Split pane and Heading outline moved from the "View tools" dropdown to
  toggle buttons next to Find (highlighted while on); View menu unchanged.

## Status (2026-10-27) — collections (prototype, phase 1 + 2)

- `Collection` node (docs/BRAINSTORM.md §4): `:::collection` /
  `::: collection` with Image / Figure items, `view=` default hint; md /
  sexp / org round-trip. Grid (centred captions), List, Full layouts;
  card size S / M / L; sort document / name / name reversed — all
  view-local (`App::collection_views`) via right-click → Collection or
  View → Collection. Click a card → fullscreen viewer (←/→, wheel,
  click halves, F window fullscreen, Esc). Thumbnails generated and
  cached under `cache_dir()/thumbs/` (BMP, box filter).
- Parser fix: heading sections are restored after a `:::` fence closes,
  so "## B" after a fenced block nests under "# Top" again.
- Example: `examples/collection.md` (+ generated images).
- Open: thumbnails are built synchronously on first layout (slow for big
  folders — move to a worker); view state is keyed by NodeId across
  documents like folded_sections; derived sources (directory / glob →
  directory browsing), document / passage cards, remote images, card
  frames / selection highlight via the style sheet.

## Status (2026-10-27) — web pages as reader views, wikipedia:, pandoc formats

- `html_page.cpp` (`XANATOO_WEB`): any other http(s) link opens in-app.
  Main content only (`<main>` / role=main, a lone `<article>`, else
  `<body>`); og:title / `<title>` as the top Section unless the content
  has an `<h1>`; headings → nested Section + Header (outline, folding);
  element ids → labels so `#fragment` / footnote links jump; first line
  "host · Open in browser" (`external:` prefix → system browser).
  Content-Type dispatch: HTML, feeds, text/plain (blank-line chunks as
  code blocks), text/markdown, text/gemini; other types error out with an
  "Open in browser" link. Obvious downloads (.pdf, .zip, images, media)
  still go straight to the system browser (`is_web_url`).
- `html_min` page mode (`sections` / `anchors` / `drop_chrome`): drops
  nav / aside / form / footer / dialog, hidden + aria-hidden + sr-only,
  ARIA nav roles, dropdown / toolbar / navbar / breadcrumb / cookie
  classes, alt="" and 1×1 images; layout tables (nested or single-row,
  role=presentation) unwrap; `<br><br>` splits paragraphs. Script bodies
  are skipped raw; unquoted `href=x/` is no longer self-closing.
- Local `.html` / `.htm` files open through the same mapper; Save on an
  HTML file or URL asks for a `.md` name instead of overwriting.
- Charsets: `detect_charset` + `to_utf8` (Latin-1 / Windows-1252 →
  UTF-8, valid UTF-8 kept whatever the label), used for pages and feeds;
  `&#150;`-style references decode as Windows-1252.
- Checked: Wikipedia (sections, infobox table, cite_note jumps, link
  navigation), jvns.ca, paulgraham.com (layout tables), gnu.org, lwn.net,
  danluu.com, blog.rust-lang.org, an RFC .txt.
- Foreign content is read-only (`is_foreign_path` / `browsing(app)`):
  remote URLs, `.html` / `.epub` paths and everything under `cache_dir()`
  are always browsed; F2 refuses with a notice; Ctrl+S / Ctrl+C / Ctrl+A
  work in browse mode; Ctrl+S saves an editable `.md` copy. Prompts keep
  their keys in browse mode (Enter used to activate the caret link).
  Long-term: automatic versioned saving into the database, clone /
  overlay for foreign content (docs/BRAINSTORM.md §8).
- `wikipedia:Title` / `wikipedia:de:Title` (`wikipedia.cpp`,
  `XANATOO_WIKIPEDIA`): REST `/api/rest_v1/page/html/<Title>` (Parsoid,
  templates expanded, references intact) through `parse_html_page`;
  article links rewritten to `wikipedia:` refs, other namespaces (File:,
  Category:, Talk, …) stay web links; redirects canonicalised from
  `dc:isVersionOf`. Raw wikitext + `pandoc -f mediawiki` was rejected: it
  drops every template ({{cite}}, infoboxes).
- Native view (toolbar button next to Reload, View → Native View,
  `UiCommand::OpenNative`): explicit jump from a web page to its native
  form (`native_url_for`; only Wikipedia articles so far). Plain
  https://*.wikipedia.org links are never rerouted automatically.
- Pandoc import generalised (`import_via_pandoc`, `pandoc_input_format`):
  epub, docx, odt, rtf, rst, LaTeX, typst, textile, MediaWiki, DokuWiki,
  DocBook, FB2, ipynb, OPML, man, POD, t2t, Muse, Creole, Djot, CSV/TSV,
  BibTeX, RIS, Jira → cached Markdown (`pandoc-import/`; EPUB keeps
  `epub-import/`), read-only like EPUB.
- Open: native view drops the `#fragment` of the web page (web pages
  store the URL without it).
- Service refs `hn:`, `reddit:`, `lemmy:` (`native.hpp`): HN, Reddit and
  Lemmy no longer hijack their https URLs — those open as web pages, the
  refs open the native Thread + Message views. Native view became a toggle
  (`UiCommand::ToggleNative`, `alternate_view_url`; pressed while native,
  View → Native View / Web Page View). Links in native views stay native.
  Services menu, welcome / books pages use the refs.
- Open: scheme-less `reddit.com/r/x` typed into Ctrl+L no longer opens
  (was the Reddit client); HN comment links to other sites' threads stay
  web links.
- Open: remote images; JavaScript-only pages show "No readable content";
  tab label is the URL, not the page title;
  other code pages (Shift_JIS, KOI8-R) are not converted.

## Status (2026-10-27) — RSS / Atom feeds, shared HTML mapper

- `html_min` (new): forgiving HTML fragment → document blocks (headings,
  nested lists, quotes, pre, tables, hr; links / em / strong / code / del;
  br as line break). Hacker News comments now use it.
- `feed.cpp` (`XANATOO_FEEDS`): RSS 2.0 / 1.0 / Atom → Thread + Message
  per entry (author, date normalised to "YYYY-MM-DD HH:MM UTC", linked
  title, content:encoded > description / content > summary, YouTube
  media:description, enclosures, "Permalink" for untitled entries).
  `feed:` prefix + HTML autodiscovery. `examples/feeds.md` lists ~30 feeds
  (all checked to parse: no leftover markup / entities).
- `html_decode_entities` knows Latin-1, Greek and common punctuation names.
- Open: remote images (would need fetching into the image cache — the
  main gap for comics / photo feeds); http-only feeds fail (https_request is TLS only); feed
  headings count in the outline / heading total; no read/unread state or
  subscriptions list yet (feeds.md is the subscription list for now).

## Status (2026-10-27) — Hacker News, ebooks

- Hacker News client (`hackernews.cpp`, `XANATOO_HN`): Top / New / Best /
  Ask / Show lists (Firebase ranked ids + one Algolia search, 30 per page,
  "More →") and item pages (whole tree from Algolia `/items/N`, top level
  in HN rank order via Firebase `kids`). Comment HTML → Paragraph,
  BlockQuote for `> …` lines, CodeBlock for `<pre>`, links / italics.
- Ebooks (`ebooks.cpp`, `XANATOO_EBOOKS`): Standard Ebooks catalogue,
  search, author and subject pages scraped into a list; book pages,
  Gutenberg `/ebooks/N` and any `.epub` URL download to
  `~/.cache/xanatoo/downloads/` and import through pandoc on the fetch
  thread. `examples/books.md` is a curated list (linked from welcome).
- EPUB import now runs pandoc with `-t gfm-raw_html`: div / section /
  attribute residue is unwrapped instead of showing as text. Existing
  import caches are reused until the EPUB is newer.
- Shared fixes: `https_request` decodes chunked bodies (was passing the
  chunk headers through), `https_get_follow`; `json_unescape` decodes
  `\uXXXX` (+ surrogate pairs) to UTF-8 instead of `?`; `json_top_level`;
  `html_decode_entities` in util.
- Open: book panes show the cache `content.md` path in the URL bar, not
  the remote URL; no download progress (large EPUBs just show the loading
  pill); HN user pages / deep comment pagination ("more" links) are not
  handled; Archive.org not done (its public-domain items mostly lack EPUBs
  or are lending-only — Gutenberg covers the same texts); SE OPDS / full
  Atom feeds need a Patrons Circle login, so the catalogue is scraped HTML.

## Status (2026-10-27) — comment thread look

- Message headers are coloured by new style roles: `message-author` (blue,
  bold), `message-op` (orange, replies by the thread's first poster),
  `message-meta` (grey date / score / reply-to). "reply to #t1_abc" now
  names the parent's author when the parent is in the document.
- Each message is a card (`LayoutResult::message_cards`): `message`
  background at even reply depth, `message-alt` at odd depth (old.reddit
  zebra nesting), drawn under images / text. Box height still includes the
  trailing gap (virtualization hint).
- Sheets: default / night / sepia-book / high-contrast / typewriter set the
  new roles; docs/STYLESHEETS.md lists them.
- Open: relative times ("3 h ago") would need layout to depend on the clock;
  score is still part of the meta string rather than its own span.

## Status (2026-10-27) — site style sheets

- `styles/hackernews.xss`, `reddit.xss` (old.reddit), `wikipedia.xss`.
- Sheet extensions: `(fonts (body-scale x))`; `header` / `thread`
  `background` = band behind the title, `border` = rule under it
  (`LayoutResult::role_fills` / `role_rules`, `add_role_band`).
- Open: pick the sheet per site automatically (selection is global; split
  panes would need per-pane fonts); HN's grey title-row text and
  non-bold authors are not expressible yet.

## Status (2026-10-27) — Home button, directory listings

- Home (toolbar house, View → Home, Alt+Home, `UiCommand::GoHome`) opens
  `home_page(app)` = welcome.md; the hook for a user-chosen home page later.
- Opening a directory no longer aborts (ifstream on a dir threw): `load_doc`
  renders a read-only Markdown index (dirs first, absolute links, dot files
  hidden); `navigate_pane` normalises to a trailing '/'.
- Open: non-document files in a listing (images, …) still open as text.
- Up (toolbar ↑, View → Up, Alt+Up on read-only pages, `UiCommand::GoUp`):
  `up_url` (native.hpp, pure + unit-tested) — local dirs, URL path levels
  (http / gemini), reddit post → subreddit → front page, hn: → hn:,
  lemmy: → instance web page; wikipedia: / nostr: / feed: have no parent.
- `examples/footnotes.md` demos marks, notes and the overlay.

## Status (2026-10-27) — footnotes

- New `Footnote` block (label = key, Paragraph children) and `FootnoteRef`
  inline (href = key). Markdown `[^key]` / `[^key]: …` (multi-paragraph via
  4-space indent, pandoc GFM style) — a bare `[^key]:` line used to become a
  bibliography entry; it now only does inside `:::biblio` (which the writer
  always emits). Org `[fn:key]`, sexp `(fnref …)` / `(footnote :key …)`.
- Marks render as superscript digits (raised glyph when the face lacks
  them); `#fn:key` resolves to the note, so click / hover preview work.
- Footnote overlay (`app_footnotes.cpp`): notes of the marks on screen are
  laid out in a strip at the pane bottom (≤ 40 % height, fade when cut),
  skipped when the note itself is on screen or the caret is under the strip.
  Click jumps to the note. View → Footnote overlay (`footnotes=` in
  view.conf). Style roles `footnote` / `footnote-ref`.
- EPUB footnotes come through pandoc's GFM output. HTML from arbitrary sites
  is not covered (pandoc only recognises its own / EPUB note markup).
- Open: org inline definitions `[fn::…]`;
  overlay is not scrollable when notes exceed 40 %.

## Status (2026-10-27) — split browsing, background page loads

- Remote pages (Gemini / Nostr / Reddit / Lemmy) now fetch + parse on a
  detached worker thread (`app_fetch.cpp`: `FetchJob` / `FetchShared`,
  `poll_fetches` per frame). The UI stays live: indeterminate progress bar +
  "Loading host… 1.2 s" pill on the target pane, status line with timer,
  toolbar Reload turns into Stop; Esc / Stop cancels. Failures show in the
  status line (and as an error page when the pane only had a stand-in).
- Split view (not diff mode) is a two-page trail (`app_split.cpp`): left
  links open on the right; right links move the right page left and open the
  target on the right (the dropped left buffer is recycled, so tabs do not
  pile up). Hovering a left link previews it on the right after a short
  dwell (250 ms local / 450 ms remote), framed + labelled "Preview"; click
  keeps it, moving back into the left pane drops it, the pointer may rest
  over the right pane to read / scroll it. Ctrl+click keeps the old
  "anchor in other pane" behaviour.
- Small page cache (16 entries, 5 min) so a previewed remote page opens
  instantly on click.
- `link_base_url`: relative gemini links resolve against the pane's own
  page instead of the global `gemini_current_url`.
- Remote tabs are labelled host/.../last-segment instead of blank.
- Open: history is still one global list (Back in the left pane after a
  split navigation walks the shared list); per-pane history would fit the
  trail model better. Local loads (incl. EPUB import via pandoc) still run
  on the UI thread.

## Status (2026-10-27) — Android: data, keyboard, SQLite, TLS, density

- Tested on a Fire 7 (Fire OS 5 / Android 5.1.1) over adb.
- APK assets (examples, FORMAT.md, styles, icons, DejaVu fonts) unpacked to
  internal storage (`src/platform.cpp`); logcat for std::cout/cerr; Back =
  Esc; on-screen keyboard on caret tap / URL field, window resizes above it
  and the caret is revealed.
- SQLite amalgamation module; `TlsStream` (tls.hpp) with OpenSSL (desktop)
  and Mbed TLS (Android, `-DXANATOO_TLS=mbedtls`) backends — Gemini, Nostr,
  Reddit, Lemmy work on the device. mbedTLS entropy from /dev/urandom
  (getrandom blocked ~60 s on Fire OS 5).
- `--ui-scale` / Android density scales chrome, default text and px
  lengths.
- Open: certificate verification (all backends, pre-existing), rare
  SIGSEGV in SDL 2.30.3 RenderPresent during keyboard resize (seen once).

## Status (2026-10-27) — Android port (bare bones)

- `nix build .#xanatoo-android` builds a signed debug APK (API 21+,
  armeabi-v7a / arm64-v8a / x86_64) with the Kurvenrausch ndk-build
  pipeline; FreeType is built from nixpkgs' source as a static module.
- Off on Android: OpenSSL clients, FontConfig (`XANATOO_FONTCONFIG=0`,
  generic families → /system/fonts), SQLite (`XANATOO_SQLITE=0`), all data
  files. `HOME`/`TMPDIR` → app internal storage; links via `SDL_OpenURL`.
- Not yet run on a device or emulator. Next: package examples / styles /
  icons as assets, density-scaled chrome, soft keyboard handling, then
  SQLite and OpenSSL back in. See docs/ANDROID.md.

## Status (2026-10-26) — style sheets

- `styles/*.xss`: S-expression document style sheets (docs/STYLESHEETS.md),
  separate from markup. Roles (paragraph, header, note, code-block, link, …)
  set spacing (px/em), indent, font face, text/background/border colours;
  page paper/ink/selection; font families + heading scale/weight.
  `(inherit …)` with late-bound `(define …)` tokens.
- Shipped: Default (mirrors the built-in look), Compact, High Contrast,
  Night, Night Amber (token-only variant of Night), Sepia Book, Typewriter.
- Toolbar style dropdown + View → Style sheet; choice persisted as
  `style=` in view.conf; "Reload style sheets" re-reads files.
- Behaviour changes: block font faces now combine with span emphasis, so
  code blocks render monospaced (they passed FontRole::Code but got the body
  face) and captions italic. `XANATOO_FONT` now yields to a sheet's body
  family (`--font` still wins).
- SExpr reader moved from parse.cpp into header-only `sexpr.hpp`.
- Next: per-document sheets, note kinds as roles, "why did this value win"
  explanation, hot reload, glyph fallback for faces missing ☐/☑.

## Status (2026-10-26) — scroll blitting

- Each pane caches its document layer (paper, selection, text) in a
  `PaneRaster` (two render-target textures, ping-pong). A pure scroll shifts
  the old pixels and draws only the exposed strip; layout generation
  (`LayoutResult::generation`), selection, search tint or size changes redraw
  the whole raster. Overlays + scrollbar are still drawn every frame on top.
- Panes draw at a whole-pixel scroll (fraction kept in `scroll_y`);
  `render.cpp` floors y positions so a 1px scroll shifts everything by 1px.
- Verified pixel-identical against full redraws (software + GPU renderer).
- Falls back to direct drawing when render targets are unsupported; rasters
  are dropped on `SDL_RENDER_TARGETS_RESET` / `DEVICE_RESET`.
- Not yet: soft-window / help documents, skipping the frame entirely when
  nothing changed.

## Status (2026-10-22) — touchscreen

- `app_touch.cpp`: SDL finger events on the document surface (direct
  touchscreens only). Tap = left click (links, caret, stretch, tasks); drag
  scrolls the pane / float client with fling momentum; long press (500 ms)
  selects a word and the drag extends it; two-finger pinch zooms
  (`zoom_by`, shared with View → Zoom). Scrollbars, float title/frame and
  the split gutter take touch as a plain pointer drag.
- Touch-synthesised mouse events (`SDL_TOUCH_MOUSEID`) now only feed ImGui;
  `Ui::captures_point` decides ImGui ownership by hit-testing last frame's
  windows/popups (WantCaptureMouse lags a frame behind a jumping touch).
- Left-button mouse paths are `pointer_left_down/motion/left_up(x, y)`.
- Not tested on real touch hardware yet. Possible follow-ups: keep the
  pinch centre anchored while zooming; autoscroll when a selection drag
  reaches the pane edge; selection handles.

## Status (2026-10-14) — ECS-lite refactor (docs/PLAN-ecs-lite.md)

- Phase A done: stable `BufferId`s (`buf`/`find_buf`/`add_buffer`/`remove_buffer`/`close_tab`).
  Behaviour notes: closing a tab now frees its buffer (unless a floating window still
  shows it); diff marks are indexed by pane, not by buffer.
- Phase B done: `FloatWindow` + `app_windows.cpp` (`float_press/motion/release/wheel`,
  `draw_floats`). Behaviour changes: hit-test order now matches paint order
  (docs → help → backlinks → outline); backlinks gained wheel + scrollbar drag;
  Ctrl+W closes docs → help → backlinks → outline. Panels stay persistent
  members (`help/ol/bl` + mode bools), not entries of a `floats` vector.
- Phase C done: single `Prompt` (`open_prompt`/`close_prompt`/`commit_prompt`);
  the ten `*_mode` flags and their buffers are gone. Pre-check: flags could
  coexist only through odd transitions (e.g. URL bar then Ctrl+K); the last
  opened prompt now simply wins. Behaviour changes: Gemini input prompt seeds
  the minibuffer with the final URL (was stale); Ctrl+V into path prompts goes
  through the minibuffer (was desynced); stats panel hidden for every prompt.
- Phase D partial: `draw_pane` split into `draw_pane_overlays` + `draw_pane_scrollbar`.
  Outline + backlinks keys extracted to `handle_outline_key` / `handle_backlinks_key`;
  Arrow/Home/End/Page keys extracted to `handle_nav_key`. Phase D complete
  (Node payload split intentionally left for a design note).

## Status (2026-10-13)

Architecture + look pass:
- `main()` (8.6k lines of lambdas) split into `App` data + `src/app_*.cpp`
  systems; events dispatched per SDL event type; `execute_command` shared by
  menus/toolbar; status line computed as data (`compute_status`).
- New `theme.hpp/.cpp`: classic BeOS/GNOME2 palette + bevel/gradient/window
  primitives. Floating windows (help, outline, backlinks, document windows)
  share one yellow-tab frame; status bar = sunken panels (no more overlapping
  hint/stats, readable notices); scrollbar uses bevelled buttons + grip.
- Ctrl+Tab no longer opens ImGui's window-switcher popup.
- Follow-ups: key handling (`on_keydown`, ~1800 lines) still inline — move to
  a key→UiCommand table; unify the nine `*_mode` prompt flags into one
  `Prompt` component; `draw_pane` (~750 lines) and `on_mousebuttondown` /
  `on_mousemotion` could be split into hit-test + action systems.

## Status (2026-10-12)

Right-click context menu no longer clears the selection when the click is
inside the selected range (Cut/Copy/style keep their target).

## Status (2026-10-12)

Rewrite `layout_paragraph` word-wrap from scratch (word/space machine instead
of pen-break). Fixes systematic overflow past the measure that the unit test
caught. Justify still on soft-wrapped body lines with 3em gap cap.

## Status (2026-10-12)

Scrollbar was clipped away (drawn under content clip of pane.w-kSbW) — clear
clip before drawing it. Wrap no longer lets spaces past the measure (root of
overflow test failures). Justify always applies on soft-wrapped body lines
with a soft gap cap (3em), not only ≥88% full lines.

## Status (2026-10-12)

Silence unused `glyph_right` in layout wrap unit test.

## Status (2026-10-12)

xanatoo_tests: add `external/` include path for stb_image.h (images.cpp).

## Status (2026-10-12)

Build fix: link `images.cpp` into xanatoo_tests (LayoutEngine ImageCache);
wire `reload_folds_for_focused` to clear unused-lambda warning.

## Status (2026-10-12)

Wrap overflow regression: unit test `test_layout_wrap_respects_max_width`
(Storm of Steel style paragraph); layout gutter kSbW+4; clip excludes
scrollbar; wrap uses max_width-1 safety; Font metrics-only (null renderer).

## Status (2026-10-12)

Justify quality: only stretch lines ≥88% full; cap gap to ~1.25em; trim
trailing spaces for right edge. Layout gutter pane.w-(kSbW+2). Hint to use
Measure when justifying full-width.

## Status (2026-10-12)

Layout width reserves scrollbar gutter (`pane.w - kSbW`). View **Justify**
option (persisted): distributes extra space across word gaps on wrapped body
lines (not last/hard breaks, headings, code, captions).

## Status (2026-10-12)

Scrollbar chapter marks stable under virtualization: prefer Section boxes,
fill missing Y from height_cache tree walk (nested under stubs too).

## Status (2026-10-12)

Scrollbar: trough click pages toward the click; Shift+click jumps; thumb
grab keeps offset (no jump); arrow hover highlight.

## Status (2026-10-12)

Classic scrollbar: wider track (14px), ↑/↓ buttons, chapter ticks from outline,
search-hit ticks while leap search is active, caret mark on focused pane.

## Status (2026-10-12)

Outline jump scroll: pin to layout **box top** (not caret baseline) so
heading ascenders are not clipped; force full layout when the target has
no box yet under virtualization.

## Status (2026-10-12)

Polish: View menu numbers from ViewSettings constants; `active_focus_section()`
for layout calls; Focus mode uses `drop_height_hints_for_buf`; outline title
shows focused section name when Focus is on.

## Status (2026-10-12)

Polish: dedupe View/toolbar display menus (`fill_*_items`); margins under
Measure toolbar menu; SQLite3::SQLite3 (drop deprecation); Focus menu always
enabled; status-bar hint when section Focus is on; AGENTS notes.

## Status (2026-10-12)

Browse toolbar: Line spacing + Measure dropdowns with `linespacing` /
`measure` icons (SVG + drawn fallback). Same options as View menu.

## Status (2026-10-12)

Section **Focus** mode (outline title-bar toggle / Ctrl+Alt+F / View menu):
body layout shows only the focused Section/Article (ancestors as headers for
context; other branches omitted). Outline still lists the full tree; clicking
a row retargets the filter. View filter only — document unchanged.

## Status (2026-10-12)

Outline jump scrolls the heading to the **top** of the pane (not merely into
view at the bottom edge). Typing/arrow `scroll_caret_into_view` unchanged.

## Status (2026-10-12)

Virtualization: drop height-hint cache on undo/redo, navigate/load, New, and
Reload — spine-only invalidation left stale block heights after full tree
replace (wrong skip heights / scroll). Typing still uses spine invalidation.

## Status (2026-10-12)

Fix: declare `view_settings` + `apply_view_settings` in main (handlers were
committed without the load/apply wiring — compile error).

## Status (2026-10-12)

Outline click jump reliability: narrow ▸/▾ hit zone so heading text always
jumps; resolve Section-only rows to first text child; scroll_caret falls back
to ancestor layout boxes and force-full layout when virtualization has no
glyph for the target.

## Status (2026-10-12)

View display settings (rendering only, not document format): line spacing
(tight/normal/relaxed/loose), margins (narrow…extra), measure/column width
(full / ~720 / ~960). Persisted in `~/.config/xanatoo/view.conf`. LayoutEngine
reads LayoutOptions (margin_x/y, line_spacing, max_content_width).

HarfBuzz remains deferred — English/feature polish first.

## Status (2026-10-12)

EPUB import: post-pandoc `sanitize_epub_markdown` strips Calibre residual
HTML (`<span class="calibreN">` empty/nbsp spacers, unwrap simple spans,
empty page `<a id>` markers, `&nbsp;` / U+00A0 → space). Runs on cache hits
too so older imports clean up without re-pandoc. Unit tests covered.

## Status (2026-10-12)

Backlinks hit-test: drop unused `cx`/`ch` (only `cy` used for row pick).

## Status (2026-10-12)

Compile fix: reorder `invalidate_height_around` / `open_transclude_in_other_pane`
after their callees (`flash_status`, `scroll_caret_into_view`, `navigate_to_url`);
repair mismatched braces in Ctrl+Shift+O outline toggle (jump-to-case errors);
append include `#anchor` when opening transclude in other pane.

## Status (2026-10-12)

EPUB import via pandoc → GFM Markdown (`content.md` + `--extract-media` under
`~/.cache/xanatoo/epub-import/`). Original `.epub` stays immutable; buffer path
is the derived Markdown. Drop / open / CLI accept `.epub` when pandoc is on PATH.

## Status (2026-10-12)

Virtualization: per-pane width cache invalidation; edit/fold drops height hints along the caret/section spine only. Outline/link/include jumps expand folded ancestors (persisted in sidestore).

## Status (2026-10-12)

Docked dual-pane outline (Ctrl+Shift+O; Ctrl+Alt+O toggles float/dock). SQLite side store for section folds (~/.config/xanatoo/sidestore.sqlite).

## Status (2026-10-12)

Section promote/demote + subtree move; same-doc backlinks panel (Ctrl+Alt+B); transclude Alt+click / Open in other pane + broken status.


Minibuffer: Tab path completion, ↑↓ history (persisted under ~/.config/xanatoo/), cursor editing (C-a/e/k/u/w).


nix develop: verbose shellHook banner + expanded docs/ENVIRONMENT.md (biltoo/Pingus pattern write-up).


Design brainstorm captured in `docs/BRAINSTORM.md` (galleries, transclusion, identity, Xanadu-style links, SQLite direction).

## Status (2026-10-12) — continued

Large-document virtualization (v1): viewport-scoped sequential layout with
per-pane block height cache. Off-screen sequential blocks skip glyph
materialisation when height hints exist.

## Status (2026-10-12) — section tree

Product decisions:
- Fold / view state ultimately in side SQLite (not the document); docs trend
  toward immutability; originals may be ingested for versioning/annotation.
- Jump to outline target without auto-expand (expand later).
- Always normalize org stars and ATX/setext into nested Section + Header trees.
- Never auto-derive Article (intentional magazine-scale unit only).
- Structure highlight labels Header as headerN (level / section depth).

Implemented: org + markdown ATX/setext → Section tree; org serialize walks
sections; structure overlay shows headerN; unit tests for nest ownership.

Implemented (223): view-local Section/Article fold (layout skips body;
outline twisties + ←/→; Ctrl+Shift+F). Fold not in document (SQLite later).
Jump still does not auto-expand.

Still open: richer sidestore (annotations/versioning); independent outline UI expand vs body fold.

## Status (2026-10-11)

nix develop: xanatoo-configure/build/run/run-gdb/test PATH scripts (biltoo pattern).

More unit tests: blockquote/code/columns, gemtext structures, MD round-trip (146 asserts).

More unit tests: sexpr parse, format paths, stretch/link (130 asserts).

More unit tests: org parse, path helpers, parse_document dispatch (109 asserts).

Expanded unit tests: markup/gemtext parse + more URL/JSON edges (85 asserts).

Unit tests: tests/unit_tests.cpp + xanatoo_tests CMake target (59 assertions).

navigate_to_url: shared finish_remote_open for Nostr/Reddit/Lemmy.

Nostr: use shared json_min.hpp (drop local JSON helpers).

Centralize starts_with_ci / ascii_lower(sv) / trim_ascii in util.hpp (re-applied).

Fix: is_network_startup lambda capture of is_in_app_remote_url.

Continue: Nostr uses shared SSL init; trim unused includes; is_inapp_doc_href via is_in_app_remote_url.

Continue refactor: Gemini uses shared TLS/TCP; main is_in_app_remote_url helper.

Refactor: shared https.cpp + json_min.hpp; slim reddit/lemmy clients.

README: welcome screenshot under docs/screenshot-welcome.png.

Lemmy comments threaded via Message.alt from Lemmy path (indent in layout).

Lemmy post pages: fetch comments via /api/v3/comment/list (no longer embedded in /post).
Post titles / “N comments” / c/name link in-pane.

Lemmy read-only via API v3 (community listings + posts/comments → Thread/Message).
Reddit compile fix already on origin (201).

Reddit read-only via public `.json` HTTPS API (listings + post/comments → Thread/Message).
Nostr Message body fix already on origin (197).

Stage **01**–**04** core features in place; ImGui without X11 syswm dependency.
LCS structural diff marks; cross-document `:::include`.

- [x] Document model, layout, FreeType + SDL2
- [x] Minimal markup parser
- [x] Caret, hit-testing, insert / delete
- [x] **Enter splits paragraph** into two sibling blocks
- [x] **Selection** (shift-arrows, click-drag) with highlight
- [x] **Vertical caret movement** using layout baselines
- [x] **Stretchtext** — `:::stretch summary="…"` starts collapsed;
      click ▸ / ▾ marker to expand/collapse and reflow
- [x] Ctrl+A selects current block
- [x] `--font` / `-f` CLI option to choose the TTF
- [x] Clipboard (Ctrl+C / Ctrl+V / Ctrl+X)
- [x] Undo / redo via document snapshots (Ctrl+Z / Ctrl+Y)
- [x] Caret blink
- [x] Build fix: qualify NodeId in main.cpp
- [x] Open document from CLI (`file.md` / `--open`)
- [x] Save with Ctrl+S via `serialize_markup`
- [x] Default font via `XANATOO_FONT` + flake wrapper (DejaVu)
- [x] Image loading (stb_image: PNG/JPEG/BMP/GIF) + layout/render
- [x] Dirty flag / asterisk in window title
- [x] Side-by-side `:::columns` / `:::column` layout
- [x] Multi-node selection copy and delete
- [x] **Nostr read-only** threads (/ → Thread/Message)
- [x] Dual-pane multi-view (Ctrl+\\ toggle, Tab focus)
- [x] Two-file CLI opens independent docs in dual-pane
- [x] Draggable pane divider (split_ratio)
- [x] Per-document undo stacks and dirty flags
- [x] Leap search (Ctrl+F / F3) — Canon Cat–inspired
- [x] On-screen leap query status bar
- [x] Scroll-to-match after search
- [x] Untitled Ctrl+S → auto `untitled.md` (or untitled-N.md)
- [x] Build fix: serialize_markup inside namespace xanatoo
- [x] Link underlines + click-to-follow `#anchor`
- [x] Ctrl+←/→ word movement
- [x] Scroll caret into view on arrow navigation
- [x] Image drag-resize (display_scale + corner handle)
- [x] Home/End line movement
- [x] Persist image `{scale=…}` in markup round-trip
- [x] Double-click selects word
- [x] Bottom status bar with common keyboard shortcuts
- [x] Ctrl+B / Ctrl+I / Ctrl+K bold, italic, link on selection
- [x] Ctrl+Shift+S Save As (path entry in status bar)
- [x] Ctrl+K link href edit mode in status bar
- [x] Mouse wheel scrolls pane under cursor
- [x] Ctrl+Shift+A select whole document
- [x] Rename third_party/ → external/
- [x] Ctrl+E wrap block in stretchtext
- [x] Multi-node bold/italic/link styling
- [x] Esc clears selection (no longer quits); Ctrl+Q quits with dirty warn
- [x] Backspace at block start merges with previous paragraph
- [x] Ctrl++ / Ctrl+- font zoom (clears glyph cache)
- [x] Scrollbar thumb per pane
- [x] Ctrl+` code span style
- [x] Ctrl+H leap-replace (Tab switches find/replace)
- [x] Draggable scrollbar
- [x] README keybinding reference
- [x] Ctrl+0–6 paragraph / heading levels
- [x] Ctrl+Shift+[ / ] collapse / expand all stretchtext
- [x] Ctrl+Shift+E edit stretch summary
- [x] Ctrl+Shift+U unwrap stretchtext
- [x] Alt+↑/↓ reorder blocks
- [x] Ctrl+D duplicate block
- [x] Open external http(s)/mailto links via xdg-open (Linux) / open (macOS)
- [x] Hovered link href shown in status bar
- [x] Unordered lists (`-`/`*`/`+`) and ordered lists (`1.`) — parse, layout bullets, serialize
- [x] Enter in list item splits to sibling ListItem; empty item exits list; ordered renumber
- [x] Hover preview tooltip for internal `#anchor` links (target passage snippet)
- [x] Block quotes (`> …`) — parse, layout bar, serialize; Ctrl+Shift+Q toggle
- [x] Horizontal rules (`---`/`***`/`___`) — parse, layout line, serialize
- [x] Ctrl+Shift+8 / Ctrl+Shift+7 toggle unordered / ordered list on block
- [x] Inline transclusion: `:::include #Target` live embed + click-to-source
- [x] Drag-and-drop block reorder (left-margin grab, sibling relocate)
- [x] Adjustable column widths by dragging gutters (`Column.display_scale` weights)
- [x] Cross-parent block drag (into/out of columns & lists; ListItem↔Paragraph)
- [x] Nested lists: Tab/Shift+Tab indent/outdent; indented markup parse/serialize
- [x] F1 keyboard reference overlay (Esc/F1 to close)
- [x] Sample document moved to `examples/welcome.md` + PNG images
- [x] Dual-pane visual links: sync jump, Ctrl+Click other pane, connector line
- [x] Side-by-side diff marks (Ctrl+Shift+D, two independent files)
- [x] Heading outline (Ctrl+Shift+O) — jump via Enter / 1-9 / click
- [x] Ctrl+Enter new paragraph after block; Ctrl+Shift+- insert HR
- [x] Status-bar document stats (words, chars, headings)
- [x] Page Up/Down move caret; Ctrl+Home/End document extremes
- [x] Paste plain text splits blank lines into sibling paragraphs
- [x] Insert image (Ctrl+Shift+I path prompt)
- [x] FontConfig: bold/italic/bold-italic/mono faces for styled text
- [x] Shift+F3 / find_prev; soft background under inline code
- [x] Ctrl+R reload from disk (Ctrl+Shift+R force)
- [x] Selection word/char counts in status bar
- [x] Dear ImGui menu bar / toolbar (no X11 syswm on Linux)
- [x] Structural LCS-based dual-pane diff marks (insertions no longer cascade)
- [x] Cross-document transclude: `:::include path.md#Heading` (and whole-file)
- [x] Experimental S-expression serialize/parse (`.sexp` / `.glyph` / `.ss`)
- [x] Sexpr polish: `:id`, merged text spans, compact floats; File→Export; cache flush on reload
- [x] Drag-and-drop image files into a pane (PNG/JPEG/GIF/BMP)
- [x] Refactor: `util.hpp` (ascii_lower, anchors, image path); -Wall; dead-code cleanup
- [x] Extract CLI/bootstrap to `cli.hpp`; multi-delete no longer leaves empty husks
- [x] Leap-search match highlight (amber); `advance_caret`; ascii_lower in find
- [x] Fix mouse/selection offset (`content_y` + hit-test); triple-click; select_all/node/word
- [x] Scrollbar / page / scroll-into-view account for toolbar height
- [x] Classic Gtk2/Clearlooks-ish ImGui theme + SVG→PNG toolbar icons
- [x] Help as ImGui software window (draggable/resizable, not OS window)
- [x] Help is a **real document** (`examples/help.md`) rendered in a software window
- [x] Help window: non-modal keyboard; document Y offset matches window position
- [x] Help: stretchtext toggle + scrollbar; colorful BeOS/GNOME2 toolbar icons
- [x] ImGui UI font: DejaVu / Bitstream Vera Sans (GNOME 2 default family)
- [x] Semantic format: FORMAT.md; :label; doc meta; note; codeblock
- [x] Semantic format: deflist/term/def; figure/caption
- [x] Semantic format: cite + biblio; typed links (:rel)
- [x] Semantic format: include :excerpt ranges; semantic tables
- [x] Editor: insert note / code block / table / definition list
- [x] Project renamed glyphtoo → **xanatoo** (namespace, paths, binary, env)
- [x] Software windows for arbitrary docs (FORMAT.md, semantic.sexp, path…)
- [x] Nix build: python3 for icon rasterize; libx11 (drop deprecated xorg.libX11)
- [x] Icon install/runtime: XANATOO_ICON_DIR env, exe-relative share path, flake wrap
- [x] Soft-window mouse: browse independent of help_mode; drag/resize/scroll motion
- [x] Outline as software window (drag/resize/scroll; non-modal; stays open on jump)
- [x] Multiple document software windows (stack; raise on click; cascade open)
- [x] Soft-window docking: edge snap + double-click title maximize/restore
- [x] Markdown semantic round-trip: Note/CodeBlock/DefList/Figure/Biblio/Table
- [x] Fix `:::note` creating Paragraph instead of Note
- [x] Markup `:::include … excerpt="…"` + serialize excerpt
- [x] Inline Cite as `[text](cite:key)` and link `{rel=…}` in markup
- [x] GFM pipe tables parse/serialize under `:::table` or standalone
- [x] Table presentation: grid lines, header fills, cell padding
- [x] Code-block soft background + border
- [x] Note bar distinct (amber) from block-quote bar
- [x] Table cell layout merges images/quote/note/code decorations
- [x] Layout respects hard newlines (`\n`) in paragraphs/code blocks
- [x] Enter in CodeBlock inserts newline (does not split the block)
- [x] Enter in TableCell inserts a new row (same column count)
- [x] Tab / Shift+Tab moves between table cells
- [x] Word-level intra-block diff marks for similar modified blocks
- [x] Citation insert/edit (Ctrl+Shift+C / Format → Citation)
- [x] Bibliography entry insert (Format → Bibliography Entry)
- [x] Status bar prompts for citation key vs link href
- [x] Typed link `:rel` — edit via `href rel=name`, hover shows relation
- [x] `hit_test_link_ex` returns kind/rel/node for richer status UI
- [x] Table column weights (`display_scale` / `:weight`) + draggable separators
- [x] GUI: context menu (right-click), expanded Format/View menus
- [x] GUI: structure highlight mode (Ctrl+Shift+L / View / toolbar)
- [x] GUI: stretch wrap/summary, list indent, columns, select block reachable via menu
- [x] Soft-window tile zones (left/right/top/bottom + maximize) with drag preview
- [x] Nested block content in table cells (layout + sexpr + insert-into-cell)
- [x] Same-document transclusion write-through (embed body glyphs → source node)
- [x] Insert Include/Transclude via GUI + status prompt
- [x] Click external include opens source file in soft window
- [x] Insert Figure (image + caption) via GUI
- [x] Heading levels 1–6 / demote in Format menu (`set_heading_level`)
- [x] Caption layout uses italic face
- [x] Replace All (leap/replace mode + Edit menu)
- [x] Paste into code blocks preserves newlines
- [x] Soft-window docks reflow on window resize
- [x] duplicate_block copies label + climbs to Figure/Table; menu + context items
- [x] Drag-and-drop opens .md/.sexp/.txt in soft window (images still insert)
- [x] GFM ``` / ~~~ fenced code blocks (parse + markdown serialize)
- [x] GFM task list items (`- [ ]` / `- [x]`) parse/serialize + click toggle
- [x] GFM strikethrough (`~~text~~` / InlineKind::Strike)
- [x] Empty-doc caret recovery (seed paragraph so typing works)
- [x] Toggle Task Item (menu + Ctrl+Shift+T)
- [x] GFM autolinks (`http(s)://…` and `<url>`)
- [x] Toolbar expanded (strike/cite/headings/task/table/figure/view toggles)
- [x] Context menu: outline/diff/find/replace; iconless toolbar uses tip labels
- [x] Horizontally scrollable toolbar on narrow windows
- [x] Setext headings (`===` / `---`) in markdown parse
- [x] Markdown backslash escapes (parse + plain-text serialize)
- [x] GFM hard line breaks (two trailing spaces) in paragraphs and quotes
- [x] Esc closes path-in-window prompt, help, and top soft window
- [x] Fix path-in-window TEXTINPUT (was typing into the document)
- [x] Underscore `_emphasis_` (word-boundary aware)
- [x] Markdown nested table cells via :::row / :::cell / :::th
- [x] UI: missing toolbar icons (strike/cite/h1–h3/para/task/table/figure/note/replace/outline/copy/paste/new/open)
- [x] UI: File → New / Open… (Ctrl+N / Ctrl+O); Insert + Help menus
- [x] UI: toolbar New/Open/Copy/Paste buttons
- [x] Event routing: status-bar prompts no longer blocked by ImGui WantCaptureKeyboard
- [x] Event routing: smarter want_capture_keyboard (text/popup/active only)
- [x] Event routing: status prompts absorb non-prompt keys (no caret drift)
- [x] Event routing: right-click on soft windows does not open doc context menu
- [x] Event routing: document click dismisses status-bar prompts
- [x] UI: zoomin/zoomout/structure/diff toolbar icons
- [x] UI: Unwrap Stretch menu item (Ctrl+Shift+U)
- [x] Format menu: Structure submenu (Insert no longer duplicated)
- [x] Help menu: FORMAT.md + semantic demo entries
- [x] Ctrl+V pastes into status-bar prompts (paths, search, links…)
- [x] Middle-click paste at caret (Linux-style)
- [x] Mouse motion over soft windows no longer drives pane hover/selection underneath
- [x] Dirty-buffer protection for New / Open / Quit (repeat to confirm discard)
- [x] Status-bar flash notices for warnings and open/new feedback
- [x] File→Quit uses the same unsaved-changes warn as Ctrl+Q
- [x] Status bar excluded from document mouse hit-tests and wheel scroll
- [x] Save success flashes status bar; clears discard-armed state
- [x] Menu/toolbar commands release ImGui input capture for the document
- [x] help.md documents Ctrl+N / Ctrl+O / middle-click / drag-open
- [x] Safer ImGui capture release (clear active/nav; no popup stack churn)
- [x] Status bar ignored during mouse motion (non-drag) and image drops
- [x] Open dirty-confirm status message; Ctrl+W closes soft window/help/outline
- [x] Consistent status_bar_h instead of magic 28 for layout/hit tests
- [x] Alt+F/E/O/I/V/H opens File/Edit/Format/Insert/View/Help menus
- [x] Thread / Message nodes (mailing-list & forum threading, about= comments)
- [x] examples/thread-rfc.md lengthy RFC discussion example
- [x] Diff demo pair (examples/diff-v1.md + diff-v2.md)
- [x] Examples menu (View + Help); LoadDiffDemo dual-pane with marks
- [x] Structure highlight: kind tags on the right edge of each region
- [x] Fix ImGui 1.91 field names in release_input_capture (HoveredId, NavCursorVisible)
- [x] Diff: status feedback + can_diff disables menu when only one buffer
- [x] LoadDiffDemo respects dirty-buffer discard arming
- [x] Structure tags skip table-row shells (less noise)
- [x] Fix overlay screen Y (+ ui_top) for diff bars, word marks, gutters, drag
- [x] Grip-dot hit-test uses content_y; dual-pane caret connector aligned
- [x] Scrollbar click uses content view height (ui_top + status bar)
- [x] Structure tags skip paragraphs; status flash on link follow
- [x] PageUp/Down scrolls soft window / help / outline under cursor
- [x] Context menu Diff Marks respects can_diff
- [x] Semantic Header / Section / Article (HTML-like sectioning)
- [x] examples/sectioning.md + Format structure commands
- [x] Sectioning polish: keys, help, Header in editor text paths
- [x] Enter on Header starts a body paragraph; wrap status flashes
- [x] Browse mode (read-only) + gamepad navigation
- [x] Browse mode gates menus, paste, drag-edit; steady caret
- [x] Gemtext parser (.gmi) + optional gemini:// client (OpenSSL)
- [x] URL bar (Ctrl+L) for gemini:// and local paths
- [x] Shared navigate_to_url; Gemini 10/11 input; flake OpenSSL; Open URL menu
- [x] Gemini: socket timeouts; non-text MIME notice; README/AGENTS
- [x] Gemini history (Alt+Left/Right), reload, CLI gemini:// startup
- [x] Copy page URL; quiet gemini startup; gamepad history back
- [x] File Back/Forward; status bar shows capsule URL + history index
- [x] Interactive menu-bar URL field; default window 1024×768
- [x] Fix broken multi-line string in Gemini unsupported-MIME notice
- [x] Session history for local + gemini://; History panel (Ctrl+Shift+H)
- [x] Bookmarks menu + manager; persist ~/.config/xanatoo/bookmarks; Ctrl+Shift+B
- [x] Welcome hub links to all examples; in-app open for local doc links
- [x] Hover affordances: link wash/underline, stretch/image/gutter, hand cursor, target tooltip
- [x] Hover: task checkbox, block grip, transclude region (status/tooltip/cursor)
- [x] Org-mode subset parser/serialiser (.org); Node.tags; sample.org
- [x] Multi-doc redo: shared buffers, tab strip, pop-out/pop-in windows
- [x] Bugfix: dirty checks, TAB cases, soft layout base, tab UI, open-switch
- [x] Bugfix: soft-window doc access, stretch undo, reload/new soft sync
- [x] Build fix: org unused var; soft relative links; tab close keeps floats
- [x] Perf: large-doc render cull, history depth, org parse, outline, hit_test
- [x] Initial layout uses real window size (was stuck at 920 until resize)
- [x] Fix endless relayout when window size reports 0; column width clamp; tags copy
- [x] Diff uses pane buffers; navigate switches existing tab; Next/Prev tab title
- [x] Split browse/edit toolbars; Back/Forward/URL on browse row
- [x] Compact edit toolbar with format/structure/lists/insert/view menus
- [x] Fix tab bar flicker; icon text fallbacks + lists icon
- [x] Multi-dir icon load; drawn fallbacks for browse/menu icons
- [x] Diff demo crash: relayout after UI; clear soft/hover on buffer replace
- [x] R36S / PortMaster flake outputs; XANATOO_DATA_DIR for examples
- [x] Flake: hostPlatform.isLinux; drop x86_64-darwin (nixpkgs 26.11+)
- [x] R36S: SDL2 include path; C++17 starts_with polyfill
- [x] R36S: stick to kurvenrausch eoan SDL; ImGui legacy renderer path

### R36S / PortMaster — confirmed working; polish later
- [x] UI font restored to pre-R36S DejaVu 14px (throw away scale/Proggy experiments)
- [x] Gamepad: connect hint, Y=history forward, browse-mode status hints; ImGui NavEnableGamepad
- [x] On-screen virtual keyboard (F3 / Guide / View menu)
- [x] Right-stick software cursor (A/B click) for R36S
- [x] Stick cursor always on with pad; accel; LB+A caret; disconnect cleanup
- [x] --fullscreen + R36S launcher uses it
- [x] Stick cursor: drawn sprite (not system/X11 cursor) — visible on KMS/DRM
- [x] Stick cursor fully separate from hardware mouse (no WarpMouse)
- [x] Stick sprite alpha fixed (ABGR8888 upload, same as glyphs)
- [x] Stick sprite is extra; real system mouse unchanged

## Open work / next steps

0. **Large-document navigation** — section Focus mode (246); virtualization — v1 in (viewport + height-hint skip for sequential blocks). Still open: tighter invalidation on edits, column-internal skip, measure-only pass without FreeType for far blocks, history depth already capped.

1. Optional: HarfBuzz for complex scripts
2. Replace snapshot undo with a tighter operation log if memory becomes an issue
3. Editable transclusion mirror (same-doc write-through in; external still read-only)
4. Word-level / intra-block diff highlighting (basic pairing in)
5. **Tables** — grid + column weights + nested block cells (markdown :::row/:::cell in)
6. **Docking** — edge snap + maximize + tile zones in
7. **Citations** / typed link relations / range transclusion (insert+jump+:rel edit/hover in)

## Design notes

- Stretch nodes carry `summary` + `expanded`. Layout synthesises a
  one-line marker when collapsed; children are skipped.
- Selection is two Carets (anchor + focus). Highlight is derived from
  the glyph list, not stored on the document.
- Block split creates a new sibling via `Document::insert_after` and
  moves the trailing spans across.
- List: `level` 0 = unordered, 1 = ordered. ListItem `level` holds the
  ordered display number (0 for bullets). Layout draws "•" or "N." as
  synthetic glyphs on the ListItem node.
- Enter on ListItem: same-kind sibling + ordered renumber. Enter on an
  empty ListItem exits the list (paragraph after the list; empty list
  removed). Backspace-merge drops empty List parents.
- BlockQuote: text node with left bar (`LayoutResult::quote_bars`).
  Ctrl+Shift+Q toggles; Ctrl+0 demotes to paragraph.
- HorizontalRule: non-text node; `hr_lines` in layout. Toggle list wraps
  a text block in a List or unwraps a ListItem back to Paragraph.
- Transclude: `src` holds `#anchor` or `path.md[#anchor]`. Layout snapshots
  the target's spans (glyphs tagged with the Transclude node). Local click
  jumps to the source. Cross-doc paths resolve relative to the host file's
  directory via a process-wide external document cache.
- Diff marks: LCS of plain-text block fingerprints; only non-common nodes
  are marked (insertions/deletions no longer cascade).
- Block drag: grab left 16px of a layout box; drop on a same-parent sibling
  (top/bottom half = before/after). `relocate_block` splices the child list.
- Column widths: `Column.display_scale` is a relative weight (default 1).
  Layout emits `column_gaps`; dragging a gap shifts weight between neighbours.
  Markup: `:::column weight=1.5`.
- Cross-parent `relocate_block`: reparents across Columns/Lists; converts
  ListItem↔Paragraph when crossing a List; drops emptied lists.
- Nested lists: ListItem may own a child List. Tab nests under the previous
  sibling; Shift+Tab promotes. Markup uses 2-space indent per level.
- Shared helpers live in `util.hpp` (ascii_lower, collect_text_nodes,
  find_anchor_node, is_raster_image_path) so layout/editor/main do not
  each reimplement anchor matching or extension checks.

## Bundle history

- `xanatoo-001.*` — stage 01 scaffold
- `xanatoo-002.*` — markup, caret, basic edit
- `xanatoo-003.*-selection-stretch-…` — selection, stretchtext
- `xanatoo-004.*-font-cli-…` — `--font` CLI
- `xanatoo-005.*-clipboard-undo-…` — clipboard, undo, blink + NodeId fix
- `xanatoo-006.*-open-save-font-…` — open/save + default font
- `xanatoo-007.*-images-dirty-…` — images + dirty flag
- `xanatoo-008.*-columns-multisel-…` — columns + multi-sel
- `xanatoo-009.*-dual-pane-…` — dual-pane multi-view
- `xanatoo-010.*-two-docs-divider-…` — two docs + divider
- `xanatoo-011.*-undo-leap-…` — per-doc undo + leap search
- `xanatoo-012.*-search-ui-save-…` — search UI + untitled save + ns fix
- `xanatoo-013.*-links-words-…` — links + word move + scroll fix
- `xanatoo-014.*-image-resize-…` — image resize + Home/End
- `xanatoo-015.*-scale-persist-…` — scale persist + dblclick
- `xanatoo-016.*-status-bar-…` — shortcuts status bar
- `xanatoo-017.*-style-saveas-…` — styles + save as
- `xanatoo-018.*-link-edit-…` — link href edit + wheel + select-all
- `xanatoo-019.*-external-stretch-…` — external/ + Ctrl+E stretch
- `xanatoo-020.*-multistyle-quit-…` — multi-node style + quit safety
- `xanatoo-021.*-merge-zoom-…` — merge + zoom
- `xanatoo-022.*-scroll-replace-…` — scrollbar + replace + code style
- `xanatoo-023.*-scroll-drag-docs-…` — scroll drag + README
- `xanatoo-024.*-heading-stretch-…` — heading levels + expand-all
- `xanatoo-025.*-stretch-edit-…` — stretch summary + unwrap
- `xanatoo-026.*-block-move-…` — block reorder + duplicate
- `xanatoo-027.*-external-links-…` — external links + hover
- `xanatoo-028.*-lists-…` — list parse/layout/serialize
- `xanatoo-029.*-list-edit-…` — list Enter/exit/renumber
- `xanatoo-030.*-link-preview-…` — anchor hover preview
- `xanatoo-031.*-blockquote-…` — block quotes
- `xanatoo-032.*-hr-list-toggle-…` — HR + list toggle
- `xanatoo-033.*-transclude-…` — :::include transclusion
- `xanatoo-034.*-block-drag-…` — block drag reorder
- `xanatoo-035.*-column-width-…` — column width drag
- `xanatoo-036.*-cross-parent-drag-…` — cross-parent block drag
- `xanatoo-037.*-nested-lists-…` — nested lists
- `xanatoo-038.*-f1-help-…` — F1 help overlay
- `xanatoo-039.*-example-doc-…` — examples/welcome.md + images
- `xanatoo-040.*-pane-links-…` — dual-pane visual links
- `xanatoo-041.*-diff-marks-…` — dual-pane diff marks
- `xanatoo-042.*-outline-…` — heading outline
- `xanatoo-043.*-insert-blocks-…` — Ctrl+Enter / HR insert
- `xanatoo-044.*-doc-stats-…` — status-bar document stats
- `xanatoo-045.*-page-nav-…` — page / document navigation
- `xanatoo-046.*-paste-paras-…` — structured multiline paste
- `xanatoo-047.*-insert-image-…` — insert image by path
- `xanatoo-048.*-fontconfig-…` — FontConfig styled faces
- `xanatoo-049.*-find-prev-code-bg-…` — find prev + code background
- `xanatoo-050.*-reload-…` — Ctrl+R reload from disk
- `xanatoo-051.*-rebased-…` — rebased onto origin/master
- `xanatoo-052.*-sel-stats-…` — selection status stats
- `xanatoo-053.*-imgui-…` — Dear ImGui menu/toolbar
- `xanatoo-054.*-imgui-no-x11-…` — ImGui without X11 syswm
- `xanatoo-055.*-lcs-diff-xdoc-…` — LCS diff + cross-doc include
- `xanatoo-056.*-sexpr-…` — S-expression format
- `xanatoo-057.*-sexpr-polish-…` — sexpr polish
- `xanatoo-058.*-dnd-image-…` — drag-and-drop images
- `xanatoo-059.*-refactor-cleanup-…` — util refactor
- `xanatoo-060.*-cli-delete-cleanup-…` — CLI extract + delete husks
- `xanatoo-061.*-search-highlight-…` — search highlight
- `xanatoo-062.*-mouse-select-…` — mouse offset + selection
- `xanatoo-063.*-classic-ui-icons-…` — classic theme + icons
- `xanatoo-064.*-help-window-…` — ImGui help window
- `xanatoo-065.*-help-document-…` — help as real document
- `xanatoo-066.*-help-window-fix-…` — help non-modal + Y fix
- `xanatoo-067.*-help-resize-…` — help resize
- `xanatoo-068.*-help-stretch-icons-…` — help stretch + icons
- `xanatoo-069.*-ui-font-…` — UI font
- `xanatoo-070.*-semantic-format-…` — meta/label/note/code
- `xanatoo-071.*-deflist-figure-…` — deflist + figure
- `xanatoo-072.*-cite-rel-…` — cite + :rel
- `xanatoo-073.*-excerpt-table-…` — excerpt + table
- `xanatoo-074.*-insert-semantic-…` — insert semantic (as glyphtoo tip before rename)
- `xanatoo-075.*-rename-…` — rename glyphtoo → xanatoo
- `xanatoo-076.*-doc-window-…` — software windows for arbitrary files
- `xanatoo-077.*-nix-icons-libx11-…` — Nix python3 icons + libx11
- `xanatoo-078.*-icon-install-…` — icon install + XANATOO_ICON_DIR
- `xanatoo-079.*-softwin-mouse-…` — soft-window mouse hit-testing
- `xanatoo-080.*-outline-window-…` — outline software window
- `xanatoo-081.*-multi-softwin-…` — multiple document software windows
- `xanatoo-082.*-softwin-dock-…` — soft-window docking
- `xanatoo-083.*-md-semantic-…` — markdown semantic round-trip
- `xanatoo-084.*-table-present-…` — table grid presentation
- `xanatoo-085.*-newline-table-enter-…` — newlines + table Enter
- `xanatoo-086.*-word-diff-table-tab-…` — word diff + table Tab
- `xanatoo-087.*-cite-biblio-…` — citation + biblio insert
- `xanatoo-088.*-link-rel-…` — typed link :rel
- `xanatoo-089.*-table-weights-…` — table column weights
- `xanatoo-090.*-gui-structure-…` — context menu + structure highlight
- `xanatoo-091.*-dock-tiles-…` — soft-window tile zones
- `xanatoo-092.*-nested-cells-…` — nested table cells
- `xanatoo-093.*-transclude-edit-…` — same-doc write-through
- `xanatoo-094.*-insert-include-…` — insert include GUI
- `xanatoo-095.*-figure-headings-…` — figure + heading menu
- `xanatoo-096.*-replace-all-…` — replace all
- `xanatoo-097.*-paste-dock-…` — code paste + dock resize
- `xanatoo-098.*-duplicate-gui-…` — this delivery
- `xanatoo-099.*-drop-docs-…` — this delivery
- `xanatoo-100.*-gfm-fences-…` — this delivery
- `xanatoo-101.*-task-lists-…` — this delivery
- `xanatoo-102.*-strikethrough-…` — this delivery
- `xanatoo-103.*-empty-caret-task-…` — this delivery
- `xanatoo-104.*-autolink-…` — this delivery
- `xanatoo-105.*-gui-toolbar-…` — this delivery
- `xanatoo-106.*-toolbar-setext-…` — this delivery
- `xanatoo-107.*-md-escapes-…` — this delivery
- `xanatoo-108.*-hard-breaks-…` — this delivery
- `xanatoo-109.*-esc-soft-…` — this delivery
- `xanatoo-110.*-path-input-em-…` — this delivery
- `xanatoo-111.*-nested-table-md-…` — nested table markdown
- `xanatoo-112.*-ui-icons-events-…` — icons, New/Open, capture fix
- `xanatoo-113.*-events-icons-…` — prompt keys, soft right-click
- `xanatoo-114.*-menus-paste-…` — menus, prompt paste, middle-click
- `xanatoo-115.*-dirty-status-…` — dirty New/Open/Quit notices
- `xanatoo-116.*-status-focus-…` — status hits, ImGui release, help
- `xanatoo-117.*-ctrlw-statusbar-…` — Ctrl+W, status bar routing
- `xanatoo-118.*-alt-menus-…` — Alt+letter menus
- `xanatoo-119.*-thread-messages-…` — Thread/Message markup
- `xanatoo-120.*-diff-examples-…` — diff demo + Examples menu
- `xanatoo-121.*-structure-labels-…` — structure kind tags
- `xanatoo-122.*-imgui-hoverid-…` — ImGui 1.91 field fix
- `xanatoo-123.*-diff-feedback-…` — diff menu feedback
- `xanatoo-124.*-overlay-y-…` — overlay Y alignment
- `xanatoo-125.*-grip-connector-…` — grip dots + connector Y
- `xanatoo-126.*-scrollbar-click-…` — scrollbar click jump
- `xanatoo-127.*-structure-links-…` — structure tags + link flash
- `xanatoo-128.*-page-soft-…` — PageUp soft chrome
- `xanatoo-129.*-sectioning-…` — Header/Section/Article model
- `xanatoo-130.*-sectioning-polish-…` — sectioning keys/help
- `xanatoo-131.*-header-enter-…` — Enter on Header → paragraph
- `xanatoo-132.*-browse-gamepad-…` — browse + gamepad
- `xanatoo-133.*-browse-gate-…` — browse gates
- `xanatoo-134.*-gemini-…` — gemtext + gemini client
- `xanatoo-135.*-gemini-polish-…` — navigate_to_url polish
- `xanatoo-136.*-gemini-timeout-…` — timeouts + MIME
- `xanatoo-137.*-gemini-history-…` — history + reload
- `xanatoo-138.*-gemini-copy-url-…` — copy URL
- `xanatoo-139.*-gemini-status-nav-…` — status + nav menu
- `xanatoo-140.*-url-bar-interactive-…` — interactive URL field + 1024×768
- `xanatoo-141.*-fix-gemini-mime-string-…` — fix Gemini MIME notice string
- `xanatoo-142.*-history-bookmarks-…` — history + bookmarks
- `xanatoo-143.*-welcome-hub-…` — welcome hub + in-app doc links
- `xanatoo-144.*-hover-affordances-…` — link/stretch/image/gutter hover
- `xanatoo-145.*-hover-task-grip-include-…` — task/grip/include hover
- `xanatoo-146.*-org-mode-…` — org-mode subset
- `xanatoo-147.*-tabs-mdi-…` — tabs + MDI shared buffers
- `xanatoo-148.*-bugfix-mdi-…` — first MDI bugfix pass
- `xanatoo-149.*-bugfix-soft-…` — soft-window shared-buffer fixes
- `xanatoo-150.*-build-soft-links-…` — soft links / tab close
- `xanatoo-151.*-large-doc-perf-…` — large-doc performance
- `xanatoo-152.*-initial-layout-size-…` — initial layout width
- `xanatoo-153.*-layout-loop-guard-…` — layout loop guard
- `xanatoo-154.*-diff-nav-tabs-…` — diff/nav/tabs
- `xanatoo-155.*-browse-edit-toolbars-…` — dual toolbars
- `xanatoo-156.*-compact-edit-toolbar-…` — compact edit toolbar
- `xanatoo-157.*-tabs-icons-…` — tab flicker + icon fallbacks
- `xanatoo-158.*-icon-load-fallbacks-…` — icon load
- `xanatoo-159.*-diff-demo-crash-…` — diff demo crash
- `xanatoo-160.*-r36s-portmaster-…` — R36S port
- `xanatoo-161.*-flake-platforms-…` — flake platforms
- `xanatoo-162.*-r36s-build-fixes-…` — SDL/C++17
- `xanatoo-163.*-r36s-sdl-2.0.20-…` — (reverted)
- `xanatoo-164.*-r36s-eoan-imgui-…` — eoan SDL + ImGui legacy
- `xanatoo-165.*-r36s-polish-notes-…` — polish notes
- `xanatoo-166.*-ui-scale-gamepad-…` — UI scale / gamepad
- `xanatoo-167.*-virtual-keyboard-…` — virtual keyboard
- `xanatoo-168.*-stick-cursor-…` — stick cursor
- `xanatoo-169.*-stick-cursor-polish-…` — stick cursor polish
- `xanatoo-170.*-sprite-cursor-ui-font-…` — sprite cursor + UI font
- `xanatoo-171.*-stick-separate-alpha-…` — stick separate + alpha
- `xanatoo-172.*-ui-font-rollback-…` — ui font rollback
- `xanatoo-173.*-dual-cursor-fixed-font-…` — dual cursor + fixed font
- `xanatoo-174.*-proggyclean-ui-font-…` — proggyclean attempt
- `xanatoo-175.*-restore-pre-r36s-ui-font-…` — restore attempt
- `xanatoo-176.*-font-atlas-surface-geom-cursor-…` — font atlas + geom cursor
- `xanatoo-177.*-r36s-org-gemini-examples-…` — examples in-pane + gemini TLS
- `xanatoo-178.*-fix-stale-node-crash-…` — soft-link node crash
- `xanatoo-179.*-no-abort-invalid-node-…` — no-abort invalid node
- `xanatoo-180.*-harden-nav-layout-caret-…` — nav/layout caret harden
- `xanatoo-181.*-ssl-gamepad-tab-soft-…` — ssl/gamepad/tab
- `xanatoo-182.*-image-key-caret-collect-…` — image key/caret collect
- `xanatoo-183.*-compact-gemini-font-detach-…` — compact/gemini/font
- `xanatoo-184.*-stale-caret-mutate-guard-…` — stale caret mutate
- `xanatoo-185.*-hit-test-invalid-glyph-…` — hit-test invalid glyph
- `xanatoo-186.*-move-serialize-bounds-…` — move/serialize bounds
- `xanatoo-187.*-doc-mutate-outline-…` — doc mutate/outline
- `xanatoo-188.*-clamp-windows-under-chrome-…` — clamp windows under chrome
- `xanatoo-189.*-block-grip-left-gutter-…` — block grip gutter
- `xanatoo-190.*-nostr-threads-…` — Nostr v1 threads
- `xanatoo-191.*-nostr-profiles-multirelay-…` — Nostr profiles/multirelay
- `xanatoo-192.*-welcome-nostr-links-…` — welcome Nostr links
- `xanatoo-193.*-nostr-connect-resilience-…` — nostr connect resilience
- `xanatoo-194.*-fix-nostr-string-literals-…` — fix string literals
- `xanatoo-195.*-nostr-recent-firehose-…` — nostr:recent firehose
- `xanatoo-196.*-fix-query-recent-…` — on origin (add missing query_recent + seed WS carry)
- `xanatoo-197.*-nostr-message-body-…` — on origin (Message body as child Paragraph; real reply-to only)
- `xanatoo-198.*-reddit-json-…` — prior (Reddit .json listings/posts)
- `xanatoo-199.*-reddit-welcome-warn-…` — on origin (welcome links + unused-var warning)
- `xanatoo-200.*-reddit-403-oauth-…` — prior (403 hardening + optional OAuth)
- `xanatoo-201.*-fix-reddit-compile-…` — on origin (oauth parse + children literal)
- `xanatoo-202.*-lemmy-api-…` — prior (Lemmy API v3)
- `xanatoo-203.*-lemmy-post-links-…` — on origin (title/comments/community hyperlinks)
- `xanatoo-204.*-lemmy-comment-list-…` — prior (comment/list API)
- `xanatoo-205.*-lemmy-comment-thread-…` — prior (path → alt indent)
- `xanatoo-206.*-readme-screenshot-…` — prior (welcome PNG in README)
- `xanatoo-207.*-shared-https-json-…` — prior (extract https + json_min)
- `xanatoo-208.*-gemini-shared-tls-…` — prior (Gemini TLS + main URL helper)
- `xanatoo-209.*-nostr-shared-ssl-…` — prior (Nostr SSL init + small cleanups)
- `xanatoo-211.*-fix-remote-url-capture-…` — prior
- `xanatoo-212.*-shared-string-helpers-…` — prior
- `xanatoo-213.*-nostr-json-min-…` — prior
- `xanatoo-214.*-finish-remote-open-…` — prior
- `xanatoo-215.*-unit-tests-…` — prior
- `xanatoo-216.*-expand-unit-tests-…` — prior
- `xanatoo-217.*-more-unit-tests-…` — prior
- `xanatoo-218.*-sexpr-unit-tests-…` — prior
- `xanatoo-219.*-more-markup-tests-…` — prior
- `xanatoo-220.*-nix-develop-helpers-…` — prior
- `xanatoo-221.*-layout-virtualization-…` — this delivery
- `xanatoo-224.*-fix-fold-compile-…` — prior
- `xanatoo-225.*-brainstorm-doc-…` — prior
- `xanatoo-237.*-epub-import-…` — prior
- `xanatoo-238.*-fix-compile-order-braces-…` — prior
- `xanatoo-239.*-backlinks-unused-vars-…` — prior
- `xanatoo-240.*-epub-sanitize-calibre-html-…` — prior
- `xanatoo-241.*-view-display-settings-…` — prior
- `xanatoo-242.*-outline-jump-reliable-…` — prior
- `xanatoo-243.*-fix-view-settings-scope-…` — prior
- `xanatoo-244.*-virt-drop-hints-undo-load-…` — prior
- `xanatoo-245.*-outline-jump-to-top-…` — prior
- `xanatoo-246.*-section-focus-mode-…` — prior
- `xanatoo-247.*-toolbar-linespace-measure-…` — prior
- `xanatoo-248.*-polish-view-menus-sqlite-…` — prior
- `xanatoo-249.*-polish-focus-view-constants-…` — prior
- `xanatoo-250.*-outline-scroll-box-top-…` — prior
- `xanatoo-251.*-classic-scrollbar-marks-…` — prior
- `xanatoo-252.*-scrollbar-page-hover-…` — prior
- `xanatoo-253.*-scrollbar-marks-virt-…` — prior
- `xanatoo-254.*-justify-scrollbar-gutter-…` — prior
- `xanatoo-255.*-justify-quality-gutter-…` — prior
- `xanatoo-256.*-wrap-width-test-clip-…` — prior
- `xanatoo-257.*-fix-tests-images-link-…` — prior
- `xanatoo-258.*-fix-tests-stb-include-…` — prior
- `xanatoo-259.*-fix-wrap-test-warning-…` — prior
- `xanatoo-260.*-fix-scrollbar-clip-wrap-…` — prior
- `xanatoo-261.*-rewrite-paragraph-wrap-…` — prior
- `xanatoo-262.*-context-menu-keep-sel-…` — this delivery
