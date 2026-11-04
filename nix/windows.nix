# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

# Offline MinGW-w64 cross builds. SDL2 uses the existing, pinned SDL2 source
# flake input; FreeType comes from nixpkgs' target package set. Both targets
# share the minimal SDL2/FreeType CMake configuration.
#
# nix build .#xanatoo-win64       portable directory (bin + share)
# nix build .#xanatoo-win64-zip   distributable zip
# (replace win64 with win32 for 32-bit Windows)

{ pkgs, sdlSrc, version }:

let
  lib = pkgs.lib;

  mkSDL2 = { crossPkgs, arch }:
    crossPkgs.stdenv.mkDerivation {
      pname = "xanatoo-sdl2-${arch}";
      version = "2.30.3";
      src = sdlSrc;
      nativeBuildInputs = [ pkgs.cmake ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DSDL_SHARED=ON"
        "-DSDL_STATIC=OFF"
        "-DSDL_TEST=OFF"
      ];
      doCheck = false;
    };

  mkApp = { crossPkgs, sdl2, arch }:
    crossPkgs.stdenv.mkDerivation {
      pname = "xanatoo-${arch}-build";
      inherit version;
      src = lib.cleanSource ../.;
      nativeBuildInputs = [ pkgs.cmake pkgs.python3 ];
      buildInputs = [ sdl2 crossPkgs.freetype crossPkgs.windows.mcfgthreads ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DPROJECT_VERSION_FULL=${version}"
        "-DXANATOO_MINIMAL_PORT=ON"
        "-DBUILD_TESTING=OFF"
        "-DSDL2_DIR=${sdl2}/lib/cmake/SDL2"
      ];
      doCheck = false; # Cannot run Windows executables in the Linux builder.
    };

  mkFolder = { crossPkgs, arch }:
    let
      sdl2 = mkSDL2 { inherit crossPkgs arch; };
      app = mkApp { inherit crossPkgs sdl2 arch; };
    in pkgs.runCommand "xanatoo-${version}-${arch}" {
      nativeBuildInputs = [ pkgs.python3 pkgs.binutils ];
    } ''
      mkdir -p $out/bin $out/share
      cp -r ${app}/bin/. $out/bin/
      cp -r ${app}/share/. $out/share/

      # Copy only the imported PE DLL closure, including SDL2, FreeType,
      # compression libraries and MinGW runtime. The helper checks for gaps.
      python3 ${../mk/windows/collect_dlls.py} $out/bin/xanatoo.exe \
        ${sdl2} \
        ${crossPkgs.freetype} \
        ${crossPkgs.libpng} \
        ${crossPkgs.zlib} \
        ${crossPkgs.bzip2} \
        ${crossPkgs.brotli} \
        ${crossPkgs.windows.mcfgthreads} \
        ${crossPkgs.stdenv.cc.cc}

      cp ${../LICENSES/GPL-3.0-or-later.txt} $out/COPYING.txt
    '';

  mkZip = { folder, arch }:
    pkgs.runCommand "xanatoo-${version}-${arch}.zip" {
      nativeBuildInputs = [ pkgs.zip ];
    } ''
      mkdir -p $out
      mkdir work
      cp -r --no-preserve=mode ${folder} work/xanatoo-${version}-${arch}
      cd work
      zip -qr9 $out/xanatoo-${version}-${arch}.zip xanatoo-${version}-${arch}
    '';

  win64 = mkFolder { crossPkgs = pkgs.pkgsCross.mingwW64; arch = "win64"; };
  win32 = mkFolder { crossPkgs = pkgs.pkgsCross.mingw32; arch = "win32"; };
in {
  inherit win64 win32;
  win64Zip = mkZip { folder = win64; arch = "win64"; };
  win32Zip = mkZip { folder = win32; arch = "win32"; };
}
