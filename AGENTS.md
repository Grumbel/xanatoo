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
| `app_passage.cpp` | the passage a view shows (`docs/PASSAGES.md`): selectors of the address are view state on `Pane`, `pane_address` / `pane_cut` / `apply_locator`; pure address parsing in `locator.hpp` |
| `app_chrome.cpp` | status line (`compute_status` is pure data → `draw_status_bar`), floating window frame, client scrollbar |
| `app_windows.cpp` | floating windows (`FloatWindow`, role = Help/Outline/Backlinks/Sitemap/Page/History/Styles/Document): one z-stack, shared hit-test / drag / resize / scroll / draw |
| `app_softwin.cpp` / `dock_slices.hpp` | docked panel geometry: adjacent panels in a side column or top/bottom band have persisted relative `dock_share` sizes; drag the separator between them to resize, independently of the dock's outer width/height handle |
| `search.cpp` | search engines (built-in table + user `search.sexp`), the `search:<engine>:<query>` address, query encoding, Ctrl+L keywords — pure, `search.hpp`, `docs/SEARCH.md` |
| `search_results.cpp` | JSON search engines → Markdown result documents (`search_results_markdown`, pure; `search_fetch` needs HTTPS): wikipedia / hn / archive / gutendex / lemmy / bluesky mappers; link text is literal in our parser, only brackets are changed |
| `app_search.cpp` | search bar state (`App::search`, engine = `ViewSettings::search_engine`): show / toggle / select engine / `run_search`; the ImGui row is in `ui.cpp` |
| `app_softwin.cpp` | window placement: `Dock` (None / Left / Right / Max) for every role — `dock_window` / `undock_window` / `toggle_maximize`, `place_windows` derives docked geometry, `dock_column_w` is what `layout_panes` leaves out |
| `app_touch.cpp` | touchscreen gestures: tap → click, drag/fling scroll, long-press select, pane text zoom and Lightbox image zoom |
| `app_buffers.cpp` / `app_view.cpp` / `app_nav.cpp` / `app_softwin.cpp` / `app_prompts.cpp` / `app_pointer.cpp` | document buffers, scroll/fold/outline, URL history + bookmarks, floating windows, minibuffer, cursor/gamepad |
| `app_fetch.cpp` | background page fetches (`FetchJob` on a worker thread → `poll_fetches`), page cache, loading indicator |
| `app_images.cpp` | remote (http/https) images: worker downloads of what `ImageCache` queued → `cache_dir()/images/`, relayout on completion and on finished thumbnails (`poll_images`); cache pruning at startup |
| `images_thumtoo.cpp` | `image_backend.hpp` over vendored thumtoo (`XANATOO_THUMTOO`): libvips decode, thumbnails composed from the shared tile cache |
| `app_collection.cpp` | collection view state (layout / size / sort), card clicks, fullscreen `Lightbox` viewer |
| `app_drafts.cpp` | unsaved edits → drafts in the revision store (`revstore.hpp`): autosave, restore on load (`attach_draft`), save / revert, `drafts:` page |
| `app_about.cpp` | `about:` page (Help → About): services, document formats, images (thumtoo / libvips loaders, MuPDF / DjVu) and libraries of this build, gathered from the feature switches and runtime versions; `about_markdown` (`about.hpp`) formats it. New services and user-visible optional deps get an entry there |
| `app_report.cpp` | startup report on stderr (gamepkgstd `LOGGING.md`): version / build / compiler, features, system, SDL / loader env, each library's runtime vs built-against version and the mapped file, video, renderer + GL strings, controllers with the mapping SDL applies, directories. New libraries get a line there too; choices made while loading (fonts, icons, style, ImGui render path) are logged where they are made, as `topic: details` on stderr |
| `app_footnotes.cpp` | footnote overlay: notes of on-screen `FootnoteRef` marks laid out in a strip at the pane bottom (`FootnoteOverlay` per pane), click jumps to the note |
| `app_page.cpp` | Page panel (`WindowRole::Page`, `PagePanel`, Ctrl+Alt+P): navigation / sidebar Chrome (`margin_chrome`, found at any depth) of the focused page as link rows in a dockable tool window; while it is open the panes leave those regions out (`LayoutEngine::set_hide_panel_chrome`, set by `update_page_panel`) |
| `app_events.cpp` / `app_windows.cpp` / `ui.cpp` | Reply panel (`WindowRole::Reply`): right-click a semantic Message card → Reply…; `App::reply` holds the target and Markdown draft, the ImGui multiline editor overlays the custom dockable frame, and `app_frame.cpp` paints an amber card wash/scrollbar mark. Mock only; never posts or handles accounts (`docs/REPLIES.md`) |
| `app_revisions.cpp` / `revstore.cpp` | Document Revisions panel (`WindowRole::Revisions`): native dockable panel shows separate working/published histories with snapshot preview, bounded line diff and conflict-checked restore. Restoring appends a new working child, never mutates the original/import source or publication pointer (`docs/REVISIONS.md`) |
| `app_windows.cpp` / `ui.cpp` | Annotations panel (`WindowRole::Annotations`): **native** dockable `FloatWindow`, not a standalone ImGui window. ImGui only supplies the rich editing widgets clipped to its client area, as with Reply; annotation revisions remain independent of document history |
| `app_windows.cpp` / `ui.cpp` | Bookmarks panel (`WindowRole::Bookmarks`): native dockable `FloatWindow`, not a standalone ImGui window; the bookmark list and actions are ImGui widgets inside its client. Preferences remains a separate settings dialog, not a dockable work panel |
| `ui_panel_layers.hpp` / `src/ui_panel_imgui_order.hpp` / `ui.cpp` | Embedded ImGui panels are classified by exact root/child names, drawn after their native FloatWindow, and reordered for ImGui hit-testing in `float_stack` order. Native foreground frames mask obscured editor mouse events; popups/menus/dialogs remain foreground. Old SDL2 fallback renderer keeps one full UI pass. See `docs/UI.md` |
| `app_sitemap.cpp` | Sitemap panel (`WindowRole::Sitemap`, `SitemapPanel`): site tree of the focused page's host, one `SiteCache` per host loaded on a worker thread; "Page" opens `sitemap:host` |
| `app_downloads.cpp` / `download.cpp` | Downloads panel (`WindowRole::Downloads`): MIME/attachment routing, streaming files, live progress/cancel, and persisted completed-file history; `https_get_follow_stream` decodes HTTP transfer chunks without buffering binaries |
| `http_url.cpp` / `https.cpp` | Scheme-preserving URL parsing/redirect resolution plus HTTP/1.1 over plain TCP (`http://`) or TLS (`https://`); historical `https_*` entry points accept both; parser is tested without network dependencies |
| `reddit_credentials.cpp` | `reddit.conf` in `config_dir()` holds optional app-only OAuth credentials with private Unix permissions; `XANATOO_REDDIT_CLIENT_ID` and `XANATOO_REDDIT_CLIENT_SECRET` override only as a complete pair, read on each fetch; Edit → Preferences provides masked editing, save, clear and official signup links (see `docs/REDDIT.md`) |
| `app_pages.cpp` | paged reading (`docs/PAGED.md`): `Pane::paged` / `PageView`, `paginate_pane` (called by `layout_panes`, cuts the layout with `paginate.hpp`), `page_turn`, page bar + edge zones + column rules (`draw_page_chrome`, `page_pointer_*`); while `pages.on`, `scroll_y` is a multiple of `stride`, `Pane::h` leaves out the bar, `sb_reserve` is 0 — use `pane_max_scroll`, `spread_at`, `page_goto_spread`, never pixel scrolling |
| `math_parse.cpp` / `math_layout.cpp` / `math_font.cpp` | TeX math (`docs/MATH.md`): `parse_math` (TeX subset → `MathNode`), `layout_math` (TeXbook App. G on a `MathFace`, em units, y up) and the FreeType `MathFont` (MATH table, glyph rasters by glyph id, `draw_math`); `FontSet::math_font()` finds the font on first use. A formula is an `InlineKind::Math` span (`src` = TeX, `display`) laid out as an atom by `layout_paragraph` (`PositionedMath`); never draw math through `Font`. Pure parts are unit-tested with a fake `MathFace` |
| `app_split.cpp` | Trail-mode linked navigation: first-tile links open the second, second-tile links advance the trail; hover previews (`SplitPreview`, only with `ViewSettings::hover_preview`, off by default) |
| `theme.cpp` | classic palette (`Palette`) + stateless bevel/gradient/window primitives shared with the ImGui theme |
| `style.cpp` | document style sheets (`styles/*.xss`, S-expressions): `DocStyle` roles → layout spacing/fonts/glyph colours + renderer decoration colours; `apply_style` in `app_view.cpp` |

