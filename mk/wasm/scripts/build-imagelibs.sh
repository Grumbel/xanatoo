#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds thumtoo's libraries as static wasm32 libraries with Emscripten
# (pthreads on: thumtoo and the fixed-layout reader run worker threads):
# zlib, libffi, pcre2, glib, libjpeg, libpng, libwebp, expat, libarchive,
# libvips, SQLite, libunarr and DjVuLibre, all into one prefix (MuPDF: build-mupdf.sh).
# Used by nix/wasm-imagelibs.nix, offline from nixpkgs sources.
#
# Env: PREFIX, PROXY_LIBINTL (tarball), *_SRC (see below), NIX_BUILD_CORES
set -euo pipefail

export EM_CACHE="${EM_CACHE:-${TMPDIR:-/tmp}/emcache}"
mkdir -p "$EM_CACHE"
P="${PREFIX:?PREFIX required}"
mkdir -p "$P/lib/pkgconfig" "$P/include"
J="${NIX_BUILD_CORES:-${JOBS:-$(nproc)}}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PKG_CONFIG_PATH="$P/lib/pkgconfig" PKG_CONFIG_LIBDIR="$P/lib/pkgconfig"
export CFLAGS="-O2 -pthread -fPIC -Wno-implicit-function-declaration"
export CXXFLAGS="-O2 -pthread"

# Source trees are copied: nix store paths are read-only.
unpack() { # name source...
  local name=$1 src=$2
  rm -rf "$name"; mkdir "$name"
  if [ -d "$src" ]; then cp -r "$src"/. "$name"/; chmod -R u+w "$name"
  else tar -xf "$src" -C "$name" --strip-components=1; fi
}

# The build sandbox may have no /usr/bin/env: point scripts at our python3.
fix_shebangs() { # dir
  grep -rlI '^#!/usr/bin/env python3\?' "$1" 2>/dev/null | while read -r f; do
    sed -i "1s|^#!/usr/bin/env python3\?\$|#!$(command -v python3)|" "$f"
  done
}

cm() { # dir cmake-flags...
  local d=$1; shift
  ( cd "$d"
    emcmake cmake -S . -B b -G Ninja -DCMAKE_INSTALL_PREFIX="$P" \
      -DCMAKE_PREFIX_PATH="$P" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED_LIBS=OFF \
      -DZLIB_LIBRARY="$P/lib/libz.a" -DZLIB_INCLUDE_DIR="$P/include" "$@" > cmake.log 2>&1 \
      || { tail -30 cmake.log; exit 1; }
    cmake --build b -j"$J" > build.log 2>&1 || { grep -E "error" build.log | head; exit 1; }
    cmake --install b > /dev/null )
}

echo "==> zlib"
unpack zlib "$ZLIB_SRC"
cm zlib -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF

echo "==> libffi"
unpack libffi "$LIBFFI_SRC"
( cd libffi
  emconfigure ./configure --host=wasm32-unknown-emscripten --prefix="$P" \
    --disable-shared --enable-static --disable-docs > /dev/null
  emmake make -j"$J" install > /dev/null )

echo "==> pcre2"
unpack pcre2 "$PCRE2_SRC"
cm pcre2 -DPCRE2_BUILD_PCRE2GREP=OFF -DPCRE2_BUILD_TESTS=OFF -DPCRE2_SUPPORT_JIT=OFF

