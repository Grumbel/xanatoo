// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/sdl.hpp"
#include "xanatoo/history_menu.hpp"
#include "xanatoo/nostr_store.hpp"
#include <string>
#include <vector>
#include <unordered_map>

#include "xanatoo/util.hpp"

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
  NavJump,     // arg = signed number of history steps (Back/Forward context menus)
  GoHome,      // Alt+Home: home page (welcome.md for now)
  GoUp,        // one level up the page hierarchy (UiState::up_url)
  StopLoading, // cancel running page fetches (toolbar Stop, Esc)
  ToggleHistory,   // View → History panel
  ToggleDownloads, // Panels → Downloads
  ToggleStylesPanel, // Panels → Style sheets
  AddBookmark,     // bookmark the selection, else the current page
  BookmarkPage,    // always bookmark the page, ignoring text selection
  BookmarkPosition,  // bookmark the caret position in the page
  RemoveBookmark,  // remove current (or arg) from bookmarks
  ToggleBookmarks, // Bookmarks manager panel
  ToggleAnnotationsPanel, // annotation layer visibility / selection
  ToggleRevisionsPanel,  // native database document revision browser
  ToggleNostrExplorer, // local relay event / pubkey catalogue
  ToggleNostrAccounts, // local watch-only identities, signer management later
  NostrSearchLocal,    // arg = search text
  NostrShowPeople,     // people vs event result tab
  NostrShowEvents,
  NostrShowGroups,
  NostrRefreshLocal,   // reload DB observations
  NostrPrevPage,       // previous 100 locally observed records
  NostrNextPage,
  NostrAddWatchIdentity, // arg = label + newline + npub/hex key
  NostrSelectIdentity, // arg = saved public key
  NewAnnotationLayer,     // new local, independently versioned layer
  SelectAnnotationLayer,  // arg = layer id (local owner only)
  ToggleAnnotationLayer,  // arg = layer id; view-only visibility
  AddAnnotationHighlight,// highlight selected passage in active local layer
  AddAnnotationNote,    // arg = note text; attach to selected passage
  SelectAnnotation,     // arg = annotation id in visible current page
  UpdateAnnotationNote, // arg = replacement body for selected local note
  RemoveAnnotation,     // arg = annotation id in local layer
  RestoreAnnotationLayerVersion, // arg = layer id + newline + revision id
  SelectBookmark,  // arg = address: select its passage in the focused view
  CopyText,        // arg = text for the clipboard (an address)
  OpenReply,       // right-clicked Message: start a mock reply draft
  EditReply,       // arg = Markdown currently in the reply editor
  SendReply,       // mock only: never sends to an external service
  // Tabs / MDI
  NewTab,          // new empty buffer + tab
  CloseTab,        // close focused buffer (Ctrl+W)
  NextTab,
  PrevTab,
  SelectTab,       // arg = tab index in tab_order
  ReorderTabs,     // arg = buffer ids "3,1,2", the new tab_order (dragged)
  TearTab,         // arg = "id,x,y": a tab dragged off the strip → window
  PopOut,          // focused buffer → floating window
  PopIn,           // top floating window → tab in focused pane
  Save,
  Publish,       // snapshot current database-native working revision locally
  EditLocalCopy, // preserve source; switch to editable local database document
  ViewOriginal,  // show immutable imported source snapshot

  SaveAs,
  ExportSexpr,
  ExportXml,
  ExportMarkdown,
  Reload,
  ForceReload,
  RevertToSaved,  // drop the buffer's draft and reload the file
  OpenDrafts,     // "drafts:" page: every stored unsaved draft
  OpenStoredDocuments,  // "documents:" page: database-native working documents
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
  ToggleTrail,
  TilesIndependent,
  TilesTrail,
  TilesCompare,
  TilesColumns,
  TilesRows,
  ToggleOutline,
  ToggleBacklinks,
  ToggleSitemap,  // site structure panel of the focused web page
  ToggleSearchBar,    // show / hide the search bar (Ctrl+K focuses it)
  FocusSearchBar,     // show the search bar and focus its field
  Search,             // search bar committed (arg = what was typed)
  SelectSearchEngine, // arg = engine id (search.hpp)
  TogglePage,     // navigation / side bars of the focused page, as a panel
  SitemapPrev,    // previous / next page in the site map's order
  SitemapNext,
  OpenTranscludeOther,  // open include target in the other pane
  ToggleDiff,
  ToggleStructure,
  ToggleBrowse,  // read-only navigation mode
  ToggleChrome,  // content only: hide menu bar, toolbars, tabs, status line
  ToggleNative,  // web page ↔ its native xanatoo form (wikipedia:, hn:, …)
  ToggleReader,  // focused view: hide / show the page chrome (nav, aside, …)
  TogglePaged,        // focused view: scrolling ↔ ebook style pages (F8)
  TogglePagedDefault, // Preferences: paged reading for new views
  PageColumns,        // arg: "0" (fit the width) … "3" columns per spread
  CollectionView,  // arg: layout / size / sort / "view" for the collection
  ToggleHelp,
  ToggleVirtualKeyboard,  // F3 / View — on-screen keyboard
  VirtualKey,             // arg = UTF-8 key ("a", "\n", "\b", " ")
  OpenInWindow,
  OpenFormatDoc,
  OpenAbout,  // generated about: page (build features, services)
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
  ViewMargins,              // arg = margin percent per side
  ViewMeasure,              // arg = column width in characters, "0" = full
  ToggleSectionFocus,       // outline: body shows one section only
  ToggleJustify,            // view: justify wrapped lines
  ToggleFootnoteOverlay,    // view: notes of on-screen footnote marks
  ToggleBookmarkMarks,      // view: wash over bookmarks inside the page
  ToggleAutoUpdates,         // Android update checks
  ToggleHoverPreview,       // split view: preview hovered links on the right
  ToggleReaderDefault,      // Preferences: reader mode for new views
  TogglePreferences,        // Edit → Preferences window
  SaveRedditCredentials,    // arg = client_id + newline + client_secret
  ClearRedditCredentials,   // remove locally saved Reddit credentials
  SetUiScale,               // Preferences: scale chrome and default text
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