Document views: `App::panes[kMaxViews]` — slots 0 / 1 are the centre
(split), document windows own a slot from `kCentreViews` up
(`FloatWindow::view`, `alloc_view` / `release_view` / `close_doc_window`).
Every view has its own rect (`Pane::x/y/w/h`, centre rows from
`place_centre_rows`, windows from `place_windows`), so the whole pane path
(layout, raster, caret, editing, overlays, footnotes) serves windows too.
Centre tiling currently has two slots: `App::tile_axis` chooses columns or
rows; `App::tile_mode` chooses independent or linked Trail. Compare is a
diff-marked independent pair (`App::diff_mode`), with a wider mode-coloured
divider and clickable badge. The divider's right-click menu and badge are
handled by `open_tile_menu`; `tile_divider_rect` and `tile_mode_button_rect`
are shared by hit-testing and drawing. This is the two-leaf foundation for
the planned arbitrary tiling tree (`docs/TILING.md`). Session save/restore
persists axis and relation, with old split sessions mapped to Trail.
The pure recursive geometry/split/close model is `tile_tree.hpp`: it is
unit-tested without SDL, and `place_centre_rows` uses its two-leaf adapter.
Do not equate its capacity for N leaves with N-view application support:
navigation, hit-testing, windows and sessions still assume centre slots 0/1.
Compare dividers draw the two-lane overview from `App::diff_overview`;
`diff_overview_positions` (`diff_overview.hpp`, pure C++20) derives markers
from document-tree order rather than the virtualized visible layout.
`tile_diff_track_rect` is the common hit/draw geometry; dragging the track
scrolls both panes to a normalized position, not an exact aligned hunk.
`app.focus` may be any live slot; tabs act on `app.centre_focus`. Use
`focus_view`, `live_views` (never `pi < 2`), `other_pane` (never
`1 - app.focus`), `pane_at(app, x, y)` and `content_y(app, y, pi)`;
after replacing a buffer's document call `refresh_views_of_buf`.
To change a page without moving the view, call `keep_views(app)` before the
change (it needs the old layout): the top line of each view stays on its
spot through the next `layout_panes`. To hold another spot, set
`Pane::view_anchor` from `view_anchor_at` first (pinch: the text between the
fingers). Pane width changes do this by themselves.
An address is `base//selector#selector` (`locator.hpp`, `docs/PASSAGES.md`):
pipes cut the view to a passage, `#` marks one in the whole page. Only the base
reaches buffers, caches and fetches. Use `pane_address` for what a view shows,
never `path_of`, where the address is shown or stored.
Back / Forward are per view (`Pane::nav`): `NavHistoryEntry` keeps the address,
caret, scroll offset, and source link. Record a shown page with
`push_nav_history(app, pi, url)`, step with `nav_step`; `sync_nav_history`
captures the current position and completes deferred restores after fetching.
`forward_nav_link` feeds the non-document link wash in `draw_pane_overlays`;
link clicks pass their `NavLink` through `navigate_to_url`; same-document
anchors use `navigate_anchor_link` so Back can return to the source. `App::visited`
is only the session log for the History panel. Only in Trail mode do the
centre panes share one trail instead (`App::split_history`, levels = left
page + pages opened from it on the right): `push_nav_history` forwards to
it and `nav_step` / `can_nav_step` walk it (`app_split.cpp`). The split trail
already tracks scroll/selection; `split_history_forward_href` identifies its
next link for the same wash.

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

