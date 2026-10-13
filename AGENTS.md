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
| `app_buffers.cpp` / `app_view.cpp` / `app_nav.cpp` / `app_softwin.cpp` / `app_prompts.cpp` / `app_pointer.cpp` | document buffers, scroll/fold/outline, URL history + bookmarks, floating windows, minibuffer, cursor/gamepad |
| `theme.cpp` | classic palette (`Palette`) + stateless bevel/gradient/window primitives shared with the ImGui theme |

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

- `include/xanatoo/https.hpp` + `src/https.cpp` — OpenSSL HTTPS GET/POST plus `ssl_library_init_once` / `tcp_connect_host` for Gemini TLS (linked when any OpenSSL feature is ON).
- `include/xanatoo/json_min.hpp` — tiny JSON field/object helpers for read-only API clients (header-only).

## Shared string helpers (`util.hpp`)

- `starts_with` / `ends_with` / `starts_with_ci`
- `ascii_lower` (string or string_view)
- `trim_ascii` for string_view edges

## Coding rules

- GPLv3+ with short SPDX headers on every source file.
- Author: Ingo Ruhnke <grumbel@gmail.com>
- Co-authored-by: Grok <grok@x.ai> on every commit.
- Prefer clear data layouts over clever abstractions.
- No drive-by feature removal; discuss first.

## Tests

- `tests/unit_tests.cpp` — offline unit tests (string helpers, json_min, URL detection for Reddit/Lemmy/Gemini/Nostr).
- Build target: `xanatoo_tests` (`ctest` or run the binary).
- Does not exercise live network fetches.

## Develop shell (`nix develop`)

- PATH scripts: `xanatoo-configure`, `xanatoo-build`, `xanatoo-run`, `xanatoo-run-gdb`, `xanatoo-test`.
- Requires `XANATOO_SOURCE` (set in shellHook). Default build dir `/tmp/xanatoo-build`.
- See `docs/ENVIRONMENT.md`.

## Layout virtualization

- `LayoutEngine::layout` accepts optional `LayoutViewport` + height-hint map.
- Sequential containers (Document, Section, Article, Thread, List, Column,
  DefList, Biblio) skip off-screen children when a prior box height is known.
- Main keeps per-pane `height_cache`
- View display prefs (`ViewSettings` / `view.conf`): line spacing, margins, measure — layout only, not document format
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
Handover is via cumulative git bundles only.
Bundle names: `xanatoo-NNN.description-BASE.bundle` (formerly `glyphtoo-NNN…`).

## Nostr

- Uses shared `ssl_library_init_once` from `https.cpp` for WSS TLS.
- Read-only note/thread fetch gated by `XANATOO_NOSTR` (default ON; needs OpenSSL).
- Opens `note1` / `nevent1` / `nostr:…` / 64-hex via Ctrl+L → Thread+Message layout.
- No posting or key management in v1.

## Gemini

- Gemtext parser is always built (`.gmi` / `.gemini`).
- Network client is gated by `XANATOO_GEMINI` (default ON; needs OpenSSL).
- URL entry is Ctrl+L / `UiCommand::OpenUrlBar` → `navigate_to_url`.

## Reddit

- Read-only client via public `https://www.reddit.com/…/.json` (OpenSSL HTTPS).
- Gated by `XANATOO_REDDIT` (default ON; needs OpenSSL).
- Ctrl+L accepts listing or post URLs with or without `.json`.
- Maps posts/comments to Thread + Message nodes.
- Optional OAuth: `XANATOO_REDDIT_CLIENT_ID` + `XANATOO_REDDIT_CLIENT_SECRET` (script app) when anonymous `.json` returns 403.
- Optional UA override: `XANATOO_REDDIT_USER_AGENT`.

## Lemmy

- Read-only client via Lemmy **API v3** JSON (`/api/v3/post/list`, `/api/v3/post`).
- Gated by `XANATOO_LEMMY` (default ON; needs OpenSSL).
- Ctrl+L accepts `https://<instance>/c/<community>` and `/post/<id>`.
- Maps posts/comments to Thread + Message nodes.
- Optional UA override: `XANATOO_LEMMY_USER_AGENT`.
