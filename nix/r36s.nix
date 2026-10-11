# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# xanatoo on the R36S handheld (RK3326, 640x480) running ArkOS, whose userland is
# Ubuntu 19.10 (eoan): glibc 2.30, libstdc++ from GCC 9, SDL2 2.0.x. A
# binary built against nixpkgs' glibc does not run there, so this builds
# against a sysroot made of eoan's arm64 packages, from the Ubuntu archive
# with their published hashes (Pingus fetches a prebuilt sysroot instead).
# The compiler is nixpkgs' aarch64 cross GCC, but every header and library
# comes from the sysroot: libstdc++ 9's headers with its shared library (so
# no ABI shims are needed), the shared libgcc_s; GCC's own libgcc.a supplies
# only compiler builtins. On the device, SDL2 is ArkOS's (KMS/DRM, its own
# build), the stable SDL2 ABI.
#
#   nix build .#xanatoo-r36s                aarch64 binary (ArkOS sysroot)
#   nix build .#xanatoo-r36s-portmaster     PortMaster folder → /roms/ports/
#   nix build .#xanatoo-r36s-portmaster-zip zip for PortMaster autoinstall

{ pkgs
, version
, date
}:

let
  lib = pkgs.lib;

  # eoan arm64 packages: glibc, the kernel headers, GCC 9's runtime and
  # libstdc++ headers, SDL2 2.0.10 (headers and library to link against).
  debs = [
    { file = "pool/main/g/glibc/libc6_2.30-0ubuntu2_arm64.deb"; sha256 = "4853da1e3c9eae109708b30dc5df00b7ced888c09f0e77bf6731015a97fb859a"; }
    { file = "pool/main/g/glibc/libc6-dev_2.30-0ubuntu2_arm64.deb"; sha256 = "a0b1860c16c50934a5fe3206ac386267d24b015b0ab677005d38e60ac1d27823"; }
    { file = "pool/main/l/linux/linux-libc-dev_5.3.0-18.19_arm64.deb"; sha256 = "7575a37b1abf722f950fa8202c49f24b9c887dcf8e1f97bad15b555cb8c483de"; }
    { file = "pool/main/g/gcc-9/libgcc1_9.2.1-9ubuntu2_arm64.deb"; sha256 = "81dfdee2c50f13dab98967b0062e51f1cebb967f407e3593c5a77ed2146ea008"; }
    { file = "pool/main/g/gcc-9/libstdc++6_9.2.1-9ubuntu2_arm64.deb"; sha256 = "5af692b4ecf237979411acf74b7319785e09dd3450a917a94f3ea65cd260436e"; }
    { file = "pool/main/g/gcc-9/libstdc++-9-dev_9.2.1-9ubuntu2_arm64.deb"; sha256 = "aa0efe55ba5002e2d3572271b6c0106dfb59f7a6c61be0a0c406c4836e12472b"; }
    { file = "pool/main/g/gcc-9/libgcc-9-dev_9.2.1-9ubuntu2_arm64.deb"; sha256 = "4e4a83c4d83471eff726fd819b4e5d266fd01c0fbb66f1376718b78bfd4ae116"; }
    { file = "pool/universe/libs/libsdl2/libsdl2-2.0-0_2.0.10+dfsg1-1ubuntu1_arm64.deb"; sha256 = "34f06694f13bbee317b89be868768e8fd5b030287431a630881f45ff99dc9264"; }
    { file = "pool/universe/libs/libsdl2/libsdl2-dev_2.0.10+dfsg1-1ubuntu1_arm64.deb"; sha256 = "9eb97797c392b68449045d814f8763ca984853abefff94ababdc72270b7d65ea"; }
    # FreeType / Fontconfig (xanatoo text rendering) and link deps
    { file = "pool/main/f/freetype/libfreetype6_2.9.1-4_arm64.deb"; sha256 = "e1a18bdec319ab92e02912b6721846907ea13e420123c7225834180ba94360f1"; }
    { file = "pool/main/f/freetype/libfreetype6-dev_2.9.1-4_arm64.deb"; sha256 = "8f4b3f018e6ef26aac15cf378856dde03b8d2a7fd69e3c41c9b8b0467f483c74"; }
    { file = "pool/main/f/fontconfig/libfontconfig1_2.13.1-2ubuntu3_arm64.deb"; sha256 = "7abc3c68569ebd52e7e27ba1cd1e97004bf5a1641212acd821220a020b713c2f"; }
    { file = "pool/main/f/fontconfig/libfontconfig1-dev_2.13.1-2ubuntu3_arm64.deb"; sha256 = "a4c7b981a7003ab75932cfabedd6697474c2ec541517ea7850be9637f3b753c3"; }
    { file = "pool/main/e/expat/libexpat1_2.2.7-2_arm64.deb"; sha256 = "07b81f7223e65c52937fce24edc9d6dae79fa2fb66408bc68103362b555d6f3a"; }
    { file = "pool/main/e/expat/libexpat1-dev_2.2.7-2_arm64.deb"; sha256 = "5201f6619b3d6332e04cd5b48913ffbf2ea61a8b67d5e3bbce194f9a262ae75b"; }
    { file = "pool/main/z/zlib/zlib1g_1.2.11.dfsg-1ubuntu3_arm64.deb"; sha256 = "409aa5163b49fee4ece16744cc245c281d0e97c1114a6a199a5a9d48f93b6bf0"; }
    { file = "pool/main/z/zlib/zlib1g-dev_1.2.11.dfsg-1ubuntu3_arm64.deb"; sha256 = "b719c443866abb74bbe31010de9a92a5f889f2c7b3b13b95e8637ab79e9bd919"; }
    { file = "pool/main/libp/libpng1.6/libpng16-16_1.6.37-1_arm64.deb"; sha256 = "6dcde9dbcaa1c59fdf086989b44613bed1b97a8b160f53767d984e344d43d167"; }
    { file = "pool/main/libp/libpng1.6/libpng-dev_1.6.37-1_arm64.deb"; sha256 = "2f03c1b26d2835d9bd81e2fc38a9095e79e3e303bdedf52c8743c74de789c472"; }
    { file = "pool/main/u/util-linux/libuuid1_2.34-0.1ubuntu2_arm64.deb"; sha256 = "219dcf7b2bea4dd43c1685c651f61f63d903ac82cad0ee6a821b942283359b65"; }
    { file = "pool/main/u/util-linux/uuid-dev_2.34-0.1ubuntu2_arm64.deb"; sha256 = "d30984212486096a8f499354dfeae0a45d7754ccee6c4e1183de1997fc44bcd8"; }
    { file = "pool/main/b/brotli/libbrotli1_1.0.7-2_arm64.deb"; sha256 = "f180a18ae74016c5db89a1ed438e2fa45cd7d64c2abcec6a32e4316724b1b85a"; }
    { file = "pool/main/b/brotli/libbrotli-dev_1.0.7-2_arm64.deb"; sha256 = "861312067f81e664925243294070dcb31e4478c21f9a237a95d9cec7ebde7155"; }
  ];
  fetchDeb = d: pkgs.fetchurl {
    url = "http://old-releases.ubuntu.com/ubuntu/${d.file}";
    inherit (d) sha256;
  };

  sysroot = pkgs.stdenvNoCC.mkDerivation {
    pname = "arkos-sysroot";
    version = "eoan-arm64";
    dontUnpack = true;
    nativeBuildInputs = [ pkgs.dpkg ];
    installPhase = ''
      mkdir -p $out
      ${lib.concatMapStrings (d: "dpkg-deb -x ${fetchDeb d} $out\n") debs}
      # Absolute symlinks point into the device's root: into the sysroot.
      find $out -type l | while read -r link; do
        target=$(readlink "$link")
        case "$target" in
          /*) ln -sfn "$out$target" "$link" ;;
        esac
      done
      # GCC 16's drivers link -lgcc_s_asneeded, a script of its own: the
      # same here, for eoan's libgcc_s.
      echo 'INPUT ( AS_NEEDED ( -lgcc_s ) )' > $out/usr/lib/gcc/${multiarch}/9/libgcc_s_asneeded.so
    '';
    # aarch64 files: the host's fixups must keep off.
    dontFixup = true;
  };

  cross = pkgs.pkgsCross.aarch64-multiplatform.stdenv.cc;
  gcc = cross.cc;
  binutils = cross.bintools.bintools;
  tp = lib.removeSuffix "-" cross.targetPrefix; # aarch64-unknown-linux-gnu
  multiarch = "aarch64-linux-gnu";
  gccLib = "${gcc}/lib/gcc/${tp}/${gcc.version}";

  # Compiler drivers that see nothing but the sysroot (and GCC's own
  # freestanding headers and builtins).
  cflags = lib.concatStringsSep " " [
    "-nostdinc"
    "--sysroot=${sysroot}"
    "-B${binutils}/${tp}/bin"
    "-isystem ${gccLib}/include"
    "-isystem ${sysroot}/usr/include/${multiarch}"
    "-isystem ${sysroot}/usr/include"
    "-march=armv8-a -mtune=cortex-a35"
    # GCC 10+ calls helpers for atomics (__aarch64_ldadd4_rel ...) that
    # GCC 9's libgcc lacks; the Cortex-A35 (ARMv8.0, no LSE) would take the
    # plain instructions anyway.
    "-mno-outline-atomics"
  ];
  cxxflags = lib.concatStringsSep " " [
    "-nostdinc -nostdinc++"
    "--sysroot=${sysroot}"
    "-B${binutils}/${tp}/bin"
    "-isystem ${sysroot}/usr/include/c++/9"
    "-isystem ${sysroot}/usr/include/${multiarch}/c++/9"
    "-isystem ${sysroot}/usr/include/c++/9/backward"
    "-isystem ${gccLib}/include"
    "-isystem ${sysroot}/usr/include/${multiarch}"
    "-isystem ${sysroot}/usr/include"
    "-march=armv8-a -mtune=cortex-a35"
    # GCC 10+ calls helpers for atomics (__aarch64_ldadd4_rel ...) that
    # GCC 9's libgcc lacks; the Cortex-A35 (ARMv8.0, no LSE) would take the
    # plain instructions anyway.
    "-mno-outline-atomics"
  ];
  ldflags = lib.concatStringsSep " " [
    "-B${sysroot}/usr/lib/${multiarch}"
    "-B${gccLib}"
    "-L${sysroot}/usr/lib/gcc/${multiarch}/9"
    "-L${sysroot}/usr/lib/${multiarch}"
    "-L${sysroot}/lib/${multiarch}"
    "-Wl,-rpath-link,${sysroot}/usr/lib/${multiarch}:${sysroot}/lib/${multiarch}"
    "-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1"
  ];
  # Linking C++: the sysroot's libstdc++, never GCC's own (-nostdlib++).
  driver = name: compiler: flags: linkFlags: libs: pkgs.writeShellScript name ''
    for a in "$@"; do
      case "$a" in
        -c|-S|-E|-M|-MM) exec ${gcc}/bin/${tp}-${compiler} ${flags} "$@" ;;
      esac
    done
    exec ${gcc}/bin/${tp}-${compiler} ${flags} ${linkFlags} "$@" ${libs}
  '';
  cc = driver "arkos-gcc" "gcc" cflags ldflags "-lm";
  cxx = driver "arkos-g++" "g++" cxxflags "-nostdlib++ ${ldflags}" "-lstdc++ -lm";

  # What the game links against instead of eoan's libSDL2, which needs
  # ALSA, PulseAudio, X11, Wayland ... (not in the sysroot): a library of the
  # same name exporting the same functions, empty. The device loads its own
  # SDL2 by that name at run time.
  sdlStub = pkgs.runCommand "libSDL2-link-stub" { } ''
    mkdir -p $out/lib
    ${binutils}/bin/${tp}-nm -D --defined-only ${sysroot}/usr/lib/${multiarch}/libSDL2-2.0.so.0 \
      | awk '$2 == "T" { print "void " $3 "(void) {}" }' > stub.c
    ${cc} -shared -fPIC -o $out/lib/libSDL2.so -Wl,-soname,libSDL2-2.0.so.0 stub.c
  '';

  r36s = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-r36s";
    inherit version;
    src = lib.cleanSource ../.;
    nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config pkgs.python3 ];
    # pkg-config must see the sysroot's .pc files (fontconfig, freetype).
    PKG_CONFIG_PATH = "${sysroot}/usr/lib/${multiarch}/pkgconfig";
    PKG_CONFIG_SYSROOT_DIR = "${sysroot}";
    cmakeFlags = [
      "-DCMAKE_SYSTEM_NAME=Linux"
      "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
      "-DCMAKE_SYSROOT=${sysroot}"
      "-DCMAKE_C_COMPILER=${cc}"
      "-DCMAKE_CXX_COMPILER=${cxx}"
      "-DCMAKE_AR=${binutils}/bin/${tp}-ar"
      "-DCMAKE_RANLIB=${binutils}/bin/${tp}-ranlib"
      "-DCMAKE_STRIP=${binutils}/bin/${tp}-strip"
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_SKIP_RPATH=ON"
      "-DCMAKE_CXX_STANDARD=17"
      "-DXANATOO_GEMINI=OFF"
      # eoan's SDL2 / FreeType: paths into the sysroot (no device RPATH).
      "-DSDL2_INCLUDE_DIRS=${sysroot}/usr/include"
      "-DSDL2_LIBRARIES=${sdlStub}/lib/libSDL2.so"
      "-DFREETYPE_INCLUDE_DIRS=${sysroot}/usr/include/freetype2"
      "-DFREETYPE_INCLUDE_DIR_ft2build=${sysroot}/usr/include/freetype2"
      "-DFREETYPE_INCLUDE_DIR_freetype2=${sysroot}/usr/include/freetype2"
      "-DFREETYPE_LIBRARY=${sysroot}/usr/lib/${multiarch}/libfreetype.so"
      "-DFONTCONFIG_INCLUDE_DIRS=${sysroot}/usr/include"
      "-DFONTCONFIG_LIBRARIES=${sysroot}/usr/lib/${multiarch}/libfontconfig.so"
    ];
    # A device binary: no nix store RPATH, interpreter or shebangs.
    dontPatchELF = true;
    dontPatchShebangs = true;
    postInstall = ''
      ${binutils}/bin/${tp}-strip $out/bin/xanatoo
    '';
    dontStrip = true;
  };

  # PortMaster: the launcher script, the port folder and its metadata.
  # Layout on device (under /roms/ports/):
  #   Xanatoo.sh
  #   xanatoo/xanatoo          binary
  #   xanatoo/examples/…       sample documents (welcome, help, …)
  #   xanatoo/icons/…          toolbar PNGs
  #   xanatoo/fonts/…          DejaVuSans.ttf (no system fontconfig needed)
  portmaster = pkgs.stdenvNoCC.mkDerivation {
    pname = "xanatoo-r36s-portmaster";
    inherit version;
    dontUnpack = true;
    dontPatchShebangs = true; # the launcher runs with the device's bash
    dontFixup = true;
    installPhase = ''
      mkdir -p $out/xanatoo/examples $out/xanatoo/icons $out/xanatoo/fonts
      install -m755 ${r36s}/bin/xanatoo $out/xanatoo/xanatoo
      # Examples next to the binary so find_repo_file / XANATOO_DATA_DIR resolve them
      cp -r ${../examples}/. $out/xanatoo/examples/
      # Icons from the r36s build's share tree (rasterized at build time)
      if [ -d ${r36s}/share/xanatoo/icons ]; then
        cp -r ${r36s}/share/xanatoo/icons/. $out/xanatoo/icons/
      fi
      # Bundled UI font (ArkOS fontconfig is sparse; avoid system lookups)
      cp ${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf \
         $out/xanatoo/fonts/DejaVuSans.ttf
      cp ${../LICENSE} $out/xanatoo/LICENSE.txt
      cp ${../mk/r36s/Xanatoo.sh} $out/Xanatoo.sh
      chmod +x $out/Xanatoo.sh
      substitute ${../mk/r36s/port.json} $out/port.json --subst-var-by version ${version}
      cp ${../mk/r36s/README.md} $out/README.md
    '';
  };

  portmasterZip = pkgs.runCommand "xanatoo-${version}-r36s-portmaster.zip" {
    nativeBuildInputs = [ pkgs.zip ];
  } ''
    mkdir -p $out
    cd ${portmaster}
    zip -r -9 $out/xanatoo.zip .
  '';

in {
  inherit sysroot r36s portmaster portmasterZip;
}
