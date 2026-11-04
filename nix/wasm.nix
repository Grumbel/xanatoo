# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

# Self-contained, offline WebAssembly build. Building SDL2 and FreeType from
# pinned Nix sources avoids Emscripten ports fetching code inside a sandbox.
#
# nix build .#xanatoo-wasm   site at result/index.html
# nix run .#xanatoo-wasm     serve the site locally

{ pkgs, sdlSrc, version }:

let
  emscripten = pkgs.emscripten;

  setupEmCache = name: ''
    export EM_CACHE="$TMPDIR/emcache-${name}"
    cp -r ${emscripten}/share/emscripten/cache "$EM_CACHE"
    chmod -R u+w "$EM_CACHE"
  '';

  commonTools = [ emscripten pkgs.cmake pkgs.ninja pkgs.python3 ];

  sdl2Wasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-sdl2-wasm";
    version = "2.30.3";
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools;
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "sdl2"}
      cp -r ${sdlSrc} SDL2-src
      chmod -R u+w SDL2-src
      emcmake cmake -S SDL2-src -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$out \
        -DCMAKE_INSTALL_BINDIR=bin \
        -DCMAKE_INSTALL_INCLUDEDIR=include -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
      cmake --build build --parallel ''${NIX_BUILD_CORES:-1}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      cmake --install build
      test -f $out/lib/libSDL2.a
      runHook postInstall
    '';
    dontStrip = true;
  };

  freetypeWasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-freetype-wasm";
    inherit (pkgs.freetype) version src;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools;
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "freetype"}
      emcmake cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$out \
        -DCMAKE_INSTALL_BINDIR=bin \
        -DCMAKE_INSTALL_INCLUDEDIR=include -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_ZLIB=TRUE -DFT_DISABLE_BZIP2=TRUE \
        -DFT_DISABLE_PNG=TRUE -DFT_DISABLE_HARFBUZZ=TRUE \
        -DFT_DISABLE_BROTLI=TRUE
      cmake --build build --parallel ''${NIX_BUILD_CORES:-1}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      cmake --install build
      test -f $out/lib/libfreetype.a
      runHook postInstall
    '';
    dontStrip = true;
  };

  xanatooWasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-wasm";
    inherit version;
    src = pkgs.lib.cleanSource ../.;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools;
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "xanatoo"}
      emcmake cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$out \
        -DPROJECT_VERSION_FULL=${version} \
        -DXANATOO_MINIMAL_PORT=ON -DBUILD_TESTING=OFF \
        -DXANATOO_WASM_SDL2_ROOT=${sdl2Wasm} \
        -DXANATOO_WASM_FREETYPE_ROOT=${freetypeWasm}
      cmake --build build --parallel ''${NIX_BUILD_CORES:-1}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      cmake --install build
      for f in index.html xanatoo.js xanatoo.wasm xanatoo.data; do
        test -s "$out/$f"
      done
      runHook postInstall
    '';
    dontStrip = true;
    meta = {
      description = "Offline hypertext workspace (WebAssembly)";
      license = pkgs.lib.licenses.gpl3Plus;
      platforms = pkgs.lib.platforms.linux;
    };
  };

  serveApp = {
    type = "app";
    program = toString (pkgs.writeShellScript "xanatoo-wasm-serve" ''
      set -e
      export PATH=${pkgs.python3}/bin:${pkgs.xdg-utils}/bin:$PATH
      cd ${xanatooWasm}
      echo "Serving xanatoo at http://127.0.0.1:8765/"
      python3 -m http.server 8765 --bind 127.0.0.1 &
      server_pid=$!
      trap 'kill "$server_pid" 2>/dev/null || true' EXIT
      sleep 0.4
      xdg-open http://127.0.0.1:8765/ >/dev/null 2>&1 || true
      wait "$server_pid"
    '');
    meta.description = "Serve the WASM build on localhost";
  };
in {
  inherit sdl2Wasm freetypeWasm xanatooWasm serveApp;
}
