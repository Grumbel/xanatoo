# Android port

```sh
nix build .#xanatoo-android          # result/xanatoo-0.1.0.apk
nix run .#install-android-xanatoo    # adb install -r
```

Same pipeline as Kurvenrausch / Pingus (`nix/android.nix`, `mk/android/`):

1. `sdl2-android`: SDL 2.30.3 from its release tarball (flake input
   `sdl2-src`) built with `ndk-build` for every ABI, plus SDL's Java side
   (`SDLActivity`) compiled to `classes.dex`.
2. `xanatoo-android`: `src/*.cpp` (minus `https.cpp`), vendored ImGui and a
   static FreeType module (nixpkgs' `freetype.src`, `mk/android/freetype/
   Android.mk`) into `libmain.so`; `aapt` + `zipalign` + `apksigner` with
   the debug keystore in `mk/android/keystore/`.

Targets Android 5.0+ (`minSdkVersion` 21), ABIs `armeabi-v7a arm64-v8a
x86_64`, NDK 29. The launcher icon is `mk/android/app/ic_launcher.svg`,
rasterized into `res/mipmap-*/`.

SQLite (side store: persisted folds) is the amalgamation generated from
nixpkgs' `sqlite.src` (`sqliteAmalgamation` in `nix/android.nix`) and built
as a static module (`mk/android/sqlite/Android.mk`).

The APK is `android:debuggable` (debug-signed development build), so
`adb shell run-as io.github.grumbel.xanatoo ls files` inspects its data.

## Data files

`build-apk.sh` packs `examples/`, `FORMAT.md`, `styles/*.xss`, the toolbar
icons (rasterized from `icons/svg` with `rsvg-convert`) and DejaVu
Sans/Serif/Mono into `assets/xanatoo/` with a `MANIFEST` (stamp line, then
one path per line: SDL can read assets but not list them).
`platform_init()` (`src/platform.cpp`) copies them to
`<internal storage>/data` whenever the stamp changed, `chdir`s there and sets
`XANATOO_DATA_DIR` / `XANATOO_ICON_DIR`, so the desktop lookups
(`examples/help.md`, `styles/`, `icons/`) work unchanged. Edits to shipped
examples are overwritten by the next upgrade; other files are kept.

DejaVu comes first in the font search (`data/fonts`), so documents look as on
desktop and glyphs Roboto lacks (▸ ▾ ☐ ☑) render.

## Platform behaviour

- `HOME` / `TMPDIR` → internal storage: view.conf, history, bookmarks.
- `std::cout` / `std::cerr` → logcat, tag `xanatoo`
  (`adb logcat -s xanatoo`).
- Back is trapped (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`) and acts like Esc,
  so it never finishes the activity with unsaved edits.
- On-screen keyboard: opens when a tap places the caret (not in browse
  mode) or an ImGui text field (URL bar) takes focus. The window is not
  fullscreen, so `adjustResize` shrinks it above the keyboard; when the
  window gets shorter the caret is scrolled back into view.
- External links: `SDL_OpenURL`.
- versionCode = last commit time (`self.lastModified`), so dirty test builds
  install over committed ones.

Tested on a Fire 7 (KFAUWI, Fire OS 5 / Android 5.1.1, 600×1024 @160 dpi).

Known: one SIGSEGV inside `SDL_RenderPresent` (SDL 2.30.3 GLES2) while the
surface was resizing for the keyboard under fast input; not reproducible in
a dozen further runs. If it returns, try SDL 2.32.x (Android surface
lifecycle fixes).

## Still off

| Feature | Desktop | Android |
|---------|---------|---------|
| Gemini / Nostr / Reddit / Lemmy | OpenSSL | off (`XANATOO_*=0`) |
| Font lookup | FontConfig | `XANATOO_FONTCONFIG=0`: generic families → `data/fonts`, `/system/fonts` |

The same switches work for desktop CMake builds (`-DXANATOO_FONTCONFIG=OFF
-DXANATOO_SQLITE=OFF -DXANATOO_GEMINI=OFF …`), which is the quick way to
check that the Android source set still compiles.
