# TODO — xanatoo

> **Rename (2026-10-10):** project moved from `glyphtoo` to **xanatoo** (`https://github.com/Grumbel/xanatoo.git`). Namespace, includes, binary, env vars (`XANATOO_FONT`), and docs updated. Historical bundle names below still use the `glyphtoo-NNN` prefix from before the rename.

## Current tip

Base commit of this work line: `568a91b` (Initial checkin of PROPOSAL.md)

Next bundle number: **348** (this delivery is **347**)

## Status (2026-11-11) — Search button in the location bar

- Magnifier beside the address when an engine searches the focused page's
  service (`engine_for_address`, `SearchEngine::sites`; `(site …)` in
  search.sexp). Click → search bar with that engine (`UiCommand::SearchSite`),
  latched while it shows that engine; a second click hides it. Unit-tested;
  checked under Xvfb on `hn:news`.
- Open: the icon is the same `find` magnifier as Find in page on the right of
  the toolbar; a distinct "search service" icon would help.

## Status (2026-11-11) — PDF, DjVu, CBZ / CBR pages

- `docs/FIXED_LAYOUT.md`: fixed-layout files open as ordinary documents of
  `NodeKind::Page` nodes (thumtoo page URI + real size), grouped into Sections
  from the PDF / DjVu outline; archive members in natural name order. Pages
  serialize in Markdown (`:::page`), S-expressions and XML; style role `page`.
- Sizes only from thumtoo (never guessed): opening waits until every page is
  sized; unreadable pages become a notice paragraph.
- Tiles via thumtoo's LOD layer (`app_pagetiles.cpp`), drawn over the raster
  each frame; paged mode starts every sheet on a new screen page.
- Checked under Xvfb with thumtoo's hello.pdf / pages.djvu fixtures and a
  3-page CBZ (scrolling and F8 paged reading). Unit tests for the pure parts.
- Web PDF / DjVu / comics open as pages too (streamed to
  `cache_dir()/documents/`, attachment or not); right-click on a link has
  Open Link in New Tab / Copy Link Address / Download Link (forces Downloads).
  Checked under Xvfb with arxiv.org/pdf/1706.03762 (15 pages, 22 outline
  headings) and Download Link into a scratch XDG_DOWNLOAD_DIR. CopyText was
  missing from the browse-mode command list (bookmark / HN copy items).
- Open: see "Open items" in `docs/FIXED_LAYOUT.md` (CBZ spread splitting,
  `#page:N`, zoom modes, progressive loading, text layers / links).

## Status (2026-11-11) — Internet Archive (`ia:`)

- `src/archive.cpp` (`docs/ARCHIVE.md`): `ia:ID` items (details, description,
  files, mpv link for movies / audio) and collections (members, paged) from
  the `/metadata` and `advancedsearch` JSON APIs, via Markdown (`ia_markdown`).
  The `ia` search engine and `archive.org/details|download|stream|embed/ID`
  (native toggle) map to it. Live-checked mapper output against a Gutenberg
  item and the Prelinger collection; the in-app GUI path was not run.
- Open: reviews, Wayback Machine, full-text viewer; lending-only items show
  metadata but their files will not download.

## Status (2026-11-11) — Clickable web addresses

- `ViewSettings::auto_links` (`auto_links=` in view.conf, default on; View menu
  and Edit → Preferences "Make web addresses clickable", `UiCommand::ToggleAutoLinks`).
  Display-time only: `layout_paragraph` runs `find_urls` (`autolink.hpp`, pure,
  `tests/autolink_tests.cpp`) over Text/Emphasis/Strong/Strike runs (not code)
  and lays the characters out as Link. The span stays Text, so carets and
  editing are untouched; the target lives in `LayoutResult::auto_links`,
  referenced by `PositionedGlyph::auto_link` (also `LinkHit`, `Hover::link_auto`).
- Not covered: URLs split over several spans (e.g. bold half), `www.` without
  scheme, link-aware features reading `span.href` (Page panel, backlinks).
- Not run: GUI check (this shell was outside `nix develop`, link step failed).

## Status (2026-11-11) — video sites (`peertube:` / `odysee:` / `dailymotion:`)

- `src/video_sites.cpp`, `XANATOO_VIDEO` (default ON, TLS). Listings,
  channels, search, video pages with comments and replies, Watch / mpv
  links; see `docs/VIDEO_SITES.md`. Live-checked against video.blender.org,
  framatube.org, sepiasearch.org, Odysee, Dailymotion.
- Open: Dailymotion comments unverified on a commented video; Odysee
  comment sort; Piped / Rumble / Vimeo / Bilibili not done.

## Status (2026-11-11) — YouTube via InnerTube (`youtube:` refs)

- `src/youtube.cpp`, `XANATOO_YOUTUBE` (default ON, needs TLS): read-only
  client of the unofficial InnerTube JSON API (no key, no account).
  `youtube:@handle[?page=N]`, `channel/UC…`, `c/x`, `user/x`,
  `playlist?list=ID`, `watch?v=ID[&comments=N][&sort=new]`. Plain
  youtube.com URLs stay web pages; "Native view" maps them (`native.hpp`).
  Full docs: `docs/YOUTUBE.md`. Tests: `xanatoo_youtube_tests` (recorded
  shapes, pure parsers) + `json_dom.hpp`.
- Fixed on the way: `https_request` could not read the last response header,
  so a chunked body (YouTube POSTs) stayed chunk-encoded.
- Live-checked: channel pages 1-3, playlist, video + comments.
- Limits: comment replies are not fetched (counts only); tokens are
  re-walked per page (page N = N requests); Shorts / Live tabs, search and
  the video owner / description block are unverified or unsupported;
  InnerTube changes without notice.

## Status (2026-11-11) — Media RSS in feeds (YouTube)

- `feed.cpp` reads `media:thumbnail`, `media:community` (`media:starRating`,
  `media:statistics views`) from `<media:group>` or directly on an entry
  (Atom and RSS), plus `yt:videoId` / `yt:channelId`. Entries show the
  thumbnail as a left-floating remote Image (no alt, so no duplicate caption) and a muted "N views · ★ x/5 (n ratings)" line
  before the description. Unit test added in `test_parse_feeds`.
- Comments / channel lists: done below (InnerTube, 2026-11-11).

## Status (2026-11-10) — Android private UI header staging (bundle 347)

- Stage `src/*.hpp` alongside `src/*.cpp` in the Android NDK source tree.
  This includes `ui_panel_imgui_order.hpp`, which `ui.cpp` includes locally.
- Shell syntax and staged-header presence verified; Android compilation itself
  remains untested locally (Nix/NDK unavailable).

## Status (2026-11-10) — SDL2 R36S drop-position build fix (bundle 346)

- The SDL2 drop-position fallback called `mouse_position()` before its inline
  declaration in `sdl.hpp`, failing all R36S translation units that included
  `app.hpp`. Use SDL2's `SDL_GetMouseState(int*, int*)` directly in that branch;
  its returned coordinates are identical to the later compatibility wrapper.
  SDL3 continues to use the event's actual drop x/y coordinates.
- The existing `tests/sdl_api_compile.cpp` exercises `drop_position` on both
  SDL variants when their respective headers are available. Full R36S/Nix
  cross-compilation still requires the toolchain from the user's build host.

## Status (2026-11-10) — semantic heading tag vocabulary (bundle 345)

- The canonical title tag is now `<heading>` in XML, `(heading ...)` in
  S-expressions and `:::heading` in Markdown; stylesheets use `(role heading
  ...)`. Exported documents, supplied styles, examples, editor menu items,
  and documentation use the new spelling.
- Old `<header>`, `(header ...)`, `:::header`, and `(role header ...)` still
  parse as titles/roles for compatibility, but are never the canonical output.
  `header` and `footer` are reserved as layout concepts; HTML `<header>` stays
  page chrome, distinct from the semantic title node. Internal `Header` type
  names were left stable, since the legacy ranked `Heading` node also exists.
- Added XML and main-unit regression coverage for new and old spellings,
  unranked titles and numeric depth, and canonical round trips. XML tests
  and a standalone S-expression/Markdown smoke run pass; `parse.cpp` syntax
  check passes. Full unit/build tests require the desktop FreeType toolchain.

## Status (2026-11-10) — R36S OpenSSL compatibility (bundle 344)

- Use `SSL_get_peer_certificate` with the pre-3.0 OpenSSL headers in the R36S
  ArkOS sysroot, and `SSL_get1_peer_certificate` with OpenSSL 3.x; both return
  an owned X509 reference that must be released with `X509_free`. This fixes
  the two missing-symbol compile errors in `tls_openssl.cpp`.
- Avoid `const char* + std::string` temporary concatenations in
  `format_locator` and `up_url`, which triggered GCC 9 array-bounds warnings
  in the R36S build. Resulting URL/locator text is unchanged.
- OpenSSL 3 host TLS peer-certificate regression passed, and the affected
  URL/locator translation units passed syntax checks. R36S's old OpenSSL
  cross-build still needs verification with the Nix toolchain.

## Status (2026-11-10) — tab context menu spacing, edit-mode opt-in (bundle 343)

- Restored ordinary popup window padding, menu item spacing and frame padding
  inside the document tab's right-click menu. The tab strip itself remains
  compact; the context menu now matches the proportions of other popups.
- Inverted the global mode state: `App::edit_mode = false` means browsing is
  the default, and F2 / the toolbar's pencil / View → Edit Mode enables
  editing. The Edit Mode button is visibly pressed while on and disabled for
  foreign read-only pages. All document edit gates still use `browsing(app)`.
- Converted gamepad/F2 handlers to share the same mode command. Saving a
  foreign document as an editable local working copy enables editing, while
  Gemini navigation restores browsing. Updated help and toolbar icon fallback.
- Vendored headless ImGui panel regression, local URL suggestion test, and
  the complete SVG-to-PNG fallback rasterization passed. Full SDL/Nix build
  and visual inspection still require a desktop toolchain.

## Status (2026-11-10) — URL bar history and toolbar toggle polish (bundle 342)

- Location bar completion offers up to eight locally ranked results from open
  tabs, bookmarks and the persistent recent-URL history, with case-insensitive
  address/title matching, deduplication, Up/Down/Enter, mouse selection and
  Escape dismissal. Selecting a tab suggestion switches to that tab.
- Normal navigation is now recorded in the existing bounded URL minibuffer
  history, including persistence across restarts and `--no-save` handling.
  Suggestions do not send remote queries while typing.
- Split/Join and outlined/filled Bookmark toolbar controls now show their
  different icons **without** a permanently depressed button face. Browse Mode
  stays visibly pressed when active; toggles without alternate artwork retain
  their existing recessed states.
- Pure C++ suggestion unit tests cover matching, ranking, duplicate URLs and
  limits. Complete SDL/ImGui visual testing still requires a full desktop
  build environment.

## Status (2026-11-10) — latched toolbar toggles, icon states (bundle 341)

- Give `tool_button` an explicit latched state so active toolbar toggles draw
  a persistent depressed face instead of losing their colours to the common
  flat-toolbar style stack. Native view, reader/sidebar extras, split, trail,
  outline, history, downloads and browse all use the same treatment.
- Independent Split changes to a Join icon and tooltip while active; the
  current URL's Bookmark icon switches between an empty and filled ribbon.
  Reload has a clearer green opposing-arrows icon. SVG build-time rasterization
  and the no-SVG-renderer fallback both include the new graphics.

## Status (2026-11-10) — external URL drag-and-drop (bundle 340)

- Recognize SDL2/SDL3 **text drops** in addition to file drops, including
  browser URLs and `text/uri-list` values (comment lines and multiple links).
  Local `file://` paths are percent-decoded; non-navigation text and unsafe
  schemes are ignored. URL-looking file drops also use this navigation path.
- Route dropped URLs to the centre pane under the actual drop coordinates,
  or to a document FloatWindow, even in linked Trail mode. Dropping onto a
  utility panel does not navigate through it. SDL2 uses current mouse
  position because its drop event lacks coordinates; SDL3 uses event x/y.
- Existing ordinary file-window opening and image-at-caret insertion remain
  unchanged. Added a standalone C++ parser test to CTest and SDL API compile
  surface checks. Full desktop SDL/Nix end-to-end testing remains pending.

## Status (2026-11-10) — source/layout menus, window conversion, split joining (bundle 339)

- Added **View → View Source** to switch between rendered, raw,
  S-expression and XML representations of the loaded document. The current
  source mode is checked in the menu. Projection switches stay local to the
  view and avoid refetching draft/database/remote content.
- Moved page columns, collection view, tile arrangement, line spacing,
  margins, measure, justification and stylesheet selection into an always
  available **Layout** menu (`Alt+L`). The edit-only **Format** menu stays.
- Added per-tab context menus (**Select**, **Move to Window**, **Close**) and
  per-document-window title context menus (**Move Window to Tab**). Targeted
  move-back selects the clicked document window and excludes utility panels.
  Tab/window moves preserve source projections, navigation, selection,
  reader/paged settings and scroll state.
- Added **Swap Panes** and **Join Left/Right** to the tile divider menu
  (Top/Bottom labels when stacked). Joins retain the chosen view and cancel
  updates for the discarded pane. Active Trail previews are reverted first.
- Vendored ImGui panel regression and whitespace checks passed. Full desktop
  SDL/Nix build and pointer-interaction verification remain pending here.

## Status (2026-11-10) — active document tab emphasis

- Document tabs now have a distinct raised active state: pale white selected
  face, a 1px rounded outline, stronger dark text and a thin 2px amber
  overline echoing BeOS title tabs. Inactive tabs are darker with muted text;
  hover uses white rather than blue. Only the document strip opts into the
  outlined/overline tab style, keeping other ImGui panels unchanged.
- Preserved existing tab reordering, close buttons, icon clipping, tear-off,
  selection synchronization and the flat new-tab toolbar action. Visual
  verification still needs a host with the complete SDL/Nix toolchain.

## Status (2026-11-10) — white button hover highlights

- Standard ImGui panel buttons, flat toolbar buttons, and native raised buttons
  now use white hover highlights instead of pale blue. This includes native
  window controls and page-navigation buttons through the shared
  `draw_button_face` painter. The gradient retains the raised bevel; pressed
  feedback and non-button selection accents remain unchanged.
- Replaced the remaining blue/green `ImGuiCol_ButtonHovered` overrides in
  toggled toolbar calls with white. Selection and scrollbar-accent colours are
  unchanged. Full Nix/SDL visual verification remains pending.

## Status (2026-11-10) — raised toolbar hover correction

- Removed the blue hover background that made toolbar controls look selected
  or sunken. At rest they remain transparent; hovering gives a neutral light
  face, rounded frame, and the same inset top-left highlight / bottom-right
  shadow as raised panel buttons. Press reverses the inset bevel and darkens
  the face, making its state visually distinct from hover.
- The inset bevel helper is shared between ordinary always-raised buttons and
  hover-only toolbar controls, retaining the corner-safe geometry introduced
  in bundle 335. Full SDL/Nix visual verification is still pending.

## Status (2026-11-10) — Clearlooks toolbar/scrollbar follow-up

- Split toolbar and regular button drawing: toolbar icon/text buttons are
  borderless and transparent at rest, showing a rounded raised outline only
  on hover/press; selected URL actions retain a flat tinted indicator. Regular
  dialog/panel buttons stay raised with corner-safe inset bevel highlights,
  eliminating the square-edge overlays on ImGui's rounded button frames.
- The native full-height scrollbars use a dedicated cool blue-grey gradient
  thumb with a darker outline against a lighter trough, retaining their
  existing metadata markers and drag behavior. ImGui scrollbars use matching
  higher-contrast thumb colours for idle, hovered and active states.
- Vendored ImGui panel tests, pure scrollbar geometry tests and native theme
  syntax check passed. Full Nix/SDL build and visual inspection remain pending
  on a host with the complete dependencies.

## Status (2026-11-09) — Subscriptions panel

- Native dockable Subscriptions panel shares the Bookmarks window geometry,
  ImGui stacking, session restoration and Clearlooks-style controls. The
  Bookmarks menu can add/remove the current changing source. The panel accepts
  manually entered Lemmy communities/users, Nostr accounts/groups, RSS/Atom,
  Reddit and Hacker News lists, with Open, refresh one/all, mark read, remove
  confirmation and per-source refresh intervals.
- Reused the existing read-only background fetch path with a dedicated
  `FetchPurpose::Subscription`, so checks never navigate a pane or trigger a
  download. Checks are bounded to two concurrent jobs; automatic starts are
  throttled to one every five seconds. First successful check sets baseline;
  subsequent checks compare up to 256 stable post/event/permalink keys and
  retain unread counts until opened or marked read. Network errors preserve
  the previous baseline and show in the panel.
