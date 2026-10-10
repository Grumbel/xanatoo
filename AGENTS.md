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