Keyboard focus: `App::key_focus` (`KeyFocus::Document/Outline/Backlinks/Sitemap/Page/Styles`).
Panels handle plain keys only while `panel_has_keys(app, …)`; opening or
clicking a panel focuses it, a pane click / jump / unused plain key returns
focus to the document. Never let a panel consume keys just because it is open.

Context menus: `ui.cpp` keeps page-content actions separate from View/Panels
controls. The location `InputText` has clipboard/address/bookmark actions;
Back/Forward buttons show nearest-first targets derived from the focused
pane's `NavHistory`, or `SplitHistory` when split browsing. `NavJump` uses a
signed offset and must preserve the destination's caret/scroll state.

Windows place one way regardless of role: Left / Right docks are side
columns that take width from the panes (stacked by `dock_seq`), Top /
Bottom docks are bands between the columns that take height (side by side
by `dock_seq`; `dock_band_h`, used by `place_centre_rows`). Dock neighbours
are sized by `FloatWindow::dock_share`, not forced equal; shared separator
dragging updates only the adjacent pair. The size per
side is `App::dock_w[4]` (L, R, T, B). Max overlays the client area; every
frame has a maximize / restore box left of the close box.
Tabs ↔ windows: dragging a tab past the strip (ImGui, `UiCommand::TearTab`
→ `tear_tab`) opens its buffer in a window (docked at a zone), dragging a
window title onto the strip (`over_tab_strip`) runs `pop_in_soft`; tab
reorder is ImGui's own (ids = buffer ids, `ReorderTabs` copies the order
into `tab_order`). Never add a per-panel
"docked" flag; set `FloatWindow::dock` through the helpers above.
See `docs/BRAINSTORM.md` "Views, arrangement, documents" for where tabs /
split / windows are heading.