- Atomic persistence in `subscriptions.txt` tracks watched addresses, titles,
  intervals, checked timestamps, unread counts and latest item IDs. Separate
  pure C++ tests cover change counting, reordering, refresh scheduling,
  persistence and `--no-save` semantics. Full SDL/Nix builds and native panel
  visual tests remain pending on a NixOS host.

## Status (2026-11-09) — Clearlooks-style GUI polish

- Kept the BeOS yellow active title tabs, with a more coherent GNOME 2
  Clearlooks-inspired control palette: cool light grey, white highlights,
  blue hover/selection, thin dark outlines, and gently rounded raised native
  button surfaces. Native close and maximize/restore buttons are now matching
  20px squares centered in their unchanged 28px click zones, with hover
  feedback and clearer glyphs. Page bar and title actions share that face.
- The ImGui theme now has consistent button frames, standard-height actions
  (no zero-vertical-padding SmallButton variants), and 36px toolbar buttons
  whether an icon is available or the text fallback is used. URL-field icon
  buttons also have visible faces. Narrow style-manager action rows wrap.
- Removed pane scrollbar up/down arrows completely, including their mouse
  hit zones and hover state. The scrollbar track now occupies the full pane
  height. A shared pure geometry helper keeps thumb drawing, dragging and
  Shift+click targeting consistent, including short documents with a bottom
  reading pad. Section/search/bookmark/reply position ticks remain.
- Pure C++ scrollbar geometry and panel-layer tests passed. Full SDL/ImGui
  build and visual checks still require the NixOS development toolchain.

## Status (2026-11-09) — immediate theme selection and font picker layout

- Clicking a theme in the Style Sheets panel now applies it immediately;
  removed the redundant **Use this theme** button. Unsaved source edits
  still protect against switching away until saved or discarded, with an
  explicit message explaining why the list is locked.
- The body, heading, and code font fields now have separate labels above
  full-width inputs and font dropdowns, with shorter prompts and full-name
  tooltips. When docked too narrowly for the two-column manager layout,
  the theme list moves above the editor instead of clipping its controls.
- The pure `ui_panel_layers_tests` test passed. Full application/ImGui visual
  verification remains pending on a host with the SDL/FreeType toolchain.

## Status (2026-11-09) — style sheet manager and reading themes

- Added Editorial, Linen, Blue Hour and Forum styles, each with multiple
  installed-font fallbacks and more deliberate spacing, page backgrounds,
  text/link colours and comment-card hierarchy. The default, Reddit and
  Hacker News styles also separate comment cards more generously; Compact
  intentionally stays dense. Every shipped `.xss` parses as one document.
- Replaced the native Style Sheets panel's simple click list with an embedded,
  dockable ImGui manager. It selects themes, duplicates bundled templates into
  `$XDG_DATA_HOME/xanatoo/styles`, edits personal `.xss` source, inserts font
  rules from a searchable FontConfig family listing or typed family names, validates and
  atomically saves, and offers confirmed deletion. Unsaved source prevents
  accidental selection changes. Source edits remain fully user-owned.
- Exact-host/domain pins (including Lemmy instances) and service pins are
  stored in `style-pins.conf`. A domain pin wins over service-wide pins, then
  `view.conf` supplies the fallback. The active view's effective style follows
  navigation without rewriting the saved global default. The manager can pin
  the selected sheet for the current page and remove saved pins.
- Pure panel-layer compilation/run and a standalone `style.cpp` smoke test
  passed: all 16 shipped sheets loaded without warnings, and the personal
  sheet create/save/delete and persistent pin lifecycles worked. The main
  offline unit suite has matching regression tests. Full SDL/Nix/FontConfig
  compilation and visual checking remain pending because their development
  dependencies are absent here.

## Status (2026-11-09) — Nix TLS trust regression

- `tls_peer_tests` was already fully offline: it handshakes over a local Unix
  socketpair. Nix reported OpenSSL verification result 18 for the test's
  self-signed leaf, showing that the ephemeral PEM CA was not loaded into the
  client store. The OpenSSL backend now explicitly loads `SSL_CERT_FILE` (or
  `NIX_SSL_CERT_FILE` if unset), in addition to its default verification paths.
- Host `tls_peer_tests` passes with verified hostname, rejected hostname and
  untrusted certificate, and explicit unverified Gemini exceptions. A full
  network-disabled Nix build remains to be checked on NixOS.

## Status (2026-11-09) — Lemmy Explorer build fix

- Corrected `build_ui_state` to pass an explicit null diagnostic pointer to
  `remote_fetch_supported`, which requires two arguments. Previously this
  prevented `src/app_frame.cpp` from compiling in Lemmy-enabled builds.

## Status (2026-11-09) — Lemmy Explorer and instance discovery

- Panels → **Lemmy Explorer** is a native dockable/floating panel. Its embedded
  ImGui content participates in native draw-list and pointer z-order. Four
  tabs show observed servers, communities, people and posts. Local filtering,
  server-only view, feed sorting and opening discovered entries are supported;
  selected server, sort, tab and scope survive session restoration.
- A 16-instance starter directory, Add instance, and the official Lemmy
  directory link offer manual discovery; fetching a server's federated peer
  list records linked and allowed servers (not blocked ones). Successful
  community lists, post feeds, searches and user pages automatically populate
  the locally persistent directory without requiring an account. Public
  API-v3 read-only endpoints are reused and native person links open profiles.
- Pure Lemmy JSON discovery tests, `lemmy.cpp` and catalogue C++ compilations,
  native panel-layer tests and vendored ImGui child draw-list tests pass.
  A full SDL/Nix build and visual interaction testing remain pending here.
  Lemmy v4-only instances are not yet supported by this API-v3 client.

## Status (2026-11-09) — native/ImGui pointer stacking cleanup

- Embedded Reply/Bookmarks/Annotations/Nostr ImGui roots and nested children
  now follow `float_stack` for hit-testing as well as paint order. Native-only
  panels mask covered editors, while global popups and dialogs keep priority.
- Mouse-down, motion and wheel events are filtered before ImGui receives them
  if they land on a native foreground panel covering an embedded editor;
  mouse-up remains forwarded. Touch and cursor selection share the same test,
  and the gamepad pointer respects native occlusion.
- Pure C++ ownership tests and a live vendored-ImGui overlap/child stacking
  test pass. No SDL/Nix development tools are available here to build the
  complete app; desktop interaction testing is still required.

## Status (2026-11-09) — Gemini certificate type include fix

- `app.hpp` owns an `std::optional<TlsPeerCertificate>` in the background
  fetch state, so it now directly includes `tls.hpp`. Previously translation
  units such as `app_nav.cpp` that included `app.hpp` before `gemini.hpp`
  failed because the complete certificate type was not declared yet.
- TLS's public header has no OpenSSL or Mbed TLS includes; this change is
  independent of the configured network backend. Full Nix/SDL compilation
  is still pending on the native development environment.

## Status (2026-11-09) — embedded panel paint ordering

- Prepared ImGui's display lists before native window painting; five rich
  embedded panel bodies (Reply, Bookmarks, Annotations, Nostr Explorer and
  Nostr Accounts) now render immediately after their own FloatWindow frames
  in the shared back-to-front stack. Menus, popups, tooltips, toolbars and the
  settings dialog remain on top. Child ImGui windows are classified with their
  parent panel; pure and live vendored-ImGui draw-list tests pass.
- Old SDL2/R36S per-triangle caching still uses the historical single render
  pass until its cache is reworked. Interactive hit-testing above obscured
  widgets and desktop UI testing remain pending; no full SDL/Nix build tools
  are available in this environment.

## Status (2026-11-09) — UI panel / resize cleanup

- Bookmarks is now a dockable native `FloatWindow`, with ImGui only in its
  client area. It shares the native close/maximize controls, docking, resizing
  and session persistence with the other panels. Preferences remains a
  standalone settings dialog by design.
- Docked neighbours have movable internal dividers and persisted relative
  sizes; tile and native frame edges show directional resize pointers, and
  docked neighbours display subtle separator grips. The SDL2 ImGui cursor
  backend now notices and restores cursors changed by the native chrome.
- A click on an ImGui popup or embedded widget cannot also drag the native
  panel behind it. Fixed the fixed-status-line paint order when a tall free
  panel extends into its strip; recorded the layering rules in `docs/UI.md`.
- Pure C++ dock geometry tests pass. The full SDL/Nix application build and
  hands-on UI testing remain unavailable in this container. Compositing rich
  ImGui panel bodies in native `float_stack` order is still an open issue.

## Status (2026-11-09) — Gemini certificate metadata in trust review

- Gemini first-use and changed-pin reviews now display the leaf certificate
  subject, issuer, validity start/end (UTC), and self-signature status beside
  full SHA-256 hashes. Hashes are grouped for wrapping, and peer-controlled
  names are added as literal document spans, not interpreted as Markdown.
- OpenSSL verifies an alleged self-signature using the certificate's own key;
  Mbed TLS exposes self-issued name equality but marks actual self-signature
  verification as unknown. Details remain informational, not CA trust.
- Offline OpenSSL tests cover date/name extraction and deliberately misleading
  self-issued certificates signed with a different key. Full SDL/Nix UI and
  Mbed TLS builds still need testing on the target machines.

## Status (2026-11-09) — Gemini first-contact certificate review

- The Gemini network client no longer silently pins the first certificate.
  After its TLS handshake, an unknown host:port presents its full leaf SHA-256
  fingerprint in an error/review document, with Enter to trust it and Esc to
  cancel. No Gemini URL is sent and no stored or session pin is created until
  approval; acceptance forces a fresh network fetch and pin recheck.
- First-use approval refuses to overwrite a pin established by another fetch,
  while duplicate approval of the same fingerprint is safe. In `--no-save`,
  first-use trust is session-only. Invalid or unreadable trust state never
  offers a bypass. Background tabs require selecting and reloading to review.
- Offline Gemini TOFU tests cover first-use review, saved/session-only
  approvals and conflicting pins; the Gemini network code passes C++20 syntax
  checks. A full SDL/Nix build and hands-on prompt testing remain pending.

## Status (2026-11-09) — Gemini certificate replacement confirmation

- Gemini TOFU failures now include structured host/port and previous/presented
  SHA-256 fingerprints. On a foreground mismatch, Xanatoo shows both hashes in
  the error document and asks Enter to approve the replacement or Esc to keep
  the original pin. Approval updates that host:port only and retries with a
  fresh TLS handshake; other TLS failures cannot trigger the approval UI.
- Replacement rejects invalid or stale approvals. In `--no-save` mode,
  approved replacements remain in session memory even when a saved pin exists,
  and a changed saved pin invalidates the temporary override. No automatic
  certificate rollover is performed.
- Pure C++20 Gemini trust-store tests cover replacement, stale approvals,
  third fingerprints, and session-only overrides; Gemini network code passes
  C++20 syntax checks. Full SDL/Nix builds and manual UI testing still needed.

## Status (2026-11-09) — Gemini certificate trust on first use

- Added SHA-256 peer-leaf fingerprint extraction to both TLS backends. Gemini
  now records the first certificate by hostname and port and checks it after
  each handshake *before* writing the Gemini URL. Unexpected replacements
  stop the fetch with previous/received fingerprints; no automatic rollover.
- Pins live in `state_dir()/gemini-known-hosts` with atomic replacement and
  owner-only permissions; malformed/unreadable state fails closed. With
  `--no-save`, existing saved pins are still checked and new pins only persist
  in memory for the running session. First connections remain susceptible to
  interception; first-use confirmation now provides a review opportunity but
  cannot verify the fingerprint independently.
- Offline OpenSSL fingerprint and pure trust-store tests pass on Linux, and
  changed Gemini/TLS translation units pass C++20 syntax checks. Full Nix/SDL
  and Mbed TLS Android builds remain to be verified on the target machines.

## Status (2026-11-09) — Nostr group-store test SDL3 build fix

- Fixed the new `xanatoo_nostr_group_store_tests` target to use the selected
  SDL2/SDL3 backend and link its imported target. The target previously
  compiled `src/paths.cpp` without `XANATOO_SDL3=1` or an SDL include path,
  so the default SDL3 configuration incorrectly included `<SDL.h>` and failed.
- This container lacks the full SDL/Nix development tools; the fix is
  checked against CMake target wiring and awaits an actual `nix build` or
  `nix develop` build on NixOS.

## Status (2026-11-09) — verified HTTPS and Nostr WSS by default

- Made the shared TLS transport validate server certificate chains and
  hostnames by default, including ordinary HTTPS pages, service requests,
  streamed downloads/images and Nostr WSS. Gemini deliberately opts out
  pending TOFU for self-signed certificates. Plain HTTP is still available
  by explicit URL, but HTTPS-to-HTTP redirects are rejected.
- Separated the NIP-11 HTTPS/public-only/256 KiB guard from the general
  12 MiB buffered response limit, so ordinary verified browsing does not
  inherit the metadata size restriction. Reddit OAuth requests refuse to
  send bearer tokens outside `https://oauth.reddit.com`.
- Mbed TLS now caches parsed system CA roots for repeated HTTPS requests;
  supports common Linux/NixCA and Android Conscrypt paths, and fails closed
  if none are usable. Added offline OpenSSL socket-pair TLS identity tests,
  redirect-downgrade tests and `docs/TLS.md`.
- Changed network units pass desktop C++20 syntax checks; local TLS and URL
  regressions pass. Full SDL/Nix build, Android Mbed TLS compilation and
  physical device trust-store testing remain outstanding.

## Status (2026-11-09) — verified relay identity for NIP-29 metadata

- NIP-29 group discovery now attempts a certificate-verified NIP-11 HTTPS GET
  (`Accept: application/nostr+json`) for each scanned relay. It reports NIP-29
  support hints, the presence of a valid `self` master key and any failed
  verification; automatically discovered HTTPS targets use the same public-IP
  restrictions as their WSS connections.
- Metadata from a NIP-11 relay receives `[relay]` only when the BIP-340
  signature checks and the event signer equals the relay's authenticated
  `self` key. `[sig]` and `[hash]` remain weaker provenance levels. SQLite
  migration preserves old groups as unbound, and event-ID-aware updates avoid
  downgrading the verification on a later offline refetch.
- Opt-in certificate and hostname validation is now available on OpenSSL and
  Mbed TLS backends. The default TLS behavior is unchanged outside NIP-11;
  these other network clients still need certificate-verification hardening.
  No system CA store means no relay-master assertion.
- Added pure NIP-11 parsing/capability fixtures and signed kind-39000 BIP-340
  fixtures. OpenSSL and hash-only tests pass, and the SQLite-backed trust,
  priority, `--no-save`, and migration tests pass against the host SQLite
  runtime using a temporary compile shim (development headers unavailable).
  Full CMake/SDL integration and live relay checks still need NixOS validation.

## Status (2026-11-09) — NIP-29 discovery without manual relay setup

- The Groups tab now accepts a bare relay hostname, `http(s)://` or `ws(s)://`
  address, normalizes to a secure `wss://` connection, and gives a useful error
  for malformed entries instead of demanding users know the URL scheme.
- Empty-input discovery prioritizes locally observed group hosts, seeds with
  two NIP-29-specific public relays and learns additional hosts from NIP-51
  public `kind:10009` group lists (both cached and a bounded live sample).
  It scans at most five group relays per request and reports relay-by-relay
  results in the returned document. Autodiscovered targets must resolve to
  public IP addresses; manually selected local test relays remain supported.
- Relay candidates and public group lists are suggestions, not a global index
  or proof of group membership; NIP-11 master-key binding is still pending.
- Added a standalone pure-C++20 URL/hint regression target, which compiles and
  passes without the native dependencies. `src/nostr.cpp` passes Nostr-enabled
  syntax checking and `src/nostr_store.cpp` passes non-SQLite syntax checking.
  The full SDL/SQLite build and live network discovery still need validation
  in the NixOS development shell.

## Status (2026-11-09) — recursive tile geometry foundation

- Added a pure C++20 `TileTree` model with stable external view IDs, nested
  horizontal/vertical splits, per-node ratios, recursive geometry and
  divider enumeration. Splitting an existing leaf preserves its view ID;
  closing any leaf except the last collapses its parent without replacing
  the surviving document view.
- The existing centre-pane layout now calculates its rectangles through
  this tree, with a two-leaf adapter for the current `App::split` state.
  Normal two-pane column and row layouts, page bars, docked columns, and
  the left scrollbar remain supported.
- New standalone portable `tile_tree_tests` cover columns, rows, four
  nested tiles, close/collapse, view identities and invalid operations.
  The g++ C++20 test compiles and passes. SDL3 GUI regression compilation
  still needs the NixOS development environment.
- The geometry model can represent arbitrarily many nested tiles, but the
  application **still presents only two centre views**. The next integration
  must bind live `Pane` slots to tree leaves, update focus/tab/nav/hit-test,
  and offer Split/Close Tile commands with per-divider controls. Avoid
  treating extra centre leaves as floating windows.

