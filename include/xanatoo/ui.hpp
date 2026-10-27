// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace xanatoo {

// Commands requested by the ImGui menu / toolbar / context menu.
// Applied by the main loop.
enum class UiCommand {
  None = 0,
  // File
  NewDocument,
  OpenDocument,
  OpenUrlBar,  // Ctrl+L — focus URL bar / status fallback
  NavigateUrl,  // toolbar/menu URL field committed (arg = URL or path)
  NavBack,     // Alt+Left session history
  NavForward,  // Alt+Right
  StopLoading, // cancel running page fetches (toolbar Stop, Esc)
  ToggleHistory,   // View → History panel
  AddBookmark,     // bookmark current page URL/path
  RemoveBookmark,  // remove current (or arg) from bookmarks
  ToggleBookmarks, // Bookmarks manager panel
  // Tabs / MDI
  NewTab,          // new empty buffer + tab
  CloseTab,        // close focused buffer (Ctrl+W)
  NextTab,
  PrevTab,
  SelectTab,       // arg = tab index in tab_order
  PopOut,          // focused buffer → floating window
  PopIn,           // top floating window → tab in focused pane
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
  CopyPageUrl,  // current gemini:// or file path
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
  PromoteSection,
  DemoteSection,
  MoveSectionUp,
  MoveSectionDown,
  ParagraphLevel,
  // View
  ToggleSplit,
  ToggleOutline,
  ToggleBacklinks,
  OpenTranscludeOther,  // open include target in the other pane
  ToggleDiff,
  ToggleStructure,
  ToggleBrowse,  // read-only navigation mode
  ToggleNative,  // web page ↔ its native xanatoo form (wikipedia:, hn:, …)
  CollectionView,  // arg: layout / size / sort / "view" for the collection
  ToggleHelp,
  ToggleVirtualKeyboard,  // F3 / View — on-screen keyboard
  VirtualKey,             // arg = UTF-8 key ("a", "\n", "\b", " ")
  OpenInWindow,
  OpenFormatDoc,
  OpenSemanticDoc,
  OpenExample,       // arg = filename under examples/ (or repo root)
  LoadDiffDemo,      // dual-pane diff-v1 | diff-v2 + diff marks on
  ZoomIn,
  ZoomOut,
  // View display (rendering only — not document format)
  ViewLineSpacingTight,
  ViewLineSpacingNormal,
  ViewLineSpacingRelaxed,
  ViewLineSpacingLoose,
  ViewMarginsNarrow,
  ViewMarginsNormal,
  ViewMarginsWide,
  ViewMarginsExtra,
  ViewMeasureFull,          // max_content_width = 0
  ViewMeasureComfortable,   // ~720px
  ViewMeasureWide,          // ~960px
  ToggleSectionFocus,       // outline: body shows one section only
  ToggleJustify,            // view: justify wrapped lines
  ToggleFootnoteOverlay,    // view: notes of on-screen footnote marks
  SelectStyle,              // arg = style sheet name (styles/<name>.xss)
  ReloadStyles,             // rescan styles/ and re-read the active sheet
};



// One entry of the Services menu (front pages of the built-in clients).
struct UiService {
  std::string label;
  std::string target;    // URL / ref, or example file name when `example`
  bool example = false;  // local page from examples/ (OpenExample)
  bool enabled = true;
  std::string tip;       // URL, or why the client is unavailable
  bool separator_before = false;
};

