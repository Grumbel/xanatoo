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

# User files stay with the port on the SD card (gamepkgstd DIR-10); the
# game applies its normal Linux rules to these.
export XDG_CONFIG_HOME="$GAMEDIR/conf/config"
export XDG_STATE_HOME="$GAMEDIR/conf/state"
export XDG_DATA_HOME="$GAMEDIR/conf/data"
export XDG_CACHE_HOME="$GAMEDIR/conf/cache"

# Bundled libraries the device may lack (libsqlite3).
export LD_LIBRARY_PATH="$GAMEDIR/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -f "$GAMEDIR/fonts/DejaVuSans.ttf" ]; then
  export XANATOO_FONT="$GAMEDIR/fonts/DejaVuSans.ttf"
fi

# PortMaster gamepad mapping for SDL's controller API
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# gptokeyb: Select+Start quits on devices that need it.
$GPTOKEYB "xanatoo" &
pm_platform_helper "$GAMEDIR/xanatoo"

# ArkOS installs several SDL2 builds side by side but points
# libSDL2-2.0.so.0 at eoan's 2.0.10, which lacks SDL_RenderGeometry (the
# UI then takes a slow fallback path). Preload the newest one; it satisfies
# the binary's libSDL2-2.0.so.0 by its soname.
sdl_preload=""
sdl_default=$(readlink -f /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0 2>/dev/null)
sdl_newest=$(ls /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.* 2>/dev/null | sort -V | tail -n 1)
if [ -n "$sdl_newest" ] && [ -n "$sdl_default" ] &&
   [ "$(printf '%s\n%s\n' "$sdl_default" "$sdl_newest" | sort -V | tail -n 1)" != "$sdl_default" ]; then
  echo "xanatoo: using $sdl_newest instead of $sdl_default"
  sdl_preload="$sdl_newest"
fi

# The port folder is the data directory (examples, styles, icons, font);
# with no file given, examples/welcome.md opens.
LD_PRELOAD="$sdl_preload${LD_PRELOAD:+:$LD_PRELOAD}" \
  ./xanatoo --fullscreen --data-dir "$GAMEDIR" "$@"
pm_finish
