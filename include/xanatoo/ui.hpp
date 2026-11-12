// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/catalog_credentials.hpp"

#include "xanatoo/sdl.hpp"
#include "xanatoo/history_menu.hpp"
#include "xanatoo/urlbar_suggestions.hpp"
#include "xanatoo/nostr_store.hpp"
#include "xanatoo/lemmy_directory.hpp"
#include "xanatoo/style.hpp"
#include "xanatoo/subscriptions.hpp"
#include "xanatoo/ui_panel_layers.hpp"
#include <string>
#include <optional>
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
  ToggleSubscriptions, // watched feeds / communities / accounts
  Subscribe, // add a source by address (empty = current page)
  Unsubscribe, // remove subscription by address
  SubscriptionOpen, // open and mark read
  SubscriptionMarkRead, // mark read without opening
  SubscriptionRefresh, // force a fetch now (empty = all)
  SubscriptionInterval, // address + newline + integer minutes
  ToggleAnnotationsPanel, // annotation layer visibility / selection
  ToggleRevisionsPanel,  // native database document revision browser
  ToggleNostrExplorer, // local relay event / pubkey catalogue
  ToggleLemmyExplorer, // collected Lemmy instances, communities, users, posts
  LemmySetTab,         // arg = 0..3
  LemmySetSearch,      // arg = local filter text
  LemmySetInstance,    // arg = known instance host
  LemmyAddInstance,    // arg = https://host or host
  LemmySetSort,        // arg = Hot, New, Active, TopDay, TopWeek
  LemmyToggleInstanceOnly,
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
  OpenLinkInTab,   // arg = href of a link in the focused view: new tab
  DownloadUrl,     // arg = http(s) URL: save to Downloads, never show
  DownloadAction,  // arg = action on App::download_menu (right-clicked
                   // Downloads row): open / open-tab / system / folder /
                   // copy-path / copy-url / cancel / forget
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
  TearTab,         // arg = "id,x,y": move a specific tab to a document window
  PopOut,          // focused buffer → floating window
  PopIn,           // arg = optional buffer id: document window → centre tab
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
  SwapPanes,       // exchange two centre views, including navigation state
  JoinLeftPane,    // collapse split, retaining the left/top view
  JoinRightPane,   // collapse split, retaining the right/bottom view
  ToggleOutline,
  ToggleBacklinks,
  ToggleSitemap,  // site structure panel of the focused web page
  ToggleSearchBar,    // show / hide the search bar (Ctrl+K focuses it)
  FocusSearchBar,     // show the search bar and focus its field
  Search,             // search bar committed (arg = what was typed)
  SelectSearchEngine, // arg = engine id (search.hpp)
  SearchSite,         // location bar button: search bar with engine arg
  TogglePage,     // navigation / side bars of the focused page, as a panel
  SitemapPrev,    // previous / next page in the site map's order
  SitemapNext,
  OpenTranscludeOther,  // open include target in the other pane
  ToggleDiff,
  ToggleStructure,
  ToggleEdit,  // opt-in editing mode; browsing is the default
  ToggleChrome,  // content only: hide menu bar, toolbars, tabs, status line
  ToggleNative,  // web page ↔ its native xanatoo form (wikipedia:, hn:, …)
  ViewSourceRendered, // leave a source projection and show the document
  ViewSourceRaw,      // focused document's raw source (if available)
  ViewSourceSexpr,    // focused document's S-expression representation
  ViewSourceXml,      // focused document's XML representation
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
  ToggleAutoLinks,          // view: plain-text http(s):// URLs become links
  ToggleHoverPreview,       // split view: preview hovered links on the right
  ToggleReaderDefault,      // Preferences: reader mode for new views
  TogglePreferences,        // Edit → Preferences window
  ToggleCredentials,        // Edit → Credentials dialog
  SaveCatalogCredentials,   // arg = six newline-separated private values
  ClearCatalogCredentials,  // remove all saved catalog keys
  SaveRedditCredentials,    // arg = client_id + newline + client_secret
  ClearRedditCredentials,   // remove locally saved Reddit credentials
  SetUiScale,               // Preferences: scale chrome and default text
  SelectStyle,              // arg = style sheet name (styles/<name>.xss)
  ReloadStyles,             // rescan styles/ and re-read the active sheet
  StyleCreate,              // arg = new stem + '\n' + parent stem
  StyleSave,                // arg = stem + '\n' + source
  StyleDelete,              // arg = user-owned stem
  StylePin,                 // arg = domain:host/service:name + '\n' + stem
  StyleUnpin,               // arg = domain:host/service:name
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
  // Native FloatWindows determine both the painted and interactive stacking
  // of embedded ImGui forms. Chrome remains on top of all native windows.
  std::vector<UiPanelLayer> native_panel_order;
  std::optional<UiPanelLayer> pointer_native_top;
  float pointer_x = 0.f, pointer_y = 0.f;
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
  bool lemmy_explorer_open = false;
  bool lemmy_supported = false;
  int lemmy_tab = 0;
  std::string lemmy_search, lemmy_instance, lemmy_sort;
  bool lemmy_instance_only = false;
  int lemmy_explorer_x = 0, lemmy_explorer_y = 0;
  int lemmy_explorer_w = 0, lemmy_explorer_h = 0;
  std::vector<LemmyDirectoryEntry> lemmy_entries;
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
  bool auto_links = true;  // ViewSettings::auto_links
  bool auto_updates = false;
  bool reader_default = false;  // ViewSettings::reader_mode
  bool reader = false;          // focused view hides the page chrome
  bool paged = false;           // focused view shows pages
  bool paged_default = false;   // ViewSettings::paged
  int page_columns = 0;         // ViewSettings::page_columns
  bool has_chrome = false;      // focused page has page chrome to hide
  bool preferences_open = false;
  bool credentials_open = false;
  bool backlinks = false;
  bool sitemap = false;
  bool page_panel = false;
  bool searchbar = false;  // the search bar row is shown
  std::vector<UiSearchEngine> search_engines;
  std::string search_engine;  // id of the engine in use
  std::string site_search;  // engine that searches the focused page's service
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
  bool browse = true;  // read-only navigation (default, or foreign content)
  bool can_edit = false;  // false for foreign/read-only content
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
  std::string context_link;       // right-clicked link target, if any
  std::string context_link_href;  // … as written in the page
  bool context_link_web = false;  // … and it is http(s): can be downloaded
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
  // Address bar suggestions: open tabs, bookmarks, recent visits, newest first.
  std::vector<UrlBarSuggestion> urlbar_entries;
  // Other view of the focused page ("" = none): the native ref of a web
  // page (wikipedia:Title, hn:item?id=1) or the web URL of a native view.
  std::string alternate_url;
  std::string up_url;  // parent page of the focused one ("" at the top)
  bool native_view = false;  // focused page is a native ref (alternate = web)
  std::string source_mode;  // empty = rendered, otherwise source-raw/sexpr/xml
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
  std::vector<std::string> recent_visited;  // newest first, for the History menu
  bool downloads_open = false;
  // The Downloads row of an open right-click menu (empty name: none).
  struct DownloadMenu {
    std::string name, path, url;
    bool active = false;    // still transferring: only Cancel applies
    bool complete = false;  // finished and the file still exists
  } download_menu;
  bool styles_open = false;
  int styles_x = 0, styles_y = 0, styles_w = 0, styles_h = 0;
  std::vector<StyleEntry> style_entries;
  StylePins style_pins;
  uint64_t style_save_generation = 0;
  std::vector<std::string> style_pin_candidates;
  std::string default_style;
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
  bool subscriptions_open = false;
  int subscriptions_x = 0, subscriptions_y = 0;
  int subscriptions_w = 0, subscriptions_h = 0;
  bool current_subscribable = false;
  bool current_subscribed = false;
  std::string subscription_address;
  // Excludes the potentially large per-source recent-ID snapshots; the UI
  // needs only status data, not the change-detection state.
  struct SubscriptionItem {
    std::string address, title, error;
    int interval_minutes = 15;
    int64_t checked_at = 0;
    uint32_t unread = 0;
    bool initialized = false, pending = false;
  };
  std::vector<SubscriptionItem> subscriptions;
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
  bool captures_point(float x, float y,
                      std::optional<UiPanelLayer> native_top = std::nullopt) const;
  // Used by event routing so a hidden embedded editor does not receive a
  // queued mouse press/wheel before ImGui's next NewFrame().
  bool pointer_obscured(float x, float y,
                        std::optional<UiPanelLayer> native_top) const;

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
  // Right-click on the chrome of a document FloatWindow (exact buffer id).
  void open_window_menu(float x, float y, uint32_t buffer_id);
  // Divider badge / right-click: relationship and orientation menu.
  void open_tile_menu(float x, float y);
  // The menu of a bookmark's margin icon (UiState::bookmark_menu).
  void open_bookmark_menu(float x, float y);
  // The menu of a Downloads panel row (UiState::download_menu).
  void open_download_menu(float x, float y);

  UiFrameResult begin_frame(const UiState& state);
  // Finalize ImGui's command lists before native panels are painted. Each
  // embedded form is then drawn immediately after its owning native frame;
  // application chrome and popups are painted last in end_frame().
  void prepare_render();
  void render_embedded_panel(SDL_Renderer* renderer, UiPanelLayer panel);
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
                   bool* right_clicked = nullptr, bool latched = false);
  bool tool_menu_begin(const char* id, const char* icon_name, const char* tip);
  void toolbars(const UiState& state, UiFrameResult& result, UiCommand& cmd);
  void draw_url_suggestions(const UiState& state, UiFrameResult& result,
                            UiCommand& cmd);
  void tool_menu_end();

  bool ready_ = false;
  bool render_prepared_ = false;
  bool layered_panels_ = false;
  float scale_ = 1.f;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  std::unordered_map<std::string, Icon> icons_;

  bool context_open_ = false;
  bool window_menu_open_ = false;
  uint32_t window_menu_buffer_id_ = 0;
  float window_menu_x_ = 0.f;
  float window_menu_y_ = 0.f;
  bool tile_menu_open_ = false;
  bool bookmark_menu_open_ = false;
  bool download_menu_open_ = false;
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
  bool url_suggestions_open_ = false;
  bool url_suggestions_dismissed_ = false;  // Esc until next focus/edit
  bool url_field_active_ = false;
  int url_suggestion_selected_ = -1;  // no suggestion: Enter uses typed URL
  std::string url_suggestion_query_;
  float url_suggestion_x_ = 0.f, url_suggestion_y_ = 0.f;
  float url_suggestion_w_ = 0.f;
  bool select_url_bar_ = false;  // context menu: select current field contents
  bool url_context_selection_ = false;
  int url_context_begin_ = 0;
  int url_context_end_ = 0;
  int url_context_cursor_ = 0;
  bool url_restore_selection_ = false;
  bool url_context_was_open_ = false;
  bool url_context_acted_ = false;
  char search_input_[512] = {};  // search bar text (kept between searches)
  char subscription_input_[1024] = {};
  std::string subscription_remove_confirm_;
  char nostr_local_search_[512] = {};
  char nostr_group_relay_[512] = {};
  char lemmy_search_input_[256] = {};
  char lemmy_instance_input_[256] = {};
  std::string style_editor_sheet_;
  std::string style_editor_loaded_;
  char style_editor_[65536] = {};
  char style_new_name_[81] = {};
  char style_body_font_[128] = {};
  char style_heading_font_[128] = {};
  char style_code_font_[128] = {};
  char style_font_filter_[128] = {};
  char style_pin_input_[256] = {};
  bool style_editor_dirty_ = false;
  uint64_t style_save_seen_ = 0;
  bool style_delete_confirm_ = false;
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
  char catalog_credential_input_[catalog_key_count][512] = {};
  bool catalog_credential_shown_[catalog_key_count] = {};
  bool catalog_credentials_loaded_ = false;
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
