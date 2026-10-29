// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>

namespace xanatoo {

// User preferences (view.conf) — not part of the document format. Mostly
// layout geometry (line spacing, margins, measure), plus a few behaviour
// switches set in Edit → Preferences.
struct ViewSettings {
  // Multiplier on font metrics line_height between wrapped lines (was 1.3).
  float line_spacing = 1.30f;
  // Left/right inset of the text column, in percent of the pane width
  // (each side; never less than kMinMarginPx).
  float margin_pct = 5.f;
  // Top inset before the first block (was 24).
  float margin_y = 24.f;
  // Cap on the text column in characters of the body font (follows zoom and
  // the style sheet); 0 = use the full pane (minus margins). When set, the
  // column is centered in the pane.
  int measure_chars = 0;
  // Justify wrapped body lines (not last line / headings / code).
  bool justify = false;
  // Show the notes of on-screen footnote marks at the bottom of each pane.
  bool footnote_overlay = true;
  // Split view: hovering a link in the left pane previews it on the right.
  // Off by default: the right pane changing without a click confuses.
  bool hover_preview = false;
  // Document style sheet (styles/<name>.xss); "default" = built in.
  std::string style = "default";

  static constexpr float kLineTight = 1.10f;
  static constexpr float kLineNormal = 1.30f;
  static constexpr float kLineRelaxed = 1.55f;
  static constexpr float kLineLoose = 1.80f;

  // Menu steps: margin percent per side, measure in characters (0 = full).
  static constexpr float kMarginSteps[] = {2.f, 5.f, 10.f, 15.f, 20.f, 25.f,
                                           30.f};
  static constexpr int kMeasureSteps[] = {0, 60, 72, 90, 120, 160, 200, 300};
  // Floor so text never touches the pane edge / scrollbar (px, pre-DPI).
  static constexpr float kMinMarginPx = 8.f;

  void clamp();
};

std::string view_settings_path();
bool load_view_settings(ViewSettings& out);
bool save_view_settings(const ViewSettings& in);

}  // namespace xanatoo