Rules: no behaviour on data types; new modal state goes in `App` (group
related fields into small records like `FloatPanel` / `Hover`); menus,
toolbar and shortcuts should route through `execute_command`.
Mouse cursor feedback must use the same hit-test as the action it advertises:
`float_resize_at` for native panel edges, `dock_split_at` for neighbours,
and `tile_divider_rect` for centre tiles. ImGui handles its own controls;
the SDL2 backend must detect external cursor changes to restore its I-beam.

## Versioning

- gamepkgstd `VERSIONING.md` (`~/projects/gamepkgstd`): the top-level
  `VERSION` file is the only place the version is written; on master it
  always ends in `-dev`. Nix builds report `<VERSION>.<revCount>+g<rev>`
  through `-DPROJECT_VERSION_FULL` → `XANATOO_VERSION` (`--version`,
  `about:`, Android `versionName`). Never hard-code a version elsewhere.

## Directories (`paths.hpp`)

- Never build a user path by hand: `config_dir()` (view.conf),
  `state_dir()` (revstore, sidestore, bookmarks, history),
  `user_data_dir()` (user `styles/`), `cache_dir()`; each may be empty
  (no usable dir, error already printed) — skip the write then. Call
  `ensure_dir` before writing; write small files with `write_file_atomic`
  (util.hpp) and write nothing config / state when `no_save()`.
- Installed data: `data_dir()` / `data_file("examples/x.md")` (from
  `--data-dir`, `<exe>/../share/xanatoo`, the install prefix; Android
  sets it to the unpacked assets). Never the cwd, no env variables.
  The build tree has `share/xanatoo/` with symlinks into the source.
