{
  description = "xanatoo — interactive document composition engine";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          xanatoo = pkgs.stdenv.mkDerivation {
            pname = "xanatoo";
            version = "0.1.0";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              # CMake custom command runs scripts/rasterize_icons.py (SVG→PNG).
              python3
            ];

            buildInputs = with pkgs; [
              SDL2
              freetype
              fontconfig
              openssl
              # Prefer top-level libx11; xorg.* package set is deprecated.
              libx11
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
            ];

            meta = with pkgs.lib; {
              description = "Interactive document composition engine";
              license = licenses.gpl3Plus;
              platforms = platforms.linux ++ platforms.darwin;
              mainProgram = "xanatoo";
            };
          };
        in
        {
          # Default package wraps the binary with font + installed icon path.
          default = pkgs.symlinkJoin {
            name = "xanatoo";
            paths = [ xanatoo ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              wrapProgram $out/bin/xanatoo \
                --set-default XANATOO_FONT \
                  "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf" \
                --set-default XANATOO_ICON_DIR \
                  "${xanatoo}/share/xanatoo/icons"
            '';
            meta = xanatoo.meta;
          };

          # Unwrapped binary for packaging / debugging
          xanatoo-unwrapped = xanatoo;
        } // (
          # R36S / ArkOS PortMaster packages (Linux hosts only: cross to aarch64).
          if pkgs.stdenv.hostPlatform.isLinux then
            let
              version = "0.1.0";
              date = "1970-01-01";
              r36s = import ./nix/r36s.nix { inherit pkgs version date; };
            in {
              arkos-sysroot = r36s.sysroot;
              xanatoo-r36s = r36s.r36s;
              xanatoo-r36s-portmaster = r36s.portmaster;
              xanatoo-r36s-portmaster-zip = r36s.portmasterZip;
            }
          else { }
        ));

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
            export XANATOO_ICON_DIR="''${XANATOO_ICON_DIR:-$XANATOO_BUILD_DIR/icons}"
            # examples/, welcome.md, help: prefer the live source tree
            export XANATOO_DATA_DIR="''${XANATOO_DATA_DIR:-$XANATOO_SOURCE}"
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
              exec "$XANATOO_BUILD_DIR/xanatoo" "$@"
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
                --args "$XANATOO_BUILD_DIR/xanatoo" "$@"
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
            ];
            CMAKE_BUILD_TYPE = "Debug";
            shellHook = ''
              export XANATOO_SOURCE="$PWD"
              export XANATOO_BUILD_DIR="''${XANATOO_BUILD_DIR:-/tmp/xanatoo-build}"
              export XANATOO_FONT="''${XANATOO_FONT:-${dejavuFont}}"
              export XANATOO_ICON_DIR="''${XANATOO_ICON_DIR:-$XANATOO_BUILD_DIR/icons}"
              export XANATOO_DATA_DIR="''${XANATOO_DATA_DIR:-$XANATOO_SOURCE}"
              # Prefer ccache when available (ccacheStdenv or explicit launcher).
              if command -v ccache >/dev/null 2>&1; then
                export CMAKE_CXX_COMPILER_LAUNCHER="''${CMAKE_CXX_COMPILER_LAUNCHER:-ccache}"
              fi
              echo "xanatoo develop: source=$XANATOO_SOURCE build=$XANATOO_BUILD_DIR"
              echo "  xanatoo-configure | xanatoo-build | xanatoo-run | xanatoo-run-gdb | xanatoo-test"
            '';
          };
        });
    };
}
