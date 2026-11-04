# Portable desktop and web builds

The first Windows and WebAssembly ports deliberately use a reduced, **offline**
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
the system browser via SDL2; it cannot open through xanatoo's native network
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
the `-zip` outputs contain those folders. `.#sdl2-wasm` and
`.#freetype-wasm` expose the individual WebAssembly libraries for debugging.
`nix/wasm.nix` compiles SDL2 from the pinned `sdl2-src` flake input and
FreeType from the nixpkgs source, offline. The `XANATOO_WASM_*_ROOT` CMake
options select these libraries without triggering Emscripten port downloads.

`.github/workflows/check.yml` executes `nix flake check` and uploads the
actual Windows and web flake outputs. There is no separate, drifting CI build
recipe. The standalone commands below remain useful for development outside
Nix, but are not the source of truth for releases.

## Windows (MinGW-w64)

The flake cross-compiles both `x86_64` and `i686` with MinGW-w64. The Windows
packages now use nixpkgs' `SDL2` (sdl2-compat), backed by SDL3. They ship
`SDL3.dll` as well as the SDL2-compatible DLL. This moves their runtime onto
SDL3 without changing the application or vendored ImGui SDL2 APIs yet; full
source migration is planned separately. A classic SDL2 source builder remains
in `nix/windows.nix` as a fallback if the compatibility package cannot be
cross-compiled on a given nixpkgs revision.

For an alternative local MINGW64 (or MINGW32) shell, install the corresponding
`mingw-w64-...-gcc`, `cmake`, `ninja`, `SDL2`, and `freetype` packages and run:

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

Install the Emscripten SDK and Ninja; use Emscripten's built-in SDL2 and
FreeType ports (there is no native SDL/FreeType installation to link):

```sh
emcmake cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel
cmake --install build/wasm --prefix dist/web
python3 -m http.server --directory dist/web 8000
```

Open `http://localhost:8000/`. The deployment includes `index.html`,
`xanatoo.js`, `xanatoo.wasm`, and `xanatoo.data`; serve all four from the
same directory. The last file preloads the welcome document, styles,
icons, and fallback fonts into the Emscripten virtual filesystem under
`/data`. The browser drives `run_frame()` via Emscripten's main loop, not
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
