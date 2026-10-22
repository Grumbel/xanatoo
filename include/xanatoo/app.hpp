// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "xanatoo/cli.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/minibuffer.hpp"
#include "xanatoo/render.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/types.hpp"
#include "xanatoo/ui.hpp"
#include "xanatoo/view_settings.hpp"

// Application world: plain data (App + the records below) and free-function
// "systems" that read/transform it. No behaviour lives on the data types.
namespace xanatoo::app {

constexpr int kSbW = 14;
constexpr int kSbArrow = 14;
constexpr int kSbLayoutGutter = kSbW + 4;
constexpr float kBlockGripW = 18.f;
constexpr int soft_snap_px = 16;
constexpr int soft_dock_zone = 36;
constexpr int status_bar_h = 28;
constexpr int kMaxNavHistory = 100;
constexpr int help_title_h = 28;
constexpr int help_pad = 12;
constexpr int help_edge = 6;

// Stable identity of a document buffer; never reused, 0 = none.
using BufferId = uint32_t;
constexpr BufferId kNoBuffer = 0;

struct DocBuffer {
  BufferId id = kNoBuffer;
  xanatoo::Document doc;
  std::string path;
  bool dirty = false;
  xanatoo::History history;
};

struct Pane {
  BufferId buf = kNoBuffer;
  xanatoo::Selection sel{};
  float scroll_y = 0.f;
  float preferred_x = 0.f;
  xanatoo::LayoutResult layout{};
  int x = 0;
  int w = 0;
  // Prior block heights for viewport virtualization (node → height).
  std::unordered_map<xanatoo::NodeId, float> height_cache;
  int last_layout_pane_w = 0;  // drop cache when this pane's width changes
  // Content-Y band last materialised (including overscan).
  float materialised_top = 0.f;
  float materialised_bottom = 0.f;
};

enum class SoftDock { None = 0, Left, Right, Top, Bottom, Max };

enum class WindowRole : uint8_t { Help, Outline, Backlinks, Document };

// One floating window: help / outline / backlinks panels and document
// windows share geometry + interaction state; role picks the content.
struct FloatWindow {
  WindowRole role = WindowRole::Document;
  // Document role only:
  BufferId buf = kNoBuffer;
  std::string title;  // display override; empty → derive from path
  xanatoo::Selection sel{};
  xanatoo::LayoutResult layout;
  bool needs_layout = true;
  bool maximized = false;
  SoftDock dock = SoftDock::None;
  int restore_x = 120, restore_y = 70, restore_w = 540, restore_h = 440;
  Uint32 last_title_click = 0;
  // Geometry + interaction (all roles):
  int x = 120, y = 70, w = 540, h = 440;
  bool dragging = false;
  int drag_dx = 0, drag_dy = 0;
  int resize = 0;  // bit 1=right, 2=bottom
  int resize_start_x = 0, resize_start_y = 0;
  int resize_start_w = 0, resize_start_h = 0;
  float scroll = 0.f;
  bool scroll_dragging = false;
};

inline FloatWindow make_float_panel(WindowRole role, int x, int y, int w,
                                    int h) {
  FloatWindow f;
  f.role = role;
  f.x = x;
  f.y = y;
  f.w = w;
  f.h = h;
  return f;
}

// What the pointer is currently over (rebuilt on every mouse move).
struct Hover {
  std::string href;
  std::string rel;
  bool is_cite = false;
  std::string preview;
  xanatoo::NodeId link_node = xanatoo::INVALID_NODE;
  size_t link_span = 0;
  int link_pane = -1;
  xanatoo::NodeId stretch = xanatoo::INVALID_NODE;
  int stretch_pane = -1;
  xanatoo::NodeId image = xanatoo::INVALID_NODE;
  int image_pane = -1;
  bool image_handle = false;
  bool col_gap = false;
  int col_gap_pane = -1;
  xanatoo::NodeId task = xanatoo::INVALID_NODE;
  int task_pane = -1;
  xanatoo::NodeId grip = xanatoo::INVALID_NODE;
  int grip_pane = -1;
  xanatoo::NodeId transclude = xanatoo::INVALID_NODE;
  int transclude_pane = -1;
  std::string transclude_src;
};

// Touchscreen gestures (app_touch.cpp). Fingers on the document surface
// become taps (left click), pans with momentum, long-press selection and
// two-finger pinch zoom; touches on ImGui chrome are left to ImGui.
enum class TouchMode : uint8_t {
  Idle,     // no finger down
  Pending,  // one finger down: not yet a tap, pan or long press
  Pan,      // dragging scrolls the target
  Select,   // after a long press: dragging extends the selection
  Direct,   // scrollbar / float frame / split gutter: plain pointer drag
  Pinch,    // two fingers: zoom
  Ignored,  // ImGui's touch or a finished pinch; wait for all fingers up
};

struct TouchPoint {
  SDL_FingerID id = 0;
  float x = 0.f, y = 0.f;  // window pixels
};

struct Touch {
  TouchMode mode = TouchMode::Idle;
  TouchPoint fingers[2];  // tracked fingers; further ones are ignored
  int count = 0;
  // Gesture origin; its float window (if any) or pane is the scroll target.
  float start_x = 0.f, start_y = 0.f;
  Uint32 start_ticks = 0;
  bool on_float = false;
  int pane = 0;
  // Pan velocity (scroll px/s) and the fling that continues it.
  float velocity = 0.f;
  Uint32 last_move_ticks = 0;
  bool flinging = false;
  Uint32 fling_ticks = 0;
  // Pinch: finger distance and body size when the second finger landed.
  float pinch_dist = 0.f;
  int pinch_body_px = 0;
};

// What the status bar says this frame (derived from world state each frame).
enum class StatusTone { Hint, Info, Browse, Warn, Prompt, Notice };
struct StatusLine {
  std::string text;   // left panel: hint, hover target, prompt, notice
  std::string stats;  // right panel: word / selection counts (may be empty)
  StatusTone tone = StatusTone::Hint;
};

// The single active status-bar prompt (at most one at a time).
enum class PromptKind : uint8_t {
  None,
  Search,
  Replace,  // search + replacement fields
  SaveAs,
  OpenFile,
  OpenInWindow,
  UrlBar,
  InsertImage,
  InsertFigure,
  InsertInclude,
  EditLink,
  EditCite,
  EditSummary,
};

struct Prompt {
  PromptKind kind = PromptKind::None;
  std::string text;           // find text / path / URL / href / summary …
  std::string text2;          // replacement text (Replace only)
  bool focus_second = false;  // Replace: editing text2
  // Target of EditLink / EditCite / EditSummary.
  xanatoo::NodeId node = xanatoo::INVALID_NODE;
  size_t span = 0;
};

struct App {
  App(SDL_Window* w, SDL_Renderer* r)
      : window(w),
        sdl(r),
        images(r),
        layout_engine(fonts, &images),
        renderer(r, fonts, &images) {}
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // --- platform / services ---
  CliOptions opt;
  SDL_Window* window = nullptr;
  SDL_Renderer* sdl = nullptr;
  int body_px = 17;
  int heading_px = 26;
  xanatoo::FontSet fonts;
  std::vector<DocBuffer> buffers;  // dense storage; address by BufferId
  BufferId next_buffer_id = 1;
  std::vector<BufferId> tab_order;
  ImageCache images;
  LayoutEngine layout_engine;
  xanatoo::ViewSettings view_settings;
  Renderer renderer;
  xanatoo::Ui ui;
  float ui_top = 0.f;
  float ui_bottom = 0.f;
  Pane panes[2];
  int focus = 0;
  bool split = false;  // auto-enabled when two files are given
  float split_ratio = 0.5f;
  bool dragging_sel = false;
  bool dragging_split = false;
  bool dragging_scroll = false;
  int scroll_drag_pane = 0;
  float scroll_drag_grab = 0.f;
  int sb_hover = 0;
  bool dragging_image = false;
  xanatoo::NodeId resize_image = xanatoo::INVALID_NODE;
  float resize_start_x = 0.f;
  float resize_start_scale = 1.f;
  bool dragging_block = false;
  xanatoo::NodeId drag_block_id = xanatoo::INVALID_NODE;
  xanatoo::NodeId drop_target_id = xanatoo::INVALID_NODE;
  bool drop_before = true;
  float drop_line_y = -1.f;
  bool dragging_col_gap = false;
  xanatoo::NodeId col_gap_left = xanatoo::INVALID_NODE;
  xanatoo::NodeId col_gap_right = xanatoo::INVALID_NODE;
  float col_gap_start_x = 0.f;
  float col_gap_left_w = 1.f;
  float col_gap_right_w = 1.f;
  Uint32 last_click_ticks = 0;
  float last_click_x = -999.f;
  float last_click_y = -999.f;
  int click_count = 0;
  int win_w = 1024;
  int win_h = 768;
  bool running = true;
  bool needs_layout = true;
  int last_layout_w = 0;
  int last_layout_h = 0;
  std::vector<FloatWindow> soft_wins;  // Document-role floats, back→front
  SoftDock dock_preview = SoftDock::None;
  Prompt prompt;
  // Last find / replace text, kept when another prompt displaces a search
  // so F3 / Replace All keep working.
  std::string last_search, last_replace;
  xanatoo::Minibuffer minibuf;
  std::vector<std::string> minibuf_hist_path;
  std::vector<std::string> minibuf_hist_url;
  std::string gemini_current_url;
  std::vector<std::string> nav_history;
  int nav_hist_pos = -1;
  std::vector<std::string> bookmarks;
  bool history_panel_open = false;
  bool bookmarks_panel_open = false;
  std::string status_notice;
  Uint32 status_notice_until = 0;
  bool discard_armed = false;
  bool help_mode = false;
  xanatoo::Document help_doc;
  xanatoo::LayoutResult help_layout;
  bool help_needs_layout = true;
  bool help_loaded = false;
  bool outline_mode = false;
  bool outline_docked = true;
  int outline_dock_w = 280;
  std::vector<xanatoo::OutlineEntry> outline;
  int outline_sel = 0;
  bool backlinks_mode = false;
  std::vector<xanatoo::BacklinkEntry> backlinks;
  int backlinks_sel = 0;
  std::string backlinks_label;
  std::unordered_set<xanatoo::NodeId> folded_sections;
  std::string last_fold_doc_key;
  bool section_focus_mode = false;
  xanatoo::NodeId section_focus_id = xanatoo::INVALID_NODE;
  bool diff_mode = false;
  std::vector<xanatoo::NodeId> diff_marks[2];
  std::vector<xanatoo::DiffWordMark> diff_words[2];
  bool structure_mode = false;
  bool browse_mode = false;
  bool virtual_keyboard = false;
  SDL_GameController* gamepad = nullptr;
  bool stick_cursor_on = false;
  float stick_cx = 0.f;
  float stick_cy = 0.f;
  bool stick_cursor_init = false;
  bool stick_lmb = false;
  bool stick_rmb = false;
  float mouse_x = 0.f;
  float mouse_y = 0.f;
  SDL_Cursor* cursor_arrow = nullptr;
  SDL_Cursor* cursor_hand = nullptr;
  SDL_Cursor* cursor_sizewe = nullptr;
  SDL_Cursor* cursor_sizenwse = nullptr;
  SDL_Cursor* cursor_sizeall = nullptr;
  SDL_Cursor* cursor_current = nullptr;
  Uint32 caret_blink_start = 0;
  FloatWindow help = make_float_panel(WindowRole::Help, 80, 100, 560, 480);
  FloatWindow bl = make_float_panel(WindowRole::Backlinks, 80, 120, 360, 320);
  FloatWindow ol = make_float_panel(WindowRole::Outline, 40, 100, 320, 420);
  Hover hover;
  Touch touch;
};

// ---- buffers ----
std::string resolve_open_path(App& app, std::string path);
xanatoo::Document load_doc(App& app, std::string path);
std::string parent_dir(const std::string& path);
bool is_in_app_remote_url(std::string_view s);
xanatoo::Document load_doc_or_placeholder(App& app, const std::string& path);
std::string buffer_tab_label(App& app, BufferId bi);
void update_title(App& app);
xanatoo::Document& doc_of(App& app, Pane& p);
std::string& path_of(App& app, Pane& p);
std::string base_of(App& app, Pane& p);
bool shared_doc(App& app);
Pane& focused_pane(App& app);
void checkpoint(App& app);
void mark_dirty(App& app);
bool any_dirty(App& app);
bool allow_discard_dirty(App& app, BufferId di);
void edit(App& app);
std::string find_help_path(App& app);
std::string find_repo_file(App& app, const char* name);
std::string canon_path(App& app, const std::string& path);
BufferId find_buffer(App& app, const std::string& path);
BufferId ensure_buffer(App& app, const std::string& path);
// Buffer accessors. Never hold a DocBuffer& across add_buffer/remove_buffer.
DocBuffer* find_buf(App& app, BufferId id);
DocBuffer& buf(App& app, BufferId id);  // asserts the buffer exists
BufferId add_buffer(App& app, xanatoo::Document doc, std::string path);
// Removes the buffer and every pane / float / tab reference to it
// (repoints panes at the next tab; creates an untitled buffer if none left).
void remove_buffer(App& app, BufferId id);
// Close a tab: like remove_buffer, but keeps the buffer while a floating
// window still shows it.
void close_tab(App& app, BufferId id);
void pop_out_focused(App& app);
void pop_in_soft(App& app, size_t si);
void open_browse(App& app, const std::string& path, const std::string& title);
void ensure_help_doc(App& app);
void layout_help(App& app);
std::tuple<int, int, int, int> help_client_rect(App& app);

// ---- view ----
void apply_view_settings(App& app);
void refresh_outline(App& app);
void refresh_backlinks(App& app);
void invalidate_height_around(App& app, Pane& pane, xanatoo::NodeId id);
void drop_height_hints_for_buf(App& app, BufferId bi);
xanatoo::NodeId active_focus_section(App& app);
void toggle_section_fold(App& app, xanatoo::NodeId section);
void toggle_section_focus_mode(App& app);
void reload_folds_for_focused(App& app);
void toggle_fold_at_caret(App& app);
void refresh_diff(App& app);
void expand_ancestors_for_node(App& app, xanatoo::NodeId target);
void layout_panes(App& app);
void ensure_virtual_layout(App& app, Pane& p);
std::optional<float> caret_doc_y(App& app, Pane& p);
void force_full_pane_layout(App& app, Pane& p);
void scroll_caret_into_view(App& app, Pane& p);
void scroll_caret_to_top(App& app, Pane& p);
float pane_max_scroll(App& app, const Pane& p);
// Body/heading font size ± `steps` points (View → Zoom, pinch).
void zoom_by(App& app, int steps);
void nav_page(App& app, bool up);
void nav_move_block(App& app, bool prev);
void nav_move_line(App& app, bool up);
void nav_header(App& app, bool prev);
bool toggle_stretch_at_caret(App& app);
void collapse_or_expand_all(App& app, bool expand);
void jump_outline_sel(App& app);
int pane_at(App& app, float mx);
float local_x(App& app, float mx, int pi);
float content_y(App& app, float window_y);
std::tuple<int, int, int, int> outline_client_rect(App& app);
float outline_row_step(App& app);

// ---- prompts ----
// ---- prompts ----
inline bool prompt_active(const App& app) {
  return app.prompt.kind != PromptKind::None;
}
inline bool prompt_is_search(const App& app) {
  return app.prompt.kind == PromptKind::Search ||
         app.prompt.kind == PromptKind::Replace;
}
// Path/URL kinds edited through the Minibuffer (Tab completion, history).
bool prompt_uses_minibuf(PromptKind k);
// Opens `kind` (closing any prompt that was open); `initial` seeds the text.
void open_prompt(App& app, PromptKind kind, std::string initial = {});
// Closes the prompt and clears its text. Search text survives in last_search
// unless `forget_search` (Esc).
void close_prompt(App& app, bool forget_search = false);
// Opens the Find (or Find+Replace) prompt, keeping the current / last query.
void open_search(App& app, bool replace);
const char* prompt_label(PromptKind k);
// Enter pressed: commit the active prompt. Returns false when the key should
// fall through to the document (no prompt, or an empty/plain find).
bool commit_prompt(App& app, bool ctrl, bool shift);
bool minibuf_active(App& app);
void minibuf_begin(App& app, const std::string& initial = {});
void minibuf_commit_path_hist(App& app);
void minibuf_commit_url_hist(App& app);
void minibuf_sync_out(App& app);
void flash_status(App& app, const std::string& msg);

// ---- pointer ----
void open_first_gamepad(App& app);
void clear_interaction_hover(App& app);
void draw_stick_cursor(App& app, SDL_Renderer* r, int cx, int cy, bool pressed);
void set_cursor(App& app, SDL_Cursor* c);
void reset_blink(App& app);

// Left-button press / drag / release at window point (x, y); the mouse
// handlers and touch taps share these (app_events.cpp).
void pointer_left_down(App& app, int x, int y, bool imgui_mouse);
void pointer_motion(App& app, int x, int y, bool imgui_mouse);
void pointer_left_up(App& app, int x, int y);

// ---- touch (app_touch.cpp) ----
// Mouse events SDL synthesises from a touchscreen (which == SDL_TOUCH_MOUSEID).
bool is_touch_mouse_event(const SDL_Event& ev);
void touch_finger_down(App& app, const SDL_TouchFingerEvent& tf);
void touch_finger_motion(App& app, const SDL_TouchFingerEvent& tf);
void touch_finger_up(App& app, const SDL_TouchFingerEvent& tf);
// Per frame: long-press timer and fling momentum.
void update_touch(App& app);

// ---- nav ----
void push_nav_history(App& app, const std::string& url);
bool is_bookmarked(App& app, const std::string& url);
void add_bookmark(App& app, const std::string& url);
void remove_bookmark(App& app, const std::string& url);
bool is_inapp_doc_href(App& app, const std::string& href);
std::string resolve_doc_href(App& app, const std::string& href,
                             const std::string& base_path = {});
bool navigate_to_url(App& app, std::string target, bool record_history = true);
void open_transclude_in_other_pane(App& app);
void activate_at_caret(App& app);

// ---- softwin ----
std::tuple<int, int, int, int> soft_client_rect(App& app,
                                                const FloatWindow& sw);
void snap_soft_geom(App& app, int& x, int& y, int w, int h);
std::tuple<int, int, int> soft_client_bounds(App& app);
SoftDock compute_dock_zone(App& app, int mx, int my);
std::tuple<int, int, int, int> dock_rect(App& app, SoftDock d);
void apply_soft_dock(App& app, FloatWindow& sw, SoftDock d);
void restore_soft_float(App& app, FloatWindow& sw, int mx, int my);
void maximize_soft(App& app, FloatWindow& sw);
void clamp_all_floating(App& app);
xanatoo::Document& soft_doc(App& app, FloatWindow& sw);
const std::string& soft_path(App& app, const FloatWindow& sw);
std::string soft_title(App& app, const FloatWindow& sw);
void layout_soft(App& app, FloatWindow& sw);
void raise_soft(App& app, size_t i);

// ---- floating windows (app_windows.cpp) ----
// z-order, back→front: outline, backlinks, help, document windows.
std::vector<FloatWindow*> float_stack(App& app);
FloatWindow* float_at(App& app, int x, int y);  // topmost window under (x,y)
std::tuple<int, int, int, int> float_client_rect(App& app,
                                                 const FloatWindow& w);
float float_max_scroll(App& app, const FloatWindow& w);
bool float_press(App& app, int x, int y);   // true = click consumed
bool float_motion(App& app, int x, int y);  // true = drag/resize/scroll active
void float_release(App& app, int x, int y);
bool float_wheel(App& app, int x, int y, int wheel_y);
bool float_page_scroll(App& app, int x, int y, bool up);
bool close_top_float(App& app);  // closes the topmost window, if any

// ---- keymap ----
// Ctrl+<key> chord → command (nullopt = not a table binding).
std::optional<xanatoo::UiCommand> keymap_lookup(SDL_Keycode sym, bool ctrl,
                                                bool shift);

// ---- chrome (theme-based canvas widgets) ----
StatusLine compute_status(App& app);
void draw_floating_frame(App& app, const SDL_Rect& outer,
                         const std::string& title, bool active,
                         bool resizable = true);
void draw_client_scrollbar(App& app, int cx, int cy, int cw, int ch,
                           float scroll, float max_scroll);

// ---- frame stages ----
void sync_window_size(App& app);
void relayout_if_needed(App& app);
void run_chrome(App& app);
void poll_gamepad_axes(App& app);
void draw_pane(App& app, int i);
void draw_panes(App& app);
void draw_split_chrome(App& app);
void draw_status_bar(App& app);
void draw_outline_panel(App& app);
void draw_backlinks_panel(App& app);
void draw_help_window(App& app);
void draw_floats(App& app);  // all floating windows, back→front
void draw_dock_preview(App& app);
void draw_hover_tooltip(App& app);
void present_frame(App& app);
void handle_event(App& app, SDL_Event& ev);
void run_frame(App& app);
// Apply a menu/toolbar/shortcut command (shared by chrome and keyboard).
void execute_command(App& app, xanatoo::UiCommand cmd,
                     const std::string& arg = {});
bool app_init(App& app);
void app_shutdown(App& app);

}  // namespace xanatoo::app
