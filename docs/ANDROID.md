# Android port

```sh
nix build .#xanatoo-android          # result/xanatoo-0.1.0.apk
nix run .#install-android-xanatoo    # adb install -r
```

Same pipeline as Kurvenrausch / Pingus (`nix/android.nix`, `mk/android/`):

1. `sdl3-android`: SDL3 from nixpkgs built with `ndk-build` for every ABI,
   plus SDL's Java side (`SDLActivity`) compiled to `classes.dex`.
2. `xanatoo-android`: `src/*.cpp` (minus `https.cpp`), vendored ImGui and a
   static FreeType module (nixpkgs' `freetype.src`, `mk/android/freetype/
   Android.mk`) into `libmain.so`; `aapt` + `zipalign` + `apksigner` with
   the debug keystore in `mk/android/keystore/`.

Targets Android 5.1+ (`minSdkVersion` 22), ABIs `armeabi-v7a arm64-v8a
x86_64`, NDK 29. The app icon `data/icons/xanatoo.svg` is rasterized into
legacy `res/mipmap-*/` icons by `tools/make_icons.py` (Android 5 through 7).
Android 8+ uses an adaptive icon with separate gradient background and
foreground layers; the foreground stays within Android's 66 dp safe zone so
launcher masks do not shrink the mark into a padded square. The adaptive icon
also includes a monochrome layer for themed icons on Android 13+ launchers.

SQLite (side store: persisted folds) is the amalgamation generated from
nixpkgs' `sqlite.src` (`sqliteAmalgamation` in `nix/android.nix`) and built
as a static module (`mk/android/sqlite/Android.mk`).

The APK is `android:debuggable` (debug-signed development build), so
`adb shell run-as org.grumbel.xanatoo ls files` inspects its data.

## Data files

`build-apk.sh` packs `examples/`, `FORMAT.md`, `styles/*.xss`, the toolbar
icons (rasterized from `icons/svg` with `rsvg-convert`) and DejaVu
Sans/Serif/Mono into `assets/xanatoo/` with a `MANIFEST` (stamp line, then
one path per line: SDL can read assets but not list them).
`platform_init()` (`src/platform.cpp`) copies them to
`<internal storage>/assets` whenever the stamp changed and makes that the
data directory (`paths.hpp`), so the desktop lookups (`examples/help.md`,
`styles/`, `icons/`) work unchanged. Edits to shipped examples are
overwritten by the next upgrade; other files are kept.

DejaVu comes first in the font search (`<data dir>/fonts`), so documents look as on
desktop and glyphs Roboto lacks (▸ ▾ ☐ ☑) render.

## Platform behaviour

- User directories (gamepkgstd DIR-5): `<internal>/config` (view.conf),
  `<internal>/state` (drafts, bookmarks, history), `<internal>/data` (user
  style sheets, working directory for relative saves), cache in
  `Context.getCacheDir()`. `HOME` / `TMPDIR` also point at internal storage.
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

Historical SDL2 issue: one SIGSEGV inside `SDL_RenderPresent` (SDL 2.30.3
GLES2) while the surface resized for the keyboard under fast input. It did
not recur in a dozen runs. The SDL3 build needs a fresh device smoke test.

## Network (Mbed TLS)

Gemini, Nostr (WSS), Reddit and Lemmy are on, over `TlsStream`
(`include/xanatoo/tls.hpp`) with the Mbed TLS backend
(`src/tls_mbedtls.cpp`, nixpkgs' `mbedtls.src` as a static module,
`mk/android/mbedtls/Android.mk`); the APK requests `INTERNET`. Desktop keeps
OpenSSL by default and can use the same backend with `-DXANATOO_TLS=mbedtls`.

Entropy: mbedTLS's platform source calls `getrandom()` blocking, which on
Fire OS 5's kernel stalled ~60 s (servers closed the connection before the
ClientHello). The Android module builds with `MBEDTLS_NO_PLATFORM_ENTROPY` +
`MBEDTLS_ENTROPY_HARDWARE_ALT`; `mbedtls_hardware_poll()` reads
`/dev/urandom`.

HTTPS and Nostr WSS now verify the certificate chain and requested hostname;
the Mbed TLS backend loads Android CA files from
`/apex/com.android.conscrypt/cacerts` or `/system/etc/security/cacerts`
(and desktop/Nix CA bundle paths). If the system CA store cannot be read,
the connection fails instead of proceeding unverified. This needs device
testing on Fire OS 5 and newer Android releases. Gemini still accepts
self-signed certificates without TOFU/pinning (see `docs/TLS.md`).

## Still off

| Feature | Desktop | Android |
|---------|---------|---------|
| Font lookup | FontConfig | `XANATOO_FONTCONFIG=0`: generic families → `<data dir>/fonts`, `/system/fonts` |

The same switch works for desktop CMake builds (`-DXANATOO_FONTCONFIG=OFF
-DXANATOO_TLS=mbedtls`), which is the quick way to check that the Android
source set still compiles.

## Display density

`app.ui_scale` = (`--ui-scale`, else `platform_default_ui_scale()`) times the
saved UI scale preference: Android dpi / 160 in quarter steps (at least 1),
R36S 0.75, desktop 1. It scales toolbar icons, menu/chrome text, the canvas
chrome metrics (`set_chrome_scale`: status bar, title bars, close box, panel
padding, scrollbars, UI font), panel default sizes, the default document text
size, and px lengths in layout (style sheet px, the view margin floor:
`LayoutOptions::px_scale`; em lengths already follow the font). Test on
desktop with `--ui-scale 2`. The Fire 7 is 160 dpi, so it runs at 1.

Toolbar icons are rendered at 32 px at the platform default from 64 px PNGs.
The Edit → Preferences UI scale setting multiplies toolbar, menu, chrome and
default document text sizes; Android's display-density scale still applies
underneath it. The 640×480 R36S package starts at 75% of the desktop default.

## Next

- Test system-CA validation on actual Android versions; implement Gemini TOFU.
- Per-glyph font fallback (Android system fonts for scripts DejaVu lacks).

## Updates

Automatic update checks are off by default; enable them in Edit → Preferences.
When enabled, the app checks the website for a newer Android package at most
once a day after a successful check. An Android notification links to the
download page; the user downloads the APK and confirms installation through
Android. Android 13 and later ask for notification permission when an update
is found. Failed checks are retried on the next launch. The website publishes
`downloads/android-update.json` from the same source revision as its APK.
The installed and downloaded APKs must have matching signing certificates.
