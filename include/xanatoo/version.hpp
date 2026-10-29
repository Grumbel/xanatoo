// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// The build's version (gamepkgstd VER-3): VERSION, plus .<revCount>+g<rev>
// for Nix dev builds. Never write a version anywhere else.

#include <string>
#include <string_view>

namespace xanatoo {

const char* version_string();

// HTTP User-Agent: "xanatoo/<version> (<purpose>; +<project URL>)".
std::string user_agent(std::string_view purpose);

}  // namespace xanatoo