## Status (2026-11-09) — Compare divider overview

- The widened Compare divider now shows an overview of both documents:
  orange tick marks indicate diff blocks in each document's sequence, and
  blue outlines indicate each pane's current viewport. It works with
  side-by-side and stacked tile arrangements.
- Clicking/dragging the overview scrolls both panes to corresponding
  normalized positions, including paged views. The standard document
  scrollbars remain independent. The overview's document-order markers are
  approximate, not exact pixel-aligned diff hunks; source mapping and
  next/previous-hunk navigation remain future work.
- `diff_overview_positions` traverses the document tree, avoiding missing
  offscreen blocks when Xanatoo virtualizes large documents. A standalone
  C++20 test of tree-order mapping, invalid IDs and empty inputs compiles
  with g++ and passes. The SDL3 GUI still cannot be compiled in this
  workspace without the NixOS development toolchain.

## Status (2026-11-09) — tiled browsing, mode-controlled dividers

- Independent tiles no longer implicitly use the linked split navigation
  history: links and Back/Forward stay within their own pane. The new Trail
  toolbar button explicitly opts into linked navigation. Switching away
  retains matching Trail history for later resumption.
- Both centre views can now be arranged in columns or stacked in rows, with
  a draggable divider in either direction. The divider is colour-coded by
  mode, has an always-visible `TILES` / `TRAIL` / `DIFF` badge and a
  right-click menu for changing mode and orientation. Compare uses a wider
  divider as the starting point for future diff navigation.
- Session persistence records tile orientation and mode (older split sessions
  restore as Trail); Ctrl+Backslash and Ctrl+Shift+D route through the same
  commands as the menus, avoiding divergent keyboard behaviour.
- Current centre tiling still supports exactly two views. The follow-up
  arbitrary recursive layout, stable view relationships, divider minimaps
  and workspace snapshots are designed in `docs/TILING.md`.
- Native SDL3 GUI compilation/interaction still requires the project's
  NixOS toolchain; the checkout environment lacks Nix/CMake/SDL3.

## Status (2026-11-09) — Nostr Explorer build corrections

- Corrected the Events-tab label expression in `ui.cpp`: start the label
  with a `std::string` before appending the literal `"kind "`.
- Corrected `app_fetch.cpp`'s Nostr completion check: compare the service
  name as text (`std::string_view`) rather than comparing two pointers.
- Both changes address diagnostics from the user's native SDL3 Debug build;
  a full GUI rebuild still needs to be confirmed on that NixOS setup.

## Status (2026-11-09) — NIP-29 relay-group catalogue and discovery

- Group indexing now retains `relay + group ID` identities for signed/hash-
  checked `kind:39000` metadata and group `h`-tagged events; individual relays
  can host distinct groups sharing an ID. The cache tracks claimed names,
  about, parent, access flags, and deduplicated group message counts.
- Nostr Explorer adds a Groups tab and explicit `wss://` relay discovery.
  `nostr:groups` samples public relays; `nostr:groups@wss://...` targets one
  relay. Opening a group fetches its `kind:1/9/11` public timeline and falls
  back to locally cached group messages when offline.
- Fixed a general SQLite search bug: empty `std::string_view{}` was binding
  SQL NULL instead of an empty string, hiding all rows in unfiltered searches.
- Group signatures still do not establish that metadata came from the relay's
  NIP-11 master key; group discovery never claims authoritative membership.
- Linked SQLite integration covers relay-scoped group identity, deduplication,
  metadata, cached group-event retrieval and the previous schema migration. Native
  GUI compilation/interaction still requires the NixOS development setup.
- Next: authenticate relay metadata via NIP-11, add NIP-29 membership/admin
  event views and NIP-19 `naddr` sharing, then NIP-05 lookup, mbedTLS Schnorr
  verification and local cache retention/export/clear controls.

## Status (2026-11-09) — Nostr event integrity and cache migration

- Added NIP-01 canonical serialization and SHA-256 event ID checking before
  accepting downloaded events. On OpenSSL-backed Nostr builds, BIP-340
  secp256k1 Schnorr signatures are also checked; other TLS builds currently
  retain hash-only status rather than claiming authenticated identities.
- Cache rows record hash-only vs signature-verified status. New event/profile
  queries exclude 309-era unverified rows. Schema migration keeps those
  observations (no destructive rewrite) and reports their count separately;
  a later valid re-fetch can upgrade an old row. `--no-save` never migrates.
- Explorer shows signed, hash-only, and legacy-unverified counts. Incoming
  invalid event hashes/signatures are rejected before presentation/caching.
- Added an independent verifier test target and official BIP-340 positive and
  negative vectors, plus NIP-01 serialization and a fully signed event fixture
  (checked against an independent signer). OpenSSL and non-OpenSSL
  verifier tests, no-SQLite syntax checks, and a linked SQLite smoke test
  (including older-schema migration) pass in this container. Full SDL GUI
  compilation still needs the user's development environment.
- Next: equivalent BIP-340 verification when using mbedTLS, stronger strict
  JSON/UTF-8 validation and NIP-05 lookup, and cache retention/export/clear
  controls; NIP-29 discovery and actual signing identities remain open.

## Status (2026-11-09) — Persistent Nostr catalogue and native panels

- Wrote a transport-independent publication plan (`docs/PUBLISHING.md`):
  immutable local revisions, WebDAV/static hosting, Nostr announcements and
  discussion, IPFS archival/pinning, receipts, and exportable manifests.
- Documented Nostr feature development, identities, relay trust limitations,
  and current groups/communities/channels in `docs/NOSTR.md`. NIP-29
  relay-based groups are the preferred future group discovery target;
  NIP-72/NIP-28 remain legacy reading/interoperability possibilities.
- Relay fetches now persist their original event JSON, individual relay
  observations, replies and encountered author/mention public keys to
  `state_dir()/nostr.sqlite`. Unique events are counted only once; kind-0
  metadata is kept by latest event timestamp and distinguished from verified
  identity. Cache can recover viewed notes/profiles after relay failure.
- Panels → **Nostr Explorer** and **Nostr Accounts** are native float/dock
  panels with ImGui only for embedded controls. Explorer searches and pages
  through cached people/events and shows local note/event/key/relay stats. Accounts
  saves multiple named **watch-only** public identities and their selection;
  it deliberately cannot generate signing keys or publish yet.
- Cache and accounts are optional with SQLite, and `--no-save` suppresses
  writes. A linked SQLite runtime smoke test passes; no-SQLite source syntax
  checks pass. Full GUI build/interaction requires SDL development packages.
- Next: verify event IDs and Schnorr signatures before trusting stored
  events; implement cache retention/export/clear, relay-based discovery,
  richer event-kind views, NIP-29 groups, and secure NIP-46 or NIP-49-backed
  signing identities. UI docking/clipping needs hands-on verification.

## Status (2026-11-09) — Native revision and annotation panels

- Document revisions now have a native dockable/resizable `FloatWindow`
  panel, persisted with the session. It lists working/published ancestry,
  previews snapshots, shows bounded line diffs and restores a selected
  snapshot as a new working child without altering the publication or
  immutable imported original.
- Migrated Annotations from a standalone ImGui window into the same native
  panel system. ImGui only draws note editing and layer controls *inside*
  the panel's native client area, as with Reply. Both default to the right
  dock and appear under Panels.
- SQLite integration passes imported-original restore, separate publication
  ancestry, stale-head rejection and cross-document identity checks. Native
  GUI compilation still requires the user's NixOS development environment.
- Next: interactive GUI verification of panel scroll/stacking/clipping,
  graphical document diffs, better converter provenance, per-layer colours
  and orphan reattachment.

## Status (2026-11-09) — Imported-source provenance and file-free Save

- Added a `document_origins` table linking a file/URL alias to a native key,
  its immutable original parsed tree and a content-addressed copy of its exact
  source bytes. Reimporting cannot overwrite either original or working head.
- The first edit to a local file detaches it into native SQLite revisions;
  Ctrl+S likewise imports source documents instead of writing over the file.
  File → Edit local copy enables editing a fetched read-only page after
  preserving its response and source snapshot.
- Opening a known file/URL again resolves to its local working copy; the
  alias also works via startup file arguments and the Open File dialog.
  File → View original and `db-original:doc:<id>` display the immutable parsed
  snapshot without modifying the working document. The documents index links
  to both versions.
- Save As is now explicitly **Export a copy**. Export does not retarget the
  document and rejects existing output files, avoiding accidental overwrites.
- Focused SQLite integration passes for first-import preservation, replay of
  source aliases, idempotence and original ancestry, alongside annotation
  history and native published revisions. No-SQLite syntax checks pass.
  Full SDL GUI compilation still requires a development environment.
- Next: GUI revision browser (including restoring the original as a *new*
  working revision), per-source import improvements for converter formats,
  robust remote aliases and explicit publishing destinations.

## Status (2026-11-09) — Database-native documents, autosave and local Publish

- Added `document_heads` to the existing SQLite revision store. Native
  `doc:<64 hex>` keys have independent append-only working and published
  ancestry. Save and publish validate expected heads, and every old revision
  can be loaded by ID. Existing file/draft heads and annotation layers are
  unaffected.
- File → New immediately creates a stored document; edits autosave working
  revisions, Ctrl+S saves the working head, and File → Publish locally
  creates an explicit publication snapshot without contacting any network.
- File → Local documents (`documents:`) lists persisted native documents;
  `db:doc:<64 hex>` opens/reopens one across sessions. Export and Save As
  write a file but keep a native document's database identity unchanged.
- SQLite integration passed for saving, publication ancestry, stale heads,
  distinct owners and reopening; no-SQLite source checks pass. Full native
  SDL/CMake build remains to be checked on the user's NixOS development host.
- Next: migrate edits of imported files/remote documents into native working
  revisions while keeping the untouched original; add a revision browser with
  restore/diff, and configure explicit publication destinations. Annotation
  layers stay independent of all source/document revisions.

## Status (2026-11-09) — Fix native unit-test linker

- The supplied NixOS build log showed `xanatoo_tests` missing the
  `load_hidden_annotation_layers` and `save_annotation_layer_visibility`
  definitions. `sidestore.cpp` already belonged to the app target but was
  absent from the separate test executable's source list.
- Added `src/sidestore.cpp` to `xanatoo_tests` in CMake. This is the exact
  implementation of the missing functions and uses dependencies already
  linked into the target (SQLite, document, paths and utilities).
- Focused standalone SQLite integration still passes for layer revisions,
  history restoration and stored visibility. Full native CMake/Nix verification
  awaits the user's development environment.

## Status (2026-11-09) — Annotation history restore and clickable tabs

- The active personal annotation layer now has a bounded (24 entries) history
  browser with timestamps, item counts and a Restore button. Restoring an old
  snapshot commits it as a new child of the current head instead of deleting
  later revisions. Wrong-layer restores and stale-head conflicts fail safely.
- Note tabs share their layout and hit-test geometry. Clicking or
  right-clicking a tab focuses its passage and opens the independent note
  editor. Multiple notes near the same anchor offset their tabs.
- SQLite integration tests verify ancestry, restoring deleted notes,
  rejection of foreign revisions and stale restore attempts. Source syntax
  checks pass without SQLite. GUI SDL build and interactive click tests still
  need a development machine with SDL/FreeType/SQLite headers.
- Next: per-layer colours and orphan reattachment, broader version history
  navigation, then native local-database document working/published heads.

## Status (2026-11-09) — Editable note overlays and revisioned removal

- Annotations now attach Markdown/text notes to a selected passage in an
  independently versioned local layer. The page gets a distinct note tab;
  sources remain untouched. Other people's layers remain read-only.
- The Annotations panel lists entries on the current page, including lost
  anchors; selecting an entry jumps to its resolved passage. Notes have
  a multiline editor with save-on-idle (0.8s), save-on-blur and manual Save;
  Remove advances the layer head but retains the previous revision.
- Shared mutation helpers for add/edit/remove save a copy first, so conflict
  or SQLite failure cannot change the caller's layer or revision cursor.
  Note drafts clear only after confirmed creation; un-hiding a layer on
  creation also persists its visibility setting.
- Focused SQLite integration passes for history, edits, deletes, stale edits,
  and reopen, plus no-SQLite source syntax checks and whitespace validation.
  Full SDL GUI compilation and hands-on UI verification need a development host.
- Next: clickable annotation margin tabs, history browser / restore command,
  layer-specific styling and anchor reattachment; then native local-database
  document working/published heads and publishing destinations.

## Status (2026-11-09) — Annotation highlights and layer controls

- The Annotations panel lists stored independent layers, creates local
  layers, picks the active local layer, and toggles which layers are shown.
  Visibility is per-user state in `sidestore.sqlite`, not published layer data.
- Highlight Selection from the panel or document context menu commits an
  annotation targeting a durable passage. This also works on read-only web
  documents and never dirties or writes their source content.
- Visible annotations re-resolve against the current document, draw selection
  washes and scrollbar ticks, and keep unresolved anchors in storage.
- Focused SQLite integration exercises old-schema migration, independent
  revisions, conflicts, reopen, and saved visibility. Source syntax checks
  pass; the full SDL/CMake GUI test suite still needs a development host.
- Next: annotation text-box UI and editing/deleting notes, annotation
  indicators with distinct per-layer styling, orphaned-anchor UI, and native
  database-backed document working/published heads.

## Status (2026-11-09) — Versioned annotation layer foundation

- Add immutable typed-object revisions and independent `object_heads` in the
  existing SQLite revision store, preserving legacy drafts and file heads.
- Add annotation layers as versioned collections (owner, name, stable
  annotation IDs, kinds, bodies, styles and multi-document passage targets).
  A layer stores only references/annotations, never a copy of source content.
- Layer saves use optimistic revision checks; historical layers can be loaded
  by revision id, and corrupt/truncated snapshots are rejected.
- Focused SQLite integration test passed against the runtime library, including
  a pre-existing database schema, reopen, independent heads, and legacy drafts.
  Changed sources also compile without SQLite. Full CMake/SDL test suites
  need a development host with their headers and tools installed.
- Next: render enabled layers, persist per-user visibility, edit annotations
  from the UI, and eventually generalize document save/publish heads. The
  current file-based editing behavior is unchanged in this increment.

## Status (2026-11-08) — Reply panel edge positioning

- Position floating/docked panels before reading their absolute ImGui child
  positions for the frame. Previously a panel could snap at draw time after
  the Reply editor had already used its unsnapped position, separating the
  editor from its frame when dragged/resized against a window border.
- Make Reply labels and the mock status wrap in narrow docks, reserve footer
  space using the actual ImGui available height, and allow small panels to
  scroll to the Send button. Native Debug build and all three CTest suites
  pass; interactive dragging on a device still needs a manual check.

## Status (2026-11-08) — Reply panel mockup

- `git pull --ff-only` succeeded; remote master was already at the current
  local tip. New changes follow that same history.
- Right-click any semantic Message card (HN, Reddit, Lemmy, Nostr, or local
  Thread/Message documents) → **Reply…** opens a regular floating/dockable
  panel with multiline Markdown editing, selection-aware Bold/Italic/Code/
  Link/Quote controls, and a Send button that reports "not sent" (mockup).
- The target comment is washed and outlined in amber in the document, with
  an amber tick drawn over the scrollbar thumb. The last known document
  position is retained while virtualisation removes the message card from
  the active layout; stable comment labels rebind after reparses.
- Added a completely offline exercise page (`examples/reply-demo.xml`) and
  `docs/REPLIES.md` explaining the mock workflow and lack of network posting.
- Native SDL2 Debug build and all three CTest suites pass; the offline demo
  loads under Xvfb. Handheld/R36S and Android builds were not exercised here.

## Status (2026-11-07) — R36S GCC 9 and man-page build fixes

- The supplied `nix flake check` log showed two independent failures:
  floating-point `std::to_chars` / `std::from_chars` in `src/xml.cpp` are not
  provided by ArkOS's GCC 9 libstdc++, and `mandoc` warned about two
  man-page sentences on the same line.
- XML scale import/export now uses classic-locale C++ streams with
  `max_digits10` precision, while keeping integral `charconv` unchanged.
  Added exact round-trip checks for extreme finite floats, comma-decimal
  locale independence, and malformed/nonfinite scale rejection.
- Fixed the man-page sentence break. Native SDL2 Debug build, all three
  CTest suites, and local `groff -mdoc` validation pass. An R36S Nix
  cross-build and `mandoc` lint remain to be checked on a Nix host.

## Status (2026-11-07) — Reddit OAuth configuration

- Added masked Client ID / secret fields, save and clear buttons, and
  links to Reddit's Data API access request and legacy app preferences in
  Edit → Preferences. Environment credentials override saved values.
