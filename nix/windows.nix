# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

# Offline MinGW-w64 cross builds. On nixpkgs, SDL2 is sdl2-compat, which
# implements the existing SDL2 API on top of SDL3. Keep a classic SDL2 builder
# as a fallback while the app and vendored ImGui backend migrate to SDL3 APIs.
# FreeType comes from nixpkgs' target package set.
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
        # SDL2's sdl2.pc joins these with ${prefix} / ${exec_prefix}.
        # Absolute Nix store paths would produce invalid doubled paths.
        "-DCMAKE_INSTALL_BINDIR=bin"
        "-DCMAKE_INSTALL_INCLUDEDIR=include"
        "-DCMAKE_INSTALL_LIBDIR=lib"
        "-DSDL_SHARED=ON"
        "-DSDL_STATIC=OFF"
        "-DSDL_TEST=OFF"
      ];
      doCheck = false;
    };

  mkApp = { crossPkgs, sdl2, arch, sdl3Compat }:
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
      ] ++ lib.optionals (!sdl3Compat) [
        "-DSDL2_DIR=${sdl2}/lib/cmake/SDL2"
      ];
      doCheck = false; # Cannot run Windows executables in the Linux builder.
    };

  mkFolder = { crossPkgs, arch, sdl3Compat ? true }:
    let
      sdl2 = if sdl3Compat then crossPkgs.SDL2
             else mkSDL2 { inherit crossPkgs arch; };
      app = mkApp { inherit crossPkgs sdl2 arch sdl3Compat; };
    in pkgs.runCommand "xanatoo-${version}-${arch}" {
      nativeBuildInputs = [ pkgs.python3 pkgs.binutils ];
    } ''
      mkdir -p $out/bin $out/share
      cp -r ${app}/bin/. $out/bin/
      cp -r ${app}/share/. $out/share/

      # Copy the PE import closure, including SDL2, FreeType, compression
      # libraries and MinGW runtime. The helper checks for missing DLLs.
      dll_search_dirs=(
        ${sdl2}
        ${lib.getDev sdl2}
        ${lib.getBin sdl2}
        ${lib.optionalString sdl3Compat "${crossPkgs.sdl3} ${lib.getDev crossPkgs.sdl3} ${lib.getBin crossPkgs.sdl3}"}
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

      ${lib.optionalString sdl3Compat ''
        # sdl2-compat loads SDL3 at runtime rather than necessarily listing
        # it as a PE import. Bundle it explicitly and inspect its imports too.
        sdl3_dll=$(find ${crossPkgs.sdl3} ${lib.getDev crossPkgs.sdl3} ${lib.getBin crossPkgs.sdl3} -type f -iname SDL3.dll -print -quit)
        if [ -z "$sdl3_dll" ]; then
          echo "Missing SDL3.dll in ${crossPkgs.sdl3}" >&2
          exit 1
        fi
        cp "$sdl3_dll" "$out/bin/SDL3.dll"
        python3 ${../mk/windows/collect_dlls.py} $out/bin/SDL3.dll \
          "''${dll_search_dirs[@]}"
      ''}

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
