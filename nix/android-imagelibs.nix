# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# The C libraries thumtoo needs on Android (libvips, libarchive, glib and
# their codecs), cross-built with nixpkgs' Android cross set (NDK 27 via
# `useAndroidPrebuilt`, API 21) as static libraries and merged into one
# `lib/libimagedeps.a` + `include/` per ABI. All of it is plain C (libvips
# without its C++ API), so it links into the NDK 29 / c++_shared libmain.so
# that ndk-build makes; thumtoo's own C++ is an ndk-build module
# (mk/android/thumtoo/Android.mk).
#
# nixpkgs' own glib / libarchive / libwebp derivations do not cross-build
# for bionic (attr, elfutils, bash, tools), hence the minimal ones here.

{ nixpkgs, system, abi }:

let
  target = {
    "armeabi-v7a" = "armv7a-unknown-linux-androideabi";
    "arm64-v8a" = "aarch64-unknown-linux-android";
    "x86_64" = "x86_64-unknown-linux-android";
  }.${abi};

  pkgs = import nixpkgs {
    inherit system;
    config.allowUnfree = true;
    config.android_sdk.accept_license = true;
    crossSystem = {
      config = target;
      androidSdkVersion = "21";
      androidNdkVersion = "27";
      useAndroidPrebuilt = true;
    };
    # nixpkgs' cc-wrapper asks every x86 Linux clang ≥ 19 for TLS
    # descriptors (-mtls-dialect=gnu2), which clang refuses for Android.
    overlays = [ (final: prev:
      if prev.stdenv.hostPlatform.isAndroid && prev.stdenv.hostPlatform.isx86 then {
        stdenv = prev.overrideCC prev.stdenv (prev.stdenv.cc.overrideAttrs (o: {
          postFixup = (o.postFixup or "") + ''
            sed -i 's/-mtls-dialect=gnu2//' $out/nix-support/add-local-cc-cflags-before.sh
          '';
        }));
      } else { }) ];
  };
  inherit (pkgs) lib;

  # Not pkgsStatic (that is musl): bionic, static libraries only.
  sstd = pkgs.stdenvAdapters.makeStaticLibraries pkgs.stdenv;
  st = p: p.override { stdenv = sstd; };

  zlib = st pkgs.zlib;
  libffi = st pkgs.libffi;
  pcre2 = st pkgs.pcre2;
  # bionic has no iconv before API 28.
  libiconv = st pkgs.libiconvReal;
  expat = st pkgs.expat;
  # armv7 NEON probe uses getauxval(AT_HWCAP) & HWCAP_ARM_NEON; bionic's
  # headers lack the constant (kernel value 1 << 12).
  # ENABLE_SHARED wins over BUILD_SHARED_LIBS=OFF: drop the .so explicitly.
  libjpeg = (st pkgs.libjpeg_turbo).overrideAttrs (o: {
    cmakeFlags = (o.cmakeFlags or [ ]) ++ [ "-DENABLE_SHARED=0" "-DENABLE_STATIC=1" ];
  } // lib.optionalAttrs pkgs.stdenv.hostPlatform.isAarch32 {
    env = (o.env or { }) // { NIX_CFLAGS_COMPILE = "-DHWCAP_ARM_NEON=4096"; };
  });
  libpng = st pkgs.libpng;

  # Library only: nixpkgs' build adds the tools (libtiff, OpenEXR, …).
  libwebp = sstd.mkDerivation {
    pname = "libwebp-android";
    inherit (pkgs.libwebp) version src;
    nativeBuildInputs = [ pkgs.buildPackages.cmake ];
    cmakeFlags = [
      "-DBUILD_SHARED_LIBS=OFF" "-DWEBP_BUILD_ANIM_UTILS=OFF"
      "-DWEBP_BUILD_CWEBP=OFF" "-DWEBP_BUILD_DWEBP=OFF" "-DWEBP_BUILD_GIF2WEBP=OFF"
      "-DWEBP_BUILD_IMG2WEBP=OFF" "-DWEBP_BUILD_VWEBP=OFF" "-DWEBP_BUILD_WEBPINFO=OFF"
      "-DWEBP_BUILD_WEBPMUX=OFF" "-DWEBP_BUILD_EXTRAS=OFF"
    ];
  };

  # Minimal static glib (+ gobject / gmodule / gio): no NLS, ELF, mount,
  # xattr or SELinux support.
  glib = sstd.mkDerivation {
    pname = "glib-android";
    inherit (pkgs.glib) version src;
    depsBuildBuild = [ pkgs.buildPackages.stdenv.cc ];
    nativeBuildInputs = with pkgs.buildPackages; [ meson ninja pkg-config python3 ];
    buildInputs = [ zlib libffi pcre2 libiconv ];
    # libintl stub glib's wrap would download (gettext() → identity).
    proxyLibintl = pkgs.fetchurl {
      url = "https://github.com/frida/proxy-libintl/archive/refs/tags/0.5.tar.gz";
      sha256 = "f7a1cbd7579baaf575c66f9d99fb6295e9b0684a28b095967cfda17857595303";
    };
    postUnpack = "tar -xf $proxyLibintl -C $sourceRoot/subprojects";
    postPatch = "patchShebangs --build tools gio/gdbus-2.0/codegen gobject glib";
    # The .pc files name build tools under $out/bin; a cross build does not
    # install them, so point them at the build machine's.
    postInstall = ''
      for t in glib-mkenums glib-genmarshal glib-compile-resources gdbus-codegen; do
        ln -sf ${pkgs.buildPackages.glib.dev}/bin/$t $out/bin/$t
      done
    '';
    mesonFlags = [
      "-Ddefault_library=static"
      "-Dtests=false" "-Dinstalled_tests=false"
      "-Dintrospection=disabled" "-Dnls=disabled" "-Dlibelf=disabled"
      "-Dlibmount=disabled" "-Dselinux=disabled" "-Dxattr=false"
      "-Dsysprof=disabled" "-Ddocumentation=false" "-Dman-pages=disabled"
      "-Ddtrace=disabled" "-Dsystemtap=disabled" "-Dglib_debug=disabled"
    ];
  };

  # libarchive: zip / tar / 7z (zlib only); no ACL, xattr, crypto or tools.
  libarchive = sstd.mkDerivation {
    pname = "libarchive-android";
    inherit (pkgs.libarchive) version src;
    nativeBuildInputs = [ pkgs.buildPackages.cmake ];
    buildInputs = [ zlib libiconv ];
    cmakeFlags = [
      "-DBUILD_SHARED_LIBS=OFF" "-DBUILD_TESTING=OFF"
      "-DENABLE_TEST=OFF" "-DENABLE_TAR=OFF" "-DENABLE_CPIO=OFF" "-DENABLE_CAT=OFF"
      "-DENABLE_UNZIP=OFF" "-DENABLE_ACL=OFF" "-DENABLE_XATTR=OFF"
      "-DENABLE_OPENSSL=OFF" "-DENABLE_MBEDTLS=OFF" "-DENABLE_NETTLE=OFF"
      "-DENABLE_LIBB2=OFF" "-DENABLE_LZ4=OFF" "-DENABLE_LZO=OFF" "-DENABLE_LZMA=OFF"
      "-DENABLE_ZSTD=OFF" "-DENABLE_BZip2=OFF" "-DENABLE_LIBXML2=OFF"
      "-DENABLE_EXPAT=OFF" "-DENABLE_PCREPOSIX=OFF" "-DENABLE_PCRE2POSIX=OFF"
      "-DENABLE_CNG=OFF" "-DENABLE_ICONV=ON" "-DENABLE_WERROR=OFF"
    ];
    # android_lf.h (large-file shims) is only in contrib/.
    preConfigure = "export NIX_CFLAGS_COMPILE=\"$NIX_CFLAGS_COMPILE -I$PWD/contrib/android/include\"";
  };

  # libvips: JPEG / PNG / WebP / GIF (built-in nsgif), nothing else.
  vips = sstd.mkDerivation {
    pname = "vips-android";
    inherit (pkgs.vips) version src;
    depsBuildBuild = [ pkgs.buildPackages.stdenv.cc ];
    nativeBuildInputs = with pkgs.buildPackages; [ meson ninja pkg-config python3 glib ];
    buildInputs = [ glib expat libjpeg libpng libwebp zlib libffi pcre2 libiconv ];
    postPatch = "patchShebangs --build .";
    mesonFlags = [
      "-Ddefault_library=static" "-Dprefer_static=true" "-Ddeprecated=false" "-Dexamples=false"
      "-Dcplusplus=false" "-Ddocs=false" "-Dcpp-docs=false" "-Dmodules=disabled"
      "-Dintrospection=disabled" "-Dvapi=false"
      "-Djpeg=enabled" "-Dpng=enabled" "-Dwebp=enabled" "-Dnsgif=true" "-Dzlib=enabled"
      "-Dspng=disabled" "-Dcgif=disabled" "-Dexif=disabled" "-Dfftw=disabled"
      "-Dfontconfig=disabled" "-Darchive=disabled" "-Dheif=disabled"
      "-Dimagequant=disabled" "-Duhdr=disabled" "-Djpeg-xl=disabled" "-Dlcms=disabled"
      "-Dmagick=disabled" "-Dmatio=disabled" "-Dnifti=disabled" "-Dopenexr=disabled"
      "-Dopenjpeg=disabled" "-Dopenslide=disabled" "-Dhighway=disabled"
      "-Dorc=disabled" "-Dpangocairo=disabled" "-Dpdfium=disabled" "-Dpoppler=disabled"
      "-Dquantizr=disabled" "-Draw=disabled" "-Drsvg=disabled" "-Dtiff=disabled"
      "-Dcfitsio=disabled"
    ];
  };

  libDirs = map lib.getLib [ vips libarchive glib expat libjpeg libpng libwebp zlib.static libffi pcre2 libiconv ];
  incDirs = map lib.getDev [ vips libarchive glib ];

in pkgs.buildPackages.stdenvNoCC.mkDerivation {
  pname = "xanatoo-android-imagelibs-${abi}";
  version = vips.version;
  dontUnpack = true;
  # One archive, so ndk-build needs one prebuilt module and no link order.
  # glibconfig.h is per ABI (lib/glib-2.0/include).
  installPhase = ''
    mkdir -p $out/lib $out/include
    {
      echo "create $out/lib/libimagedeps.a"
      for d in ${lib.concatStringsSep " " libDirs}; do
        for a in $d/lib/*.a; do
          case "$(basename $a)" in
            libgirepository-2.0.a|libgthread-2.0.a|libwebpdecoder.a|libpng.a) ;;
            *) echo "addlib $a" ;;
          esac
        done
      done
      echo save
      echo end
    } | ${pkgs.buildPackages.llvmPackages.llvm}/bin/llvm-ar -M
    for d in ${lib.concatStringsSep " " incDirs}; do
      cp -rn $d/include/. $out/include/
    done
    chmod -R u+w $out/include
    cp ${lib.getLib glib}/lib/glib-2.0/include/glibconfig.h $out/include/glib-2.0/
  '';
  passthru = { inherit vips glib libarchive; };
}