- Persist approved app-only credentials in `config_dir()/reddit.conf`,
  separate from visual settings. On Unix, write atomically with mode 0600;
  reject control characters, incomplete pairs, and writes under `--no-save`.
  Read them on each request, so restarts are unnecessary.
- `docs/REDDIT.md` explains the approval workflow, obtaining credentials
  when legacy app creation is permitted, security, and Reddit's announced
  March 2027 public Data API shutdown. Automatic credential creation is not
  offered: approval and account registration require Reddit-side user action.
- Added offline round-trip, replacement, permissions, environment override,
  invalid input and no-save tests; native SDL2 Debug build and CTest passed.
- Updated the native Ubuntu build recipe in `docs/ENVIRONMENT.md` to select
  SDL2 explicitly; current CMake defaults to SDL3.

## Status (2026-11-07) — Fix errors from user's Debug build

- Added `<cstring>` for the URL context menu's `std::strlen` use.
- Compare `FetchShared::service` to `"Reddit"` by contents with
  `std::string_view`, avoiding the unspecified pointer comparison.
- Confirmed no other `service ==` string-literal checks and checked the
  diff for whitespace errors. Native CMake Debug with SDL2, OpenSSL and all
  network clients enabled built successfully; all three CTest suites passed.
- Headless Xvfb startup opened `examples/tour.md`, initialized the OpenGL
  renderer and loaded 55/55 icons. Nix/thumtoo builds were not exercised.

## Status (2026-11-07) — Context menus

- Added a right-click location field menu for Cut/Copy/Paste/Select All,
  Copy/Open Address, Paste and Go, and add/remove page bookmark. It preserves
  an edited URL and its selection while the popup owns keyboard focus.
- Right-click Back/Forward now lists up to 20 reachable URLs, nearest first.
  Selecting an entry jumps by the corresponding history offset, including
  direct multi-step jumps in the split-browser trail without fetching every
  intermediate page. The target-index helper has offline unit tests.
- Removed panel/view toggles from the document context menu. Added explicit
  bookmark selection, page, and reading-position actions; page bookmarking
  ignores a text selection. Existing bookmark-margin-icon menus remain.
- The standalone history-target regression passes. GUI compilation/runtime
  still requires SDL headers, which are absent from this container.

## Status (2026-11-07) — Reddit RSS fallback review

- Audited the 403 error document, native Reddit URL conversion, and RSS/Atom
  dispatch: the fallback link is a normal inline link, is only offered for
  HTTP 403 from the Reddit client, and feeds route to the existing reader.
- Fixed an edge case shared by `is_reddit_url` and `reddit_json_url`: a Reddit
  homepage address with `?query` or `#fragment` but no slash previously
  treated the suffix as part of the host. Added regression checks for both
  JSON and RSS conversion and a lookalike-domain rejection.
- Offline standalone URL/dispatch checks and Reddit-client syntax checks pass.
  A live Reddit request and the full SDL/FreeType GUI build remain untested.

## Status (2026-11-07) — Reddit HTTP 403 RSS fallback

- Native `reddit:` error documents now offer a clickable Reddit `.rss` link
  on HTTP 403, using the existing feed reader rather than retrying behind
  the user's back. Other Reddit errors remain unchanged.
- `reddit_rss_url` covers homepage, subreddit listings/sorts, comment-thread
  paths, `.json` variants and query strings; the offline URL checks pass.
- Important limitation: Reddit's official October 8 changelog announces the
  end of RSS feed support on 2026-11-13. This is a temporary fallback, and
  RSS may also return 403 from blocked networks.

## Status (2026-11-07) — Plain HTTP and scheme-preserving redirects

- The existing `https_*` network helpers now accept `http://` over raw TCP
  (port 80), keeping TLS and port 443 for `https://`. Both buffered service
  requests and streaming web/download requests use the same transport.
- Redirects retain the scheme in `Location`; relative paths, query-only
  redirects, custom ports, fragments and bracketed IPv6 addresses are handled
  by a shared parser with standalone regression tests.
- Inline images, RSS/Atom feed URLs and sitemap references no longer silently
  upgrade explicit HTTP addresses. Default URLs generated by services remain
  HTTPS. Syntax checks and URL tests pass; this container blocks loopback TCP
  smoke tests and lacks the full SDL/FreeType build environment.

## Status (2026-11-07) — MIME-based web downloads

- Send non-renderable HTTPS responses and `Content-Disposition: attachment`
  into a streaming download rather than an error page or external browser.
  Page navigation stays where it was. Hover previews never write files.
- Added a dockable Downloads panel with bytes/progress, cancel, completion,
  failed transfer status, saved file path and containing-folder action; a
  toolbar button opens it without navigating away.
- Downloads rows have a right-click menu: Open / Open in New Tab (in
  xanatoo, e.g. downloaded PDFs and comics), Open with System Application,
  Open Containing Folder, Copy Path / Download Link, Cancel (active) and
  Remove from List (keeps the file). The Dir button and these go through
  `xdg-open` (SDL_OpenURL); failures now show SDL's error in the status bar.
- Save temporary partial files in the user's Downloads directory, atomically
  avoiding existing filenames. Filename/MIME rules and non-overwrite handling
  have standalone checks. Completed files are remembered in the state dir;
  full GUI build still needs SDL/FreeType headers.
- Open questions: resumable transfers, TLS certificate verification,
  broader direct image/viewable-media support.

## Status (2026-11-07) — Forced-wrap overflow after ordinary text

- Before laying out a word, check whether it fits beside the already placed
  words. Move it to a fresh line if not; otherwise the forced-break path used
  the word's width alone but emitted the earlier text too, overflowing the
  column and clipping the ↩ continuation marker.
- Reserve continuation-mark space with the same font role used to draw it;
  omit the decoration if an unusually narrow float band cannot hold it.
- Added a metrics-only layout regression with long unspaced text after ordinary
  words, including a linked inline span, at several column widths and with
  justification both on and off.

## Status (2026-11-07) — Flake checks

- Fixed man-page lint, provided the SDL3 API check with a compiler, and
  added REUSE metadata for bundled fonts and the PDF research corpus.
- Explicitly selected SDL2 for its R36S package; the full `nix flake check
  -L --keep-going` passed. No CLI bundle.

## Status (2026-11-07) — Opt-in Android updates

- Added an Android Preferences switch for automatic update checks, off by
  default, and linked to it from the welcome page. Disabling it suppresses
  pending results and removes the update notification.
- Java and changed desktop units compiled; 3897 offline checks passed. The
  full desktop build still lacks SQLite headers in this shell. Device testing
  remains outstanding. No CLI bundle.

## Status (2026-11-07) — Android update notice

- Android checks the website for a newer APK at startup and offers a system
  notification linking to the download page. The site publishes the APK's
  version code and name in a generated manifest. Installation remains user-led.
- Android 13+ requires notification permission; Android 5.1+ is supported.
- Java compilation and DEX conversion passed. APK build was still running;
  verify on a device, including download and installation. Fixed an SDL3 icon
  size API build error encountered during the first APK build.

## Status (2026-11-07) — Search and location shortcuts

- Ctrl+K focuses search, Ctrl+L focuses location, and link creation moved to
  Ctrl+Shift+K. Bar shortcuts now work across ImGui and prompt focus.
- Offline unit tests passed. Full application build is blocked in this shell
  by missing SQLite headers. No CLI bundle.

## Status (2026-11-07) — Tab strip refresh

- Reserved space for the new-tab button beside the scrolling tab strip,
  refreshed the tab spacing and button, and reused cached site icons on tabs.
- Changed UI units compiled and 3895 offline checks passed. No CLI bundle.

## Follow-up (2026-11-07) — Location icon borders

- Hide contextual button borders until toggled and inset the icon group from
  the right edge of the location field. No CLI bundle.

## Follow-up (2026-11-07) — Icons inside the location field

- Embedded the contextual controls in the location field, reserving text space,
  and added icon artwork and rasterizer fallbacks for reader and site map.
- Compiled the changed UI unit and rasterized the icon set. No CLI bundle.

## Status (2026-11-07) — Context controls in the location bar

- Added reader, page navigation and site-map controls beside the address when
  applicable; site-map discovery starts when its control is opened. Controls
  track the focused view and fit on the separate location row on narrow windows.
- Changed application units compiled and 3895 offline checks passed. No CLI bundle.

## Status (2026-11-07) — Location and bookmark icons

- Show cached site icons in the location field and bookmark lists, with PDF
  thumbnails when supported and the first image of an open imported EPUB.
  The bundled decoder handles PNG entries in ICO files. Sites declaring only
  a nonstandard icon URL and bitmap-only ICOs need further support.
- Changed application files compiled; 3895 offline checks passed. Full app
  build in this shell is missing the SQLite headers. No CLI bundle.

## Status (2026-11-06) — Narrow URL bar

- Move the location field to a full-width second row when toolbar controls
  leave less than 200 scaled pixels for it. The changed source compiled and
  the existing unit suite passed (3895 checks); application linking in this
  shell lacks Fontconfig, FreeType and SQLite linker aliases. No CLI bundle.

## Status (2026-11-06) — Table cell spans

- Added column and row spans to table cells, HTML import (including
  `rowspan=0` and row groups), Markdown/S-expression round trips, and row
  insertion through spans. A derived occupancy grid drives spanning table
  layout, cell sizing, header fills and segmented borders.
- Added an example and `docs/TABLES.md`. Application and unit suite built;
  3895 checks passed. No CLI bundle.

## Status (2026-11-06) — Floats on both sides

- Recognize Parsoid thumbnails and framed files as right floats by default,
  retaining explicit left, right and no-float alignment. Header rules stop at
  active floats; text waits for room when floats leave an unusable gap.
- Built the app and unit suite; 3850 checks passed, including opposite-side
  floats, same-side stacking and narrow gaps. Live Wikipedia fetch could not
  be checked from this sandbox (DNS unavailable). No CLI bundle.

## Status (2026-11-06) — Floating infobox tables

- Preserve HTML infobox tables, including captions and nested table content;
  recognize infobox and explicit HTML float/alignment hints. Floated tables
  share the existing exclusion bands and fall back to flow in narrow panes.
- Added table-role width and round-trip support for figure and table floats.
  Application and unit suite built; 3785 checks passed. No CLI bundle.

## Status (2026-11-06) — Figure size and image links

- Size floating figures to their loaded image, and preserve block image links;
  Wikipedia thumbnail links open the original Wikimedia upload in the viewer.
- Built the application and unit tests with the configured library paths;
  3768 checks passed. No CLI bundle.

## Status (2026-11-06) — Figure floats

- Float figures as a unit with their captions, with a narrow-column flow
  fallback. HTML figure frames now close correctly and retain linked images
  and semantic captions.
- Changed source files compiled; linking in this shell lacks the Fontconfig,
  FreeType and SQLite linker aliases. No CLI bundle.

## Status (2026-11-06) — Image floats

- Added `float=left|right` to Markdown block images and `:float` to
  S-expression images, with text wrapping and a narrow-column flow fallback.
- Added serialization and layout coverage; the unit suite passed (3760 checks).
  No CLI bundle; number 290 remains unchanged.

## Follow-up (2026-11-06) — Focus the History panel

- Added History to shared panel keyboard-focus state. Opening it by menu or
  shortcut, or clicking its frame, now highlights its title; closing it returns
  focus to the document.
- SDL2 syntax-only compilation and `git diff --check` passed. No bundle;
  number 290 remains unchanged.

## Follow-up (2026-11-06) — Preserve long-word prefixes

- Fixed the forced-wrap state so a word is moved as a whole only before its
  first character; prefixes already accumulated are now emitted intact. The
  line also reserves room for the ↩ mark to avoid clipping its final text.
- SDL2 syntax-only compilation and `git diff --check` passed. The unit-test
  target compiled but could not link because this shell lacks Fontconfig and
  FreeType linker aliases. No bundle; number 290 remains unchanged.

## Status (2026-11-06) — Break long unspaced text runs

- Long words now wrap at character boundaries to stay within the text measure.
  A muted ↩ marker identifies each forced break without adding content to the
  document or changing caret/selection positions.
- SDL2 syntax-only compilation and `git diff --check` passed. This CLI change
  has no bundle; number 290 remains for the next Web chat.

## Status (2026-11-06) — Pinch zoom in the image viewer

- Added a Lightbox two-finger gesture that zooms around the fingers' midpoint
  and bypasses SDL's synthesized primary-finger drag while pinching.
- Completed Style Sheets keyboard focus: Up/Down/Home/End select, Enter applies,
  and Escape closes. SDL3 syntax-only compilation passed across the touch,
  gallery, event, frame and panel integration paths; `git diff --check` passed.
  This CLI change has no bundle; number 290 remains for the next Web chat.

## Status (2026-11-06) — Focus the Style Sheets panel

- Added Style Sheets to the shared `KeyFocus` state. Clicking the panel or
  opening it from Panels now activates its title and active-sheet highlight;
  closing it returns focus to the document. SDL3 syntax-only compilation and
  `git diff --check` passed. This CLI change has no bundle; number 290 remains
  for the next Web chat.

## Status (2026-11-06) — Add a Style Sheets panel

- Added a dockable Style Sheets panel listing discovered sheets, highlighting
  the active one, and applying a selection immediately. Added it to the Panels
  menu and session geometry/open-state persistence. SDL3 syntax-only
  compilation passed for all six changed C++ units; `git diff --check` passed.
  This CLI change has no bundle; number 290 remains for the next Web chat.

## Status (2026-11-06) — Anchor toolbar menus to their buttons

- Toolbar popups now open below the lower-left corner of their triggering
  button instead of at the pointer click location. SDL3 syntax-only
  compilation and `git diff --check` passed. Follow-up: set the popup position
  after its hover tooltip, which otherwise consumed ImGui's pending window
  position. This CLI change has no bundle; number 290 remains for the next Web
  chat handover.

## Status (2026-11-06) — Refine gallery edge zones

- Narrowed the previous/next hit and hover strips to the outer eighths of the
  viewer, aligned them to the edges, and extended them through the full height.
  The top close strip now occupies the center between those side zones. SDL3
  syntax-only compilation and `git diff --check` passed. No CLI bundle created.

## Status (2026-11-06) — Keep ImGui above the image gallery

- Moved Lightbox drawing into the scene pass before ImGui rendering. The
  gallery remains above document panels while menus and ImGui windows now
  render over it. SDL3 syntax-only compilation and `git diff --check` passed.
  This CLI change has no bundle; number 290 remains for the next Web chat.

## Status (2026-11-06) — Add a Panels menu

- Added a dedicated Panels menu for the dockable panels: Keyboard Reference,
  Heading Outline, Backlinks, Site Map, Page Navigation, History and Style
  Sheets. Menu checkmarks reflect the panels' open states, and existing
  shortcuts remain. SDL3 syntax-only compilation and `git diff --check`
  passed. This CLI change has no bundle; number 290 remains for the next Web
  chat handover.

## Status (2026-11-06) — Enlarge the ImGui menu font

- Kept the GNOME 2-era Sans family and raised the default ImGui UI font from
  14 px to 16 px so menus read better beside the 32 px toolbar icons; UI scale
  still scales it. SDL3 syntax-only compilation and `git diff --check` passed.
  This CLI change has no bundle; number 290 remains for the next Web chat.

## Status (2026-11-06) — Allow UI scale changes in Browse mode

- Preferences already opened in Browse mode, but the command filter rejected
  the selected UI scale as a document edit. Added the interface setting to
  the allowed view/UI commands. SDL3 syntax-only compilation and
  `git diff --check` passed. This CLI change has no bundle; number 290 remains
  for the next Web chat handover.

## Status (2026-11-06) — Align URL controls with toolbar icons

- Enlarged and vertically centered the URL field, centered its dirty marker,
  and extended group dividers to match the padded icon-button row. SDL3
  syntax-only compilation and `git diff --check` passed. This CLI change has
  no bundle; number 290 remains for the next Web chat handover.

## Status (2026-11-06) — Make History a regular panel

- Replaced the separate ImGui History window with a shared `FloatWindow`
  panel. It now uses the common frame, docking, move/resize, maximize,
  scrollbar and session geometry paths while preserving newest-first entries,
  current-page highlighting and click-to-open navigation.
- SDL3 syntax-only compilation passed for all six changed translation units;
  `git diff --check` passed. This CLI change has no bundle; number 290 remains
  for the next Web chat handover.

## Status (2026-11-06) — Mark toolbar menu buttons

- Added a small muted dropdown triangle at the lower-right of icon-backed
  toolbar buttons that open menus. SDL3 syntax-only compilation and
  `git diff --check` passed. This CLI change has no bundle; number 290 remains
  for the next Web chat handover.

## Status (2026-11-06) — Match toolbar height and separators

