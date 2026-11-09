// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/ui_panel_layers.hpp"
#include "ui_panel_imgui_order.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cassert>
#include <string_view>

// Verify the actual vendored ImGui's draw-list names, including its nested
// BeginChild scheme. The classifier's pure tests alone cannot detect ImGui
// changing how it names or groups its render lists.
int main() {
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // test must not write an imgui.ini in the build tree
  io.DisplaySize = ImVec2(800.f, 600.f);
  io.DeltaTime = 1.f / 60.f;
  unsigned char* pixels = nullptr;
  int width = 0, height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  for (int frame = 0; frame < 2; ++frame) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.f, 40.f));
    ImGui::SetNextWindowSize(ImVec2(220.f, 210.f));
    if (ImGui::Begin("##nostr_explorer_body")) {
      ImGui::TextUnformatted("Parent");
      if (ImGui::BeginChild("##nostr_results", ImVec2(140.f, 100.f)))
        ImGui::TextUnformatted("Child");
      ImGui::EndChild();
    }
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(470.f, 240.f));
    ImGui::SetNextWindowSize(ImVec2(220.f, 210.f));
    if (ImGui::Begin("##lemmy_explorer_body")) {
      ImGui::TextUnformatted("Lemmy communities");
      if (ImGui::BeginChild("##lemmy_results", ImVec2(150.f, 100.f)))
        ImGui::TextUnformatted("Discovered servers");
      ImGui::EndChild();
    }
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(320.f, 50.f));
    ImGui::SetNextWindowSize(ImVec2(200.f, 200.f));
    if (ImGui::Begin("Preferences"))
      ImGui::TextUnformatted("Settings");
    ImGui::End();
    ImGui::Render();
  }

  bool parent = false, child = false, chrome = false;
  bool lemmy_parent = false, lemmy_child = false;
  const ImDrawData* data = ImGui::GetDrawData();
  assert(data);
  for (int i = 0; i < data->CmdListsCount; ++i) {
    const char* owner = data->CmdLists[i]->_OwnerName;
    if (!owner) continue;
    const std::string_view name(owner);
    const auto layer = xanatoo::ui_panel_layer(name);
    if (name == "##nostr_explorer_body")
      parent = layer == xanatoo::UiPanelLayer::NostrExplorer;
    if (name.find("##nostr_explorer_body/##nostr_results_") == 0)
      child = layer == xanatoo::UiPanelLayer::NostrExplorer;
    if (name == "##lemmy_explorer_body")
      lemmy_parent = layer == xanatoo::UiPanelLayer::LemmyExplorer;
    if (name.find("##lemmy_explorer_body/##lemmy_results_") == 0)
      lemmy_child = layer == xanatoo::UiPanelLayer::LemmyExplorer;
    if (name == "Preferences")
      chrome = layer == xanatoo::UiPanelLayer::Chrome;
  }
  assert(parent && child && lemmy_parent && lemmy_child && chrome);

  // Native z-order can change without the two embedded ImGui windows being
  // focused. Both root and scrollable child must follow the same ordering.
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(50.f, 60.f));
    ImGui::SetNextWindowSize(ImVec2(270.f, 230.f));
    if (ImGui::Begin("##reply_editor")) {
      if (ImGui::BeginChild("##reply_child", ImVec2(240.f, 170.f)))
        ImGui::TextUnformatted("Reply text");
      ImGui::EndChild();
    }
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(70.f, 80.f));
    ImGui::SetNextWindowSize(ImVec2(270.f, 230.f));
    if (ImGui::Begin("##bookmarks_panel_body")) {
      if (ImGui::BeginChild("##bookmarks_child", ImVec2(240.f, 170.f)))
        ImGui::TextUnformatted("Bookmarks list");
      ImGui::EndChild();
    }
    ImGui::End();
    xanatoo::sync_embedded_imgui_order({xanatoo::UiPanelLayer::Reply,
                                        xanatoo::UiPanelLayer::Bookmarks});
    ImGui::Render();
  }
  auto hovered_layer = [] {
    ImGuiWindow* hovered = nullptr;
    ImGui::FindHoveredWindowEx(ImVec2(130.f, 140.f), true, &hovered, nullptr);
    assert(hovered);
    return xanatoo::ui_panel_layer(hovered->Name);
  };
  assert(hovered_layer() == xanatoo::UiPanelLayer::Bookmarks);
  xanatoo::sync_embedded_imgui_order({xanatoo::UiPanelLayer::Bookmarks,
                                      xanatoo::UiPanelLayer::Reply});
  assert(hovered_layer() == xanatoo::UiPanelLayer::Reply);
  ImGui::DestroyContext();
}