echo "==> glib"
unpack glib "$GLIB_SRC"
tar -xf "$PROXY_LIBINTL" -C glib/subprojects
fix_shebangs glib
cat > cross.ini <<EOT
[binaries]
c = 'emcc'
cpp = 'em++'
ar = 'emar'
ranlib = 'emranlib'
strip = 'emstrip'
pkg-config = 'pkg-config'
[built-in options]
c_args = ['-pthread', '-O2']
cpp_args = ['-pthread', '-O2']
c_link_args = ['-pthread']
[host_machine]
system = 'emscripten'
cpu_family = 'wasm32'
cpu = 'wasm32'
endian = 'little'
[properties]
needs_exe_wrapper = true
EOT
CROSS="$PWD/cross.ini"
python3 "$PATCH_GTYPE" glib/gobject/gtype.h
( cd glib
  # No DNS in the browser: gio's resolver wants res_query().
  sed -i "s/error('Could not find res_query()')/message('no res_query (wasm)')/" gio/meson.build
  # wasm32 size_t is unsigned long, but the probe picks unsigned int.
  sed -i "s/^elif g_sizet_compatibility\['int'\]/elif false/" meson.build
  python3 - <<'PY'
p = 'glib/glib-unix.c'
s = open(p).read()
s = s.replace('#error "g_unix_fd_query_path() not supported on this platform"',
  'g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS, "g_unix_fd_query_path() not supported");\n  return NULL;')
open(p, 'w').write(s)
PY
  # Emscripten's libc declares these without defining them.
  cat >> glib/gthread.c <<'EOT'

#ifdef __EMSCRIPTEN__
#include <errno.h>
#include <sched.h>
int pthread_getname_np (pthread_t t, char *name, size_t len) { (void) t; if (len) name[0] = 0; return ERANGE; }
int pthread_getaffinity_np (pthread_t t, size_t sz, cpu_set_t *set) { (void) t; (void) sz; (void) set; return ENOSYS; }
int __sched_cpucount (size_t sz, const cpu_set_t *set) { (void) sz; (void) set; return 0; }
#endif
EOT
  cat >> glib/gspawn-posix.c <<'EOT'

#ifdef __EMSCRIPTEN__
#include <errno.h>
#include <spawn.h>
int posix_spawnp (pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[]) { (void) pid; (void) file; (void) fa; (void) at; (void) argv; (void) envp; return ENOSYS; }
#endif
EOT
  cat >> gio/gthreadedresolver.c <<'EOT'

#ifdef __EMSCRIPTEN__
int res_query (const char *dname, int cls, int type, unsigned char *answer, int len) { (void) dname; (void) cls; (void) type; (void) answer; (void) len; return -1; }
#endif
EOT
  meson setup b --cross-file "$CROSS" --prefix="$P" --default-library=static \
    -Dtests=false -Dinstalled_tests=false -Dintrospection=disabled -Dnls=disabled \
    -Dlibelf=disabled -Dlibmount=disabled -Dselinux=disabled -Dxattr=false \
    -Dsysprof=disabled -Ddocumentation=false -Dman-pages=disabled -Ddtrace=disabled \
    -Dsystemtap=disabled -Dglib_debug=disabled > meson.log 2>&1 || { tail -30 meson.log; exit 1; }
  ninja -C b -j"$J" > build.log 2>&1 || { grep -E "error|FAILED" build.log | head; exit 1; }
  ninja -C b install > /dev/null )

fix_shebangs "$P/bin"  # glib-genmarshal, glib-mkenums: used by libvips

echo "==> libjpeg-turbo"
unpack jpeg "$JPEG_SRC"
cm jpeg -DENABLE_SHARED=0 -DENABLE_STATIC=1 -DWITH_SIMD=0 -DWITH_TURBOJPEG=0 \
  -DWITH_JAVA=0 -DWITH_TESTS=0 -DREQUIRE_SIMD=0

echo "==> libpng"
unpack libpng "$LIBPNG_SRC"
cm libpng -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
  -DPNG_FRAMEWORK=OFF -DPNG_HARDWARE_OPTIMIZATIONS=OFF

echo "==> libwebp"
unpack webp "$WEBP_SRC"
cm webp -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
  -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF

echo "==> expat"
unpack expat "$EXPAT_SRC"
cm expat -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF \
  -DEXPAT_BUILD_DOCS=OFF -DEXPAT_SHARED_LIBS=OFF

echo "==> libarchive"
unpack arch "$LIBARCHIVE_SRC"
cm arch -DENABLE_TEST=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF \
  -DENABLE_UNZIP=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_OPENSSL=OFF \
  -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF \
  -DENABLE_LZO=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_BZip2=OFF \
  -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF \
  -DENABLE_PCRE2POSIX=OFF -DENABLE_CNG=OFF -DENABLE_ICONV=OFF -DENABLE_WERROR=OFF