- Sized the URL toolbar for its 32 px icons and padding, and replaced text
  pipes with vertical dividers that scale to the icon row. Updated edit-row
  wrap estimates for the divider width. SDL3 syntax-only compilation and
  `git diff --check` passed. This CLI change has no bundle; number 290 remains
  for the next Web chat handover.

## Status (2026-11-06) — Wrap the edit toolbar

- Removed the horizontal scrolling toolbar row. Edit actions now wrap into
  additional rows, preferring separator-delimited groups; oversized groups
  can split across rows on very narrow windows.
- Toolbar height is calculated from the wrapped row count. SDL3 syntax-only
  compilation and `git diff --check` passed. This CLI change has no bundle;
  number 290 remains for the next Web chat handover.

## Status (2026-11-06) — Add configurable UI scale

- Added a saved Edit → Preferences UI scale with presets from 75% to 200%.
  Changing it updates toolbar icons, ImGui menus, canvas chrome and default
  document text immediately; `view.conf` preserves the choice.
- Toolbar glyphs now rasterize at 64 px and display at 32 px at the platform
  default scale. The R36S package defaults to 75% for its 640×480 screen.
- SDL3 syntax-only compile passed for the changed UI, app, platform and
  settings translation units. This CLI change has no bundle; number 290
  remains for the next Web chat handover.

## Status (2026-11-06) — Add icons to command menus

- Added the matching toolbar icons to common File, Edit, Format, Insert and
  navigation commands in ImGui menus. ImGui retains the full menu hit target,
  keyboard navigation, shortcut and checkmark columns.
- The SDL3 `ui.cpp` translation unit passes a syntax-only compile.
- This CLI change has no bundle; number 290 remains for the next Web chat
  handover.

## Status (2026-11-06) — SDL3 ImGui enum arithmetic warning

- Converted ImGui key and SDL scancode enums to integers before calculating
  alphabetic, numeric and function-key offsets, removing Clang's deprecated
  cross-enum arithmetic warnings without changing the key mapping.
- This CLI change has no bundle; number 290 remains for the next Web chat
  handover.

## Status (2026-11-06) — Export SDL3 Android entry point

- Included SDL3's `SDL_main.h` in the translation unit that defines `main`,
  allowing SDL's platform mapping to export the `SDL_main` symbol required by
  Android's `SDLActivity::nativeRunMain`.
- Confirmed the Android header mapping exports an unmangled `SDL_main`
  symbol with a local compile check; device/APK verification remains pending.
- This CLI change has no bundle; number 290 remains for the next Web chat
  handover.

## Status (2026-11-06) — Refresh app and Android launcher icon

- Added a warmer gradient, subtle symbol shadow and small highlights to the
  existing X mark. Regenerated Linux and legacy Android PNG icons.
- Added Android adaptive foreground/background layers for Android 8+, sized
  the mark to Android's safe zone, and supplied a monochrome layer for themed
  launchers on Android 13+.
- Kept density-specific legacy icons for Android 5–7. Reviewed the square
  logo and an adaptive circular-mask preview; validated all icon XML files.
- This CLI change has no bundle; number 290 remains for the next Web chat
  handover.

## Status (2026-11-06) — Toolbar button interaction states

- Removed the idle toolbar frame and added a raised bevel on hover with a
  darker sunken face while pressed. The styling is scoped to toolbar buttons.
- Build verification is pending: the existing build cache points to a removed
  Nix compiler, the Nix shell cannot update its read-only fetcher cache, and a
  direct configure cannot find SDL3 development files. This CLI change has no
  bundle; number 290 remains for the next Web chat handover.

## Status (2026-11-06) — Restrict bundle creation to Web chat

- Clarified in `AGENTS.md` that cumulative Git bundles are only for Web chat
  handovers. CLI sessions commit changes directly and leave the next bundle
  number reserved for the next Web chat handover.

## Status (2026-11-06) — Refresh toolbar icon set (bundle 289)

- Redrew all 53 toolbar SVGs with a consistent GNOME 2-inspired style:
  compact beveled forms, muted colors, paper and metal surfaces, and small
  highlight details while keeping each icon's existing action and filename.
- Rendered the complete icon set at 20 px and reviewed the contact sheet.

## Status (2026-11-06) — Complete WebAssembly data preload (bundle 288)

- Expanded the WASM virtual filesystem preload from only `welcome.md` to all
  of `examples/`, including relative images and collection assets. Also preload
  `FORMAT.md`, which the format-reference menu opens from the data root.
- Rebuilt `.#xanatoo-wasm` successfully and verified the generated loader
  contains paths for `examples/tour.md`, example images, `FORMAT.md`, styles
  and bundled fonts.

## Status (2026-11-06) — SDL3 on all default targets (bundle 287)

- Made SDL3 the CMake default and switched the native Linux package, Win32/
  Win64 cross builds, WebAssembly build, Android APK and default R36S /
  PortMaster output to SDL3. The SDL2 backend remains as an explicit CMake
  fallback and a separately named R36S rollback package.
- Replaced Windows sdl2-compat packaging with direct SDL3 linking. The
  WebAssembly build now builds static SDL3 offline from nixpkgs and links the
  first-party ImGui SDL3 backend. The default R36S package ships SDL3.
- Kept the SDL2 R36S package available under explicit rollback names and
  updated its launcher and handheld documentation to describe SDL3 as the
  standard package.
- Removed the SDL2 flake input and updated platform documentation. SDL3 key
  name and platform API differences are covered by the shared SDL boundary.
- Verified `nix build .#xanatoo-unwrapped`, both Windows ZIPs,
  `nix build .#xanatoo-wasm .#sdl3-wasm`, and
  `nix build .#xanatoo-r36s-portmaster-zip`. Device/browser runtime checks
  remain outstanding.

## Status (2026-11-06) — Android SDL3 migration (bundle 286)

- Switched the Android APK pipeline from SDL 2.30.3 to nixpkgs SDL3, matching
  Kurvenrausch's NDK/SDLActivity build and APK packaging. The app now compiles
  through its SDL3 API bridge and first-party ImGui SDL3 backend, and links
  against `libSDL3.so`.
- Raised Android's minimum API from 21 to 22, matching the SDL3 Android port.
  SDL3's Android orientation policy respects the system rotation lock.
- Updated the shared SDL bridge for SDL3 keycodes and CPU reporting, and the
  Android platform code for SDL3 I/O streams, JNI access, storage paths and
  display scaling.
- `nix build .#xanatoo-android` passes for armeabi-v7a, arm64-v8a and x86_64.
  Device orientation behavior still needs runtime verification.

## Status (2026-11-05) — Fix named variant test compilation (bundle 285)

- `test_named_style_scopes` now inspects `Document::children` directly instead
  of calling `kids_of` before its definition. The other identifier errors
  (`aside`, `normal`) were consequences of that failed declaration.
- Corrected a missing closing parenthesis in the test's `warning` variant,
  which would otherwise nest `compact` under `warning` instead of testing
  two separate scopes.

## Status (2026-11-05) — Explicit block style variants (bundle 284)

- Added named `(variant name (role …) …)` groups to `.xss` sheets. Role
  property overrides apply to explicitly named `style=name` fenced blocks,
  including descendants, without selectors or CSS specificity. Nested named
  variants compose; siblings return to their enclosing style.
- The Markdown parser and serializer preserve style names on article,
  section, header, aside, note and figure fences. Layout resolves scoped
  role properties; rendering looks up the corresponding node's style for
  borders, fills, inline-code backgrounds and underlining; pagination keeps
  those scopes. Margin prepass respects variant width/placement.
- New example `styles/annotated.xss` and tests for overrides, roundtrips,
  inherited/nested scopes and scoped rendering. Per-document base sheets are
  deferred: split panes currently share a single font set and renderer style.
- `src/parse.cpp` passes standalone C++ syntax checking; CMake cannot
  configure runtime tests in this workspace without SDL2 development headers.

## Status (2026-11-05) — Rebased cumulative bundle 283

- Rebasing the aside layout fix onto the current upstream `master` also
  incorporates the newer floating-window changes. Bundle 283 supersedes
  the pre-rebase bundle 282 and remains cumulative from `568a91b`.

## Status (2026-11-05) — Asides use only existing margins

- `LayoutEngine::setup_margins` no longer steals text-column width to
  manufacture extra side margin for `placement margin` asides. It uses an
  existing >= 8 em margin (possibly narrower than the preferred aside
  width), or leaves the margin empty.
- If the existing margin cannot fit the aside, `placement margin` now uses
  the same in-column floating path as `placement float` while the aside can
  fit beside at least 10 em of text. On a narrow pane the aside remains a
  full-width flow block. Explicit `placement float` / `placement flow`
  behaviours remain unchanged.
- Updated stylesheet documentation/comments and regression tests for:
  unchanged body geometry with a partially fitting existing margin,
  no-margin float fallback (left and right), narrow full-width fallback,
  and explicit flow placement. C++ runtime tests still need to be run in
  a Nix/native build environment (this workspace has no CMake or SDL2).

## Status (2026-11-05) — Load failures are pages, not status messages

- A failed navigation (network error, unsupported, missing local file) now
  shows an error page in the pane at the failed address (history entry,
  URL bar keeps the typed URL); before only a placeholder pane did.
- HTTP 4xx/5xx with a readable body: the site's own page is shown under a
  banner with the official status line (`http_status_line`); not cached.
  `web_fetch` returns the doc and sets `err` / `http_status`.

## Status (2026-11-05) — Screenplays, shooting scripts and stage-play layout research

- Extended the PDF layout inventory with **12** primary-source script/play
  references (now 30 corpus items): Academy screenplay sample and produced
  scripts, BBC screenplay/shooting and stage examples, a 1914 Library of
  Congress photoplay deposit, Wilde/Shaw/Capek historical stage-play scans,
  and Dramatists Guild play/musical templates.
- Catalogued scenes and production scene numbers (including `OMITTED`),
  character-linked speeches, dialogue continuations, interleaved actions,
  simultaneous speech, stage/lighting/sound directions, cast lists, lyrics,
  acts, and verse lines. Added a synthetic, **not yet parsed** Markdown-role
  sketch and benchmark criteria in `docs/PDF_LAYOUT_RESEARCH.md`.
