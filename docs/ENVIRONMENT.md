# Develop environment — Nix develop shell pattern

Out-of-tree CMake workflow for xanatoo via `nix develop`: **deps + env + PATH
scripts** in the shell; packaged wrappers stay separate for `nix build` /
`nix run`.

Derived from [biltoo](https://github.com/Grumbel/biltoo) and applied similarly
in [Pingus](https://github.com/Pingus/pingus). This file is the permanent
reference; a shorter banner is printed when you enter `nix develop`.

---

## Goals

| Goal | Mechanism |
|------|-----------|
| One command for the common loop | `xanatoo-run` / `xanatoo-run-gdb` **build before run** |
| Fail clearly outside the shell | Preamble requires `XANATOO_SOURCE` (set only in `shellHook`) |
| No silent stale builds | Compare CMake cache vs current source; **reconfigure** on mismatch |
| Live tree while developing | Out-of-tree build under `$XANATOO_BUILD_DIR`; data from `$XANATOO_SOURCE` |
| Unwrapped binary finds assets | Mirror package-wrapper env (`XANATOO_FONT`, icon/data dirs) in helpers |
| Useful debug symbols | **Debug** in develop; **RelWithDebInfo** in packages |
| Comfortable gdb | Auto-`run`; quit on clean exit; keep session on failure |

---

## Commands

| Script | Role |
|--------|------|
| `xanatoo-configure` | `cmake -S $XANATOO_SOURCE -B $XANATOO_BUILD_DIR -G Ninja` (Debug by default) |
| `xanatoo-build` | Reconfigure if cache missing or source path changed, then build |
| `xanatoo-run` | Build, then run `$XANATOO_BUILD_DIR/xanatoo` |
| `xanatoo-run-gdb` | Build, then `gdb` with auto-`run` (quit on exit 0) |
| `xanatoo-test` | Build, then run `xanatoo_tests` / ctest |

```sh
nix develop
xanatoo-configure && xanatoo-build
xanatoo-run examples/welcome.md
# or one-shot:
nix develop -c xanatoo-run-gdb
nix develop -c xanatoo-test
```

Helpers are **PATH scripts** (`writeShellScriptBin`), not `shellHook` functions.
Only real binaries work with `nix develop -c xanatoo-run`.

---

## Minimal flake shape (conceptual)

```nix
devShells.default = pkgs.mkShell.override { stdenv = pkgs.ccacheStdenv; } {
  inputsFrom = [ /* main package — pulls buildInputs */ ];
  packages = [
    pkgs.cmake pkgs.ninja pkgs.gdb pkgs.ccache
    (pkgs.writeShellScriptBin "xanatoo-configure" ''…'')
    (pkgs.writeShellScriptBin "xanatoo-build" ''…'')
    (pkgs.writeShellScriptBin "xanatoo-run" ''…'')
    (pkgs.writeShellScriptBin "xanatoo-run-gdb" ''…'')
    (pkgs.writeShellScriptBin "xanatoo-test" ''…'')
  ];
  CMAKE_BUILD_TYPE = "Debug";
  shellHook = ''
    export XANATOO_SOURCE="$PWD"
    export XANATOO_BUILD_DIR="''${XANATOO_BUILD_DIR:-/tmp/xanatoo-build}"
    # Mirror anything the package wrapper sets for the unwrapped binary
    # export XANATOO_FONT=…
    echo "…"  # banner — see flake.nix
  '';
};
```

---

## Checklist (what each piece does)

### 1. Preamble requires `XANATOO_SOURCE`

Every helper starts with a shared preamble:

```bash
set -euo pipefail
if [ -z "${XANATOO_SOURCE:-}" ]; then
  echo "$0: XANATOO_SOURCE is not set (enter the shell with: nix develop)" >&2
  exit 1
fi
XANATOO_BUILD_DIR="${XANATOO_BUILD_DIR:-/tmp/xanatoo-build}"
```

Running a script outside the shell fails immediately instead of writing into a
random cwd or a stale absolute path.

### 2. Build before run

`xanatoo-run`, `xanatoo-run-gdb`, and `xanatoo-test` call `xanatoo-build` first.
One command for the edit → compile → try loop; no forgotten rebuilds.

### 3. Reconfigure when paths change

CMake records `CMAKE_HOME_DIRECTORY` in the cache. `xanatoo-build` compares the
cached path to the current `$XANATOO_SOURCE` and re-runs `xanatoo-configure` when
they differ or the cache/tree is gone.

Without this, moving a checkout leaves you compiling a ghost tree until someone
notices.

### 4. Nested C++ deps (n/a for xanatoo today)

biltoo resolves a live sibling (`THUMTOO_SOURCE_DIR`) for develop vs flake inputs
for package. xanatoo has no out-of-tree nested library today — everything is
in-tree under `src/` / `include/`. If that changes, prefer:

- **Develop:** live checkout path so edits rebuild incrementally.
- **Package:** flake inputs / fixed store paths.

Warn if a “live” path is under `/nix/store` (frozen snapshot).

### 5. Mirror wrapper env in the shell

Anything the **packaged** binary gets via `makeWrapper` must also be set for the
**unwrapped** out-of-tree binary:

| Concern | xanatoo |
|---------|---------|
| Default font | `XANATOO_FONT` (DejaVu from nixpkgs) |
| Data (icons, examples, help, styles) | `--data-dir $XANATOO_BUILD_DIR/share/xanatoo` |

The build tree mirrors the installed `share/`: `share/xanatoo/icons` is
rasterized, `styles/`, `examples/` and `FORMAT.md` are symlinks to the
source tree (edits show at once), `share/icons` links `data/icons`. The
package finds its data next to the binary (`<exe>/../share/xanatoo`).

Do **not** copy Qt-only variables into this SDL2 project.

### 6. Build type split

| Context | Type |
|---------|------|
| `nix develop` / out-of-tree | `Debug` (override with `CMAKE_BUILD_TYPE=…`) |
| `nix build` package | `RelWithDebInfo` (+ separate debug info where useful) |

### 7. gdb helper

```bash
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
```

- Starts the inferior immediately (no manual `run`).
- Quiet until stop.
- **Exit 0** → gdb quits (no dangling prompt after a successful run).
- **Crash / signal / non-zero** → interactive prompt for `bt`, etc.

---

## Environment variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `XANATOO_SOURCE` | `$PWD` in shellHook | Source tree; **required** by helpers |
| `XANATOO_BUILD_DIR` | `/tmp/xanatoo-build` | Out-of-tree build directory |
| `CMAKE_BUILD_TYPE` | `Debug` in develop | Override before configure |
| `XANATOO_FONT` | DejaVu Sans from nixpkgs | Same as package wrapper |

`xanatoo --show-paths` prints the data, config, state, user data and cache
directories and where each came from.

Running a helper **outside** `nix develop` fails with a clear error
(`XANATOO_SOURCE is not set`).

---

## What *not* to copy blindly

- **Qt-only** env (`QT_PLUGIN_PATH`, offscreen platform) into non-Qt apps.
- **Default build dir** location — any writable sticky path is fine
  (`/tmp/xanatoo-build`, `$HOME/.cache/…`).
- **Nested-library resolvers** unless the flake has the same dual live-tree /
  store-input setup as biltoo’s thumtoo.
- **`inputsFrom = [ package ]`** content — always verify the package’s
  `buildInputs` match what local cmake needs.

---

## Mapping (biltoo → xanatoo)

| biltoo | xanatoo |
|--------|---------|
| `BILTOO_SOURCE` | `XANATOO_SOURCE` |
| `BILTOO_BUILD_DIR` | `XANATOO_BUILD_DIR` (default `/tmp/xanatoo-build`) |
| `biltoo-configure` | `xanatoo-configure` |
| `biltoo-build` | `xanatoo-build` |
| `biltoo-run` | `xanatoo-run` |
| `biltoo-run-gdb` | `xanatoo-run-gdb` |
| `THUMTOO_SOURCE_DIR` | *(n/a — no nested out-of-tree lib)* |
| Qt / PipeWire env | Font + icon + data dirs; SDL2 via `inputsFrom` |

---

## Package builds

```sh
nix build          # RelWithDebInfo + wrapped binary (font + icons)
nix build .#xanatoo-unwrapped
```

Develop uses **Debug**; packages use **RelWithDebInfo**. No ccache requirement
for plain `nix build`.

---

## Reference

- biltoo flake develop shell: https://github.com/Grumbel/biltoo
- Reusable idea: **develop shell = deps + env + PATH scripts; scripts drive
  out-of-tree configure/build/run/gdb; packaged wrappers stay separate for
  `nix build` / `nix run`.**


---

## SDL2 / sdl2-compat startup abort

On current nixpkgs, `SDL2` is **sdl2-compat**: a shim that loads **SDL3** at
runtime. If the dynamic loader finds a host `libSDL2` (via a polluted
`LD_LIBRARY_PATH`) or cannot resolve the matching SDL3, the process can
**`abort()` inside `dllinit` before `main`**:

```
#0  raise / abort
#3  dllinit () from libSDL2-2.0.so.0   # sdl2-compat
#4  _dl_init / _dl_start_user
```

**Mitigation in this shell:** helpers and `shellHook` set `LD_LIBRARY_PATH` to
the nixpkgs `SDL2` + `sdl3` (+ peers), ignoring a hostile host path. Extra
entries: `XANATOO_EXTRA_LD_LIBRARY_PATH`.

**Debug the loader:**

```sh
export SDL2COMPAT_DEBUG_LOGGING=1
xanatoo-run
# or
ldd $XANATOO_BUILD_DIR/xanatoo | grep -i sdl
```

---

## Debug symbols (develop)

`xanatoo-configure` defaults to:

| Flag | Value |
|------|--------|
| `CMAKE_BUILD_TYPE` | `Debug` |
| `CMAKE_CXX_FLAGS_DEBUG` | `-g3 -Og -fno-omit-frame-pointer` |

After changing these, wipe the build dir and reconfigure:

```sh
rm -rf "$XANATOO_BUILD_DIR"
xanatoo-configure && xanatoo-build
xanatoo-run-gdb
```

Package builds remain `RelWithDebInfo` via `nix build` (not stripped of all
info, but optimised). For deepest SDL stack traces you need debuginfo for
glibc/SDL from nixpkgs (`separateDebugInfo` / `enableDebugging`), which is
optional and heavier than the app flags above.

**Note:** plain `-O0` triggers glibc `#warning _FORTIFY_SOURCE requires … -O`
under Nix hardening, and `CMAKE_C_FLAGS_DEBUG` is unused (CXX-only project).
Develop defaults to **`-Og`** instead: still debugger-friendly, keeps Fortify
happy. Override with `CMAKE_CXX_FLAGS_DEBUG='-g3 -O0 -U_FORTIFY_SOURCE …'` if
you truly want zero optimisation.
