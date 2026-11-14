// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <optional>
#include <string_view>

namespace xanatoo {

// The embedded ImGui bodies whose title, geometry and stacking belong to native
// FloatWindows. Children use ImGui's "parent/child_ID" window naming scheme.
// Menus, tooltips and popups have their own windows and must remain foreground.
enum class UiPanelLayer { Chrome, Reply, Bookmarks, Subscriptions, Styles, Annotations,
                          NostrExplorer, NostrAccounts, LemmyExplorer, Certificates };

// Bit of `layer` in a set of embedded panels (UiState::shaded_panels).
inline unsigned ui_panel_bit(UiPanelLayer layer) {
  return 1u << static_cast<unsigned>(layer);
}

inline const char* ui_panel_root(UiPanelLayer layer) {
  switch (layer) {
    case UiPanelLayer::Reply: return "##reply_editor";
    case UiPanelLayer::Bookmarks: return "##bookmarks_panel_body";
    case UiPanelLayer::Subscriptions: return "##subscriptions_panel_body";
    case UiPanelLayer::Styles: return "##styles_panel_body";
    case UiPanelLayer::Annotations: return "##annotation_panel_body";
    case UiPanelLayer::NostrExplorer: return "##nostr_explorer_body";
    case UiPanelLayer::NostrAccounts: return "##nostr_accounts_body";
    case UiPanelLayer::LemmyExplorer: return "##lemmy_explorer_body";
    case UiPanelLayer::Certificates: return "##certificates_body";
    case UiPanelLayer::Chrome: return nullptr;
  }
  return nullptr;
}

// A top native frame owns the pointer unless the ImGui hit belongs to that
// frame's own embedded form. Chrome (menus, dialogs, popups) stays above all
// native panels. nullopt means that no native frame covers the pointer.
inline bool ui_panel_obscured(UiPanelLayer hovered,
                              std::optional<UiPanelLayer> native_top) {
  return native_top && hovered != UiPanelLayer::Chrome &&
         hovered != *native_top;
}

inline UiPanelLayer ui_panel_layer(std::string_view owner) {
  constexpr UiPanelLayer panels[] = {
    UiPanelLayer::Reply, UiPanelLayer::Bookmarks, UiPanelLayer::Subscriptions, UiPanelLayer::Styles,
    UiPanelLayer::Annotations,
    UiPanelLayer::NostrExplorer, UiPanelLayer::NostrAccounts,
    UiPanelLayer::LemmyExplorer, UiPanelLayer::Certificates,
  };
  for (const auto layer : panels) {
    const std::string_view prefix(ui_panel_root(layer));
    if (owner == prefix ||
        (owner.size() > prefix.size() &&
         owner.substr(0, prefix.size()) == prefix &&
         owner[prefix.size()] == '/'))
      return layer;
  }
  return UiPanelLayer::Chrome;
}

}  // namespace xanatoo
