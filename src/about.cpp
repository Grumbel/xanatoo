// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/about.hpp"

namespace xanatoo {

std::string about_markdown(const AboutInfo& info) {
  std::string md = "# About xanatoo\n\n";
  md += "Interactive document composition engine, version **" +
        info.version + "**. This page is generated from the running build.\n";
  for (const AboutSection& s : info.sections) {
    md += "\n## " + s.title + "\n\n";
    if (!s.intro.empty()) md += s.intro + "\n\n";
    for (const AboutItem& it : s.items) {
      md += "- **" + it.name + "** — ";
      if (it.on) {
        md += it.what;
        if (!it.link.empty()) md += " — [" + it.link + "](" + it.link + ")";
      } else {
        md += "*not available*";
        if (!it.what.empty()) md += " (" + it.what + ")";
        if (!it.flag.empty()) md += "; needs " + it.flag;
      }
      md += "\n";
    }
  }
  return md;
}

}  // namespace xanatoo
