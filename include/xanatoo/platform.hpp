// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Platform glue that has no desktop equivalent. Everything is a no-op off
// Android; see docs/ANDROID.md.

namespace xanatoo {

// Before SDL_Init: hints (trap the Back key). After: on Android set the
// user directories (paths.hpp) to internal storage, point HOME / TMPDIR
// there, and unpack the APK's data files (examples, help, style sheets,
// icons) into <internal>/assets, the data directory, when their stamp
// changed.
void platform_pre_init();
void platform_init();

// The document took a tap / an ImGui text field gained focus: bring up the
// on-screen keyboard (Android); desktop keeps text input on permanently.
void platform_request_keyboard();
// Text entry finished (URL committed): hide the on-screen keyboard (Android).
void platform_release_keyboard();

// Desktop starts text input once at launch; Android waits for a tap so the
// on-screen keyboard does not cover the page on start.
bool platform_text_input_at_start();

// Chrome / default zoom factor when --ui-scale is not given: Android uses
// the display density (dpi / 160, quarter steps, at least 1); desktop 1.
float platform_default_ui_scale();

}  // namespace xanatoo
