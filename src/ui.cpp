// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/ui.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace glyphtoo {

Ui::~Ui() { shutdown(); }

bool Ui::init(SDL_Window* window, SDL_Renderer* renderer) {
  if (ready_) return true;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;  // no imgui.ini for now

  ImGui::StyleColorsLight();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowBorderSize = 0.f;
  style.FrameBorderSize = 0.f;
  style.WindowPadding = ImVec2(8.f, 4.f);
  style.FramePadding = ImVec2(8.f, 4.f);
  style.ItemSpacing = ImVec2(8.f, 4.f);

  if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) return false;
  if (!ImGui_ImplSDLRenderer2_Init(renderer)) return false;
  ready_ = true;
  return true;
}

void Ui::shutdown() {
  if (!ready_) return;
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ready_ = false;
}

void Ui::process_event(const SDL_Event& ev) {
  if (!ready_) return;
  ImGui_ImplSDL2_ProcessEvent(&ev);
}

bool Ui::want_capture_keyboard() const {
  if (!ready_) return false;
  return ImGui::GetIO().WantCaptureKeyboard;
}

bool Ui::want_capture_mouse() const {
  if (!ready_) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

static bool menu_item_cmd(const char* label, UiCommand c, UiCommand& out,
                          bool enabled = true, const char* shortcut = nullptr) {
  if (ImGui::MenuItem(label, shortcut, false, enabled)) {
    out = c;
    return true;
  }
  return false;
}

UiFrameResult Ui::begin_frame(const UiState& state) {
  UiFrameResult result;
  if (!ready_) return result;

  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  UiCommand cmd = UiCommand::None;

  // ---- Main menu bar ----
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      menu_item_cmd("Save", UiCommand::Save, cmd, true, "Ctrl+S");
      menu_item_cmd("Save As…", UiCommand::SaveAs, cmd, true, "Ctrl+Shift+S");
      ImGui::Separator();
      menu_item_cmd("Reload", UiCommand::Reload, cmd, state.has_path, "Ctrl+R");
      menu_item_cmd("Force Reload", UiCommand::ForceReload, cmd, state.has_path,
                    "Ctrl+Shift+R");
      ImGui::Separator();
      menu_item_cmd("Quit", UiCommand::Quit, cmd, true, "Ctrl+Q");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      menu_item_cmd("Undo", UiCommand::Undo, cmd, true, "Ctrl+Z");
      menu_item_cmd("Redo", UiCommand::Redo, cmd, true, "Ctrl+Y");
      ImGui::Separator();
      menu_item_cmd("Cut", UiCommand::Cut, cmd, state.selection, "Ctrl+X");
      menu_item_cmd("Copy", UiCommand::Copy, cmd, state.selection, "Ctrl+C");
      menu_item_cmd("Paste", UiCommand::Paste, cmd, true, "Ctrl+V");
      menu_item_cmd("Select All", UiCommand::SelectAll, cmd, true, "Ctrl+Shift+A");
      ImGui::Separator();
      menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Format")) {
      menu_item_cmd("Bold", UiCommand::Bold, cmd, state.selection, "Ctrl+B");
      menu_item_cmd("Italic", UiCommand::Italic, cmd, state.selection, "Ctrl+I");
      menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
      menu_item_cmd("Link", UiCommand::Link, cmd, state.selection, "Ctrl+K");
      ImGui::Separator();
      menu_item_cmd("New Paragraph", UiCommand::InsertParagraph, cmd, true,
                    "Ctrl+Enter");
      menu_item_cmd("Horizontal Rule", UiCommand::InsertHr, cmd, true,
                    "Ctrl+Shift+-");
      menu_item_cmd("Insert Image…", UiCommand::InsertImage, cmd, true,
                    "Ctrl+Shift+I");
      ImGui::Separator();
      menu_item_cmd("Toggle Bullet List", UiCommand::ToggleUnorderedList, cmd,
                    true, "Ctrl+Shift+8");
      menu_item_cmd("Toggle Numbered List", UiCommand::ToggleOrderedList, cmd,
                    true, "Ctrl+Shift+7");
      menu_item_cmd("Toggle Block Quote", UiCommand::ToggleBlockQuote, cmd, true,
                    "Ctrl+Shift+Q");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Split Pane", "Ctrl+\\", state.split))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff))
        cmd = UiCommand::ToggleDiff;
      ImGui::Separator();
      menu_item_cmd("Zoom In", UiCommand::ZoomIn, cmd, true, "Ctrl++");
      menu_item_cmd("Zoom Out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
      ImGui::Separator();
      if (ImGui::MenuItem("Keyboard Help", "F1", state.help))
        cmd = UiCommand::ToggleHelp;
      ImGui::EndMenu();
    }
    // Path / dirty indicator on the right of the menu bar
    {
      std::string right = state.doc_label.empty() ? "untitled" : state.doc_label;
      if (state.dirty) right = "*" + right;
      float w = ImGui::CalcTextSize(right.c_str()).x;
      float avail = ImGui::GetWindowWidth();
      if (avail > w + 24.f) {
        ImGui::SameLine(avail - w - 12.f);
        ImGui::TextDisabled("%s", right.c_str());
      }
    }
    result.top_reserved = ImGui::GetFrameHeight();
    ImGui::EndMainMenuBar();
  }

  // ---- Toolbar under the menu ----
  const float toolbar_h = 36.f;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, toolbar_h));
  ImGuiWindowFlags tb_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
  if (ImGui::Begin("##toolbar", nullptr, tb_flags)) {
    auto btn = [&](const char* label, UiCommand c, bool enabled = true) {
      if (!enabled) ImGui::BeginDisabled();
      if (ImGui::SmallButton(label)) cmd = c;
      if (!enabled) ImGui::EndDisabled();
      ImGui::SameLine();
    };
    btn("Save", UiCommand::Save);
    btn("Undo", UiCommand::Undo);
    btn("Redo", UiCommand::Redo);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    btn("B", UiCommand::Bold, state.selection);
    btn("I", UiCommand::Italic, state.selection);
    btn("`code`", UiCommand::Code, state.selection);
    btn("Link", UiCommand::Link, state.selection);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    btn("• List", UiCommand::ToggleUnorderedList);
    btn("1. List", UiCommand::ToggleOrderedList);
    btn("Quote", UiCommand::ToggleBlockQuote);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    btn(state.split ? "Unsplit" : "Split", UiCommand::ToggleSplit);
    btn("Outline", UiCommand::ToggleOutline);
    btn("Find", UiCommand::Find);
    btn("Help", UiCommand::ToggleHelp);
  }
  ImGui::End();
  ImGui::PopStyleVar();

  result.top_reserved += toolbar_h;
  result.cmd = cmd;
  return result;
}

void Ui::end_frame(SDL_Renderer* renderer) {
  if (!ready_) return;
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

}  // namespace glyphtoo
