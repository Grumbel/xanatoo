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

  mupdfSrc = pkgs.fetchurl {
    url = "https://mupdf.com/downloads/archive/mupdf-1.28.5-source.tar.gz";
    hash = "sha256-mKXBDNogw5ks33b/ayoRScMr15zHltP3AyMLEYW36TQ=";
  };

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
        -DCMAKE_C_FLAGS=-pthread \
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

  # thumtoo's libraries (libvips + glib, libarchive, SQLite, MuPDF, DjVuLibre,
  # libunarr) as static wasm32 libraries with pthreads: PDF / DjVu / comics.
  sqliteAmalgamation = pkgs.stdenv.mkDerivation {
    pname = "sqlite-amalgamation";
    inherit (pkgs.sqlite) version src;
    nativeBuildInputs = [ pkgs.unzip pkgs.tcl ];
    configureFlags = [ "--disable-tcl" ];
    buildPhase = "make sqlite3.c";
    installPhase = "mkdir -p $out && cp sqlite3.c sqlite3.h $out/";
  };

  imageLibsBase = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-imagelibs-base-wasm";
    version = pkgs.vips.version;
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools ++ (with pkgs; [
      meson pkg-config autoconf automake libtool glib.dev gettext bzip2
    ]);
    env = {
      PATCH_GTYPE = "${../mk/wasm/scripts/patch-gtype.py}";
      ZLIB_SRC = "${pkgs.zlib.src}";
      LIBFFI_SRC = "${pkgs.libffi.src}";
      PCRE2_SRC = "${pkgs.pcre2.src}";
      GLIB_SRC = "${pkgs.glib.src}";
      JPEG_SRC = "${pkgs.libjpeg_turbo.src}";
      LIBPNG_SRC = "${pkgs.libpng.src}";
      WEBP_SRC = "${pkgs.libwebp.src}";
      EXPAT_SRC = "${pkgs.expat.src}";
      LIBARCHIVE_SRC = "${pkgs.libarchive.src}";
      VIPS_SRC = "${pkgs.vips.src}";
      UNARR_SRC = "${pkgs.libunarr.src}";
      DJVULIBRE_SRC = "${pkgs.djvulibre.src}";
      DJVULIBRE_VERSION = pkgs.djvulibre.version;
      SQLITE_AMALGAMATION = "${sqliteAmalgamation}";
      SQLITE_VERSION = pkgs.sqlite.version;
      # libintl stub that glib's wrap would download.
      PROXY_LIBINTL = "${pkgs.fetchurl {
        url = "https://github.com/frida/proxy-libintl/archive/refs/tags/0.5.tar.gz";
        sha256 = "f7a1cbd7579baaf575c66f9d99fb6295e9b0684a28b095967cfda17857595303";
      }}";
    };
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "imagelibs"}
      PREFIX=$out bash ${../mk/wasm/scripts/build-imagelibs.sh}
      runHook postBuild
    '';
    installPhase = "true";
    dontStrip = true;
    dontFixup = true;
  };

  # The same prefix plus MuPDF (thumtoo's pin, flake.nix pinMupdf: nixpkgs'
  # 1.27 lacks APIs it uses).
  imageLibsWasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-imagelibs-wasm";
    version = pkgs.vips.version;
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools;
    env.MUPDF_SRC = "${mupdfSrc}";
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "mupdf"}
      mkdir -p $out
      cp -a ${imageLibsBase}/. $out/
      chmod -R u+w $out
      PREFIX=$out bash ${../mk/wasm/scripts/build-mupdf.sh}
      runHook postBuild
    '';
    installPhase = "true";
    dontStrip = true;
    dontFixup = true;
  };

  xanatooWasm = pkgs.stdenv.mkDerivation {
    pname = "xanatoo-wasm";
    inherit version;
    src = pkgs.lib.cleanSource ../.;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = commonTools ++ [ pkgs.pkg-config ];
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "xanatoo"}
      emcmake cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$out \
        -DPROJECT_VERSION_FULL=${version} \
        -DXANATOO_SDL3=ON -DXANATOO_MINIMAL_PORT=ON -DBUILD_TESTING=OFF \
        -DXANATOO_WASM_SDL3_ROOT=${sdl3Wasm} \
        -DXANATOO_WASM_FREETYPE_ROOT=${freetypeWasm} \
        -DXANATOO_WASM_DEPS_ROOT=${imageLibsWasm}
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
      echo "xanatoo ${version} (wasm)"
      # Port 0: a free port each run, so a browser never reuses a cached
      # older build from the previous origin. The server prints its URL.
      url_file=$(mktemp)
      python3 ${../mk/wasm/scripts/serve.py} ${xanatooWasm} 0 > "$url_file" &
      server_pid=$!
      trap 'kill "$server_pid" 2>/dev/null || true; rm -f "$url_file"' EXIT
      for _ in $(seq 50); do [ -s "$url_file" ] && break; sleep 0.1; done
      url=$(head -n1 "$url_file")
      echo "Serving xanatoo $url"
      xdg-open "$url" >/dev/null 2>&1 || true
      wait "$server_pid"
    '');
    meta.description = "Serve the WASM build on localhost";
  };
in {
  inherit sdl3Wasm freetypeWasm imageLibsWasm xanatooWasm serveApp;
}
