// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/ui_panel_layers.hpp"

#include <cassert>
#include <optional>

int main() {
  using xanatoo::UiPanelLayer;
  using xanatoo::ui_panel_layer;
  assert(ui_panel_layer("##reply_editor") == UiPanelLayer::Reply);
  assert(ui_panel_layer("##bookmarks_panel_body") == UiPanelLayer::Bookmarks);
  assert(ui_panel_layer("##annotation_panel_body") == UiPanelLayer::Annotations);
  assert(ui_panel_layer("##nostr_explorer_body") == UiPanelLayer::NostrExplorer);
  assert(ui_panel_layer("##nostr_accounts_body") == UiPanelLayer::NostrAccounts);
  assert(ui_panel_layer("##nostr_explorer_body/##nostr_results_89ABCDEF") ==
         UiPanelLayer::NostrExplorer);
  assert(ui_panel_layer("##reply_editor/child_00000001") == UiPanelLayer::Reply);
  // A lookalike name must not become part of a native panel's draw pass.
  assert(ui_panel_layer("##reply_editor_extra") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("##nostr_explorer_body_else") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("Preferences") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("##vkeyboard") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("##Menu_00") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("##Tooltip_00") == UiPanelLayer::Chrome);
  assert(ui_panel_layer("") == UiPanelLayer::Chrome);
  assert(std::string_view(xanatoo::ui_panel_root(UiPanelLayer::Reply)) ==
         "##reply_editor");
  assert(xanatoo::ui_panel_root(UiPanelLayer::Chrome) == nullptr);
  using xanatoo::ui_panel_obscured;
  // A native-only panel covers every embedded form underneath it.
  assert(ui_panel_obscured(UiPanelLayer::Reply, UiPanelLayer::Chrome));
  // A different embedded form may be above the one ImGui had hovered.
  assert(ui_panel_obscured(UiPanelLayer::Bookmarks, UiPanelLayer::Reply));
  // The foreground form and global chrome still receive input.
  assert(!ui_panel_obscured(UiPanelLayer::Reply, UiPanelLayer::Reply));
  assert(!ui_panel_obscured(UiPanelLayer::Chrome, UiPanelLayer::Reply));
  assert(!ui_panel_obscured(UiPanelLayer::Reply, std::nullopt));
}
