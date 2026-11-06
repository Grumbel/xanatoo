#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds static SDL3 for wasm32 with Emscripten, offline from the nixpkgs
# source tree.
#
# Env: SDL_SRC, PREFIX, NIX_BUILD_CORES / JOBS
set -euo pipefail

export EM_CACHE="${EM_CACHE:-${TMPDIR:-/tmp}/emcache}"
mkdir -p "$EM_CACHE"

PREFIX="${PREFIX:-$PWD/prefix}"
mkdir -p "$PREFIX/lib" "$PREFIX/include"

if [ -z "${SDL_SRC:-}" ]; then
  echo "error: SDL_SRC required" >&2
  exit 1
fi

echo "==> SDL3 static (wasm32) -> $PREFIX"
cp -a "$SDL_SRC" SDL3-src
chmod -R u+w SDL3-src
mkdir -p build-sdl3
cd build-sdl3
emcmake cmake ../SDL3-src \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TEST=OFF \
  -DSDL_STATIC_PIC=ON
emmake make -j"${NIX_BUILD_CORES:-${JOBS:-$(nproc)}}"
emmake make install
cd ..

if [ ! -f "$PREFIX/lib/libSDL3.a" ]; then
  echo "error: no libSDL3.a under $PREFIX/lib" >&2
  exit 1
fi
echo "==> SDL3 ready under $PREFIX"
