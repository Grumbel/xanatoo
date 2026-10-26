// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>

namespace xanatoo {

// User rendering preferences — not part of the document format.
// Affects layout geometry only (line spacing, margins, measure).
struct ViewSettings {
  // Multiplier on font metrics line_height between wrapped lines (was 1.3).
  float line_spacing = 1.30f;
  // Left/right inset of the text column inside a pane (was 32).
  float margin_x = 32.f;
  // Top inset before the first block (was 24).
  float margin_y = 24.f;
  // Cap on content column width in px; 0 = use the full pane (minus margins).
  // When set, the column is centered in the pane.
  float max_content_width = 0.f;
  // Justify wrapped body lines (not last line / headings / code).
  bool justify = false;
  // Document style sheet (styles/<name>.xss); "default" = built in.
  std::string style = "default";

  static constexpr float kLineTight = 1.10f;
  static constexpr float kLineNormal = 1.30f;
  static constexpr float kLineRelaxed = 1.55f;
  static constexpr float kLineLoose = 1.80f;

  static constexpr float kMarginNarrow = 12.f;
  static constexpr float kMarginNormal = 32.f;
  static constexpr float kMarginWide = 56.f;
  static constexpr float kMarginExtra = 80.f;

  // Comfortable measure for body text (~66–75 chars at typical sizes).
  static constexpr float kMeasureComfortable = 720.f;
  static constexpr float kMeasureWide = 960.f;

  void clamp();
};

std::string view_settings_path();
bool load_view_settings(ViewSettings& out);
bool save_view_settings(const ViewSettings& in);

}  // namespace xanatoo
