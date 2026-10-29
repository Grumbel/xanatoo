# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

{
  description = "xanatoo — interactive document composition engine";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # SDL2 sources for the Android build (nix/android.nix).
    sdl2-src = {
      url = "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-2.30.3.tar.gz";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, sdl2-src }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      # gamepkgstd VER-5: VERSION is the only place the version is written.
      versionBase = nixpkgs.lib.fileContents ./VERSION;
      gitRev = self.shortRev or self.dirtyShortRev or "dirty";
      version =
        if nixpkgs.lib.hasInfix "-dev" versionBase
        then "${versionBase}.${toString (self.revCount or 0)}+g${gitRev}"
        else versionBase;

      # thumtoo's dependency list (libvips, libarchive, MuPDF pin, …) from the
      # vendored flake.nix: external/ is no flake input, so its outputs
      # function is evaluated directly (lib uses only nixpkgs), as galapix does.
      thumtooLib = ((import ./external/thumtoo/flake.nix).outputs {
        self = { };
        inherit nixpkgs;
        benchtoo = null;
      }).lib;
      thumtooBuildInputs = pkgs:
        thumtooLib.mkBuildInputs (pkgs // { mupdf = thumtooLib.pinMupdf pkgs; });

      # Android (Linux hosts): the SDK and NDK are unfree. Android 5.0
      # (API 21) is the minimum; the Java side compiles against API 33.
      androidFor = system:
        let
          androidPkgs = import nixpkgs {
            inherit system;
            config.allowUnfree = true;
            config.android_sdk.accept_license = true;
          };
        in import ./nix/android.nix {
          pkgs = androidPkgs;
          inherit nixpkgs;
          sdlSrc = sdl2-src;
          sdlVersion = "2.30.3";
          buildToolsVersion = "30.0.3";
          packagePlatform = "21";
          compilePlatform = "33";
          targetAbis = [ "armeabi-v7a" "arm64-v8a" "x86_64" ];
          androidSdk = (androidPkgs.androidenv.composeAndroidPackages {
            platformVersions = [ "21" "33" ];
            buildToolsVersions = [ "30.0.3" ];
            includeNDK = true;
            ndkVersion = "29.0.14206865";
            includeEmulator = false;
            includeSources = false;
          }).androidsdk;
          inherit version;
          # Last commit time: also set for dirty trees (revCount is not), so
          # local test builds install over committed ones; < 2^31 until 2036.
          versionCode = self.lastModified or 1;
        };
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          xanatoo = pkgs.stdenv.mkDerivation {
            pname = "xanatoo";
            inherit version;
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              # CMake custom command runs scripts/rasterize_icons.py (SVG→PNG).
              python3
            ];

            buildInputs = with pkgs; [
              SDL2
              # nixpkgs SDL2 is sdl2-compat: runtime needs matching SDL3 on the
              # loader path (see develop shell LD_LIBRARY_PATH).
              sdl3
              freetype
              fontconfig
              expat # fontconfig.pc Requires: expat
              openssl
              mbedtls # optional TLS backend: -DXANATOO_TLS=mbedtls
              sqlite
              # Prefer top-level libx11; xorg.* package set is deprecated.
              libx11
              libGL
            ] ++ thumtooBuildInputs pkgs;

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
              "-DPROJECT_VERSION_FULL=${version}"
            ];
            # CTest unit tests (FLAKE-7); skipped automatically when cross.
            doCheck = true;

            meta = with pkgs.lib; {
              description = "Interactive document composition engine";
              license = licenses.gpl3Plus;
              platforms = platforms.linux ++ platforms.darwin;
              mainProgram = "xanatoo";
            };
          };

          # The binary wrapped with a font and pandoc; data is found next
          # to the binary (<exe>/../share/xanatoo).
          wrapped = pkgs.symlinkJoin {
            name = "xanatoo-${version}";
            paths = [ xanatoo ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              wrapProgram $out/bin/xanatoo \
                --set-default XANATOO_FONT \
                  "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf" \
                --prefix PATH : "${pkgs.pandoc}/bin"
            '';
            meta = xanatoo.meta;
          };
        in
        {
          # FLAKE-1: default and xanatoo are the same native build.
          default = wrapped;
          xanatoo = wrapped;

          # Unwrapped binary for packaging / debugging
          xanatoo-unwrapped = xanatoo;
        } // (
          # R36S / ArkOS PortMaster packages (Linux hosts only: cross to aarch64).
          if pkgs.stdenv.hostPlatform.isLinux then
            let
              date = "1970-01-01";
              r36s = import ./nix/r36s.nix { inherit pkgs version date; };
              android = androidFor system;
            in {
              xanatoo-android = android.apk;
              sdl2-android = android.sdlAndroidLibs;
              imagedeps-android = android.imageLibs;
              arkos-sysroot = r36s.sysroot;
              xanatoo-r36s = r36s.r36s;
              xanatoo-r36s-portmaster = r36s.portmaster;
              xanatoo-r36s-portmaster-zip = r36s.portmasterZip;
            }
          else { }
        ));

      # gamepkgstd FLAKE-9..11: `nix flake check` builds every package, checks
      # that every app's program exists, and runs the extra checks:
      # --version / --help (OPT-2, VER-3), man page, REUSE, and desktop
      # integration (INT-6..8: every launcher file is installed, the
      # .desktop / metainfo files validate).
      checks = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          pkg = self.packages.${system}.xanatoo-unwrapped;
          appChecks = nixpkgs.lib.mapAttrs' (name: app:
            nixpkgs.lib.nameValuePair "app-${name}"
              (pkgs.runCommand "xanatoo-check-app-${name}" { } ''
                test -x ${app.program}
                touch $out
              '')) self.apps.${system};
        in self.packages.${system} // appChecks // {
          version = pkgs.runCommand "xanatoo-check-version" { } ''
            out_v=$(${pkg}/bin/xanatoo --version)
            if [ "$out_v" != "xanatoo ${version}" ]; then
              echo "--version printed '$out_v', expected 'xanatoo ${version}'" >&2
              exit 1
            fi
            touch $out
          '';
          help = pkgs.runCommand "xanatoo-check-help" { } ''
            ${pkg}/bin/xanatoo --help > /dev/null
            touch $out
          '';
          reuse = pkgs.runCommand "xanatoo-check-reuse" {
            nativeBuildInputs = [ pkgs.reuse ];
          } ''
            cd ${self}
            HOME=$TMPDIR reuse lint
            touch $out
          '';
          man = pkgs.runCommand "xanatoo-check-man" {
            nativeBuildInputs = [ pkgs.mandoc ];
          } ''
            # stdenv fixup gzips man pages
            mandoc -T lint -W warning ${pkg}/share/man/man1/xanatoo.1.gz
            touch $out
          '';
          layout = pkgs.runCommand "xanatoo-check-layout" { } ''
            for f in \
              bin/xanatoo \
              share/man/man1/xanatoo.1.gz \
              share/applications/xanatoo.desktop \
              share/metainfo/xanatoo.metainfo.xml \
              share/icons/hicolor/scalable/apps/xanatoo.svg \
              share/icons/hicolor/{16x16,32x32,48x48,64x64,128x128,256x256}/apps/xanatoo.png
            do
              if [ ! -e ${pkg}/$f ]; then
                echo "missing: $f" >&2
                exit 1
              fi
            done
            touch $out
          '';
          integration = pkgs.runCommand "xanatoo-check-integration" {
            nativeBuildInputs = [ pkgs.desktop-file-utils pkgs.appstream ];
          } ''
            desktop-file-validate ${pkg}/share/applications/xanatoo.desktop
            # The app ID is the bare executable name on purpose (INT-1).
            appstreamcli validate --no-net \
              --override=cid-desktopapp-is-not-rdns=info \
              ${pkg}/share/metainfo/xanatoo.metainfo.xml
            touch $out
          '';
        });

      # FLAKE-5: run the native build; adb-install the APK (Linux hosts).
      apps = forAllSystems (system:
        let
          run = {
            type = "app";
            program = nixpkgs.lib.getExe self.packages.${system}.default;
            meta.description = "Run xanatoo";
          };
        in {
          default = run;
          xanatoo = run;
        } // nixpkgs.lib.optionalAttrs (nixpkgs.lib.hasSuffix "-linux" system) {
          install-android-xanatoo = (androidFor system).installApp;
        });

      # Develop shell: deps + env + PATH scripts for out-of-tree CMake.
      # Helpers are writeShellScriptBin so `nix develop -c xanatoo-run` works.
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          # Main package for inputsFrom (pulls buildInputs into the shell).
          pkg = self.packages.${system}.xanatoo-unwrapped;

          dejavuFont =
            "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf";

          # Shared preamble: require shell entry, resolve build dir.
          xanatooDevPreamble = ''
            set -euo pipefail
            if [ -z "''${XANATOO_SOURCE:-}" ]; then
              echo "$0: XANATOO_SOURCE is not set (enter the shell with: nix develop)" >&2
              exit 1
            fi
            XANATOO_BUILD_DIR="''${XANATOO_BUILD_DIR:-/tmp/xanatoo-build}"

            _xanatoo_canon_path() {
              local p="$1"
              p="''${p%/}"
              if [ -d "$p" ]; then
                ( cd "$p" && pwd )
              else
                printf '%s\n' "$p"
              fi
            }

            # Mirror packaged wrapper env for the unwrapped out-of-tree binary.
            export XANATOO_FONT="''${XANATOO_FONT:-${dejavuFont}}"
            # Data directory of the build tree: generated icons, symlinks to
            # the live sources (styles/, examples/, FORMAT.md); DEV-6.
            XANATOO_DATA_ARGS=(--data-dir "$XANATOO_BUILD_DIR/share/xanatoo")

            # Host LD_LIBRARY_PATH often breaks sdl2-compat: wrong libSDL2 or
            # missing SDL3 → SIGABRT in dllinit() before main(). Prefer the
            # nixpkgs set that matches the linked SDL2.
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [
              SDL2 sdl3 libx11 libGL freetype fontconfig openssl sqlite
            ])}''${XANATOO_EXTRA_LD_LIBRARY_PATH:+:$XANATOO_EXTRA_LD_LIBRARY_PATH}"
          '';

          xanatooConfigure = pkgs.writeShellScriptBin "xanatoo-configure" (
            xanatooDevPreamble
            + ''
              mkdir -p "$XANATOO_BUILD_DIR"
              echo "xanatoo-configure: source=$XANATOO_SOURCE"
              echo "xanatoo-configure: build=$XANATOO_BUILD_DIR"
              echo "xanatoo-configure: CMAKE_BUILD_TYPE=''${CMAKE_BUILD_TYPE:-Debug}"
              cmake -S "$XANATOO_SOURCE" -B "$XANATOO_BUILD_DIR" -G Ninja \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                -DCMAKE_CXX_FLAGS_DEBUG="''${CMAKE_CXX_FLAGS_DEBUG:--g3 -Og -fno-omit-frame-pointer}" \
                "$@"
            ''
          );

          xanatooBuild = pkgs.writeShellScriptBin "xanatoo-build" (
            xanatooDevPreamble
            + ''
              # Reconfigure when cache is missing or source path changed.
              if [ ! -f "$XANATOO_BUILD_DIR/CMakeCache.txt" ]; then
                echo "xanatoo-build: no cache → running xanatoo-configure"
                xanatoo-configure || exit 1
              else
                cached=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' \
                  "$XANATOO_BUILD_DIR/CMakeCache.txt" | head -n1 || true)
                want=$(_xanatoo_canon_path "$XANATOO_SOURCE")
                cached_c=
                if [ -n "$cached" ]; then
                  cached_c=$(_xanatoo_canon_path "$cached")
                fi
                if [ -z "$cached" ] || [ ! -d "$cached" ]; then
                  echo "xanatoo-build: cached source missing ($cached) → reconfigure"
                  xanatoo-configure || exit 1
                elif [ -n "$cached_c" ] && [ "$cached_c" != "$want" ]; then
                  echo "xanatoo-build: CMAKE_HOME_DIRECTORY changed:" >&2
                  echo "  was: $cached_c" >&2
                  echo "  now: $want" >&2
                  echo "  → re-running xanatoo-configure" >&2
                  xanatoo-configure || exit 1
                fi
              fi
              cmake --build "$XANATOO_BUILD_DIR" -j "''${XANATOO_BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}" "$@"
            ''
          );

          xanatooRun = pkgs.writeShellScriptBin "xanatoo-run" (
            xanatooDevPreamble
            + ''
              xanatoo-build || exit 1
              if [ ! -x "$XANATOO_BUILD_DIR/xanatoo" ]; then
                echo "xanatoo-run: $XANATOO_BUILD_DIR/xanatoo missing after build" >&2
                exit 1
              fi
              exec "$XANATOO_BUILD_DIR/xanatoo" "''${XANATOO_DATA_ARGS[@]}" "$@"
            ''
          );

          xanatooRunGdb = pkgs.writeShellScriptBin "xanatoo-run-gdb" (
            xanatooDevPreamble
            + ''
              xanatoo-build || exit 1
              if [ ! -x "$XANATOO_BUILD_DIR/xanatoo" ]; then
                echo "xanatoo-run-gdb: $XANATOO_BUILD_DIR/xanatoo missing after build" >&2
                exit 1
              fi
              if ! command -v gdb >/dev/null 2>&1; then
                echo "xanatoo-run-gdb: gdb not found (should be in the nix develop shell)" >&2
                exit 1
              fi
              # -q: quiet banner. -ex run: start without typing "run".
              # Quit only on exit code 0; crashes leave an interactive prompt.
              gdb -q \
                -ex "set pagination off" \
                -ex "set confirm off" \
                -ex "set debuginfod enabled off" \
                -ex run \
                -ex 'python
try:
  ec = gdb.parse_and_eval("$_exitcode")
  if int(ec) == 0:
    gdb.execute("quit")
except Exception:
  pass
' \
                --args "$XANATOO_BUILD_DIR/xanatoo" "''${XANATOO_DATA_ARGS[@]}" "$@"
            ''
          );

          xanatooTest = pkgs.writeShellScriptBin "xanatoo-test" (
            xanatooDevPreamble
            + ''
              xanatoo-build || exit 1
              if [ -x "$XANATOO_BUILD_DIR/xanatoo_tests" ]; then
                "$XANATOO_BUILD_DIR/xanatoo_tests" "$@"
              elif [ -f "$XANATOO_BUILD_DIR/CTestTestfile.cmake" ]; then
                ctest --test-dir "$XANATOO_BUILD_DIR" --output-on-failure "$@"
              else
                echo "xanatoo-test: no xanatoo_tests binary or CTest files in $XANATOO_BUILD_DIR" >&2
                exit 1
              fi
            ''
          );
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.ccacheStdenv; } {
            inputsFrom = [ pkg ];
            packages = with pkgs; [
              cmake
              ninja
              pkg-config
              python3
              gdb
              ccache
              clang-tools
              dejavu_fonts
              xanatooConfigure
              xanatooBuild
              xanatooRun
              xanatooRunGdb
              xanatooTest
              pandoc  # EPUB → Markdown import (optional at runtime)
            ];
            CMAKE_BUILD_TYPE = "Debug";
            shellHook = ''
              export XANATOO_SOURCE="$PWD"
              export XANATOO_BUILD_DIR="''${XANATOO_BUILD_DIR:-/tmp/xanatoo-build}"
              export XANATOO_FONT="''${XANATOO_FONT:-${dejavuFont}}"
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [
                SDL2 sdl3 libx11 libGL freetype fontconfig openssl sqlite
              ])}''${XANATOO_EXTRA_LD_LIBRARY_PATH:+:$XANATOO_EXTRA_LD_LIBRARY_PATH}"
              # Prefer ccache when available (ccacheStdenv or explicit launcher).
              if command -v ccache >/dev/null 2>&1; then
                export CMAKE_CXX_COMPILER_LAUNCHER="''${CMAKE_CXX_COMPILER_LAUNCHER:-ccache}"
              fi
              # Full debug info for out-of-tree builds (override before configure).
              # -Og: optimise for debug; satisfies Nix _FORTIFY_SOURCE (needs -O).
              # C-only FLAGS_DEBUG omitted — project is CXX-only.
              export CMAKE_CXX_FLAGS_DEBUG="''${CMAKE_CXX_FLAGS_DEBUG:--g3 -Og -fno-omit-frame-pointer}"
              cat <<'XANATOO_DEV_BANNER'
