// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <string>

namespace glyphtoo {

// Commands requested by the ImGui menu / toolbar. Applied by the main loop.
enum class UiCommand {
  None = 0,
  // File
  Save,
  SaveAs,
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
  std::string status_left;   // optional extra label
  std::string doc_label;     // filename in toolbar
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

  // Feed every SDL event before application handling.
  void process_event(const SDL_Event& ev);

  bool want_capture_keyboard() const;
  bool want_capture_mouse() const;

  // Draw menu + toolbar; returns any command activated this frame.
  UiFrameResult begin_frame(const UiState& state);
  void end_frame(SDL_Renderer* renderer);

  bool ready() const { return ready_; }

private:
  bool ready_ = false;
};

}  // namespace glyphtoo
