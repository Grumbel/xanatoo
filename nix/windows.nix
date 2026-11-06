# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

# Offline MinGW-w64 cross builds using native SDL3 and FreeType from nixpkgs.
#
# nix build .#xanatoo-win64       portable directory (bin + share)
# nix build .#xanatoo-win64-zip   distributable zip
# (replace win64 with win32 for 32-bit Windows)

{ pkgs, version }:

let
  lib = pkgs.lib;

  mkFolder = { crossPkgs, arch }:
    let
      sdl3 = crossPkgs.sdl3;
      app = crossPkgs.stdenv.mkDerivation {
        pname = "xanatoo-${arch}-build";
        inherit version;
        src = lib.cleanSource ../.;
        nativeBuildInputs = [ pkgs.cmake pkgs.python3 ];
        buildInputs = [ sdl3 crossPkgs.freetype crossPkgs.windows.mcfgthreads ];
        cmakeFlags = [
          "-DCMAKE_BUILD_TYPE=Release"
          "-DPROJECT_VERSION_FULL=${version}"
          "-DXANATOO_SDL3=ON"
          "-DXANATOO_MINIMAL_PORT=ON"
          "-DBUILD_TESTING=OFF"
        ];
        doCheck = false;
      };
    in pkgs.runCommand "xanatoo-${version}-${arch}" {
      nativeBuildInputs = [ pkgs.python3 pkgs.binutils ];
    } ''
      mkdir -p $out/bin $out/share
      cp -r ${app}/bin/. $out/bin/
      cp -r ${app}/share/. $out/share/

      dll_search_dirs=(
        ${sdl3}
        ${lib.getDev sdl3}
        ${lib.getBin sdl3}
        ${crossPkgs.freetype}
        ${crossPkgs.libpng}
        ${crossPkgs.zlib}
        ${crossPkgs.bzip2}
        ${crossPkgs.brotli}
        ${crossPkgs.windows.mcfgthreads}
        ${crossPkgs.stdenv.cc.cc}
      )
      python3 ${../mk/windows/collect_dlls.py} $out/bin/xanatoo.exe \
        "''${dll_search_dirs[@]}"

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
