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

## First port: what is off

| Feature | Desktop | Android now |
|---------|---------|-------------|
| Gemini / Nostr / Reddit / Lemmy | OpenSSL | off (`XANATOO_*=0`) |
| Font lookup | FontConfig | `XANATOO_FONTCONFIG=0`: generic families → `/system/fonts` (Roboto, Noto Serif, Droid Sans Mono) |
| Fold persistence | SQLite | `XANATOO_SQLITE=0`: per session |
| Data (examples, help, icons, style sheets) | share/xanatoo | not packaged: built-in help, text toolbar buttons, Default style only |
| External links | xdg-open | `SDL_OpenURL` |

`main()` points `HOME` and `TMPDIR` at the app's internal storage, so
`view.conf`, history and bookmarks land in the private app directory.

The same switches work for desktop CMake builds (`-DXANATOO_FONTCONFIG=OFF
-DXANATOO_SQLITE=OFF -DXANATOO_GEMINI=OFF …`), which is the quick way to
check that the Android source set still compiles.

## Next

- Package `examples/`, `styles/` and the toolbar icons as APK assets and
  copy them to internal storage on first start (code uses `std::filesystem`).
- Scale chrome (ImGui + `ui_font`) by display density; 14 px is tiny on
  phones.
- Bring back SQLite (amalgamation as an ndk-build module) and OpenSSL.
- Soft keyboard: `SDL_StartTextInput` is called once at start; show/hide
  it with the caret.