- Windows and Emscripten default to `XANATOO_MINIMAL_PORT=ON` (offline
  SDL2/FreeType builds without FontConfig, SQLite, TLS clients or thumtoo).
  The web main loop uses `emscripten_set_main_loop_arg` and preloaded `/data`;
  Windows paths come from SDL. Maintain the feature guards and bundled font
  fallback. The authoritative cross-target packages are in `flake.nix` with
  `nix/windows.nix` and `nix/wasm.nix`. GitHub must build these exact flake
  outputs, never an independent CMake recipe. Build and limitations:
  `docs/PORTS.md`.
- SDL3 transition (in progress): Win32/Win64 flake packages use nixpkgs'
  SDL2-compatible API over an SDL3 runtime, explicitly packaging SDL3.dll.
  The app and vendored ImGui still compile against SDL2 headers; WASM,
  Android, and the default R36S port currently retain SDL2. Do not change
  their ABI until the SDL3 API/backend migration and platform tests are
  complete. First-party render, window, surface, time, mouse, input events,
  gamepads, cursor and SDL diagnostic operations now use `xanatoo/sdl.hpp`
  (switch `XANATOO_SDL3`); `checks.sdl3-api` compiles this
  bridge with real SDL3 headers. `-DXANATOO_SDL3=ON` selects an experimental
  first-party ImGui platform/renderer backend (`src/imgui_sdl3.cpp`) and the
  flake's separate `xanatoo-sdl3-native` package. `nix/r36s.nix` also has an
  experimental SDL3 KMS/DRM build and alternative PortMaster package that
  ships `libSDL3.so.0`; keep the SDL2 package as the default. Do not switch
  production packages until native SDL3 builds and runtime tests pass.
- New options go into `cli.cpp`, `print_usage` and `docs/xanatoo.1`
  (`checks.man` / `checks.help`).
- Version strings: `version_string()`; network User-Agents via
  `user_agent("purpose")` (`version.hpp`).

## Tech stack (current)

- C++20
- SDL2 (window, events, renderer)
- FreeType (glyph rasterization)
- FontConfig (styled faces)
- Dear ImGui (menu bar / toolbar; vendored under external/imgui)
- CMake + Nix flake

## Shared network helpers

- `include/xanatoo/tls.hpp` — `TlsStream` (connect over a socket fd with SNI, peer certificate SHA-256, write_all / read / pending; owns the fd). Backends `src/tls_openssl.cpp` (desktop default) and `src/tls_mbedtls.cpp` (Android; `-DXANATOO_TLS=mbedtls`). Certificate-chain/hostname verification defaults ON; Gemini explicitly opts out of CA verification and enforces `gemini_tofu.cpp` pins before sending URLs. Never call OpenSSL / Mbed TLS directly elsewhere.
- `TlsStream::peer_certificate_details()` supplies printable subject/issuer,
  UTC validity dates and self-issuance/self-signature status for Gemini trust
  review. OpenSSL checks the actual self-signature; Mbed TLS reports unknown
  until that signature can be verified portably. Certificate names are untrusted
  text: do not parse them as Markdown or conflate matching DNs with a valid
  cryptographic self-signature.
- `include/xanatoo/https.hpp` + `src/https.cpp` — HTTPS GET/POST over `TlsStream` (chunked bodies decoded; `https_get_follow` follows redirects without HTTPS→HTTP downgrade) plus `tcp_connect_host` for Gemini / Nostr (built when any network feature is ON). Keep NIP-11 HTTPS-only/public-only/256 KiB limits independent of ordinary verified HTTPS browsing. See `docs/TLS.md`.
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
- Always commit your working changes when a task is done (no need to ask); never leave finished work uncommitted.
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
  one `libimagedeps.a`; `nix build .#imagedeps-android`.
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
Work is committed directly to git. Git bundles are only for Web chat
handovers: when running in Web chat, deliver cumulative bundles from the
original `568a91b` base through the current `HEAD`, and keep the next bundle
number in `TODO.md`. When running as CLI, do not create bundles; the bundle
number is reserved for the next Web chat handover. Never create a parallel
history or distribute patch files.