- Identified [Fountain](https://fountain.io/syntax/) (screenplay plaintext)
  and [TEI P5 Performance Texts](https://www.tei-c.org/release/doc/tei-p5-doc/en/html/DR.html)
  (semantic drama) as interoperability/reference targets. Distinguish writer
  drafts from numbered production scripts; printed indentation and Courier
  pagination are style/output conventions, not semantic nodes.
- **Research only:** no runtime parser, layout engine, PDF importer or source
  downloads. Modern samples are reference-only; historical translations
  need individual local copyright review before automated AI processing.

## Status (2026-11-05) — Magazine layout research: Mondo 2000 and avant-garde periodicals

- Expanded `docs/PDF_LAYOUT_RESEARCH.md` with a magazine-specific semantic
  reading-order problem and a two-output approach: a reflowable article
  edition plus facsimile/graphic edition with region links.
- Added Mondo 2000 issues 1 and 4, Emigre 11, and historical Dada, 391 and
  The Popular Magazine to the machine-readable corpus (18 items in total).
  Modern magazine scans remain reference-only pending explicit reuse/AI
  permission; the historical titles are preliminary public-domain candidates.
- Extended evaluation ideas for sidebars, separate advertisements, article
  continuation, pull-quote duplication, image/text overlays and composed
  art. No content downloaded or importer code changed.

## Status (2026-11-05) — PDF layout feature research and import corpus

- Curated candidate historical books, magazines, maps, poetry and two paired
  scientific PDF/JATS XML article samples in `docs/PDF_LAYOUT_RESEARCH.md` and
  `research/pdf-layout-corpus.json` (catalog links only; no PDFs redistributed).
- Distinguished semantic structure from reflow/layout hints and facsimile
  provenance, and inventoried existing Xanatoo markup versus proposed roles
  (equation numbering, theorem/proof, tables with spans, lexical entries,
  verse, marginal notes and article reading order).
- Outlined an offline PDF-to-structured-tree pipeline with OCR/layout-model
  baselines, uncertainty and page-region traceability, plus a validation
  benchmark using JATS XML as structural reference where licensed.
- **Research only:** no new importer or markup syntax implemented. Before
  bulk processing, verify exact editions and their AI/redistribution rights.

## Status (2026-11-05) — Android SDL2 build and R36S experimental SDL3

- Excluded `src/imgui_sdl3.cpp` from Android's wildcard SDL2 source list;
  the module continues using the vendored SDL2 ImGui backends. This resolves
  the reported `SDL3/SDL.h` missing-header failure.
- Split EPUB HTML/YAML string appends to avoid false-positive GCC 9
  `-Warray-bounds` warnings from string-literal + rvalue-string chains.
- Added a separate native SDL3 R36S cross target using the KMS/DRM build
  recipe in `Grumbel/sdl-jstest` and packaging `libSDL3.so.0` in its own
  PortMaster ZIP; legacy SDL2 packages are unchanged. Exposed the variants
  in `flake.nix`, with the same checks as other platform outputs.
- Local native build and CTest verify the shared source changes, but Nix,
  Android NDK and R36S device testing are unavailable here. SDL3 cross
  build/runtime compatibility remains experimental.

## Status (2026-11-05) — SDL URL API compatibility on R36S

- Explicitly include SDL3's `SDL_misc.h` for `SDL_OpenURL` and SDL2's
  equivalent where available. The R36S sysroot has SDL2 2.0.10, which
  predates `SDL_OpenURL` (added in 2.0.14); gracefully report unsupported
  instead of failing compilation. The native build and tests pass; the
  older SDL2 cross-compilation still needs verification on a Nix host.

## Status (2026-11-05) — Paragraph character initializer cleanup

- Explicitly default-initialize `CharInfo::math` in the paragraph layout
  aggregate. This preserves the null pointer for ordinary text and image
  spans and eliminates three `-Wmissing-field-initializers` warnings.

## Status (2026-11-05) — Experimental native SDL3 application target

- Added a small first-party Dear ImGui SDL3 backend for Xanatoo's single
  window: SDL3 mouse/keyboard/text/focus events and clipboard, cursor updates,
  font atlas texture, and ImGui draw lists through SDL3 geometry rendering.
  SDL2 continues to use the existing vendored backends. The experimental
  SDL3 backend has no secondary viewport or ImGui gamepad navigation support.
- `-DXANATOO_SDL3=ON` now selects SDL3::SDL3, the native backend and the
  first-party SDL3 adapter. `.#xanatoo-sdl3-native` is a separate minimal
  package in `flake.nix`, included in `nix flake check`/GitHub's normal checks;
  it does **not** replace the released SDL2/sdl2-compat packages.
- Native SDL2 CMake build and CTest were rerun. SDL3 headers, the Nix toolchain
  and a cross-platform SDL3 runtime are unavailable in this container, so
  SDL3 compilation, startup, fonts, input, clipping and DPI still need
  verification on a Nix host. Treat the package as experimental until then.

## Status (2026-11-05) — SDL3 input and diagnostics bridge

- Moved gamepad enumeration/open/close, axes, button mappings, joystick instance
  IDs, synthetic mouse clicks and motion, keyboard fields, touch IDs and
  cursor names behind the SDL2/SDL3 adapter. The common event dispatcher now
  maps SDL2 and SDL3 event constants to one set of document handlers.
- SDL3 drop-file data is borrowed from the event; SDL2's allocated filename
  is copied and freed exactly once. Executable base-path ownership and
  external URL results also use version-aware wrappers.
- The About page and startup report now use version-appropriate SDL library,
  renderer, display, controller and touch-device APIs. The SDL3 compile-only
  check in `flake.nix` exercises the expanded public adapter.
- Native SDL2 app build and CTest were run locally. Native SDL3 application
  builds are now available as a separate experimental target (above), with
  a Nix host and SDL3 headers still required for validation. Win32/Win64
  continue through sdl2-compat; WASM/Android/R36S still use SDL2.

## Status (2026-11-05) — Shared SDL2/SDL3 API bridge

- Added `include/xanatoo/sdl.hpp`, a narrow backend boundary for window and
  renderer creation, text input, gamepad startup, integer-pixel drawing,
  render targets/clipping, surfaces, mouse coordinates, fullscreen, and SDL2's
  wrap-safe 32-bit deadline semantics. First-party render call sites now use
  this boundary; existing SDL2 and portable builds retain their ABI.
- SDL3 accepts floating-point rectangles and mouse positions; the bridge keeps
  document geometry integer based. Startup text input now occurs after the
  window exists, which also supplies SDL3's required window argument.
- `checks.sdl3-api` compiles a dedicated translation unit against real `sdl3`
  headers through `flake.nix`. Native SDL2 minimal app build and CTest pass;
  this environment does not have Nix/SDL3 available to run the new flake check.
- **Still pending before `XANATOO_SDL3=1` can build the full application:**
  vendored ImGui SDL3 platform and renderer backends, a whole-app SDL3 compile
  and runtime smoke test, and target-by-target validation. Win32/Win64 remain
  on SDL2 compatibility backed by SDL3 until that work is verified.

## Status (2026-11-04) — Start staged SDL3 migration; cross-build fix

- Force relative CMake install directories for the Win32/Win64 SDL2
  derivations. SDL2's generated `sdl2.pc` combines `${prefix}` with these
  values; the Nix CMake hook's absolute directories gave malformed paths
  such as `${exec_prefix}//nix/store/...`, rejected during fixup.
- Apply the same relative-directory configuration to the manually built WASM
  SDL2 and FreeType dependencies. This needs confirmation by a real Nix build.
- Windows now defaults to nixpkgs' SDL2 compatibility package, backed by
  SDL3, instead of our pinned SDL2 2.30.3 build. Package SDL3.dll explicitly
  (sdl2-compat can load it dynamically), and recursively collect its PE
  dependencies. Retain the classic builder as a fallback in the Nix code.
- This is a runtime transition, **not** a completed SDL3 source-API migration:
  SDL2 application calls, vendored ImGui SDL2 backends, R36S, Android, and
  the current WASM build still use SDL2 interfaces. The remaining migration
  requires SDL3-capable ImGui backends, event/renderer/surface/gamepad API
  changes, and target-by-target validation on a Nix host.

## Status (2026-11-04) — Canonical Nix cross-platform packages

- Added `nix/windows.nix` and `nix/wasm.nix` to the flake: Win32/Win64
  folders and ZIPs, WASM static site and serving app, and intermediate
  SDL2/FreeType wasm libraries. WASM sources build offline under Nix.
- GitHub Actions now uses `nix flake check` and uploads those exact flake
  outputs; removed the independently configured portable CI workflow.
- `docs/PORTS.md` and the README now put Nix first. Local Linux CMake builds
  still work. No Nix executable is available in this ChatGPT container, so
  real cross-target evaluation/builds must be confirmed on a Nix host or CI.

## Status (2026-11-04) — Initial Win32/Win64 and WebAssembly builds

- Added a dependency-light offline CMake profile, enabled by default on
  Windows and Emscripten, with bundled DejaVu fonts and stb_image fallback.
  Linux retains its full-featured configuration by default.
- Added Windows SDL-pref paths, executable-relative asset lookup, native
  external-URL handling, and a MinGW DLL-collection helper. Windows CI builds
  32-bit and 64-bit packages.
- Added an Emscripten-driven frame loop, preloaded assets and standalone HTML
  shell. Web CI produces a static WASM artifact. Persistence, file import,
  TLS/network services, and subprocess imports remain deferred.
- Detailed build commands and current limitations: `docs/PORTS.md`.
  Minimal native build/CTest/Xvfb smoke test passed; cross-compiler/runtime
  validation on Windows and in a browser is still required.

## Status (2026-11-04) — Reading-position history and forward link cue

- Per-view Back/Forward entries now retain scroll position and caret, including
  around asynchronous page loads and same-document `#` jumps; new navigations
  start at the top, while history restores the saved offset. The session
  format retains the old URL lines for compatibility and adds position and
  source-link metadata.
- The link leading to the Forward stop gets a subtle navigation overlay after
  Back, matching its exact document span where possible. The split-browsing
  trail exposes its next link for the same cue; split-view scroll restoration
  was already present.
- Added pure history tests for position capture, forward identity, branching,
  and bounded trails. Native CMake build and unit tests passed.

## Status (2026-11-04) — HN comment separators and reply navigation

- Message headers now synthesize the ` · ` separators as independent,
  non-interactive text, preserving distinct author/date/parent link spans
  and the date's semantic timestamp annotation. Link hit regions stop at
  the final glyph rather than extending into adjacent punctuation.
- Fixed `#id:COMMENT` anchor resolution: typed ID fragments now find the
  stable `Message::label`, so `reply to` navigates to its parent comment.
- Added layout and navigation regression assertions for both separators,
  link targets, timestamp hit areas, and the parent comment's scrollable
  caret. Lean and full CMake builds and CTests passed.

## Status (2026-11-04) — Semantic markup design note

- Captured the brainstorming in `docs/SEMANTIC_MARKUP.md`: composable
  semantic primitives (temporal, entity, identifier, value, metric,
  reference, place, state), typed hypertext relations, expected reader
  capabilities, examples, prior art, design constraints, and an incremental
  implementation order. Syntax examples are proposals, not implemented.
- Linked the note from `docs/BRAINSTORM.md`; documentation-only change.

## Status (2026-11-04) — Hacker News comment links and semantic time

- Usernames in lists and comment headers now link to a native
  `hn:user?id=NAME` view backed by the official HN user API (karma, join
  instant, profile description, optional web profile link).
- Comment dates link to their own `hn:item?id=N` views. `reply to NAME`
  links point to the parent message's `#id:PARENT` anchor. Right-click on
  an item-page comment exposes **Copy Link to Comment** (a native passage
  address retaining surrounding discussion) and **Copy Web Permalink**.
- Added `InlineSpan.datetime` annotations, HTML `<time datetime="…">`
  retention, and three-line hover text showing UTC time, local time, and
  two-unit relative age/future distance. These are generic document
  semantics and independent of clickable link/style roles.
- Offline tests cover HN user URL handling, linkable message-header slots,
  timestamp hit testing, HTML `<time>`, ISO offsets, leap days, and relative
  age. Lean and full thumtoo-enabled CMake builds and CTests passed.

## Status (2026-11-04) — Hacker News semantic story hierarchy

- HN lists now use `Article` entries rather than comment-style `Message`
  cards. Each contains a linked `Header` title with its source hostname on
  the same line, followed by a `Caption` for rank, points, author, timestamp,
  and the discussion link. Item pages retain proper `Message` comment threads
  but give the story its own heading and metadata caption.
- Introduced the semantic `InlineSpan::role=Metadata`: independently of a
  span's link/text kind, `(role metadata (font small) (color ...))` can set
  quiet inline typography (including within a larger heading). General
  inline style roles can now set `font` too. Kept sizes and colours in the
  `.xss` style sheets and documented the new styling behaviour.
- Offline unit test checks mixed-size title/hostname placement and metadata
  discussion link hit-testing. Lean and full native builds/CTests verified.

## Status (2026-11-04) — ChatGPT native CMake environment

- Provisioned the Ubuntu 24.04 ChatGPT container with CMake, Ninja, SDL2,
  FreeType, Fontconfig, SQLite, OpenSSL, and Xvfb; the default thumtoo build
  additionally uses libvips and libarchive. Nix is not required for either
  native build.
- Recorded the tested install commands, APT sandbox/cache workaround, build
  and test commands, and headless GUI smoke test in `docs/ENVIRONMENT.md`.
- Both the lean and full thumtoo native Debug builds passed CTest (1/1 each)
  and started under Xvfb with Mesa llvmpipe, the tour example opened, and
  53/53 toolbar icons loaded. No application code changes.

## Status (2026-11-04) — README and website refresh

- Rewrote `README.md` around the present document, hypertext, reader,
  search, workspace, platform and revision features; corrected the old
  local-only description, quick-start and dependencies, and added a clear
  caveat about TLS peer certificate verification.
- The separate `xanatoo.github.io` source tree now presents passage
  addresses, current formats and workflow features in its existing
  editorial style, with its own git bundle and no source-history fork.
- Documentation-only change; no application behaviour changed.

## Status (2026-11-03) — TeX math

- Own renderer (`docs/MATH.md`): `math_parse.cpp` (TeX subset →
  `MathNode`), `math_layout.cpp` (TeXbook App. G on the OpenType MATH
  constants / variants / assemblies), `math_font.cpp` (FreeType `MathFont`,
  `draw_math`), `InlineKind::Math` spans (`src` = TeX, `display`), layout as
  an inline atom with depth (`PositionedMath`), pagination, Markdown `$…$`
  `$$…$$`, Org `\(…\)` / `\[…\]`, S-expression `(math …)`, HTML (`<math>`
  annotations, Wikipedia `mwe-math-element`, KaTeX, MathJax scripts),
  clipboard `$…$`, `examples/math.md`, About entry. Unit test `test_math`.
- Checked under Xvfb: examples/math.md and `wikipedia:Quadratic_formula`.
- Open: bundle a math font (Latin Modern Math is GFL, STIX Two Math OFL;
  needed for Android and for systems without one — `find_math_font_path`
  already looks for `fonts/latinmodern-math.otf` in the data dir), MathKernInfo
  for script corners (integral limits), `\middle`, `\cancel`, wide display
  formulas (overflow instead of scale / scroll), selection rectangles and
  caret height around tall formulas, editing a formula in place (the span is
  one caret position), optional MicroTeX backend on desktop.

## Status (2026-11-03) — lightbox zoom

- Lightbox (`app_collection.cpp`): caption shows `W×H · zoom%`; wheel zooms
  around the pointer, `+` / `-` zoom, `0` fit, `1` 100%, drag pans
  (`Lightbox::scale` 0 = fit, `pan_x/y`; reset on stepping). Wheel no longer
  steps (arrows / edge clicks do). Built and unit tests pass; not yet checked
  under Xvfb.

## Status (2026-11-03) — search bar and `search:` addresses

- `search.cpp` / `search.hpp`: 15 engines (Wikipedia en / de, Hacker News,
  arXiv, Internet Archive, Standard Ebooks, Gutenberg, Reddit, Lemmy,
  Bluesky, Mastodon hashtag, Flickr tags, Marginalia, Wiby, DuckDuckGo),
  `kind` page / feed / JSON mapper; user engines in `search.sexp`, the
  `search:<engine>:<query>` address, Ctrl+L keywords (`w xanadu`).
  `app_search.cpp` + the `##toolbar_search` row in `ui.cpp`: hidden by
  default, Ctrl+Alt+K / View → Search Bar, engine button + dropdown, Esc
  closes; engine saved as `search_engine` in view.conf. Design and the
  engine check: `docs/SEARCH.md`. Unit test `test_search`.
- Checked under Xvfb: bar, dropdown, Enter, `ax electron spin` in the
  location bar (feed cards), Wikipedia / DuckDuckGo / Standard Ebooks /
  Gutenberg / Wiby results.
- JSON engines (`search_results.cpp`, pure `search_results_markdown` + unit
  test on canned answers): Wikipedia, HN, Internet Archive, Gutendex, Lemmy,
  Bluesky → result documents with native links (`wikipedia:`, `hn:item?id=`,
  `lemmy:host/post/N`). Checked under Xvfb, and a click on
  "Xanadu (film)" opens `wikipedia:Xanadu_(film)`. Parser findings: link
  text is literal (no escapes / emphasis), `\<` is not unescaped.
- Open: paging ("more results"), Mastodon accounts / hashtags, engine icons
  (letter badges), toolbar button, suggestions, "search this site".

## Status (2026-11-03) — Directory (mini Yahoo!)

- `examples/directory.md` + `examples/directory/*.md` (social, sci-fi, tech,
  science, culture, games, news, media, reference): ~265 links, mostly feeds.
  In Help → Examples and the Services menu. Every external URL was fetched
  with curl when the list was written (feeds returned entries); dead ones
  were left out, not guessed. Feed URLs `is_feed_url` cannot recognise carry
  a `feed:` prefix.
- Findings: Twitter / X, Instagram and TikTok have no usable public feeds
  (Nitter gone, login walls, JS-only). Open per-user / per-tag RSS: Mastodon,
  Bluesky (`/profile/h/rss`), Tumblr, Pixelfed, Flickr, Pinterest, YouTube
  (`channel_id`), Medium, Substack, DeviantArt, Stack Exchange, GitHub.
  Telegram: `t.me/s/<channel>` as a web page. Reddit rate-limits (429) on
  bursts. `[**bold**](url)` renders literal asterisks in link text (parser
  gap, not fixed). YouTube watch links open as web pages in-app.
- Next (step 2): interactive search. Candidate APIs, all keyless: Wikipedia
  (`/w/api.php?action=opensearch`), Internet Archive
  (`advancedsearch.php?output=json`), arXiv (`export.arxiv.org/api/query`,
  Atom), Bluesky (`public.api.bsky.app/xrpc/app.bsky.feed.searchPosts`),
  Mastodon (`/api/v2/search`, `/api/v1/timelines/tag/<t>`), Lemmy
  (`/api/v3/search`), HN (Algolia), Reddit (`/search.json`), Gutenberg
  (Gutendex), Standard Ebooks (`?query=`, already works). Needs a search
  prompt kind (`PromptKind`) + `search:` result pages.

## Status (2026-11-03) — session restore

- `app_session.cpp`: `state_dir()/session.conf` holds each centre view's
  address (absolute for local files), scroll, reader / paged override and
  Back / Forward trail, the tabs in order, `App::visited` (History panel),
  split + ratio, focus, dock widths and every panel (open, dock, geometry).
  Window size is left to the window manager.
- `autosave_session` (per frame, checked once a second) rewrites the file
  whenever its text changed, so every opened page / tab / scroll lands in it
  within a second. It waits while page loads or a restored scroll are still
  pending, so a half-restored state never overwrites the saved one.
- `restore_session` (app_init) runs unless a document is named on the command
  line or `--new-session` is given; scroll waits for the page
  (`apply_pending_scroll`). `--no-save` writes nothing.
- Open: floating document windows are not restored; unsaved-draft tabs
  without a path are not listed.

## Status (2026-11-03) — paged reading (ebook style pages)

- F8 / View → Paged Reading (`Pane::paged`, default `ViewSettings::paged`,
  `page_columns` 0 = fit / 1–3): the view shows spreads of pages instead of
  scrolling. `paginate` (`paginate.hpp`) cuts the one-column layout into
  pages at line boundaries and stacks them as spreads of a plain
  `LayoutResult`, so hit-testing / carets / selection / overlays are
  untouched; `scroll_y` is a multiple of the spread height. Design and
  open points: `docs/PAGED.md`.
- Bar below the page (`app_pages.cpp`): `Pages 7–8 of 78`, seek slider,
  `Loc 42 of 539 · 10%` (locations = glyphs / 128, font independent), ‹ ›.
  Turn: PgDn / PgUp, wheel, click on the page edges (arrow on hover), swipe,
  ← / → in browse mode; caret moves, outline / search / link jumps turn to
  the page of their target. Zoom / resize / column change keep the reading
  position (view anchor).
- Checked under Xvfb (1024×768 book of 12 chapters): two columns, turn by
  key / edge / wheel / slider, zoom to one column, F8 back to scrolling at
  the same spot, Ctrl+End / Ctrl+Home. Unit test `test_paginate`.
- Open: whole page re-laid out per change (no virtualization while paged);
  no chapter ticks on the slider; no page sheet / margin background; pop-out
  / tear-off copy `scroll_y` between paged and scrolling views; the footnote
  strip overlays the page foot; touch swipe not tried on a device.

## Status (2026-11-03) — page margins, article gaps (style sheets)

- `(page (margin-background …) (margin-image "f.png") (image "p.png"))`:
  the paper is the text column's sheet (`LayoutResult::page_x/page_w`), the
  margin colour / tiled pattern shows around it and behind margin boxes.
  `Renderer::draw_backdrop` paints it (pane raster, help window); without a
  margin the old full-pane paper fill.
- `(role article (space-before …) (space-after …))` → `page_gaps`: bands where
  the margin shows between articles (leading / trailing gap extends to the
  page edge). Default spacing 0, so nothing changes without a sheet.
  `styles/desk.xss` shows it; unit tests in `test_layout_article_gaps`.
- Open: web pages only get gaps where `<article>` becomes an Article node
  (not checked on real pages); floating windows / footnote strip still fill
  plain paper; no sheet border / shadow yet.

## Status (2026-11-03) — top / bottom docks, tabs ↔ windows

- `Dock::Top` / `Dock::Bottom`: bands between the side columns (windows
  side by side, height `App::dock_w[2|3]`, resize at the edge facing the
  panes); drop zones at the top / bottom edge (the middle of the top edge
  still maximizes unless a top band exists); `place_centre_rows` leaves the
  bands out of the panes.
- Maximize / restore box on every window frame (left of ×); title buttons
  moved one box left.
- Tabs: drag to reorder (ImGui Reorderable, ids = buffer ids); drag a tab
  down out of the strip → window (docks at a zone, preview while
  dragging); drag a document window onto the strip → tab again.
- Checked under Xvfb (dock bottom, maximize / restore, reorder, tear off,
  window → tab). Open: dropping a window on the strip appends the tab
  (no insertion point); the strip highlight is hidden under ImGui; only
  Document windows turn into tabs (panels stay panels).

## Status (2026-11-03) — side boxes in the margin, feature tour

- Asides no longer float inside the column by default: the style roles
  `aside` and new `sidebar` have `placement margin` (`(side …)` and
  `(placement margin|float|flow)` replace the one-commit-old `(float …)`).
  A margin box sits beside its flow position, does not move the text and
  stacks with the other boxes on its side. `setup_margins` takes width
  from the text column when the margin is too narrow (column ≥ 28 em, box
  ≥ 8 em), else the box goes into the flow at full width. `float` stays as
  the HTML compatibility placement.
- Web page chrome: navigation / sidebar Chrome (`margin_chrome`: not with
  tables, code, columns or collections inside) is not margin text but a
  separate dockable, resizable "Page" panel (View → Page Navigation,
  Ctrl+Alt+P; `app_page.cpp`, `WindowRole::Page`, docked left by default,
  like frames): link rows grouped by region, Enter / click follows the
  link. While it is open the panes leave those regions out; headers,
  footers, forms stay in the flow. (An earlier margin `sidebar` role was
  dropped for this.)
- Distinct look: `FontRole::Small` … `SmallCode` (body faces at 0.85 ×,
  `(font small)` …), the box role's colour for the text inside
  (`ctx_role_`), fill + frame for asides; Night and High contrast sheets
  set both roles.
- Includes of a container (`:::include #section-id`) show its first text
  block (`transclude_text_node`), was "[missing transclusion]".
- Examples: `examples/tour.md` is the one long feature tour (sections,
  inline markup and images, lists / tasks, deflist, note, code, tables,
  side boxes, stretchtext, columns, figure, collections, include, thread,
  footnotes, citations); boxes / sectioning / footnotes / collection /
  thread-rfc are merged into it (Help → Examples, welcome.md).
  `test_tour_example` keeps it showing every block kind.
- Checked under Xvfb: tour at 1280 px (right and left margin asides,
  include, collections, thread, footnote overlay), a local page with
  `<nav>` + `<aside>` (aside in the right margin; nav in the Page panel).
- Footnotes are set in the small face (`footnote` role: `(font small)`,
  muted colour; the key in the margin column too), so notes and the
  overlay strip read as secondary text.
- Every page under `examples/` is installed (the whole directory) and is
  listed in Help → Examples (`include/xanatoo/examples.hpp`, used by
  `ui.cpp`); `test_examples_in_menu` fails when a page is missing there
  or a menu entry has no file. Biblio entries must stay on one line.
- Open:
  - Page panel: hides the regions in every pane (not only the focused
    one); only link and text rows, no nested folding; no hover preview.
  - Margin boxes inside a skipped (off-screen) section are not laid out,
    so a tall box from above the viewport can be missing and stacking
    may differ from the full layout.
  - List bullets inside margin boxes keep the body size.

## Status (2026-11-03) — inline images and asides (layout, HTML 3.0 flavour)

- Inline images: `InlineKind::Image` span (`text` = U+FFFC, one caret
  position; `src` / `alt` / `width` / `height`, `href` = optional link),
  made by `inline_image()`. Layout treats it as an atom on the baseline
  (taller images lower their line), `PositionedImage::in_text` (no resize
  handle). Click: its link, else the viewer (which now steps through inline
  images too). Typing next to one makes a text span.
- Markdown `![alt](src){width=W height=H}` inside text, `[![alt](src)](href)`
  linked; a line holding only an image is still a block Image. sexp
  `(img "src" :alt … :width … :href …)` inside text; org writes `[[src]]`,
  gemtext `[alt]`. HTML: icons (declared edge ≤ 48) and images in list
  items / cells / quotes are inline spans now (was `[image: alt]` links);
  percent `width` / `height` are ignored.
- Asides: `NodeKind::Aside` (container of blocks; `src` = side hint).
  `:::aside [side=left]` / pandoc `::: aside`, sexp `(aside :side … (p …))`
  (a text-only `(aside "…")` stays a Note). HTML `<aside>` inside `<main>` /
  the lone article is an Aside (`HtmlOptions::asides`), outside it or with
  chrome classes still Chrome.
- Style role `aside`: `width` (16em), `float` (right / left / none),
  `indent` = padding, `background` / `border` (new `role_frames`). Floats
  only while the box is ≤ 45 % of the column and ≥ 10 em of text stay
  beside it, else it sits in the flow at full width.
- Floats in layout: `LayoutEngine::floats_` per layout() call;
  `layout_paragraph` places every line in the band the floats leave free
  (`float_band`), blocks with a box of their own (tables, code, images,
  figures, columns, threads, rules, transclusions) start below
  (`clear_floats`). Asides are never skipped by virtualization.
- Fixed on the way: a space that overflowed a line started the next one.
- Merges of column / cell layouts go through `append_result` (cells now
  keep message bars / cards and nested column gaps too).
- Checked under Xvfb with `examples/boxes.md` (now part of `tour.md`): right and left floats, wrap
  beside and below, fallback at a narrow width, images in table cells,
  click → viewer (1 / 3).
- Open / next (HTML 3.0 as the yardstick, not a clone):
  - `align=left|right` on `<img>` / `<figure>` / `<table>` (and Wikipedia's
    `infobox` tables) as floats; `<br clear=…>` / a `clear` for blocks.
  - `align=top|middle` for inline images (only baseline now).
  - List markers and quote / note bars beside a *left* float stay at x;
    header bands and message cards run under floats.
  - A failed inline image shows an empty box, not its alt text.
  - Copying text copies U+FFFC for an image (plain_text keeps it so char
    offsets hold); a paragraph of only an inline image saves as a block
    image line.
  - Floats inside a skipped (off-screen) section are not registered, so the
    first lines after it may wrap differently from the hint.

## Status (2026-11-03) — reader mode for web pages

- HTML pages keep their chrome (nav / aside / footer / form / dialog,
  landmark roles, navbox / share / cookie classes, what lies around `<main>`)
  as `NodeKind::Chrome` nodes instead of dropping it; whole pages are the
  default now. Hidden / screen-reader-only text, `alt=""` images and
  tracking pixels are still dropped; EPUB chapters still drop the chrome.
- Reader mode is per-view state (`Pane::reader`, default
  `ViewSettings::reader_mode` = `reader_mode` in view.conf, Preferences):
  F9, View → Reader Mode, toolbar book button. Layout hides the Chrome
  nodes and puts "Reader mode: N parts of the page hidden (…) · Show all"
  at the top; the outline follows. Not part of the address
  (docs/PASSAGES.md "Reader mode"). Jumps and find hits into hidden chrome
  leave reader mode.
- Checked under Xvfb on a local page: full page, F9, Show all, tooltip.
- Open: per-region markers ("▸ navigation") to open one region; default
  per host; Wikipedia native view could default to reader mode.

## Status (2026-11-03) — passages: text selectors, bookmarks of a passage (slice 3)

- `text:` selectors resolve (`resolve_text`, web text-fragment grammar,
  whitespace / ASCII case folded) and are made by `text_selector` (short
  range whole, long range first + last four words, context until unique;
  empty selection = a point `prefix-,,-suffix`). `#:~:text=` is an alias.
- `Pane::marks` is a list of ranges drawn like the selection: `#text:` marks
  the range, `#id:` the node's text. A `//text:` pipe cuts to the smallest
  block holding the range (clipping it is slice 4).
- `Bookmark` record (address, title, quote, date); the `bookmarks` file keeps
  one address per line with tab-separated `title=` / `quote=` / `at=`, so
  old files still load. Ctrl+Shift+B bookmarks the selection, else the page
  (menu label said Ctrl+Alt+B before); Bookmarks → Bookmark Reading Position
  stores a point at the caret. Panel and menu show title + quote.
- Checked under Xvfb: selection and reading-position bookmarks land in the
  file with title and quote, the panel lists them, reopening a selection
  bookmark marks exactly that range.
- Bookmarks inside the page shown are marked: green wash (`bookmark` page
  colour of the style) over ranges, a ribbon at reading positions; found
  again after edits once typing pauses (400 ms, hidden meanwhile). View →
  Bookmarks in the page / Preferences (`bookmark_marks` in view.conf). The
  star is lit for bookmarked passages too, its tooltip counts them.
- Margin ribbon per bookmark (left of the drag grip): tooltip with title +
  quote, click selects the passage, right-click menu (Select Passage, Go to
  Bookmark, Show Only This, Copy Address, Remove, Show Bookmarks…).
  Scrollbar ticks per bookmark, estimated off the materialised band.
  Checked under Xvfb, including ticks on a 120-paragraph page.
- Open: the ribbon needs a margin; with a narrow `margin_pct` it lands
  on the grip. "Moved" resolution (nearest of several matches); bookmarks
  panel grouped by document; Open as cut.
- Next: slice 4, clipped text cuts, "Show only this", Copy address.

## Status (2026-10-31) — passages: cuts, marks, addresses (slices 1 + 2)

- Design and the remaining slices in `docs/PASSAGES.md`. Bookmark, selection,
  transclusion and image crop share one address: `//selector` cuts (the
  passage as a document of its own), `#selector` points (whole document,
  passage marked).
- `locator.hpp` (`parse_locator`, `format_locator`, `locator_up`,
  `resolve_node`, `node_selector`), unit-tested. `text:` / `rect:` / `at:`
  parse; `id:` and `path:` resolve here (`text:` in slice 3), the others
  report "Passage not found".
- `app_passage.cpp`: selectors are view state on `Pane`, only the base gets
  opened or fetched. `navigate_pane` splits the address, same page = no load.
  Location bar, history, bookmarks, Copy URL use `pane_address`. Up steps
  `a//id:b` → `a#b` → `a`, holding the text on its spot. Section Focus moved
  from `App` to `Pane` and is a cut now, so it shows in the location bar.
- `#` marks whole nodes in the page style's `mark` colour; a click dismisses
  it. Remote `#fragment`s are marked as well. Cuts of non-section blocks work
  in layout. `xanatoo 'a.md//id:b'` works on the command line.
- Checked under Xvfb with a local file: cuts, chains, `#`, Up, Back, Section
  Focus, bookmarking a cut, lost selectors. Not checked: remote pages, split
  browsing, a passage across Reload.
- Open: a link opened in a new tab shows the whole document (pipes dropped);
  a cut still shows the headers of parent sections; the caret can leave a cut
  with the keyboard (as in Section Focus); Forward/Up from a hover preview's
  cached page refetches when the URL had a fragment.

## Status (2026-10-31) — view anchors (zoom keeps the text in place)

- `ViewAnchor` (document position + spot in the pane), `view_anchor_at`,
  `keep_views`; `layout_panes` scrolls a held anchor back to its spot,
  relaying out while that leaves the materialised band. Pinch holds the
  text between the fingers from the first to the last step; zoom keys, view
  settings, style change, collection view, arriving images and pane width
  changes hold each view's top line.
- `zoom_by` scales the height hints instead of dropping them (no full layout
  per pinch step on a whole book); `ensure_virtual_layout` holds the view
  while the estimates become measured heights on scrolling.
- Checked under Xvfb with an injected pinch on a 3000-section document
  (LD_PRELOAD shim for fake touches), not yet on a real touchscreen. The
  anchor is one character, so a rewrap can still shift the lines around it
  by one line per zoom step. Folding does not use the anchor yet.

## Status (2026-10-31) — built-in EPUB reader

- `src/epub_native.cpp` (`epub_to_markdown`): zip + OPF spine → chapters
  through `parse_html_page` → cached Markdown. `import_via_pandoc` uses it
  for `.epub` only when pandoc is missing, so Android reads books; desktop
  stays on pandoc. Other formats still need pandoc.
- Open: run it on a device; footnotes are plain jumps (no `[^n]`), ids are
  not made unique across chapters, Zip64 is not read. A truncated download
  stays in `cache_dir()/downloads` (seen with one Standard Ebooks file).

## Status (2026-10-29) — startup report (gamepkgstd LOGGING.md)

- `print_startup_report` (`app_report.cpp`) runs first in `app_init`:
  `topic: details` lines on stderr with runtime vs built-against library
  versions and the mapped `.so` (from `/proc/self/maps`), video, renderer,
  GL vendor / renderer / version, controllers with the mapping SDL applies
  and where its bindings come from, directories. Font / icon / style /
  gamepad lines moved to stderr in the same format; stdout is line
  buffered so a piped log keeps its order.
- First R36S report: Mali-G31, OpenGL ES 3.2 (`libmali-bifrost-g31-rxp0-gbm`),
  KMSDRM 640x480 @ 61 Hz; with the preloaded SDL 2.30.10 the pad's GUID
  gains a CRC, but SDL still applies PortMaster's SDL_GAMECONTROLLERCONFIG
  bindings (same layout as on 2.0.10).
- `nix build .#xanatoo-android` builds (new code: `SDL_GetAndroidSDKVersion`,
  `ro.product.model`); the report has not been looked at in logcat yet.

## Status (2026-10-29) — R36S: ImGui via runtime SDL_RenderGeometryRaw

- Scrolling on the R36S was much smoother with the chrome hidden: built
  against eoan's SDL 2.0.10 headers, the ImGui backend drew every triangle
  with its own SDL_RenderCopy / FillRect (+ colour-mod changes), thousands
  of GLES draw calls per frame for menu bar + toolbars.
- The backend now looks up `SDL_RenderGeometryRaw` in the SDL loaded at
  runtime (2.0.18+, ArkOS ships its own build) and only falls back to the
  per-triangle path without it; it logs `imgui: SDL x.y.z at runtime, …`.
- The device (ArkOS on the R36S) has SDL 2.0.10, so the fallback runs.
  Made cheap: no anti-aliasing / rounding while it is active
  (`ImGui_ImplSDLRenderer2_UsesTriangleFallback`), a quad's two triangles
  in one call, colour mods only on change, and the whole UI drawn into a
  target texture only when the draw data hash changes, blended in with one
  copy otherwise. Desktop measurement at 640x480 (fallback forced with
  `-DIMGUI_SDL_RENDERER_LEGACY=1 -DIMGUI_SDL_RENDERER_FORCE_FALLBACK`):
  ~2420 SDL draw calls per frame before, 320 per UI redraw now, 1 copy
  while only the page scrolls. Not yet measured on the device.
- On the device (help.md, left stick held via evdev injection): previous
  fallback ~20 FPS idle and scrolling; cached chrome 61 FPS (vsync);
  ArkOS' SDL 2.30.10 61 FPS too.
- `Xanatoo.sh` preloads the newest `libSDL2-2.0.so.0.*` in the multiarch
  dir when it is newer than the default link (ArkOS: 2.30.10 over eoan's
  2.0.10), so the R36S gets real RenderGeometry (rounding, AA, arrows).
  The cached fallback stays for firmwares without a newer SDL.
- Open: non-rectangular triangles (menu arrows, check marks) still draw as
  their bounding boxes on the fallback.
- Open: stick scrolling is per frame (18 px), so it slows with the frame
  rate; scale by frame time.
- Open: `pm_finish` / `pm_platform_helper` are missing in this device's
  PortMaster, so gptokeyb outlives a quit started outside gptokeyb.
- Later: SDL3 (desktop already runs on it via sdl2-compat; R36S would have
  to bundle libSDL3 for KMSDRM / Mali; Android 5 = API 21 is SDL3's floor).

## Status (2026-10-29) — browse-mode chrome, content only

- Browse mode hides the edit toolbar row, the Format / Insert menus and
  the editing entries of the context menu; Find, Split, Outline and View
  tools (now also holding line spacing / text width / style sheet) sit on
  the browse row, so it stays one row no wider than before.
- Content only (`App::chrome_hidden`, `UiCommand::ToggleChrome`, F11,
  gamepad X — which used to toggle the outline): no menu bar, toolbars,
  tabs; the status line (`status_bar_h` 0) returns while a prompt or
  notice is up. Right-click menu / View menu leave it.
- Open: touch-only Android has no F11 / X — needs a gesture (e.g. tap on
  the top edge) to bring the chrome back; maybe remember the switch in
  view.conf.

## Status (2026-10-29) — versioning (gamepkgstd VERSIONING.md)

- `VERSION` (`0.1.0-dev`) is the only place the version is written
  (VER-1); `flake.nix` derives `0.1.0-dev.<revCount>+g<shortRev>` (VER-5)
  and passes it as `-DPROJECT_VERSION_FULL` to the native and R36S builds
  and as `versionName` / `XANATOO_VERSION` to Android; CMake falls back to
  the bare `VERSION` contents (VER-6). `xanatoo --version` prints it,
  `--help` goes to stdout with exit 0 (OPT-2); `checks.version` /
  `checks.help` verify both (FLAKE-11).
- gamepkgstd audit (2026-10-29), fixed: one directory module
  (`paths.hpp`: config / state / user data / cache, XDG only when
  absolute, `--*-dir` options, `--portable` / `portable.txt`, `--no-save`,
  `--show-paths`); state (revisions, sidestore, bookmarks, minibuffer
  history) moved from the config dir to `XDG_STATE_HOME`, user style
  sheets to `XDG_DATA_HOME` (existing files moved by hand, no migration
  code); installed data via `--data-dir` / `<exe>/../share/xanatoo` /
  install prefix, never the cwd (the build tree mirrors that layout;
  `XANATOO_DATA_DIR` / `XANATOO_ICON_DIR` are gone); atomic writes
  (`write_file_atomic`); options per OPTIONS.md (`-f` fullscreen,
  `--windowed`, `--geometry`, `--`; `--font` has no short form); man page
  `docs/xanatoo.1`; User-Agents carry the version (`user_agent()`); REUSE
  (`REUSE.toml`, `LICENSES/`, `checks.reuse`); flake `checks` = packages +
  apps + version / help / man / reuse / layout / integration, `doCheck`,
  wrapped default package, `imagedeps-android` naming; metainfo has no
  unreleased `<release>`; GitHub CI runs `nix flake check`; PortMaster
  launcher keeps config / state / data / cache apart under `conf/`.
- Deliberate deviations (README "Packaging standard"): Android
  `versionCode` stays `self.lastModified` (VER-7); man page section 1.
- Open: `nix flake check` builds the Android APK
  and R36S cross packages — heavy for GitHub runners (disk / time), may
  need a split CI job; the R36S build now passes again (it had broken:
  no SQLite in the sysroot, thumtoo needs libvips, code is C++20 but the
  build forced C++17): eoan SQLite debs + `libs/libsqlite3.so.0` in the
  port, `XANATOO_THUMTOO=OFF`, `-DCMAKE_CXX_STANDARD=20` against
  libstdc++ 9 (language features only, see gamepkgstd `R36S.md`); works
  on the device, as does Android with the new paths; both may want UI
  polish for small screens.

## Status (2026-10-29) — about: page

- Help → About xanatoo opens `about:` (`app_about.cpp`, read-only,
  regenerated on Reload): version, every service (also the always-built
  ones, with an example link while enabled, the CMake switch while not),
  document formats (pandoc found on PATH + its extensions), images
  (thumtoo version, libvips version + loader suffixes, MuPDF / DjVuLibre,
  remote images), platform (SDL / video driver / renderer, FreeType,
  FontConfig, SQLite, ImGui, TLS backend, cache / settings dirs).
- Open: services as a registry (one table instead of the per-service
  code in `is_in_app_remote_url` / dispatch) would let this page and
  later plugins list them from data.

## Status (2026-10-29) — Nostr links / media, remote images, thumtoo on Android

- Nostr: author names link to `nostr:npub1…` (author feed: profile +
  recent notes), dates to `nostr:note1…`; note text linkifies URLs,
  NIP-27 refs and NIP-08 `#[n]`, labels media from NIP-92 `imeta`.
- Remote images (`app_images.cpp`): `ImageCache` queues http(s) URLs,
  workers download into `cache_dir()/images/` + 512px thumbnail, pruned
  at startup. stb_image only (no WebP / AVIF). Not yet used by web pages.
- Plan: replace the stb path with vendored thumtoo
  (`~/projects/thumtoo/thumtoo.git`, git subtree in `external/thumtoo`
  like galapix / biltoo) behind the same `ImageCache` seam, and adopt its
  Location model (`//` pipes, e.g. `book.epub//markdown` for pandoc).
- Android probe (aarch64, API 21, NDK 27): nixpkgs cross set
  (`crossSystem.useAndroidPrebuilt`) + `makeStaticLibraries` stdenv (not
  `pkgsStatic`: that is musl). Builds statically: zlib, libffi, pcre2,
  GNU libiconv (bionic has none before API 28), expat, libjpeg-turbo,
  libpng, libwebp (library only), glib 2.88 (meson, proxy-libintl in
  `subprojects/`, no libmount / selinux / xattr), libvips 8.18 (JPEG / PNG
  / WebP / GIF only, `prefer_static`), libarchive 3.8 (zlib + iconv,
  `-I contrib/android/include`), SQLite amalgamation, **libthumtoo**
  (no MuPDF / DjVu / curl / tesseract / dbus / unarr); `thumtoo-prepare`
  links (13 MB unstripped). ~20 MB of `.a` files.
  nixpkgs' own glib / libarchive / sqlite / libjxl / libwebp derivations
  fail (attr, elfutils, bash, tools) — minimal derivations needed.
- thumtoo `3cb19f1`: ladder levels are JPEG (WebP with alpha) instead of
  JXL, so libvips without libjxl suffices; the Android probe rebuilds.
- thumtoo `f29ec55`: tiles of images with alpha are WebP (`CodecId::Webp`),
  `get_pixels_from_tiles` keeps alpha. Vendored as subtree `external/thumtoo`.
- Device run (Kindle Fire KFAUWI, armeabi-v7a only, API 22): static
  `thumtoo-prepare` (armv7a probe; libjpeg-turbo needs `ENABLE_SHARED=0`
  and `-DHWCAP_ARM_NEON=4096`) sizes, ladders and tiles files and CBZ
  members into SQLite.
- Desktop integration (`XANATOO_THUMTOO`, `images_thumtoo.cpp`): libvips
  decode (WebP / TIFF / … inline), async thumbnails from coarse tiles in
  the shared `$XDG_CACHE_HOME/thumtoo` cache; warm start shows them at once.
- Android: `nix/android-imagelibs.nix` (C libs per ABI → one
  `libimagedeps.a`; x86_64 needs the cc-wrapper's `-mtls-dialect=gnu2`
  removed) + `mk/android/thumtoo/Android.mk` (thumtoo's C++ under
  ndk-build, xanatoo's sqlite3); APK 34 MB (libmain.so 13 MB armv7).
  Kindle Fire: welcome / collection images through libvips, thumbnails
  from tiles. Found and fixed upstream (thumtoo `fe678e4`): concurrent
  probes got each other's row ids (`last_insert_rowid` on the shared
  connection) → one image's tiles under another file.
- Open: R36S (eoan sysroot, `std::span` shim for GCC 9); remote images
  could go to thumtoo directly (it has a curl fetcher) instead of
  `cache_dir()/images/`.

## Status (2026-10-29) — desktop integration (gamepkgstd INTEGRATION.md)

- App ID `xanatoo` everywhere: `data/xanatoo.desktop` (opens files: `%F`,
  Markdown / Org / Gemini MIME types), `data/xanatoo.metainfo.xml`,
  hicolor icons 16–256 + scalable, installed by CMake. One icon source
  `data/icons/xanatoo.svg` (was `mk/android/app/ic_launcher.svg`);
  `tools/make_icons.py` regenerates the committed hicolor and Android
  mipmap PNGs. `main()` sets the WM class / app ID hints by name (SDL2 has
  no macros for them; sdl2-compat reads `SDL_APP_ID`) and the 64×64 window
  icon. Flake `checks.layout` / `checks.integration` (desktop-file-validate,
  appstreamcli). PortMaster gets `xanatoo/cover.png` and `screenshot.png`.
- Android `package` is now `org.grumbel.xanatoo` (was
  `io.github.grumbel.xanatoo`: no hosting provider in IDs); it installs
  as a new app beside the old one, which keeps its data.
- Open: no adaptive icon (INT-14); PortMaster launcher `Xanatoo.sh` vs display name
  `xanatoo` (INT-15); no `--version` / `version` check; no Windows build.

## Status (2026-10-29) — middle-click opens links in a new tab

- Middle-click on a link (`open_link_in_tab`, `app_nav.cpp`) adds a tab
  right after the pane's current one and keeps the view, like a browser
  background tab. Local docs load at once (an open buffer is reused);
  remote pages get a "Loading …" stand-in buffer filled by a
  `FetchPurpose::Tab` job (`FetchJob::buf`, `finish_tab`; an error page
  on failure, nothing if the tab was closed or navigated meanwhile).
  Anchors into the page itself just jump; external links open outside.
  Off links middle-click still pastes. `resolve_link` / `LinkTarget`
  moved from `app_split.cpp` to `app_nav.cpp` (shared).
- Open: several middle-clicks stack each new tab right after the current
  one (reverse order); a `file#frag` fragment is not applied in the new
  tab; no loading indicator on a pane that shows a stand-in tab.

## Status (2026-10-29) — split history

- Split browsing Back / Forward walk one shared trail (`SplitHistory`):
  each level is a left page plus the pages opened from it on the right.
  Back steps through those, then slides back a level (left page moves
  right, `move_page` keeps its buffer); Forward retraces. A new click
  drops the Forward part. Entries keep caret / scroll (restored once a
  remote page lands) and the link that opened them (trail mark restored).
  Status "n/m" counts trail stops. Applies whichever centre pane has
  focus; windows keep `Pane::nav`.
- Reset when split browsing ends; per-pane `Pane::nav` is still kept up to
  date, so the single pane's Back works afterwards.
- Open: tab switches are still not navigation (not recorded in the trail).

## Status (2026-10-29) — split trail mark

- Split view marks the left-pane link whose target the right pane shows
  (`App::split_trail`, `SplitTrail`): orange wash + underline and a ▸ at
  the pane edge. Set by left clicks and by right clicks (the page moves
  left with its clicked link); dropped once either pane shows another
  page (`check_trail` in `update_split_preview`; remote targets are taken
  once the fetch lands). Hidden while a hover preview shows.
- Its target is marked on the right too (`SplitTrail::target`, the caret
  node of an anchor / `file#frag` jump): same wash, ◂ at the pane edge.
  Remote `#fragment` targets are not marked (no jump there yet); the
  passage mark of `docs/PASSAGES.md` does that now.
- Open: a failed remote fetch leaves the mark on the old right page.

## Status (2026-10-29) — scrollbar chapter marks per view

- Marks blinked: they came from `App::outline` (built from the focused
  pane, refreshed only while the Outline panel is open), so other views
  and freshly loaded pages showed node ids of a different document.
  `section_marks` (`app_frame.cpp`) now walks each pane's own document:
  layout box Y where a box exists, height-hint estimates only inside a
  stub box, nothing for blocks the layout left out (section focus).
  The preamble row no longer gets a mark.

## Status (2026-10-29) — pandoc raw HTML residue

- EPUB imports showed `<hgroup>` / `<footer>` / `<svg>` tag lines
  (Standard Ebooks, Gutenberg). Pandoc's EPUB reader keeps them as raw
  HTML and the gfm writer prints them despite `-raw_html`. Every pandoc
  import now runs `-L strip-raw-html.lua` (`kStripRawHtmlLua`), `kFormat`
  bumped to re-import caches. Pandoc quirks are collected in
  `docs/PANDOC.md`. Open: escaped HTML in the front-matter description;
  check whether `sanitize_epub_markdown` is still needed; report the two
  pandoc issues upstream.

## Status (2026-10-29) — document windows are full views

- `Pane` is the one document view: `App::panes[kMaxViews]`, slots 0 / 1
  centre, the rest owned by document windows (`FloatWindow::view`; the
  window's own buf / sel / layout / needs_layout are gone). Each view has
  its own rect (`Pane::y` / `h` next to `x` / `w`), so virtualized layout,
  raster cache, caret, editing, selection drag, links (navigate inside
  the window), hover, footnote overlay, scrollbar and load indicator all
  work in windows, docked or maximized.
- `app.focus` can be a window view; `centre_focus` is what tabs, "Move
  Window to Tab" and closing a window fall back to. Tab (outside lists /
  tables) cycles live views. Split preview, diff and the shared-doc
  caret mirror stay centre-only.
- Pages opened inside a window still join the tab strip (tabs = buffer
  list). Open: per-slot tab strips, `ViewId` relations (BRAINSTORM §11);
  per-view history done (below).
- Ctrl+Shift+P toggles: pop out, or (caret in a window) back to a tab
  (Ctrl+Shift+I stays Insert Image).

## Status (2026-10-29) — per-view Back / Forward

- `Pane::nav` (`NavHistory`): every view (centre panes, document
  windows) has its own trail; `push_nav_history(app, pi, url)` records,
  `nav_step` / `can_nav_step` walk the focused view. The global list is
  now `App::visited`, a session log for the History panel only.
- New window: trail starts at its page; pop out copies the tab's trail;
  Move Window to Tab pushes the page onto the centre slot's trail; split
  "shift left" pushes the moved page onto the left pane's trail.
- Status line "3/7" counts the focused view's trail.
- Open: tab switches are not navigation (no trail entry); a tab strip per
  centre slot would make the trail and the tab list one thing.

## Status (2026-10-29) — preferences, hover previews off

- Split-view hover previews are now opt-in (`ViewSettings::hover_preview`,
  `hover_preview=` in view.conf, default off): the right pane changing
  without a click confused.
- Edit → Preferences…: ImGui window with checkboxes (hover preview,
  footnote overlay, justify); each issues the existing toggle command.
- ImGui UI font now loads General Punctuation, Arrows and Geometric
  Shapes, so "…", "→", "▸" in menus no longer draw as "?".

## Status (2026-10-29) — site maps

- `sitemap.hpp` / `src/sitemap.cpp`: robots.txt `Sitemap:` lines (else
  /sitemap.xml, /sitemap_index.xml, /wp-sitemap.xml), indexes followed
  (≤ 25 files, ≤ 50k URLs), URL list → path tree; `sitemap:host` page
  with "Recently changed" (by `lastmod`) and one Section per directory.
- Go → Site Map (Ctrl+Alt+M): `WindowRole::Sitemap` panel, docked right
  by default; follows the focused page (reveals and bolds it), ↑↓ ←→
  Enter, click twisty folds, "Page" opens the document form.
- `.xml.gz` sitemaps through the built-in `inflate.hpp` (gzip / zlib /
  raw DEFLATE, no zlib dependency).
- Pages shown this session are recorded (`note_page_seen`, from
  `show_doc_in_pane` + the requested URL of a redirect): the panel labels
  them by title (site suffix "Post | Site" cut when a heading matches),
  marks them with a dot and counts "seen/total" per directory.
- Alt+PgUp / Alt+PgDn (Go menu): previous / next page of the site in
  tree order, panel open or not (starts reading the map on first use).
- Open: seen pages are per session (persist with history?); sitemap
  files are fetched one after another (nytimes.com ~40 s) — parallel
  fetches or a progress count; hover previews count as seen.

## Status (2026-10-29) — one docking model for all windows

- `FloatWindow::dock` (`Dock::None / Left / Right / Max`) for every role
  replaces `SoftDock` (doc windows only, overlay tiling) and
  `outline_docked` / `outline_dock_w` (outline only, reserved column).
- Left / Right docks are real side columns: `layout_panes` leaves out
  `dock_column_w`; windows on one side stack (`dock_seq`, drop position
  picks the slot), the inner edge resizes the shared column
  (`App::dock_w`, panes keep ≥ 320 px). Drag a title to an edge or onto a
  column to dock; drag a docked title > 8 px to tear it off at its free
  geometry; double-click a title → Max ↔ previous placement.
- Dropped: Top / Bottom half-screen tiles of doc windows (panes have no
  vertical extent yet). "Dock Window to Tab" → "Move Window to Tab".
- Design direction (views vs. arrangement vs. documents):
  `docs/BRAINSTORM.md` §11.

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

- Home (toolbar house, Go → Home, Alt+Home, `UiCommand::GoHome`) opens
  `home_page(app)` = welcome.md; the hook for a user-chosen home page later.
- Opening a directory no longer aborts (ifstream on a dir threw): `load_doc`
  renders a read-only Markdown index (dirs first, absolute links, dot files
  hidden); `navigate_pane` normalises to a trailing '/'.
- Open: non-document files in a listing (images, …) still open as text.
- Up (toolbar ↑, Go → Up, Alt+Up on read-only pages, `UiCommand::GoUp`):
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

## Window chrome unification (partial)

Done: ImGui windows (Preferences / History / Bookmarks) now share the canvas
windows' look — square corners, 12px scrollbar with the `scroll_trough`
palette colours, 8px padding; Backlinks uses `help_pad` like all other
canvas windows.

Still different (decide later): ImGui windows use ImGui's title bar and a
triangular resize grip, canvas windows use `theme::draw_window_chrome`
(gradient title, ridged SE grip, maximize box). Real unification means moving
Preferences/History/Bookmarks to `FloatWindow` roles (dialogs vs tool windows
should then differ explicitly, e.g. title colour). Close box is on the right
everywhere (Gnome2 HIG). Check the grip is actually visible on narrow docked
windows (`resizable` is false when docked, by design).

Update: canvas windows share `window_client_rect` (app.hpp): the sunken well
under the title, left inset only, scrollbar flush at the right edge, and a
`help_grip_h` strip at the bottom for the resize grip (room for a future
horizontal scrollbar). Not yet done: History / Bookmarks (ImGui) vs Outline
should become the same `FloatWindow` kind of list panel; ImGui windows have
no bottom grip strip. Not checked visually — look at it in the running app.
