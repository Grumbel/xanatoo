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

## Continuity

Read `TODO.md` first. Record plans and outcomes there.
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
