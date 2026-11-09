// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/ui_panel_layers.hpp"

#include <cassert>

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
}
