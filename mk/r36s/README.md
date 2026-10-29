<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# xanatoo for the R36S (ArkOS) and other PortMaster handhelds

Native aarch64 build linked against an ArkOS-era (Ubuntu 19.10) userland:
GCC 16 compiling C++20 against GCC 9's libstdc++ headers and library, so
the binary needs nothing newer than the device's glibc 2.30 / libstdc++.
Image decoding uses stb_image (PNG / JPEG / GIF / BMP): there is no libvips
in that sysroot, so no thumtoo.

Uses the device's SDL2 (KMS/DRM). ArkOS installs several SDL2 builds but
links the default to eoan's 2.0.10; `Xanatoo.sh` preloads the newest one
(e.g. 2.30.10), so ImGui gets `SDL_RenderGeometry` instead of the slow
per-triangle fallback. Example documents, toolbar icons, and a
UI/body font ship inside the port folder. The gamepad pointer is a drawn
sprite (not the system cursor — KMS/DRM has none).

## Install

Copy `Xanatoo.sh` and the `xanatoo/` folder to `/roms/ports/`, or put the
zip into `ports/PortMaster/autoinstall/` and start PortMaster once.

## Layout

```
/roms/ports/Xanatoo.sh
/roms/ports/xanatoo/xanatoo          # binary
/roms/ports/xanatoo/examples/        # welcome.md, help.md, …
/roms/ports/xanatoo/styles/          # style sheets
/roms/ports/xanatoo/icons/           # toolbar PNGs
/roms/ports/xanatoo/fonts/           # DejaVuSans.ttf
/roms/ports/xanatoo/libs/            # libsqlite3.so.0 (drafts, folds)
/roms/ports/xanatoo/conf/            # created at runtime: config/, state/
                                     # (drafts, bookmarks), data/, cache/
```

UI menus use DejaVu Sans when available (same as the body font).

## Network

Local `.gmi` / `.org` examples work offline. `gemini://` links need OpenSSL
(enabled in current R36S builds; the device must provide `libssl.so.1.1`).

## Controls

| Input | Action |
|-------|--------|
| Right stick | Drawn software mouse cursor (sprite) |
| A / South | Left click at cursor |
| B / East | Right click at cursor |
| D-pad | Move caret / navigate |
| Left stick | Scroll document |
| Start | Help |
| Guide | Virtual keyboard |
| Select+Start | Quit (gptokeyb) |

Touch and the on-screen keyboard vary by CFW. File → Examples in the menu
lists the bundled documents.

## Build (from the source tree)

```bash
nix build .#xanatoo-r36s
nix build .#xanatoo-r36s-portmaster
nix build .#xanatoo-r36s-portmaster-zip
```

## Later polish

Core PortMaster build is usable. Optional improvements:

- ImGui font size / face for 640×480 readability
- Gamepad bindings beyond the generic SDL controller map
- Virtual keyboard when no physical keyboard is attached
