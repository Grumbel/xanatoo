// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Platform glue that has no desktop equivalent. Everything is a no-op off
// Android; see docs/ANDROID.md.

namespace xanatoo {

// Before SDL_Init: hints (trap the Back key). After: on Android point HOME /
// TMPDIR at internal storage, unpack the APK's data files (examples, help,
// style sheets, icons) into <internal>/data when their stamp changed, chdir
// there and set XANATOO_DATA_DIR / XANATOO_ICON_DIR.
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

}  // namespace xanatoo
