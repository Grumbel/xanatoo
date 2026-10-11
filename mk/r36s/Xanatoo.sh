#!/bin/bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# xanatoo: PortMaster launcher for the R36S / ArkOS (and similar handhelds).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/xanatoo"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Config and bookmarks stay with the port on the SD card.
mkdir -p "$GAMEDIR/conf"
export XDG_CONFIG_HOME="$GAMEDIR/conf"
export XDG_STATE_HOME="$GAMEDIR/conf"

# Data next to the binary (examples, icons, font)
export XANATOO_DATA_DIR="$GAMEDIR"
export XANATOO_ICON_DIR="$GAMEDIR/icons"
# Body document font only. UI chrome uses ImGui ProggyClean (bitmap).
# Do NOT set XANATOO_UI_FONT to DejaVu — blurry at menu size on R36S LCD.
if [ -f "$GAMEDIR/fonts/DejaVuSans.ttf" ]; then
  export XANATOO_FONT="$GAMEDIR/fonts/DejaVuSans.ttf"
fi
unset XANATOO_UI_FONT
unset XANATOO_UI_SCALE

# PortMaster gamepad mapping for SDL's controller API
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# gptokeyb: Select+Start quits on devices that need it.
$GPTOKEYB "xanatoo" &
pm_platform_helper "$GAMEDIR/xanatoo"

# Open the welcome document when present so examples are immediately reachable.
if [ -f "$GAMEDIR/examples/welcome.md" ]; then
  ./xanatoo --fullscreen "$GAMEDIR/examples/welcome.md" "$@"
else
  ./xanatoo --fullscreen "$@"
fi
pm_finish
