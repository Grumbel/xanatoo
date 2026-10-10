// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <string>
#include <unordered_map>

namespace glyphtoo {

// Commands requested by the ImGui menu / toolbar. Applied by the main loop.
enum class UiCommand {
  None = 0,
  // File
  Save,
  SaveAs,
  ExportSexpr,
  ExportMarkdown,
  Reload,
  ForceReload,
  Quit,
  // Edit
  Undo,
  Redo,
  Cut,
  Copy,
  Paste,
  SelectAll,
  Find,
  Replace,
  // Format
  Bold,
  Italic,
  Code,
  Link,
  InsertParagraph,
  InsertHr,
  InsertImage,
  InsertNote,
  InsertCodeBlock,
  InsertTable,
  InsertDefList,
  ToggleUnorderedList,
  ToggleOrderedList,
  ToggleBlockQuote,
  // View
  ToggleSplit,
  ToggleOutline,
  ToggleDiff,
  ToggleHelp,
  ZoomIn,
  ZoomOut,
};

struct UiState {
  bool dirty = false;
  bool has_path = false;
  bool split = false;
  bool outline = false;
  bool diff = false;
  bool help = false;
  bool selection = false;
  std::string status_left;
  std::string doc_label;
};

struct UiFrameResult {
  UiCommand cmd = UiCommand::None;
  // Pixels reserved at the top of the window for menu + toolbar.
  float top_reserved = 0.f;
};

class Ui {
public:
  Ui() = default;
  ~Ui();

  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  bool init(SDL_Window* window, SDL_Renderer* renderer);
  void shutdown();

  void process_event(const SDL_Event& ev);

  bool want_capture_keyboard() const;
  bool want_capture_mouse() const;

  UiFrameResult begin_frame(const UiState& state);
  void end_frame(SDL_Renderer* renderer);

  bool ready() const { return ready_; }

private:
  struct Icon {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
  };

  void apply_classic_theme();
  void load_icons(SDL_Renderer* renderer);
  const Icon* icon(const char* name) const;
  // Toolbar button: icon (if loaded) + optional label; returns true if clicked.
  bool tool_button(const char* id, const char* icon_name, const char* tip,
                   UiCommand c, UiCommand& out, bool enabled = true);

  bool ready_ = false;
  SDL_Renderer* renderer_ = nullptr;
  std::unordered_map<std::string, Icon> icons_;
};

}  // namespace glyphtoo
