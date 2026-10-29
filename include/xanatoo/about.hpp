// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// The "about:" page: what this build of xanatoo can do. app_about.cpp
// gathers the items (build switches, runtime library versions); this turns
// them into Markdown, one Section per group so the outline is the overview.

#include <string>
#include <vector>

namespace xanatoo {

struct AboutItem {
  std::string name;
  bool on = true;
  std::string what;  // what it gives the user (Markdown inline)
  std::string link;  // example target, shown while on
  std::string flag;  // build switch that turns it on, shown while off
};

struct AboutSection {
  std::string title;
  std::string intro;  // paragraph before the items (Markdown)
  std::vector<AboutItem> items;
};

struct AboutInfo {
  std::string version;
  std::vector<AboutSection> sections;
};

std::string about_markdown(const AboutInfo& info);

}  // namespace xanatoo
