# Portable desktop and web builds

The Windows and WebAssembly packages deliberately use a reduced, **offline**
feature set. Their goal is to exercise the same document model, SDL rendering,
FreeType typography, editor, and hypertext navigation as the Linux application
before bringing additional network and storage backends across platforms.

`XANATOO_MINIMAL_PORT` defaults to **ON** for Windows (32- and 64-bit) and
Emscripten, and **OFF** elsewhere. It disables:

- FontConfig, SQLite and thumtoo/libvips;
- the native TLS/HTTP clients (Gemini, Hacker News, Reddit, Lemmy, Nostr,
  feeds, Wikipedia, web pages, and remote ebooks).

This keeps the required third-party dependencies to SDL and FreeType. Fonts
are loaded from `share/xanatoo/fonts/` (bundled DejaVu faces); images fall
back to the built-in stb_image decoders. Local Markdown, Org, structured
documents, simplified HTML, direct EPUB import, editing, links, and the
document UI remain available. An external `http(s)` link can still open in
the system browser via SDL3; it cannot open through xanatoo's native network
reader in these builds. Pandoc subprocess imports are unavailable.

## Canonical Nix builds and CI

The **flake is authoritative**. On an x86_64 Linux build host:

```sh
nix build -L .#xanatoo-win32-zip
nix build -L .#xanatoo-win64-zip
nix build -L .#xanatoo-wasm
nix run .#xanatoo-wasm  # serve the site at localhost:8765
nix flake check -L      # includes the platform packages and native tests
```

The Windows folder outputs are `.#xanatoo-win32` and `.#xanatoo-win64`;
the `-zip` outputs contain those folders. `.#sdl3-wasm` and
`.#freetype-wasm` expose the individual WebAssembly libraries for debugging.
`nix/wasm.nix` compiles SDL3 and FreeType from nixpkgs sources, offline. The
`XANATOO_WASM_*_ROOT` CMake options select these libraries without triggering
Emscripten port downloads.

`.github/workflows/check.yml` executes `nix flake check` and uploads the
actual Windows and web flake outputs. There is no separate, drifting CI build
recipe. The standalone commands below remain useful for development outside
Nix, but are not the source of truth for releases.

## Windows (MinGW-w64)

The flake cross-compiles both `x86_64` and `i686` with MinGW-w64 and links
directly to SDL3. All regular builds use the SDL3 application backend and the
small single-window ImGui adapter (`src/imgui_sdl3.cpp`). The shared
`xanatoo/sdl.hpp` boundary handles window startup, text input, timer/mouse
differences, surfaces, drawing, gamepads, keyboard/events, touch, cursors,
path ownership, and platform reporting. The SDL2 backend remains available
only by explicitly configuring `-DXANATOO_SDL3=OFF`.

```sh
nix build -L .#xanatoo-sdl3-native
# Or, without Nix, with SDL3 and FreeType development packages installed:
cmake -S . -B build/sdl3 -G Ninja -DXANATOO_SDL3=ON \
  -DXANATOO_MINIMAL_PORT=ON
cmake --build build/sdl3
```

`nix flake check -L` includes the native SDL3 package and the `sdl3-api`
compile-only bridge check. ImGui gamepad navigation and secondary viewports
are not supported by the minimal backend.

R36S/ArkOS uses SDL3 built for the eoan glibc/libstdc++ sysroot and bundled
with the PortMaster package, using the KMS/DRM recipe from
`Grumbel/sdl-jstest`:

```sh
nix build -L .#sdl3-r36s                    # SDL3 library for ArkOS
nix build -L .#xanatoo-r36s                 # application
nix build -L .#xanatoo-r36s-portmaster-zip
```

The PortMaster bundle ships `libSDL3.so.0` and uses the regular launcher
without SDL2 preloading. The old SDL2 ABI package remains available under
the `xanatoo-r36s-sdl2*` names as a rollback option.

For an alternative local MINGW64 (or MINGW32) shell, install the corresponding
`mingw-w64-...-gcc`, `cmake`, `ninja`, `SDL3`, and `freetype` packages and run:

```sh
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows --parallel
cmake --install build/windows --prefix dist/windows
python mk/windows/collect_dlls.py dist/windows/bin/xanatoo.exe /mingw64/bin
```

Use `/mingw32/bin` when compiling for 32-bit Windows. The DLL collection
helper walks PE imports and copies all required **non-system** MinGW DLLs
beside `xanatoo.exe`. The distributable is `dist/windows/` in its entirety,
including `share/xanatoo` and `share/icons`, not just the executable. The
Windows state/config/cache directories use SDL's per-user preferences path;
`--portable` and individual directory overrides are still supported.

To run directly from the build tree, use `build/windows/xanatoo.exe`.

## WebAssembly (Emscripten)

Install the Emscripten SDK and Ninja, then build a static SDL3 library for
Emscripten using `mk/wasm/scripts/build-sdl3.sh`. Point CMake at that install:

```sh
SDL_SRC=/path/to/SDL PREFIX=/tmp/sdl3-wasm \
  bash mk/wasm/scripts/build-sdl3.sh
emcmake cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DXANATOO_SDL3=ON -DXANATOO_WASM_SDL3_ROOT=/tmp/sdl3-wasm
cmake --build build/wasm --parallel
cmake --install build/wasm --prefix dist/web
python3 -m http.server --directory dist/web 8000
```

Open `http://localhost:8000/`. The deployment includes `index.html`,
`xanatoo.js`, `xanatoo.wasm`, and `xanatoo.data`; serve all four from the
same directory. The last file preloads the welcome document, styles,
icons, and fallback fonts into `/data` in Emscripten's virtual filesystem.
It also includes every example document and its relative images, plus
`FORMAT.md`. The browser drives `run_frame()` via Emscripten's main loop, not
a blocking native SDL loop.

This **preview is ephemeral**: file-system persistence, native file dialogs,
host-file imports, subprocesses, and network service clients are not yet
implemented. Changes may be edited during a session but are not saved, even
if the user has a local file open in another browser tab. Refreshing the
page restarts the welcome document. A browser-to-app file import/export
bridge and persistent storage (for example, IndexedDB) are future work.

## Verification and further work

Run the minimal profile on Linux to exercise most shared code without either
cross-compiler:

```sh
cmake -S . -B build/minimal -G Ninja -DXANATOO_MINIMAL_PORT=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/minimal --parallel
ctest --test-dir build/minimal --output-on-failure
./build/minimal/xanatoo --new-session --no-save
```

Cross-build verification still requires those toolchains; native Linux checks do not prove
the Windows or WebAssembly binaries will run.

Next steps: run each artifact on its actual platform, fix any platform-specific
compiler/runtime issues; implement browser file import/export and persistence;
then migrate the network stack behind a cross-platform transport (fetch on
Wasm, socket/TLS backend on Windows) before turning services back on. Do not
disable `XANATOO_MINIMAL_PORT` on these targets unless their dependencies and
platform implementations have been validated.