// One engine of the search bar's menu.
struct UiSearchEngine {
  std::string id, name, keyword, badge;
};

struct UiState {
  bool dirty = false;
  bool can_publish = false;
  bool can_make_local_copy = false;
  bool has_path = false;
  bool split = false;
  bool trail = false;
  bool tiles_stacked = false;
  bool outline = false;
  bool section_focus = false;
  bool justify = false;
  bool footnote_overlay = true;
  bool bookmark_marks = true;
  bool annotations_panel_open = false;
  int annotation_x = 0, annotation_y = 0, annotation_w = 0, annotation_h = 0;
  bool revisions_panel_open = false;
  bool can_show_revisions = false;
  bool nostr_explorer_open = false;
  bool nostr_accounts_open = false;
  int nostr_tab = 0;
  std::string nostr_search;
  int nostr_page = 0;
  bool nostr_people_more = false;
  bool nostr_events_more = false;
  bool nostr_groups_more = false;
  int nostr_explorer_x = 0, nostr_explorer_y = 0;
  int nostr_explorer_w = 0, nostr_explorer_h = 0;
  int nostr_accounts_x = 0, nostr_accounts_y = 0;
  int nostr_accounts_w = 0, nostr_accounts_h = 0;
  NostrCacheStats nostr_stats;
  std::vector<NostrCachedPerson> nostr_people;
  std::vector<NostrCachedEvent> nostr_events;
  std::vector<NostrCachedGroup> nostr_groups;
  std::vector<NostrLocalIdentity> nostr_identities;
  std::string nostr_selected_identity;
  struct AnnotationLayerItem {
    std::string id, name, owner;
    bool visible = true;
    bool active = false;
    size_t count = 0;
  };
  std::vector<AnnotationLayerItem> annotation_layers;
  struct AnnotationItem {
    std::string id, layer_id, owner, kind, body;
    bool resolved = false;
    bool selected = false;
  };
  std::vector<AnnotationItem> annotations;
  struct AnnotationHistoryItem {
    std::string id;
    int64_t time = 0;
    size_t count = 0;
  };
  std::string annotation_history_layer;
  std::vector<AnnotationHistoryItem> annotation_history;
  uint64_t annotation_note_created_gen = 0;
  bool hover_preview = false;
  bool auto_updates = false;
  bool reader_default = false;  // ViewSettings::reader_mode
  bool reader = false;          // focused view hides the page chrome
  bool paged = false;           // focused view shows pages
  bool paged_default = false;   // ViewSettings::paged
  int page_columns = 0;         // ViewSettings::page_columns
  bool has_chrome = false;      // focused page has page chrome to hide
  bool preferences_open = false;
  bool backlinks = false;
  bool sitemap = false;
  bool page_panel = false;
  bool searchbar = false;  // the search bar row is shown
  std::vector<UiSearchEngine> search_engines;
  std::string search_engine;  // id of the engine in use
  bool has_nav = false;  // focused page has navigation for the panel
  bool has_site = false; // focused address has a site to search for a sitemap
  bool diff = false;
  bool can_diff = false;  // two independent documents loaded
  bool structure = false;
  // Display prefs (for menu checks)
  float view_line_spacing = 1.30f;
  float view_margin_pct = 5.f;
  int view_measure_chars = 0;
  float ui_scale = 1.f;  // preference multiplier on the platform default
  // Style sheets: (name, title) pairs and the active name.
  std::vector<std::pair<std::string, std::string>> styles;
  std::string style;
  bool browse = false;  // read-only navigation
  bool chrome_hidden = false;  // content only: no menu bar / toolbars / tabs
  bool virtual_keyboard = false;
  // Gamepad right-stick software pointer (extra; real mouse stays separate)
  bool stick_cursor = false;
  bool stick_active = false;  // axis moved or A/B held this frame
  float stick_cx = 0.f, stick_cy = 0.f;
  bool stick_lmb = false, stick_rmb = false;
  bool help = false;
  bool selection = false;
  std::string context_comment_native;  // passage address for a right-clicked HN comment
  std::string context_comment_web;     // external HN per-comment permalink
  bool context_can_reply = false;
  bool reply_open = false;
  uint64_t reply_generation = 0;
  std::string reply_author, reply_page, reply_label, reply_text;
  int reply_x = 0, reply_y = 0, reply_w = 0, reply_h = 0;
  // True when caret is on a ListItem (enables indent/outdent menu items).
  bool on_list_item = false;
  // True when caret is inside a Stretch (enables edit summary).
  bool in_stretch = false;
  bool can_nav_back = false;
  bool can_nav_forward = false;
  std::vector<HistoryMenuItem> back_history;
  std::vector<HistoryMenuItem> forward_history;
  // Other view of the focused page ("" = none): the native ref of a web
  // page (wikipedia:Title, hn:item?id=1) or the web URL of a native view.
  std::string alternate_url;
  std::string up_url;  // parent page of the focused one ("" at the top)
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
  bool downloads_open = false;
  bool styles_open = false;
  bool bookmarks_open = false;
  int bookmarks_x = 0, bookmarks_y = 0;
  int bookmarks_w = 0, bookmarks_h = 0;
  bool current_bookmarked = false;
  bool page_bookmarked = false;
  int page_bookmarks = 0;  // bookmarks of passages inside the focused page
  // The bookmark a margin-icon menu is for (empty address: none).
  struct BookmarkMenu {
    std::string address, label, cut;  // cut: the passage as a cut address
  } bookmark_menu;
  // Persistent bookmarks for the Bookmarks menu / panel.
  std::vector<Bookmark> bookmarks;
  SDL_Texture* location_icon = nullptr;
  std::vector<SDL_Texture*> bookmark_icons;
  // Tab strip (labels left→right); active_tab is index into tab_labels.
  std::vector<std::string> tab_labels;
  std::vector<uint32_t> tab_ids;  // buffer id of each tab (stable ImGui ids)
  std::vector<SDL_Texture*> tab_icons;
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
  // A tab is being dragged away from the strip (the app shows where a drop
  // would dock it).
  bool tab_drag_out = false;
};

