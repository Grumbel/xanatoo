# Develop environment

Out-of-tree CMake workflow via `nix develop` (see biltoo / Pingus pattern).

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

Helpers are **PATH scripts** (`writeShellScriptBin`), not shell functions, so
`nix develop -c …` works.

## Environment

| Variable | Default | Meaning |
|----------|---------|---------|
| `XANATOO_SOURCE` | `$PWD` in shellHook | Source tree; **required** by helpers |
| `XANATOO_BUILD_DIR` | `/tmp/xanatoo-build` | Out-of-tree build directory |
| `CMAKE_BUILD_TYPE` | `Debug` in develop | Override before configure |
| `XANATOO_FONT` | DejaVu Sans from nixpkgs | Same as package wrapper |
| `XANATOO_ICON_DIR` | `$XANATOO_BUILD_DIR/icons` | Rasterized toolbar icons |
| `XANATOO_DATA_DIR` | `$XANATOO_SOURCE` | examples / welcome / help lookup |

Running a helper **outside** `nix develop` fails with a clear error
(`XANATOO_SOURCE is not set`).

## Package builds

```sh
nix build          # RelWithDebInfo + wrapped binary (font + icons)
nix build .#xanatoo-unwrapped
```

Develop uses **Debug**; packages use **RelWithDebInfo**. No ccache requirement
for plain `nix build`.
