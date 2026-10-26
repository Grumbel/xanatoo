// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/view_settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace xanatoo {

void ViewSettings::clamp() {
  line_spacing = std::clamp(line_spacing, 1.0f, 2.5f);
  margin_x = std::clamp(margin_x, 0.f, 200.f);
  margin_y = std::clamp(margin_y, 0.f, 120.f);
  if (max_content_width < 0.f) max_content_width = 0.f;
  if (max_content_width > 0.f && max_content_width < 200.f)
    max_content_width = 200.f;
  if (max_content_width > 4000.f) max_content_width = 4000.f;
}

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
      else if (key == "margin_x")
        out.margin_x = std::stof(val);
      else if (key == "margin_y")
        out.margin_y = std::stof(val);
      else if (key == "max_content_width")
        out.max_content_width = std::stof(val);
      else if (key == "justify")
        out.justify = (val == "1" || val == "true" || val == "yes");
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
  out << "# xanatoo view settings (rendering only; not document format)\n";
  out << "line_spacing=" << v.line_spacing << "\n";
  out << "margin_x=" << v.margin_x << "\n";
  out << "margin_y=" << v.margin_y << "\n";
  out << "max_content_width=" << v.max_content_width << "\n";
  out << "justify=" << (v.justify ? "1" : "0") << "\n";
  out << "style=" << v.style << "\n";
  return static_cast<bool>(out);
}

}  // namespace xanatoo
