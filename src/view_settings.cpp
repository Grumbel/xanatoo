// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/view_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace xanatoo {

void ViewSettings::clamp() {
  line_spacing = std::clamp(line_spacing, 1.0f, 2.5f);
  margin_pct = std::clamp(margin_pct, 0.f, 40.f);
  margin_y = std::clamp(margin_y, 0.f, 120.f);
  if (measure_chars < 0) measure_chars = 0;
  if (measure_chars > 0 && measure_chars < 20) measure_chars = 20;
  if (measure_chars > 400) measure_chars = 400;
}

namespace {

// Old view.conf stored pixel values: map them onto the nearest menu step.
float margin_from_px(float px) {
  float best = ViewSettings::kMarginSteps[0];
  for (float s : ViewSettings::kMarginSteps)
    if (std::abs(s - px / 6.4f) < std::abs(best - px / 6.4f)) best = s;
  return best;
}

int measure_from_px(float px) {
  if (px < 1.f) return 0;
  int best = ViewSettings::kMeasureSteps[1];
  for (int s : ViewSettings::kMeasureSteps)
    if (s > 0 && std::abs(s - px / 10.f) < std::abs(best - px / 10.f))
      best = s;
  return best;
}

}  // namespace

std::string view_settings_path() {
  fs::path base;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0]) {
    base = fs::path(xdg) / "xanatoo";
  } else if (const char* home = std::getenv("HOME"); home && home[0]) {
    base = fs::path(home) / ".config" / "xanatoo";
  } else {
    base = fs::temp_directory_path() / "xanatoo-config";
  }
  return (base / "view.conf").string();
}

bool load_view_settings(ViewSettings& out) {
  ViewSettings def;
  out = def;
  std::ifstream in(view_settings_path());
  if (!in) return false;
  std::string line;
  bool have_pct = false, have_chars = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                            s.back() == '\r'))
        s.pop_back();
      size_t i = 0;
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
      if (i) s.erase(0, i);
    };
    trim(key);
    trim(val);
    try {
      if (key == "line_spacing")
        out.line_spacing = std::stof(val);
      else if (key == "margin_pct") {
        out.margin_pct = std::stof(val);
        have_pct = true;
      }
      else if (key == "margin_x" && !have_pct)
        out.margin_pct = margin_from_px(std::stof(val));
      else if (key == "margin_y")
        out.margin_y = std::stof(val);
      else if (key == "measure_chars") {
        out.measure_chars = std::stoi(val);
        have_chars = true;
      }
      else if (key == "max_content_width" && !have_chars)
        out.measure_chars = measure_from_px(std::stof(val));
      else if (key == "justify")
        out.justify = (val == "1" || val == "true" || val == "yes");
      else if (key == "footnotes")
        out.footnote_overlay = (val == "1" || val == "true" || val == "yes");
      else if (key == "hover_preview")
        out.hover_preview = (val == "1" || val == "true" || val == "yes");
      else if (key == "style" && !val.empty())
        out.style = val;
    } catch (...) {
    }
  }
  out.clamp();
  return true;
}

bool save_view_settings(const ViewSettings& in) {
  ViewSettings v = in;
  v.clamp();
  fs::path path = view_settings_path();
  try {
    fs::create_directories(path.parent_path());
  } catch (...) {
    return false;
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << "# xanatoo preferences (not document format)\n";
  out << "line_spacing=" << v.line_spacing << "\n";
  out << "margin_pct=" << v.margin_pct << "\n";
  out << "margin_y=" << v.margin_y << "\n";
  out << "measure_chars=" << v.measure_chars << "\n";
  out << "justify=" << (v.justify ? "1" : "0") << "\n";
  out << "footnotes=" << (v.footnote_overlay ? "1" : "0") << "\n";
  out << "hover_preview=" << (v.hover_preview ? "1" : "0") << "\n";
  out << "style=" << v.style << "\n";
  return static_cast<bool>(out);
}

}  // namespace xanatoo