## Hacker News

- Read-only (`src/hackernews.cpp`), gated by `XANATOO_HN` (default ON).
- Lists (`/`, `/news`, `/newest`, `/best`, `/ask`, `/show`, `?p=N`): ranked
  ids from the Firebase API + one Algolia search for titles / scores.
- Items (`/item?id=N`): whole tree in one Algolia `/items/N` request; top
  level re-ordered by the Firebase item's `kids` (HN rank).
- Lists contain semantic `Article` entries: `Header` for the linked story
  title (source hostname is an inline `Metadata` span on the same line),
  `Caption` for rank / points / author / time / discussion link. Item pages
  remain `Message` threads. `styles/hackernews.xss` controls sizes and colours;
  do not add site-specific geometry to the layout engine.
- `hn_append_html` maps comment HTML → Paragraph / BlockQuote (`> …`) /
  CodeBlock with Link / Emphasis spans.
- `hn:user?id=NAME` fetches the official Firebase user record (case-sensitive)
  and presents karma, join time and profile text. Links from author names
  resolve here; `rel=external` marks a deliberately web-only link.
- Message header link slots are semantic annotations on `Message.spans`:
  `0` author, `1` date (plus `datetime`), `2` reply-to `#id:PARENT`.
  Layout reconstructs the visible header and preserves those annotations;
  do not hard-code HN URLs into layout. In item pages, the date opens that
  comment's own `hn:item?id=N` view. Right-clicking a comment offers both
  `hn:item?id=STORY#id:COMMENT` and the web permalink for sharing.
- `InlineSpan::datetime` is a generic ISO 8601 instant also populated from
  HTML `<time datetime>`. `hit_test_timestamp` works independently of links;
  `time.cpp` formats UTC, local and relative hover lines. Keep this semantic
  annotation separate from style-sheet roles.

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
- `HtmlOptions::images` (web pages, feeds): block-level `<img>` → Image
  node (remote ones load like any remote image; lazy `data-src` / `srcset`
  via `html_img_src`); icons (declared edge ≤ 48) and images inside
  quotes / list items / cells → inline image spans (`data:` ones stay
  `[image: alt]` links).
- `HtmlOptions::asides` (the main content of web pages): `<aside>` → Aside
  box; elsewhere, or with chrome classes, it is page chrome.
- `chrome_nodes` (with `drop_chrome`): chrome blocks become Chrome nodes
  (`html_append_chrome`); inside running text, list items or cells they are
  dropped. EPUB chapters pass `keep_chrome = false` to `parse_html_page`.