xanatoo — nix develop (out-of-tree CMake)

  Pattern: deps + env + PATH scripts here; packaged wrappers stay for
  `nix build` / `nix run`. Same idea as biltoo / Pingus.

  Commands (build-before-run; safe outside this shell → clear error):
    xanatoo-configure   cmake -S $XANATOO_SOURCE -B $XANATOO_BUILD_DIR -G Ninja
    xanatoo-build       reconfigure if cache missing / source path changed, then ninja
    xanatoo-run         build, then run the unwrapped binary (args forwarded)
    xanatoo-run-gdb     build, then gdb auto-run (quit on exit 0; stay on crash)
    xanatoo-test        build, then xanatoo_tests / ctest

  One-shot from outside:
    nix develop -c xanatoo-run examples/welcome.md
    nix develop -c xanatoo-run-gdb
    nix develop -c xanatoo-test

  Why this shape:
    • Helpers are writeShellScriptBin on PATH (not shellHook functions) so
      `nix develop -c …` works.
    • XANATOO_SOURCE is set only in shellHook — helpers refuse to run without it.
    • xanatoo-build compares CMAKE_HOME_DIRECTORY to $XANATOO_SOURCE and
      reconfigures when the checkout moved or the cache is stale/gone.
    • Develop defaults to Debug; `nix build` packages use RelWithDebInfo.
    • Unwrapped binary gets the package wrapper's font (XANATOO_FONT) and
      --data-dir $XANATOO_BUILD_DIR/share/xanatoo (icons + live sources).

  Env (override anytime):
    XANATOO_SOURCE      this tree ($PWD)
    XANATOO_BUILD_DIR   default /tmp/xanatoo-build
    CMAKE_BUILD_TYPE    default Debug
    XANATOO_FONT        DejaVu Sans from nixpkgs

  Typical loop:
    xanatoo-configure && xanatoo-build
    xanatoo-run
    # or: xanatoo-run-gdb

  If the binary aborts before main in sdl2-compat dllinit:
    · this shell sets LD_LIBRARY_PATH to nixpkgs SDL2+SDL3 (host path is toxic)
    · re-enter the shell, or: export SDL2COMPAT_DEBUG_LOGGING=1
    · then: xanatoo-run-gdb   (should stop with a better message on stderr)

  Debug symbols: CMAKE_BUILD_TYPE=Debug with -g3 -Og -fno-omit-frame-pointer
  (set at configure time). After changing flags: rm -rf $XANATOO_BUILD_DIR &&
  xanatoo-configure && xanatoo-build

  Full write-up: docs/ENVIRONMENT.md
XANATOO_DEV_BANNER
              echo "  source=$XANATOO_SOURCE"
              echo "  build=$XANATOO_BUILD_DIR  type=''${CMAKE_BUILD_TYPE:-Debug}"
            '';
          };
        });
    };
}
