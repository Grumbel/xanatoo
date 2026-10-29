// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/version.hpp"

#ifndef XANATOO_VERSION
#error "XANATOO_VERSION must be defined by the build (VERSION file)"
#endif

namespace xanatoo {

const char* version_string() { return XANATOO_VERSION; }

std::string user_agent(std::string_view purpose) {
  return "xanatoo/" + std::string(XANATOO_VERSION) + " (" +
         std::string(purpose) + "; +https://github.com/Grumbel/xanatoo)";
}

}  // namespace xanatoo