echo "==> libvips"
unpack vips "$VIPS_SRC"
fix_shebangs vips
# G_IMPLEMENT_INTERFACE calls the init with two arguments (see patch-gtype.py).
sed -i -e 's/vips_g_input_stream_seekable_iface_init(GSeekableIface \*iface)/vips_g_input_stream_seekable_iface_init(GSeekableIface *iface, gpointer data G_GNUC_UNUSED)/' \
  vips/libvips/iofuncs/ginputsource.c
grep -q 'iface, gpointer data G_GNUC_UNUSED' vips/libvips/iofuncs/ginputsource.c
( cd vips
  meson setup b --cross-file "$CROSS" --prefix="$P" --default-library=static \
    -Dprefer_static=true -Ddeprecated=false -Dexamples=false -Dcplusplus=false \
    -Ddocs=false -Dcpp-docs=false -Dmodules=disabled -Dintrospection=disabled \
    -Dvapi=false -Djpeg=enabled -Dpng=enabled -Dwebp=enabled -Dnsgif=true \
    -Dzlib=enabled -Dspng=disabled -Dcgif=disabled -Dexif=disabled -Dfftw=disabled \
    -Dfontconfig=disabled -Darchive=disabled -Dheif=disabled -Dimagequant=disabled \
    -Duhdr=disabled -Djpeg-xl=disabled -Dlcms=disabled -Dmagick=disabled \
    -Dmatio=disabled -Dnifti=disabled -Dopenexr=disabled -Dopenjpeg=disabled \
    -Dopenslide=disabled -Dhighway=disabled -Dorc=disabled -Dpangocairo=disabled \
    -Dpdfium=disabled -Dpoppler=disabled -Dquantizr=disabled -Draw=disabled \
    -Drsvg=disabled -Dtiff=disabled -Dcfitsio=disabled > meson.log 2>&1 \
    || { tail -30 meson.log; exit 1; }
  ninja -C b -j"$J" > build.log 2>&1 || { grep -E "error|FAILED" build.log | head; exit 1; }
  ninja -C b install > /dev/null )

echo "==> SQLite"
mkdir sqlite && cd sqlite
emcc -O2 -pthread -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  -c "$SQLITE_AMALGAMATION/sqlite3.c" -o sqlite3.o
emar rcs "$P/lib/libsqlite3.a" sqlite3.o
cp "$SQLITE_AMALGAMATION/sqlite3.h" "$P/include/"
cat > "$P/lib/pkgconfig/sqlite3.pc" <<EOT
prefix=$P
Name: SQLite
Description: SQLite
Version: $SQLITE_VERSION
Libs: -L$P/lib -lsqlite3
Cflags: -I$P/include
EOT
cd ..

echo "==> libunarr"
unpack unarr "$UNARR_SRC"
cm unarr -DENABLE_7Z=ON -DUSE_SYSTEM_BZ2=OFF -DUSE_SYSTEM_LZMA=OFF -DUSE_SYSTEM_ZLIB=OFF

echo "==> DjVuLibre"
unpack djvulibre "$DJVULIBRE_SRC"
( cd djvulibre
  emconfigure ./configure --host=wasm32-unknown-emscripten --prefix="$P" \
    --disable-shared --enable-static --disable-desktopfiles --disable-xmltools \
    --without-jpeg --disable-i18n CXXFLAGS="-O2 -pthread" > /dev/null
  emmake make -C libdjvu -j"$J" libdjvulibre.la > /dev/null
  mkdir -p "$P/include/libdjvu"
  cp libdjvu/.libs/libdjvulibre.a "$P/lib/"
  cp libdjvu/ddjvuapi.h libdjvu/miniexp.h "$P/include/libdjvu/" )
cat > "$P/lib/pkgconfig/ddjvuapi.pc" <<EOT
prefix=$P
Name: DjVuLibre
Description: DjVu decoder
Version: $DJVULIBRE_VERSION
Libs: -L$P/lib -ldjvulibre
Cflags: -I$P/include
EOT

for f in libvips.a libglib-2.0.a libarchive.a libunarr.a libdjvulibre.a; do
  test -f "$P/lib/$f" || { echo "missing $P/lib/$f"; ls "$P/lib"; exit 1; }
done
echo "==> done: $P"
