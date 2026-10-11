<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# xanatoo for the R36S (ArkOS) and other PortMaster handhelds

Native aarch64 build linked against an ArkOS-era (Ubuntu 19.10) userland.
Uses the device's SDL2 (KMS/DRM). Example documents, toolbar icons, and a
DejaVu font ship inside the port folder.

## Install

Copy `Xanatoo.sh` and the `xanatoo/` folder to `/roms/ports/`, or put the
zip into `ports/PortMaster/autoinstall/` and start PortMaster once.

## Layout

```
/roms/ports/Xanatoo.sh
/roms/ports/xanatoo/xanatoo          # binary
/roms/ports/xanatoo/examples/        # welcome.md, help.md, …
/roms/ports/xanatoo/icons/           # toolbar PNGs
/roms/ports/xanatoo/fonts/           # DejaVuSans.ttf
/roms/ports/xanatoo/conf/            # created at runtime (bookmarks, …)
```

## Controls

| Input | Action |
|-------|--------|
| D-pad / left stick | Move caret / scroll |
| A / South | Activate (link, stretch, …) |
| B / East | Browse mode toggle (when mapped) |
| Start | Help |
| Select+Start | Quit (gptokeyb) |

Touch and the on-screen keyboard vary by CFW. File → Examples in the menu
lists the bundled documents.

## Build (from the source tree)

```bash
nix build .#xanatoo-r36s
nix build .#xanatoo-r36s-portmaster
nix build .#xanatoo-r36s-portmaster-zip
```
