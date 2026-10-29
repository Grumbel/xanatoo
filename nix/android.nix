# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# The Android build, after Kurvenrausch / Pingus's ndk-build pipeline
# (mk/android/): SDL2's native library and Java side are built once from its
# release tarball; xanatoo (plus a static FreeType built from nixpkgs'
# source) into libmain.so against it; both packaged into a signed APK.
#
# Network clients (Gemini / Nostr / Reddit / Lemmy) use Mbed TLS; SQLite is
# built from its amalgamation; images go through thumtoo over prebuilt
# libvips / glib / libarchive (nix/android-imagelibs.nix); no FontConfig (fonts come from the bundled
# DejaVu and /system/fonts). Data files ship as APK assets (see
# mk/android/scripts/build-apk.sh).
#
#   nix build .#xanatoo-android        the APK
#   nix run .#install-android-xanatoo  install it with adb

{ pkgs
, nixpkgs           # for the cross-built image libraries
, sdlSrc
, sdlVersion
, androidSdk
, buildToolsVersion
, packagePlatform   # minSdk, linked against
, compilePlatform   # the Java side's classpath
, targetAbis
, version
, versionCode
}:

let
  abis = pkgs.lib.concatStringsSep " " targetAbis;

  # ndk-build entry point: every subdirectory's Android.mk (SDL, FreeType,
  # the app).
  topAndroidMk = pkgs.writeText "Android.mk" "include $(call all-subdir-makefiles)\n";

  applicationMk = pkgs.writeText "Application.mk" ''
    APP_STL := c++_shared
    APP_ABI := ${abis}
    APP_PLATFORM := android-${packagePlatform}
  '';

  sdlAndroidLibs = pkgs.stdenvNoCC.mkDerivation {
    pname = "sdl2-android-libs";
    version = sdlVersion;
    dontUnpack = true;
    nativeBuildInputs = [ androidSdk pkgs.jdk17 pkgs.gnumake ];
    env = {
      BUILD_TOOLS_VERSION = buildToolsVersion;
      COMPILE_PLATFORM = compilePlatform;
      PACKAGE_PLATFORM = packagePlatform;
      SDL_SRC = "${sdlSrc}";
      APPLICATION_MK = applicationMk;
      TOP_ANDROID_MK = topAndroidMk;
    };
    buildPhase = ''
      runHook preBuild
      export ANDROID_HOME=${androidSdk}/libexec/android-sdk
      bash ${../mk/android/scripts}/build-sdl-libs.sh
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      TARGET_ABIS=${pkgs.lib.escapeShellArg abis} bash ${../mk/android/scripts}/install-sdl-libs.sh
      runHook postInstall
    '';
    # Android ELF: the host's patchelf and strip must keep off.
    dontPatchELF = true;
    dontStrip = true;
  };

  # sqlite3.c + sqlite3.h from nixpkgs' full source tree (needs tclsh).
  sqliteAmalgamation = pkgs.stdenv.mkDerivation {
    pname = "sqlite-amalgamation";
    inherit (pkgs.sqlite) version src;
    nativeBuildInputs = [ pkgs.unzip pkgs.tcl ];
    configureFlags = [ "--disable-tcl" ];
    buildPhase = "make sqlite3.c";
    installPhase = ''
      mkdir -p $out
      cp sqlite3.c sqlite3.h $out/
    '';
  };

  # Prebuilt C libraries for thumtoo, one directory per ABI.
  imageLibs = pkgs.linkFarm "xanatoo-android-imagelibs" (map (abi: {
    name = abi;
    path = import ./android-imagelibs.nix { inherit nixpkgs abi; system = pkgs.stdenv.buildPlatform.system; };
  }) targetAbis);

  sdlPrebuiltMk = pkgs.writeText "SDL2-prebuilt-Android.mk" ''
    LOCAL_PATH := $(call my-dir)
    include $(CLEAR_VARS)
    LOCAL_MODULE := SDL2
    LOCAL_SRC_FILES := ${sdlAndroidLibs}/lib/$(TARGET_ARCH_ABI)/libSDL2.so
    include $(PREBUILT_SHARED_LIBRARY)
  '';

  # The sources the APK is built from: no build directories, no .git.
  appSrc = pkgs.lib.fileset.toSource {
    root = ../.;
    fileset = pkgs.lib.fileset.unions [
      ../src ../include ../external
      ../examples ../styles ../icons/svg ../scripts/rasterize_icons.py
      ../FORMAT.md
    ];
  };

  apkName = "xanatoo-${version}.apk";

  apk = pkgs.stdenvNoCC.mkDerivation {
    pname = "xanatoo-android";
    inherit version;
    dontUnpack = true;
    nativeBuildInputs = [
      androidSdk pkgs.jdk17 pkgs.zip pkgs.gnumake pkgs.xz
      # Toolbar icons: rasterize_icons.py uses rsvg-convert (else its
      # built-in pixel fallback).
      pkgs.python3 pkgs.librsvg
    ];
    env = {
      BUILD_TOOLS_VERSION = buildToolsVersion;
      PACKAGE_PLATFORM = packagePlatform;
      TARGET_ABIS = abis;
      APP_NAME = "xanatoo";
      APP_DIR = "${../mk/android/app}";
      APP_SRC_DIR = "${appSrc}";
      FREETYPE_SRC = "${pkgs.freetype.src}";
      DEJAVU_DIR = "${pkgs.dejavu_fonts}/share/fonts/truetype";
      FREETYPE_MK = "${../mk/android/freetype/Android.mk}";
      SQLITE_SRC = sqliteAmalgamation;
      SQLITE_MK = "${../mk/android/sqlite/Android.mk}";
      MBEDTLS_SRC = "${pkgs.mbedtls.src}";
      MBEDTLS_MK = "${../mk/android/mbedtls/Android.mk}";
      THUMTOO_MK = "${../mk/android/thumtoo/Android.mk}";
      IMAGELIBS = imageLibs;
      APPLICATION_MK = applicationMk;
      TOP_ANDROID_MK = topAndroidMk;
      SDL_PREBUILT_MK = sdlPrebuiltMk;
      SDL_ANDROID_LIBS = sdlAndroidLibs;
      KEYSTORE = "${../mk/android/keystore/debug.keystore}";
      APP_VERSION = version;
      VERSION_CODE = toString versionCode;
    };
    buildPhase = ''
      runHook preBuild
      export ANDROID_HOME=${androidSdk}/libexec/android-sdk
      bash ${../mk/android/scripts}/build-apk.sh
      runHook postBuild
    '';
    installPhase = ''
      mkdir -p $out
      cp out/xanatoo.apk $out/${apkName}
    '';
    dontPatchELF = true;
    dontStrip = true;
  };

  installApp = {
    type = "app";
    program = toString (pkgs.writeShellScript "install-xanatoo-apk" ''
      exec ${pkgs.android-tools}/bin/adb install -r ${apk}/${apkName}
    '');
    meta.description = "Install the xanatoo APK on a connected Android device with adb";
  };

in {
  inherit sdlAndroidLibs sqliteAmalgamation imageLibs apk installApp;
}
