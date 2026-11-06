# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

# Self-contained, offline WebAssembly build. Build SDL3 and FreeType from
# nixpkgs sources so no Emscripten ports download code in the sandbox.
#
# nix build .#xanatoo-wasm   site at result/index.html
# nix run .#xanatoo-wasm     serve the site locally

{ pkgs, version }:

let
  emscripten = pkgs.emscripten;

  setupEmCache = name: ''
    export EM_CACHE="$TMPDIR/emcache-${name}"
    cp -r ${emscripten}/share/emscripten/cache "$EM_CACHE"
    chmod -R u+w "$EM_CACHE"
  '';

  commonTools = [ emscripten pkgs.cmake pkgs.ninja pkgs.python3 pkgs.gnumake ];

  sdl3Wasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-sdl3-wasm";
    inherit (pkgs.sdl3) version src;
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools;
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "sdl3"}
      PREFIX=$PWD/prefix SDL_SRC=${pkgs.sdl3.src} \
        bash ${../mk/wasm/scripts/build-sdl3.sh}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -a prefix/. $out/
      test -f $out/lib/libSDL3.a
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
        -DXANATOO_SDL3=ON -DXANATOO_MINIMAL_PORT=ON -DBUILD_TESTING=OFF \
        -DXANATOO_WASM_SDL3_ROOT=${sdl3Wasm} \
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
  inherit sdl3Wasm freetypeWasm xanatooWasm serveApp;
}
