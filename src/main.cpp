// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// xanatoo — interactive document composition engine

#include "xanatoo/types.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/render.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/ui.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/minibuffer.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/view_settings.hpp"
#include "xanatoo/cli.hpp"

#include <SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  xanatoo::CliOptions opt;
  if (!xanatoo::parse_cli(argc, argv, opt)) {
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) !=
      0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_GameControllerEventState(SDL_ENABLE);
  SDL_StartTextInput();

  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
  if (opt.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window* window = SDL_CreateWindow(
      "xanatoo",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1024, 768,
      win_flags);
  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* sdl = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!sdl) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  std::string font_path = opt.font_path;
  if (font_path.empty()) {
    if (const char* env = std::getenv("XANATOO_FONT")) {
      if (env[0] != '\0') font_path = env;
    }
  }
  // Preferred path is optional — FontConfig resolves a full family set.
  if (!font_path.empty() && !fs::exists(font_path)) {
    std::cerr << "Font file does not exist: " << font_path
              << " (falling back to FontConfig)\n";
    font_path.clear();
  }

  int body_px = 17;
  int heading_px = 26;
  xanatoo::FontSet fonts;
  if (!fonts.load(sdl, font_path, body_px, heading_px)) {
    std::cerr << "Failed to load fonts via FontConfig. "
                 "Pass a TTF with --font or install DejaVu/Liberation.\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  xanatoo::Font& body_font = fonts.regular;


  // Resolve EPUB (immutable) → derived Markdown via pandoc; other paths pass through.
  auto resolve_open_path = [&](std::string path) -> std::string {
    if (path.empty() || !xanatoo::is_epub_path(path)) return path;
    auto imp = xanatoo::import_epub_via_pandoc(path);
    if (!imp.ok) {
      std::cerr << "EPUB import failed: " << imp.error << "\n";
      return {};
    }
    return imp.markdown_path;
  };

  auto load_doc = [&](std::string path) -> xanatoo::Document {
    if (path.empty()) {
      return xanatoo::parse_document(xanatoo::fallback_document(), {});
    }
    if (xanatoo::is_epub_path(path)) {
      std::string md = resolve_open_path(path);
      if (md.empty()) {
        return xanatoo::parse_document(
            "# EPUB import failed\n\nInstall **pandoc** and try again.\n", {});
      }
      path = std::move(md);
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "Failed to open document: " << path << "\n";
      return xanatoo::parse_document(xanatoo::fallback_document(), {});
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    std::cout << "Opened: " << path << "\n";
    return xanatoo::parse_document(source, path);
  };

  auto parent_dir = [](const std::string& path) -> std::string {
    if (path.empty()) return {};
    std::filesystem::path p(path);
    if (p.has_parent_path()) return p.parent_path().string();
    return {};
  };

  // Shared document buffers: tabs + floating windows all reference these.
  struct DocBuffer {
    xanatoo::Document doc;
    std::string path;
    bool dirty = false;
    xanatoo::History history;
  };
  std::vector<DocBuffer> buffers;

  // Gemini / Reddit / Lemmy / Nostr — opened in-pane (not the system browser).
  auto is_in_app_remote_url = [](std::string_view s) {
    return xanatoo::is_gemini_url(s) || xanatoo::is_reddit_url(s) ||
           xanatoo::is_lemmy_url(s) || xanatoo::is_nostr_ref(s);
  };

  auto load_doc_or_placeholder = [&](const std::string& path) {
    if (is_in_app_remote_url(path)) {
      // Real fetch happens after navigate_to_url is defined.
      return xanatoo::parse_document("# Loading…\n", {});
    }
    return load_doc(path);
  };

  {
    std::string p0 = opt.file_path;
    std::string p1 = opt.file_path2;
    if (p0.empty()) {
      p0 = xanatoo::find_default_document();
      if (p0.empty())
        std::cerr << "Note: examples/welcome.md not found; starting empty.\n";
    }
    DocBuffer b0;
    {
      std::string r0 = resolve_open_path(p0);
      b0.path = r0.empty() ? p0 : r0;
      b0.doc = load_doc_or_placeholder(b0.path.empty() ? p0 : b0.path);
      // If EPUB resolve failed, still attempt load_doc on original for error page.
      if (xanatoo::is_epub_path(p0) && r0.empty())
        b0.doc = load_doc(p0);
    }
    buffers.push_back(std::move(b0));
    if (!p1.empty()) {
      DocBuffer b1;
      {
        std::string r1 = resolve_open_path(p1);
        b1.path = r1.empty() ? p1 : r1;
        b1.doc = load_doc_or_placeholder(b1.path);
        if (xanatoo::is_epub_path(p1) && r1.empty())
          b1.doc = load_doc(p1);
      }
      buffers.push_back(std::move(b1));
    }
  }

  // Tab order (buffer indices left→right in the tab strip).
  std::vector<int> tab_order;
  for (int i = 0; i < static_cast<int>(buffers.size()); ++i)
    tab_order.push_back(i);

  auto buffer_tab_label = [&](int bi) -> std::string {
    if (bi < 0 || bi >= static_cast<int>(buffers.size())) return "?";
    std::string s;
    if (buffers[static_cast<size_t>(bi)].dirty) s += "*";
    const std::string& path = buffers[static_cast<size_t>(bi)].path;
    if (path.empty()) {
      s += "untitled";
    } else {
      try {
        s += fs::path(path).filename().string();
      } catch (...) {
        s += path;
      }
    }
    return s;
  };

  xanatoo::ImageCache images(sdl);

  auto update_title = [&]() {
    std::string title = "xanatoo";
    if (!buffers.empty()) {
      title += " — ";
      title += buffer_tab_label(0);
      if (buffers.size() > 1) {
        title += " (+";
        title += std::to_string(buffers.size() - 1);
        title += " more)";
      }
    }
    SDL_SetWindowTitle(window, title.c_str());
  };
  update_title();

  xanatoo::LayoutEngine layout_engine(fonts, &images);
  xanatoo::ViewSettings view_settings;
  xanatoo::load_view_settings(view_settings);
  layout_engine.set_options(xanatoo::LayoutOptions::from_view(view_settings));
  xanatoo::Renderer renderer(sdl, fonts, &images);
  xanatoo::Ui ui;
  if (!ui.init(window, sdl, opt.ui_scale)) {
    std::cerr << "Warning: ImGui UI failed to init; continuing without menus\n";
  }
  float ui_top = 0.f;  // menu + toolbar height (updated each frame)
  float ui_bottom = 0.f;  // virtual keyboard (updated each frame)

  struct Pane {
    int buf = 0;  // index into buffers
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

  Pane panes[2];
  panes[0].buf = 0;
  panes[1].buf = (buffers.size() > 1) ? 1 : 0;
  xanatoo::ensure_valid_caret(buffers[0].doc, panes[0].sel.focus);
  panes[0].sel.anchor = panes[0].sel.focus;
  xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(panes[1].buf)].doc,
                              panes[1].sel.focus);
  panes[1].sel.anchor = panes[1].sel.focus;

  int focus = 0;
  bool split = buffers.size() > 1;  // auto-split when two files given
  float split_ratio = 0.5f;        // left pane fraction
  bool dragging_sel = false;
  bool dragging_split = false;
  bool dragging_scroll = false;
  int scroll_drag_pane = 0;
  float scroll_drag_grab = 0.f;  // Y offset within thumb when drag starts
  int sb_hover = 0;              // 0 none, 1 ↑, 2 ↓, 3 track (focused pane)
  // Classic BeOS / GNOME 2–style document scrollbar (wider + arrow buttons).
  const int kSbW = 14;
  const int kSbArrow = 14;
  // Layout must not use the scrollbar strip (track + 2px air).
  // Content width matches the clip to the left of the scrollbar.
  const int kSbLayoutGutter = kSbW + 4;
  bool dragging_image = false;
  xanatoo::NodeId resize_image = xanatoo::INVALID_NODE;
  float resize_start_x = 0.f;
  float resize_start_scale = 1.f;
  // Block drag-reorder: grip lives in the left gutter *before* the text
  // (box.x - grip_w .. box.x), so it does not steal text selection.
  constexpr float kBlockGripW = 18.f;
  bool dragging_block = false;
  xanatoo::NodeId drag_block_id = xanatoo::INVALID_NODE;
  xanatoo::NodeId drop_target_id = xanatoo::INVALID_NODE;
  bool drop_before = true;
  float drop_line_y = -1.f;  // content y for insertion indicator
  // Column gutter drag (adjust relative display_scale weights)
  bool dragging_col_gap = false;
  xanatoo::NodeId col_gap_left = xanatoo::INVALID_NODE;
  xanatoo::NodeId col_gap_right = xanatoo::INVALID_NODE;
  float col_gap_start_x = 0.f;
  float col_gap_left_w = 1.f;
  float col_gap_right_w = 1.f;
  Uint32 last_click_ticks = 0;
  float last_click_x = -999.f, last_click_y = -999.f;
  int click_count = 0;

  auto doc_of = [&](Pane& p) -> xanatoo::Document& {
    return buffers[static_cast<size_t>(p.buf)].doc;
  };
  auto path_of = [&](Pane& p) -> std::string& {
    return buffers[static_cast<size_t>(p.buf)].path;
  };
  auto base_of = [&](Pane& p) -> std::string {
    return parent_dir(buffers[static_cast<size_t>(p.buf)].path);
  };
  auto shared_doc = [&]() -> bool {
    return panes[0].buf == panes[1].buf;
  };
  buffers[0].history.reset(buffers[0].doc, panes[0].sel);
  if (buffers.size() > 1)
    buffers[1].history.reset(buffers[1].doc, panes[1].sel);

  int win_w = 1024, win_h = 768;  // match SDL_CreateWindow defaults
  {
    int nw = 0, nh = 0;
    SDL_GetWindowSize(window, &nw, &nh);
    if (nw >= 64 && nh >= 64) {
      win_w = nw;
      win_h = nh;
    }
  }
  // Handheld / small displays: slightly larger document text.
  if (win_w > 0 && win_w <= 720 && body_px < 20) {
    body_px = 20;
    heading_px = 28;
    fonts.set_sizes(body_px, heading_px);
  }
  bool running = true;
  bool needs_layout = true;
  int last_layout_w = 0, last_layout_h = 0;

  auto apply_view_settings = [&]() {
    view_settings.clamp();
    layout_engine.set_options(xanatoo::LayoutOptions::from_view(view_settings));
    panes[0].height_cache.clear();
    panes[1].height_cache.clear();
    needs_layout = true;
    if (!xanatoo::save_view_settings(view_settings))
      std::cerr << "Could not persist view settings to "
                << xanatoo::view_settings_path() << "\n";
  };

  // Leap-style search (Canon Cat inspiration)
  bool search_mode = false;
  std::string search_query;
  bool replace_mode = false;
  bool replace_focus = false;  // false=edit find, true=edit replacement
  std::string replace_text;
  bool save_as_mode = false;
  std::string save_as_path;
  bool image_insert_mode = false;
  bool image_insert_as_figure = false;
  std::string image_insert_path;
  bool include_insert_mode = false;
  std::string include_insert_src;
  bool link_edit_mode = false;
  bool link_edit_is_cite = false;  // true → citation key, not link href
  std::string link_edit_href;
  xanatoo::NodeId link_edit_node = xanatoo::INVALID_NODE;
  size_t link_edit_span = 0;
  bool summary_edit_mode = false;
  std::string summary_edit_text;
  xanatoo::NodeId summary_edit_node = xanatoo::INVALID_NODE;

  // Floating software windows for arbitrary documents (stack; back = topmost).
  // Soft-window tile zones (drag title bar to screen edge).
  enum class SoftDock { None = 0, Left, Right, Top, Bottom, Max };

  struct SoftWindow {
    int buf = -1;  // index into buffers (−1 = unused)
    std::string title;  // display override; empty → derive from path
    xanatoo::Selection sel{};
    xanatoo::LayoutResult layout;
    float scroll = 0.f;
    bool needs_layout = true;
    int x = 120, y = 70, w = 540, h = 440;
    bool dragging = false;
    int drag_dx = 0, drag_dy = 0;
    int resize = 0;  // 1=right, 2=bottom
    bool scroll_dragging = false;
    int resize_start_x = 0, resize_start_y = 0;
    int resize_start_w = 0, resize_start_h = 0;
    // Docking / maximize
    bool maximized = false;
    SoftDock dock = SoftDock::None;
    int restore_x = 120, restore_y = 70, restore_w = 540, restore_h = 440;
    Uint32 last_title_click = 0;
  };
  std::vector<SoftWindow> soft_wins;
  SoftDock dock_preview = SoftDock::None;  // live zone while dragging
  const int soft_snap_px = 16;
  const int soft_dock_zone = 36;  // edge thickness that triggers tiling
  const int status_bar_h = 28;
  bool path_in_window_mode = false;
  std::string path_in_window_buf;
  bool open_file_mode = false;  // File→Open into focused pane (status-bar path)
  std::string open_file_buf;
  bool url_bar_mode = false;    // Ctrl+L — open gemini:// or file path
  std::string url_bar_buf;
  // Shared Emacs-style minibuffer for path/URL prompts (cursor, Tab, history).
  xanatoo::Minibuffer minibuf;
  std::vector<std::string> minibuf_hist_path =
      xanatoo::load_minibuffer_history("path");
  std::vector<std::string> minibuf_hist_url =
      xanatoo::load_minibuffer_history("url");
  auto minibuf_active = [&]() {
    return open_file_mode || save_as_mode || path_in_window_mode ||
           url_bar_mode || image_insert_mode || include_insert_mode;
  };
  auto minibuf_begin = [&](const std::string& initial = {}) {
    minibuf.clear();
    if (!initial.empty()) minibuf.set_text(initial);
  };
  auto minibuf_commit_path_hist = [&]() {
    xanatoo::push_minibuffer_history(minibuf_hist_path, minibuf.text);
    xanatoo::save_minibuffer_history("path", minibuf_hist_path);
  };
  auto minibuf_commit_url_hist = [&]() {
    xanatoo::push_minibuffer_history(minibuf_hist_url, minibuf.text);
    xanatoo::save_minibuffer_history("url", minibuf_hist_url);
  };
  // Sync legacy string buffers from minibuf (Enter handlers still read them).
  auto minibuf_sync_out = [&]() {
    if (open_file_mode) open_file_buf = minibuf.text;
    if (save_as_mode) save_as_path = minibuf.text;
    if (path_in_window_mode) path_in_window_buf = minibuf.text;
    if (url_bar_mode) url_bar_buf = minibuf.text;
    if (image_insert_mode) image_insert_path = minibuf.text;
    if (include_insert_mode) include_insert_src = minibuf.text;
  };
  std::string gemini_current_url;  // base for relative gemini links
  std::vector<std::string> nav_history;
  int nav_hist_pos = -1;  // index into nav_history
  constexpr int kMaxNavHistory = 100;
  std::vector<std::string> bookmarks = xanatoo::load_bookmarks();
  bool history_panel_open = false;
  bool bookmarks_panel_open = false;
    std::string status_notice;
  Uint32 status_notice_until = 0;
  bool discard_armed = false;

  bool help_mode = false;  // F1 help as a real document in a software window
  xanatoo::Document help_doc;
  xanatoo::LayoutResult help_layout;
  float help_scroll = 0.f;
  bool help_needs_layout = true;
  bool help_loaded = false;
  // Software window geometry (window coordinates)
  int help_x = 80, help_y = 100, help_w = 560, help_h = 480;
  bool help_dragging = false;
  int help_drag_dx = 0, help_drag_dy = 0;
  // Resize: bit flags 1=right, 2=bottom (corner = both)
  int help_resize = 0;
  bool help_scroll_dragging = false;
  int help_resize_start_x = 0, help_resize_start_y = 0;
  int help_resize_start_w = 0, help_resize_start_h = 0;
  const int help_title_h = 28;
  const int help_pad = 12;
  const int help_edge = 6;  // hit thickness for edges/corner
  // Heading outline as a software window (Ctrl+Shift+O) — same chrome as Help.
  bool outline_mode = false;
  bool outline_docked = true;  // dual-pane outline | body (vs floating chrome)
  int outline_dock_w = 280;
  std::vector<xanatoo::OutlineEntry> outline;
  int outline_sel = 0;
  // Same-doc backlinks soft window (Ctrl+Shift+B).
  bool backlinks_mode = false;
  std::vector<xanatoo::BacklinkEntry> backlinks;
  int backlinks_sel = 0;
  std::string backlinks_label;
  int bl_x = 80, bl_y = 120, bl_w = 360, bl_h = 320;
  float bl_scroll = 0.f;
  bool bl_dragging = false;
  int bl_drag_dx = 0, bl_drag_dy = 0;
  // View-local Section/Article fold (not in the document; future: SQLite).
  std::unordered_set<xanatoo::NodeId> folded_sections;
  std::string last_fold_doc_key;
  // Outline "Focus" mode: body shows only the focused section (layout filter).
  bool section_focus_mode = false;
  xanatoo::NodeId section_focus_id = xanatoo::INVALID_NODE;
  int ol_x = 40, ol_y = 100, ol_w = 320, ol_h = 420;
  bool ol_dragging = false;
  int ol_drag_dx = 0, ol_drag_dy = 0;
  int ol_resize = 0;  // 1=right, 2=bottom
  int ol_resize_start_x = 0, ol_resize_start_y = 0;
  int ol_resize_start_w = 0, ol_resize_start_h = 0;
  float ol_scroll = 0.f;
  bool ol_scroll_dragging = false;
  // Side-by-side version marks (two independent documents)
  bool diff_mode = false;
  std::vector<xanatoo::NodeId> diff_marks[2];
  std::vector<xanatoo::DiffWordMark> diff_words[2];
  // Colour-code layout boxes by NodeKind (logical structure overlay)
  bool structure_mode = false;
  // Read-only navigation: no typing / edits; gamepad-friendly browsing
  bool browse_mode = false;
  bool virtual_keyboard = false;
  SDL_GameController* gamepad = nullptr;
  // Analog-stick software pointer (R36S / no mouse). Right stick moves it;
  // A/B emit left/right clicks at the pointer. Drawn as an on-screen arrow.
  bool stick_cursor_on = false;
  float stick_cx = 0.f, stick_cy = 0.f;
  bool stick_cursor_init = false;
  bool stick_lmb = false, stick_rmb = false;
  auto open_first_gamepad = [&]() {
    if (gamepad) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      if (SDL_IsGameController(i)) {
        gamepad = SDL_GameControllerOpen(i);
        if (gamepad) {
          const char* name = SDL_GameControllerName(gamepad);
          std::cout << "Gamepad: " << (name ? name : "(unknown)") << "\n";
          break;
        }
      }
    }
  };
  open_first_gamepad();
  if (gamepad) stick_cursor_on = true;
  // Hovered interactive element (status bar, tooltip, highlight, cursor)
  std::string hovered_href;
  std::string hovered_rel;      // typed :rel when present
  bool hovered_is_cite = false;
  std::string hovered_preview;  // internal #anchor target snippet
  xanatoo::NodeId hovered_link_node = xanatoo::INVALID_NODE;
  size_t hovered_link_span = 0;
  int hovered_link_pane = -1;
  xanatoo::NodeId hovered_stretch = xanatoo::INVALID_NODE;
  int hovered_stretch_pane = -1;
  xanatoo::NodeId hovered_image = xanatoo::INVALID_NODE;
  int hovered_image_pane = -1;
  bool hovered_image_handle = false;
  bool hovered_col_gap = false;
  int hovered_col_gap_pane = -1;
  xanatoo::NodeId hovered_task = xanatoo::INVALID_NODE;
  int hovered_task_pane = -1;
  xanatoo::NodeId hovered_grip = xanatoo::INVALID_NODE;
  int hovered_grip_pane = -1;
  xanatoo::NodeId hovered_transclude = xanatoo::INVALID_NODE;
  int hovered_transclude_pane = -1;
  std::string hovered_transclude_src;
  float mouse_x = 0.f, mouse_y = 0.f;

  auto clear_interaction_hover = [&]() {
    hovered_href.clear();
    hovered_rel.clear();
    hovered_preview.clear();
    hovered_is_cite = false;
    hovered_link_node = xanatoo::INVALID_NODE;
    hovered_link_span = 0;
    hovered_link_pane = -1;
    hovered_stretch = xanatoo::INVALID_NODE;
    hovered_stretch_pane = -1;
    hovered_image = xanatoo::INVALID_NODE;
    hovered_image_pane = -1;
    hovered_image_handle = false;
    hovered_col_gap = false;
    hovered_col_gap_pane = -1;
    hovered_task = xanatoo::INVALID_NODE;
    hovered_task_pane = -1;
    hovered_grip = xanatoo::INVALID_NODE;
    hovered_grip_pane = -1;
    hovered_transclude = xanatoo::INVALID_NODE;
    hovered_transclude_pane = -1;
    hovered_transclude_src.clear();
  };

  SDL_Cursor* cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  SDL_Cursor* cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
  SDL_Cursor* cursor_sizewe = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
  SDL_Cursor* cursor_sizenwse =
      SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
  SDL_Cursor* cursor_sizeall =
      SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
  SDL_Cursor* cursor_current = cursor_arrow;
  // Stick pointer is drawn with SDL rects/lines (no texture — avoids the
  // alpha-inverted garbage we hit with CreateTexture/UpdateTexture).
  auto draw_stick_cursor = [&](SDL_Renderer* r, int cx, int cy, bool pressed) {
    // Classic arrow: tip at (cx, cy). White fill + black outline.
    const int ox[7] = {0, 0, 4, 6, 9, 7, 11};
    const int oy[7] = {0, 14, 11, 17, 15, 10, 8};
    auto fill_tri = [&](int x0, int y0, int x1, int y1, int x2, int y2,
                        Uint8 R, Uint8 G, Uint8 B) {
      // Scanline fill for one triangle (no RenderGeometry needed).
      auto edge = [](int ax, int ay, int bx, int by, int y, int& xL, int& xR) {
        if (ay == by) return;
        if (ay > by) {
          std::swap(ax, bx);
          std::swap(ay, by);
        }
        if (y < ay || y > by) return;
        float t = (by == ay) ? 0.f : float(y - ay) / float(by - ay);
        int x = ax + int((bx - ax) * t);
        if (x < xL) xL = x;
        if (x > xR) xR = x;
      };
      int miny = std::min(y0, std::min(y1, y2));
      int maxy = std::max(y0, std::max(y1, y2));
      SDL_SetRenderDrawColor(r, R, G, B, 255);
      for (int y = miny; y <= maxy; ++y) {
        int xL = 100000, xR = -100000;
        edge(x0, y0, x1, y1, y, xL, xR);
        edge(x1, y1, x2, y2, y, xL, xR);
        edge(x2, y2, x0, y0, y, xL, xR);
        if (xL <= xR) SDL_RenderDrawLine(r, cx + xL, cy + y, cx + xR, cy + y);
      }
    };
    // Two triangles form the arrow head + stem-ish shape.
    const Uint8 fr = pressed ? 180 : 255;
    const Uint8 fg = pressed ? 200 : 255;
    const Uint8 fb = pressed ? 255 : 255;
    fill_tri(ox[0], oy[0], ox[1], oy[1], ox[2], oy[2], fr, fg, fb);
    fill_tri(ox[0], oy[0], ox[2], oy[2], ox[6], oy[6], fr, fg, fb);
    fill_tri(ox[2], oy[2], ox[5], oy[5], ox[6], oy[6], fr, fg, fb);
    fill_tri(ox[2], oy[2], ox[3], oy[3], ox[4], oy[4], fr, fg, fb);
    // Black outline
    SDL_SetRenderDrawColor(r, 20, 24, 32, 255);
    for (int i = 0; i < 7; ++i) {
      int j = (i + 1) % 7;
      // Outline only along silhouette edges (approximate)
      SDL_RenderDrawLine(r, cx + ox[i], cy + oy[i], cx + ox[j], cy + oy[j]);
    }
    // Explicit tip + left edge + bottom for a clearer outline
    SDL_RenderDrawLine(r, cx + 0, cy + 0, cx + 0, cy + 14);
    SDL_RenderDrawLine(r, cx + 0, cy + 0, cx + 11, cy + 8);
    SDL_RenderDrawLine(r, cx + 0, cy + 14, cx + 4, cy + 11);
  };
  auto set_cursor = [&](SDL_Cursor* c) {
    // Real system cursor always tracks the hardware mouse (shapes included).
    // The stick pointer is drawn separately and does not own this.
    if (!c) c = cursor_arrow;
    if (c != cursor_current) {
      SDL_SetCursor(c);
      cursor_current = c;
    }
  };

  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto F = [&]() -> Pane& { return panes[focus]; };

  auto refresh_outline = [&]() {
    xanatoo::collect_outline(doc_of(F()), outline, &folded_sections);
    if (outline_sel >= static_cast<int>(outline.size()))
      outline_sel = std::max(0, static_cast<int>(outline.size()) - 1);
    if (outline_sel < 0) outline_sel = 0;
  };

  auto refresh_backlinks = [&]() {
    backlinks.clear();
    const auto& d = doc_of(F());
    xanatoo::NodeId id = F().sel.focus.node;
    std::string lab = xanatoo::address_label(d, id);
    if (lab.empty()) {
      // Prefer enclosing section's header label, then any # from plain text.
      xanatoo::NodeId sec = xanatoo::enclosing_section(d, id);
      if (sec != xanatoo::INVALID_NODE) lab = xanatoo::address_label(d, sec);
    }
    if (lab.empty() && id != xanatoo::INVALID_NODE) {
      // Match jump_to_href style: use first heading text as soft anchor? skip
      lab = xanatoo::address_label(d, id);
    }
    backlinks_label = lab;
    if (!lab.empty())
      xanatoo::collect_backlinks(d, lab, backlinks);
    if (backlinks_sel >= static_cast<int>(backlinks.size()))
      backlinks_sel = std::max(0, static_cast<int>(backlinks.size()) - 1);
    if (backlinks_sel < 0) backlinks_sel = 0;
  };

  auto flash_status = [&](const std::string& msg) {
    status_notice = msg;
    status_notice_until = SDL_GetTicks() + 4000;
  };

  auto invalidate_height_around = [&](Pane& pane, xanatoo::NodeId id) {
    if (id == xanatoo::INVALID_NODE) return;
    const auto& d = doc_of(pane);
    for (xanatoo::NodeId cur = id; cur != xanatoo::INVALID_NODE && cur < d.next_id;
         cur = d.get(cur).parent) {
      pane.height_cache.erase(cur);
    }
  };

  // Full drop of virtualization hints for a buffer (undo/redo, reload, navigate).
  // Spine-only invalidation is wrong when the whole tree is replaced.
  auto drop_height_hints_for_buf = [&](int bi) {
    for (int pi = 0; pi < 2; ++pi) {
      if (panes[pi].buf != bi) continue;
      panes[pi].height_cache.clear();
      panes[pi].materialised_top = 0.f;
      panes[pi].materialised_bottom = 0.f;
    }
  };

  // Layout filter id for section Focus mode (INVALID = full document).
  auto active_focus_section = [&]() -> xanatoo::NodeId {
    if (!section_focus_mode) return xanatoo::INVALID_NODE;
    return section_focus_id;
  };

  auto toggle_section_fold = [&](xanatoo::NodeId section) {
    if (section == xanatoo::INVALID_NODE) return;
    const auto& d = doc_of(F());
    if (section >= d.next_id) return;
    const auto& n = d.get(section);
    if (n.kind != xanatoo::NodeKind::Section &&
        n.kind != xanatoo::NodeKind::Article)
      return;
    const bool now_folded = !folded_sections.count(section);
    if (now_folded)
      folded_sections.insert(section);
    else
      folded_sections.erase(section);
    for (int pi = 0; pi < 2; ++pi) {
      if (panes[pi].buf == F().buf)
        invalidate_height_around(panes[pi], section);
    }
    needs_layout = true;
    if (outline_mode) refresh_outline();
    // Persist view-local fold in side SQLite (documents stay immutable).
    std::string doc_key = path_of(F());
    if (!doc_key.empty()) {
      std::string sk = xanatoo::section_stable_key(d, section);
      xanatoo::save_section_fold(doc_key, sk, now_folded);
    }
  };

  auto toggle_section_focus_mode = [&]() {
    section_focus_mode = !section_focus_mode;
    if (section_focus_mode) {
      // Seed from outline selection or caret section.
      section_focus_id = xanatoo::INVALID_NODE;
      if (!outline.empty() && outline_sel >= 0 &&
          outline_sel < static_cast<int>(outline.size())) {
        section_focus_id = outline[static_cast<size_t>(outline_sel)].section;
      }
      if (section_focus_id == xanatoo::INVALID_NODE) {
        section_focus_id =
            xanatoo::enclosing_section(doc_of(F()), F().sel.focus.node);
      }
      if (section_focus_id == xanatoo::INVALID_NODE) {
        section_focus_mode = false;
        flash_status("Focus: no section under caret / outline");
        return;
      }
      drop_height_hints_for_buf(F().buf);
      needs_layout = true;
      flash_status("Focus section on — outline picks what you see");
    } else {
      section_focus_id = xanatoo::INVALID_NODE;
      drop_height_hints_for_buf(F().buf);
      needs_layout = true;
      flash_status("Focus section off — full document");
    }
  };

  auto reload_folds_for_focused = [&]() {
    folded_sections.clear();
    std::string doc_key = path_of(F());
    if (doc_key.empty()) return;
    std::unordered_set<std::string> keys;
    xanatoo::load_section_folds(doc_key, keys);
    xanatoo::folds_keys_to_ids(doc_of(F()), keys, folded_sections);
    needs_layout = true;
    if (outline_mode) refresh_outline();
  };

  // Fold the Section/Article that contains the caret (or the section itself).
  auto toggle_fold_at_caret = [&]() {
    auto& d = doc_of(F());
    xanatoo::NodeId id = F().sel.focus.node;
    while (id != xanatoo::INVALID_NODE && id < d.next_id) {
      const auto& n = d.get(id);
      if (n.kind == xanatoo::NodeKind::Section ||
          n.kind == xanatoo::NodeKind::Article) {
        toggle_section_fold(id);
        flash_status(folded_sections.count(id) ? "Section folded"
                                               : "Section expanded");
        return;
      }
      id = n.parent;
    }
    flash_status("No section under caret");
  };


  auto refresh_diff = [&]() {
    diff_marks[0].clear();
    diff_marks[1].clear();
    diff_words[0].clear();
    diff_words[1].clear();
    if (!diff_mode || shared_doc()) return;
    const int b0 = panes[0].buf, b1 = panes[1].buf;
    if (b0 < 0 || b1 < 0 || b0 >= static_cast<int>(buffers.size()) ||
        b1 >= static_cast<int>(buffers.size()) || b0 == b1)
      return;
    xanatoo::compute_diff_marks(buffers[static_cast<size_t>(b0)].doc,
                                buffers[static_cast<size_t>(b1)].doc,
                                diff_marks[0], diff_marks[1], &diff_words[0],
                                &diff_words[1]);
  };

  auto checkpoint = [&]() {
    Pane& p = F();
    buffers[static_cast<size_t>(p.buf)].history.push(doc_of(p), p.sel);
  };
  auto mark_dirty = [&]() {
    int di = F().buf;
    discard_armed = false;  // new edits require a fresh discard warning
    if (di >= 0 && di < static_cast<int>(buffers.size())) {
      if (!buffers[static_cast<size_t>(di)].dirty) {
        buffers[static_cast<size_t>(di)].dirty = true;
        update_title();
      }
      // Soft windows sharing this buffer must re-layout.
      for (auto& sw : soft_wins)
        if (sw.buf == di) sw.needs_layout = true;
      // Virtualization: drop height hints for the edited node and ancestors
      // so the next layout remeasures only the affected spine (not the world).
      for (int pi = 0; pi < 2; ++pi) {
        if (panes[pi].buf == di)
          invalidate_height_around(panes[pi], F().sel.focus.node);
      }
    }
  };

  // Expand folded Section/Article ancestors so a jump target is visible.
  // Persists unfolds to the side store (view state, not the document).
  auto expand_ancestors_for_node = [&](xanatoo::NodeId target) {
    if (target == xanatoo::INVALID_NODE) return;
    const auto& d = doc_of(F());
    bool any = false;
    std::string doc_key = path_of(F());
    for (xanatoo::NodeId id = target; id != xanatoo::INVALID_NODE && id < d.next_id;
         id = d.get(id).parent) {
      const auto& n = d.get(id);
      if (n.kind != xanatoo::NodeKind::Section &&
          n.kind != xanatoo::NodeKind::Article)
        continue;
      if (!folded_sections.count(id)) continue;
      folded_sections.erase(id);
      any = true;
      if (!doc_key.empty()) {
        std::string sk = xanatoo::section_stable_key(d, id);
        xanatoo::save_section_fold(doc_key, sk, false);
      }
      for (int pi = 0; pi < 2; ++pi) {
        if (panes[pi].buf == F().buf) invalidate_height_around(panes[pi], id);
      }
    }
    if (any) {
      needs_layout = true;
      if (outline_mode) refresh_outline();
    }
  };


  auto push_nav_history = [&](const std::string& url) {
    if (url.empty()) return;
    if (nav_hist_pos >= 0 &&
        nav_hist_pos < static_cast<int>(nav_history.size()) &&
        nav_history[static_cast<size_t>(nav_hist_pos)] == url)
      return;
    if (nav_hist_pos + 1 < static_cast<int>(nav_history.size()))
      nav_history.resize(static_cast<size_t>(nav_hist_pos + 1));
    nav_history.push_back(url);
    while (static_cast<int>(nav_history.size()) > kMaxNavHistory)
      nav_history.erase(nav_history.begin());
    nav_hist_pos = static_cast<int>(nav_history.size()) - 1;
  };

  // Seed session history with documents opened on the command line.
  for (const auto& b : buffers) {
    if (!b.path.empty()) push_nav_history(b.path);
  }

  auto is_bookmarked = [&](const std::string& url) -> bool {
    if (url.empty()) return false;
    return std::find(bookmarks.begin(), bookmarks.end(), url) != bookmarks.end();
  };

  auto add_bookmark = [&](const std::string& url) {
    if (url.empty()) {
      flash_status("Nothing to bookmark (no path/URL)");
      return;
    }
    if (is_bookmarked(url)) {
      flash_status("Already bookmarked");
      return;
    }
    bookmarks.push_back(url);
    if (xanatoo::save_bookmarks(bookmarks))
      flash_status("Bookmarked " + url);
    else
      flash_status("Bookmark added (could not persist to disk)");
  };

  auto remove_bookmark = [&](const std::string& url) {
    if (url.empty()) return;
    auto it = std::find(bookmarks.begin(), bookmarks.end(), url);
    if (it == bookmarks.end()) {
      flash_status("Not in bookmarks");
      return;
    }
    bookmarks.erase(it);
    if (xanatoo::save_bookmarks(bookmarks))
      flash_status("Removed bookmark");
    else
      flash_status("Bookmark removed (could not persist to disk)");
  };

  auto any_dirty = [&]() -> bool {
    for (const auto& b : buffers)
      if (b.dirty) return true;
    return false;
  };

  auto allow_discard_dirty = [&](int di) -> bool {
    if (di < 0 || di >= static_cast<int>(buffers.size())) return true;
    if (!buffers[static_cast<size_t>(di)].dirty) return true;
    if (discard_armed) {
      discard_armed = false;
      return true;
    }
    discard_armed = true;
    flash_status("Unsaved changes — Ctrl+S to save, or repeat to discard");
    std::cerr << "Unsaved changes — save first, or repeat the command to discard.\n";
    return false;
  };
  auto edit = [&]() {
    checkpoint();
    mark_dirty();
  };

  auto layout_panes = [&]() {
    const int dock =
        (outline_mode && outline_docked)
            ? std::max(160, std::min(outline_dock_w, win_w / 2))
            : 0;
    const int ww = std::max(64, win_w - dock);
    const int gutter = split ? 6 : 0;
    if (split) {
      int left_w = static_cast<int>(split_ratio * (ww - gutter));
      left_w = std::max(120, std::min(ww - gutter - 120, left_w));
      panes[0].x = dock;
      panes[0].w = left_w;
      panes[1].x = dock + left_w + gutter;
      panes[1].w = ww - left_w - gutter;
    } else {
      panes[0].x = dock;
      panes[0].w = ww;
      panes[1].x = dock;
      panes[1].w = 0;
    }
    // Clamp pane buffer indices if tabs were closed out from under them.
    for (int pi = 0; pi < 2; ++pi) {
      if (panes[pi].buf < 0 ||
          panes[pi].buf >= static_cast<int>(buffers.size())) {
        panes[pi].buf = tab_order.empty() ? 0 : tab_order.front();
      }
    }
    // Block heights depend on width; drop hints when this pane's width changed.
    for (int pi = 0; pi < 2; ++pi) {
      if (panes[pi].w != panes[pi].last_layout_pane_w) {
        panes[pi].height_cache.clear();
        panes[pi].last_layout_pane_w = panes[pi].w;
      }
    }
    auto layout_one = [&](Pane& pane) {
      try {
        xanatoo::ensure_valid_caret(doc_of(pane), pane.sel.focus);
        xanatoo::ensure_valid_caret(doc_of(pane), pane.sel.anchor);
        float view_h =
            static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
        if (view_h < 80.f) view_h = 80.f;
        xanatoo::LayoutViewport vp;
        vp.top = pane.scroll_y;
        vp.bottom = pane.scroll_y + view_h;
        vp.overscan = std::max(800.f, view_h * 1.5f);
        const std::unordered_map<xanatoo::NodeId, float>* hints =
            pane.height_cache.empty() ? nullptr : &pane.height_cache;
        pane.layout = layout_engine.layout(
            doc_of(pane), static_cast<float>(std::max(40, pane.w - kSbLayoutGutter)), base_of(pane), vp, hints,
            &folded_sections,
            active_focus_section());
        // Refresh height hints from measured (and skipped-stub) boxes.
        for (const auto& b : pane.layout.boxes) {
          if (b.node != xanatoo::INVALID_NODE && b.height > 0.f)
            pane.height_cache[b.node] = b.height;
        }
        if (pane.layout.virtualized) {
          pane.materialised_top = pane.layout.materialised_top;
          pane.materialised_bottom = pane.layout.materialised_bottom;
        } else if (vp.active()) {
          pane.materialised_top = vp.top - vp.overscan;
          pane.materialised_bottom = vp.bottom + vp.overscan;
        } else {
          pane.materialised_top = 0.f;
          pane.materialised_bottom = pane.layout.content_height;
        }
      } catch (const std::exception& e) {
        std::cerr << "pane layout failed: " << e.what() << "\n";
        pane.layout = {};
      }
    };
    layout_one(panes[0]);
    if (split) layout_one(panes[1]);
    needs_layout = false;
    last_layout_w = win_w;
    last_layout_h = win_h;
    refresh_diff();
  };



  // Re-layout when scroll leaves the materialised band (virtualization).
  auto ensure_virtual_layout = [&](Pane& p) {
    if (p.height_cache.empty()) return;
    float view_h =
        static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
    if (view_h < 80.f) view_h = 80.f;
    const float margin = 64.f;
    const float vis_top = p.scroll_y;
    const float vis_bot = p.scroll_y + view_h;
    if (vis_top < p.materialised_top + margin ||
        vis_bot > p.materialised_bottom - margin) {
      needs_layout = true;
    }
  };

  // Document Y of the caret for scrolling. Prefer glyphs; fall back to the
  // caret node's layout box, then any ancestor box (virtualized layouts often
  // only stub the Section, while the outline points at the Header child).
  auto caret_doc_y = [&](Pane& p) -> std::optional<float> {
    const float lh = static_cast<float>(body_font.line_height());
    auto cpos = xanatoo::caret_screen_pos(p.layout, p.sel.focus, 0.f, lh);
    if (cpos.valid) return cpos.y;
    const auto& d = doc_of(p);
    for (xanatoo::NodeId id = p.sel.focus.node;
         id != xanatoo::INVALID_NODE && id < d.next_id;
         id = d.get(id).parent) {
      for (const auto& box : p.layout.boxes) {
        if (box.node == id) return box.y;
      }
    }
    return std::nullopt;
  };

  // Materialise the whole pane once so off-screen outline targets get boxes.
  auto force_full_pane_layout = [&](Pane& p) {
    try {
      xanatoo::ensure_valid_caret(doc_of(p), p.sel.focus);
      xanatoo::ensure_valid_caret(doc_of(p), p.sel.anchor);
      p.layout = layout_engine.layout(
          doc_of(p), static_cast<float>(std::max(40, p.w - kSbLayoutGutter)),
          base_of(p), {}, nullptr, &folded_sections,
          active_focus_section());
      for (const auto& b : p.layout.boxes) {
        if (b.node != xanatoo::INVALID_NODE && b.height > 0.f)
          p.height_cache[b.node] = b.height;
      }
      p.materialised_top = 0.f;
      p.materialised_bottom = p.layout.content_height;
    } catch (const std::exception& e) {
      std::cerr << "force full layout failed: " << e.what() << "\n";
    }
  };

  auto scroll_caret_into_view = [&](Pane& p) {
    auto yopt = caret_doc_y(p);
    if (!yopt) {
      // Target not in the current (virtualized) band — full pass then retry.
      force_full_pane_layout(p);
      yopt = caret_doc_y(p);
    }
    if (!yopt) return;
    float doc_y = *yopt;
    float margin = 40.f;
    // Visible content height excludes menu/toolbar and status bar.
    float view_h = static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
    if (view_h < 80.f) view_h = 80.f;
    if (doc_y - p.scroll_y < margin) {
      p.scroll_y = std::max(0.f, doc_y - margin);
    } else if (doc_y - p.scroll_y > view_h - margin) {
      p.scroll_y = doc_y - (view_h - margin);
    }
    // After a long jump, re-virtualize around the new scroll position.
    needs_layout = true;
    ensure_virtual_layout(p);
  };

  // Outline / TOC navigation: pin the target near the top of the pane so the
  // section body is readable without scrolling up from a bottom-edge snap.
  // Prefer the layout *box top* (not caret baseline) so heading ascenders are
  // not clipped under the toolbar.
  auto scroll_caret_to_top = [&](Pane& p) {
    auto box_top_of = [&](xanatoo::NodeId id) -> std::optional<float> {
      if (id == xanatoo::INVALID_NODE) return std::nullopt;
      for (const auto& box : p.layout.boxes) {
        if (box.node == id) return box.y;
      }
      return std::nullopt;
    };

    auto resolve_top = [&]() -> std::optional<float> {
      const auto& d = doc_of(p);
      // Header/Heading box, then Section, then any ancestor box.
      if (auto y = box_top_of(p.sel.focus.node)) return y;
      xanatoo::NodeId sec =
          xanatoo::enclosing_section(d, p.sel.focus.node);
      if (auto y = box_top_of(sec)) return y;
      for (xanatoo::NodeId id = p.sel.focus.node;
           id != xanatoo::INVALID_NODE && id < d.next_id;
           id = d.get(id).parent) {
        if (auto y = box_top_of(id)) return y;
      }
      // Last resort: caret baseline minus roughly one line so glyphs fit.
      if (auto cy = caret_doc_y(p)) {
        const float lh = static_cast<float>(body_font.line_height());
        return std::max(0.f, *cy - lh);
      }
      return std::nullopt;
    };

    auto yopt = resolve_top();
    if (!yopt) {
      force_full_pane_layout(p);
      yopt = resolve_top();
    }
    if (!yopt) return;
    // Small pad so the box edge is not flush under the chrome.
    const float margin = 8.f;
    float view_h = static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
    if (view_h < 80.f) view_h = 80.f;
    float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
    p.scroll_y = std::max(0.f, std::min(max_s, *yopt - margin));
    needs_layout = true;
    ensure_virtual_layout(p);
  };



  // ---- Browse / gamepad navigation (no text entry) ----
  auto nav_page = [&](bool up) {
    Pane& p = F();
    const float view_h = static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
    const float step = view_h * 0.85f;
    float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
    if (up)
      p.scroll_y = std::max(0.f, p.scroll_y - step);
    else
      p.scroll_y = std::min(max_s, p.scroll_y + step);
    ensure_virtual_layout(p);
  };

  auto nav_move_block = [&](bool prev) {
    Pane& p = F();
    auto& d = doc_of(p);
    if (prev)
      xanatoo::move_to_prev_node(d, p.sel.focus);
    else
      xanatoo::move_to_next_node(d, p.sel.focus);
    p.sel.anchor = p.sel.focus;
    scroll_caret_into_view(p);
    reset_blink();
  };

  auto nav_move_line = [&](bool up) {
    Pane& p = F();
    auto& d = doc_of(p);
    if (up) {
      if (!xanatoo::move_up(p.layout, p.sel.focus, p.preferred_x))
        xanatoo::move_to_prev_node(d, p.sel.focus);
    } else {
      if (!xanatoo::move_down(p.layout, p.sel.focus, p.preferred_x))
        xanatoo::move_to_next_node(d, p.sel.focus);
    }
    p.sel.anchor = p.sel.focus;
    scroll_caret_into_view(p);
    reset_blink();
  };

  auto nav_header = [&](bool prev) {
    refresh_outline();
    if (outline.empty()) return;
    if (prev) {
      if (outline_sel > 0) --outline_sel;
    } else {
      if (outline_sel + 1 < static_cast<int>(outline.size())) ++outline_sel;
    }
    Pane& p = F();
    p.sel.focus.node = outline[static_cast<size_t>(outline_sel)].node;
    p.sel.focus.span_index = 0;
    p.sel.focus.char_index = 0;
    p.sel.anchor = p.sel.focus;
    scroll_caret_into_view(p);
    reset_blink();
  };

  auto toggle_stretch_at_caret = [&]() -> bool {
    Pane& p = F();
    auto& d = doc_of(p);
    for (xanatoo::NodeId id = p.sel.focus.node; id != xanatoo::INVALID_NODE;
         id = d.get(id).parent) {
      if (d.get(id).kind == xanatoo::NodeKind::Stretch) {
        d.get(id).expanded = !d.get(id).expanded;
        needs_layout = true;
        flash_status(d.get(id).expanded ? "Expanded" : "Collapsed");
        return true;
      }
    }
    // Also toggle Stretch under the caret's layout box.
    auto cpos = xanatoo::caret_screen_pos(
        p.layout, p.sel.focus, p.scroll_y,
        static_cast<float>(body_font.line_height()));
    if (cpos.valid) {
      for (const auto& box : p.layout.boxes) {
        if (box.node == xanatoo::INVALID_NODE) continue;
        if (d.get(box.node).kind != xanatoo::NodeKind::Stretch) continue;
        if (cpos.y + p.scroll_y < box.y || cpos.y + p.scroll_y > box.y + box.height)
          continue;
        d.get(box.node).expanded = !d.get(box.node).expanded;
        needs_layout = true;
        flash_status(d.get(box.node).expanded ? "Expanded" : "Collapsed");
        return true;
      }
    }
    return false;
  };

  auto collapse_or_expand_all = [&](bool expand) {
    auto& d = doc_of(F());
    for (auto& node : d.nodes) {
      if (node.kind == xanatoo::NodeKind::Stretch) node.expanded = expand;
    }
    needs_layout = true;
    flash_status(expand ? "Expanded all" : "Collapsed all");
  };

  auto find_help_path = [&]() -> std::string {
    const char* candidates[] = {
        "examples/help.md",
        "../examples/help.md",
        "../../examples/help.md",
        nullptr,
    };
    for (const char** c = candidates; *c; ++c) {
      if (fs::exists(*c)) return fs::absolute(*c).string();
    }
#ifdef __linux__
    try {
      fs::path exe = fs::read_symlink("/proc/self/exe");
      fs::path dir = exe.parent_path();
      for (const auto& c : {
               dir / "examples" / "help.md",
               dir / ".." / "examples" / "help.md",
               dir / ".." / "share" / "xanatoo" / "examples" / "help.md",
           }) {
        std::error_code ec;
        auto canon = fs::weakly_canonical(c, ec);
        if (!ec && fs::exists(canon)) return canon.string();
      }
    } catch (...) {
    }
#endif
    return {};
  };


  auto find_repo_file = [&](const char* name) -> std::string {
    // try relative paths with common prefixes
    const char* prefixes[] = {"", "examples/", "../", "../examples/", "../../",
                              "../../examples/", nullptr};
    for (const char** pre = prefixes; *pre; ++pre) {
      std::string pth = std::string(*pre) + name;
      if (fs::exists(pth)) return fs::absolute(pth).string();
    }
    // PortMaster / installed layout: XANATOO_DATA_DIR points at the port
    // folder that holds examples/, icons/, fonts/.
    if (const char* data = std::getenv("XANATOO_DATA_DIR")) {
      if (data[0] != '\0') {
        for (const auto& c : {
                 fs::path(data) / name,
                 fs::path(data) / "examples" / name,
             }) {
          std::error_code ec;
          auto canon = fs::weakly_canonical(c, ec);
          if (!ec && fs::exists(canon)) return canon.string();
        }
      }
    }
#ifdef __linux__
    try {
      fs::path exe = fs::read_symlink("/proc/self/exe");
      fs::path dir = exe.parent_path();
      for (const auto& c : {
               dir / name,
               dir / "examples" / name,
               dir / ".." / name,
               dir / ".." / "examples" / name,
               dir / ".." / "share" / "xanatoo" / "examples" / name,
           }) {
        std::error_code ec;
        auto canon = fs::weakly_canonical(c, ec);
        if (!ec && fs::exists(canon)) return canon.string();
      }
    } catch (...) {
    }
#endif
    return {};
  };

  // True for relative/local document links we should open in-pane
  // (not http(s)/mailto/gemini schemes).
  auto is_inapp_doc_href = [&](const std::string& href) -> bool {
    if (href.empty() || href[0] == '#') return false;
    if (is_in_app_remote_url(href)) return false;
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0 ||
        href.rfind("mailto:", 0) == 0)
      return false;
    // Scheme with :// → external; bare paths and file-looking names → in-app.
    if (href.find("://") != std::string::npos) return false;
    return true;
  };

  // Resolve a relative path against a document path (or the focused pane).
  auto resolve_doc_href = [&](const std::string& href,
                              const std::string& base_path = {}) -> std::string {
    if (href.empty()) return {};
    fs::path hp(href);
    if (hp.is_absolute()) return href;
    std::string base = base_path.empty() ? path_of(F()) : base_path;
    if (!base.empty() && !xanatoo::is_gemini_url(base)) {
      fs::path parent = fs::path(base).parent_path();
      if (!parent.empty()) {
        std::error_code ec;
        auto canon = fs::weakly_canonical(parent / href, ec);
        if (!ec) return canon.string();
        return (parent / href).string();
      }
    }
    // Fall back to repo search (examples/, cwd, …).
    std::string found = find_repo_file(href.c_str());
    if (!found.empty()) return found;
    return href;
  };

  // Load a gemini:// URL or local path into the focused pane.
  auto navigate_to_url = [&](std::string target,
                             bool record_history = true) -> bool {
    while (!target.empty() &&
           (target.back() == ' ' || target.back() == '\t'))
      target.pop_back();
    if (target.empty()) return false;
    Pane& p = F();

    auto load_into_pane = [&](xanatoo::Document doc, const std::string& label) {
      buffers[static_cast<size_t>(p.buf)].doc = std::move(doc);
      path_of(p) = label;
      buffers[static_cast<size_t>(p.buf)].dirty = false;
      xanatoo::Document& nd = buffers[static_cast<size_t>(p.buf)].doc;
      xanatoo::ensure_valid_caret(nd, p.sel.focus);
      p.sel.anchor = p.sel.focus;
      p.scroll_y = 0.f;
      // Drop stale layout boxes that still reference the previous tree —
      // otherwise the next frame can call doc.get() with dead node ids.
      p.layout = {};
      drop_height_hints_for_buf(p.buf);
      buffers[static_cast<size_t>(p.buf)].history.reset(nd, p.sel);
      if (panes[0].buf == panes[1].buf) {
        panes[1 - focus].sel = p.sel;
        panes[1 - focus].scroll_y = 0.f;
        panes[1 - focus].layout = {};
      }
      for (auto& sw : soft_wins)
        if (sw.buf == p.buf) {
          sw.needs_layout = true;
          sw.scroll = 0.f;
          sw.layout = {};
          xanatoo::ensure_valid_caret(nd, sw.sel.focus);
          sw.sel.anchor = sw.sel.focus;
        }
      needs_layout = true;
      update_title();
    };

    // Shared post-fetch path for Nostr / Reddit / Lemmy documents.
    auto finish_remote_open =
        [&](std::optional<xanatoo::Document> doc, const std::string& err,
            const char* service, std::string label, size_t label_max,
            const std::string& history_url) -> bool {
      if (!doc) {
        flash_status(std::string(service) + " failed: " + err);
        std::cerr << service << ": " << err << "\n";
        return false;
      }
      if (!allow_discard_dirty(p.buf)) return false;
      if (label.size() > label_max) label = label.substr(0, label_max) + "…";
      load_into_pane(std::move(*doc), label);
      if (std::find(tab_order.begin(), tab_order.end(), p.buf) ==
          tab_order.end())
        tab_order.push_back(p.buf);
      if (record_history) push_nav_history(history_url);
      flash_status(std::string("Opened ") + service);
      return true;
    };

    if (xanatoo::is_gemini_url(target)) {
#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
      flash_status("Fetching " + target + "…");
      std::string err;
      auto resp = xanatoo::gemini_fetch(target, &err);
      if (!resp) {
        flash_status("Gemini failed: " + err);
        std::cerr << "Gemini: " << err << "\n";
        return false;
      }
      if (resp->status == 10 || resp->status == 11) {
        // Input request — open URL bar with query for the user to complete.
        url_bar_mode = true;
        minibuf_begin(url_bar_buf);
        url_bar_buf = resp->final_url;
        if (!url_bar_buf.empty() && url_bar_buf.back() != '?')
          url_bar_buf.push_back('?');
        flash_status(std::string(resp->status == 11 ? "Sensitive input: "
                                                    : "Input: ") +
                     resp->meta);
        return false;
      }
      if (resp->status < 20 || resp->status >= 30) {
        flash_status("Gemini status " + std::to_string(resp->status) + " " +
                     resp->meta);
        return false;
      }
      if (!allow_discard_dirty(p.buf)) return false;
      std::string meta = resp->meta;
      for (char& c : meta)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      // Drop parameters: "text/gemini; charset=utf-8"
      std::string mime = meta;
      size_t semi = mime.find(';');
      if (semi != std::string::npos) mime.resize(semi);
      while (!mime.empty() && mime.back() == ' ') mime.pop_back();

      xanatoo::Document gdoc;
      if (mime.find("text/markdown") != std::string::npos ||
          mime.find("text/x-markdown") != std::string::npos) {
        gdoc = xanatoo::parse_document(resp->body, "remote.md");
      } else if (mime.empty() || mime.find("text/gemini") != std::string::npos ||
                 mime.find("text/plain") != std::string::npos) {
        gdoc = xanatoo::parse_gemtext(resp->body);
      } else {
        // Binary or unknown — show a short notice instead of mojibake.
        std::string notice =
            "# Unsupported media\n\n"
            "Gemini returned MIME type `" + resp->meta + "` (" +
            std::to_string(resp->body.size()) +
            " bytes). Only text/gemini, text/plain, and "
            "text/markdown are rendered in-pane.\n";
        gdoc = xanatoo::parse_gemtext(notice);
      }
      gemini_current_url = resp->final_url;
      load_into_pane(std::move(gdoc), resp->final_url);
      browse_mode = true;
      if (record_history) push_nav_history(resp->final_url);
      flash_status("Gemini " + std::to_string(resp->status) + " " +
                   resp->final_url);
      return true;
#else
      // No TLS client in this build — still offer the local gemtext sample.
      flash_status("gemini:// needs TLS (build with -DXANATOO_GEMINI=ON)");
      {
        std::string sample = find_repo_file("gemini-sample.gmi");
        if (sample.empty())
          sample = find_repo_file("examples/gemini-sample.gmi");
        if (!sample.empty() && fs::exists(sample)) {
          // Fall through to local open of the sample after the message.
          target = sample;
        } else {
          return false;
        }
      }
#endif
    }


    // Nostr note / nevent (read-only thread view)
    if (xanatoo::is_nostr_ref(target)) {
#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
      flash_status("Fetching Nostr " + target.substr(0, 24) + "…");
      std::string err;
      auto doc = xanatoo::nostr_fetch_thread(target, &err);
      return finish_remote_open(std::move(doc), err, "Nostr", target, 64,
                                target);
#else
      flash_status("Nostr needs build with -DXANATOO_NOSTR=ON");
      return false;
#endif
    }

    // Reddit listing / post via public .json API
    if (xanatoo::is_reddit_url(target)) {
#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
      flash_status("Fetching Reddit…");
      std::string err;
      auto doc = xanatoo::reddit_fetch(target, &err);
      return finish_remote_open(std::move(doc), err, "Reddit", target, 72,
                                target);
#else
      flash_status("Reddit needs build with -DXANATOO_REDDIT=ON");
      return false;
#endif
    }

    // Lemmy community / post via API v3
    if (xanatoo::is_lemmy_url(target)) {
#if defined(XANATOO_LEMMY) && XANATOO_LEMMY
      flash_status("Fetching Lemmy…");
      std::string err;
      auto doc = xanatoo::lemmy_fetch(target, &err);
      return finish_remote_open(std::move(doc), err, "Lemmy", target, 72,
                                target);
#else
      flash_status("Lemmy needs build with -DXANATOO_LEMMY=ON");
      return false;
#endif
    }

    // Local path (or offline fallback for gemini:// when TLS is disabled)
    std::string path = target;
    if (!fs::exists(path)) {
      std::string found = find_repo_file(path.c_str());
      if (!found.empty()) path = found;
    }
    if (!fs::exists(path)) {
      flash_status("Not found: " + target);
      return false;
    }
    gemini_current_url.clear();
    // Prefer an already-open buffer for this path (avoid double-load).
    {
      std::string canon = path;
      try { canon = fs::weakly_canonical(path).string(); } catch (...) {}
      for (int i = 0; i < static_cast<int>(buffers.size()); ++i) {
        if (buffers[static_cast<size_t>(i)].path.empty()) continue;
        std::string bp = buffers[static_cast<size_t>(i)].path;
        try { bp = fs::weakly_canonical(bp).string(); } catch (...) {}
        if (bp == canon) {
          p.buf = i;
          if (std::find(tab_order.begin(), tab_order.end(), i) == tab_order.end())
            tab_order.push_back(i);
          xanatoo::ensure_valid_caret(doc_of(p), p.sel.focus);
          p.sel.anchor = p.sel.focus;
          p.layout = {};
          needs_layout = true;
          update_title();
          if (record_history) push_nav_history(path);
          flash_status("Switched to " + path);
          return true;
        }
      }
    }
    if (!allow_discard_dirty(p.buf)) return false;
    {
      const bool was_epub = xanatoo::is_epub_path(path);
      std::string open_path = resolve_open_path(path);
      if (open_path.empty()) {
        flash_status("EPUB import failed (need pandoc on PATH)");
        return false;
      }
      load_into_pane(load_doc(open_path), open_path);
      if (std::find(tab_order.begin(), tab_order.end(), p.buf) == tab_order.end())
        tab_order.push_back(p.buf);
      if (record_history) push_nav_history(open_path);
      flash_status(was_epub ? ("Imported EPUB → " + open_path)
                            : ("Opened " + open_path));
    }
    return true;
  };


  auto open_transclude_in_other_pane = [&]() {
    auto& p = F();
    const auto& d = doc_of(p);
    xanatoo::NodeId id = p.sel.focus.node;
    // Walk up to a Transclude node under the caret.
    xanatoo::NodeId tid = xanatoo::INVALID_NODE;
    for (xanatoo::NodeId cur = id; cur != xanatoo::INVALID_NODE;
         cur = d.get(cur).parent) {
      if (d.get(cur).kind == xanatoo::NodeKind::Transclude) {
        tid = cur;
        break;
      }
    }
    if (tid == xanatoo::INVALID_NODE) {
      // Fallback: first visible transclude under caret y — skip, require caret
      flash_status("No include under caret");
      return;
    }
    const auto& tn = d.get(tid);
    xanatoo::NodeId local = xanatoo::resolve_transclude_local(d, tn.src);
    if (!split) {
      split = true;
      needs_layout = true;
    }
    int other = 1 - focus;
    if (local != xanatoo::INVALID_NODE) {
      panes[other].buf = p.buf;  // same doc
      panes[other].sel.focus = {local, 0, 0};
      panes[other].sel.anchor = panes[other].sel.focus;
      scroll_caret_into_view(panes[other]);
      flash_status("Include → other pane: " + tn.src);
      return;
    }
    // External path
    std::string path_part = tn.src;
    size_t hash = path_part.find('#');
    std::string anchor;
    if (hash != std::string::npos) {
      anchor = path_part.substr(hash);
      path_part = path_part.substr(0, hash);
    }
    if (path_part.empty()) {
      flash_status("Broken include: " + tn.src);
      return;
    }
    std::filesystem::path resolved = path_part;
    std::string base = path_of(p);
    if (!base.empty() && resolved.is_relative())
      resolved = std::filesystem::path(base).parent_path() / path_part;
    if (!std::filesystem::exists(resolved)) {
      flash_status("Broken include (missing file): " + resolved.string());
      return;
    }
    // Load into other pane's buffer if possible — navigate_to_url uses focus.
    int saved = focus;
    focus = other;
    navigate_to_url(resolved.string() + (anchor.empty() ? "" : anchor));
    if (!anchor.empty()) {
      if (auto dest = xanatoo::jump_to_href(doc_of(panes[other]), anchor)) {
        panes[other].sel.focus = *dest;
        panes[other].sel.anchor = *dest;
        scroll_caret_into_view(panes[other]);
      }
    }
    focus = saved;
    flash_status("Opened include in other pane: " + resolved.string());
  };



  auto activate_at_caret = [&]() {
    // Prefer stretch toggle; else try internal link under caret glyph.
    if (toggle_stretch_at_caret()) return;
    Pane& p = F();
    auto& d = doc_of(p);
    auto cpos = xanatoo::caret_screen_pos(
        p.layout, p.sel.focus, p.scroll_y,
        static_cast<float>(body_font.line_height()));
    if (!cpos.valid) return;
    if (auto href = xanatoo::hit_test_link(d, p.layout, cpos.x, cpos.y,
                                           p.scroll_y)) {
      if (auto dest = xanatoo::jump_to_href(d, *href)) {
        expand_ancestors_for_node(dest->node);
        p.sel.focus = *dest;
        p.sel.anchor = p.sel.focus;
        scroll_caret_into_view(p);
        flash_status("Followed " + *href);
        reset_blink();
      } else if (!href->empty() && (*href)[0] != '#') {
        std::string abs = *href;
        if (!xanatoo::is_gemini_url(abs) && !gemini_current_url.empty()) {
          std::string resolved =
              xanatoo::resolve_gemini_url(gemini_current_url, abs);
          if (!resolved.empty()) abs = resolved;
        }
        if (is_in_app_remote_url(abs)) {
          navigate_to_url(std::move(abs));
        } else if (is_inapp_doc_href(abs)) {
          navigate_to_url(resolve_doc_href(abs));
        } else {
          xanatoo::open_external_url(*href);
          flash_status("Opening " + *href);
        }
      }
    }
  };


  // CLI / argv may pass gemini:// / reddit / nostr — fetch after helpers exist
  if (!buffers.empty() && is_in_app_remote_url(buffers[0].path)) {
    std::string u = buffers[0].path;
    buffers[0].path.clear();
    navigate_to_url(std::move(u));
  } else if (buffers.size() > 1 && is_in_app_remote_url(buffers[1].path)) {
    std::string u = buffers[1].path;
    buffers[1].path.clear();
    focus = 1;
    navigate_to_url(std::move(u));
    focus = 0;
  }



  auto soft_client_rect = [&](const SoftWindow& sw) {
    return std::tuple{sw.x + help_pad, sw.y + help_title_h + 4,
                      sw.w - help_pad * 2,
                      sw.h - help_title_h - help_pad - 4};
  };

  // Keep floating windows fully in the client area (below menu/toolbar/tabs,
  // above status bar). Title bar must stay reachable to move/close.
  auto snap_soft_geom = [&](int& x, int& y, int w, int h) {
    const int top = static_cast<int>(ui_top);
    const int bottom = win_h - status_bar_h;
    // Title bar (help_title_h) must remain below the chrome.
    if (y < top) y = top;
    if (x < 0) x = 0;
    if (w > win_w) w = win_w;  // caller may not use w out-param; size clamped elsewhere
    if (x + w > win_w) x = std::max(0, win_w - w);
    // Prefer keeping the title bar on-screen even if the body is tall.
    if (y + help_title_h > bottom)
      y = std::max(top, bottom - help_title_h);
    else if (y + h > bottom)
      y = std::max(top, bottom - h);
    // Edge snap (optional stickiness near edges)
    if (x < soft_snap_px) x = 0;
    if (y < top + soft_snap_px) y = top;
    if (x + w > win_w - soft_snap_px) x = std::max(0, win_w - w);
    if (y + h > bottom - soft_snap_px) y = std::max(top, bottom - h);
  };

  auto soft_client_bounds = [&]() {
    const int top = static_cast<int>(ui_top);
    const int bottom = win_h - status_bar_h;
    return std::tuple{top, bottom, std::max(160, bottom - top)};
  };

  // Which tile zone is under the pointer (title-bar drag).
  auto compute_dock_zone = [&](int mx, int my) -> SoftDock {
    auto [top, bottom, ch] = soft_client_bounds();
    (void)ch;
    if (my < top || my > bottom) return SoftDock::None;
    // Corners prefer left/right over top/bottom for split layouts.
    if (mx < soft_dock_zone) return SoftDock::Left;
    if (mx > win_w - soft_dock_zone) return SoftDock::Right;
    // Top-center → maximize; top sides already handled as L/R
    if (my < top + soft_dock_zone) {
      if (mx > win_w / 4 && mx < (win_w * 3) / 4) return SoftDock::Max;
      return SoftDock::Top;
    }
    if (my > bottom - soft_dock_zone) return SoftDock::Bottom;
    return SoftDock::None;
  };

  auto dock_rect = [&](SoftDock d) -> std::tuple<int, int, int, int> {
    auto [top, bottom, ch] = soft_client_bounds();
    switch (d) {
      case SoftDock::Left:
        return {0, top, std::max(200, win_w / 2), ch};
      case SoftDock::Right: {
        int x = win_w / 2;
        return {x, top, win_w - x, ch};
      }
      case SoftDock::Top:
        return {0, top, win_w, std::max(120, ch / 2)};
      case SoftDock::Bottom: {
        int h = std::max(120, ch / 2);
        return {0, bottom - h, win_w, h};
      }
      case SoftDock::Max:
        return {0, top, win_w, ch};
      default:
        return {0, top, 0, 0};
    }
  };

  auto apply_soft_dock = [&](SoftWindow& sw, SoftDock d) {
    if (d == SoftDock::None) return;
    // Save floating geometry once when leaving free placement
    if (sw.dock == SoftDock::None && !sw.maximized) {
      sw.restore_x = sw.x;
      sw.restore_y = sw.y;
      sw.restore_w = sw.w;
      sw.restore_h = sw.h;
    }
    auto [x, y, w, h] = dock_rect(d);
    sw.x = x;
    sw.y = y;
    sw.w = w;
    sw.h = h;
    sw.dock = d;
    sw.maximized = (d == SoftDock::Max);
    sw.needs_layout = true;
  };

  auto restore_soft_float = [&](SoftWindow& sw, int mx, int my) {
    if (sw.dock == SoftDock::None && !sw.maximized) return;
    sw.w = sw.restore_w;
    sw.h = sw.restore_h;
    sw.x = mx - sw.w / 2;
    sw.y = my - help_title_h / 2;
    if (sw.x < 0) sw.x = 0;
    if (sw.y < static_cast<int>(ui_top)) sw.y = static_cast<int>(ui_top);
    sw.dock = SoftDock::None;
    sw.maximized = false;
    sw.needs_layout = true;
  };

  auto maximize_soft = [&](SoftWindow& sw) {
    if (!sw.maximized && sw.dock == SoftDock::None) {
      sw.restore_x = sw.x;
      sw.restore_y = sw.y;
      sw.restore_w = sw.w;
      sw.restore_h = sw.h;
      apply_soft_dock(sw, SoftDock::Max);
    } else if (sw.maximized || sw.dock != SoftDock::None) {
      sw.x = sw.restore_x;
      sw.y = sw.restore_y;
      sw.w = sw.restore_w;
      sw.h = sw.restore_h;
      sw.maximized = false;
      sw.dock = SoftDock::None;
      sw.needs_layout = true;
      snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
    }
  };

  // Re-clamp every frame so windows cannot sit under menu/toolbar/tabs when
  // ui_top grows (e.g. tab bar appears) or after undock/restore.
  auto clamp_all_floating = [&]() {
    const int top = static_cast<int>(ui_top);
    for (auto& sw : soft_wins) {
      if (sw.dock != SoftDock::None || sw.maximized) {
        SoftDock d = sw.maximized ? SoftDock::Max : sw.dock;
        auto [x, y, w, h] = dock_rect(d);
        // Only rewrite geometry when chrome/size changed (avoids needs_layout spam).
        if (sw.x != x || sw.y != y || sw.w != w || sw.h != h) {
          sw.x = x;
          sw.y = y;
          sw.w = w;
          sw.h = h;
          sw.needs_layout = true;
        }
        continue;
      }
      snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
      if (sw.y < top) sw.y = top;
    }
    snap_soft_geom(help_x, help_y, help_w, help_h);
    if (help_y < top) help_y = top;
    snap_soft_geom(ol_x, ol_y, ol_w, ol_h);
    if (ol_y < top) ol_y = top;
  };

  auto soft_doc = [&](SoftWindow& sw) -> xanatoo::Document& {
    if (buffers.empty()) {
      DocBuffer empty;
      empty.doc = xanatoo::parse_document(xanatoo::fallback_document(), {});
      buffers.push_back(std::move(empty));
      tab_order.push_back(0);
      panes[0].buf = panes[1].buf = 0;
      buffers[0].history.reset(buffers[0].doc, panes[0].sel);
    }
    if (sw.buf < 0 || sw.buf >= static_cast<int>(buffers.size()))
      return buffers[0].doc;
    return buffers[static_cast<size_t>(sw.buf)].doc;
  };
  auto soft_path = [&](const SoftWindow& sw) -> const std::string& {
    if (sw.buf < 0 || sw.buf >= static_cast<int>(buffers.size())) {
      static const std::string empty;
      return empty;
    }
    return buffers[static_cast<size_t>(sw.buf)].path;
  };
  auto soft_title = [&](const SoftWindow& sw) -> std::string {
    if (!sw.title.empty()) return sw.title;
    const auto& path = soft_path(sw);
    if (path.empty()) return "untitled";
    try {
      return fs::path(path).filename().string();
    } catch (...) {
      return path;
    }
  };

  auto layout_soft = [&](SoftWindow& sw) {
    float mw = static_cast<float>(std::max(120, sw.w - help_pad * 2));
    std::string base = parent_dir(soft_path(sw));
    try {
      xanatoo::Document& d = soft_doc(sw);
      xanatoo::ensure_valid_caret(d, sw.sel.focus);
      sw.sel.anchor = sw.sel.focus;
      sw.layout = layout_engine.layout(d, mw, base);
    } catch (const std::exception& e) {
      std::cerr << "soft layout failed: " << e.what() << "\n";
      sw.layout = {};
    }
    sw.needs_layout = false;
  };

  // Raise window i to the top of the stack (back of vector).
  auto raise_soft = [&](size_t i) {
    if (i + 1 >= soft_wins.size()) return;
    SoftWindow sw = std::move(soft_wins[i]);
    soft_wins.erase(soft_wins.begin() + static_cast<std::ptrdiff_t>(i));
    soft_wins.push_back(std::move(sw));
  };

  auto canon_path = [&](const std::string& path) -> std::string {
    try {
      return fs::weakly_canonical(path).string();
    } catch (...) {
      return path;
    }
  };

  // Find buffer with same path, or -1.
  auto find_buffer = [&](const std::string& path) -> int {
    if (path.empty()) return -1;
    std::string c = canon_path(path);
    for (int i = 0; i < static_cast<int>(buffers.size()); ++i) {
      if (buffers[static_cast<size_t>(i)].path.empty()) continue;
      if (canon_path(buffers[static_cast<size_t>(i)].path) == c) return i;
    }
    return -1;
  };

  // Load path into a new or existing buffer; returns buffer index.
  auto ensure_buffer = [&](const std::string& path) -> int {
    int bi = find_buffer(path);
    if (bi >= 0) {
      if (std::find(tab_order.begin(), tab_order.end(), bi) == tab_order.end())
        tab_order.push_back(bi);
      return bi;
    }
    DocBuffer b;
    b.path = path;
    b.doc = load_doc(path);
    bi = static_cast<int>(buffers.size());
    buffers.push_back(std::move(b));
    tab_order.push_back(bi);
    update_title();
    return bi;
  };

  // Pop focused pane buffer into a floating window (shared buffer).
  auto pop_out_focused = [&]() {
    Pane& p = F();
    int bi = p.buf;
    for (size_t i = 0; i < soft_wins.size(); ++i) {
      if (soft_wins[i].buf == bi) {
        raise_soft(i);
        flash_status("Already in a window");
        return;
      }
    }
    SoftWindow sw;
    sw.buf = bi;
    sw.sel = p.sel;
    sw.scroll = p.scroll_y;
    sw.needs_layout = true;
    const int cascade = static_cast<int>(soft_wins.size()) * 28;
    const int top = static_cast<int>(ui_top);
    sw.x = help_x + 30 + cascade;
    sw.y = std::max(help_y, top) + 30 + cascade;
    if (sw.x + sw.w > win_w - 20) sw.x = 40 + (cascade % 60);
    if (sw.y < top) sw.y = top + 20 + (cascade % 40);
    if (sw.y + sw.h > win_h - 40)
      sw.y = top + 20 + (cascade % 40);
    snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
    soft_wins.push_back(std::move(sw));
    flash_status("Popped out to window");
  };

  // Dock a floating window back into the focused pane as a tab.
  auto pop_in_soft = [&](size_t si) {
    if (si >= soft_wins.size()) return;
    int bi = soft_wins[si].buf;
    if (bi < 0 || bi >= static_cast<int>(buffers.size())) {
      soft_wins.erase(soft_wins.begin() + static_cast<std::ptrdiff_t>(si));
      return;
    }
    panes[focus].buf = bi;
    panes[focus].sel = soft_wins[si].sel;
    panes[focus].scroll_y = soft_wins[si].scroll;
    xanatoo::ensure_valid_caret(doc_of(panes[focus]), panes[focus].sel.focus);
    panes[focus].sel.anchor = panes[focus].sel.focus;
    panes[focus].layout = {};
    soft_wins.erase(soft_wins.begin() + static_cast<std::ptrdiff_t>(si));
    needs_layout = true;
    update_title();
    flash_status("Docked to tab");
  };

  auto open_browse = [&](const std::string& path, const std::string& title) {
    if (path.empty() || !fs::exists(path)) {
      std::cerr << "Cannot open in window: " << path << "\n";
      return;
    }
    int bi = ensure_buffer(path);
    // Focus existing floating window for this buffer.
    for (size_t i = 0; i < soft_wins.size(); ++i) {
      if (soft_wins[i].buf == bi) {
        raise_soft(i);
        std::cout << "Software window (focus): " << path << "\n";
        return;
      }
    }
    SoftWindow sw;
    sw.buf = bi;
    if (!title.empty()) sw.title = title;
    sw.needs_layout = true;
    sw.scroll = 0.f;
    xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(bi)].doc, sw.sel.focus);
    sw.sel.anchor = sw.sel.focus;
    const int cascade = static_cast<int>(soft_wins.size()) * 28;
    const int top = static_cast<int>(ui_top);
    sw.x = help_x + 30 + cascade;
    sw.y = std::max(help_y, top) + 30 + cascade;
    if (sw.x + sw.w > win_w - 20) sw.x = 40 + (cascade % 60);
    if (sw.y < top) sw.y = top + 20 + (cascade % 40);
    if (sw.y + sw.h > win_h - 40)
      sw.y = top + 20 + (cascade % 40);
    snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
    soft_wins.push_back(std::move(sw));
    std::cout << "Software window: " << path << "\n";
  };

  auto ensure_help_doc = [&]() {
    if (help_loaded) return;
    std::string hp = find_help_path();
    if (!hp.empty()) {
      help_doc = load_doc(hp);
      std::cout << "Help document: " << hp << "\n";
    } else {
      // Minimal built-in so F1 always works offline
      help_doc = xanatoo::parse_document(
          "# xanatoo help\n\n"
          "Place `examples/help.md` next to the binary for the full reference.\n\n"
          "Press **F1** or **Esc** to close.\n",
          {});
      std::cerr << "examples/help.md not found; using short built-in help.\n";
    }
    help_loaded = true;
    help_needs_layout = true;
    help_scroll = 0.f;
  };

  auto layout_help = [&]() {
    if (!help_loaded) return;
    float mw = static_cast<float>(std::max(120, help_w - help_pad * 2));
    help_layout = layout_engine.layout(help_doc, mw);
    help_needs_layout = false;
  };

  auto help_client_rect = [&]() {
    // x,y,w,h of document client area in window coords
    return std::tuple{help_x + help_pad, help_y + help_title_h + 4,
                      help_w - help_pad * 2,
                      help_h - help_title_h - help_pad - 4};
  };

  auto jump_outline_sel = [&]() {
    if (outline.empty() || outline_sel < 0 ||
        outline_sel >= static_cast<int>(outline.size()))
      return;
    Pane& p = F();
    const auto& er = outline[static_cast<size_t>(outline_sel)];
    xanatoo::Document& d = doc_of(p);
    // Prefer the title Header; if the row only has a Section id (no header
    // child), land on the first text node under that section so
    // ensure_valid_caret does not relocate to the top of the document.
    xanatoo::NodeId dest = er.node;
    if (dest != xanatoo::INVALID_NODE && dest < d.next_id) {
      const auto& n = d.get(dest);
      if (n.id != dest || !xanatoo::is_text_node_kind(n.kind))
        dest = xanatoo::INVALID_NODE;
    } else {
      dest = xanatoo::INVALID_NODE;
    }
    if (dest == xanatoo::INVALID_NODE && er.section != xanatoo::INVALID_NODE) {
      std::vector<xanatoo::NodeId> texts;
      xanatoo::collect_text_nodes(d, er.section, texts);
      if (!texts.empty()) dest = texts.front();
      else dest = er.section;
    }
    if (dest == xanatoo::INVALID_NODE) return;
    // Keep section-focus filter in sync with outline selection.
    if (section_focus_mode) {
      xanatoo::NodeId sec = er.section;
      if (sec == xanatoo::INVALID_NODE) {
        sec = xanatoo::enclosing_section(d, dest);
      }
      if (sec != section_focus_id) {
        section_focus_id = sec;
        drop_height_hints_for_buf(p.buf);
        needs_layout = true;
      }
    }
    expand_ancestors_for_node(dest);
    if (needs_layout) layout_panes();
    p.sel.focus = {dest, 0, 0};
    xanatoo::ensure_valid_caret(d, p.sel.focus);
    p.sel.anchor = p.sel.focus;
    // Outline jumps need a complete materialisation so box tops exist for
    // the target (virtualized bands often omit it until after scroll).
    if (!p.layout.boxes.empty()) {
      bool have_box = false;
      for (const auto& b : p.layout.boxes) {
        if (b.node == p.sel.focus.node) {
          have_box = true;
          break;
        }
      }
      if (!have_box) force_full_pane_layout(p);
    } else {
      force_full_pane_layout(p);
    }
    scroll_caret_to_top(p);
    reset_blink();
  };

  auto pane_at = [&](float mx) -> int {
    if (!split) return 0;
    return (mx >= static_cast<float>(panes[1].x)) ? 1 : 0;
  };

  auto local_x = [&](float mx, int pi) {
    return mx - static_cast<float>(panes[pi].x);
  };

  // Window Y → content Y (0 at top of document viewport, below menu/toolbar).
  // Hit-tests compare against glyph.y - scroll_y in this space.
  auto content_y = [&](float window_y) { return window_y - ui_top; };

  auto outline_client_rect = [&]() {
    if (outline_docked && outline_mode) {
      const int top = static_cast<int>(ui_top);
      const int h = win_h - status_bar_h - top;
      return std::tuple{help_pad, top + help_title_h + 4,
                        outline_dock_w - help_pad * 2,
                        h - help_title_h - help_pad - 4};
    }
    return std::tuple{ol_x + help_pad, ol_y + help_title_h + 4,
                      ol_w - help_pad * 2,
                      ol_h - help_title_h - help_pad - 4};
  };
  auto outline_row_step = [&]() {
    return static_cast<float>(std::max(16, body_font.line_height() + 4));
  };

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ui.process_event(ev);
      // Let ImGui consume mouse/keyboard when interacting with menus
      const bool imgui_mouse = ui.want_capture_mouse();
      const bool imgui_keys = ui.want_capture_keyboard();
      switch (ev.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_WINDOWEVENT:
          if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
              ev.window.event == SDL_WINDOWEVENT_RESIZED) {
            const int nw = ev.window.data1;
            const int nh = ev.window.data2;
            if (nw >= 64 && nh >= 64) {
              win_w = nw;
              win_h = nh;
              // Keep tiled/maximized soft windows filling the new client area.
              for (auto& sw : soft_wins) {
                if (sw.dock != SoftDock::None || sw.maximized)
                  apply_soft_dock(sw, sw.maximized ? SoftDock::Max : sw.dock);
              }
              needs_layout = true;
            }
          }
          break;

        case SDL_DROPFILE: {
          // Drag-and-drop: documents open in a soft window; images insert at caret.
          char* dropped = ev.drop.file;
          if (!dropped) break;
          std::string fpath(dropped);
          SDL_free(dropped);

          auto lower_ext = [](std::string p) {
            for (char& c : p)
              if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return p;
          };
          const std::string low = lower_ext(fpath);
          const bool is_doc =
              low.size() >= 3 &&
              (xanatoo::ends_with(low, ".md") || xanatoo::ends_with(low, ".markdown") ||
               xanatoo::ends_with(low, ".sexp") || xanatoo::ends_with(low, ".txt") ||
               xanatoo::ends_with(low, ".org") || xanatoo::ends_with(low, ".gmi") ||
               xanatoo::ends_with(low, ".gemini") || xanatoo::ends_with(low, ".epub"));

          if (is_doc) {
            if (!std::filesystem::exists(fpath)) {
              std::cerr << "Drop: file not found: " << fpath << "\n";
              break;
            }
            std::string title;
            try {
              title = std::filesystem::path(fpath).filename().string();
            } catch (...) {
              title = fpath;
            }
            open_browse(fpath, title);
            std::cout << "Opened dropped document: " << fpath << "\n";
            break;
          }

          if (!xanatoo::is_raster_image_path(fpath)) {
            std::cerr << "Drop ignored (not an image or document): " << fpath
                      << "\n";
            break;
          }

          int mx = 0, my_raw = 0;
          SDL_GetMouseState(&mx, &my_raw);
          if (static_cast<float>(my_raw) < ui_top) break;
          if (my_raw >= win_h - status_bar_h) break;

          focus = pane_at(static_cast<float>(mx));
          Pane& p = F();
          auto& d = doc_of(p);
          float lx = local_x(static_cast<float>(mx), focus);
          float my = content_y(static_cast<float>(my_raw));

          // Place caret at drop location when possible; otherwise keep current.
          if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
            p.sel.focus = *hit;
            p.sel.anchor = p.sel.focus;
          }
          xanatoo::ensure_valid_caret(d, p.sel.focus);
          if (p.sel.focus.node == xanatoo::INVALID_NODE) {
            std::cerr << "Drop: no place to insert image\n";
            break;
          }

          // Prefer a path relative to the document directory for portable markup.
          std::string store = fpath;
          std::string base = parent_dir(path_of(p));
          if (!base.empty()) {
            try {
              std::filesystem::path rel =
                  std::filesystem::relative(fpath, base);
              if (!rel.empty() && rel.native().find("..") == std::string::npos)
                store = rel.generic_string();
            } catch (...) {
            }
          }
          std::string alt;
          try {
            alt = std::filesystem::path(fpath).stem().string();
          } catch (...) {
            alt = store;
          }

          edit();
          if (xanatoo::insert_image(d, p.sel.focus, store, alt)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
            reset_blink();
            std::cout << "Dropped image: " << store << "\n";
          } else {
            std::cerr << "Drop: could not insert image at caret\n";
          }
          break;
        }

        case SDL_MOUSEWHEEL: {
          int mxw = 0, myw = 0;
          SDL_GetMouseState(&mxw, &myw);
          // Soft windows first (match paint z-order: outline → browse → help).
          if (outline_mode && mxw >= ol_x && mxw < ol_x + ol_w &&
              myw >= ol_y && myw < ol_y + ol_h) {
            ol_scroll -= static_cast<float>(ev.wheel.y) * 40.f;
            if (ol_scroll < 0.f) ol_scroll = 0.f;
            break;
          }
          {
            bool handled = false;
            for (int i = static_cast<int>(soft_wins.size()) - 1; i >= 0; --i) {
              SoftWindow& sw = soft_wins[static_cast<size_t>(i)];
              if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
                  myw < sw.y + sw.h) {
                sw.scroll -= static_cast<float>(ev.wheel.y) * 40.f;
                if (sw.scroll < 0.f) sw.scroll = 0.f;
                handled = true;
                break;
              }
            }
            if (handled) break;
          }
          if (help_mode && mxw >= help_x && mxw < help_x + help_w &&
              myw >= help_y && myw < help_y + help_h) {
            help_scroll -= static_cast<float>(ev.wheel.y) * 48.f;
            if (help_scroll < 0.f) help_scroll = 0.f;
            break;
          }
          if (imgui_mouse) break;
          if (myw >= win_h - status_bar_h) break;
          int pi = pane_at(static_cast<float>(mxw));
          Pane& p = panes[pi];
          p.scroll_y -= static_cast<float>(ev.wheel.y) * 48.f;
          p.scroll_y = std::max(0.f, p.scroll_y);
          {
            float view_h =
                static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
            float max_s =
                std::max(0.f, p.layout.content_height - view_h + 40.f);
            if (p.scroll_y > max_s) p.scroll_y = max_s;
          }
          ensure_virtual_layout(p);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
          // Menu/toolbar strip belongs to ImGui; status bar is not document area.
          if (static_cast<float>(ev.button.y) < ui_top) break;
          if (ev.button.y >= win_h - status_bar_h) break;

          if (ev.button.button == SDL_BUTTON_LEFT) {
            const int mxw = ev.button.x, myw = ev.button.y;

            // Software windows are custom-drawn (not ImGui). Hit-test them
            // before the ImGui capture check and before the document panes.
            // Z-order matches paint order: outline → browse → help.

            // ---- Backlinks software window ----
            if (backlinks_mode &&
                mxw >= bl_x - help_edge && mxw <= bl_x + bl_w + help_edge &&
                myw >= bl_y - help_edge && myw <= bl_y + bl_h + help_edge) {
              const bool inside =
                  mxw >= bl_x && mxw < bl_x + bl_w && myw >= bl_y &&
                  myw < bl_y + bl_h;
              if (inside && myw < bl_y + help_title_h) {
                if (mxw >= bl_x + bl_w - 28) {
                  backlinks_mode = false;
                  bl_dragging = false;
                } else {
                  bl_dragging = true;
                  bl_drag_dx = mxw - bl_x;
                  bl_drag_dy = myw - bl_y;
                }
                break;
              }
              if (inside) {
                const int title_h = help_title_h;
                const int cy = bl_y + title_h + 4;
                const float step = 22.f;
                int row = static_cast<int>(
                    (static_cast<float>(myw - cy) + bl_scroll) / step);
                if (row >= 0 && row < static_cast<int>(backlinks.size())) {
                  backlinks_sel = row;
                  auto& pp = F();
                  pp.sel.focus = {
                      backlinks[static_cast<size_t>(row)].source, 0, 0};
                  pp.sel.anchor = pp.sel.focus;
                  expand_ancestors_for_node(pp.sel.focus.node);
                  scroll_caret_into_view(pp);
                  reset_blink();
                }
                break;
              }
            }

            // ---- Outline software window ----
            if (outline_mode &&
                mxw >= ol_x - help_edge &&
                mxw <= ol_x + ol_w + help_edge &&
                myw >= ol_y - help_edge &&
                myw <= ol_y + ol_h + help_edge) {
              const bool inside =
                  mxw >= ol_x && mxw < ol_x + ol_w &&
                  myw >= ol_y && myw < ol_y + ol_h;
              {
                const bool on_r =
                    mxw >= ol_x + ol_w - help_edge &&
                    myw >= ol_y + help_title_h &&
                    myw <= ol_y + ol_h + help_edge;
                const bool on_b =
                    myw >= ol_y + ol_h - help_edge &&
                    mxw >= ol_x &&
                    mxw <= ol_x + ol_w + help_edge;
                if (on_r || on_b) {
                  if (outline_docked) {
                    // Docked: only horizontal resize (dock width).
                    ol_resize = 1;
                  } else {
                    ol_resize = (on_r ? 1 : 0) | (on_b ? 2 : 0);
                  }
                  ol_resize_start_x = mxw;
                  ol_resize_start_y = myw;
                  ol_resize_start_w = ol_w;
                  ol_resize_start_h = ol_h;
                  ol_dragging = false;
                  break;
                }
              }
              if (inside && myw < ol_y + help_title_h) {
                if (mxw >= ol_x + ol_w - 28) {
                  outline_mode = false;
                  ol_dragging = false;
                  ol_resize = 0;
                } else {
                  // Focus chip sits just left of ×.
                  const char* flab = section_focus_mode ? "Focus●" : "Focus";
                  int fw = body_font.measure(flab);
                  int fx = ol_x + ol_w - 28 - fw - 10;
                  if (mxw >= fx - 4 && mxw < ol_x + ol_w - 28) {
                    toggle_section_focus_mode();
                    ol_dragging = false;
                  } else {
                    ol_dragging = true;
                    ol_drag_dx = mxw - ol_x;
                    ol_drag_dy = myw - ol_y;
                  }
                }
                break;
              }
              if (inside) {
                auto [cx, cy, cw, ch] = outline_client_rect();
                const float step = outline_row_step();
                const int sb_w = 10;
                if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 &&
                    myw >= cy && myw <= cy + ch) {
                  float content_h =
                      static_cast<float>(outline.size()) * step + 8.f;
                  float max_s =
                      std::max(0.f, content_h - static_cast<float>(ch));
                  float view_h = static_cast<float>(std::max(1, ch));
                  float ratio =
                      (static_cast<float>(myw - cy) / view_h);
                  ratio = std::max(0.f, std::min(1.f, ratio));
                  ol_scroll = ratio * max_s;
                  ol_scroll_dragging = true;
                  break;
                }
                if (mxw >= cx && mxw < cx + cw && myw >= cy && myw < cy + ch) {
                  int row = static_cast<int>(
                      (static_cast<float>(myw - cy) + ol_scroll) / step);
                  if (row >= 0 && row < static_cast<int>(outline.size())) {
                    outline_sel = row;
                    const auto& er = outline[static_cast<size_t>(row)];
                    // Disclosure hit only on the ▸/▾ glyph, not the heading
                    // text (old 48+level*12 zone stole clicks into fold).
                    float local_x = static_cast<float>(mxw - cx);
                    const float indent_px =
                        static_cast<float>(std::max(0, er.level - 1)) * 12.f;
                    // Paint draws at cx+8 with level spaces then the twisty.
                    const float twist_x0 = 8.f + indent_px;
                    const float twist_x1 = twist_x0 + 22.f;
                    const bool on_twist =
                        er.has_children && local_x >= twist_x0 - 2.f &&
                        local_x < twist_x1;
                    if (on_twist && er.section != xanatoo::INVALID_NODE) {
                      toggle_section_fold(er.section);
                    } else {
                      jump_outline_sel();
                    }
                  }
                }
                break;
              }
            }

            // ---- Document software windows (topmost first) ----
            {
              bool soft_hit = false;
              for (int ii = static_cast<int>(soft_wins.size()) - 1; ii >= 0;
                   --ii) {
                const size_t i = static_cast<size_t>(ii);
                SoftWindow& sw = soft_wins[i];
                if (mxw < sw.x - help_edge || mxw > sw.x + sw.w + help_edge ||
                    myw < sw.y - help_edge || myw > sw.y + sw.h + help_edge)
                  continue;
                const bool inside =
                    mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
                    myw < sw.y + sw.h;
                const bool on_r =
                    mxw >= sw.x + sw.w - help_edge &&
                    myw >= sw.y + help_title_h &&
                    myw <= sw.y + sw.h + help_edge;
                const bool on_b =
                    myw >= sw.y + sw.h - help_edge && mxw >= sw.x &&
                    mxw <= sw.x + sw.w + help_edge;
                if (on_r || on_b) {
                  raise_soft(i);
                  SoftWindow& top = soft_wins.back();
                  top.resize = (on_r ? 1 : 0) | (on_b ? 2 : 0);
                  top.resize_start_x = mxw;
                  top.resize_start_y = myw;
                  top.resize_start_w = top.w;
                  top.resize_start_h = top.h;
                  top.dragging = false;
                  soft_hit = true;
                  break;
                }
                if (inside && myw < sw.y + help_title_h) {
                  if (mxw >= sw.x + sw.w - 28) {
                    soft_wins.erase(soft_wins.begin() +
                                    static_cast<std::ptrdiff_t>(i));
                  } else {
                    raise_soft(i);
                    SoftWindow& top = soft_wins.back();
                    const Uint32 now = SDL_GetTicks();
                    if (top.last_title_click != 0 &&
                        now - top.last_title_click < 400) {
                      maximize_soft(top);
                      top.last_title_click = 0;
                      top.dragging = false;
                    } else {
                      top.last_title_click = now;
                      // Dragging a tiled/maximized window restores float first.
                      if (top.maximized || top.dock != SoftDock::None)
                        restore_soft_float(top, mxw, myw);
                      top.dragging = true;
                      top.drag_dx = mxw - top.x;
                      top.drag_dy = myw - top.y;
                      dock_preview = SoftDock::None;
                    }
                  }
                  soft_hit = true;
                  break;
                }
                if (inside) {
                  raise_soft(i);
                  SoftWindow& top = soft_wins.back();
                  auto [cx, cy, cw, ch] = soft_client_rect(top);
                  const int sb_w = 10;
                  if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 &&
                      myw >= cy && myw <= cy + ch) {
                    float max_s = std::max(
                        0.f, top.layout.content_height -
                                 static_cast<float>(ch) + 40.f);
                    float view_h = static_cast<float>(std::max(1, ch));
                    float ratio =
                        (static_cast<float>(myw - cy) / view_h);
                    ratio = std::max(0.f, std::min(1.f, ratio));
                    top.scroll = ratio * max_s;
                    top.scroll_dragging = true;
                    soft_hit = true;
                    break;
                  }
                  float lx = static_cast<float>(mxw - cx);
                  float ly = static_cast<float>(myw - cy);
                  if (lx >= 0 && ly >= 0 && lx <= cw && ly <= ch) {
                    auto& sdoc = soft_doc(top);
                    if (auto sid = xanatoo::hit_test_stretch(
                            top.layout, lx, ly, top.scroll)) {
                      if (sdoc.get(*sid).kind ==
                          xanatoo::NodeKind::Stretch) {
                        // Shared buffer: stretch state is document-owned.
                        int bi = top.buf;
                        if (bi >= 0 && bi < static_cast<int>(buffers.size())) {
                          // Snapshot for undo (view-independent history).
                          buffers[static_cast<size_t>(bi)].history.push(
                              sdoc, top.sel);
                          sdoc.get(*sid).expanded =
                              !sdoc.get(*sid).expanded;
                          buffers[static_cast<size_t>(bi)].dirty = true;
                          update_title();
                        } else {
                          sdoc.get(*sid).expanded =
                              !sdoc.get(*sid).expanded;
                        }
                        top.needs_layout = true;
                        needs_layout = true;
                        for (auto& osw : soft_wins)
                          if (osw.buf == top.buf) osw.needs_layout = true;
                      }
                    }
                    if (auto href = xanatoo::hit_test_link(
                            sdoc, top.layout, lx, ly, top.scroll)) {
                      if (!href->empty() && (*href)[0] == '#') {
                        if (auto dest =
                                xanatoo::jump_to_href(sdoc, *href)) {
                          auto cpos = xanatoo::caret_screen_pos(
                              top.layout, *dest, 0.f,
                              static_cast<float>(body_font.line_height()));
                          if (cpos.valid)
                            top.scroll = std::max(0.f, cpos.y - 24.f);
                        }
                      } else if (!href->empty()) {
                        // Follow file / gemini / external from soft window.
                        // Always load into the focused pane (same as main-pane
                        // clicks). open_browse here left stale soft layouts and
                        // carets that later threw "invalid node id".
                        std::string abs = *href;
                        if (is_in_app_remote_url(abs)) {
                          navigate_to_url(abs);
                        } else if (is_inapp_doc_href(abs)) {
                          std::string resolved =
                              resolve_doc_href(abs, soft_path(top));
                          navigate_to_url(resolved.empty() ? abs : resolved);
                        } else if (abs[0] != '#') {
                          xanatoo::open_external_url(abs);
                          flash_status("Opening " + abs);
                        }
                      }
                    }
                  }
                  soft_hit = true;
                  break;
                }
              }
              if (soft_hit) break;
            }

            // ---- Help software window ----
            if (help_mode &&
                mxw >= help_x - help_edge &&
                mxw <= help_x + help_w + help_edge &&
                myw >= help_y - help_edge &&
                myw <= help_y + help_h + help_edge) {
              const bool inside =
                  mxw >= help_x && mxw < help_x + help_w &&
                  myw >= help_y && myw < help_y + help_h;
              {
                const bool on_right =
                    mxw >= help_x + help_w - help_edge &&
                    myw >= help_y + help_title_h &&
                    myw <= help_y + help_h + help_edge;
                const bool on_bottom =
                    myw >= help_y + help_h - help_edge &&
                    mxw >= help_x &&
                    mxw <= help_x + help_w + help_edge;
                if (on_right || on_bottom) {
                  help_resize = (on_right ? 1 : 0) | (on_bottom ? 2 : 0);
                  help_resize_start_x = mxw;
                  help_resize_start_y = myw;
                  help_resize_start_w = help_w;
                  help_resize_start_h = help_h;
                  help_dragging = false;
                  break;
                }
              }
              if (inside && myw < help_y + help_title_h) {
                if (mxw >= help_x + help_w - 28) {
                  help_mode = false;
                  help_dragging = false;
                  help_resize = 0;
                } else {
                  help_dragging = true;
                  help_drag_dx = mxw - help_x;
                  help_drag_dy = myw - help_y;
                }
                break;
              }
              if (inside) {
                auto [cx, cy, cw, ch] = help_client_rect();
                const int sb_w = 10;
                if (mxw >= cx + cw - sb_w && mxw <= cx + cw + 2 &&
                    myw >= cy && myw <= cy + ch) {
                  float max_s = std::max(
                      0.f, help_layout.content_height -
                               static_cast<float>(ch) + 40.f);
                  float view_h = static_cast<float>(std::max(1, ch));
                  float ratio =
                      (static_cast<float>(myw - cy) / view_h);
                  ratio = std::max(0.f, std::min(1.f, ratio));
                  help_scroll = ratio * max_s;
                  help_scroll_dragging = true;
                  break;
                }
                float lx = static_cast<float>(mxw - cx);
                float ly = static_cast<float>(myw - cy);
                if (lx >= 0 && ly >= 0 && lx <= cw && ly <= ch) {
                  if (auto sid = xanatoo::hit_test_stretch(
                          help_layout, lx, ly, help_scroll)) {
                    if (help_doc.get(*sid).kind ==
                        xanatoo::NodeKind::Stretch) {
                      help_doc.get(*sid).expanded =
                          !help_doc.get(*sid).expanded;
                      help_needs_layout = true;
                      break;
                    }
                  }
                  if (auto href = xanatoo::hit_test_link(
                          help_doc, help_layout, lx, ly, help_scroll)) {
                    if (!href->empty() && (*href)[0] == '#') {
                      if (auto dest =
                              xanatoo::jump_to_href(help_doc, *href)) {
                        auto cpos = xanatoo::caret_screen_pos(
                            help_layout, *dest, 0.f,
                            static_cast<float>(body_font.line_height()));
                        if (cpos.valid)
                          help_scroll = std::max(0.f, cpos.y - 24.f);
                      }
                    }
                  }
                }
                break;  // absorb click even if no hit target
              }
            }

            // Below soft windows: let ImGui keep the event if it wants it.
            if (imgui_mouse) break;

            // Clicking the document dismisses status-bar prompts (Esc also works).
            if (save_as_mode || path_in_window_mode || open_file_mode ||
                url_bar_mode || image_insert_mode || include_insert_mode ||
                link_edit_mode || summary_edit_mode) {
              save_as_mode = false;
              save_as_path.clear();
              path_in_window_mode = false;
              path_in_window_buf.clear();
              open_file_mode = false;
              open_file_buf.clear();
              url_bar_mode = false;
              url_bar_buf.clear();
              image_insert_mode = false;
              image_insert_path.clear();
              image_insert_as_figure = false;
              include_insert_mode = false;
              include_insert_src.clear();
              link_edit_mode = false;
              link_edit_href.clear();
              summary_edit_mode = false;
              summary_edit_text.clear();
            }

            float mx = static_cast<float>(ev.button.x);
            float my = content_y(static_cast<float>(ev.button.y));

            // Classic scrollbar (right edge): arrows page, track jumps/drags.
            {
              int pi = pane_at(mx);
              int sx = panes[pi].x + panes[pi].w - kSbW;
              // `my` is content Y (0 at top of viewport). Usable height excludes
              // the menu/toolbar and the status bar.
              float view_h =
                  static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
              if (mx >= sx && my >= 0.f && my < view_h) {
                focus = pi;
                Pane& sp = panes[pi];
                float content_h =
                    std::max(view_h, sp.layout.content_height);
                float max_s = std::max(0.f, content_h - view_h + 40.f);
                const float step =
                    static_cast<float>(body_font.line_height()) * 3.f;
                if (my < static_cast<float>(kSbArrow)) {
                  sp.scroll_y = std::max(0.f, sp.scroll_y - step);
                  ensure_virtual_layout(sp);
                  break;
                }
                if (my >= view_h - static_cast<float>(kSbArrow)) {
                  sp.scroll_y = std::min(max_s, sp.scroll_y + step);
                  ensure_virtual_layout(sp);
                  break;
                }
                // Track between arrows: page toward click, or grab the thumb.
                float track_h =
                    std::max(1.f, view_h - 2.f * static_cast<float>(kSbArrow));
                float track_my = my - static_cast<float>(kSbArrow);
                float thumb_h = std::max(
                    20.f, track_h * (view_h / std::max(view_h, content_h)));
                if (thumb_h > track_h) thumb_h = track_h;
                float travel = std::max(0.f, track_h - thumb_h);
                float ratio =
                    (max_s > 0.f) ? (sp.scroll_y / max_s) : 0.f;
                float thumb_top = ratio * travel;
                float thumb_bot = thumb_top + thumb_h;
                if (track_my < thumb_top || track_my > thumb_bot) {
                  // Shift+click jumps; plain click pages toward the click.
                  if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                    float tr = track_my / std::max(1.f, track_h);
                    tr = std::max(0.f, std::min(1.f, tr));
                    sp.scroll_y = tr * max_s;
                  } else if (track_my < thumb_top) {
                    sp.scroll_y =
                        std::max(0.f, sp.scroll_y - view_h * 0.85f);
                  } else {
                    sp.scroll_y =
                        std::min(max_s, sp.scroll_y + view_h * 0.85f);
                  }
                  ensure_virtual_layout(sp);
                  break;
                }
                // On thumb — drag without jumping.
                dragging_scroll = true;
                scroll_drag_pane = pi;
                scroll_drag_grab = track_my - thumb_top;
                break;
              }
            }

            // Drag the split gutter?
            if (split) {
              int gutter_x = panes[1].x - 3;
              if (mx >= gutter_x && mx <= gutter_x + 6) {
                dragging_split = true;
                break;
              }
            }

            focus = pane_at(mx);
            Pane& p = F();
            float lx = local_x(mx, focus);

            // Image resize handle (bottom-right 12px square)?
            {
              const float hs = 12.f;
              for (const auto& pi : p.layout.images) {
                float hx = pi.x + pi.width - hs;
                float hy = pi.y + pi.height - hs - p.scroll_y;
                if (lx >= hx && lx <= hx + hs && my >= hy && my <= hy + hs) {
                  if (browse_mode) break;
                  edit();
                  dragging_image = true;
                  resize_image = pi.node;
                  resize_start_x = mx;
                  resize_start_scale = doc_of(p).get(pi.node).display_scale;
                  if (resize_start_scale < 0.05f) resize_start_scale = 1.f;
                  break;
                }
              }
              if (dragging_image) break;
            }

            // Column gutter drag (between side-by-side columns)
            {
              float adj_y = my + p.scroll_y;
              for (const auto& gap : p.layout.column_gaps) {
                if (adj_y < gap.y || adj_y > gap.y + gap.height) continue;
                if (std::abs(lx - gap.x) > 8.f) continue;
                if (browse_mode) break;
                edit();
                dragging_col_gap = true;
                col_gap_left = gap.left;
                col_gap_right = gap.right;
                col_gap_start_x = mx;
                float lw = doc_of(p).get(gap.left).display_scale;
                float rw = doc_of(p).get(gap.right).display_scale;
                if (lw < 0.1f) lw = 1.f;
                if (rw < 0.1f) rw = 1.f;
                col_gap_left_w = lw;
                col_gap_right_w = rw;
                break;
              }
              if (dragging_col_gap) break;
            }

            // Block drag: grab gutter immediately left of the block text
            {
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x - kBlockGripW || lx >= box.x) continue;
                const auto& bn = doc_of(p).get(box.node);
                // Only reorder movable siblings (not structural containers)
                if (bn.kind == xanatoo::NodeKind::Document ||
                    bn.kind == xanatoo::NodeKind::Columns ||
                    bn.kind == xanatoo::NodeKind::Column ||
                    bn.kind == xanatoo::NodeKind::List)
                  continue;
                if (bn.parent == xanatoo::INVALID_NODE) continue;
                if (browse_mode) break;
                dragging_block = true;
                drag_block_id = box.node;
                drop_target_id = xanatoo::INVALID_NODE;
                drop_line_y = -1.f;
                break;
              }
              if (dragging_block) break;
            }

            if (auto sid = xanatoo::hit_test_stretch(p.layout, lx, my, p.scroll_y)) {
              xanatoo::NodeId id = *sid;
              if (doc_of(p).get(id).kind == xanatoo::NodeKind::Stretch) {
                edit();
                doc_of(p).get(id).expanded = !doc_of(p).get(id).expanded;
                needs_layout = true;
                break;
              }
            }

            // Follow links: internal #anchors jump in-document; http(s)/mailto open externally
            if (auto href = xanatoo::hit_test_link(
                    doc_of(p), p.layout, lx, my, p.scroll_y)) {
              if (auto dest = xanatoo::jump_to_href(doc_of(p), *href)) {
                const bool ctrl_click =
                    (SDL_GetModState() & KMOD_CTRL) != 0;
                expand_ancestors_for_node(dest->node);
                // Ctrl+click: open target in the other pane (enable split)
                if (ctrl_click) {
                  if (!split) {
                    split = true;
                    needs_layout = true;
                  }
                  int other = 1 - focus;
                  panes[other].sel.focus = *dest;
                  panes[other].sel.anchor = *dest;
                  scroll_caret_into_view(panes[other]);
                  flash_status("Link in other pane: " + *href);
                  std::cout << "Opened link in other pane: " << *href << "\n";
                } else {
                  p.sel.focus = *dest;
                  p.sel.anchor = p.sel.focus;
                  scroll_caret_into_view(p);
                  // Dual-pane shared doc: mirror target in the other view
                  if (split && shared_doc()) {
                    int other = 1 - focus;
                    panes[other].sel.focus = *dest;
                    panes[other].sel.anchor = *dest;
                    scroll_caret_into_view(panes[other]);
                  }
                  flash_status("Followed " + *href);
                  std::cout << "Followed link: " << *href << "\n";
                }
                reset_blink();
                break;
              } else if (!href->empty() && (*href)[0] != '#') {
                std::string abs = *href;
                if (!xanatoo::is_gemini_url(abs) && !gemini_current_url.empty()) {
                  std::string resolved =
                      xanatoo::resolve_gemini_url(gemini_current_url, abs);
                  if (!resolved.empty()) abs = resolved;
                }
                if (is_in_app_remote_url(abs)) {
                  navigate_to_url(std::move(abs));
                  break;
                }
                if (is_inapp_doc_href(abs)) {
                  navigate_to_url(resolve_doc_href(abs));
                  break;
                }
                flash_status("Opening " + *href);
                std::cout << "Opening external link: " << *href << "\n";
                xanatoo::open_external_url(*href);
                break;
              }
            }

            // Click list-item marker column → toggle GFM task checkbox
            if (!browse_mode) {
              bool toggled = false;
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + 26.f) continue;
                auto& bn = doc_of(p).get(box.node);
                if (bn.kind != xanatoo::NodeKind::ListItem) continue;
                // Ordered items keep numbers unless already a task.
                if (bn.level > 0 && bn.label != "task" &&
                    bn.label != "task-done")
                  continue;
                edit();
                if (bn.label == "task")
                  bn.label = "task-done";
                else if (bn.label == "task-done")
                  bn.label = "task";
                else
                  bn.label = "task";  // bullet → unchecked task
                needs_layout = true;
                reset_blink();
                toggled = true;
                break;
              }
              if (toggled) break;
            }

            // Click a transclusion → jump to local source, or open external file.
            // Alt+click opens the target in the other pane.
            {
              bool followed = false;
              const bool alt_click = (SDL_GetModState() & KMOD_ALT) != 0;
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + box.width) continue;
                const auto& bn = doc_of(p).get(box.node);
                if (bn.kind != xanatoo::NodeKind::Transclude) continue;
                if (alt_click) {
                  p.sel.focus = {box.node, 0, 0};
                  p.sel.anchor = p.sel.focus;
                  open_transclude_in_other_pane();
                  followed = true;
                  break;
                }
                if (auto dest = xanatoo::jump_to_href(doc_of(p), bn.src)) {
                  expand_ancestors_for_node(dest->node);
                  p.sel.focus = *dest;
                  p.sel.anchor = p.sel.focus;
                  scroll_caret_into_view(p);
                  if (split && shared_doc()) {
                    int other = 1 - focus;
                    panes[other].sel.focus = *dest;
                    panes[other].sel.anchor = *dest;
                    scroll_caret_into_view(panes[other]);
                  }
                  reset_blink();
                  flash_status("Followed include " + bn.src);
                  followed = true;
                } else {
                  std::string path_part = bn.src;
                  size_t hash = path_part.find('#');
                  if (hash != std::string::npos)
                    path_part = path_part.substr(0, hash);
                  if (!path_part.empty() &&
                      (path_part.find('.') != std::string::npos ||
                       path_part.find('/') != std::string::npos)) {
                    std::filesystem::path resolved = path_part;
                    std::string base = path_of(p);
                    if (!base.empty() && resolved.is_relative()) {
                      resolved = std::filesystem::path(base).parent_path() /
                                 path_part;
                    }
                    if (std::filesystem::exists(resolved)) {
                      navigate_to_url(resolved.string());
                      followed = true;
                    } else {
                      flash_status("Broken include: " + bn.src);
                    }
                  } else {
                    flash_status("Broken include: " + bn.src);
                  }
                }
                break;
              }
              if (followed) break;
            }

            if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              Uint32 now = SDL_GetTicks();
              bool near = (now - last_click_ticks) < 400 &&
                          std::abs(mx - last_click_x) < 4.f &&
                          std::abs(my - last_click_y) < 4.f;
              click_count = near ? click_count + 1 : 1;
              last_click_ticks = now;
              last_click_x = mx;
              last_click_y = my;

              p.sel.focus = *hit;
              xanatoo::Document& dd = doc_of(p);
              if (click_count >= 3) {
                // Triple-click: select whole block
                xanatoo::select_node(dd, p.sel, hit->node);
                dragging_sel = false;
              } else if (click_count == 2) {
                // Double-click: select word
                xanatoo::select_word_at(dd, p.sel);
                dragging_sel = false;
              } else if (!(SDL_GetModState() & KMOD_SHIFT)) {
                p.sel.anchor = p.sel.focus;
                dragging_sel = true;
              } else {
                dragging_sel = true;
              }
              p.preferred_x = lx;
              reset_blink();
            }
          } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
            // Middle-click paste at caret (common on Linux).
            if (browse_mode) break;
            if (imgui_mouse) break;
            const int mxw = ev.button.x, myw = ev.button.y;
            bool over_soft = false;
            if (outline_mode && mxw >= ol_x && mxw < ol_x + ol_w &&
                myw >= ol_y && myw < ol_y + ol_h)
              over_soft = true;
            if (!over_soft && help_mode && mxw >= help_x &&
                mxw < help_x + help_w && myw >= help_y && myw < help_y + help_h)
              over_soft = true;
            if (!over_soft) {
              for (int i = static_cast<int>(soft_wins.size()) - 1; i >= 0; --i) {
                const SoftWindow& sw = soft_wins[static_cast<size_t>(i)];
                if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
                    myw < sw.y + sw.h) {
                  over_soft = true;
                  break;
                }
              }
            }
            if (!over_soft && SDL_HasClipboardText()) {
              float mx = static_cast<float>(mxw);
              float my = content_y(static_cast<float>(myw));
              focus = pane_at(mx);
              Pane& pp = F();
              float lx = local_x(mx, focus);
              if (auto hit =
                      xanatoo::hit_test(pp.layout, lx, my, pp.scroll_y)) {
                pp.sel.focus = *hit;
                pp.sel.anchor = pp.sel.focus;
              }
              char* clip = SDL_GetClipboardText();
              if (clip && clip[0]) {
                edit();
                xanatoo::paste_plain_text(doc_of(pp), pp.sel.focus, clip);
                pp.sel.anchor = pp.sel.focus;
                needs_layout = true;
                reset_blink();
              }
              if (clip) SDL_free(clip);
            }
          } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            // Soft windows / outline / help absorb right-clicks (no doc menu under them).
            const int rx = ev.button.x, ry = ev.button.y;
            bool over_soft = false;
            if (outline_mode && rx >= ol_x && rx < ol_x + ol_w &&
                ry >= ol_y && ry < ol_y + ol_h)
              over_soft = true;
            if (!over_soft && help_mode && rx >= help_x && rx < help_x + help_w &&
                ry >= help_y && ry < help_y + help_h)
              over_soft = true;
            if (!over_soft) {
              for (int i = static_cast<int>(soft_wins.size()) - 1; i >= 0; --i) {
                const SoftWindow& sw = soft_wins[static_cast<size_t>(i)];
                if (rx >= sw.x && rx < sw.x + sw.w && ry >= sw.y &&
                    ry < sw.y + sw.h) {
                  over_soft = true;
                  break;
                }
              }
            }
            // Document context menu (ImGui popup).
            if (!over_soft && !imgui_mouse) {
              float mx = static_cast<float>(ev.button.x);
              float my = content_y(static_cast<float>(ev.button.y));
              focus = pane_at(mx);
              Pane& p = F();
              float lx = local_x(mx, focus);
              // Place caret under the click so structure/insert actions
              // apply to the right block.
              if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
                p.sel.focus = *hit;
                if (!(SDL_GetModState() & KMOD_SHIFT)) p.sel.anchor = p.sel.focus;
              }
              ui.open_context_menu(static_cast<float>(ev.button.x),
                                   static_cast<float>(ev.button.y));
            }
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            for (auto& sw : soft_wins) {
              if (sw.dragging) {
                SoftDock zone =
                    compute_dock_zone(ev.button.x, ev.button.y);
                if (zone != SoftDock::None) {
                  apply_soft_dock(sw, zone);
                } else {
                  snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
                }
              } else if (sw.resize) {
                snap_soft_geom(sw.x, sw.y, sw.w, sw.h);
                sw.needs_layout = true;
              }
              sw.dragging = false;
              sw.resize = 0;
              sw.scroll_dragging = false;
            }
            dock_preview = SoftDock::None;
            if (help_dragging || help_resize) {
              snap_soft_geom(help_x, help_y, help_w, help_h);
              if (help_resize) help_needs_layout = true;
            }
            help_dragging = false;
            help_resize = 0;
            help_scroll_dragging = false;
            if (ol_dragging || ol_resize)
              snap_soft_geom(ol_x, ol_y, ol_w, ol_h);
            ol_dragging = false;
            ol_resize = 0;
            ol_scroll_dragging = false;
            bl_dragging = false;
            if (dragging_block && drag_block_id != xanatoo::INVALID_NODE &&
                drop_target_id != xanatoo::INVALID_NODE &&
                drop_target_id != drag_block_id) {
              edit();
              if (xanatoo::relocate_block(doc_of(F()), drag_block_id,
                                          drop_target_id, drop_before)) {
                needs_layout = true;
                F().sel.focus.node = drag_block_id;
                F().sel.focus.span_index = 0;
                F().sel.focus.char_index = 0;
                F().sel.anchor = F().sel.focus;
              }
            }
            dragging_sel = false;
            dragging_split = false;
            dragging_scroll = false;
            dragging_image = false;
            dragging_block = false;
            drag_block_id = xanatoo::INVALID_NODE;
            drop_target_id = xanatoo::INVALID_NODE;
            drop_line_y = -1.f;
            dragging_col_gap = false;
            col_gap_left = xanatoo::INVALID_NODE;
            col_gap_right = xanatoo::INVALID_NODE;
            resize_image = xanatoo::INVALID_NODE;
          }
          break;

        case SDL_MOUSEMOTION:
          // Event coordinates: hardware mouse OR synthetic stick inject.
          // Real OS cursor is never warped; stick is an extra sprite.
          mouse_x = static_cast<float>(ev.motion.x);
          mouse_y = static_cast<float>(ev.motion.y);
          {
            const int mx_i = static_cast<int>(mouse_x);
            const int my_i = static_cast<int>(mouse_y);

          // Soft-window drag/resize continue even if ImGui wants the mouse
          // (capture flag can lag one frame after a title-bar grab).
          if (ol_dragging) {
            ol_x = mx_i - ol_drag_dx;
            ol_y = my_i - ol_drag_dy;
            if (ol_x < 0) ol_x = 0;
            if (ol_y < static_cast<int>(ui_top))
              ol_y = static_cast<int>(ui_top);
            break;
          }
          if (ol_scroll_dragging) {
            auto [cx, cy, cw, ch] = outline_client_rect();
            float step = outline_row_step();
            float content_h =
                static_cast<float>(outline.size()) * step + 8.f;
            float max_s =
                std::max(0.f, content_h - static_cast<float>(ch));
            float view_h = static_cast<float>(std::max(1, ch));
            float ratio =
                (mouse_y - static_cast<float>(cy)) / view_h;
            ratio = std::max(0.f, std::min(1.f, ratio));
            ol_scroll = ratio * max_s;
            break;
          }
          if (ol_resize) {
            int dx = mx_i - ol_resize_start_x;
            int dy = my_i - ol_resize_start_y;
            if (ol_resize & 1) {
              ol_w = ol_resize_start_w + dx;
              if (outline_docked) {
                if (ol_w < 160) ol_w = 160;
                if (ol_w > win_w / 2) ol_w = win_w / 2;
                if (ol_w != outline_dock_w) {
                  outline_dock_w = ol_w;
                  needs_layout = true;
                  layout_panes();
                }
              } else {
                if (ol_w < 200) ol_w = 200;
                if (ol_w > win_w - 20) ol_w = win_w - 20;
              }
            }
            if (ol_resize & 2 && !outline_docked) {
              ol_h = ol_resize_start_h + dy;
              if (ol_h < 140) ol_h = 140;
              if (ol_h > win_h - 40) ol_h = win_h - 40;
            }
            break;
          }
          if (bl_dragging) {
            bl_x = mx_i - bl_drag_dx;
            bl_y = my_i - bl_drag_dy;
            if (bl_x < 0) bl_x = 0;
            if (bl_y < static_cast<int>(ui_top))
              bl_y = static_cast<int>(ui_top);
            break;
          }
          {
            bool soft_motion = false;
            for (auto& sw : soft_wins) {
              if (sw.dragging) {
                sw.x = mx_i - sw.drag_dx;
                sw.y = my_i - sw.drag_dy;
                if (sw.x < 0) sw.x = 0;
                if (sw.y < static_cast<int>(ui_top))
                  sw.y = static_cast<int>(ui_top);
                dock_preview = compute_dock_zone(mx_i, my_i);
                soft_motion = true;
                break;
              }
              if (sw.scroll_dragging) {
                auto [cx, cy, cw, ch] = soft_client_rect(sw);
                float max_s = std::max(
                    0.f, sw.layout.content_height -
                             static_cast<float>(ch) + 40.f);
                float view_h = static_cast<float>(std::max(1, ch));
                float ratio =
                    (mouse_y - static_cast<float>(cy)) / view_h;
                ratio = std::max(0.f, std::min(1.f, ratio));
                sw.scroll = ratio * max_s;
                soft_motion = true;
                break;
              }
              if (sw.resize) {
                int dx = mx_i - sw.resize_start_x;
                int dy = my_i - sw.resize_start_y;
                if (sw.resize & 1) {
                  sw.w = sw.resize_start_w + dx;
                  if (sw.w < 280) sw.w = 280;
                  if (sw.w > win_w - 20) sw.w = win_w - 20;
                  sw.needs_layout = true;
                }
                if (sw.resize & 2) {
                  sw.h = sw.resize_start_h + dy;
                  if (sw.h < 160) sw.h = 160;
                  if (sw.h > win_h - 40) sw.h = win_h - 40;
                }
                soft_motion = true;
                break;
              }
            }
            if (soft_motion) break;
          }
          if (help_dragging) {
            help_x = mx_i - help_drag_dx;
            help_y = my_i - help_drag_dy;
            if (help_x < 0) help_x = 0;
            if (help_y < static_cast<int>(ui_top)) help_y = static_cast<int>(ui_top);
            break;
          }
          if (help_scroll_dragging) {
            auto [cx, cy, cw, ch] = help_client_rect();
            float max_s = std::max(
                0.f, help_layout.content_height - static_cast<float>(ch) + 40.f);
            float view_h = static_cast<float>(std::max(1, ch));
            float ratio =
                (mouse_y - static_cast<float>(cy)) / view_h;
            ratio = std::max(0.f, std::min(1.f, ratio));
            help_scroll = ratio * max_s;
            break;
          }
          if (help_resize) {
            int dx = mx_i - help_resize_start_x;
            int dy = my_i - help_resize_start_y;
            if (help_resize & 1) {
              help_w = help_resize_start_w + dx;
              if (help_w < 280) help_w = 280;
              if (help_w > win_w - 20) help_w = win_w - 20;
              help_needs_layout = true;
            }
            if (help_resize & 2) {
              help_h = help_resize_start_h + dy;
              if (help_h < 160) help_h = 160;
              if (help_h > win_h - 40) help_h = win_h - 40;
            }
            break;
          }
          // Soft-window interactions handled; rest of motion is for panes.
          if (imgui_mouse) break;

          // Status bar is not part of the document surface.
          if (my_i >= win_h - status_bar_h &&
              !dragging_sel && !dragging_scroll && !dragging_split &&
              !dragging_image && !dragging_block && !dragging_col_gap) {
            break;
          }

          // Hovering a soft window (without an active document drag) should not
          // drive pane selection, scroll-hover, or gutter feedback underneath.
          if (!dragging_sel && !dragging_scroll && !dragging_split &&
              !dragging_image && !dragging_block && !dragging_col_gap) {
            const int mxw = mx_i, myw = my_i;
            bool over_soft = false;
            if (outline_mode && mxw >= ol_x && mxw < ol_x + ol_w &&
                myw >= ol_y && myw < ol_y + ol_h)
              over_soft = true;
            if (!over_soft && help_mode && mxw >= help_x &&
                mxw < help_x + help_w && myw >= help_y && myw < help_y + help_h)
              over_soft = true;
            if (!over_soft) {
              for (const auto& sw : soft_wins) {
                if (mxw >= sw.x && mxw < sw.x + sw.w && myw >= sw.y &&
                    myw < sw.y + sw.h) {
                  over_soft = true;
                  break;
                }
              }
            }
            if (over_soft) break;
          }

          // Scrollbar hover (focused pane) for arrow highlight.
          if (!dragging_scroll) {
            sb_hover = 0;
            float myh = mouse_y - ui_top;
            float view_h =
                static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
            int sx = panes[focus].x + panes[focus].w - kSbW;
            if (mouse_x >= sx && mouse_x < panes[focus].x + panes[focus].w &&
                myh >= 0.f && myh < view_h) {
              if (myh < static_cast<float>(kSbArrow))
                sb_hover = 1;
              else if (myh >= view_h - static_cast<float>(kSbArrow))
                sb_hover = 2;
              else
                sb_hover = 3;
            }
          }

          if (dragging_scroll) {
            float my = mouse_y - ui_top;
            Pane& sp = panes[scroll_drag_pane];
            float view_h =
                static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
            float content_h = std::max(view_h, sp.layout.content_height);
            float max_s = std::max(0.f, content_h - view_h + 40.f);
            float track_h =
                std::max(1.f, view_h - 2.f * static_cast<float>(kSbArrow));
            float track_my = my - static_cast<float>(kSbArrow);
            float thumb_h = std::max(
                20.f, track_h * (view_h / std::max(view_h, content_h)));
            if (thumb_h > track_h) thumb_h = track_h;
            float travel = std::max(1.f, track_h - thumb_h);
            float thumb_top = track_my - scroll_drag_grab;
            float ratio = thumb_top / travel;
            ratio = std::max(0.f, std::min(1.f, ratio));
            sp.scroll_y = ratio * max_s;
            ensure_virtual_layout(sp);
            break;
          }
          if (dragging_split && split) {
            float mx = mouse_x;
            split_ratio = mx / static_cast<float>(win_w);
            split_ratio = std::max(0.15f, std::min(0.85f, split_ratio));
            needs_layout = true;
            break;
          }
          if (dragging_image && resize_image != xanatoo::INVALID_NODE) {
            float mx = mouse_x;
            float dx = mx - resize_start_x;
            // 200px drag ≈ 1.0 scale change
            float scale = resize_start_scale + dx / 200.f;
            scale = std::max(0.15f, std::min(3.0f, scale));
            doc_of(F()).get(resize_image).display_scale = scale;
            needs_layout = true;
            break;
          }
          if (dragging_col_gap && col_gap_left != xanatoo::INVALID_NODE &&
              col_gap_right != xanatoo::INVALID_NODE) {
            float mx = mouse_x;
            float dx = mx - col_gap_start_x;
            // Shift weight from one column to the other; keep total constant
            float total = col_gap_left_w + col_gap_right_w;
            float delta = dx / 200.f * total;
            float nl = col_gap_left_w + delta;
            float nr = col_gap_right_w - delta;
            if (nl < 0.2f) {
              nl = 0.2f;
              nr = total - nl;
            }
            if (nr < 0.2f) {
              nr = 0.2f;
              nl = total - nr;
            }
            doc_of(F()).get(col_gap_left).display_scale = nl;
            doc_of(F()).get(col_gap_right).display_scale = nr;
            needs_layout = true;
            break;
          }
          if (dragging_block && drag_block_id != xanatoo::INVALID_NODE) {
            Pane& p = F();
            float adj_y = content_y(mouse_y) + p.scroll_y;
            drop_target_id = xanatoo::INVALID_NODE;
            drop_line_y = -1.f;
            for (const auto& box : p.layout.boxes) {
              if (box.node == drag_block_id) continue;
              if (adj_y < box.y || adj_y > box.y + box.height) continue;
              const auto& bn = doc_of(p).get(box.node);
              // Skip structural containers as drop targets
              if (bn.kind == xanatoo::NodeKind::Document ||
                  bn.kind == xanatoo::NodeKind::Columns ||
                  bn.kind == xanatoo::NodeKind::List)
                continue;
              // Top half → before, bottom half → after
              drop_before = (adj_y < box.y + box.height * 0.5f);
              drop_target_id = box.node;
              drop_line_y = drop_before ? box.y : (box.y + box.height);
              break;
            }
            break;
          }
          if (dragging_sel) {
            float mx = mouse_x;
            float my = content_y(mouse_y);
            // Keep focus on the pane where drag started
            Pane& p = F();
            float lx = local_x(mx, focus);
            if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              p.sel.focus = *hit;
              p.preferred_x = lx;
            }
          } else {
            // Track interactive elements under the cursor for status, tooltip,
            // highlight, and pointer shape.
            float mx = mouse_x;
            float my = content_y(mouse_y);
            // mouse_x/y already set above (stick or hardware); do not clobber.
            int hover_pane = pane_at(mx);
            Pane& hp = panes[hover_pane];
            float lx = local_x(mx, hover_pane);
            auto& hd = doc_of(hp);

            hovered_href.clear();
            hovered_rel.clear();
            hovered_is_cite = false;
            hovered_preview.clear();
            hovered_link_node = xanatoo::INVALID_NODE;
            hovered_link_span = 0;
            hovered_link_pane = -1;
            hovered_stretch = xanatoo::INVALID_NODE;
            hovered_stretch_pane = -1;
            hovered_image = xanatoo::INVALID_NODE;
            hovered_image_pane = -1;
            hovered_image_handle = false;
            hovered_col_gap = false;
            hovered_col_gap_pane = -1;
            hovered_task = xanatoo::INVALID_NODE;
            hovered_task_pane = -1;
            hovered_grip = xanatoo::INVALID_NODE;
            hovered_grip_pane = -1;
            hovered_transclude = xanatoo::INVALID_NODE;
            hovered_transclude_pane = -1;
            hovered_transclude_src.clear();

            // Column gutters
            for (const auto& gap : hp.layout.column_gaps) {
              float gx = gap.x;
              float gy0 = gap.y - hp.scroll_y;
              float gy1 = gy0 + gap.height;
              if (lx >= gx - 5.f && lx <= gx + 5.f && my >= gy0 && my <= gy1) {
                hovered_col_gap = true;
                hovered_col_gap_pane = hover_pane;
                break;
              }
            }

            // Image body / resize handle (bottom-right 12px)
            if (!hovered_col_gap) {
              for (const auto& pi : hp.layout.images) {
                float ix0 = pi.x, iy0 = pi.y - hp.scroll_y;
                float ix1 = ix0 + pi.width, iy1 = iy0 + pi.height;
                if (lx >= ix0 && lx <= ix1 && my >= iy0 && my <= iy1) {
                  hovered_image = pi.node;
                  hovered_image_pane = hover_pane;
                  const float hs = 12.f;
                  if (lx >= ix1 - hs && my >= iy1 - hs)
                    hovered_image_handle = true;
                  break;
                }
              }
            }

            // Stretch marker (collapsed line or expanded header)
            if (!hovered_col_gap &&
                hovered_image == xanatoo::INVALID_NODE) {
              if (auto sid =
                      xanatoo::hit_test_stretch(hp.layout, lx, my, hp.scroll_y)) {
                if (hd.get(*sid).kind == xanatoo::NodeKind::Stretch) {
                  hovered_stretch = *sid;
                  hovered_stretch_pane = hover_pane;
                }
              }
            }

            // Links / cites (skip stretch-marker glyphs — those are stretch)
            if (!hovered_col_gap && hovered_stretch == xanatoo::INVALID_NODE &&
                hovered_image == xanatoo::INVALID_NODE) {
              if (auto hit = xanatoo::hit_test_link_ex(hd, hp.layout, lx, my,
                                                       hp.scroll_y)) {
                // Stretch summaries are laid out as Link-styled glyphs on a
                // Stretch node; treat them as stretch, not as hyperlinks.
                if (hd.get(hit->node).kind == xanatoo::NodeKind::Stretch) {
                  hovered_stretch = hit->node;
                  hovered_stretch_pane = hover_pane;
                } else if (!hit->href.empty()) {
                  hovered_href = hit->href;
                  hovered_rel = hit->rel;
                  hovered_is_cite = (hit->kind == xanatoo::InlineKind::Cite);
                  hovered_link_node = hit->node;
                  hovered_link_span = hit->span_index;
                  hovered_link_pane = hover_pane;
                  if (auto prev = xanatoo::link_preview_text(hd, hit->href)) {
                    hovered_preview = *prev;
                  }
                }
              }
            }

            // Task checkbox column (left ~26px of a ListItem)
            if (!hovered_col_gap && hovered_stretch == xanatoo::INVALID_NODE &&
                hovered_image == xanatoo::INVALID_NODE &&
                hovered_link_node == xanatoo::INVALID_NODE) {
              float adj_y = my + hp.scroll_y;
              for (const auto& box : hp.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + 26.f) continue;
                const auto& bn = hd.get(box.node);
                if (bn.kind != xanatoo::NodeKind::ListItem) continue;
                if (bn.level > 0 && bn.label != "task" &&
                    bn.label != "task-done")
                  continue;
                hovered_task = box.node;
                hovered_task_pane = hover_pane;
                break;
              }
            }

            // Block drag grip (gutter left of the block text)
            if (!hovered_col_gap && hovered_task == xanatoo::INVALID_NODE &&
                hovered_stretch == xanatoo::INVALID_NODE &&
                hovered_link_node == xanatoo::INVALID_NODE &&
                hovered_image == xanatoo::INVALID_NODE) {
              float adj_y = my + hp.scroll_y;
              for (const auto& box : hp.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x - kBlockGripW || lx >= box.x) continue;
                const auto& bn = hd.get(box.node);
                if (bn.kind == xanatoo::NodeKind::Document ||
                    bn.kind == xanatoo::NodeKind::Columns ||
                    bn.kind == xanatoo::NodeKind::Column ||
                    bn.kind == xanatoo::NodeKind::List)
                  continue;
                hovered_grip = box.node;
                hovered_grip_pane = hover_pane;
                break;
              }
            }

            // Transclude / include region
            if (!hovered_col_gap && hovered_task == xanatoo::INVALID_NODE &&
                hovered_grip == xanatoo::INVALID_NODE &&
                hovered_stretch == xanatoo::INVALID_NODE &&
                hovered_link_node == xanatoo::INVALID_NODE &&
                hovered_image == xanatoo::INVALID_NODE) {
              float adj_y = my + hp.scroll_y;
              for (const auto& box : hp.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + box.width) continue;
                const auto& bn = hd.get(box.node);
                if (bn.kind != xanatoo::NodeKind::Transclude) continue;
                hovered_transclude = box.node;
                hovered_transclude_pane = hover_pane;
                hovered_transclude_src = bn.src;
                break;
              }
            }
          }
          }  // mx_i / my_i scope
          break;

        case SDL_TEXTINPUT: {
          // Status-bar prompts own typing even when ImGui still reports capture
          // (menu bar / toolbar focus must not steal path or search input).
          if (minibuf_active()) {
            minibuf.insert_utf8(ev.text.text);
            minibuf_sync_out();
            break;
          }

          
          
          
          if (link_edit_mode) {
            link_edit_href += ev.text.text;
            break;
          }
          if (summary_edit_mode) {
            summary_edit_text += ev.text.text;
            break;
          }
          if (search_mode) {
            if (replace_mode && !search_query.empty() && replace_focus) {
              replace_text += ev.text.text;
              break;
            }
            search_query += ev.text.text;
            Pane& p = F();
            size_t mlen = 0;
            xanatoo::Caret from = p.sel.focus;
            if (from.char_index > 0) from.char_index -= 1;
            if (auto hit = xanatoo::find_next(doc_of(p), search_query, from, mlen)) {
              p.sel.anchor = *hit;
              p.sel.focus = *hit;
              xanatoo::advance_caret(doc_of(p), p.sel.focus, mlen);
              scroll_caret_into_view(p);
            }
            reset_blink();
            break;
          }
          if (browse_mode) break;  // read-only: no typing into the document
          if (imgui_keys) break;
          edit();
          Pane& p = F();
          xanatoo::Document& td = doc_of(p);
          if (!p.sel.empty()) {
            xanatoo::delete_selection(td, p.sel);
          }
          xanatoo::ensure_valid_caret(td, p.sel.focus);
          xanatoo::insert_text(td, p.sel.focus, ev.text.text);
          p.sel.anchor = p.sel.focus;
          needs_layout = true;
          reset_blink();
          break;
        }

        case SDL_CONTROLLERDEVICEADDED:
          if (!gamepad) {
            open_first_gamepad();
            if (gamepad) {
              stick_cursor_on = true;
              stick_cursor_init = false;
              flash_status(
                  "Pad: R-stick cursor · A click · B rclick · "
                  "D-pad nav · L-stick scroll · Start help");
            }
          }
          break;
        case SDL_CONTROLLERDEVICEREMOVED:
          if (gamepad &&
              ev.cdevice.which ==
                  SDL_JoystickInstanceID(
                      SDL_GameControllerGetJoystick(gamepad))) {
            if (stick_lmb || stick_rmb) {
              SDL_Event me{};
              me.type = SDL_MOUSEBUTTONUP;
              me.button.state = SDL_RELEASED;
              me.button.clicks = 1;
              me.button.x = static_cast<Sint32>(stick_cx);
              me.button.y = static_cast<Sint32>(stick_cy);
              me.button.windowID = SDL_GetWindowID(window);
              if (stick_lmb) {
                me.button.button = SDL_BUTTON_LEFT;
                SDL_PushEvent(&me);
                stick_lmb = false;
              }
              if (stick_rmb) {
                me.button.button = SDL_BUTTON_RIGHT;
                SDL_PushEvent(&me);
                stick_rmb = false;
              }
            }
            SDL_GameControllerClose(gamepad);
            gamepad = nullptr;
            stick_cursor_on = false;
            stick_cursor_init = false;
          }
          break;
        case SDL_CONTROLLERBUTTONDOWN: {
          // Gamepad is always for navigation (never text entry).
          switch (ev.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
              nav_move_line(true);
              break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
              nav_move_line(false);
              break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
              // Collapse stretch under caret, else previous block
              if (!toggle_stretch_at_caret()) nav_move_block(true);
              break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
              if (!toggle_stretch_at_caret()) nav_move_block(false);
              break;
            case SDL_CONTROLLER_BUTTON_A: {
              // With a pad, A is left-click at the stick pointer (menus + hits).
              // Hold LB to fall back to caret activate (stretch / link under caret).
              const bool lb = SDL_GameControllerGetButton(
                  gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
              if (!lb) {
                stick_cursor_on = true;
                stick_lmb = true;
                SDL_Event me{};
                me.type = SDL_MOUSEBUTTONDOWN;
                me.button.button = SDL_BUTTON_LEFT;
                me.button.state = SDL_PRESSED;
                me.button.clicks = 1;
                me.button.x = static_cast<Sint32>(stick_cx);
                me.button.y = static_cast<Sint32>(stick_cy);
                me.button.windowID = SDL_GetWindowID(window);
                SDL_PushEvent(&me);
              } else {
                activate_at_caret();
              }
              break;
            }
            case SDL_CONTROLLER_BUTTON_B: {
              // Right-click at pointer; LB+B = collapse stretch under caret.
              const bool lb = SDL_GameControllerGetButton(
                  gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
              if (!lb) {
                stick_cursor_on = true;
                stick_rmb = true;
                SDL_Event me{};
                me.type = SDL_MOUSEBUTTONDOWN;
                me.button.button = SDL_BUTTON_RIGHT;
                me.button.state = SDL_PRESSED;
                me.button.clicks = 1;
                me.button.x = static_cast<Sint32>(stick_cx);
                me.button.y = static_cast<Sint32>(stick_cy);
                me.button.windowID = SDL_GetWindowID(window);
                SDL_PushEvent(&me);
              } else {
                if (!toggle_stretch_at_caret()) collapse_or_expand_all(false);
              }
              break;
            }
            case SDL_CONTROLLER_BUTTON_X:
              outline_mode = !outline_mode;
              if (outline_mode) {
                refresh_outline();
                outline_sel = 0;
              }
              break;
            case SDL_CONTROLLER_BUTTON_Y:
              // Forward in history when possible; else structure overlay.
              if (nav_hist_pos + 1 < static_cast<int>(nav_history.size())) {
                ++nav_hist_pos;
                navigate_to_url(
                    nav_history[static_cast<size_t>(nav_hist_pos)],
                    /*record_history=*/false);
              } else {
                structure_mode = !structure_mode;
                flash_status(structure_mode ? "Structure on" : "Structure off");
              }
              break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
              nav_header(true);
              break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
              nav_header(false);
              break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:
              collapse_or_expand_all(false);
              break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
              collapse_or_expand_all(true);
              break;
            case SDL_CONTROLLER_BUTTON_BACK:
              // Prefer capsule history when available; else toggle browse.
              if (nav_hist_pos > 0) {
                --nav_hist_pos;
                navigate_to_url(nav_history[static_cast<size_t>(nav_hist_pos)],
                                /*record_history=*/false);
              } else {
                browse_mode = !browse_mode;
                flash_status(browse_mode ? "Browse mode (read-only)"
                                         : "Edit mode");
              }
              break;
            case SDL_CONTROLLER_BUTTON_START:
              help_mode = !help_mode;
              if (help_mode) {
                ensure_help_doc();
                help_needs_layout = true;
              }
              break;
            case SDL_CONTROLLER_BUTTON_GUIDE:
              virtual_keyboard = !virtual_keyboard;
              if (virtual_keyboard && browse_mode) {
                browse_mode = false;
                flash_status("Virtual keyboard — edit mode");
              } else {
                flash_status(virtual_keyboard ? "Virtual keyboard on"
                                             : "Virtual keyboard off");
              }
              break;
            default:
              break;
          }
          break;
        }

        case SDL_CONTROLLERBUTTONUP: {
          if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A && stick_lmb) {
            stick_lmb = false;
            SDL_Event me{};
            me.type = SDL_MOUSEBUTTONUP;
            me.button.button = SDL_BUTTON_LEFT;
            me.button.state = SDL_RELEASED;
            me.button.clicks = 1;
            me.button.x = static_cast<Sint32>(stick_cx);
            me.button.y = static_cast<Sint32>(stick_cy);
            me.button.windowID = SDL_GetWindowID(window);
            SDL_PushEvent(&me);
          } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B && stick_rmb) {
            stick_rmb = false;
            SDL_Event me{};
            me.type = SDL_MOUSEBUTTONUP;
            me.button.button = SDL_BUTTON_RIGHT;
            me.button.state = SDL_RELEASED;
            me.button.clicks = 1;
            me.button.x = static_cast<Sint32>(stick_cx);
            me.button.y = static_cast<Sint32>(stick_cy);
            me.button.windowID = SDL_GetWindowID(window);
            SDL_PushEvent(&me);
          }
          break;
        }

        case SDL_KEYDOWN: {
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
          bool alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
          Pane& p = F();
          xanatoo::Document& d = doc_of(p);

          // Alt+letter opens the matching top-level menu (Windows/GNOME style).
          // Handled before status-prompt isolation so chrome stays reachable.
          if (alt && !ctrl && !shift) {
            const SDL_Keycode k = ev.key.keysym.sym;
            const char* menu = nullptr;
            switch (k) {
              case SDLK_f: menu = "File"; break;
              case SDLK_e: menu = "Edit"; break;
              case SDLK_o: menu = "Format"; break;  // classic "fOrmat"
              case SDLK_i: menu = "Insert"; break;
              case SDLK_v: menu = "View"; break;
              case SDLK_b: menu = "Bookmarks"; break;
              case SDLK_h: menu = "Help"; break;
              default: break;
            }
            if (menu) {
              ui.request_menu(menu);
              break;
            }
          }

          // Status prompts own Esc / Backspace / Enter; block document keys so
          // arrows do not move the caret while typing a path or query.
          // Path/URL minibuffer also needs Tab (complete), arrows (cursor /
          // history), Home/End, Delete, and C-a/e/k/u/w.
          const bool status_prompt =
              save_as_mode || path_in_window_mode || open_file_mode ||
              url_bar_mode || image_insert_mode || include_insert_mode ||
              link_edit_mode || summary_edit_mode || search_mode;
          if (status_prompt) {
            const SDL_Keycode k = ev.key.keysym.sym;
            const bool minibuf_path =
                open_file_mode || save_as_mode || path_in_window_mode ||
                url_bar_mode || image_insert_mode || include_insert_mode;
            const bool prompt_key =
                k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_RETURN ||
                k == SDLK_KP_ENTER || k == SDLK_DELETE ||
                // Paste into the status-bar buffer
                (ctrl && k == SDLK_v) ||
                // Search: F3 / Shift+F3 still jump matches while the query is open
                (search_mode && (k == SDLK_F3 || k == SDLK_g)) ||
                // Allow Tab to switch replace-field focus when replace is open
                (search_mode && replace_mode && k == SDLK_TAB) ||
                // Path minibuffer: completion + cursor + history
                (minibuf_path &&
                 (k == SDLK_TAB || k == SDLK_UP || k == SDLK_DOWN ||
                  k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_HOME ||
                  k == SDLK_END ||
                  (ctrl && (k == SDLK_a || k == SDLK_e || k == SDLK_k ||
                            k == SDLK_u || k == SDLK_w))));
            if (!prompt_key) break;
          } else if (imgui_keys && ev.key.keysym.sym != SDLK_ESCAPE &&
                     ev.key.keysym.sym != SDLK_F1 &&
                     ev.key.keysym.sym != SDLK_F2) {
            break;
          }

          // Browse mode: navigation and view toggles only (no document edits).
          if (browse_mode) {
            const SDL_Keycode k = ev.key.keysym.sym;
            const bool browse_ok =
                k == SDLK_ESCAPE || k == SDLK_F1 || k == SDLK_F2 ||
                k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP ||
                k == SDLK_DOWN || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN ||
                k == SDLK_HOME || k == SDLK_END || k == SDLK_TAB ||
                k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE ||
                k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET ||
                (ctrl && (k == SDLK_q || k == SDLK_o || k == SDLK_f ||
                          k == SDLK_g || k == SDLK_BACKSLASH || k == SDLK_l ||
                          k == SDLK_l ||
                          k == SDLK_EQUALS || k == SDLK_MINUS ||
                          k == SDLK_PLUS || k == SDLK_KP_PLUS ||
                          k == SDLK_KP_MINUS)) ||
                (ctrl && shift &&
                 (k == SDLK_o || k == SDLK_l || k == SDLK_d || k == SDLK_f ||
                  k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET));
            if (!browse_ok) break;
            // Space / Enter activate stretch or link
            if (k == SDLK_SPACE ||
                ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !ctrl)) {
              activate_at_caret();
              break;
            }
          }

          // Help window is non-modal: Esc/F1 close it; other keys edit the document.
          if (help_mode &&
              (ev.key.keysym.sym == SDLK_ESCAPE ||
               ev.key.keysym.sym == SDLK_F1)) {
            help_mode = false;
            help_dragging = false;
            break;
          }


          // ---- Minibuffer (path/URL prompts): Tab, history, cursor ----
          if (minibuf_active()) {
            const SDL_Keycode k = ev.key.keysym.sym;
            bool used = true;
            if (k == SDLK_ESCAPE) {
              open_file_mode = false;
              save_as_mode = false;
              path_in_window_mode = false;
              url_bar_mode = false;
              image_insert_mode = false;
              include_insert_mode = false;
              open_file_buf.clear();
              save_as_path.clear();
              path_in_window_buf.clear();
              url_bar_buf.clear();
              image_insert_path.clear();
              include_insert_src.clear();
              minibuf.clear();
            } else if (k == SDLK_TAB) {
              if (url_bar_mode && (minibuf.text.find("://") != std::string::npos)) {
                // no filesystem completion for remote schemes
                used = false;
              } else {
                if (!minibuf.tab_complete_path())
                  flash_status("No path completions");
                minibuf_sync_out();
              }
            } else if (k == SDLK_UP) {
              if (url_bar_mode)
                minibuf.history_older(minibuf_hist_url);
              else
                minibuf.history_older(minibuf_hist_path);
              minibuf_sync_out();
            } else if (k == SDLK_DOWN) {
              if (url_bar_mode)
                minibuf.history_newer(minibuf_hist_url);
              else
                minibuf.history_newer(minibuf_hist_path);
              minibuf_sync_out();
            } else if (k == SDLK_LEFT && !ctrl) {
              minibuf.move_left();
            } else if (k == SDLK_RIGHT && !ctrl) {
              minibuf.move_right();
            } else if (k == SDLK_HOME || (ctrl && k == SDLK_a)) {
              minibuf.move_home();
            } else if (k == SDLK_END || (ctrl && k == SDLK_e)) {
              minibuf.move_end();
            } else if (ctrl && k == SDLK_k) {
              minibuf.kill_to_end();
              minibuf_sync_out();
            } else if (ctrl && k == SDLK_u) {
              minibuf.kill_line();
              minibuf_sync_out();
            } else if (ctrl && k == SDLK_w) {
              minibuf.kill_word_back();
              minibuf_sync_out();
            } else if (k == SDLK_BACKSPACE) {
              if (ctrl)
                minibuf.kill_word_back();
              else
                minibuf.backspace();
              minibuf_sync_out();
            } else if (k == SDLK_DELETE) {
              minibuf.delete_forward();
              minibuf_sync_out();
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
              used = false;  // fall through to existing Enter handlers
            } else {
              used = false;
            }
            if (used) break;
          }

          if (backlinks_mode) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                (ev.key.keysym.sym == SDLK_b && ctrl && alt)) {
              backlinks_mode = false;
              break;
            }
            if (ev.key.keysym.sym == SDLK_UP) {
              if (backlinks_sel > 0) --backlinks_sel;
              break;
            }
            if (ev.key.keysym.sym == SDLK_DOWN) {
              if (backlinks_sel + 1 < static_cast<int>(backlinks.size()))
                ++backlinks_sel;
              break;
            }
            if (ev.key.keysym.sym == SDLK_RETURN ||
                ev.key.keysym.sym == SDLK_KP_ENTER) {
              if (backlinks_sel >= 0 &&
                  backlinks_sel < static_cast<int>(backlinks.size())) {
                auto& p = F();
                p.sel.focus = {backlinks[static_cast<size_t>(backlinks_sel)].source,
                               0, 0};
                p.sel.anchor = p.sel.focus;
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;
            }
          }

          // Outline software window: Esc/toggle close; arrows + 1-9 + Enter
          // navigate without eating other document keys.
          if (outline_mode) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                (ev.key.keysym.sym == SDLK_o && ctrl && shift)) {
              outline_mode = false;
              ol_dragging = false;
              ol_resize = 0;
              break;
            }
            bool outline_consumed = false;
            switch (ev.key.keysym.sym) {
              case SDLK_UP:
                if (outline_sel > 0) --outline_sel;
                outline_consumed = true;
                break;
              case SDLK_DOWN:
                if (outline_sel + 1 < static_cast<int>(outline.size()))
                  ++outline_sel;
                outline_consumed = true;
                break;
              case SDLK_HOME:
                outline_sel = 0;
                outline_consumed = true;
                break;
              case SDLK_END:
                if (!outline.empty())
                  outline_sel = static_cast<int>(outline.size()) - 1;
                outline_consumed = true;
                break;
              case SDLK_LEFT: {
                if (outline_sel >= 0 &&
                    outline_sel < static_cast<int>(outline.size())) {
                  const auto& er =
                      outline[static_cast<size_t>(outline_sel)];
                  if (er.section != xanatoo::INVALID_NODE && !er.folded)
                    toggle_section_fold(er.section);
                }
                outline_consumed = true;
                break;
              }
              case SDLK_RIGHT: {
                if (outline_sel >= 0 &&
                    outline_sel < static_cast<int>(outline.size())) {
                  const auto& er =
                      outline[static_cast<size_t>(outline_sel)];
                  if (er.section != xanatoo::INVALID_NODE && er.folded)
                    toggle_section_fold(er.section);
                }
                outline_consumed = true;
                break;
              }
              case SDLK_RETURN:
              case SDLK_KP_ENTER:
                jump_outline_sel();
                outline_consumed = true;
                break;
              default:
                if (ev.key.keysym.sym >= SDLK_1 &&
                    ev.key.keysym.sym <= SDLK_9) {
                  int idx = static_cast<int>(ev.key.keysym.sym - SDLK_1);
                  if (idx < static_cast<int>(outline.size())) {
                    outline_sel = idx;
                    jump_outline_sel();
                    outline_consumed = true;
                  }
                }
                break;
            }
            if (outline_consumed) break;
          }

          auto clear_sel = [&]() { p.sel.anchor = p.sel.focus; };
          auto move_and_sel = [&](auto mover) {
            if (!shift) clear_sel();
            mover();
            if (!shift) p.sel.anchor = p.sel.focus;
          };

          switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
              if (outline_mode) {
                outline_mode = false;
              } else if (save_as_mode) {
                save_as_mode = false;
                save_as_path.clear();
              } else if (path_in_window_mode) {
                path_in_window_mode = false;
                path_in_window_buf.clear();
              } else if (open_file_mode) {
                open_file_mode = false;
                open_file_buf.clear();
              } else if (url_bar_mode) {
                url_bar_mode = false;
                url_bar_buf.clear();
              } else if (image_insert_mode) {
                image_insert_mode = false;
                image_insert_as_figure = false;
                image_insert_path.clear();
              } else if (include_insert_mode) {
                include_insert_mode = false;
                include_insert_src.clear();
              } else if (link_edit_mode) {
                link_edit_mode = false;
                link_edit_is_cite = false;
                link_edit_href.clear();
              } else if (summary_edit_mode) {
                summary_edit_mode = false;
                summary_edit_text.clear();
              } else if (search_mode) {
                search_mode = false;
                replace_mode = false;
                replace_focus = false;
                search_query.clear();
                replace_text.clear();
                reset_blink();
              } else if (help_mode) {
                help_mode = false;
              } else if (!soft_wins.empty()) {
                soft_wins.pop_back();
              } else if (!p.sel.empty()) {
                p.sel.anchor = p.sel.focus;
                reset_blink();
              }
              // Esc no longer quits — use Ctrl+Q
              break;

            case SDLK_F1:
              help_mode = !help_mode;
              if (help_mode) {
                ensure_help_doc();
                help_needs_layout = true;
              }
              break;

            case SDLK_F2:
              browse_mode = !browse_mode;
              if (browse_mode) {
                search_mode = false;
                save_as_mode = false;
                open_file_mode = false;
                link_edit_mode = false;
                flash_status(
                    "Browse mode — read-only · gamepad navigate · F2 edits");
              } else {
                flash_status("Edit mode");
              }
              break;

            case SDLK_EQUALS:
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
              if (ctrl) {
                body_px = std::min(48, body_px + 1);
                heading_px = std::min(64, heading_px + 2);
                fonts.set_sizes(body_px, heading_px);
                needs_layout = true;
              }
              break;

            case SDLK_MINUS:
            case SDLK_KP_MINUS:
              if (ctrl && shift) {
                // Insert horizontal rule after current block
                edit();
                if (xanatoo::insert_sibling_after(
                        d, p.sel.focus, xanatoo::NodeKind::HorizontalRule)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
                break;
              }
              if (ctrl) {
                body_px = std::max(10, body_px - 1);
                heading_px = std::max(14, heading_px - 2);
                fonts.set_sizes(body_px, heading_px);
                needs_layout = true;
              }
              break;

            case SDLK_q:
              if (ctrl && shift) {
                // Toggle block quote on the current text block
                edit();
                auto& node = d.get(p.sel.focus.node);
                if (node.kind == xanatoo::NodeKind::BlockQuote) {
                  node.kind = xanatoo::NodeKind::Paragraph;
                } else if (node.kind == xanatoo::NodeKind::Paragraph ||
                           node.kind == xanatoo::NodeKind::Heading ||
                           node.kind == xanatoo::NodeKind::Header ||
                           node.kind == xanatoo::NodeKind::ListItem) {
                  node.kind = xanatoo::NodeKind::BlockQuote;
                  node.level = 0;
                }
                needs_layout = true;
                reset_blink();
              } else if (ctrl) {
                if (any_dirty() && !discard_armed) {
                  discard_armed = true;
                  flash_status(
                      "Unsaved changes — Ctrl+S to save, or Ctrl+Q again to quit");
                  std::cerr
                      << "Unsaved changes — save first, or Ctrl+Q again to quit.\n";
                  break;
                }
                discard_armed = false;
                running = false;
              }
              break;

            case SDLK_w:
              if (ctrl) {
                // Soft window → help → outline → document tab
                if (!soft_wins.empty()) {
                  soft_wins.pop_back();
                  flash_status("Closed window");
                } else if (help_mode) {
                  help_mode = false;
                  help_dragging = false;
                } else if (outline_mode) {
                  outline_mode = false;
                  ol_dragging = false;
                  ol_resize = 0;
                } else {
                  // Close focused tab
                  int tab_i = -1;
                  for (int ti = 0; ti < static_cast<int>(tab_order.size()); ++ti)
                    if (tab_order[static_cast<size_t>(ti)] == p.buf) {
                      tab_i = ti;
                      break;
                    }
                  if (tab_i >= 0) {
                    // Reuse CloseTab logic via command path is heavy; inline.
                    int bi = tab_order[static_cast<size_t>(tab_i)];
                    if (allow_discard_dirty(bi)) {
                      tab_order.erase(tab_order.begin() + tab_i);
                      if (tab_order.empty()) {
                        DocBuffer b;
                        b.doc = xanatoo::parse_document(
                            xanatoo::fallback_document(), {});
                        buffers.push_back(std::move(b));
                        int nbi = static_cast<int>(buffers.size()) - 1;
                        tab_order.push_back(nbi);
                        panes[0].buf = panes[1].buf = nbi;
                      } else {
                        int next = tab_order[static_cast<size_t>(std::min(
                            tab_i, static_cast<int>(tab_order.size()) - 1))];
                        if (panes[0].buf == bi) panes[0].buf = next;
                        if (panes[1].buf == bi) panes[1].buf = next;
                      }
                      needs_layout = true;
                      update_title();
                      flash_status("Tab closed");
                    }
                  }
                }
              }
              break;

            case SDLK_f:
              if (ctrl && shift) {
                // View-local section fold (not stored in the document).
                toggle_fold_at_caret();
                break;
              }
              if (ctrl && alt) {
                // Body shows only the focused outline section.
                toggle_section_focus_mode();
                break;
              }
              if (ctrl) {
                search_mode = true;
                replace_mode = false;
                search_query.clear();
                std::cout << "Leap search: type to find, F3 next, Esc cancel\n";
                reset_blink();
              }
              break;

            case SDLK_h:
              if (ctrl && shift) {
                history_panel_open = !history_panel_open;
                break;
              }
              if (ctrl) {
                // Leap replace: find with current query (or enter search), then type replacement
                search_mode = true;
                replace_mode = true;
                replace_text.clear();
                if (search_query.empty()) {
                  std::cout << "Leap replace: type find text, Tab to edit replacement, Enter replaces\n";
                } else {
                  std::cout << "Leap replace: type replacement for \"" << search_query << "\"\n";
                }
                reset_blink();
              }
              break;

            case SDLK_F3:
            case SDLK_g:
              if (ev.key.keysym.sym == SDLK_F3 || ctrl) {
                if (search_query.empty() && !search_mode) break;
                search_mode = true;
                Pane& pp = F();
                size_t mlen = 0;
                const bool backward =
                    shift || (ev.key.keysym.mod & KMOD_SHIFT) != 0;
                auto hit =
                    backward
                        ? xanatoo::find_prev(doc_of(pp), search_query,
                                             pp.sel.focus, mlen)
                        : xanatoo::find_next(doc_of(pp), search_query,
                                             pp.sel.focus, mlen);
                if (hit) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  xanatoo::advance_caret(doc_of(pp), pp.sel.focus, mlen);
                  scroll_caret_into_view(pp);
                  reset_blink();
                }
              }
              break;

            case SDLK_TAB:
              if (replace_mode) {
                replace_focus = !replace_focus;
                break;
              }
              // Ctrl+Tab / Ctrl+Shift+Tab — cycle document tabs
              if (ctrl && !tab_order.empty()) {
                auto& p = F();
                int cur = 0;
                for (int ti = 0; ti < static_cast<int>(tab_order.size()); ++ti)
                  if (tab_order[static_cast<size_t>(ti)] == p.buf) {
                    cur = ti;
                    break;
                  }
                int n = static_cast<int>(tab_order.size());
                cur = shift ? (cur - 1 + n) % n : (cur + 1) % n;
                p.buf = tab_order[static_cast<size_t>(cur)];
                xanatoo::ensure_valid_caret(doc_of(p), p.sel.focus);
                p.sel.anchor = p.sel.focus;
                p.layout = {};
                needs_layout = true;
                update_title();
                break;
              }
              // Tab / Shift+Tab: nest or promote list items; move table cells
              {
                auto& d = doc_of(F());
                auto& p = F();
                if (p.sel.focus.node != xanatoo::INVALID_NODE &&
                    d.get(p.sel.focus.node).kind ==
                        xanatoo::NodeKind::ListItem) {
                  edit();
                  bool ok = shift ? xanatoo::outdent_list_item(d, p.sel.focus)
                                  : xanatoo::indent_list_item(d, p.sel.focus);
                  if (ok) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  break;
                }
                if (p.sel.focus.node != xanatoo::INVALID_NODE &&
                    d.get(p.sel.focus.node).kind ==
                        xanatoo::NodeKind::TableCell) {
                  if (xanatoo::table_move_cell(d, p.sel.focus, !shift)) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  break;
                }
              }
              if (split) {
                focus = 1 - focus;
                reset_blink();
              }
              break;

            case SDLK_BACKSLASH:
              if (ctrl) {
                split = !split;
                if (split) {
                  // Second pane starts on the same buffer as the first.
                  panes[1].buf = panes[0].buf;
                  panes[1].sel = panes[0].sel;
                  panes[1].scroll_y = panes[0].scroll_y;
                } else {
                  focus = 0;
                }
                needs_layout = true;
              }
              break;

            case SDLK_BACKSPACE:
              if (path_in_window_mode) {
                if (!path_in_window_buf.empty()) {
                  while (!path_in_window_buf.empty() &&
                         (static_cast<unsigned char>(path_in_window_buf.back()) & 0xc0) ==
                             0x80)
                    path_in_window_buf.pop_back();
                  if (!path_in_window_buf.empty()) path_in_window_buf.pop_back();
                }
                break;
              }
              if (open_file_mode) {
                if (!open_file_buf.empty()) {
                  while (!open_file_buf.empty() &&
                         (static_cast<unsigned char>(open_file_buf.back()) & 0xc0) ==
                             0x80)
                    open_file_buf.pop_back();
                  if (!open_file_buf.empty()) open_file_buf.pop_back();
                }
                break;
              }
              if (url_bar_mode) {
                if (!url_bar_buf.empty()) {
                  while (!url_bar_buf.empty() &&
                         (static_cast<unsigned char>(url_bar_buf.back()) & 0xc0) ==
                             0x80)
                    url_bar_buf.pop_back();
                  if (!url_bar_buf.empty()) url_bar_buf.pop_back();
                }
                break;
              }
              if (save_as_mode) {
                minibuf_sync_out();
                if (!save_as_path.empty()) {
                  save_as_path = xanatoo::expand_user_path(save_as_path);
                  while (!save_as_path.empty() &&
                         (static_cast<unsigned char>(save_as_path.back()) & 0xc0) == 0x80)
                    save_as_path.pop_back();
                  if (!save_as_path.empty()) save_as_path.pop_back();
                }
                break;
              }
              if (image_insert_mode) {
                if (!image_insert_path.empty()) {
                  while (!image_insert_path.empty() &&
                         (static_cast<unsigned char>(image_insert_path.back()) &
                          0xc0) == 0x80)
                    image_insert_path.pop_back();
                  if (!image_insert_path.empty()) image_insert_path.pop_back();
                }
                break;
              }
              if (include_insert_mode) {
                if (!include_insert_src.empty()) {
                  while (!include_insert_src.empty() &&
                         (static_cast<unsigned char>(include_insert_src.back()) &
                          0xc0) == 0x80)
                    include_insert_src.pop_back();
                  if (!include_insert_src.empty()) include_insert_src.pop_back();
                }
                break;
              }
              if (link_edit_mode) {
                if (!link_edit_href.empty()) {
                  while (!link_edit_href.empty() &&
                         (static_cast<unsigned char>(link_edit_href.back()) & 0xc0) == 0x80)
                    link_edit_href.pop_back();
                  if (!link_edit_href.empty()) link_edit_href.pop_back();
                }
                break;
              }
              if (summary_edit_mode) {
                if (!summary_edit_text.empty()) {
                  while (!summary_edit_text.empty() &&
                         (static_cast<unsigned char>(summary_edit_text.back()) & 0xc0) == 0x80)
                    summary_edit_text.pop_back();
                  if (!summary_edit_text.empty()) summary_edit_text.pop_back();
                }
                break;
              }
              if (search_mode) {
                std::string& target = (replace_mode && replace_focus) ? replace_text : search_query;
                if (!target.empty()) {
                  while (!target.empty() &&
                         (static_cast<unsigned char>(target.back()) & 0xc0) == 0x80)
                    target.pop_back();
                  if (!target.empty()) target.pop_back();
                }
                reset_blink();
                break;
              }
              edit();
              if (!p.sel.empty()) {
                xanatoo::delete_selection(d, p.sel);
              } else {
                xanatoo::delete_backward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_DELETE:
              edit();
              if (!p.sel.empty()) {
                xanatoo::delete_selection(d, p.sel);
              } else {
                xanatoo::delete_forward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (replace_mode && search_mode && !search_query.empty()) {
                Pane& pp = F();
                xanatoo::Document& dd = doc_of(pp);
                if (ctrl && shift) {
                  // Replace all occurrences
                  edit();
                  size_t n =
                      xanatoo::replace_all(dd, search_query, replace_text);
                  needs_layout = true;
                  std::cout << "Replace all: " << n << " substitution(s)\n";
                  reset_blink();
                  break;
                }
                if (!pp.sel.empty()) {
                  edit();
                  xanatoo::delete_selection(dd, pp.sel);
                  if (!replace_text.empty()) {
                    xanatoo::insert_text(dd, pp.sel.focus, replace_text);
                    pp.sel.anchor = pp.sel.focus;
                  }
                  needs_layout = true;
                }
                // Find next
                size_t mlen = 0;
                if (auto hit = xanatoo::find_next(dd, search_query, pp.sel.focus, mlen)) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  xanatoo::advance_caret(dd, pp.sel.focus, mlen);
                  scroll_caret_into_view(pp);
                }
                reset_blink();
                break;
              }
              if (link_edit_mode) {
                if (link_edit_node != xanatoo::INVALID_NODE) {
                  auto& node = d.get(link_edit_node);
                  // BiblioEntry: edit the entry's label (cite target key)
                  if (node.kind == xanatoo::NodeKind::BiblioEntry &&
                      link_edit_is_cite) {
                    std::string key = link_edit_href;
                    if (key.rfind("cite:", 0) == 0) key = key.substr(5);
                    if (!key.empty() && key[0] == '#') key = key.substr(1);
                    if (key.empty()) key = "key";
                    node.label = key;
                    mark_dirty();
                    needs_layout = true;
                    std::cout << "Bibliography key: " << key << "\n";
                  } else if (link_edit_span < node.spans.size()) {
                    auto& sp = node.spans[link_edit_span];
                    if (link_edit_is_cite) {
                      // Strip optional cite: / # prefix from the typed key
                      std::string key = link_edit_href;
                      if (key.rfind("cite:", 0) == 0) key = key.substr(5);
                      if (!key.empty() && key[0] == '#') key = key.substr(1);
                      sp.href = key;
                      sp.kind = xanatoo::InlineKind::Cite;
                      std::cout << "Citation key: " << key << "\n";
                    } else {
                      // Accept "href rel=glossary" or "href {rel=glossary}"
                      std::string raw = link_edit_href;
                      std::string rel;
                      auto strip_rel = [&](std::string& s) {
                        auto rpos = s.find("rel=");
                        if (rpos == std::string::npos) return;
                        size_t after = rpos + 4;
                        while (after < s.size() &&
                               (s[after] == ' ' || s[after] == '\t'))
                          ++after;
                        size_t end = after;
                        while (end < s.size() && s[end] != ' ' &&
                               s[end] != '\t' && s[end] != '}')
                          ++end;
                        rel = s.substr(after, end - after);
                        // Remove the rel clause (and optional braces) from href
                        size_t cut = rpos;
                        while (cut > 0 &&
                               (s[cut - 1] == ' ' || s[cut - 1] == '\t' ||
                                s[cut - 1] == '{'))
                          --cut;
                        s = s.substr(0, cut);
                        while (!s.empty() &&
                               (s.back() == ' ' || s.back() == '\t'))
                          s.pop_back();
                      };
                      strip_rel(raw);
                      sp.href = raw;
                      if (!rel.empty()) sp.rel = rel;
                      sp.kind = xanatoo::InlineKind::Link;
                      std::cout << "Link href: " << sp.href;
                      if (!sp.rel.empty()) std::cout << " rel=" << sp.rel;
                      std::cout << "\n";
                    }
                    mark_dirty();
                    needs_layout = true;
                  }
                }
                link_edit_mode = false;
                link_edit_is_cite = false;
                link_edit_href.clear();
                break;
              }
              if (summary_edit_mode) {
                if (summary_edit_node != xanatoo::INVALID_NODE) {
                  d.get(summary_edit_node).summary = summary_edit_text;
                  mark_dirty();
                  needs_layout = true;
                }
                summary_edit_mode = false;
                summary_edit_text.clear();
                break;
              }
              if (path_in_window_mode) {
                minibuf_sync_out();
                if (!path_in_window_buf.empty()) {
                  std::string path = xanatoo::expand_user_path(path_in_window_buf);
                  if (!fs::exists(path)) {
                    std::string found = find_repo_file(path.c_str());
                    if (!found.empty()) path = found;
                  }
                  open_browse(path, path);
                  minibuf_commit_path_hist();
                }
                path_in_window_mode = false;
                path_in_window_buf.clear();
                minibuf.clear();
                break;
              }
              if (url_bar_mode) {
                minibuf_sync_out();
                std::string target = url_bar_buf;
                url_bar_mode = false;
                url_bar_buf.clear();
                if (!target.empty()) minibuf_commit_url_hist();
                minibuf.clear();
                navigate_to_url(std::move(target));
                break;
              }
              if (open_file_mode) {
                minibuf_sync_out();
                if (!open_file_buf.empty()) {
                  std::string path = xanatoo::expand_user_path(open_file_buf);
                  if (!fs::exists(path)) {
                    std::string found = find_repo_file(path.c_str());
                    if (!found.empty()) path = found;
                  }
                  if (!fs::exists(path)) {
                    flash_status("Open: file not found");
                    std::cerr << "Open: file not found: " << path << "\n";
                  } else {
                    int existing = find_buffer(path);
                    if (existing >= 0) {
                      // Already open — switch the focused pane to that tab.
                      p.buf = existing;
                      needs_layout = true;
                      update_title();
                      push_nav_history(path);
                      flash_status("Switched to " + path);
                      minibuf_commit_path_hist();
                      open_file_mode = false;
                      open_file_buf.clear();
                      minibuf.clear();
                    } else if (!allow_discard_dirty(p.buf)) {
                      flash_status(
                          "Unsaved changes — Enter again to discard and open");
                      break;
                    } else {
                      buffers[static_cast<size_t>(p.buf)].doc = load_doc(path);
                      path_of(p) = path;
                      buffers[static_cast<size_t>(p.buf)].dirty = false;
                      xanatoo::ensure_valid_caret(
                          buffers[static_cast<size_t>(p.buf)].doc, p.sel.focus);
                      p.sel.anchor = p.sel.focus;
                      p.scroll_y = 0.f;
                      buffers[static_cast<size_t>(p.buf)].history.reset(
                          buffers[static_cast<size_t>(p.buf)].doc, p.sel);
                      if (panes[0].buf == panes[1].buf) {
                        panes[1 - focus].sel = p.sel;
                        panes[1 - focus].scroll_y = 0.f;
                      }
                      // Ensure path is on the tab strip
                      if (std::find(tab_order.begin(), tab_order.end(),
                                    p.buf) == tab_order.end())
                        tab_order.push_back(p.buf);
                      needs_layout = true;
                      update_title();
                      push_nav_history(path);
                      flash_status("Opened " + path);
                      std::cout << "Opened: " << path << "\n";
                      minibuf_commit_path_hist();
                      open_file_mode = false;
                      open_file_buf.clear();
                    }
                  }
                } else {
                  open_file_mode = false;
                  open_file_buf.clear();
                }
                break;
              }
              if (save_as_mode) {
                minibuf_sync_out();
                if (!save_as_path.empty()) {
                  save_as_path = xanatoo::expand_user_path(save_as_path);
                  std::string text = xanatoo::serialize_document(d, save_as_path);
                  std::ofstream out(save_as_path);
                  if (!out) {
                    std::cerr << "Failed to save: " << save_as_path << "\n";
                  } else {
                    out << text;
                    path_of(p) = save_as_path;
                    buffers[static_cast<size_t>(p.buf)].dirty = false;
                    discard_armed = false;
                    update_title();
                    flash_status("Saved as " + save_as_path);
                    std::cout << "Saved as: " << save_as_path << "\n";
                    minibuf_commit_path_hist();
                  }
                }
                save_as_mode = false;
                save_as_path.clear();
                minibuf.clear();
                break;
              }
              if (image_insert_mode) {
                minibuf_sync_out();
                if (!image_insert_path.empty()) {
                  image_insert_path = xanatoo::expand_user_path(image_insert_path);
                  edit();
                  bool ok = false;
                  if (image_insert_as_figure) {
                    ok = static_cast<bool>(xanatoo::insert_figure(
                        d, p.sel.focus, image_insert_path));
                    if (ok)
                      std::cout << "Inserted figure: " << image_insert_path
                                << "\n";
                  } else {
                    ok = static_cast<bool>(xanatoo::insert_image(
                        d, p.sel.focus, image_insert_path));
                    if (ok)
                      std::cout << "Inserted image: " << image_insert_path
                                << "\n";
                  }
                  if (ok) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                    minibuf_commit_path_hist();
                  }
                }
                image_insert_mode = false;
                image_insert_as_figure = false;
                image_insert_path.clear();
                minibuf.clear();
                break;
              }
              if (include_insert_mode) {
                minibuf_sync_out();
                if (!include_insert_src.empty()) {
                  edit();
                  if (xanatoo::insert_transclude(d, p.sel.focus,
                                                 include_insert_src)) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                    std::cout << "Inserted include: " << include_insert_src
                              << "\n";
                    minibuf_commit_path_hist();
                  }
                }
                include_insert_mode = false;
                include_insert_src.clear();
                minibuf.clear();
                break;
              }
              // Ctrl+Enter: new empty paragraph after current block
              if (ctrl) {
                edit();
                if (xanatoo::insert_sibling_after(
                        d, p.sel.focus, xanatoo::NodeKind::Paragraph)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
                break;
              }
              edit();
              if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
              xanatoo::split_block(d, p.sel.focus);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_LEFT:
              if (alt && !ctrl) {
                // Capsule history back
                if (nav_hist_pos > 0) {
                  --nav_hist_pos;
                  navigate_to_url(nav_history[static_cast<size_t>(nav_hist_pos)],
                                  /*record_history=*/false);
                } else {
                  flash_status("No previous page");
                }
                break;
              }
              move_and_sel([&] {
                if (ctrl) {
                  xanatoo::move_word_left(d, p.sel.focus);
                } else if (!xanatoo::move_left(d, p.sel.focus)) {
                  xanatoo::move_to_prev_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_RIGHT:
              if (alt && !ctrl) {
                // Capsule history forward
                if (nav_hist_pos + 1 < static_cast<int>(nav_history.size())) {
                  ++nav_hist_pos;
                  navigate_to_url(nav_history[static_cast<size_t>(nav_hist_pos)],
                                  /*record_history=*/false);
                } else {
                  flash_status("No next page");
                }
                break;
              }
              move_and_sel([&] {
                if (ctrl) {
                  xanatoo::move_word_right(d, p.sel.focus);
                } else if (!xanatoo::move_right(d, p.sel.focus)) {
                  xanatoo::move_to_next_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_UP:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0 &&
                  (ev.key.keysym.mod & KMOD_SHIFT) != 0) {
                edit();
                if (xanatoo::move_section_up(d, p.sel.focus)) {
                  needs_layout = true;
                  reset_blink();
                  flash_status("Section moved up");
                }
              } else if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (xanatoo::move_block_up(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!xanatoo::move_up(p.layout, p.sel.focus, p.preferred_x))
                    xanatoo::move_to_prev_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_DOWN:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (xanatoo::move_block_down(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!xanatoo::move_down(p.layout, p.sel.focus, p.preferred_x))
                    xanatoo::move_to_next_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN: {
              const bool up = (ev.key.keysym.sym == SDLK_PAGEUP);
              // When the pointer is over soft chrome, page that surface instead
              // of the document (matches wheel routing).
              int mxk = 0, myk = 0;
              SDL_GetMouseState(&mxk, &myk);
              auto page_scroll = [&](float& scroll, float content_h,
                                     float view_h) {
                if (view_h < 1.f) view_h = 1.f;
                const float step = view_h * 0.85f;
                float max_s = std::max(0.f, content_h - view_h + 40.f);
                if (up)
                  scroll = std::max(0.f, scroll - step);
                else
                  scroll = std::min(max_s, scroll + step);
              };
              if (outline_mode && mxk >= ol_x && mxk < ol_x + ol_w &&
                  myk >= ol_y && myk < ol_y + ol_h) {
                page_scroll(ol_scroll, static_cast<float>(outline.size()) * 22.f,
                            static_cast<float>(ol_h - help_title_h - 8));
                break;
              }
              {
                bool soft_paged = false;
                for (int i = static_cast<int>(soft_wins.size()) - 1; i >= 0;
                     --i) {
                  SoftWindow& sw = soft_wins[static_cast<size_t>(i)];
                  if (mxk >= sw.x && mxk < sw.x + sw.w && myk >= sw.y &&
                      myk < sw.y + sw.h) {
                    auto [cx, cy, cw, ch] = soft_client_rect(sw);
                    page_scroll(sw.scroll, sw.layout.content_height,
                                static_cast<float>(std::max(1, ch)));
                    soft_paged = true;
                    break;
                  }
                }
                if (soft_paged) break;
              }
              if (help_mode && mxk >= help_x && mxk < help_x + help_w &&
                  myk >= help_y && myk < help_y + help_h) {
                auto [cx, cy, cw, ch] = help_client_rect();
                page_scroll(help_scroll, help_layout.content_height,
                            static_cast<float>(std::max(1, ch)));
                break;
              }
              const float view_h =
                  static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
              const float step = view_h * 0.85f;
              float max_s = std::max(
                  0.f, p.layout.content_height - view_h + 40.f);
              if (up)
                p.scroll_y = std::max(0.f, p.scroll_y - step);
              else
                p.scroll_y = std::min(max_s, p.scroll_y + step);
              ensure_virtual_layout(p);
              // Move caret to a glyph near the top of the new viewport
              move_and_sel([&] {
                if (p.layout.glyphs.empty()) return;
                float target_y = p.scroll_y + view_h * 0.15f;
                float best = 1e9f;
                xanatoo::Caret best_c = p.sel.focus;
                for (const auto& g : p.layout.glyphs) {
                  float dy = std::abs(g.y - target_y);
                  if (dy < best) {
                    best = dy;
                    best_c = {g.node, g.span_index, g.char_index};
                  }
                }
                p.sel.focus = best_c;
              });
              reset_blink();
              break;
            }

            case SDLK_HOME:
              move_and_sel([&] {
                if (ctrl) {
                  xanatoo::move_to_document_start(d, p.sel.focus);
                } else {
                  // Line start via layout: find leftmost glyph on same baseline
                  auto cpos = xanatoo::caret_screen_pos(
                      p.layout, p.sel.focus, p.scroll_y,
                      static_cast<float>(body_font.line_height()));
                  if (cpos.valid) {
                    float best_x = 1e9f;
                    xanatoo::Caret best = p.sel.focus;
                    for (const auto& g : p.layout.glyphs) {
                      if (g.node != p.sel.focus.node) continue;
                      if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
                      if (g.x < best_x) {
                        best_x = g.x;
                        best = {g.node, g.span_index, g.char_index};
                      }
                    }
                    p.sel.focus = best;
                  } else {
                    p.sel.focus.span_index = 0;
                    p.sel.focus.char_index = 0;
                  }
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_END:
              move_and_sel([&] {
                if (ctrl) {
                  xanatoo::move_to_document_end(d, p.sel.focus);
                } else {
                  auto cpos = xanatoo::caret_screen_pos(
                      p.layout, p.sel.focus, p.scroll_y,
                      static_cast<float>(body_font.line_height()));
                  if (cpos.valid) {
                    float best_x = -1e9f;
                    xanatoo::Caret best = p.sel.focus;
                    for (const auto& g : p.layout.glyphs) {
                      if (g.node != p.sel.focus.node) continue;
                      if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
                      if (g.x >= best_x) {
                        best_x = g.x;
                        best = {g.node, g.span_index, g.char_index + 1};
                      }
                    }
                    p.sel.focus = best;
                  }
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_c:
              if (ctrl && shift && !p.sel.empty()) {
                // Ctrl+Shift+C — wrap selection as a citation; type the key
                std::string text = xanatoo::selected_text(d, p.sel);
                std::string key = text;
                for (char& ch : key) {
                  if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch - 'A' + 'a');
                  else if (!((ch >= 'a' && ch <= 'z') ||
                             (ch >= '0' && ch <= '9')))
                    ch = '-';
                }
                while (!key.empty() && key.front() == '-')
                  key.erase(key.begin());
                while (!key.empty() && key.back() == '-') key.pop_back();
                if (key.empty()) key = "key";
                if (key.size() > 32) key.resize(32);
                edit();
                xanatoo::apply_style_to_selection(
                    d, p.sel, xanatoo::InlineKind::Cite, key);
                link_edit_mode = true;
                link_edit_is_cite = true;
                search_mode = false;
                save_as_mode = false;
                link_edit_node = p.sel.focus.node;
                link_edit_span = p.sel.focus.span_index;
                link_edit_href = key;
                needs_layout = true;
                reset_blink();
              } else if (ctrl) {
                std::string text = xanatoo::selected_text(d, p.sel);
                if (!text.empty()) SDL_SetClipboardText(text.c_str());
              }
              break;

            case SDLK_x:
              if (ctrl && shift) {
                if (!p.sel.empty()) {
                  edit();
                  xanatoo::apply_style_to_selection(
                      d, p.sel, xanatoo::InlineKind::Strike);
                  needs_layout = true;
                  reset_blink();
                }
              } else if (ctrl) {
                std::string text = xanatoo::selected_text(d, p.sel);
                if (!text.empty()) {
                  SDL_SetClipboardText(text.c_str());
                  edit();
                  xanatoo::delete_selection(d, p.sel);
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_v:
              if (ctrl) {
                if (SDL_HasClipboardText()) {
                  char* clip = SDL_GetClipboardText();
                  if (clip && clip[0]) {
                    // Status-bar prompts: paste into the active buffer, not the doc.
                    auto append_clip = [&](std::string& buf) {
                      // Single-line prompts: strip newlines.
                      for (const char* s = clip; *s; ++s) {
                        if (*s == '\n' || *s == '\r') continue;
                        buf.push_back(*s);
                      }
                    };
                    if (save_as_mode) {
                      append_clip(save_as_path);
                    } else if (path_in_window_mode) {
                      append_clip(path_in_window_buf);
                    } else if (open_file_mode) {
                      append_clip(open_file_buf);
                    } else if (url_bar_mode) {
                      append_clip(url_bar_buf);
                    } else if (image_insert_mode) {
                      append_clip(image_insert_path);
                    } else if (include_insert_mode) {
                      append_clip(include_insert_src);
                    } else if (link_edit_mode) {
                      append_clip(link_edit_href);
                    } else if (summary_edit_mode) {
                      append_clip(summary_edit_text);
                    } else if (search_mode) {
                      if (replace_mode && replace_focus)
                        append_clip(replace_text);
                      else
                        append_clip(search_query);
                    } else {
                      edit();
                      if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
                      xanatoo::paste_plain_text(d, p.sel.focus, clip);
                      p.sel.anchor = p.sel.focus;
                      needs_layout = true;
                      reset_blink();
                    }
                  }
                  if (clip) SDL_free(clip);
                }
              }
              break;

            case SDLK_z:
              if (ctrl) {
                if (buffers[static_cast<size_t>(p.buf)].history.undo(d, p.sel)) {
                  if (panes[0].buf == panes[1].buf) {
                    xanatoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  drop_height_hints_for_buf(p.buf);
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_y:
              if (ctrl) {
                if (buffers[static_cast<size_t>(p.buf)].history.redo(d, p.sel)) {
                  if (panes[0].buf == panes[1].buf) {
                    xanatoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  drop_height_hints_for_buf(p.buf);
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_n:
              if (ctrl) {
                if (!allow_discard_dirty(p.buf)) break;
                buffers[static_cast<size_t>(p.buf)].doc =
                    xanatoo::parse_document(xanatoo::fallback_document(), {});
                path_of(p).clear();
                buffers[static_cast<size_t>(p.buf)].dirty = false;
                xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(p.buf)].doc, p.sel.focus);
                p.sel.anchor = p.sel.focus;
                p.scroll_y = 0.f;
                p.layout = {};
                drop_height_hints_for_buf(p.buf);
                buffers[static_cast<size_t>(p.buf)].history.reset(buffers[static_cast<size_t>(p.buf)].doc, p.sel);
                if (panes[0].buf == panes[1].buf) {
                  panes[1 - focus].sel = p.sel;
                  panes[1 - focus].scroll_y = 0.f;
                  panes[1 - focus].layout = {};
                }
                needs_layout = true;
                update_title();
                flash_status("New document");
              }
              break;

            case SDLK_s:
              if (ctrl && shift) {
                save_as_mode = true;
                minibuf_begin(save_as_path);
                search_mode = false;
                save_as_path = path_of(p).empty() ? "untitled.md" : path_of(p);
                break;
              }
              if (ctrl) {
                std::string& spath = path_of(p);
                if (spath.empty()) {
                  std::string candidate = "untitled.md";
                  for (int n = 1; n < 1000; ++n) {
                    if (!std::filesystem::exists(candidate)) break;
                    candidate = "untitled-" + std::to_string(n) + ".md";
                  }
                  spath = candidate;
                  std::cout << "Saving untitled buffer as: " << spath << "\n";
                }
                {
                  std::string text = xanatoo::serialize_document(d, spath);
                  std::ofstream out(spath);
                  if (!out) {
                    std::cerr << "Failed to save: " << spath << "\n";
                  } else {
                    out << text;
                    std::cout << "Saved: " << spath << "\n";
                    buffers[static_cast<size_t>(p.buf)].dirty = false;
                    discard_armed = false;
                    update_title();
                    flash_status("Saved " + spath);
                  }
                }
              }
              break;

            case SDLK_o:
              if (ctrl && alt) {
                outline_docked = !outline_docked;
                if (outline_mode) {
                  needs_layout = true;
                  layout_panes();
                }
                flash_status(outline_docked ? "Outline docked (left)"
                                            : "Outline floating");
                break;
              }
              if (ctrl && shift) {
                outline_mode = !outline_mode;
                if (outline_mode) {
                  refresh_outline();
                  outline_sel = 0;
                  ol_scroll = 0.f;
                  // Prefer selecting the heading nearest the caret
                  for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
                    if (outline[static_cast<size_t>(i)].node ==
                        p.sel.focus.node) {
                      outline_sel = i;
                      break;
                    }
                  }
                }
                needs_layout = true;
                layout_panes();
                break;
              }
              if (ctrl) {
                open_file_mode = true;
                minibuf_begin();
                open_file_buf.clear();
                path_in_window_mode = false;
                save_as_mode = false;
                search_mode = false;
              }
              break;

            case SDLK_r:
              if (ctrl) {
                // Reload focused document from disk or re-fetch gemini://
                std::string& rpath = path_of(p);
                if (rpath.empty()) {
                  flash_status("Reload: no path (Save As or Open URL first)");
                  break;
                }
                if (buffers[static_cast<size_t>(p.buf)].dirty && !shift) {
                  flash_status(
                      "Unsaved changes — Ctrl+Shift+R to discard and reload");
                  break;
                }
                if (is_in_app_remote_url(rpath)) {
                  navigate_to_url(rpath, /*record_history=*/false);
                  break;
                }
                xanatoo::clear_external_doc_cache();
                buffers[static_cast<size_t>(p.buf)].doc = load_doc(rpath);
                buffers[static_cast<size_t>(p.buf)].dirty = false;
                buffers[static_cast<size_t>(p.buf)].history.reset(buffers[static_cast<size_t>(p.buf)].doc, p.sel);
                for (int pi = 0; pi < 2; ++pi) {
                  if (panes[pi].buf != p.buf) continue;
                  xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(p.buf)].doc,
                                              panes[pi].sel.focus);
                  panes[pi].sel.anchor = panes[pi].sel.focus;
                  panes[pi].scroll_y = 0.f;
                  panes[pi].layout = {};
                }
                drop_height_hints_for_buf(p.buf);
                update_title();
                needs_layout = true;
                reset_blink();
                flash_status("Reloaded " + rpath);
                std::cout << "Reloaded: " << rpath << "\n";
              }
              break;

            case SDLK_d:
              if (ctrl && shift) {
                // Side-by-side version marks (needs two independent docs)
                if (!split) {
                  split = true;
                  needs_layout = true;
                }
                if (shared_doc() || buffers.size() < 2) {
                  flash_status(
                      "Diff needs two files — Examples → Diff demo, or "
                      "xanatoo a.md b.md");
                  std::cout << "Diff marks need two open files "
                               "(xanatoo a.md b.md).\n";
                } else {
                  diff_mode = !diff_mode;
                  refresh_diff();
                  flash_status(diff_mode ? "Diff marks on" : "Diff marks off");
                  std::cout << (diff_mode ? "Diff marks on\n"
                                          : "Diff marks off\n");
                }
                break;
              }
              if (ctrl) {
                edit();
                if (auto nid = xanatoo::duplicate_block(d, p.sel.focus.node)) {
                  p.sel.focus.node = *nid;
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_l:
              if (ctrl && shift) {
                structure_mode = !structure_mode;
                flash_status(structure_mode ? "Structure highlight on"
                                            : "Structure highlight off");
                std::cout << (structure_mode ? "Structure highlight on\n"
                                             : "Structure highlight off\n");
              } else if (ctrl && !shift) {
                // Focus interactive toolbar URL bar
                search_mode = false;
                save_as_mode = false;
                open_file_mode = false;
                path_in_window_mode = false;
                url_bar_mode = false;
                ui.focus_url_bar();
              }
              break;

            case SDLK_8:
              // Ctrl+Shift+8 — toggle unordered list (common editor binding)
              if (ctrl && shift) {
                edit();
                if (xanatoo::toggle_list(d, p.sel.focus, false)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_7:
              // Ctrl+Shift+7 — toggle ordered list
              if (ctrl && shift) {
                edit();
                if (xanatoo::toggle_list(d, p.sel.focus, true)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_t:
              if (ctrl && shift) {
                // Cycle task item state
                edit();
                if (xanatoo::toggle_task_item(d, p.sel.focus)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              } else if (ctrl) {
                // New tab
                DocBuffer b;
                b.doc = xanatoo::parse_document(xanatoo::fallback_document(), {});
                int bi = static_cast<int>(buffers.size());
                buffers.push_back(std::move(b));
                tab_order.push_back(bi);
                p.buf = bi;
                p.sel = {};
                xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(bi)].doc,
                                            p.sel.focus);
                p.sel.anchor = p.sel.focus;
                p.scroll_y = 0.f;
                buffers[static_cast<size_t>(bi)].history.reset(
                    buffers[static_cast<size_t>(bi)].doc, p.sel);
                needs_layout = true;
                update_title();
                flash_status("New tab");
              }
              break;

            case SDLK_p:
              if (ctrl && shift) {
                pop_out_focused();
              }
              break;

            case SDLK_b:
              if (ctrl && alt) {
                backlinks_mode = !backlinks_mode;
                if (backlinks_mode) {
                  refresh_backlinks();
                  backlinks_sel = 0;
                  bl_scroll = 0.f;
                }
                break;
              }
              if (ctrl && shift) {
                std::string url = path_of(p);
                if (url.empty()) url = gemini_current_url;
                if (is_bookmarked(url))
                  remove_bookmark(url);
                else
                  add_bookmark(url);
                break;
              }
              if (ctrl && !p.sel.empty()) {
                edit();
                xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Strong);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_i:
              if (ctrl && shift) {
                // Insert image — prompt for path in status bar
                image_insert_mode = true;
                minibuf_begin();
                image_insert_path.clear();
                save_as_mode = false;
                search_mode = false;
                link_edit_mode = false;
                summary_edit_mode = false;
                break;
              }
              if (ctrl && !p.sel.empty()) {
                edit();
                xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Emphasis);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_BACKQUOTE:
              if (ctrl && !p.sel.empty()) {
                edit();
                xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Code);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_k:
              if (ctrl && !p.sel.empty()) {
                std::string text = xanatoo::selected_text(d, p.sel);
                std::string href = "#" + text;
                edit();
                xanatoo::apply_style_to_selection(
                    d, p.sel, xanatoo::InlineKind::Link, href);
                link_edit_mode = true;
                link_edit_is_cite = false;
                search_mode = false;
                save_as_mode = false;
                link_edit_node = p.sel.focus.node;
                link_edit_span = p.sel.focus.span_index;
                link_edit_href = href;
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_e:
              if (ctrl && shift) {
                // Edit summary of enclosing stretch
                xanatoo::NodeId sid = xanatoo::enclosing_stretch(d, p.sel.focus.node);
                if (sid != xanatoo::INVALID_NODE) {
                  summary_edit_mode = true;
                  summary_edit_node = sid;
                  summary_edit_text = d.get(sid).summary;
                  search_mode = false;
                  save_as_mode = false;
                  link_edit_mode = false;
                }
              } else if (ctrl) {
                edit();
                std::string summary = xanatoo::selected_text(d, p.sel);
                if (xanatoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_u:
              if (ctrl && shift) {
                // Browse / capsule: copy page URL; edit: unwrap stretch
                std::string page = path_of(p);
                if (page.empty()) page = gemini_current_url;
                if (browse_mode || xanatoo::is_gemini_url(page)) {
                  if (page.empty()) {
                    flash_status("No page URL");
                  } else {
                    SDL_SetClipboardText(page.c_str());
                    flash_status("Copied " + page);
                  }
                } else {
                  edit();
                  if (auto first =
                          xanatoo::unwrap_stretch(d, p.sel.focus.node)) {
                    p.sel.focus.node = *first;
                    p.sel.focus.span_index = 0;
                    p.sel.focus.char_index = 0;
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                }
              }
              break;

            case SDLK_0:
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
            case SDLK_5:
            case SDLK_6:
              if (ctrl && shift) {
                // Ctrl+Shift+1/2 — wrap in Section / Article
                int n = static_cast<int>(ev.key.keysym.sym - SDLK_0);
                if (n == 1) {
                  edit();
                  if (xanatoo::wrap_in_section(d, p.sel.focus)) {
                    needs_layout = true;
                    reset_blink();
                  }
                } else if (n == 2) {
                  edit();
                  if (xanatoo::wrap_in_article(d, p.sel.focus)) {
                    needs_layout = true;
                    reset_blink();
                  }
                }
              } else if (ctrl && !shift) {
                int level = static_cast<int>(ev.key.keysym.sym - SDLK_0);
                edit();
                bool ok = false;
                if (level == 1)
                  ok = xanatoo::make_header(d, p.sel.focus);
                else
                  ok = xanatoo::set_heading_level(d, p.sel.focus, level);
                if (ok) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_LEFTBRACKET:
              if (ctrl && alt) {
                edit();
                if (xanatoo::demote_section(d, p.sel.focus)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  flash_status("Section demoted");
                } else {
                  flash_status("Cannot demote section");
                }
                break;
              }
              if (ctrl && shift) {
                // Collapse all stretch nodes
                edit();
                for (auto& node : d.nodes) {
                  if (node.kind == xanatoo::NodeKind::Stretch)
                    node.expanded = false;
                }
                needs_layout = true;
              }
              break;

            case SDLK_RIGHTBRACKET:
              if (ctrl && alt) {
                edit();
                if (xanatoo::promote_section(d, p.sel.focus)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  flash_status("Section promoted");
                } else {
                  flash_status("Cannot promote section");
                }
                break;
              }
              if (ctrl && shift) {
                // Expand all stretch nodes
                edit();
                for (auto& node : d.nodes) {
                  if (node.kind == xanatoo::NodeKind::Stretch)
                    node.expanded = true;
                }
                needs_layout = true;
              }
              break;

            case SDLK_a:
              if (ctrl && shift) {
                xanatoo::select_all(d, p.sel);
              } else if (ctrl) {
                xanatoo::select_node(d, p.sel, p.sel.focus.node);
              }
              break;

            default:
              break;
          }
          break;
        }
      }
    }

    // Keep layout width in sync with the real client area (not only on
    // SIZE_CHANGED — first map / HiDPI / tiling WMs often skip that event).
    // Ignore 0×0 (minimize / not mapped): accepting it set last_layout_w=0 and
    // forced a full relayout every frame → flicker then crash.
    {
      int nw = 0, nh = 0;
      SDL_GetWindowSize(window, &nw, &nh);
      if (nw >= 64 && nh >= 64 && (nw != win_w || nh != win_h)) {
        win_w = nw;
        win_h = nh;
        needs_layout = true;
        for (auto& sw : soft_wins) {
          if (sw.dock != SoftDock::None || sw.maximized)
            apply_soft_dock(sw, sw.maximized ? SoftDock::Max : sw.dock);
        }
      }
    }

    if (needs_layout && win_w >= 64 && win_h >= 64) {
      layout_panes();
      {
        std::string dk = path_of(F());
        if (dk != last_fold_doc_key) {
          last_fold_doc_key = dk;
          reload_folds_for_focused();
        }
      }
      if (outline_mode) refresh_outline();
      if (backlinks_mode) refresh_backlinks();
    }

    float lh = static_cast<float>(body_font.line_height());
    // ImGui menu + toolbar
    {
      xanatoo::UiState ui_state;
      ui_state.dirty = buffers[static_cast<size_t>(F().buf)].dirty;
      ui_state.has_path = !path_of(F()).empty();
      ui_state.split = split;
      ui_state.outline = outline_mode;
      ui_state.section_focus = section_focus_mode;
      ui_state.justify = view_settings.justify;
      ui_state.backlinks = backlinks_mode;
      ui_state.diff = diff_mode;
      ui_state.can_diff = !shared_doc() && panes[0].buf != panes[1].buf &&
                          panes[0].buf >= 0 && panes[1].buf >= 0;
      ui_state.structure = structure_mode;
      ui_state.view_line_spacing = view_settings.line_spacing;
      ui_state.view_margin_x = view_settings.margin_x;
      ui_state.view_max_content_width = view_settings.max_content_width;
      ui_state.browse = browse_mode;
      ui_state.virtual_keyboard = virtual_keyboard;
      ui_state.help = help_mode;
      ui_state.selection = !F().sel.empty();
      {
        auto& fd = doc_of(F());
        auto nid = F().sel.focus.node;
        // Guard: caret can briefly point at a node from a replaced tree.
        bool nid_ok = false;
        if (nid != xanatoo::INVALID_NODE) {
          try {
            (void)fd.get(nid);
            nid_ok = true;
          } catch (...) {
            xanatoo::ensure_valid_caret(fd, F().sel.focus);
            F().sel.anchor = F().sel.focus;
            nid = F().sel.focus.node;
            try {
              (void)fd.get(nid);
              nid_ok = nid != xanatoo::INVALID_NODE;
            } catch (...) {
              nid_ok = false;
            }
          }
        }
        ui_state.on_list_item =
            nid_ok && fd.get(nid).kind == xanatoo::NodeKind::ListItem;
        ui_state.in_stretch =
            nid_ok &&
            xanatoo::enclosing_stretch(fd, nid) != xanatoo::INVALID_NODE;
      }
      ui_state.doc_label = path_of(F()).empty() ? std::string("untitled")
                                                : path_of(F());
      ui_state.can_nav_back = nav_hist_pos > 0;
      ui_state.can_nav_forward =
          nav_hist_pos + 1 < static_cast<int>(nav_history.size());
      ui_state.history_open = history_panel_open;
      ui_state.bookmarks_open = bookmarks_panel_open;
      ui_state.history_entries = nav_history;
      ui_state.history_pos = nav_hist_pos;
      ui_state.bookmarks = bookmarks;
      {
        const std::string& cur = path_of(F());
        ui_state.current_bookmarked = is_bookmarked(cur);
      }
      ui_state.tab_labels.clear();
      ui_state.active_tab = 0;
      for (int ti = 0; ti < static_cast<int>(tab_order.size()); ++ti) {
        int bi = tab_order[static_cast<size_t>(ti)];
        ui_state.tab_labels.push_back(buffer_tab_label(bi));
        if (bi == panes[focus].buf) ui_state.active_tab = ti;
      }
      ui_state.has_floating = !soft_wins.empty();
      // Right-stick software cursor (extra pointer; never touches OS mouse).
      bool stick_active = false;
      if (gamepad) {
        auto axis_e = [&](SDL_GameControllerAxis a) -> float {
          Sint16 v = SDL_GameControllerGetAxis(gamepad, a);
          const float dead = 8000.f;
          if (v > -dead && v < dead) return 0.f;
          return static_cast<float>(v) / 32767.f;
        };
        if (!stick_cursor_init) {
          stick_cx = static_cast<float>(win_w) * 0.5f;
          stick_cy = static_cast<float>(win_h) * 0.5f;
          stick_cursor_init = true;
        }
        stick_cursor_on = true;
        float rx = axis_e(SDL_CONTROLLER_AXIS_RIGHTX);
        float ry = axis_e(SDL_CONTROLLER_AXIS_RIGHTY);
        if (rx != 0.f || ry != 0.f) {
          // Non-linear response: fine aim near centre, faster at full tilt.
          auto curve = [](float v) {
            float s = (v < 0.f) ? -1.f : 1.f;
            float a = std::fabs(v);
            return s * a * a;  // quadratic
          };
          rx = curve(rx);
          ry = curve(ry);
          const float speed = 11.f + 0.014f * static_cast<float>(win_w);
          stick_cx += rx * speed;
          stick_cy += ry * speed;
          if (stick_cx < 0.f) stick_cx = 0.f;
          if (stick_cy < 0.f) stick_cy = 0.f;
          if (stick_cx > static_cast<float>(win_w - 1))
            stick_cx = static_cast<float>(win_w - 1);
          if (stick_cy > static_cast<float>(win_h - 1))
            stick_cy = static_cast<float>(win_h - 1);
          stick_active = true;
        }
        if (stick_lmb || stick_rmb) stick_active = true;
        if (stick_active) {
          const int ix = static_cast<int>(stick_cx);
          const int iy = static_cast<int>(stick_cy);
          static int last_ix = -1, last_iy = -1;
          if (ix != last_ix || iy != last_iy || stick_lmb || stick_rmb) {
            last_ix = ix;
            last_iy = iy;
            // Synthetic events at stick position — OS cursor stays put.
            SDL_Event me{};
            me.type = SDL_MOUSEMOTION;
            me.motion.x = ix;
            me.motion.y = iy;
            me.motion.xrel = 0;
            me.motion.yrel = 0;
            me.motion.windowID = SDL_GetWindowID(window);
            me.motion.state = 0;
            if (stick_lmb) me.motion.state |= SDL_BUTTON(SDL_BUTTON_LEFT);
            if (stick_rmb) me.motion.state |= SDL_BUTTON(SDL_BUTTON_RIGHT);
            SDL_PushEvent(&me);
          }
        }
      }
      ui_state.stick_cursor = stick_cursor_on;
      ui_state.stick_active = stick_active;
      ui_state.stick_cx = stick_cx;
      ui_state.stick_cy = stick_cy;
      ui_state.stick_lmb = stick_lmb;
      ui_state.stick_rmb = stick_rmb;

      auto ui_frame = ui.begin_frame(ui_state);
      ui_top = ui_frame.top_reserved;
      ui_bottom = ui_frame.bottom_reserved;

      using C = xanatoo::UiCommand;
      auto& p = F();
      auto& d = doc_of(p);

      // Browse mode: ignore mutating commands from menus/toolbar.
      if (browse_mode && ui_frame.cmd != C::None) {
        const C c = ui_frame.cmd;
        const bool allowed =
            c == C::Quit || c == C::OpenDocument || c == C::OpenUrlBar ||
            c == C::NavigateUrl || c == C::NavBack || c == C::NavForward ||
            c == C::ToggleHistory || c == C::ToggleBookmarks ||
            c == C::AddBookmark || c == C::RemoveBookmark ||
            c == C::NewTab || c == C::CloseTab || c == C::NextTab ||
            c == C::PrevTab || c == C::SelectTab || c == C::PopOut ||
            c == C::PopIn ||
            c == C::Save ||
            c == C::SaveAs || c == C::ExportSexpr || c == C::ExportMarkdown ||
            c == C::Reload || c == C::ForceReload || c == C::Copy ||
            c == C::CopyPageUrl ||
            c == C::SelectAll || c == C::SelectBlock || c == C::Find ||
            c == C::ToggleSplit || c == C::ToggleOutline || c == C::ToggleBacklinks ||
            c == C::OpenTranscludeOther || c == C::ToggleDiff ||
            c == C::ToggleStructure || c == C::ToggleBrowse ||
            c == C::ToggleHelp || c == C::ToggleVirtualKeyboard ||
            c == C::OpenFormatDoc ||
            c == C::OpenSemanticDoc || c == C::OpenInWindow ||
            c == C::OpenExample || c == C::LoadDiffDemo || c == C::ZoomIn ||
            c == C::ZoomOut || c == C::ViewLineSpacingTight ||
            c == C::ViewLineSpacingNormal || c == C::ViewLineSpacingRelaxed ||
            c == C::ViewLineSpacingLoose || c == C::ViewMarginsNarrow ||
            c == C::ViewMarginsNormal || c == C::ViewMarginsWide ||
            c == C::ViewMarginsExtra || c == C::ViewMeasureFull ||
            c == C::ViewMeasureComfortable || c == C::ViewMeasureWide ||
            c == C::ToggleSectionFocus || c == C::ToggleJustify;
        if (!allowed) {
          flash_status("Browse mode — F2 to edit");
          ui_frame.cmd = C::None;
        }
      }

      switch (ui_frame.cmd) {
        case C::Quit: {
          if (any_dirty() && !discard_armed) {
            discard_armed = true;
            flash_status(
                "Unsaved changes — Ctrl+S to save, or Quit again to exit");
            break;
          }
          discard_armed = false;
          running = false;
          break;
        }
        case C::NewDocument: {
          if (!allow_discard_dirty(p.buf)) break;
          buffers[static_cast<size_t>(p.buf)].doc =
              xanatoo::parse_document(xanatoo::fallback_document(), {});
          path_of(p).clear();
          buffers[static_cast<size_t>(p.buf)].dirty = false;
          xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(p.buf)].doc, p.sel.focus);
          p.sel.anchor = p.sel.focus;
          p.scroll_y = 0.f;
          buffers[static_cast<size_t>(p.buf)].history.reset(buffers[static_cast<size_t>(p.buf)].doc, p.sel);
          if (panes[0].buf == panes[1].buf) {
            panes[1 - focus].sel = p.sel;
            panes[1 - focus].scroll_y = 0.f;
          }
          for (auto& sw : soft_wins)
            if (sw.buf == p.buf) {
              sw.needs_layout = true;
              sw.scroll = 0.f;
              sw.sel = p.sel;
            }
          needs_layout = true;
          update_title();
          flash_status("New document");
          break;
        }
        case C::OpenDocument:
          open_file_mode = true;
          minibuf_begin();
          open_file_buf.clear();
          path_in_window_mode = false;
          save_as_mode = false;
          search_mode = false;
          url_bar_mode = false;
          break;
        case C::OpenUrlBar:
          search_mode = false;
          save_as_mode = false;
          open_file_mode = false;
          path_in_window_mode = false;
          url_bar_mode = false;
          ui.focus_url_bar();
          break;
        case C::NavigateUrl:
          if (!ui_frame.arg.empty())
            navigate_to_url(ui_frame.arg);
          break;
        case C::NavBack:
          if (nav_hist_pos > 0) {
            --nav_hist_pos;
            navigate_to_url(nav_history[static_cast<size_t>(nav_hist_pos)],
                            /*record_history=*/false);
          } else {
            flash_status("No previous page");
          }
          break;
        case C::NavForward:
          if (nav_hist_pos + 1 < static_cast<int>(nav_history.size())) {
            ++nav_hist_pos;
            navigate_to_url(nav_history[static_cast<size_t>(nav_hist_pos)],
                            /*record_history=*/false);
          } else {
            flash_status("No next page");
          }
          break;
        case C::NewTab: {
          DocBuffer b;
          b.doc = xanatoo::parse_document(xanatoo::fallback_document(), {});
          int bi = static_cast<int>(buffers.size());
          buffers.push_back(std::move(b));
          tab_order.push_back(bi);
          panes[focus].buf = bi;
          panes[focus].sel = {};
          xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(bi)].doc,
                                      panes[focus].sel.focus);
          panes[focus].sel.anchor = panes[focus].sel.focus;
          panes[focus].scroll_y = 0.f;
          buffers[static_cast<size_t>(bi)].history.reset(
              buffers[static_cast<size_t>(bi)].doc, panes[focus].sel);
          needs_layout = true;
          update_title();
          flash_status("New tab");
          break;
        }
        case C::CloseTab: {
          int tab_i = -1;
          if (!ui_frame.arg.empty()) {
            try { tab_i = std::stoi(ui_frame.arg); } catch (...) { tab_i = -1; }
          }
          if (tab_i < 0 || tab_i >= static_cast<int>(tab_order.size())) {
            // Close focused pane's buffer tab
            for (int ti = 0; ti < static_cast<int>(tab_order.size()); ++ti) {
              if (tab_order[static_cast<size_t>(ti)] == panes[focus].buf) {
                tab_i = ti;
                break;
              }
            }
          }
          if (tab_i < 0 || tab_order.empty()) break;
          int bi = tab_order[static_cast<size_t>(tab_i)];
          if (!allow_discard_dirty(bi)) break;
          // Keep floating windows that still show this buffer (MDI).
          tab_order.erase(tab_order.begin() + tab_i);
          // If no tabs left, create empty one
          if (tab_order.empty()) {
            DocBuffer b;
            b.doc = xanatoo::parse_document(xanatoo::fallback_document(), {});
            buffers.push_back(std::move(b));
            int nbi = static_cast<int>(buffers.size()) - 1;
            tab_order.push_back(nbi);
            panes[0].buf = panes[1].buf = nbi;
          } else {
            int next = tab_order[static_cast<size_t>(
                std::min(tab_i, static_cast<int>(tab_order.size()) - 1))];
            if (panes[0].buf == bi) panes[0].buf = next;
            if (panes[1].buf == bi) panes[1].buf = next;
          }
          needs_layout = true;
          update_title();
          flash_status("Tab closed");
          break;
        }
        case C::SelectTab: {
          int tab_i = 0;
          try { tab_i = std::stoi(ui_frame.arg); } catch (...) { break; }
          if (tab_i < 0 || tab_i >= static_cast<int>(tab_order.size())) break;
          const int bi = tab_order[static_cast<size_t>(tab_i)];
          if (panes[focus].buf == bi) break;  // already showing
          panes[focus].buf = bi;
          // Fresh layout + caret for the newly focused buffer.
          xanatoo::ensure_valid_caret(doc_of(panes[focus]), panes[focus].sel.focus);
          panes[focus].sel.anchor = panes[focus].sel.focus;
          panes[focus].layout = {};
          needs_layout = true;
          update_title();
          break;
        }
        case C::NextTab:
        case C::PrevTab:
          if (!tab_order.empty()) {
            int cur = 0;
            for (int ti = 0; ti < static_cast<int>(tab_order.size()); ++ti)
              if (tab_order[static_cast<size_t>(ti)] == panes[focus].buf) {
                cur = ti;
                break;
              }
            const int ntabs = static_cast<int>(tab_order.size());
            if (ui_frame.cmd == C::NextTab)
              cur = (cur + 1) % ntabs;
            else
              cur = (cur - 1 + ntabs) % ntabs;
            panes[focus].buf = tab_order[static_cast<size_t>(cur)];
            xanatoo::ensure_valid_caret(doc_of(panes[focus]),
                                       panes[focus].sel.focus);
            panes[focus].sel.anchor = panes[focus].sel.focus;
            panes[focus].layout = {};
            needs_layout = true;
            update_title();
          }
          break;
        case C::PopOut:
          pop_out_focused();
          break;
        case C::PopIn:
          if (!soft_wins.empty())
            pop_in_soft(soft_wins.size() - 1);
          else
            flash_status("No floating window to dock");
          break;
        case C::ToggleHistory:
          history_panel_open = !history_panel_open;
          break;
        case C::ToggleBookmarks:
          bookmarks_panel_open = !bookmarks_panel_open;
          break;
        case C::AddBookmark: {
          std::string url = path_of(p);
          if (url.empty()) url = gemini_current_url;
          add_bookmark(url);
          break;
        }
        case C::RemoveBookmark: {
          std::string url = ui_frame.arg;
          if (url.empty()) {
            url = path_of(p);
            if (url.empty()) url = gemini_current_url;
          }
          remove_bookmark(url);
          break;
        }
        case C::Save: {
          std::string& spath = path_of(p);
          if (spath.empty()) {
            save_as_mode = true;
            save_as_path = "untitled.md";
            minibuf_begin(save_as_path);
          } else {
            std::string text = xanatoo::serialize_document(d, spath);
            std::ofstream out(spath);
            if (out) {
              out << text;
              buffers[static_cast<size_t>(p.buf)].dirty = false;
              discard_armed = false;
              update_title();
              flash_status("Saved " + spath);
              std::cout << "Saved: " << spath << "\n";
            }
          }
          break;
        }
        case C::SaveAs:
          save_as_mode = true;
          search_mode = false;
          save_as_path = path_of(p).empty() ? "untitled.md" : path_of(p);
          minibuf_begin(save_as_path);
          break;
        case C::ExportSexpr:
        case C::ExportMarkdown: {
          save_as_mode = true;
          search_mode = false;
          const bool as_sexp = (ui_frame.cmd == C::ExportSexpr);
          const char* ext = as_sexp ? ".sexp" : ".md";
          std::string cur = path_of(p);
          if (cur.empty()) {
            save_as_path = as_sexp ? "untitled.sexp" : "untitled.md";
          } else {
            std::filesystem::path fp(cur);
            save_as_path = fp.stem().string() + ext;
            if (fp.has_parent_path() && !fp.parent_path().empty())
              save_as_path = (fp.parent_path() / (fp.stem().string() + ext)).string();
          }
          minibuf_begin(save_as_path);
          break;
        }
        case C::Reload:
        case C::ForceReload: {
          std::string& rpath = path_of(p);
          if (rpath.empty()) break;
          if (buffers[static_cast<size_t>(p.buf)].dirty && ui_frame.cmd == C::Reload) {
            flash_status("Unsaved changes — use Force Reload");
            break;
          }
          if (is_in_app_remote_url(rpath)) {
            navigate_to_url(rpath, /*record_history=*/false);
            break;
          }
          xanatoo::clear_external_doc_cache();
          buffers[static_cast<size_t>(p.buf)].doc = load_doc(rpath);
          buffers[static_cast<size_t>(p.buf)].dirty = false;
          buffers[static_cast<size_t>(p.buf)].history.reset(buffers[static_cast<size_t>(p.buf)].doc, p.sel);
          xanatoo::ensure_valid_caret(buffers[static_cast<size_t>(p.buf)].doc, p.sel.focus);
          p.sel.anchor = p.sel.focus;
          p.scroll_y = 0.f;
          p.layout = {};
          for (auto& sw : soft_wins)
            if (sw.buf == p.buf) {
              sw.needs_layout = true;
              sw.scroll = 0.f;
              sw.layout = {};
              sw.sel = p.sel;
            }
          update_title();
          needs_layout = true;
          flash_status("Reloaded " + rpath);
          break;
        }
        case C::Undo:
          if (buffers[static_cast<size_t>(p.buf)].history.undo(d, p.sel)) {
            xanatoo::ensure_valid_caret(d, p.sel.focus);
            p.sel.anchor = p.sel.focus;
            p.layout = {};
            drop_height_hints_for_buf(p.buf);
            needs_layout = true;
            for (auto& sw : soft_wins)
              if (sw.buf == p.buf) {
                sw.needs_layout = true;
                sw.layout = {};
                sw.sel = p.sel;
              }
          }
          break;
        case C::Redo:
          if (buffers[static_cast<size_t>(p.buf)].history.redo(d, p.sel)) {
            xanatoo::ensure_valid_caret(d, p.sel.focus);
            p.sel.anchor = p.sel.focus;
            p.layout = {};
            drop_height_hints_for_buf(p.buf);
            needs_layout = true;
            for (auto& sw : soft_wins)
              if (sw.buf == p.buf) {
                sw.needs_layout = true;
                sw.layout = {};
                sw.sel = p.sel;
              }
          }
          break;
        case C::Cut: {
          std::string text = xanatoo::selected_text(d, p.sel);
          if (!text.empty()) {
            SDL_SetClipboardText(text.c_str());
            edit();
            xanatoo::delete_selection(d, p.sel);
            needs_layout = true;
          }
          break;
        }
        case C::CopyPageUrl: {
          std::string url = path_of(p);
          if (url.empty()) url = gemini_current_url;
          if (url.empty()) {
            flash_status("No page URL");
            break;
          }
          SDL_SetClipboardText(url.c_str());
          flash_status("Copied " + url);
          break;
        }
        case C::Copy: {
          std::string text = xanatoo::selected_text(d, p.sel);
          if (!text.empty()) SDL_SetClipboardText(text.c_str());
          break;
        }
        case C::Paste:
          if (SDL_HasClipboardText()) {
            char* clip = SDL_GetClipboardText();
            if (clip && clip[0]) {
              edit();
              if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
              xanatoo::paste_plain_text(d, p.sel.focus, clip);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
            }
            if (clip) SDL_free(clip);
          }
          break;
        case C::SelectAll:
          xanatoo::select_all(d, p.sel);
          break;
        case C::SelectBlock:
          if (p.sel.focus.node != xanatoo::INVALID_NODE)
            xanatoo::select_node(d, p.sel, p.sel.focus.node);
          break;
        case C::DuplicateBlock:
          edit();
          if (auto nid = xanatoo::duplicate_block(d, p.sel.focus.node)) {
            p.sel.focus.node = *nid;
            p.sel.focus.span_index = 0;
            p.sel.focus.char_index = 0;
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
            reset_blink();
          }
          break;
        case C::MoveBlockUp:
          edit();
          if (xanatoo::move_block_up(d, p.sel.focus.node)) {
            needs_layout = true;
            reset_blink();
          }
          break;
        case C::MoveBlockDown:
          edit();
          if (xanatoo::move_block_down(d, p.sel.focus.node)) {
            needs_layout = true;
            reset_blink();
          }
          break;
        case C::Find:
          search_mode = true;
          replace_mode = false;
          break;
        case C::Replace:
          search_mode = true;
          replace_mode = true;
          break;
        case C::ReplaceAll:
          if (!search_query.empty()) {
            edit();
            size_t n = xanatoo::replace_all(d, search_query, replace_text);
            needs_layout = true;
            std::cout << "Replace all: " << n << " substitution(s)\n";
          } else {
            search_mode = true;
            replace_mode = true;
          }
          break;
        case C::Bold:
          if (!p.sel.empty()) {
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Strong);
            needs_layout = true;
          }
          break;
        case C::Italic:
          if (!p.sel.empty()) {
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Emphasis);
            needs_layout = true;
          }
          break;
        case C::Strike:
          if (!p.sel.empty()) {
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Strike);
            needs_layout = true;
          }
          break;
        case C::Code:
          if (!p.sel.empty()) {
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Code);
            needs_layout = true;
          }
          break;
        case C::Link:
          if (!p.sel.empty()) {
            std::string text = xanatoo::selected_text(d, p.sel);
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Link,
                                              "#" + text);
            link_edit_mode = true;
            link_edit_is_cite = false;
            link_edit_node = p.sel.focus.node;
            link_edit_span = p.sel.focus.span_index;
            link_edit_href = "#" + text;
            needs_layout = true;
          }
          break;
        case C::Cite:
          if (!p.sel.empty()) {
            std::string text = xanatoo::selected_text(d, p.sel);
            std::string key = text;
            for (char& ch : key) {
              if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
              else if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')))
                ch = '-';
            }
            while (!key.empty() && key.front() == '-') key.erase(key.begin());
            while (!key.empty() && key.back() == '-') key.pop_back();
            if (key.empty()) key = "key";
            if (key.size() > 32) key.resize(32);
            edit();
            xanatoo::apply_style_to_selection(d, p.sel, xanatoo::InlineKind::Cite,
                                              key);
            link_edit_mode = true;
            link_edit_is_cite = true;
            link_edit_node = p.sel.focus.node;
            link_edit_span = p.sel.focus.span_index;
            link_edit_href = key;
            needs_layout = true;
          }
          break;
        case C::InsertBiblioEntry: {
          edit();
          std::string key = "key";
          if (xanatoo::insert_biblio_entry(d, p.sel.focus, key)) {
            p.sel.anchor = p.sel.focus;
            // Open key-edit on the entry label via status: reuse link_edit for label
            // (label is not a span; set it when Enter confirms after typing).
            link_edit_mode = true;
            link_edit_is_cite = true;
            link_edit_node = p.sel.focus.node;
            link_edit_span = 0;
            link_edit_href = key;
            needs_layout = true;
          }
          break;
        }
        case C::InsertParagraph:
          edit();
          if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                             xanatoo::NodeKind::Paragraph)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertHr:
          edit();
          if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                             xanatoo::NodeKind::HorizontalRule)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertNote:
          edit();
          if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                             xanatoo::NodeKind::Note)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertCodeBlock:
          edit();
          if (xanatoo::insert_sibling_after(d, p.sel.focus,
                                             xanatoo::NodeKind::CodeBlock)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertTable:
          edit();
          if (xanatoo::insert_table(d, p.sel.focus)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertDefList:
          edit();
          if (xanatoo::insert_deflist(d, p.sel.focus)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertColumns:
          edit();
          if (xanatoo::insert_columns(d, p.sel.focus, 2)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertTransclude: {
          include_insert_mode = true;
          minibuf_begin();
          include_insert_src.clear();
          if (!p.sel.empty()) {
            std::string t = xanatoo::selected_text(d, p.sel);
            if (!t.empty() && t[0] != '#') t = "#" + t;
            include_insert_src = t;
          }
          break;
        }
        case C::InsertImage:
          image_insert_mode = true;
          minibuf_begin();
          image_insert_as_figure = false;
          image_insert_path.clear();
          break;
        case C::InsertFigure:
          image_insert_mode = true;
          minibuf_begin();
          image_insert_as_figure = true;
          image_insert_path.clear();
          break;
        case C::Heading1:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 1)) needs_layout = true;
          break;
        case C::Heading2:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 2)) needs_layout = true;
          break;
        case C::Heading3:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 3)) needs_layout = true;
          break;
        case C::Heading4:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 4)) needs_layout = true;
          break;
        case C::Heading5:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 5)) needs_layout = true;
          break;
        case C::Heading6:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 6)) needs_layout = true;
          break;
        case C::MakeHeader:
          edit();
          if (xanatoo::make_header(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Header");
          }
          break;
        case C::WrapSection:
          edit();
          if (xanatoo::wrap_in_section(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Wrapped in section");
          }
          break;
        case C::WrapArticle:
          edit();
          if (xanatoo::wrap_in_article(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Wrapped in article");
          }
          break;
        case C::UnwrapSectioning:
          edit();
          if (xanatoo::unwrap_sectioning(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Unwrapped section/article");
          }
          break;
        case C::PromoteSection:
          edit();
          if (xanatoo::promote_section(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Section promoted");
          } else {
            flash_status("Cannot promote section");
          }
          break;
        case C::DemoteSection:
          edit();
          if (xanatoo::demote_section(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Section demoted");
          } else {
            flash_status("Cannot demote section (need previous section sibling)");
          }
          break;
        case C::MoveSectionUp:
          edit();
          if (xanatoo::move_section_up(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Section moved up");
          }
          break;
        case C::MoveSectionDown:
          edit();
          if (xanatoo::move_section_down(d, p.sel.focus)) {
            needs_layout = true;
            flash_status("Section moved down");
          }
          break;
        case C::ParagraphLevel:
          edit();
          if (xanatoo::set_heading_level(d, p.sel.focus, 0)) needs_layout = true;
          break;
        case C::ToggleUnorderedList:
          edit();
          if (xanatoo::toggle_list(d, p.sel.focus, false)) needs_layout = true;
          break;
        case C::ToggleOrderedList:
          edit();
          if (xanatoo::toggle_list(d, p.sel.focus, true)) needs_layout = true;
          break;
        case C::ToggleTask:
          edit();
          if (xanatoo::toggle_task_item(d, p.sel.focus)) needs_layout = true;
          break;
        case C::ToggleBlockQuote: {
          edit();
          auto& node = d.get(p.sel.focus.node);
          if (node.kind == xanatoo::NodeKind::BlockQuote) {
            node.kind = xanatoo::NodeKind::Paragraph;
          } else if (node.kind == xanatoo::NodeKind::Paragraph ||
                     node.kind == xanatoo::NodeKind::Heading ||
                     node.kind == xanatoo::NodeKind::ListItem ||
                     node.kind == xanatoo::NodeKind::Note) {
            node.kind = xanatoo::NodeKind::BlockQuote;
            node.level = 0;
          }
          needs_layout = true;
          break;
        }
        case C::IndentList:
          edit();
          if (xanatoo::indent_list_item(d, p.sel.focus)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::OutdentList:
          edit();
          if (xanatoo::outdent_list_item(d, p.sel.focus)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::WrapStretch: {
          edit();
          std::string summary = xanatoo::selected_text(d, p.sel);
          if (xanatoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
            needs_layout = true;
          }
          break;
        }
        case C::EditStretchSummary: {
          xanatoo::NodeId sid =
              xanatoo::enclosing_stretch(d, p.sel.focus.node);
          if (sid != xanatoo::INVALID_NODE) {
            summary_edit_mode = true;
            summary_edit_node = sid;
            summary_edit_text = d.get(sid).summary;
            search_mode = false;
            save_as_mode = false;
            link_edit_mode = false;
          }
          break;
        }
        case C::UnwrapStretch: {
          edit();
          if (auto first = xanatoo::unwrap_stretch(d, p.sel.focus.node)) {
            p.sel.focus.node = *first;
            p.sel.focus.span_index = 0;
            p.sel.focus.char_index = 0;
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
            reset_blink();
          }
          break;
        }
        case C::ToggleSplit:
          split = !split;
          needs_layout = true;
          break;
        case C::ToggleOutline:
          outline_mode = !outline_mode;
          if (outline_mode) {
            refresh_outline();
            outline_sel = 0;
            ol_scroll = 0.f;
          }
          needs_layout = true;
          layout_panes();
          break;
        case C::ToggleBacklinks:
          backlinks_mode = !backlinks_mode;
          if (backlinks_mode) {
            refresh_backlinks();
            backlinks_sel = 0;
            bl_scroll = 0.f;
            if (backlinks_label.empty())
              flash_status("Backlinks: place caret on a labelled node");
            else
              flash_status("Backlinks to #" + backlinks_label + " (" +
                           std::to_string(backlinks.size()) + ")");
          }
          break;
        case C::OpenTranscludeOther:
          open_transclude_in_other_pane();
          break;
        case C::ToggleDiff:
          if (shared_doc() || buffers.size() < 2) {
            flash_status(
                "Diff needs two files — Examples → Diff demo, or "
                "xanatoo a.md b.md");
          } else {
            diff_mode = !diff_mode;
            refresh_diff();
            flash_status(diff_mode ? "Diff marks on" : "Diff marks off");
          }
          break;
        case C::ToggleStructure:
          structure_mode = !structure_mode;
          flash_status(structure_mode ? "Structure highlight on"
                                      : "Structure highlight off");
          break;
        case C::ToggleBrowse:
          browse_mode = !browse_mode;
          if (browse_mode) {
            // Clear any status prompts so they do not trap keys
            search_mode = false;
            save_as_mode = false;
            open_file_mode = false;
            link_edit_mode = false;
            flash_status(
                "Browse mode — read-only · F2 exits · gamepad: D-pad move, "
                "A open, B collapse, LB/RB headers, LT/RT page");
          } else {
            flash_status("Edit mode");
          }
          break;
        case C::ToggleHelp:
          help_mode = !help_mode;
          if (help_mode) {
            ensure_help_doc();
            help_needs_layout = true;
          }
          break;
        case C::ToggleVirtualKeyboard:
          virtual_keyboard = !virtual_keyboard;
          flash_status(virtual_keyboard ? "Virtual keyboard on (F3)"
                                        : "Virtual keyboard off");
          break;
        case C::VirtualKey: {
          if (browse_mode) {
            flash_status("Browse mode — F2 to edit");
            break;
          }
          if (ui_frame.arg.empty()) break;
          Pane& vp = F();
          xanatoo::Document& vd = doc_of(vp);
          if (ui_frame.arg == "\b") {
            edit();
            if (!vp.sel.empty())
              xanatoo::delete_selection(vd, vp.sel);
            else
              xanatoo::delete_backward(vd, vp.sel.focus);
            vp.sel.anchor = vp.sel.focus;
            needs_layout = true;
            scroll_caret_into_view(vp);
            reset_blink();
          } else if (ui_frame.arg == "\n") {
            edit();
            if (!vp.sel.empty())
              xanatoo::delete_selection(vd, vp.sel);
            xanatoo::split_block(vd, vp.sel.focus);
            vp.sel.anchor = vp.sel.focus;
            needs_layout = true;
            scroll_caret_into_view(vp);
            reset_blink();
          } else {
            edit();
            if (!vp.sel.empty())
              xanatoo::delete_selection(vd, vp.sel);
            xanatoo::insert_text(vd, vp.sel.focus, ui_frame.arg);
            vp.sel.anchor = vp.sel.focus;
            needs_layout = true;
            scroll_caret_into_view(vp);
            reset_blink();
          }
          break;
        }
        case C::OpenFormatDoc: {
          std::string path = find_repo_file("FORMAT.md");
          if (path.empty()) path = find_repo_file("FORMAT.md");
          open_browse(path, "FORMAT.md — format spec");
          break;
        }
        case C::OpenSemanticDoc: {
          std::string path = find_repo_file("semantic.sexp");
          if (path.empty()) path = find_repo_file("examples/semantic.sexp");
          // find_repo_file already tries examples/
          if (path.empty()) {
            for (const char* c : {"examples/semantic.sexp", "../examples/semantic.sexp"})
              if (fs::exists(c)) { path = fs::absolute(c).string(); break; }
          }
          open_browse(path, "semantic.sexp — demo");
          break;
        }
        case C::OpenInWindow:
          path_in_window_mode = true;
          minibuf_begin();
          path_in_window_buf.clear();
          break;
        case C::OpenExample: {
          std::string name = ui_frame.arg;
          if (name.empty()) break;
          // Prefer examples/<name> under data dir / cwd (PortMaster layout).
          std::string path = find_repo_file(name.c_str());
          if (path.empty())
            path = find_repo_file((std::string("examples/") + name).c_str());
          if (path.empty() || !fs::exists(path)) {
            flash_status("Example not found: " + name);
            std::cerr << "Example not found: " << name
                      << " (set XANATOO_DATA_DIR to the port folder)\n";
            break;
          }
          // Load into the focused pane (not a soft window). On R36S 640×480
          // floating windows are too tight for org tables / gemtext samples.
          if (!navigate_to_url(path, /*record_history=*/true)) {
            flash_status("Failed to open: " + name);
            break;
          }
          {
            std::string title = name;
            try {
              title = fs::path(path).filename().string();
            } catch (...) {
            }
            flash_status("Opened example: " + title);
          }
          break;
        }
        case C::LoadDiffDemo: {
          if (any_dirty() && !discard_armed) {
            discard_armed = true;
            flash_status(
                "Unsaved changes — save first, or repeat Diff demo to discard");
            break;
          }
          discard_armed = false;
          std::string p0 = find_repo_file("diff-v1.md");
          std::string p1 = find_repo_file("diff-v2.md");
          if (p0.empty()) p0 = find_repo_file("examples/diff-v1.md");
          if (p1.empty()) p1 = find_repo_file("examples/diff-v2.md");
          if (p0.empty() || p1.empty() || !fs::exists(p0) || !fs::exists(p1)) {
            flash_status("Diff demo files not found (diff-v1.md / diff-v2.md)");
            std::cerr << "Diff demo: examples/diff-v1.md and diff-v2.md required\n";
            break;
          }
          // Drop floats — their buf indices and layouts refer to old trees.
          soft_wins.clear();
          clear_interaction_hover();
          buffers.clear();
          {
            DocBuffer b0;
            b0.path = p0;
            b0.doc = load_doc(p0);
            buffers.push_back(std::move(b0));
            DocBuffer b1;
            b1.path = p1;
            b1.doc = load_doc(p1);
            buffers.push_back(std::move(b1));
          }
          tab_order = {0, 1};
          panes[0].buf = 0;
          panes[1].buf = 1;
          split = true;
          focus = 0;
          xanatoo::ensure_valid_caret(buffers[0].doc, panes[0].sel.focus);
          panes[0].sel.anchor = panes[0].sel.focus;
          panes[0].scroll_y = 0.f;
          xanatoo::ensure_valid_caret(buffers[1].doc, panes[1].sel.focus);
          panes[1].sel.anchor = panes[1].sel.focus;
          panes[1].scroll_y = 0.f;
          buffers[0].history.reset(buffers[0].doc, panes[0].sel);
          buffers[1].history.reset(buffers[1].doc, panes[1].sel);
          diff_mode = true;
          refresh_diff();
          needs_layout = true;
          update_title();
          flash_status("Diff demo — orange bars mark changed blocks");
          std::cout << "Diff demo: " << p0 << " | " << p1 << "\n";
          break;
        }
        case C::ZoomIn:
          body_px = std::min(48, body_px + 1);
          heading_px = std::min(64, heading_px + 2);
          fonts.set_sizes(body_px, heading_px);
          needs_layout = true;
          break;
        case C::ZoomOut:
          body_px = std::max(10, body_px - 1);
          heading_px = std::max(14, heading_px - 2);
          fonts.set_sizes(body_px, heading_px);
          needs_layout = true;
          break;
        case C::ViewLineSpacingTight:
          view_settings.line_spacing = xanatoo::ViewSettings::kLineTight;
          apply_view_settings();
          flash_status("Line spacing: tight");
          break;
        case C::ViewLineSpacingNormal:
          view_settings.line_spacing = xanatoo::ViewSettings::kLineNormal;
          apply_view_settings();
          flash_status("Line spacing: normal");
          break;
        case C::ViewLineSpacingRelaxed:
          view_settings.line_spacing = xanatoo::ViewSettings::kLineRelaxed;
          apply_view_settings();
          flash_status("Line spacing: relaxed");
          break;
        case C::ViewLineSpacingLoose:
          view_settings.line_spacing = xanatoo::ViewSettings::kLineLoose;
          apply_view_settings();
          flash_status("Line spacing: loose");
          break;
        case C::ViewMarginsNarrow:
          view_settings.margin_x = xanatoo::ViewSettings::kMarginNarrow;
          apply_view_settings();
          flash_status("Margins: narrow");
          break;
        case C::ViewMarginsNormal:
          view_settings.margin_x = xanatoo::ViewSettings::kMarginNormal;
          apply_view_settings();
          flash_status("Margins: normal");
          break;
        case C::ViewMarginsWide:
          view_settings.margin_x = xanatoo::ViewSettings::kMarginWide;
          apply_view_settings();
          flash_status("Margins: wide");
          break;
        case C::ViewMarginsExtra:
          view_settings.margin_x = xanatoo::ViewSettings::kMarginExtra;
          apply_view_settings();
          flash_status("Margins: extra wide");
          break;
        case C::ViewMeasureFull:
          view_settings.max_content_width = 0.f;
          apply_view_settings();
          flash_status("Measure: full width");
          break;
        case C::ViewMeasureComfortable:
          view_settings.max_content_width =
              xanatoo::ViewSettings::kMeasureComfortable;
          apply_view_settings();
          flash_status("Measure: comfortable (~720px)");
          break;
        case C::ViewMeasureWide:
          view_settings.max_content_width =
              xanatoo::ViewSettings::kMeasureWide;
          apply_view_settings();
          flash_status("Measure: wide (~960px)");
          break;
        case C::ToggleSectionFocus:
          toggle_section_focus_mode();
          break;
        case C::ToggleJustify:
          view_settings.justify = !view_settings.justify;
          apply_view_settings();
          if (view_settings.justify) {
            if (view_settings.max_content_width < 1.f)
              flash_status(
                  "Justify on — View→Measure Comfortable for book-like width");
            else
              flash_status("Justify on");
          } else {
            flash_status("Justify off");
          }
          break;
        default:
          break;
      }
      if (ui_frame.cmd != C::None)
        ui.release_input_capture();
    }

    // UI commands (Open, Diff demo, New tab, …) often replace documents after
    // the frame's first layout pass. Relayout before draw so glyphs/carets
    // never reference node ids from the previous document tree.
    if (needs_layout && win_w >= 64 && win_h >= 64) {
      layout_panes();
      {
        std::string dk = path_of(F());
        if (dk != last_fold_doc_key) {
          last_fold_doc_key = dk;
          reload_folds_for_focused();
        }
      }
      if (outline_mode) refresh_outline();
      if (backlinks_mode) refresh_backlinks();
    }

    // Gamepad sticks / triggers (poll each frame).
    if (gamepad) {
      auto axis = [&](SDL_GameControllerAxis a) -> float {
        Sint16 v = SDL_GameControllerGetAxis(gamepad, a);
        const float dead = 8000.f;
        if (v > -dead && v < dead) return 0.f;
        return static_cast<float>(v) / 32767.f;
      };
      // Left stick Y: scroll focused pane (unchanged).
      float ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
      if (ly != 0.f) {
        Pane& p = F();
        float view_h =
            static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
        float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
        p.scroll_y =
            std::max(0.f, std::min(max_s, p.scroll_y + ly * 18.f));
      }
      // Triggers as page up/down
      static bool lt_held = false, rt_held = false;
      float lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
      float rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
      if (lt > 0.5f) {
        if (!lt_held) {
          nav_page(true);
          lt_held = true;
        }
      } else {
        lt_held = false;
      }
      if (rt > 0.5f) {
        if (!rt_held) {
          nav_page(false);
          rt_held = true;
        }
      } else {
        rt_held = false;
      }
    } else {
      stick_cursor_on = false;
      stick_cursor_init = false;
    }

        renderer.clear(248, 246, 240);

    int pane_count = split ? 2 : 1;
    for (int i = 0; i < pane_count; ++i) {
      Pane& pane = panes[i];
      float view_h_pane = static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
      float max_scroll =
          std::max(0.f, pane.layout.content_height - view_h_pane + 40.f);
      if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

      const int clip_y = static_cast<int>(ui_top);
      SDL_Rect clip{pane.x, clip_y, std::max(1, pane.w - kSbW),
                   win_h - status_bar_h - clip_y - static_cast<int>(ui_bottom)};
      SDL_RenderSetClipRect(sdl, &clip);

      float ox = static_cast<float>(pane.x);
      // screen_y = content_y - scroll_y + ui_top = content_y - (scroll_y - ui_top)
      const float draw_scroll = pane.scroll_y - ui_top;

      if (!pane.sel.empty()) {
        auto rects = xanatoo::selection_rects(
            pane.layout, pane.sel, draw_scroll, lh);
        // Leap-search match uses amber so it is distinct from a user selection.
        if (search_mode && i == focus)
          renderer.draw_selection(rects, ox, 230, 190, 70, 150);
        else
          renderer.draw_selection(rects, ox);
      }

      renderer.draw(pane.layout, draw_scroll, ox);

      // Hover affordances for interactive elements in this pane
      {
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        // Link / cite run: soft wash + thicker underline
        if (hovered_link_pane == i &&
            hovered_link_node != xanatoo::INVALID_NODE &&
            !hovered_href.empty()) {
          float run_x0 = 0.f, run_x1 = 0.f, run_top = 0.f, run_base = 0.f;
          bool in_run = false;
          auto flush = [&]() {
            if (!in_run) return;
            int x = static_cast<int>(run_x0 + ox) - 2;
            int y = static_cast<int>(run_top - draw_scroll) - 2;
            int w = static_cast<int>(run_x1 - run_x0) + 4;
            int h = body_font.line_height() + 4;
            SDL_SetRenderDrawColor(sdl, 40, 110, 220, 45);
            SDL_Rect r{x, y, w, h};
            SDL_RenderFillRect(sdl, &r);
            int uy = static_cast<int>(run_base + 3.f - draw_scroll);
            SDL_SetRenderDrawColor(sdl, 20, 80, 190, 255);
            SDL_RenderDrawLine(sdl, x + 1, uy, x + w - 2, uy);
            SDL_RenderDrawLine(sdl, x + 1, uy + 1, x + w - 2, uy + 1);
            in_run = false;
          };
          for (const auto& pg : pane.layout.glyphs) {
            if (pg.node != hovered_link_node ||
                pg.span_index != hovered_link_span ||
                (pg.kind != xanatoo::InlineKind::Link &&
                 pg.kind != xanatoo::InlineKind::Cite)) {
              flush();
              continue;
            }
            xanatoo::Font& font = fonts.pick(pg.font);
            const xanatoo::Glyph& g = font.glyph(pg.codepoint);
            float adv = static_cast<float>(
                (g.advance >> 6) > 0 ? (g.advance >> 6) : (g.width + 1));
            float top = pg.y - static_cast<float>(font.ascender());
            if (!in_run) {
              run_x0 = pg.x;
              run_x1 = pg.x + adv;
              run_top = top;
              run_base = pg.y;
              in_run = true;
            } else if (std::abs(top - run_top) < 0.5f &&
                       pg.x <= run_x1 + 2.f) {
              run_x1 = pg.x + adv;
            } else {
              flush();
              run_x0 = pg.x;
              run_x1 = pg.x + adv;
              run_top = top;
              run_base = pg.y;
              in_run = true;
            }
          }
          flush();
        }
        // Stretch marker wash
        if (hovered_stretch_pane == i &&
            hovered_stretch != xanatoo::INVALID_NODE) {
          for (const auto& box : pane.layout.boxes) {
            if (box.node != hovered_stretch) continue;
            // Only the header line for expanded stretches is interactive;
            // wash the first line-height of the box when expanded.
            float h = box.height;
            auto& hdoc = buffers[static_cast<size_t>(pane.buf)].doc;
            if (hovered_stretch == xanatoo::INVALID_NODE ||
                hovered_stretch >= hdoc.next_id ||
                static_cast<size_t>(hovered_stretch - 1) >= hdoc.nodes.size())
              break;
            auto& sn = hdoc.get(hovered_stretch);
            if (sn.expanded && h > static_cast<float>(body_font.line_height()) + 8.f)
              h = static_cast<float>(body_font.line_height()) + 6.f;
            SDL_SetRenderDrawColor(sdl, 40, 130, 90, 50);
            SDL_Rect r{pane.x + static_cast<int>(box.x),
                       static_cast<int>(box.y - pane.scroll_y + ui_top),
                       static_cast<int>(box.width), static_cast<int>(h)};
            SDL_RenderFillRect(sdl, &r);
            SDL_SetRenderDrawColor(sdl, 30, 120, 80, 200);
            SDL_RenderDrawRect(sdl, &r);
            break;
          }
        }
        // Image outline + emphasized resize handle
        if (hovered_image_pane == i &&
            hovered_image != xanatoo::INVALID_NODE) {
          for (const auto& pi : pane.layout.images) {
            if (pi.node != hovered_image) continue;
            int ix = pane.x + static_cast<int>(pi.x);
            int iy = static_cast<int>(pi.y - pane.scroll_y + ui_top);
            int iw = static_cast<int>(pi.width);
            int ih = static_cast<int>(pi.height);
            SDL_SetRenderDrawColor(sdl, 25, 90, 170, 180);
            SDL_Rect outline{ix, iy, iw, ih};
            SDL_RenderDrawRect(sdl, &outline);
            const int hs = hovered_image_handle ? 14 : 10;
            SDL_Rect handle{ix + iw - hs, iy + ih - hs, hs, hs};
            SDL_SetRenderDrawColor(sdl,
                                   hovered_image_handle ? 15 : 25,
                                   hovered_image_handle ? 70 : 90,
                                   hovered_image_handle ? 200 : 170, 255);
            SDL_RenderFillRect(sdl, &handle);
            if (hovered_image_handle) {
              SDL_SetRenderDrawColor(sdl, 255, 255, 255, 220);
              SDL_RenderDrawRect(sdl, &handle);
            }
            break;
          }
        }
        // Column gutter highlight
        if (hovered_col_gap_pane == i && hovered_col_gap) {
          float mx = mouse_x - static_cast<float>(pane.x);
          float my = content_y(mouse_y);
          for (const auto& gap : pane.layout.column_gaps) {
            float gx = gap.x;
            float gy0 = gap.y - pane.scroll_y;
            float gy1 = gy0 + gap.height;
            if (mx >= gx - 5.f && mx <= gx + 5.f && my >= gy0 && my <= gy1) {
              int gx_i = pane.x + static_cast<int>(gx);
              int gy_i = static_cast<int>(gy0 + ui_top);
              int gh = static_cast<int>(gap.height);
              SDL_SetRenderDrawColor(sdl, 25, 90, 170, 90);
              SDL_Rect wide{gx_i - 4, gy_i, 9, gh};
              SDL_RenderFillRect(sdl, &wide);
              SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
              SDL_Rect bar{gx_i - 1, gy_i, 3, gh};
              SDL_RenderFillRect(sdl, &bar);
              break;
            }
          }
        }
        // Task checkbox zone
        if (hovered_task_pane == i &&
            hovered_task != xanatoo::INVALID_NODE) {
          for (const auto& box : pane.layout.boxes) {
            if (box.node != hovered_task) continue;
            int x = pane.x + static_cast<int>(box.x);
            int y = static_cast<int>(box.y - pane.scroll_y + ui_top);
            int h = static_cast<int>(box.height);
            SDL_SetRenderDrawColor(sdl, 40, 140, 80, 55);
            SDL_Rect r{x, y, 26, h};
            SDL_RenderFillRect(sdl, &r);
            SDL_SetRenderDrawColor(sdl, 30, 120, 70, 220);
            SDL_RenderDrawRect(sdl, &r);
            break;
          }
        }
        // Block drag grip zone (drawn in the left gutter, not over text)
        if (hovered_grip_pane == i &&
            hovered_grip != xanatoo::INVALID_NODE) {
          for (const auto& box : pane.layout.boxes) {
            if (box.node != hovered_grip) continue;
            int gw = static_cast<int>(kBlockGripW);
            int x = pane.x + static_cast<int>(box.x) - gw;
            int y = static_cast<int>(box.y - pane.scroll_y + ui_top);
            int h = static_cast<int>(box.height);
            SDL_SetRenderDrawColor(sdl, 80, 120, 180, 50);
            SDL_Rect r{x, y, gw, h};
            SDL_RenderFillRect(sdl, &r);
            // Stronger grip dots centered in the gutter
            int gx = x + gw / 2 - 2;
            int gy = y + h / 2;
            SDL_SetRenderDrawColor(sdl, 40, 90, 160, 255);
            for (int d = -6; d <= 6; d += 6) {
              SDL_Rect dot{gx, gy + d, 4, 4};
              SDL_RenderFillRect(sdl, &dot);
            }
            break;
          }
        }
        // Transclude region
        if (hovered_transclude_pane == i &&
            hovered_transclude != xanatoo::INVALID_NODE) {
          for (const auto& box : pane.layout.boxes) {
            if (box.node != hovered_transclude) continue;
            int x = pane.x + static_cast<int>(box.x);
            int y = static_cast<int>(box.y - pane.scroll_y + ui_top);
            int w = static_cast<int>(box.width);
            int h = static_cast<int>(box.height);
            SDL_SetRenderDrawColor(sdl, 180, 120, 40, 40);
            SDL_Rect r{x, y, w, h};
            SDL_RenderFillRect(sdl, &r);
            SDL_SetRenderDrawColor(sdl, 180, 120, 40, 200);
            SDL_RenderDrawRect(sdl, &r);
            break;
          }
        }
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      }

      // Logical structure overlay: colour bars + wash by NodeKind + right labels
      if (structure_mode) {
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        const auto& ddoc = buffers[static_cast<size_t>(pane.buf)].doc;
        auto kind_label = [&ddoc](const xanatoo::Node& n) -> const char* {
          using K = xanatoo::NodeKind;
          switch (n.kind) {
            case K::Heading: {
              static thread_local char buf[4];
              int lv = n.level;
              if (lv < 1) lv = 1;
              if (lv > 6) lv = 6;
              buf[0] = 'h';
              buf[1] = static_cast<char>('0' + lv);
              buf[2] = 0;
              return buf;
            }
            case K::Header: {
              // Show interop depth (org stars / ATX) or section nesting.
              static thread_local char buf[12];
              int lv = n.level;
              if (lv < 1) {
                xanatoo::NodeId pid = n.parent;
                while (pid != xanatoo::INVALID_NODE &&
                       pid < ddoc.next_id &&
                       static_cast<size_t>(pid - 1) < ddoc.nodes.size()) {
                  const auto& pn = ddoc.get(pid);
                  if (pn.kind == K::Section || pn.kind == K::Article)
                    ++lv;
                  pid = pn.parent;
                }
              }
              if (lv < 1) lv = 1;
              if (lv > 9) lv = 9;
              buf[0] = 'h';
              buf[1] = 'e';
              buf[2] = 'a';
              buf[3] = 'd';
              buf[4] = 'e';
              buf[5] = 'r';
              buf[6] = static_cast<char>('0' + lv);
              buf[7] = 0;
              return buf;
            }
            case K::Article: return "article";
            case K::Section: return "section";
            case K::Paragraph: return "para";
            case K::List: return n.level == 1 ? "ol" : "ul";
            case K::ListItem:
              if (n.label == "task") return "task";
              if (n.label == "task-done") return "done";
              return "li";
            case K::BlockQuote: return "quote";
            case K::Note: return "note";
            case K::CodeBlock: return "code";
            case K::Table: return "table";
            case K::TableRow: return "row";
            case K::TableCell: return n.level > 0 ? "th" : "td";
            case K::DefList: return "deflist";
            case K::Term: return "term";
            case K::Definition: return "def";
            case K::Stretch: return "stretch";
            case K::Columns: return "columns";
            case K::Column: return "column";
            case K::Image: return "image";
            case K::Figure: return "figure";
            case K::Caption: return "caption";
            case K::Biblio: return "biblio";
            case K::BiblioEntry: return "ref";
            case K::HorizontalRule: return "hr";
            case K::Transclude: return "include";
            case K::Thread: return "thread";
            case K::Message: return "message";
            default: return nullptr;
          }
        };
        // Prefer mono for compact gutter tags; fall back to body.
        xanatoo::Font& tag_font = fonts.mono.loaded() ? fonts.mono : fonts.regular;
        const int tag_lh = tag_font.line_height();
        for (const auto& box : pane.layout.boxes) {
          if (box.node == xanatoo::INVALID_NODE) continue;
          const auto& node = ddoc.get(box.node);
          const auto kind = node.kind;
          // Skip pure container shells that span the whole doc
          if (kind == xanatoo::NodeKind::Document) continue;
          uint8_t r = 120, g = 120, b = 130, a_bar = 200, a_wash = 28;
          switch (kind) {
            case xanatoo::NodeKind::Heading:
            case xanatoo::NodeKind::Header:
              r = 40; g = 90; b = 180; a_wash = 36; break;
            case xanatoo::NodeKind::Article:
              r = 60; g = 100; b = 160; a_wash = 30; break;
            case xanatoo::NodeKind::Section:
              r = 70; g = 110; b = 170; a_wash = 26; break;
            case xanatoo::NodeKind::Paragraph:
              r = 100; g = 110; b = 120; a_wash = 18; break;
            case xanatoo::NodeKind::List:
            case xanatoo::NodeKind::ListItem:
              r = 50; g = 140; b = 90; break;
            case xanatoo::NodeKind::BlockQuote:
              r = 140; g = 155; b = 180; break;
            case xanatoo::NodeKind::Note:
              r = 200; g = 140; b = 40; break;
            case xanatoo::NodeKind::CodeBlock:
              r = 160; g = 100; b = 50; a_wash = 40; break;
            case xanatoo::NodeKind::Table:
            case xanatoo::NodeKind::TableRow:
            case xanatoo::NodeKind::TableCell:
              r = 80; g = 120; b = 160; break;
            case xanatoo::NodeKind::DefList:
            case xanatoo::NodeKind::Term:
            case xanatoo::NodeKind::Definition:
              r = 120; g = 80; b = 160; break;
            case xanatoo::NodeKind::Stretch:
              r = 180; g = 80; b = 120; break;
            case xanatoo::NodeKind::Columns:
            case xanatoo::NodeKind::Column:
              r = 70; g = 130; b = 150; break;
            case xanatoo::NodeKind::Image:
            case xanatoo::NodeKind::Figure:
            case xanatoo::NodeKind::Caption:
              r = 150; g = 70; b = 100; break;
            case xanatoo::NodeKind::Biblio:
            case xanatoo::NodeKind::BiblioEntry:
              r = 90; g = 50; b = 140; break;
            case xanatoo::NodeKind::HorizontalRule:
              r = 160; g = 160; b = 160; break;
            case xanatoo::NodeKind::Transclude:
              r = 180; g = 120; b = 40; break;
            case xanatoo::NodeKind::Thread:
            case xanatoo::NodeKind::Message:
              r = 40; g = 130; b = 140; a_wash = 32; break;
            default:
              break;
          }
          int by = static_cast<int>(box.y - pane.scroll_y + ui_top);
          int bh = static_cast<int>(box.height);
          if (by + bh < clip_y || by > win_h - status_bar_h) continue;
          // Leave room on the right for the kind tag.
          const int tag_gutter = 56;
          int wash_w = pane.w - 14 - tag_gutter;
          if (wash_w < 20) wash_w = pane.w - 14;
          SDL_SetRenderDrawColor(sdl, r, g, b, a_bar);
          SDL_Rect mark{pane.x + 1, by, 5, bh};
          SDL_RenderFillRect(sdl, &mark);
          SDL_SetRenderDrawColor(sdl, r, g, b, a_wash);
          SDL_Rect wash{pane.x + 6, by, wash_w, bh};
          SDL_RenderFillRect(sdl, &wash);

          // Small kind label on the right edge of the region.
          // Skip pure table-row shells and plain paragraphs — the wash is
          // enough for para; tags stay for structural kinds.
          const char* tag = kind_label(node);
          if (tag && kind != xanatoo::NodeKind::TableRow &&
              kind != xanatoo::NodeKind::Paragraph && bh >= 10 &&
              tag_font.loaded()) {
            int tw = tag_font.measure(tag);
            int tx = pane.x + pane.w - tw - 8;
            if (tx < pane.x + 10) tx = pane.x + 10;
            // Baseline near the top of the box so stacked regions stay readable.
            float baseline =
                static_cast<float>(by) +
                static_cast<float>(std::min(bh, tag_lh + 2)) - 3.f;
            if (baseline < static_cast<float>(clip_y) + 8.f)
              baseline = static_cast<float>(clip_y) + 8.f;
            // Soft chip behind the tag for contrast on wash.
            SDL_SetRenderDrawColor(sdl, 248, 246, 240, 200);
            SDL_Rect chip{tx - 3, by + 1, tw + 6,
                          std::min(bh - 2, tag_lh + 2)};
            if (chip.h > 4) SDL_RenderFillRect(sdl, &chip);
            renderer.draw_text(tag_font, static_cast<float>(tx), baseline, tag,
                               r, g, b);
          }
        }
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      }

      if (i == focus) {
        auto cpos = xanatoo::caret_screen_pos(
            pane.layout, pane.sel.focus, draw_scroll, lh);
        if (cpos.valid) pane.preferred_x = cpos.x;
        // Browse mode: steady caret (reading focus). Edit mode: blink.
        if (browse_mode ||
            ((SDL_GetTicks() - caret_blink_start) % 1000) < 600) {
          renderer.draw_caret(cpos, ox);
        }
      }

      // Focus indicator: thin bar at top of focused pane content
      if (i == focus) {
        SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
        SDL_Rect bar{pane.x, clip_y, pane.w, 3};
        SDL_RenderFillRect(sdl, &bar);
      }

      // Version-diff marks (orange bars on changed text blocks)
      if (diff_mode && !shared_doc()) {
        int di = pane.buf;
        const auto& marks = diff_marks[di];
        if (!marks.empty()) {
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          for (const auto& box : pane.layout.boxes) {
            bool hit = false;
            for (xanatoo::NodeId id : marks) {
              if (id == box.node) {
                hit = true;
                break;
              }
            }
            if (!hit) continue;
            // Content → screen: same convention as structure overlay (+ ui_top).
            int by = static_cast<int>(box.y - pane.scroll_y + ui_top);
            int bh = static_cast<int>(box.height);
            if (by + bh < clip_y || by > win_h - status_bar_h) continue;
            SDL_SetRenderDrawColor(sdl, 200, 120, 40, 200);
            SDL_Rect mark{pane.x + 2, by, 4, bh};
            SDL_RenderFillRect(sdl, &mark);
            SDL_SetRenderDrawColor(sdl, 200, 120, 40, 40);
            SDL_Rect wash{pane.x, by, pane.w - 6, bh};
            SDL_RenderFillRect(sdl, &wash);
          }
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        // Intra-block word-level highlights for modified (similar) blocks
        const auto& words = diff_words[di];
        if (!words.empty()) {
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          const auto& ddoc = buffers[static_cast<size_t>(di)].doc;
          for (const auto& g : pane.layout.glyphs) {
            if (g.node == xanatoo::INVALID_NODE) continue;
            // Map glyph to plain-text character index within its node
            size_t gch = 0;
            {
              const auto& gn = ddoc.get(g.node);
              for (size_t si = 0; si < g.span_index && si < gn.spans.size();
                   ++si) {
                for (size_t ci = 0; ci < gn.spans[si].text.size();) {
                  unsigned char c =
                      static_cast<unsigned char>(gn.spans[si].text[ci]);
                  if (c < 0x80)
                    ++ci;
                  else if ((c & 0xe0) == 0xc0)
                    ci += 2;
                  else if ((c & 0xf0) == 0xe0)
                    ci += 3;
                  else if ((c & 0xf8) == 0xf0)
                    ci += 4;
                  else
                    ++ci;
                  ++gch;
                }
              }
              gch += g.char_index;
            }
            bool hit = false;
            for (const auto& wm : words) {
              if (wm.node == g.node && gch >= wm.char_begin &&
                  gch < wm.char_end) {
                hit = true;
                break;
              }
            }
            if (!hit) continue;
            int gx = pane.x + static_cast<int>(g.x);
            int gy = static_cast<int>(g.y - pane.scroll_y + ui_top - lh * 0.85f);
            // Width: advance to next glyph on the same baseline/node when possible.
            int gw = 8;
            {
              const float baseline = g.y;
              float next_x = g.x + 8.f;
              for (const auto& ng : pane.layout.glyphs) {
                if (ng.node != g.node) continue;
                if (std::abs(ng.y - baseline) > 1.f) continue;
                if (ng.x > g.x + 0.5f && ng.x < next_x) next_x = ng.x;
              }
              int adv = static_cast<int>(next_x - g.x);
              if (adv > 2 && adv < 48) gw = adv;
            }
            if (gy + static_cast<int>(lh) < clip_y || gy > win_h - status_bar_h)
              continue;
            SDL_SetRenderDrawColor(sdl, 220, 160, 40, 110);
            SDL_Rect r{gx, gy, gw, static_cast<int>(lh)};
            SDL_RenderFillRect(sdl, &r);
          }
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
      }

      // Column gutters (subtle vertical handles)
      for (const auto& gap : pane.layout.column_gaps) {
        int gx = pane.x + static_cast<int>(gap.x);
        int gy = static_cast<int>(gap.y - pane.scroll_y + ui_top);
        int gh = static_cast<int>(gap.height);
        bool active = dragging_col_gap && gap.left == col_gap_left;
        SDL_SetRenderDrawColor(sdl, active ? 25 : 190, active ? 90 : 188,
                               active ? 170 : 180, 255);
        SDL_Rect gbar{gx - 1, gy, 3, gh};
        SDL_RenderFillRect(sdl, &gbar);
      }

      // Block drag-drop: insertion line + source highlight
      if (dragging_block && i == focus) {
        // Dim the dragged block
        for (const auto& box : pane.layout.boxes) {
          if (box.node != drag_block_id) continue;
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(sdl, 80, 140, 220, 60);
          SDL_Rect r{pane.x + static_cast<int>(box.x),
                     static_cast<int>(box.y - pane.scroll_y + ui_top),
                     static_cast<int>(box.width),
                     static_cast<int>(box.height)};
          SDL_RenderFillRect(sdl, &r);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        if (drop_line_y >= 0.f) {
          int ly = static_cast<int>(drop_line_y - pane.scroll_y + ui_top);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
          SDL_Rect line{pane.x + 24, ly - 1, pane.w - 40, 3};
          SDL_RenderFillRect(sdl, &line);
        }
      } else if (i == focus && !dragging_block) {
        // Subtle grip dots in the left gutter (not over the text)
        // mouse_y is window space; boxes are content space.
        float adj_y = content_y(mouse_y) + pane.scroll_y;
        float lx = mouse_x - static_cast<float>(pane.x);
        for (const auto& box : pane.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x - kBlockGripW || lx >= box.x) continue;
          const auto& bn = doc_of(pane).get(box.node);
          if (bn.kind == xanatoo::NodeKind::Document ||
              bn.kind == xanatoo::NodeKind::Columns ||
              bn.kind == xanatoo::NodeKind::Column ||
              bn.kind == xanatoo::NodeKind::List)
            continue;
          int gx = pane.x + static_cast<int>(box.x) -
                   static_cast<int>(kBlockGripW) / 2 - 1;
          int gy = static_cast<int>(box.y - pane.scroll_y + ui_top +
                                   box.height * 0.5f);
          SDL_SetRenderDrawColor(sdl, 160, 158, 150, 255);
          for (int d = -6; d <= 6; d += 6) {
            SDL_Rect dot{gx, gy + d, 3, 3};
            SDL_RenderFillRect(sdl, &dot);
          }
          break;
        }
      }

      // Classic scrollbar: track, ↑/↓, chapter/search markers, thumb.
      {
        float view_h =
            static_cast<float>(win_h - status_bar_h) - ui_top - ui_bottom;
        if (view_h < 1.f) view_h = 1.f;
        float content_h = std::max(view_h, pane.layout.content_height);
        const float arrow = static_cast<float>(kSbArrow);
        float track_h = std::max(1.f, view_h - 2.f * arrow);
        // Thumb sized against full view; position on track between arrows.
        float thumb_h = std::max(
            20.f, track_h * (view_h / content_h));
        if (thumb_h > track_h) thumb_h = track_h;
        float track_travel = std::max(0.f, track_h - thumb_h);
        float ratio =
            (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
        float thumb_y = ui_top + arrow + ratio * track_travel;
        int sx = pane.x + pane.w - kSbW;
        int top = static_cast<int>(ui_top);
        int vh = static_cast<int>(view_h);

        // Track (bevelled trough)
        SDL_SetRenderDrawColor(sdl, 210, 208, 200, 255);
        SDL_Rect track_r{sx, top, kSbW, vh};
        SDL_RenderFillRect(sdl, &track_r);
        SDL_SetRenderDrawColor(sdl, 160, 158, 150, 255);
        SDL_RenderDrawRect(sdl, &track_r);

        // Metadata marks on the track (doc Y → track fraction).
        auto mark_at = [&](float doc_y, int r, int g, int b) {
          if (content_h < 1.f) return;
          float frac = std::max(0.f, std::min(1.f, doc_y / content_h));
          int my = top + static_cast<int>(arrow + frac * track_h);
          if (my < top + static_cast<int>(arrow) ||
              my > top + vh - static_cast<int>(arrow))
            return;
          SDL_SetRenderDrawColor(sdl, r, g, b, 255);
          SDL_Rect tick{sx + 1, my - 1, kSbW - 2, 2};
          SDL_RenderFillRect(sdl, &tick);
        };
        // Chapter / section breaks. Virtualization often stubs a whole Section
        // (no Header child boxes) — prefer Section id, and synthesise Y from
        // height_cache when a node is outside the materialised band.
        if (outline.empty()) {
          xanatoo::collect_outline(doc_of(pane), outline, &folded_sections);
        }
        if (!outline.empty()) {
          std::unordered_map<xanatoo::NodeId, float> mark_y;
          mark_y.reserve(pane.layout.boxes.size() + 16);
          for (const auto& box : pane.layout.boxes) {
            if (box.node != xanatoo::INVALID_NODE)
              mark_y[box.node] = box.y;
          }
          // Fill missing Section/Article positions by walking with height hints.
          const auto& dmark = doc_of(pane);
          std::function<float(xanatoo::NodeId, float)> walk_y =
              [&](xanatoo::NodeId id, float y) -> float {
            if (id == xanatoo::INVALID_NODE || id >= dmark.next_id) return y;
            const auto& n = dmark.get(id);
            if (!mark_y.count(id)) mark_y[id] = y;
            if (n.kind == xanatoo::NodeKind::Document) {
              for (xanatoo::NodeId c : n.children) y = walk_y(c, y);
              return y;
            }
            if (n.kind == xanatoo::NodeKind::Section ||
                n.kind == xanatoo::NodeKind::Article) {
              // If layout already has a stub/full box height, advance by it
              // when children were not materialised (no child entries in mark_y
              // beyond what we just set for id).
              auto hit = pane.height_cache.find(id);
              bool child_known = false;
              for (xanatoo::NodeId c : n.children) {
                if (mark_y.count(c)) {
                  child_known = true;
                  break;
                }
              }
              if (hit != pane.height_cache.end() && !child_known) {
                // Parent was virtualized as one stub — still place nested
                // outline targets using child height hints so marks remain.
                float cy = y;
                for (xanatoo::NodeId c : n.children) {
                  if (!mark_y.count(c)) mark_y[c] = cy;
                  auto ch = pane.height_cache.find(c);
                  cy += ch != pane.height_cache.end()
                            ? ch->second
                            : static_cast<float>(body_font.line_height());
                }
                return y + hit->second;
              }
              float cy = y;
              for (xanatoo::NodeId c : n.children) cy = walk_y(c, cy);
              if (hit != pane.height_cache.end() && cy <= y + 0.5f)
                return y + hit->second;
              return cy > y ? cy : y + (hit != pane.height_cache.end()
                                            ? hit->second
                                            : 24.f);
            }
            auto hit = pane.height_cache.find(id);
            if (hit != pane.height_cache.end()) return y + hit->second;
            return y + static_cast<float>(body_font.line_height());
          };
          if (dmark.root != xanatoo::INVALID_NODE) {
            // Prefer content_height-consistent origin: layout boxes already
            // used absolute y; walk only fills gaps (does not overwrite).
            float y0 = view_settings.margin_y;
            // If root box exists, start from it.
            auto root_it = mark_y.find(dmark.root);
            if (root_it != mark_y.end()) y0 = root_it->second;
            walk_y(dmark.root, y0);
          }
          for (const auto& e : outline) {
            float y = -1.f;
            // Prefer Section (stable under virtualization stubs), then Header.
            for (xanatoo::NodeId id :
                 {e.section, e.node}) {
              if (id == xanatoo::INVALID_NODE) continue;
              auto it = mark_y.find(id);
              if (it != mark_y.end()) {
                y = it->second;
                break;
              }
            }
            if (y < 0.f) continue;
            if (e.level <= 1)
              mark_at(y, 25, 90, 170);
            else
              mark_at(y, 140, 160, 190);
          }
        }
        // Search hits (cheap capped scan while leap search is active).
        if (search_mode && !search_query.empty() &&
            &pane == &panes[focus]) {
          xanatoo::Caret from{};
          size_t mlen = 0;
          for (int n = 0; n < 48; ++n) {
            auto hit = xanatoo::find_next(doc_of(pane), search_query, from,
                                          mlen);
            if (!hit) break;
            auto cpos = xanatoo::caret_screen_pos(
                pane.layout, *hit, 0.f,
                static_cast<float>(body_font.line_height()));
            if (cpos.valid) mark_at(cpos.y, 200, 120, 40);
            from = *hit;
            from.char_index += (mlen > 0 ? mlen : 1);
          }
        }
        // Caret position on the focused pane.
        if (&pane == &panes[focus]) {
          auto cpos = xanatoo::caret_screen_pos(
              pane.layout, pane.sel.focus, 0.f,
              static_cast<float>(body_font.line_height()));
          if (cpos.valid) mark_at(cpos.y, 40, 140, 70);
        }

        // ↑ / ↓ buttons (hover highlight when sb_hover matches)
        auto draw_arrow_btn = [&](int by, bool up) {
          const bool hot =
              (&pane == &panes[focus]) &&
              ((up && sb_hover == 1) || (!up && sb_hover == 2));
          if (hot)
            SDL_SetRenderDrawColor(sdl, 200, 210, 230, 255);
          else
            SDL_SetRenderDrawColor(sdl, 230, 228, 220, 255);
          SDL_Rect btn{sx, by, kSbW, kSbArrow};
          SDL_RenderFillRect(sdl, &btn);
          SDL_SetRenderDrawColor(sdl, 120, 118, 110, 255);
          SDL_RenderDrawRect(sdl, &btn);
          int cx = sx + kSbW / 2;
          int cy = by + kSbArrow / 2;
          SDL_SetRenderDrawColor(sdl, 60, 58, 50, 255);
          if (up) {
            SDL_RenderDrawLine(sdl, cx, cy - 3, cx - 4, cy + 2);
            SDL_RenderDrawLine(sdl, cx, cy - 3, cx + 4, cy + 2);
            SDL_RenderDrawLine(sdl, cx - 4, cy + 2, cx + 4, cy + 2);
          } else {
            SDL_RenderDrawLine(sdl, cx, cy + 3, cx - 4, cy - 2);
            SDL_RenderDrawLine(sdl, cx, cy + 3, cx + 4, cy - 2);
            SDL_RenderDrawLine(sdl, cx - 4, cy - 2, cx + 4, cy - 2);
          }
        };
        draw_arrow_btn(top, true);
        draw_arrow_btn(top + vh - kSbArrow, false);

        // Thumb
        SDL_SetRenderDrawColor(sdl, 150, 148, 140, 255);
        SDL_Rect thumb{sx + 1, static_cast<int>(thumb_y), kSbW - 2,
                       std::max(8, static_cast<int>(thumb_h))};
        SDL_RenderFillRect(sdl, &thumb);
        SDL_SetRenderDrawColor(sdl, 90, 88, 80, 255);
        SDL_RenderDrawRect(sdl, &thumb);
      }
    }

    SDL_RenderSetClipRect(sdl, nullptr);

    // Divider between panes
    if (split) {
      int mid = panes[1].x - 3;
      SDL_SetRenderDrawColor(sdl, 210, 208, 200, 255);
      SDL_Rect div{mid, static_cast<int>(ui_top), 6,
                 win_h - status_bar_h - static_cast<int>(ui_top) - static_cast<int>(ui_bottom)};
      SDL_RenderFillRect(sdl, &div);
    }

    // Visual link between panes when both carets sit on the same node
    // (e.g. after following an internal link in dual-pane shared view).
    if (split && shared_doc() &&
        panes[0].sel.focus.node != xanatoo::INVALID_NODE &&
        panes[0].sel.focus.node == panes[1].sel.focus.node) {
      float lh = static_cast<float>(body_font.line_height());
      // draw_scroll matches pane rendering (content → screen under the toolbar).
      const float ds0 = panes[0].scroll_y - ui_top;
      const float ds1 = panes[1].scroll_y - ui_top;
      auto c0 = xanatoo::caret_screen_pos(panes[0].layout, panes[0].sel.focus,
                                           ds0, lh);
      auto c1 = xanatoo::caret_screen_pos(panes[1].layout, panes[1].sel.focus,
                                           ds1, lh);
      if (c0.valid && c1.valid) {
        int x0 = panes[0].x + static_cast<int>(c0.x);
        int y0 = static_cast<int>(c0.y);
        int x1 = panes[1].x + static_cast<int>(c1.x);
        int y1 = static_cast<int>(c1.y);
        // Clamp into content area below toolbar / above status bar
        int min_y = static_cast<int>(ui_top);
        int max_y = win_h - status_bar_h;
        if (y0 >= min_y && y0 < max_y && y1 >= min_y && y1 < max_y) {
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 160);
          // Straight connector + soft endpoints
          SDL_RenderDrawLine(sdl, x0, y0, x1, y1);
          SDL_RenderDrawLine(sdl, x0, y0 + 1, x1, y1 + 1);
          auto dot = [&](int x, int y) {
            SDL_Rect r{x - 3, y - 3, 7, 7};
            SDL_RenderFillRect(sdl, &r);
          };
          dot(x0, y0);
          dot(x1, y1);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
      }
    }

    // Bottom status bar — shortcuts, or leap query when searching
    {
      const int bar_h = 28;
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      if (search_mode || save_as_mode || path_in_window_mode || open_file_mode ||
          url_bar_mode || link_edit_mode || summary_edit_mode ||
          image_insert_mode || include_insert_mode) {
        SDL_SetRenderDrawColor(sdl, 30, 30, 40, 240);
      } else {
        SDL_SetRenderDrawColor(sdl, 236, 234, 228, 245);
      }
      SDL_Rect bar{0, win_h - bar_h, win_w, bar_h};
      SDL_RenderFillRect(sdl, &bar);
      // Top edge line
      if (search_mode || save_as_mode || path_in_window_mode || open_file_mode ||
          url_bar_mode || link_edit_mode || summary_edit_mode ||
          image_insert_mode || include_insert_mode) {
        SDL_SetRenderDrawColor(sdl, 80, 160, 255, 255);
      } else {
        SDL_SetRenderDrawColor(sdl, 200, 198, 190, 255);
      }
      SDL_RenderDrawLine(sdl, 0, win_h - bar_h, win_w, win_h - bar_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      std::string label;
      uint8_t tr = 40, tg = 40, tb = 45;
      if (path_in_window_mode) {
        label = "Open in window: ";
        label += minibuf.display_with_cursor();
        label += "    Tab complete · ↑↓ history · Enter · Esc";
        label += minibuf.status_hint();
        tr = 220; tg = 230; tb = 245;
      } else if (open_file_mode) {
        label = "Open file: ";
        label += minibuf.display_with_cursor();
        label += "    Tab complete · ↑↓ history · Enter · Esc";
        label += minibuf.status_hint();
        tr = 220; tg = 230; tb = 245;
      } else if (url_bar_mode) {
        label = "URL: ";
        label += minibuf.display_with_cursor();
        label += "    ↑↓ history · Enter · Esc · gemini:// or path";
        label += minibuf.status_hint();
        tr = 220; tg = 230; tb = 245;
      } else if (save_as_mode) {
        label = "Save as: ";
        label += minibuf.display_with_cursor();
        label += "    Tab complete · ↑↓ history · Enter · Esc";
        label += minibuf.status_hint();
        tr = 220; tg = 230; tb = 245;
      } else if (image_insert_mode) {
        label = image_insert_as_figure ? "Figure image path: " : "Image path: ";
        label += minibuf.display_with_cursor();
        label += "    Tab complete · ↑↓ history · Enter · Esc";
        label += minibuf.status_hint();
        tr = 220; tg = 230; tb = 245;
      } else if (include_insert_mode) {
        label = "Include: ";
        label += minibuf.display_with_cursor();
        label += "    Tab · ↑↓ · Enter · Esc  (#H · file.md · file.md#H) · Esc";
        tr = 220; tg = 230; tb = 245;
      } else if (link_edit_mode) {
        if (link_edit_is_cite) {
          label = "Citation key: ";
          label += link_edit_href.empty() ? "…" : link_edit_href;
        } else {
          label = "Link: ";
          label += link_edit_href.empty() ? "…" : link_edit_href;
          label += "    (optional: href rel=glossary)";
        }
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (summary_edit_mode) {
        label = "Stretch summary: ";
        label += summary_edit_text.empty() ? "…" : summary_edit_text;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (search_mode && replace_mode) {
        label = "Find: ";
        label += search_query.empty() ? "…" : search_query;
        label += "  →  Replace: ";
        label += replace_text.empty() ? "…" : replace_text;
        label += replace_focus ? "  [repl] " : "  [find] ";
        label += " Tab switch · Enter replace · Ctrl+Shift+Enter all · Esc";
        tr = 220; tg = 230; tb = 245;
      } else if (search_mode) {
        label = "Leap: ";
        label += search_query.empty() ? "…" : search_query;
        label += "    F3 next · Shift+F3 prev · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (!status_notice.empty() &&
                 SDL_GetTicks() < status_notice_until) {
        label = status_notice;
        tr = 255; tg = 240; tb = 200;
      } else if (dragging_block) {
        label = "Moving block — drop before/after any block (columns & lists OK)";
        tr = 40; tg = 90; tb = 160;
      } else if (dragging_col_gap) {
        label = "Resizing columns — drag to adjust relative widths";
        tr = 40; tg = 90; tb = 160;
      } else if (diff_mode) {
        label = "Diff marks on — orange bars mark passages that differ · Ctrl+Shift+D toggles";
        tr = 160; tg = 90; tb = 30;
      } else if (browse_mode) {
        std::string page = path_of(F());
        if (page.empty()) page = gemini_current_url;
        if (!page.empty() && xanatoo::is_gemini_url(page)) {
          label = page;
          if (nav_hist_pos >= 0 && !nav_history.empty()) {
            label += "  ·  ";
            label += std::to_string(nav_hist_pos + 1);
            label += "/";
            label += std::to_string(static_cast<int>(nav_history.size()));
          }
          label += "  ·  browse · F2 edit";
        } else {
          label = "Browse (read-only) · arrows / gamepad · A/Space open · F2 edit";
        }
        tr = 40; tg = 120; tb = 90;
      } else if (!path_of(F()).empty() &&
                 xanatoo::is_gemini_url(path_of(F()))) {
        label = path_of(F());
        if (nav_hist_pos >= 0 && !nav_history.empty()) {
          label += "  ·  ";
          label += std::to_string(nav_hist_pos + 1);
          label += "/";
          label += std::to_string(static_cast<int>(nav_history.size()));
        }
        tr = 50; tg = 90; tb = 70;
      } else if (structure_mode) {
        label = "Structure highlight — colour bars + kind tags on the right · Ctrl+Shift+L toggles";
        tr = 40; tg = 80; tb = 140;
      } else if (section_focus_mode) {
        label = "Focus section — body shows one section · outline picks target · Ctrl+Alt+F off";
        tr = 25; tg = 90; tb = 170;
      } else if (outline_mode) {
        label = "Outline — ↑/↓ · ←/→ fold · Enter jump · twisty · Esc";
        tr = 40; tg = 90; tb = 160;
      } else if (hovered_col_gap) {
        label = "Column gutter — drag to adjust relative widths";
        tr = 40; tg = 100; tb = 150;
      } else if (hovered_task != xanatoo::INVALID_NODE) {
        std::string st = "task";
        if (hovered_task_pane >= 0 && hovered_task_pane < 2) {
          const auto& tn =
              doc_of(panes[hovered_task_pane]).get(hovered_task);
          st = tn.label.empty() ? "bullet" : tn.label;
        }
        if (st == "task-done")
          label = "Task — click to mark unchecked";
        else if (st == "task")
          label = "Task — click to mark done";
        else
          label = "List marker — click to convert to task checkbox";
        tr = 40; tg = 120; tb = 80;
      } else if (hovered_grip != xanatoo::INVALID_NODE) {
        label = "Drag block — drop before/after another block (columns & lists OK)";
        tr = 40; tg = 100; tb = 150;
      } else if (hovered_transclude != xanatoo::INVALID_NODE) {
        label = "Include: ";
        label += hovered_transclude_src.empty() ? "(empty)"
                                                : hovered_transclude_src;
        label += "    click to jump to source";
        tr = 30; tg = 80; tb = 170;
      } else if (hovered_image != xanatoo::INVALID_NODE) {
        if (hovered_image_handle)
          label = "Image resize handle — drag corner to scale";
        else
          label = "Image — drag corner handle to resize · scale saved in markup";
        tr = 40; tg = 100; tb = 150;
      } else if (hovered_stretch != xanatoo::INVALID_NODE) {
        bool expanded = false;
        if (hovered_stretch_pane >= 0 && hovered_stretch_pane < 2) {
          auto& sd = doc_of(panes[hovered_stretch_pane]);
          expanded = sd.get(hovered_stretch).expanded;
        }
        label = expanded ? "Stretchtext — click ▾ to collapse"
                         : "Stretchtext — click ▸ to expand";
        tr = 40; tg = 100; tb = 150;
      } else if (!hovered_href.empty()) {
        if (hovered_is_cite)
          label = "Cite: ";
        else if (hovered_href[0] == '#')
          label = "Anchor: ";
        else if (xanatoo::is_gemini_url(hovered_href))
          label = "Gemini: ";
        else if (xanatoo::is_reddit_url(hovered_href))
          label = "Reddit: ";
        else if (xanatoo::is_lemmy_url(hovered_href))
          label = "Lemmy: ";
        else if (xanatoo::is_nostr_ref(hovered_href))
          label = "Nostr: ";
        else if (hovered_href.rfind("http://", 0) == 0 ||
                 hovered_href.rfind("https://", 0) == 0)
          label = "Web: ";
        else if (hovered_href.rfind("mailto:", 0) == 0)
          label = "Mail: ";
        else
          label = "Document: ";
        label += hovered_href;
        if (!hovered_rel.empty()) {
          label += "  rel=";
          label += hovered_rel;
        }
        if (hovered_href[0] == '#')
          label += "    click to jump";
        else if (hovered_href.rfind("http://", 0) == 0 ||
                 hovered_href.rfind("https://", 0) == 0 ||
                 hovered_href.rfind("mailto:", 0) == 0)
          label += "    click to open externally";
        else
          label += "    click to open";
        tr = 30; tg = 80; tb = 170;
      } else if (browse_mode) {
        label = gamepad
                    ? "Browse · R-stick pointer · A click · LB+A caret · "
                      "D-pad nav · L-stick scroll · Start help · F2 edit"
                    : "Browse mode — arrows move · Enter open · F2 edit · F1 help";
        tr = 50; tg = 90; tb = 70;
      } else {
        label =
            "F1 help · Ctrl+S save · Ctrl+A block · Ctrl+Shift+A all · "
            "Ctrl+F leap · Ctrl+B/I style · drag to select";
        tr = 70; tg = 68; tb = 62;
      }
      float baseline = static_cast<float>(win_h - 8) - ui_bottom;
      renderer.draw_text(body_font, 10.f, baseline, label, tr, tg, tb);

      // Document / selection stats on the right (idle status only)
      if (!search_mode && !save_as_mode && !link_edit_mode &&
          !summary_edit_mode && !help_mode && !outline_mode &&
          !image_insert_mode && !include_insert_mode &&
          !path_in_window_mode && !open_file_mode) {
        std::string stats;
        const auto& fsel = F().sel;
        if (!fsel.empty()) {
          std::string t = xanatoo::selected_text(doc_of(F()), fsel);
          size_t words = 0, chars = 0;
          bool in_word = false;
          for (unsigned char c : t) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
              in_word = false;
            } else {
              ++chars;
              if (!in_word) {
                in_word = true;
                ++words;
              }
            }
          }
          stats = std::to_string(words) + " sel words · " +
                  std::to_string(chars) + " sel chars";
        } else {
          auto st = xanatoo::document_stats(doc_of(F()));
          stats = std::to_string(st.words) + " words · " +
                  std::to_string(st.chars) + " chars";
          if (st.headings > 0)
            stats += " · " + std::to_string(st.headings) + " headings";
        }
        int sw = body_font.measure(stats);
        renderer.draw_text(body_font,
                           static_cast<float>(win_w - sw - 12), baseline, stats,
                           110, 108, 100);
      }
    }

    // Heading outline: docked dual-pane (left strip) or floating window
    if (outline_mode) {
      if (outline_docked) {
        ol_x = 0;
        ol_y = static_cast<int>(ui_top);
        ol_w = std::max(160, std::min(outline_dock_w, win_w / 2));
        ol_h = win_h - status_bar_h - ol_y;
        outline_dock_w = ol_w;
      } else {
        if (ol_w < 200) ol_w = 200;
        if (ol_h < 140) ol_h = 140;
      }
      const float step = outline_row_step();
      auto [cx, cy, cw, ch] = outline_client_rect();
      {
        float content_h =
            static_cast<float>(outline.size()) * step + 8.f;
        float max_s = std::max(0.f, content_h - static_cast<float>(ch));
        if (ol_scroll > max_s) ol_scroll = max_s;
      }

      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 0, 0, 0, 40);
      SDL_Rect shadow{ol_x + 3, ol_y + 3, ol_w, ol_h};
      SDL_RenderFillRect(sdl, &shadow);
      SDL_SetRenderDrawColor(sdl, 248, 246, 240, 255);
      SDL_Rect frame{ol_x, ol_y, ol_w, ol_h};
      SDL_RenderFillRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
      SDL_RenderDrawRect(sdl, &frame);
      // Title bar
      SDL_SetRenderDrawColor(sdl, 200, 216, 240, 255);
      SDL_Rect title{ol_x + 1, ol_y + 1, ol_w - 2, help_title_h};
      SDL_RenderFillRect(sdl, &title);
      SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
      SDL_RenderDrawLine(sdl, ol_x, ol_y + help_title_h, ol_x + ol_w,
                         ol_y + help_title_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      {
        std::string ol_title =
            outline_docked ? "Outline · body →" : "Outline";
        if (section_focus_mode) {
          ol_title = "Outline · Focus";
          if (!outline.empty() && outline_sel >= 0 &&
              outline_sel < static_cast<int>(outline.size())) {
            const std::string& tx =
                outline[static_cast<size_t>(outline_sel)].text;
            if (!tx.empty()) {
              ol_title += ": ";
              ol_title += tx.size() > 18 ? tx.substr(0, 17) + "…" : tx;
            }
          }
        }
        renderer.draw_text(body_font, static_cast<float>(ol_x + 10),
                           static_cast<float>(ol_y + 6 + body_font.ascender()),
                           ol_title, 25, 90, 170);
      }
      // Focus toggle (section isolation) — left of close.
      {
        const char* flab = section_focus_mode ? "Focus●" : "Focus";
        int fw = body_font.measure(flab);
        int fx = ol_x + ol_w - 28 - fw - 10;
        if (section_focus_mode) {
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 50);
          SDL_Rect chip{fx - 4, ol_y + 4, fw + 8, help_title_h - 6};
          SDL_RenderFillRect(sdl, &chip);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        renderer.draw_text(
            body_font, static_cast<float>(fx),
            static_cast<float>(ol_y + 6 + body_font.ascender()), flab,
            section_focus_mode ? 25 : 80, section_focus_mode ? 90 : 70,
            section_focus_mode ? 170 : 60);
      }
      renderer.draw_text(body_font, static_cast<float>(ol_x + ol_w - 22),
                         static_cast<float>(ol_y + 6 + body_font.ascender()),
                         "×", 80, 40, 40);

      SDL_Rect clip{cx, cy, cw, ch};
      SDL_RenderSetClipRect(sdl, &clip);
      float ty = static_cast<float>(cy) + body_font.ascender() - ol_scroll;
      for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
        if (ty > static_cast<float>(cy + ch) + step) break;
        if (ty + step < static_cast<float>(cy)) {
          ty += step;
          continue;
        }
        const auto& e = outline[static_cast<size_t>(i)];
        std::string line;
        if (i < 9) {
          line += static_cast<char>('1' + i);
          line += " ";
        } else {
          line += "  ";
        }
        for (int ind = 1; ind < e.level; ++ind) line += "  ";
        if (e.has_children)
          line += e.folded ? "▸ " : "▾ ";
        else
          line += "  ";
        line += e.text;
        if (e.folded && e.has_children) line += " …";
        if (i == outline_sel) {
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 40);
          SDL_Rect hi{cx + 2,
                      static_cast<int>(ty - body_font.ascender() - 2),
                      cw - 4, static_cast<int>(step)};
          SDL_RenderFillRect(sdl, &hi);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
          renderer.draw_text(body_font, static_cast<float>(cx + 8), ty, line,
                             25, 90, 170);
        } else {
          renderer.draw_text(body_font, static_cast<float>(cx + 8), ty, line,
                             50, 48, 42);
        }
        ty += step;
      }
      if (outline.empty()) {
        renderer.draw_text(
            body_font, static_cast<float>(cx + 8),
            static_cast<float>(cy) + body_font.ascender(),
            "(no headings in this document)", 120, 118, 110);
      }
      SDL_RenderSetClipRect(sdl, nullptr);

      // Scrollbar
      {
        float content_h =
            static_cast<float>(outline.size()) * step + 8.f;
        float max_s = std::max(0.f, content_h - static_cast<float>(ch));
        if (max_s > 1.f) {
          const int sb_w = 8;
          float view_h = static_cast<float>(ch);
          float thumb_h =
              std::max(20.f, view_h * (view_h / (view_h + max_s)));
          float track = std::max(0.f, view_h - thumb_h);
          float ratio = ol_scroll / max_s;
          int thumb_y = cy + static_cast<int>(ratio * track);
          int sx = cx + cw - sb_w;
          SDL_SetRenderDrawColor(sdl, 220, 218, 212, 255);
          SDL_Rect track_r{sx, cy, sb_w, ch};
          SDL_RenderFillRect(sdl, &track_r);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
          SDL_Rect thumb{sx, thumb_y, sb_w, static_cast<int>(thumb_h)};
          SDL_RenderFillRect(sdl, &thumb);
        }
      }
      // Resize grip
      {
        const int gx = ol_x + ol_w - 3;
        const int gy = ol_y + ol_h - 3;
        SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
        for (int i = 0; i < 3; ++i)
          SDL_RenderDrawLine(sdl, gx - i * 3, gy, gx, gy - i * 3);
      }
    }

    // Backlinks soft window (Ctrl+Alt+B)
    if (backlinks_mode) {
      if (bl_w < 200) bl_w = 200;
      if (bl_h < 120) bl_h = 120;
      const float step = 22.f;
      const int title_h = help_title_h;
      const int pad = 8;
      int cx = bl_x + pad, cy = bl_y + title_h + 4;
      int cw = bl_w - pad * 2, ch = bl_h - title_h - pad - 4;
      {
        float content_h = static_cast<float>(backlinks.size()) * step + 8.f;
        float max_s = std::max(0.f, content_h - static_cast<float>(ch));
        if (bl_scroll > max_s) bl_scroll = max_s;
      }
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 0, 0, 0, 40);
      SDL_Rect shadow{bl_x + 3, bl_y + 3, bl_w, bl_h};
      SDL_RenderFillRect(sdl, &shadow);
      SDL_SetRenderDrawColor(sdl, 248, 246, 240, 255);
      SDL_Rect frame{bl_x, bl_y, bl_w, bl_h};
      SDL_RenderFillRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 90, 60, 140, 255);
      SDL_RenderDrawRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 220, 210, 235, 255);
      SDL_Rect title{bl_x + 1, bl_y + 1, bl_w - 2, title_h};
      SDL_RenderFillRect(sdl, &title);
      SDL_SetRenderDrawColor(sdl, 90, 60, 140, 255);
      SDL_RenderDrawLine(sdl, bl_x, bl_y + title_h, bl_x + bl_w, bl_y + title_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      std::string title_s = backlinks_label.empty()
                                ? "Backlinks (no label under caret)"
                                : ("Backlinks → #" + backlinks_label);
      renderer.draw_text(body_font, static_cast<float>(bl_x + 10),
                         static_cast<float>(bl_y + 6 + body_font.ascender()),
                         title_s, 90, 60, 140);
      renderer.draw_text(body_font, static_cast<float>(bl_x + bl_w - 22),
                         static_cast<float>(bl_y + 6 + body_font.ascender()),
                         "×", 80, 40, 40);
      SDL_Rect clip{cx, cy, cw, ch};
      SDL_RenderSetClipRect(sdl, &clip);
      float ty = static_cast<float>(cy) + body_font.ascender() - bl_scroll;
      if (backlinks.empty()) {
        renderer.draw_text(body_font, static_cast<float>(cx + 8), ty,
                           "(none in this document)", 120, 118, 110);
      }
      for (int i = 0; i < static_cast<int>(backlinks.size()); ++i) {
        if (ty > static_cast<float>(cy + ch) + step) break;
        if (ty + step >= static_cast<float>(cy)) {
          const auto& e = backlinks[static_cast<size_t>(i)];
          std::string line = e.kind;
          if (!e.rel.empty()) line += ":" + e.rel;
          line += "  ";
          line += e.preview;
          if (i == backlinks_sel) {
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl, 90, 60, 140, 40);
            SDL_Rect hi{cx + 2, static_cast<int>(ty - body_font.ascender() - 2),
                        cw - 4, static_cast<int>(step)};
            SDL_RenderFillRect(sdl, &hi);
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
          }
          renderer.draw_text(body_font, static_cast<float>(cx + 8), ty, line, 40,
                             40, 45);
        }
        ty += step;
      }
      SDL_RenderSetClipRect(sdl, nullptr);
    }

    // Help as a real document in a software window
    if (help_mode) {
      ensure_help_doc();
      if (help_w < 280) help_w = 280;
      if (help_h < 200) help_h = 200;
      if (help_needs_layout) layout_help();

      // Clamp scroll
      {
        auto [cx, cy, cw, ch] = help_client_rect();
        float max_s = std::max(0.f, help_layout.content_height - static_cast<float>(ch) + 40.f);
        if (help_scroll > max_s) help_scroll = max_s;
      }

      // Frame + title bar
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 0, 0, 0, 40);
      SDL_Rect shadow{help_x + 3, help_y + 3, help_w, help_h};
      SDL_RenderFillRect(sdl, &shadow);
      SDL_SetRenderDrawColor(sdl, 246, 245, 244, 255);
      SDL_Rect frame{help_x, help_y, help_w, help_h};
      SDL_RenderFillRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 156, 154, 151, 255);
      SDL_RenderDrawRect(sdl, &frame);
      // Title bar (Clearlooks-ish blue-grey)
      SDL_SetRenderDrawColor(sdl, 200, 212, 232, 255);
      SDL_Rect title{help_x + 1, help_y + 1, help_w - 2, help_title_h};
      SDL_RenderFillRect(sdl, &title);
      SDL_SetRenderDrawColor(sdl, 156, 154, 151, 255);
      SDL_RenderDrawLine(sdl, help_x, help_y + help_title_h,
                         help_x + help_w, help_y + help_title_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      const char* title_txt = "Help — xanatoo";
      renderer.draw_text(body_font, static_cast<float>(help_x + 10),
                         static_cast<float>(help_y + 6 + body_font.ascender()),
                         title_txt, 46, 52, 54);
      // Close affordance
      renderer.draw_text(body_font, static_cast<float>(help_x + help_w - 22),
                         static_cast<float>(help_y + 6 + body_font.ascender()),
                         "×", 80, 40, 40);

      auto [cx, cy, cw, ch] = help_client_rect();
      SDL_Rect clip{cx, cy, cw, ch};
      SDL_RenderSetClipRect(sdl, &clip);
      // screen_y = content_y - scroll + client_y  ⇒  draw_scroll = scroll - client_y
      // (mirrors pane path: draw_scroll = scroll_y - ui_top)
      const float help_draw_scroll = help_scroll - static_cast<float>(cy);
      renderer.draw(help_layout, help_draw_scroll, static_cast<float>(cx));
      SDL_RenderSetClipRect(sdl, nullptr);

      // Client scrollbar when content overflows
      {
        float max_s = std::max(
            0.f, help_layout.content_height - static_cast<float>(ch) + 40.f);
        if (max_s > 1.f) {
          const int sb_w = 8;
          float view_h = static_cast<float>(ch);
          float thumb_h = std::max(20.f, view_h * (view_h / (view_h + max_s)));
          float track = std::max(0.f, view_h - thumb_h);
          float ratio = help_scroll / max_s;
          int thumb_y = cy + static_cast<int>(ratio * track);
          int sx = cx + cw - sb_w;
          SDL_SetRenderDrawColor(sdl, 220, 218, 212, 255);
          SDL_Rect track_r{sx, cy, sb_w, ch};
          SDL_RenderFillRect(sdl, &track_r);
          SDL_SetRenderDrawColor(sdl, 140, 138, 130, 255);
          SDL_Rect thumb{sx, thumb_y, sb_w, static_cast<int>(thumb_h)};
          SDL_RenderFillRect(sdl, &thumb);
        }
      }

      // SE resize grip (classic desktop affordance)
      {
        const int gx = help_x + help_w - 3;
        const int gy = help_y + help_h - 3;
        SDL_SetRenderDrawColor(sdl, 120, 118, 110, 255);
        for (int i = 0; i < 3; ++i) {
          SDL_RenderDrawLine(sdl, gx - i * 3, gy, gx, gy - i * 3);
        }
      }
    }


        clamp_all_floating();

    // Drop soft windows whose buffer was removed / is out of range.
    soft_wins.erase(
        std::remove_if(soft_wins.begin(), soft_wins.end(),
                       [&](const SoftWindow& sw) {
                         return sw.buf < 0 ||
                                sw.buf >= static_cast<int>(buffers.size());
                       }),
        soft_wins.end());

    // Document software windows (bottom → top so last is topmost)
    for (auto& sw : soft_wins) {
      if (sw.w < 280) sw.w = 280;
      if (sw.h < 200) sw.h = 200;
      if (sw.needs_layout) layout_soft(sw);
      auto [cx, cy, cw, ch] = soft_client_rect(sw);
      {
        float max_s = std::max(
            0.f, sw.layout.content_height - static_cast<float>(ch) + 40.f);
        if (sw.scroll > max_s) sw.scroll = max_s;
      }
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 0, 0, 0, 40);
      SDL_Rect shadow{sw.x + 3, sw.y + 3, sw.w, sw.h};
      SDL_RenderFillRect(sdl, &shadow);
      SDL_SetRenderDrawColor(sdl, 246, 245, 244, 255);
      SDL_Rect frame{sw.x, sw.y, sw.w, sw.h};
      SDL_RenderFillRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 120, 140, 100, 255);
      SDL_RenderDrawRect(sdl, &frame);
      SDL_SetRenderDrawColor(sdl, 210, 225, 190, 255);
      SDL_Rect title{sw.x + 1, sw.y + 1, sw.w - 2, help_title_h};
      SDL_RenderFillRect(sdl, &title);
      SDL_SetRenderDrawColor(sdl, 120, 140, 100, 255);
      SDL_RenderDrawLine(sdl, sw.x, sw.y + help_title_h, sw.x + sw.w,
                         sw.y + help_title_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      {
        const std::string stitle = soft_title(sw);
        renderer.draw_text(body_font, static_cast<float>(sw.x + 10),
                           static_cast<float>(sw.y + 6 + body_font.ascender()),
                           stitle.c_str(), 40, 55, 35);
      }
      renderer.draw_text(body_font, static_cast<float>(sw.x + sw.w - 22),
                         static_cast<float>(sw.y + 6 + body_font.ascender()),
                         "×", 80, 40, 40);
      SDL_Rect clip{cx, cy, cw, ch};
      SDL_RenderSetClipRect(sdl, &clip);
      renderer.draw(sw.layout, sw.scroll - static_cast<float>(cy),
                    static_cast<float>(cx));
      SDL_RenderSetClipRect(sdl, nullptr);
      {
        float max_s = std::max(
            0.f, sw.layout.content_height - static_cast<float>(ch) + 40.f);
        if (max_s > 1.f) {
          const int sb_w = 8;
          float view_h = static_cast<float>(ch);
          float thumb_h =
              std::max(20.f, view_h * (view_h / (view_h + max_s)));
          float track = std::max(0.f, view_h - thumb_h);
          float ratio = sw.scroll / max_s;
          int thumb_y = cy + static_cast<int>(ratio * track);
          int sx = cx + cw - sb_w;
          SDL_SetRenderDrawColor(sdl, 220, 218, 212, 255);
          SDL_Rect track_r{sx, cy, sb_w, ch};
          SDL_RenderFillRect(sdl, &track_r);
          SDL_SetRenderDrawColor(sdl, 100, 130, 80, 255);
          SDL_Rect thumb{sx, thumb_y, sb_w, static_cast<int>(thumb_h)};
          SDL_RenderFillRect(sdl, &thumb);
        }
      }
      {
        const int gx = sw.x + sw.w - 3;
        const int gy = sw.y + sw.h - 3;
        SDL_SetRenderDrawColor(sdl, 100, 130, 80, 255);
        for (int i = 0; i < 3; ++i)
          SDL_RenderDrawLine(sdl, gx - i * 3, gy, gx, gy - i * 3);
      }
    }

    // Live dock-zone preview while dragging a soft window title bar
    if (dock_preview != SoftDock::None) {
      auto [dx, dy, dw, dh] = dock_rect(dock_preview);
      if (dw > 0 && dh > 0) {
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl, 40, 100, 180, 70);
        SDL_Rect zone{dx, dy, dw, dh};
        SDL_RenderFillRect(sdl, &zone);
        SDL_SetRenderDrawColor(sdl, 40, 100, 180, 180);
        SDL_RenderDrawRect(sdl, &zone);
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        const char* label = "";
        switch (dock_preview) {
          case SoftDock::Left: label = "Tile left"; break;
          case SoftDock::Right: label = "Tile right"; break;
          case SoftDock::Top: label = "Tile top"; break;
          case SoftDock::Bottom: label = "Tile bottom"; break;
          case SoftDock::Max: label = "Maximize"; break;
          default: break;
        }
        if (label[0]) {
          int tw = body_font.measure(label);
          float lx = static_cast<float>(dx + (dw - tw) / 2);
          float ly = static_cast<float>(dy + dh / 2);
          renderer.draw_text(body_font, lx, ly, label, 30, 60, 120);
        }
      }
    }

    // Hover tooltip near the pointer: link target / stretch hint
    {
      const bool prompt =
          search_mode || save_as_mode || link_edit_mode || summary_edit_mode ||
          help_mode || url_bar_mode || open_file_mode || path_in_window_mode ||
          image_insert_mode || include_insert_mode;
      std::string tip;
      if (!prompt) {
        if (hovered_stretch != xanatoo::INVALID_NODE) {
          tip = "Click to expand / collapse";
        } else if (!hovered_href.empty()) {
          tip = hovered_href;
          if (!hovered_preview.empty() && hovered_href[0] == '#') {
            tip += "  —  ";
            tip += hovered_preview;
          }
          if (!hovered_rel.empty()) {
            tip += "  ·  rel=";
            tip += hovered_rel;
          }
        } else if (hovered_image_handle) {
          tip = "Drag to resize image";
        } else if (hovered_col_gap) {
          tip = "Drag to resize columns";
        } else if (hovered_task != xanatoo::INVALID_NODE) {
          tip = "Click to toggle task";
        } else if (hovered_grip != xanatoo::INVALID_NODE) {
          tip = "Drag to move block";
        } else if (hovered_transclude != xanatoo::INVALID_NODE) {
          tip = hovered_transclude_src.empty()
                    ? "Include"
                    : ("Include: " + hovered_transclude_src);
        }
      }
      if (!tip.empty()) {
        if (tip.size() > 96) tip = tip.substr(0, 93) + "…";
        int tw = body_font.measure(tip);
        const int pad_x = 10, pad_y = 6;
        int box_w = tw + pad_x * 2;
        int box_h = body_font.line_height() + pad_y * 2;
        int bx = static_cast<int>(mouse_x) + 14;
        int by = static_cast<int>(mouse_y) - box_h - 10;
        if (bx + box_w > win_w - 4) bx = win_w - box_w - 4;
        if (bx < 4) bx = 4;
        if (by < static_cast<int>(ui_top) + 4)
          by = static_cast<int>(mouse_y) + 18;
        if (by + box_h > win_h - 32) by = win_h - 32 - box_h;

        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl, 28, 36, 50, 235);
        SDL_Rect box{bx, by, box_w, box_h};
        SDL_RenderFillRect(sdl, &box);
        // Accent by kind
        if (hovered_stretch != xanatoo::INVALID_NODE)
          SDL_SetRenderDrawColor(sdl, 40, 140, 90, 255);
        else if (hovered_col_gap || hovered_image_handle)
          SDL_SetRenderDrawColor(sdl, 40, 100, 180, 255);
        else
          SDL_SetRenderDrawColor(sdl, 90, 150, 220, 255);
        SDL_RenderDrawRect(sdl, &box);
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        float tip_baseline =
            static_cast<float>(by + pad_y + body_font.ascender());
        renderer.draw_text(body_font, static_cast<float>(bx + pad_x),
                           tip_baseline, tip, 235, 240, 250);
      }
    }

    // Hardware mouse pointer shape (always; independent of stick sprite).
    if (hovered_col_gap)
      set_cursor(cursor_sizewe);
    else if (hovered_image_handle)
      set_cursor(cursor_sizenwse);
    else if (hovered_grip != xanatoo::INVALID_NODE)
      set_cursor(cursor_sizeall);
    else if (!hovered_href.empty() || hovered_stretch != xanatoo::INVALID_NODE ||
             hovered_image != xanatoo::INVALID_NODE ||
             hovered_task != xanatoo::INVALID_NODE ||
             hovered_transclude != xanatoo::INVALID_NODE)
      set_cursor(cursor_hand);
    else
      set_cursor(cursor_arrow);

    ui.end_frame(sdl);

    // Extra stick pointer (geometry, not a texture). System cursor untouched.
    if (stick_cursor_on) {
      draw_stick_cursor(sdl, static_cast<int>(stick_cx),
                        static_cast<int>(stick_cy), stick_lmb || stick_rmb);
    }

    renderer.present();
  }

  SDL_StopTextInput();
  ui.shutdown();
  if (gamepad) {
    SDL_GameControllerClose(gamepad);
    gamepad = nullptr;
  }
  if (cursor_arrow) SDL_FreeCursor(cursor_arrow);
  if (cursor_hand) SDL_FreeCursor(cursor_hand);
  if (cursor_sizewe) SDL_FreeCursor(cursor_sizewe);
  if (cursor_sizenwse) SDL_FreeCursor(cursor_sizenwse);
  if (cursor_sizeall) SDL_FreeCursor(cursor_sizeall);
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
