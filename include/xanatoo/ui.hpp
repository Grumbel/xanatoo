// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <string>
#include <unordered_map>

namespace xanatoo {

// Commands requested by the ImGui menu / toolbar / context menu.
// Applied by the main loop.
enum class UiCommand {
  None = 0,
  // File
  NewDocument,
  OpenDocument,
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
  SelectBlock,
  DuplicateBlock,
  MoveBlockUp,
  MoveBlockDown,
  Find,
  Replace,
  ReplaceAll,
  // Format / inline
  Bold,
  Italic,
  Strike,
  Code,
  Link,
  Cite,
  // Insert blocks
  InsertParagraph,
  InsertHr,
  InsertImage,
  InsertNote,
  InsertCodeBlock,
  InsertTable,
  InsertDefList,
  InsertBiblioEntry,
  InsertColumns,
  InsertTransclude,
  InsertFigure,
  // Structure / lists
  ToggleUnorderedList,
  ToggleOrderedList,
  ToggleTask,
  ToggleBlockQuote,
  IndentList,
  OutdentList,
  WrapStretch,
  EditStretchSummary,
  UnwrapStretch,
  Heading1,  // legacy → Header with depth hint
  Heading2,
  Heading3,
  Heading4,
  Heading5,
  Heading6,
  MakeHeader,
  WrapSection,
  WrapArticle,
  UnwrapSectioning,
  ParagraphLevel,
  // View
  ToggleSplit,
  ToggleOutline,
  ToggleDiff,
  ToggleStructure,
  ToggleBrowse,  // read-only navigation mode
  ToggleHelp,
  OpenInWindow,
  OpenFormatDoc,
  OpenSemanticDoc,
  OpenExample,       // arg = filename under examples/ (or repo root)
  LoadDiffDemo,      // dual-pane diff-v1 | diff-v2 + diff marks on
  ZoomIn,
  ZoomOut,
};

struct UiState {
  bool dirty = false;
  bool has_path = false;
  bool split = false;
  bool outline = false;
  bool diff = false;
  bool can_diff = false;  // two independent documents loaded
  bool structure = false;
  bool browse = false;  // read-only navigation
  bool help = false;
  bool selection = false;
  // True when caret is on a ListItem (enables indent/outdent menu items).
  bool on_list_item = false;
  // True when caret is inside a Stretch (enables edit summary).
  bool in_stretch = false;
  std::string status_left;
  std::string doc_label;
};

struct UiFrameResult {
  UiCommand cmd = UiCommand::None;
  // Pixels reserved at the top of the window for menu + toolbar.
  float top_reserved = 0.f;
  // Optional payload (e.g. example filename for OpenExample).
  std::string arg;
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

  // After a menu/toolbar command, drop ImGui's active widget so the next
  // keystroke goes to the document instead of the chrome.
  void release_input_capture();

  // Open a top-level menu by name ("File", "Edit", …) for Alt+letter mnemonics.
  void request_menu(const char* name);

  // Request a context menu at window coordinates (call from main on right-click).
  void open_context_menu(float x, float y);

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
  bool tool_button(const char* id, const char* icon_name, const char* tip,
                   UiCommand c, UiCommand& out, bool enabled = true);

  bool ready_ = false;
  SDL_Renderer* renderer_ = nullptr;
  std::unordered_map<std::string, Icon> icons_;

  bool context_open_ = false;
  float context_x_ = 0.f;
  float context_y_ = 0.f;

  // Pending Alt+letter menu to open on the next begin_frame().
  std::string menu_request_;
};

}  // namespace xanatoo
