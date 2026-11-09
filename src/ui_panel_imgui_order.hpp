// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Private bridge for native panel stacking and Dear ImGui's internal hit-test
// list. Keep this out of the public ui.hpp: it requires imgui_internal.h.
#include "xanatoo/ui_panel_layers.hpp"

#include "imgui_internal.h"

#include <vector>

namespace xanatoo {

inline void sync_embedded_imgui_order(
    const std::vector<UiPanelLayer>& native_back_to_front) {
  for (auto it = native_back_to_front.rbegin();
       it != native_back_to_front.rend(); ++it) {
    std::vector<ImGuiWindow*> windows;
    for (ImGuiWindow* window : GImGui->Windows)
      if (ui_panel_layer(window->Name) == *it)
        windows.push_back(window);
    // Move the whole root + children as one group, preserving their order.
    for (auto rit = windows.rbegin(); rit != windows.rend(); ++rit)
      ImGui::BringWindowToDisplayBack(*rit);
  }
}

}  // namespace xanatoo
