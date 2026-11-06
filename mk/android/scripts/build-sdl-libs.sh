#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds SDL3's native library with ndk-build for every ABI, and its Java
# side (SDLActivity) into classes.dex, following the Kurvenrausch pipeline.
#
# Env: ANDROID_HOME, BUILD_TOOLS_VERSION, COMPILE_PLATFORM, PACKAGE_PLATFORM,
#      SDL_SRC (SDL3 source tree), APPLICATION_MK, TOP_ANDROID_MK
set -euo pipefail

# shellcheck source=ndk.sh
. "$(dirname "$0")/ndk.sh"
NDK="$(resolve_ndk)"
echo "==> NDK=$NDK"
BT="$ANDROID_HOME/build-tools/$BUILD_TOOLS_VERSION"
COMPILE_JAR="$ANDROID_HOME/platforms/android-$COMPILE_PLATFORM/android.jar"

mkdir -p sdl-jni
cp -r "$SDL_SRC" sdl-jni/SDL
chmod -R u+rwX sdl-jni/SDL
cp "$APPLICATION_MK" sdl-jni/Application.mk
cp "$TOP_ANDROID_MK" sdl-jni/Android.mk

"$NDK/ndk-build" \
  NDK_PROJECT_PATH="$PWD/sdl-jni" \
  APP_BUILD_SCRIPT="$PWD/sdl-jni/Android.mk" \
  NDK_APPLICATION_MK="$PWD/sdl-jni/Application.mk" \
  -j"${NIX_BUILD_CORES:-$(nproc)}"

mkdir -p javasrc classes
cp -r sdl-jni/SDL/android-project/app/src/main/java/org javasrc/org
javac -encoding UTF-8 --release 8 -classpath "$COMPILE_JAR" -d classes $(find javasrc -name '*.java')
"$BT/d8" --output classes --min-api "$PACKAGE_PLATFORM" $(find classes -name '*.class')