- Used by feeds and Hacker News (`gt_quotes` for HN's "> " convention)
  and web pages (page-mode options below); also `html_to_plain`,
  `html_attr`, `resolve_url`, `html_element_end`.

## Tables

- `TableCell::col_span` / `row_span` are occupied column and row counts;
  `build_table_grid` (`table_grid.hpp`) derives slots and placements without
  duplicating cells. Spanning tables are laid out table-wide; borders stop at
  merged cells and a complete unspanned row supplies column weights.
- HTML `rowspan=0` resolves at its row-group boundary. Markdown uses
  `:::cell` / `:::th` with `colspan=` / `rowspan=`; S-expressions use
  `:colspan` / `:rowspan`. Enter inside a cell extends spans crossing the
  inserted row. Details: `docs/TABLES.md`.

## Web pages (`html_page.hpp`)

- `src/html_page.cpp`, gated by `XANATOO_WEB` (default ON). Any http(s)
  URL that no other service claims and that `is_web_url` accepts (not an
  obvious download) opens in-app; it is checked last in dispatch.
- `parse_html_page`: main content (`<main>`, lone `<article>`, `<body>`)
  through `html_min` page mode (`sections`, `anchors`, `drop_chrome`,
  `chrome_nodes`); also used for local `.html` files (`parse_document`
  dispatch). HTML is read-only: Save asks for a `.md` name.
- Page chrome (nav / aside / footer / form / dialog, landmark roles, navbox /
  share / cookie classes, what lies around `<main>`) is kept as
  `NodeKind::Chrome` nodes (`summary` = what it is); hidden / sr-only text is
  always dropped. Reader mode is view state (`Pane::reader`, `pane_reader` /
  `set_reader`, default `ViewSettings::reader_mode`): layout leaves Chrome out
  (`set_hide_chrome`) and draws the "… hidden · Show all" notice
  (`LayoutResult::chrome_notice`). Never put reader mode into the address or
  parse a page twice for it (docs/PASSAGES.md "Reader mode").
- `web_fetch` dispatches on Content-Type; bodies go through
  `detect_charset` / `to_utf8` (util.hpp) first.
- `external:<url>` hrefs force the system browser (`open_external_url`).
- Foreign content is read-only: `is_foreign_path` (remote URL, `.html`,
  any `is_pandoc_path` file, anything under `cache_dir()`) makes `browsing(app)` true.
  Gate document edits on `browsing(app)`, never on `app.browse_mode`
  alone (that is only the user's global toggle).

## Inline images and asides

- `InlineKind::Image` spans (`inline_image()`): `text` is U+FFFC so the
  caret sees one character; `src` / `alt` / `width` / `height` on the span,
  `href` = optional link. Layout places them as atoms on the baseline
  (`PositionedImage::in_text`; never resizable, a click follows the link or
  opens the viewer). Markdown `![a](s){width=… height=…}` inside text.
- `NodeKind::Aside`: a container of blocks, a side box (style role
  `aside`: `width`, `side`, `placement`, `indent` = padding, small `font`,
  muted `color`). `placement margin` (default) uses existing page margin
  beside its flow position (`LayoutEngine::margins_`, set up per layout()
  by `setup_margins`, which never narrows the text column;
  `layout_margin_box`); without margin room it floats inside the column
  if readable, otherwise it uses a full-width flow block.
  `placement float` is the HTML-compatible float: floats live in
  `LayoutEngine::floats_` for one layout() call, text lines take the band
  `float_band` leaves free, boxed blocks (`clears_floats`) start below.
  Never let a block that draws a full-width fill run under a float.
- Side box text uses `LayoutEngine::small_` (`FontRole::Small*`,
  `small_role`) and `ctx_role_` (the box role's text colour); never
  hard-code a smaller size or colour for margin content.
- No general box model: new floating things (figures, `align=right`
  tables) go through the same `FloatBox` list, not per-kind geometry.

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
- Without pandoc (Android), `.epub` goes through `epub_to_markdown`
  (`src/epub_native.cpp`: zip + OPF spine → `parse_html_page` → Markdown).
- Pandoc misbehaviour and our workarounds (Lua filter, post-passes) are
  collected in `docs/PANDOC.md`; add new findings there.

## Directory (`examples/directory.md`)

- A hand-written link directory (categories under `examples/directory/`):
  feeds, native refs (`hn:` / `reddit:` / `lemmy:` / `wikipedia:`) and plain
  pages. Check a new URL with curl before adding it; use the `feed:` prefix
  where `is_feed_url` cannot tell it is a feed. Sub-pages live in the
  subdirectory so the Examples-menu test only lists the front page.

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
- Relay events and encountered public keys now persist in `nostr.sqlite` via
  `nostr_store.cpp` when SQLite is enabled (no writes under `--no-save`). Events
  are deduplicated by id; distinct source relays, `p` tag/NIP-19 mentioned
  keys, and newest observed kind-0 metadata are indexed. `nostr_verify.cpp`
  reconstructs NIP-01 event IDs; OpenSSL builds additionally verify BIP-340
  Schnorr signatures. Other TLS backends cache **hash-checked, unsigned-status**
  observations only; historic pre-verifier records are preserved as excluded
  `verification=0` rows and require refetch. The UI distinguishes these cases.
  Claimed NIP-05 names are NOT verified. Do not portray the cache as a complete
  or uniformly authenticated Nostr directory. See `docs/NOSTR.md`.
- Two native dockable `FloatWindow` panels are exposed in Panels: **Nostr
  Explorer** searches local events/people and shows observed counts; **Nostr
  Accounts** manages multiple named *watch-only* public keys. They use ImGui
  only as controls within the native client frame. Explorer also has a NIP-29
  Groups tab, with `(relay,group ID)` identity, `kind:39000` metadata and
  `h`-tagged event counts. `nostr:groups@wss://relay` discovers public group
  metadata; clicking a group opens its relay-specific timeline. NIP-11 relay
  master-key binding, membership and private groups remain unimplemented.
  No signing-capable accounts, private-key input or outbound publication exist
  yet; add NIP-46/NIP-49 key management before offering actual identity
  creation or posting.
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
- Links to images (`is_image_url`: http(s) or local path by extension)
  open the lightbox over every image link of the page (`open_link_image`,
  checked before split / external handling), never the system browser.
- No posting or secret-key management in v1. Publication transport options
  (WebDAV, static export, Nostr, IPFS) are sketched in `docs/PUBLISHING.md`.

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
- After the TLS handshake, unknown host:port leaf SHA-256 fingerprints must be
  reviewed and explicitly trusted before sending any Gemini URL or saving a
  pin. Changed certificates also require explicit review. Review state goes
  from the fetch worker to a foreground prompt; approval is compare-and-swap
  against saved/session trust and the retry forces a fresh TLS handshake.
  `--no-save` approvals stay in memory. See `docs/TLS.md` and the pure
  `gemini_tofu_tests.cpp` suite; do not bypass this with another fetch path.

## Reddit

- Read-only client via public `https://www.reddit.com/…/.json` (OpenSSL HTTPS).
- Gated by `XANATOO_REDDIT` (default ON; needs OpenSSL).
- `reddit:r/<sub>/…` refs (the reddit.com path); https links open as web pages.
- Maps posts/comments to Thread + Message nodes.
- Optional OAuth: `XANATOO_REDDIT_CLIENT_ID` + `XANATOO_REDDIT_CLIENT_SECRET` (script app) when anonymous `.json` returns 403.
- Optional UA override: `XANATOO_REDDIT_USER_AGENT`.
- When a native `reddit:` fetch receives HTTP 403, the error document offers a
  clickable `reddit_rss_url` fallback. It goes through the normal RSS/Atom
  reader, without silently retrying another endpoint. Reddit announced that
  its first-party RSS feeds will end on 2026-11-13.

## Lemmy

- Read-only client via Lemmy **API v3** JSON (`/api/v3/post/list`, `/api/v3/post`).
- Gated by `XANATOO_LEMMY` (default ON; needs OpenSSL).
- `lemmy:<instance>/c/<community>`, `/u/<person>` and `/post/<id>` refs.
- Maps posts/comments to Thread + Message nodes.
- **Lemmy Explorer** in Panels is a native, dockable `FloatWindow` with an
  embedded ImGui form (not a separate ImGui window). It offers locally
  discovered Instances, Communities, People and Posts; profile/community/post
  navigation stays native. Search and sort/filter controls are per-panel.
- `lemmy_discovery.cpp` parses successful API v3 JSON responses without I/O;
  `lemmy_directory.cpp` merges bounded, thread-safe observations and persists
  them in `state_dir()/lemmy-directory.txt` (in-memory only under `--no-save`).
  API lists, search results, profile posts and comment authors populate the
  catalogue. Federated peers come from `/api/v3/federated_instances`;
  blocked hosts are omitted, linked/allowed peers retained. There is a small
  built-in seed directory and an Add instance control; no central service or
  account is necessary. Only valid ASCII hostnames are stored.
- API v3 is the existing network client's protocol; Lemmy 1.0 introduced API
  v4 with different endpoints. v4-only servers require a later client update.
- Optional UA override: `XANATOO_LEMMY_USER_AGENT`.
