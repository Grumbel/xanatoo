#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the APK: xanatoo's sources as libmain.so with ndk-build against the
# prebuilt SDL2 and a static FreeType, packaged with SDL's Java side,
# aligned and signed (after Kurvenrausch / Pingus; no data files yet).
#
# Env: ANDROID_HOME, BUILD_TOOLS_VERSION, PACKAGE_PLATFORM, TARGET_ABIS,
#      APP_NAME, APP_DIR (manifest, res/, jni/Android.mk), APP_SRC_DIR (the
#      repository: src/, include/, external/), FREETYPE_SRC (tarball),
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

# xanatoo: sources (minus the OpenSSL helpers), headers, vendored ImGui/stb.
mkdir -p src/jni/src/src
for f in "$APP_SRC_DIR"/src/*.cpp; do
  case "$(basename "$f")" in
    https.cpp) ;;
    *) cp "$f" src/jni/src/src/ ;;
  esac
done
cp -r "$APP_SRC_DIR"/include src/jni/src/include
cp -r "$APP_SRC_DIR"/external src/jni/src/external

# FreeType, built from source as a static module.
tar xf "$FREETYPE_SRC" -C src/jni/freetype --strip-components=1
cp "$FREETYPE_MK" src/jni/freetype/Android.mk

# SDL2, prebuilt; xanatoo includes <SDL.h> from include/SDL2.
cp "$SDL_PREBUILT_MK" src/jni/SDL/Android.mk
cp -r "$SDL_ANDROID_LIBS/include" src/jni/SDL/include/SDL2
chmod -R u+rwX src

"$NDK/ndk-build" \
  NDK_PROJECT_PATH="$PWD/src" \
  APP_BUILD_SCRIPT="$PWD/src/jni/Android.mk" \
  NDK_APPLICATION_MK="$PWD/src/jni/Application.mk" \
  -j"${NIX_BUILD_CORES:-$(nproc)}"

mkdir -p out
"$BT/aapt" package -f -M src/AndroidManifest.xml -S src/res -I "$PACKAGE_JAR" -F out/base.apk
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
