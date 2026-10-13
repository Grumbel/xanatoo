// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Data-driven key bindings: chord → UiCommand. Keys listed here run through
// execute_command, i.e. exactly the same code as the menus and toolbar.
// Context-sensitive keys (arrows, Enter, Esc, …) remain in on_keydown.

#include <array>
#include <optional>

#include "xanatoo/app.hpp"

namespace xanatoo::app {

namespace {

struct Binding {
  SDL_Keycode sym;
  bool shift;  // must match exactly; Ctrl is always required, Alt ignored
  xanatoo::UiCommand cmd;
};

using C = xanatoo::UiCommand;

constexpr std::array kBindings{
    Binding{SDLK_z, false, C::Undo},
    Binding{SDLK_z, true, C::Redo},
    Binding{SDLK_y, false, C::Redo},
    Binding{SDLK_c, false, C::Copy},
    Binding{SDLK_x, false, C::Cut},
    Binding{SDLK_n, false, C::NewDocument},
    Binding{SDLK_s, false, C::Save},
    Binding{SDLK_s, true, C::SaveAs},
    Binding{SDLK_r, false, C::Reload},
    Binding{SDLK_r, true, C::ForceReload},
    Binding{SDLK_d, false, C::DuplicateBlock},
};

}  // namespace

std::optional<xanatoo::UiCommand> keymap_lookup(SDL_Keycode sym, bool ctrl,
                                                bool shift) {
  if (!ctrl) return std::nullopt;
  for (const Binding& b : kBindings)
    if (b.sym == sym && b.shift == shift) return b.cmd;
  return std::nullopt;
}

}  // namespace xanatoo::app