struct UiState {
  bool dirty = false;
  bool has_path = false;
  bool split = false;
  bool outline = false;
  bool section_focus = false;
  bool justify = false;
  bool footnote_overlay = true;
  bool backlinks = false;
  bool diff = false;
  bool can_diff = false;  // two independent documents loaded
  bool structure = false;
  // Display prefs (for menu checks)
  float view_line_spacing = 1.30f;
  float view_margin_x = 32.f;
  float view_max_content_width = 0.f;
  // Style sheets: (name, title) pairs and the active name.
  std::vector<std::pair<std::string, std::string>> styles;
  std::string style;
  bool browse = false;  // read-only navigation
  bool virtual_keyboard = false;
  // Gamepad right-stick software pointer (extra; real mouse stays separate)
  bool stick_cursor = false;
  bool stick_active = false;  // axis moved or A/B held this frame
  float stick_cx = 0.f, stick_cy = 0.f;
  bool stick_lmb = false, stick_rmb = false;
  bool help = false;
  bool selection = false;
  // True when caret is on a ListItem (enables indent/outdent menu items).
  bool on_list_item = false;
  // True when caret is inside a Stretch (enables edit summary).
  bool in_stretch = false;
  bool can_nav_back = false;
  bool can_nav_forward = false;
  // Other view of the focused page ("" = none): the native ref of a web
  // page (wikipedia:Title, hn:item?id=1) or the web URL of a native view.
  std::string alternate_url;
  bool native_view = false;  // focused page is a native ref (alternate = web)
  // Caret / right-click inside a Collection: its current view for the
  // Collection menus (layout 0 grid, 1 list, 2 full; sort 0 doc, 1 name,
  // 2 name reversed; card size in px).
  std::vector<UiService> services;
  bool collection = false;
  int collection_layout = 0;
  int collection_sort = 0;
  float collection_thumb = 160.f;
  bool loading = false;  // a page fetch is running (Reload becomes Stop)
  bool history_open = false;
  bool bookmarks_open = false;
  bool current_bookmarked = false;
  // Session history (oldest → newest) for the History panel.
  std::vector<std::string> history_entries;
  int history_pos = -1;  // current index, or -1
  // Persistent bookmarks for the Bookmarks menu / panel.
  std::vector<std::string> bookmarks;
  // Tab strip (labels left→right); active_tab is index into tab_labels.
  std::vector<std::string> tab_labels;
  int active_tab = 0;
  bool has_floating = false;
  std::string status_left;
  std::string doc_label;
};

struct UiFrameResult {
  UiCommand cmd = UiCommand::None;
  // Pixels reserved at the top of the window for menu + toolbar.
  float top_reserved = 0.f;
  float bottom_reserved = 0.f;  // virtual keyboard, etc.
  // Optional payload (e.g. example filename for OpenExample).
  std::string arg;
};

class Ui {
public:
  Ui() = default;
  ~Ui();

  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  // ui_scale: chrome factor (font, padding, icons, toolbar rows); 1 = 14 px
  // menu font. The app passes --ui-scale or the display density.
  bool init(SDL_Window* window, SDL_Renderer* renderer, float ui_scale = 1.f);
  void shutdown();

  void process_event(const SDL_Event& ev);

  bool want_capture_keyboard() const;
  bool want_capture_mouse() const;
  // True when ImGui chrome (any window from the last frame, or an open popup)
  // owns window point (x, y). Unlike want_capture_mouse() this does not
  // depend on where the mouse was last frame, so touches can be routed.
  bool captures_point(float x, float y) const;

  // After a menu/toolbar command, drop ImGui's active widget so the next
  // keystroke goes to the document instead of the chrome.
  void release_input_capture();

  // Open a top-level menu by name ("File", "Edit", …) for Alt+letter mnemonics.
  void request_menu(const char* name);

  // Focus the toolbar location / URL field (Ctrl+L).
  void focus_url_bar();

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
  bool tool_menu_begin(const char* id, const char* icon_name, const char* tip);
  void tool_menu_end();

  bool ready_ = false;
  float scale_ = 1.f;
  SDL_Renderer* renderer_ = nullptr;
  std::unordered_map<std::string, Icon> icons_;

  bool context_open_ = false;
  float context_x_ = 0.f;
  float context_y_ = 0.f;

  // Pending Alt+letter menu to open on the next begin_frame().
  std::string menu_request_;

  // Interactive location / URL field (toolbar).
  char url_input_[2048] = {};
  std::string url_synced_from_;  // last doc_label pushed into url_input_
  bool focus_url_bar_ = false;   // Ctrl+L / Open URL menu
  bool url_bar_editing_ = false;
  bool wanted_text_input_ = false;  // io.WantTextInput last frame
  // Width of the button group right of the URL field (last frame), so the
  // field leaves exactly that much room.
  float url_right_w_ = 200.f;

  // Tab bar: one-shot SetSelected so keyboard Next/Prev syncs ImGui without
  // fighting click selection every frame (which caused flicker).
  bool tab_force_select_ = false;
  int tab_synced_active_ = -1;
};

}  // namespace xanatoo