class Ui {
public:
  Ui() = default;
  ~Ui();

  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  // ui_scale: chrome factor (font, padding, icons, toolbar rows); 1 = 14 px
  // menu font and 32 px toolbar icons before effective display/user scaling.
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
  // Focus the search bar field (once the bar is shown).
  void focus_search();

  // Request a context menu at window coordinates (call from main on right-click).
  void open_context_menu(float x, float y);
  // Divider badge / right-click: relationship and orientation menu.
  void open_tile_menu(float x, float y);
  // The menu of a bookmark's margin icon (UiState::bookmark_menu).
  void open_bookmark_menu(float x, float y);

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
                   UiCommand c, UiCommand& out, bool enabled = true,
                   bool* right_clicked = nullptr);
  bool tool_menu_begin(const char* id, const char* icon_name, const char* tip);
  void toolbars(const UiState& state, UiFrameResult& result, UiCommand& cmd);
  void tool_menu_end();

  bool ready_ = false;
  float scale_ = 1.f;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  std::unordered_map<std::string, Icon> icons_;

  bool context_open_ = false;
  bool tile_menu_open_ = false;
  bool bookmark_menu_open_ = false;
  float context_x_ = 0.f;
  float context_y_ = 0.f;
  float tile_menu_x_ = 0.f;
  float tile_menu_y_ = 0.f;

  // Pending Alt+letter menu to open on the next begin_frame().
  std::string menu_request_;

  // Interactive location / URL field (toolbar).
  char url_input_[2048] = {};
  std::string url_synced_from_;  // last doc_label pushed into url_input_
  bool focus_url_bar_ = false;   // Ctrl+L / Open URL menu
  bool select_url_bar_ = false;  // context menu: select current field contents
  bool url_context_selection_ = false;
  int url_context_begin_ = 0;
  int url_context_end_ = 0;
  int url_context_cursor_ = 0;
  char search_input_[512] = {};  // search bar text (kept between searches)
  char nostr_local_search_[512] = {};
  char nostr_group_relay_[512] = {};
  char nostr_identity_label_[160] = {};
  char nostr_identity_pubkey_[256] = {};
  char reply_input_[16384] = {}; // Markdown draft, not sent to any server
  uint64_t reply_generation_seen_ = 0;
  char annotation_new_note_[16384] = {};
  char annotation_edit_note_[16384] = {};
  std::string annotation_edit_synced_id_;
  std::string annotation_edit_synced_body_;
  double annotation_note_last_edit_ = 0.0;
  uint64_t annotation_note_created_seen_ = 0;
  struct ReplySelection { int begin = 0, end = 0; } reply_selection_;
  bool reply_focus_ = false;
  bool reply_restore_selection_ = false;
  char reddit_client_id_[256] = {};
  char reddit_client_secret_[256] = {};
  bool reddit_credentials_loaded_ = false;
  bool reddit_secret_visible_ = false;
  bool focus_search_ = false;    // Ctrl+K / View → Search Bar
  bool url_bar_editing_ = false;
  bool wanted_text_input_ = false;  // io.WantTextInput last frame
  // Width of the button group right of the URL field (last frame), so the
  // field leaves exactly that much room.
  float url_right_w_ = 200.f;

  // Tab bar: one-shot SetSelected so keyboard Next/Prev syncs ImGui without
  // fighting click selection every frame (which caused flicker).
  bool tab_force_select_ = false;
  int tab_synced_active_ = -1;
  // Tab being dragged off the strip (buffer id, 0 = none) until release.
  uint32_t tab_tear_id_ = 0;
};

}  // namespace xanatoo
