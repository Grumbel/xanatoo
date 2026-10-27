#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the APK: xanatoo's sources as libmain.so with ndk-build against the
# prebuilt SDL2 and a static FreeType, packaged with SDL's Java side,
# aligned and signed (after Kurvenrausch / Pingus). Data files (examples,
# FORMAT.md, style sheets, toolbar icons) go to assets/xanatoo/ with a
# MANIFEST that platform.cpp unpacks into internal storage.
#
# Env: ANDROID_HOME, BUILD_TOOLS_VERSION, PACKAGE_PLATFORM, TARGET_ABIS,
#      APP_NAME, APP_DIR (manifest, res/, jni/Android.mk), APP_SRC_DIR (the
#      repository: src/, include/, external/, examples/, styles/, icons/svg,
#      scripts/rasterize_icons.py, FORMAT.md), DEJAVU_DIR (TTFs bundled as
#      fonts/: same faces as desktop, and glyphs Roboto lacks — ▸ ▾ ☐ ☑),
#      FREETYPE_SRC (tarball), SQLITE_SRC (dir with sqlite3.c/.h), SQLITE_MK,
#      MBEDTLS_SRC (source tree), MBEDTLS_MK,
#      FREETYPE_MK, APPLICATION_MK, TOP_ANDROID_MK, SDL_PREBUILT_MK,
#      SDL_ANDROID_LIBS, KEYSTORE, APP_VERSION, VERSION_CODE
set -euo pipefail

# shellcheck source=ndk.sh
. "$(dirname "$0")/ndk.sh"
NDK="$(resolve_ndk)"
echo "==> NDK=$NDK"
BT="$ANDROID_HOME/build-tools/$BUILD_TOOLS_VERSION"
PACKAGE_JAR="$ANDROID_HOME/platforms/android-$PACKAGE_PLATFORM/android.jar"

mkdir -p src/jni/src src/jni/SDL/include src/jni/freetype
cp "$APPLICATION_MK" src/jni/Application.mk
cp "$TOP_ANDROID_MK" src/jni/Android.mk
cp "$APP_DIR/jni/Android.mk" src/jni/src/Android.mk
cp -r "$APP_DIR/res" src/res
sed -e "s/@VERSION_NAME@/$APP_VERSION/" -e "s/@VERSION_CODE@/$VERSION_CODE/" \
  -e "s/@MIN_SDK@/$PACKAGE_PLATFORM/" \
  "$APP_DIR/AndroidManifest.xml" > src/AndroidManifest.xml

# xanatoo: sources (TLS through Mbed TLS, not OpenSSL), headers, vendored
# ImGui/stb.
mkdir -p src/jni/src/src
for f in "$APP_SRC_DIR"/src/*.cpp; do
  case "$(basename "$f")" in
    tls_openssl.cpp) ;;
    *) cp "$f" src/jni/src/src/ ;;
  esac
done
cp -r "$APP_SRC_DIR"/include src/jni/src/include
cp -r "$APP_SRC_DIR"/external src/jni/src/external

# FreeType, built from source as a static module.
tar xf "$FREETYPE_SRC" -C src/jni/freetype --strip-components=1
cp "$FREETYPE_MK" src/jni/freetype/Android.mk

# Mbed TLS as a static module (TLS for HTTPS / Gemini / Nostr).
mkdir -p src/jni/mbedtls
cp -r "$MBEDTLS_SRC"/include "$MBEDTLS_SRC"/library src/jni/mbedtls/
cp "$MBEDTLS_MK" src/jni/mbedtls/Android.mk

# SQLite amalgamation as a static module (side store).
mkdir -p src/jni/sqlite
cp "$SQLITE_SRC"/sqlite3.c "$SQLITE_SRC"/sqlite3.h src/jni/sqlite/
cp "$SQLITE_MK" src/jni/sqlite/Android.mk

# SDL2, prebuilt; xanatoo includes <SDL.h> from include/SDL2.
cp "$SDL_PREBUILT_MK" src/jni/SDL/Android.mk
cp -r "$SDL_ANDROID_LIBS/include" src/jni/SDL/include/SDL2
chmod -R u+rwX src

# Data files → assets/xanatoo/ + MANIFEST (stamp line, then one path per
# line; SDL can read assets but not list them).
A=assets/xanatoo
mkdir -p "$A/examples" "$A/styles" "$A/icons" "$A/fonts"
for f in DejaVuSans DejaVuSans-Bold DejaVuSans-Oblique DejaVuSans-BoldOblique \
         DejaVuSerif DejaVuSerif-Bold DejaVuSerif-Italic DejaVuSerif-BoldItalic \
         DejaVuSansMono DejaVuSansMono-Bold DejaVuSansMono-Oblique \
         DejaVuSansMono-BoldOblique; do
  cp "$DEJAVU_DIR/$f.ttf" "$A/fonts/"
done
cp "$APP_SRC_DIR"/examples/* "$A/examples/"
cp "$APP_SRC_DIR"/styles/*.xss "$A/styles/"
cp "$APP_SRC_DIR"/FORMAT.md "$A/"
python3 "$APP_SRC_DIR"/scripts/rasterize_icons.py "$APP_SRC_DIR"/icons/svg "$A/icons" 20
chmod -R u+rwX assets
files="$(cd "$A" && find . -type f ! -name MANIFEST | sed 's|^\./||' | LC_ALL=C sort)"
hash="$(cd "$A" && printf '%s\n' "$files" | xargs sha256sum | sha256sum | cut -c1-16)"
{ echo "xanatoo $APP_VERSION ($VERSION_CODE) $hash"; printf '%s\n' "$files"; } > "$A/MANIFEST"
echo "==> $(printf '%s\n' "$files" | wc -l) data files in assets/xanatoo"

"$NDK/ndk-build" \
  NDK_PROJECT_PATH="$PWD/src" \
  APP_BUILD_SCRIPT="$PWD/src/jni/Android.mk" \
  NDK_APPLICATION_MK="$PWD/src/jni/Application.mk" \
  -j"${NIX_BUILD_CORES:-$(nproc)}"

mkdir -p out
"$BT/aapt" package -f -M src/AndroidManifest.xml -S src/res -A assets \
  -I "$PACKAGE_JAR" -F out/base.apk
cp "$SDL_ANDROID_LIBS/dex/classes.dex" out/classes.dex
for abi in $TARGET_ABIS; do
  mkdir -p "out/lib/$abi"
  cp src/libs/"$abi"/*.so "out/lib/$abi/"
done
( cd out && "$BT/aapt" add base.apk classes.dex && zip -r base.apk lib )

"$BT/zipalign" -f 4 out/base.apk out/aligned.apk
cp "$KEYSTORE" debug.keystore
"$BT/apksigner" sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --out "out/$APP_NAME.apk" out/aligned.apk
"$BT/aapt" dump badging "out/$APP_NAME.apk" | head -5
