#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds MuPDF as static wasm32 libraries (pthreads on) into PREFIX, which
# already holds build-imagelibs.sh's output (libjpeg-turbo headers).
# Env: PREFIX, MUPDF_SRC, NIX_BUILD_CORES
set -euxo pipefail

export EM_CACHE="${EM_CACHE:-${TMPDIR:-/tmp}/emcache}"
mkdir -p "$EM_CACHE"
P="${PREFIX:?PREFIX required}"
J="${NIX_BUILD_CORES:-${JOBS:-$(nproc)}}"

rm -rf mupdf; mkdir mupdf
tar -xf "$MUPDF_SRC" -C mupdf --strip-components=1
( cd mupdf
  # System libjpeg (ours): MuPDF's bundled one clashes with it at link time.
  # Base-14 fonts only (-DTOFU -DTOFU_CJK).
  emmake make build=release shared=no USE_SYSTEM_LIBS=no USE_SYSTEM_LIBJPEG=yes \
    HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_LIBCRYPTO=no HAVE_OBJCOPY=no \
    HAVE_PTHREAD=yes barcode=no OS=Linux \
    "XCFLAGS=-DTOFU -DTOFU_CJK -pthread -I$P/include" \
    "SYS_LIBJPEG_CFLAGS=-I$P/include" "SYS_LIBJPEG_LIBS=" \
    CC=emcc CXX=em++ AR=emar RANLIB=emranlib -j"$J" libs > build.log 2>&1 \
    || { grep -E "error|Error" build.log | head; tail -15 build.log; exit 1; }
  find build -name "*.a"
  cp "$(find build -name libmupdf.a)" "$(find build -name libmupdf-third.a)" "$P/lib/"
  cp -r include/mupdf "$P/include/" )

echo "==> done: $P"
