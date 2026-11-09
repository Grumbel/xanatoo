// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string_view>

namespace xanatoo {

// The five ImGui bodies whose title, geometry and stacking belong to native
// FloatWindows. Children use ImGui's "parent/child_ID" window naming scheme.
// Menus, tooltips and popups have their own windows and must remain foreground.
enum class UiPanelLayer { Chrome, Reply, Bookmarks, Annotations,
                          NostrExplorer, NostrAccounts };

inline UiPanelLayer ui_panel_layer(std::string_view owner) {
  constexpr struct { std::string_view prefix; UiPanelLayer layer; } panels[] = {
    {"##reply_editor", UiPanelLayer::Reply},
    {"##bookmarks_panel_body", UiPanelLayer::Bookmarks},
    {"##annotation_panel_body", UiPanelLayer::Annotations},
    {"##nostr_explorer_body", UiPanelLayer::NostrExplorer},
    {"##nostr_accounts_body", UiPanelLayer::NostrAccounts},
  };
  for (const auto& panel : panels) {
    if (owner == panel.prefix ||
        (owner.size() > panel.prefix.size() &&
         owner.substr(0, panel.prefix.size()) == panel.prefix &&
         owner[panel.prefix.size()] == '/'))
      return panel.layer;
  }
  return UiPanelLayer::Chrome;
}

}  // namespace xanatoo
