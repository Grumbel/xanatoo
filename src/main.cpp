// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// xanatoo — interactive document composition engine

#include "xanatoo/types.hpp"
#include "xanatoo/font.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/render.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/images.hpp"
#include "xanatoo/ui.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/cli.hpp"

#include <SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  xanatoo::CliOptions opt;
  if (!xanatoo::parse_cli(argc, argv, opt)) {
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_StartTextInput();

  SDL_Window* window = SDL_CreateWindow(
      "xanatoo",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      920, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
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


  auto load_doc = [&](const std::string& path) -> xanatoo::Document {
    if (path.empty()) {
      return xanatoo::parse_document(xanatoo::fallback_document(), {});
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

  xanatoo::Document docs[2];
  std::string paths[2];
  paths[0] = opt.file_path;
  paths[1] = opt.file_path2;
  if (paths[0].empty()) {
    paths[0] = xanatoo::find_default_document();
    if (paths[0].empty())
      std::cerr << "Note: examples/welcome.md not found; starting empty.\n";
  }
  docs[0] = load_doc(paths[0]);
  docs[1] = paths[1].empty() ? docs[0] : load_doc(paths[1]);
  // If only one file, pane 1 shares docs[0] until a second file is used
  bool shared_doc = paths[1].empty();
  bool dirty_flags[2] = {false, false};

  xanatoo::ImageCache images(sdl);

  auto update_title = [&]() {
    std::string title = "xanatoo";
    auto label = [&](int i) {
      std::string s;
      if (dirty_flags[i]) s += "*";
      if (paths[i].empty()) s += "untitled";
      else s += paths[i];
      return s;
    };
    if (shared_doc || paths[1].empty()) {
      title += " — " + label(0);
    } else {
      title += " — " + label(0) + " | " + label(1);
    }
    SDL_SetWindowTitle(window, title.c_str());
  };
  update_title();

  xanatoo::LayoutEngine layout_engine(fonts, &images);
  xanatoo::Renderer renderer(sdl, fonts, &images);
  xanatoo::Ui ui;
  if (!ui.init(window, sdl)) {
    std::cerr << "Warning: ImGui UI failed to init; continuing without menus\n";
  }
  float ui_top = 0.f;  // menu + toolbar height (updated each frame)

  struct Pane {
    int doc_index = 0;  // which docs[] entry
    xanatoo::Selection sel{};
    float scroll_y = 0.f;
    float preferred_x = 0.f;
    xanatoo::LayoutResult layout{};
    int x = 0;
    int w = 0;
  };

  Pane panes[2];
  panes[0].doc_index = 0;
  panes[1].doc_index = shared_doc ? 0 : 1;
  xanatoo::ensure_valid_caret(docs[0], panes[0].sel.focus);
  panes[0].sel.anchor = panes[0].sel.focus;
  xanatoo::ensure_valid_caret(docs[panes[1].doc_index], panes[1].sel.focus);
  panes[1].sel.anchor = panes[1].sel.focus;

  int focus = 0;
  bool split = !paths[1].empty();  // auto-split when two files given
  float split_ratio = 0.5f;        // left pane fraction
  bool dragging_sel = false;
  bool dragging_split = false;
  bool dragging_scroll = false;
  int scroll_drag_pane = 0;
  bool dragging_image = false;
  xanatoo::NodeId resize_image = xanatoo::INVALID_NODE;
  float resize_start_x = 0.f;
  float resize_start_scale = 1.f;
  // Block drag-reorder (grab left margin of a layout box)
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
    return docs[p.doc_index];
  };
  auto path_of = [&](Pane& p) -> std::string& {
    return paths[p.doc_index];
  };
  auto base_of = [&](Pane& p) -> std::string {
    return parent_dir(paths[p.doc_index]);
  };

  xanatoo::History histories[2];
  histories[0].reset(docs[0], panes[0].sel);
  histories[1].reset(docs[panes[1].doc_index], panes[1].sel);

  int win_w = 920, win_h = 720;
  bool running = true;
  bool needs_layout = true;

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
    std::string title;
    std::string path;
    xanatoo::Document doc;
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

  bool help_mode = false;  // F1 help as a real document in a software window
  xanatoo::Document help_doc;
  xanatoo::LayoutResult help_layout;
  float help_scroll = 0.f;
  bool help_needs_layout = true;
  bool help_loaded = false;
  // Software window geometry (window coordinates)
  int help_x = 80, help_y = 60, help_w = 560, help_h = 480;
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
  std::vector<xanatoo::OutlineEntry> outline;
  int outline_sel = 0;
  int ol_x = 40, ol_y = 90, ol_w = 320, ol_h = 420;
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
  // Hovered link href (shown in status bar when not in a modal mode)
  std::string hovered_href;
  std::string hovered_rel;      // typed :rel when present
  bool hovered_is_cite = false;
  std::string hovered_preview;  // internal #anchor target snippet
  float mouse_x = 0.f, mouse_y = 0.f;

  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto F = [&]() -> Pane& { return panes[focus]; };

  auto refresh_outline = [&]() {
    xanatoo::collect_outline(doc_of(F()), outline);
    if (outline_sel >= static_cast<int>(outline.size()))
      outline_sel = std::max(0, static_cast<int>(outline.size()) - 1);
  };

  auto refresh_diff = [&]() {
    diff_marks[0].clear();
    diff_marks[1].clear();
    diff_words[0].clear();
    diff_words[1].clear();
    if (!diff_mode || shared_doc || paths[1].empty()) return;
    xanatoo::compute_diff_marks(docs[0], docs[1], diff_marks[0], diff_marks[1],
                                &diff_words[0], &diff_words[1]);
  };

  auto checkpoint = [&]() {
    Pane& p = F();
    histories[p.doc_index].push(doc_of(p), p.sel);
  };
  auto mark_dirty = [&]() {
    int di = F().doc_index;
    if (!dirty_flags[di]) {
      dirty_flags[di] = true;
      update_title();
    }
  };
  auto edit = [&]() {
    checkpoint();
    mark_dirty();
  };

  auto layout_panes = [&]() {
    const int gutter = split ? 6 : 0;
    if (split) {
      int left_w = static_cast<int>(split_ratio * (win_w - gutter));
      left_w = std::max(120, std::min(win_w - gutter - 120, left_w));
      panes[0].x = 0;
      panes[0].w = left_w;
      panes[1].x = left_w + gutter;
      panes[1].w = win_w - panes[1].x;
    } else {
      panes[0].x = 0;
      panes[0].w = win_w;
      panes[1].x = 0;
      panes[1].w = 0;
    }
    panes[0].layout = layout_engine.layout(
        doc_of(panes[0]), static_cast<float>(panes[0].w), base_of(panes[0]));
    if (split) {
      panes[1].layout = layout_engine.layout(
          doc_of(panes[1]), static_cast<float>(panes[1].w), base_of(panes[1]));
    }
    needs_layout = false;
    refresh_diff();
  };

  auto scroll_caret_into_view = [&](Pane& p) {
    auto cpos = xanatoo::caret_screen_pos(
        p.layout, p.sel.focus, 0.f,
        static_cast<float>(body_font.line_height()));
    if (!cpos.valid) return;
    float doc_y = cpos.y;
    float margin = 40.f;
    // Visible content height excludes menu/toolbar and status bar.
    float view_h = static_cast<float>(win_h) - ui_top - 28.f;
    if (view_h < 80.f) view_h = 80.f;
    if (doc_y - p.scroll_y < margin) {
      p.scroll_y = std::max(0.f, doc_y - margin);
    } else if (doc_y - p.scroll_y > view_h - margin) {
      p.scroll_y = doc_y - (view_h - margin);
    }
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
    const char* prefixes[] = {"", "examples/", "../", "../examples/", "../../", "../../examples/", nullptr};
    for (const char** pre = prefixes; *pre; ++pre) {
      std::string pth = std::string(*pre) + name;
      if (fs::exists(pth)) return fs::absolute(pth).string();
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

  auto soft_client_rect = [&](const SoftWindow& sw) {
    return std::tuple{sw.x + help_pad, sw.y + help_title_h + 4,
                      sw.w - help_pad * 2,
                      sw.h - help_title_h - help_pad - 4};
  };

  // Edge-snap into the client area (below menu/toolbar, above status bar).
  auto snap_soft_geom = [&](int& x, int& y, int w, int h) {
    const int top = static_cast<int>(ui_top);
    const int bottom = win_h - status_bar_h;
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
    }
  };

  auto layout_soft = [&](SoftWindow& sw) {
    float mw = static_cast<float>(std::max(120, sw.w - help_pad * 2));
    sw.layout = layout_engine.layout(sw.doc, mw);
    sw.needs_layout = false;
  };

  // Raise window i to the top of the stack (back of vector).
  auto raise_soft = [&](size_t i) {
    if (i + 1 >= soft_wins.size()) return;
    SoftWindow sw = std::move(soft_wins[i]);
    soft_wins.erase(soft_wins.begin() + static_cast<std::ptrdiff_t>(i));
    soft_wins.push_back(std::move(sw));
  };

  auto open_browse = [&](const std::string& path, const std::string& title) {
    if (path.empty() || !fs::exists(path)) {
      std::cerr << "Cannot open in window: " << path << "\n";
      return;
    }
    // Focus existing window for the same path.
    std::string canon = path;
    try {
      canon = fs::weakly_canonical(path).string();
    } catch (...) {
    }
    for (size_t i = 0; i < soft_wins.size(); ++i) {
      std::string existing = soft_wins[i].path;
      try {
        existing = fs::weakly_canonical(existing).string();
      } catch (...) {
      }
      if (existing == canon) {
        raise_soft(i);
        std::cout << "Software window (focus): " << path << "\n";
        return;
      }
    }
    SoftWindow sw;
    sw.doc = load_doc(path);
    sw.path = path;
    sw.title = title.empty() ? path : title;
    sw.needs_layout = true;
    sw.scroll = 0.f;
    // Cascade so multiple windows stay visible.
    const int cascade = static_cast<int>(soft_wins.size()) * 28;
    sw.x = help_x + 30 + cascade;
    sw.y = help_y + 30 + cascade;
    if (sw.x + sw.w > win_w - 20) sw.x = 40 + (cascade % 60);
    if (sw.y + sw.h > win_h - 40) sw.y = static_cast<int>(ui_top) + 20 + (cascade % 40);
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
    p.sel.focus = {outline[static_cast<size_t>(outline_sel)].node, 0, 0};
    p.sel.anchor = p.sel.focus;
    scroll_caret_into_view(p);
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
          if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            win_w = ev.window.data1;
            win_h = ev.window.data2;
            // Keep tiled/maximized soft windows filling the new client area.
            for (auto& sw : soft_wins) {
              if (sw.dock != SoftDock::None || sw.maximized)
                apply_soft_dock(sw, sw.maximized ? SoftDock::Max : sw.dock);
            }
            needs_layout = true;
          }
          break;

        case SDL_DROPFILE: {
          // Drag-and-drop image files from the desktop/file manager into a pane.
          char* dropped = ev.drop.file;
          if (!dropped) break;
          std::string fpath(dropped);
          SDL_free(dropped);

          if (!xanatoo::is_raster_image_path(fpath)) {
            std::cerr << "Drop ignored (not an image): " << fpath << "\n";
            break;
          }

          int mx = 0, my_raw = 0;
          SDL_GetMouseState(&mx, &my_raw);
          if (static_cast<float>(my_raw) < ui_top) break;

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
          int pi = pane_at(static_cast<float>(mxw));
          Pane& p = panes[pi];
          p.scroll_y -= static_cast<float>(ev.wheel.y) * 48.f;
          p.scroll_y = std::max(0.f, p.scroll_y);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
          // Menu/toolbar strip belongs to ImGui.
          if (static_cast<float>(ev.button.y) < ui_top) break;

          if (ev.button.button == SDL_BUTTON_LEFT) {
            const int mxw = ev.button.x, myw = ev.button.y;

            // Software windows are custom-drawn (not ImGui). Hit-test them
            // before the ImGui capture check and before the document panes.
            // Z-order matches paint order: outline → browse → help.

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
                  ol_resize = (on_r ? 1 : 0) | (on_b ? 2 : 0);
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
                  ol_dragging = true;
                  ol_drag_dx = mxw - ol_x;
                  ol_drag_dy = myw - ol_y;
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
                    jump_outline_sel();
                    // Keep window open (TOC stays available).
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
                    if (auto sid = xanatoo::hit_test_stretch(
                            top.layout, lx, ly, top.scroll)) {
                      if (top.doc.get(*sid).kind ==
                          xanatoo::NodeKind::Stretch) {
                        top.doc.get(*sid).expanded =
                            !top.doc.get(*sid).expanded;
                        top.needs_layout = true;
                      }
                    }
                    if (auto href = xanatoo::hit_test_link(
                            top.doc, top.layout, lx, ly, top.scroll)) {
                      if (!href->empty() && (*href)[0] == '#') {
                        if (auto dest =
                                xanatoo::jump_to_href(top.doc, *href)) {
                          auto cpos = xanatoo::caret_screen_pos(
                              top.layout, *dest, 0.f,
                              static_cast<float>(body_font.line_height()));
                          if (cpos.valid)
                            top.scroll = std::max(0.f, cpos.y - 24.f);
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

            float mx = static_cast<float>(ev.button.x);
            float my = content_y(static_cast<float>(ev.button.y));

            // Drag scrollbar (right edge of a pane)?
            {
              int pi = pane_at(mx);
              int sx = panes[pi].x + panes[pi].w - 8;
              if (mx >= sx && my < win_h - 28) {
                focus = pi;
                dragging_scroll = true;
                scroll_drag_pane = pi;
                // Jump scroll so thumb centres under cursor
                float view_h = static_cast<float>(win_h - 28);
                float content_h = std::max(view_h, panes[pi].layout.content_height);
                float max_s = std::max(0.f, content_h - view_h + 40.f);
                float ratio = (view_h > 0.f) ? (my / view_h) : 0.f;
                panes[pi].scroll_y = std::max(0.f, std::min(max_s, ratio * max_s));
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

            // Block drag: grab left margin (16px) of a layout box
            {
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + 16.f) continue;
                const auto& bn = doc_of(p).get(box.node);
                // Only reorder movable siblings (not structural containers)
                if (bn.kind == xanatoo::NodeKind::Document ||
                    bn.kind == xanatoo::NodeKind::Columns ||
                    bn.kind == xanatoo::NodeKind::Column ||
                    bn.kind == xanatoo::NodeKind::List)
                  continue;
                if (bn.parent == xanatoo::INVALID_NODE) continue;
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
                  std::cout << "Opened link in other pane: " << *href << "\n";
                } else {
                  p.sel.focus = *dest;
                  p.sel.anchor = p.sel.focus;
                  scroll_caret_into_view(p);
                  // Dual-pane shared doc: mirror target in the other view
                  if (split && shared_doc) {
                    int other = 1 - focus;
                    panes[other].sel.focus = *dest;
                    panes[other].sel.anchor = *dest;
                    scroll_caret_into_view(panes[other]);
                  }
                  std::cout << "Followed link: " << *href << "\n";
                }
                reset_blink();
                break;
              } else if (!href->empty() && (*href)[0] != '#') {
                std::cout << "Opening external link: " << *href << "\n";
                xanatoo::open_external_url(*href);
                break;
              }
            }

            // Click a transclusion → jump to local source, or open external file
            {
              bool followed = false;
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + box.width) continue;
                const auto& bn = doc_of(p).get(box.node);
                if (bn.kind != xanatoo::NodeKind::Transclude) continue;
                if (auto dest = xanatoo::jump_to_href(doc_of(p), bn.src)) {
                  p.sel.focus = *dest;
                  p.sel.anchor = p.sel.focus;
                  scroll_caret_into_view(p);
                  if (split && shared_doc) {
                    int other = 1 - focus;
                    panes[other].sel.focus = *dest;
                    panes[other].sel.anchor = *dest;
                    scroll_caret_into_view(panes[other]);
                  }
                  reset_blink();
                  std::cout << "Followed transclusion: " << bn.src << "\n";
                  followed = true;
                } else {
                  // External path.md[#anchor] — open the file in a soft window
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
                      open_browse(resolved.string(),
                                  resolved.filename().string() +
                                      " — included source");
                      std::cout << "Opened include source: " << resolved
                                << "\n";
                      followed = true;
                    }
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
          } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            // Document context menu (ImGui popup).
            if (!imgui_mouse) {
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
          mouse_x = static_cast<float>(ev.motion.x);
          mouse_y = static_cast<float>(ev.motion.y);

          // Soft-window drag/resize continue even if ImGui wants the mouse
          // (capture flag can lag one frame after a title-bar grab).
          if (ol_dragging) {
            ol_x = ev.motion.x - ol_drag_dx;
            ol_y = ev.motion.y - ol_drag_dy;
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
                (static_cast<float>(ev.motion.y) - static_cast<float>(cy)) /
                view_h;
            ratio = std::max(0.f, std::min(1.f, ratio));
            ol_scroll = ratio * max_s;
            break;
          }
          if (ol_resize) {
            int dx = ev.motion.x - ol_resize_start_x;
            int dy = ev.motion.y - ol_resize_start_y;
            if (ol_resize & 1) {
              ol_w = ol_resize_start_w + dx;
              if (ol_w < 200) ol_w = 200;
              if (ol_w > win_w - 20) ol_w = win_w - 20;
            }
            if (ol_resize & 2) {
              ol_h = ol_resize_start_h + dy;
              if (ol_h < 140) ol_h = 140;
              if (ol_h > win_h - 40) ol_h = win_h - 40;
            }
            break;
          }
          {
            bool soft_motion = false;
            for (auto& sw : soft_wins) {
              if (sw.dragging) {
                sw.x = ev.motion.x - sw.drag_dx;
                sw.y = ev.motion.y - sw.drag_dy;
                if (sw.x < 0) sw.x = 0;
                if (sw.y < static_cast<int>(ui_top))
                  sw.y = static_cast<int>(ui_top);
                dock_preview =
                    compute_dock_zone(ev.motion.x, ev.motion.y);
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
                    (static_cast<float>(ev.motion.y) -
                     static_cast<float>(cy)) /
                    view_h;
                ratio = std::max(0.f, std::min(1.f, ratio));
                sw.scroll = ratio * max_s;
                soft_motion = true;
                break;
              }
              if (sw.resize) {
                int dx = ev.motion.x - sw.resize_start_x;
                int dy = ev.motion.y - sw.resize_start_y;
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
            help_x = ev.motion.x - help_drag_dx;
            help_y = ev.motion.y - help_drag_dy;
            if (help_x < 0) help_x = 0;
            if (help_y < static_cast<int>(ui_top)) help_y = static_cast<int>(ui_top);
            break;
          }
          if (help_scroll_dragging) {
            auto [cx, cy, cw, ch] = help_client_rect();
            float max_s = std::max(
                0.f, help_layout.content_height - static_cast<float>(ch) + 40.f);
            float view_h = static_cast<float>(std::max(1, ch));
            float ratio = (static_cast<float>(ev.motion.y) - static_cast<float>(cy)) / view_h;
            ratio = std::max(0.f, std::min(1.f, ratio));
            help_scroll = ratio * max_s;
            break;
          }
          if (help_resize) {
            int dx = ev.motion.x - help_resize_start_x;
            int dy = ev.motion.y - help_resize_start_y;
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

          if (dragging_scroll) {
            float my = content_y(static_cast<float>(ev.motion.y));
            Pane& sp = panes[scroll_drag_pane];
            float view_h = static_cast<float>(win_h) - ui_top - 28.f;
            float content_h = std::max(view_h, sp.layout.content_height);
            float max_s = std::max(0.f, content_h - view_h + 40.f);
            float ratio = (view_h > 0.f) ? (my / view_h) : 0.f;
            ratio = std::max(0.f, std::min(1.f, ratio));
            sp.scroll_y = ratio * max_s;
            break;
          }
          if (dragging_split && split) {
            float mx = static_cast<float>(ev.motion.x);
            split_ratio = mx / static_cast<float>(win_w);
            split_ratio = std::max(0.15f, std::min(0.85f, split_ratio));
            needs_layout = true;
            break;
          }
          if (dragging_image && resize_image != xanatoo::INVALID_NODE) {
            float mx = static_cast<float>(ev.motion.x);
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
            float mx = static_cast<float>(ev.motion.x);
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
            float mx = static_cast<float>(ev.motion.x);
            float my = content_y(static_cast<float>(ev.motion.y));
            // Keep focus on the pane where drag started
            Pane& p = F();
            float lx = local_x(mx, focus);
            if (auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              p.sel.focus = *hit;
              p.preferred_x = lx;
            }
          } else {
            // Track link under cursor for status-bar + tooltip hover feedback
            float mx = static_cast<float>(ev.motion.x);
            float my = content_y(static_cast<float>(ev.motion.y));
            mouse_x = mx;
            mouse_y = static_cast<float>(ev.motion.y);
            int hover_pane = pane_at(mx);
            Pane& hp = panes[hover_pane];
            float lx = local_x(mx, hover_pane);
            if (auto hit = xanatoo::hit_test_link_ex(doc_of(hp), hp.layout, lx,
                                                     my, hp.scroll_y)) {
              hovered_href = hit->href;
              hovered_rel = hit->rel;
              hovered_is_cite = (hit->kind == xanatoo::InlineKind::Cite);
              if (auto prev =
                      xanatoo::link_preview_text(doc_of(hp), hit->href)) {
                hovered_preview = *prev;
              } else {
                hovered_preview.clear();
              }
            } else {
              hovered_href.clear();
              hovered_rel.clear();
              hovered_is_cite = false;
              hovered_preview.clear();
            }
          }
          break;

        case SDL_TEXTINPUT: {
          if (imgui_keys) break;
          // Outline is a software window (non-modal): typing goes to the document.
          if (save_as_mode) {
            save_as_path += ev.text.text;
            break;
          }
          if (image_insert_mode) {
            image_insert_path += ev.text.text;
            break;
          }
          if (include_insert_mode) {
            include_insert_src += ev.text.text;
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
          edit();
          Pane& p = F();
          xanatoo::Document& td = doc_of(p);
          if (!p.sel.empty()) {
            xanatoo::delete_selection(td, p.sel);
          }
          xanatoo::insert_text(td, p.sel.focus, ev.text.text);
          p.sel.anchor = p.sel.focus;
          needs_layout = true;
          reset_blink();
          break;
        }

        case SDL_KEYDOWN: {
          if (imgui_keys) break;
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
          Pane& p = F();
          xanatoo::Document& d = doc_of(p);

          // Help window is non-modal: Esc/F1 close it; other keys edit the document.
          if (help_mode &&
              (ev.key.keysym.sym == SDLK_ESCAPE ||
               ev.key.keysym.sym == SDLK_F1)) {
            help_mode = false;
            help_dragging = false;
            break;
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
                           node.kind == xanatoo::NodeKind::ListItem) {
                  node.kind = xanatoo::NodeKind::BlockQuote;
                  node.level = 0;
                }
                needs_layout = true;
                reset_blink();
              } else if (ctrl) {
                if (dirty_flags[0] || dirty_flags[1]) {
                  std::cerr << "Unsaved changes — Ctrl+S to save, "
                               "Ctrl+Q again after saving, or save and quit.\n";
                  // Soft warning once: if already warned, allow quit
                  static bool quit_warned = false;
                  if (!quit_warned) {
                    quit_warned = true;
                    break;
                  }
                }
                running = false;
              }
              break;

            case SDLK_f:
              if (ctrl) {
                search_mode = true;
                replace_mode = false;
                search_query.clear();
                std::cout << "Leap search: type to find, F3 next, Esc cancel\n";
                reset_blink();
              }
              break;

            case SDLK_h:
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
                  if (shared_doc) {
                    panes[1].doc_index = 0;
                  }
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
              if (save_as_mode) {
                if (!save_as_path.empty()) {
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
                if (!path_in_window_buf.empty()) {
                  std::string path = path_in_window_buf;
                  if (!fs::exists(path)) {
                    std::string found = find_repo_file(path.c_str());
                    if (!found.empty()) path = found;
                  }
                  open_browse(path, path);
                }
                path_in_window_mode = false;
                path_in_window_buf.clear();
                break;
              }
              if (save_as_mode) {
                if (!save_as_path.empty()) {
                  std::string text = xanatoo::serialize_document(d, save_as_path);
                  std::ofstream out(save_as_path);
                  if (!out) {
                    std::cerr << "Failed to save: " << save_as_path << "\n";
                  } else {
                    out << text;
                    path_of(p) = save_as_path;
                    dirty_flags[p.doc_index] = false;
                    update_title();
                    std::cout << "Saved as: " << save_as_path << "\n";
                  }
                }
                save_as_mode = false;
                save_as_path.clear();
                break;
              }
              if (image_insert_mode) {
                if (!image_insert_path.empty()) {
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
                  }
                }
                image_insert_mode = false;
                image_insert_as_figure = false;
                image_insert_path.clear();
                break;
              }
              if (include_insert_mode) {
                if (!include_insert_src.empty()) {
                  edit();
                  if (xanatoo::insert_transclude(d, p.sel.focus,
                                                 include_insert_src)) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                    std::cout << "Inserted include: " << include_insert_src
                              << "\n";
                  }
                }
                include_insert_mode = false;
                include_insert_src.clear();
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
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
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
              const float view_h = static_cast<float>(win_h) - ui_top - 28.f;
              const float step = view_h * 0.85f;
              float max_s = std::max(
                  0.f, p.layout.content_height - view_h + 40.f);
              if (up)
                p.scroll_y = std::max(0.f, p.scroll_y - step);
              else
                p.scroll_y = std::min(max_s, p.scroll_y + step);
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
              if (ctrl) {
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
                    edit();
                    if (!p.sel.empty()) xanatoo::delete_selection(d, p.sel);
                    xanatoo::paste_plain_text(d, p.sel.focus, clip);
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  if (clip) SDL_free(clip);
                }
              }
              break;

            case SDLK_z:
              if (ctrl) {
                if (histories[p.doc_index].undo(d, p.sel)) {
                  if (panes[0].doc_index == panes[1].doc_index) {
                    xanatoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_y:
              if (ctrl) {
                if (histories[p.doc_index].redo(d, p.sel)) {
                  if (panes[0].doc_index == panes[1].doc_index) {
                    xanatoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_s:
              if (ctrl && shift) {
                save_as_mode = true;
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
                    dirty_flags[p.doc_index] = false;
                    update_title();
                  }
                }
              }
              break;

            case SDLK_o:
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
              }
              break;

            case SDLK_r:
              if (ctrl) {
                // Reload focused document from disk
                std::string& rpath = path_of(p);
                if (rpath.empty()) {
                  std::cerr << "Reload: buffer has no path (Save As first)\n";
                  break;
                }
                if (dirty_flags[p.doc_index] && !shift) {
                  std::cerr << "Reload: unsaved changes — "
                               "Ctrl+Shift+R to discard and reload\n";
                  break;
                }
                xanatoo::clear_external_doc_cache();
                docs[p.doc_index] = load_doc(rpath);
                dirty_flags[p.doc_index] = false;
                histories[p.doc_index].reset(docs[p.doc_index], p.sel);
                for (int pi = 0; pi < 2; ++pi) {
                  if (panes[pi].doc_index != p.doc_index) continue;
                  xanatoo::ensure_valid_caret(docs[p.doc_index],
                                              panes[pi].sel.focus);
                  panes[pi].sel.anchor = panes[pi].sel.focus;
                  panes[pi].scroll_y = 0.f;
                }
                update_title();
                needs_layout = true;
                reset_blink();
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
                if (shared_doc || paths[1].empty()) {
                  std::cout << "Diff marks need two open files "
                               "(xanatoo a.md b.md).\n";
                } else {
                  diff_mode = !diff_mode;
                  refresh_diff();
                  std::cout << (diff_mode ? "Diff marks on\n" : "Diff marks off\n");
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
                std::cout << (structure_mode ? "Structure highlight on\n"
                                             : "Structure highlight off\n");
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

            case SDLK_b:
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
                // Unwrap enclosing stretch
                edit();
                if (auto first = xanatoo::unwrap_stretch(d, p.sel.focus.node)) {
                  p.sel.focus.node = *first;
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
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
              if (ctrl && !shift) {
                int level = static_cast<int>(ev.key.keysym.sym - SDLK_0);
                edit();
                if (xanatoo::set_heading_level(d, p.sel.focus, level)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_LEFTBRACKET:
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

    if (needs_layout) layout_panes();
    if (outline_mode) refresh_outline();

    float lh = static_cast<float>(body_font.line_height());
    // ImGui menu + toolbar
    {
      xanatoo::UiState ui_state;
      ui_state.dirty = dirty_flags[F().doc_index];
      ui_state.has_path = !path_of(F()).empty();
      ui_state.split = split;
      ui_state.outline = outline_mode;
      ui_state.diff = diff_mode;
      ui_state.structure = structure_mode;
      ui_state.help = help_mode;
      ui_state.selection = !F().sel.empty();
      {
        auto& fd = doc_of(F());
        auto nid = F().sel.focus.node;
        ui_state.on_list_item =
            nid != xanatoo::INVALID_NODE &&
            fd.get(nid).kind == xanatoo::NodeKind::ListItem;
        ui_state.in_stretch =
            nid != xanatoo::INVALID_NODE &&
            xanatoo::enclosing_stretch(fd, nid) != xanatoo::INVALID_NODE;
      }
      ui_state.doc_label = path_of(F()).empty() ? std::string("untitled")
                                                : path_of(F());
      auto ui_frame = ui.begin_frame(ui_state);
      ui_top = ui_frame.top_reserved;

      using C = xanatoo::UiCommand;
      auto& p = F();
      auto& d = doc_of(p);
      switch (ui_frame.cmd) {
        case C::Quit:
          running = false;
          break;
        case C::Save: {
          std::string& spath = path_of(p);
          if (spath.empty()) {
            save_as_mode = true;
            save_as_path = "untitled.md";
          } else {
            std::string text = xanatoo::serialize_document(d, spath);
            std::ofstream out(spath);
            if (out) {
              out << text;
              dirty_flags[p.doc_index] = false;
              update_title();
              std::cout << "Saved: " << spath << "\n";
            }
          }
          break;
        }
        case C::SaveAs:
          save_as_mode = true;
          search_mode = false;
          save_as_path = path_of(p).empty() ? "untitled.md" : path_of(p);
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
          break;
        }
        case C::Reload:
        case C::ForceReload: {
          std::string& rpath = path_of(p);
          if (rpath.empty()) break;
          if (dirty_flags[p.doc_index] && ui_frame.cmd == C::Reload) {
            std::cerr << "Reload: unsaved changes — use Force Reload\n";
            break;
          }
          xanatoo::clear_external_doc_cache();
          docs[p.doc_index] = load_doc(rpath);
          dirty_flags[p.doc_index] = false;
          histories[p.doc_index].reset(docs[p.doc_index], p.sel);
          xanatoo::ensure_valid_caret(docs[p.doc_index], p.sel.focus);
          p.sel.anchor = p.sel.focus;
          p.scroll_y = 0.f;
          update_title();
          needs_layout = true;
          break;
        }
        case C::Undo:
          if (histories[p.doc_index].undo(d, p.sel)) needs_layout = true;
          break;
        case C::Redo:
          if (histories[p.doc_index].redo(d, p.sel)) needs_layout = true;
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
          image_insert_as_figure = false;
          image_insert_path.clear();
          break;
        case C::InsertFigure:
          image_insert_mode = true;
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
          break;
        case C::ToggleDiff:
          diff_mode = !diff_mode;
          refresh_diff();
          break;
        case C::ToggleStructure:
          structure_mode = !structure_mode;
          break;
        case C::ToggleHelp:
          help_mode = !help_mode;
          if (help_mode) {
            ensure_help_doc();
            help_needs_layout = true;
          }
          break;
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
          path_in_window_buf.clear();
          break;
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
        default:
          break;
      }
    }

    renderer.clear(248, 246, 240);

    int pane_count = split ? 2 : 1;
    for (int i = 0; i < pane_count; ++i) {
      Pane& pane = panes[i];
      float view_h_pane = static_cast<float>(win_h) - ui_top - 28.f;
      float max_scroll =
          std::max(0.f, pane.layout.content_height - view_h_pane + 40.f);
      if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

      const int clip_y = static_cast<int>(ui_top);
      SDL_Rect clip{pane.x, clip_y, pane.w, win_h - 28 - clip_y};
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

      // Logical structure overlay: colour bars + wash by NodeKind
      if (structure_mode) {
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        const auto& ddoc = docs[pane.doc_index];
        for (const auto& box : pane.layout.boxes) {
          if (box.node == xanatoo::INVALID_NODE) continue;
          const auto kind = ddoc.get(box.node).kind;
          // Skip pure container shells that span the whole doc
          if (kind == xanatoo::NodeKind::Document) continue;
          uint8_t r = 120, g = 120, b = 130, a_bar = 200, a_wash = 28;
          switch (kind) {
            case xanatoo::NodeKind::Heading:
              r = 40; g = 90; b = 180; a_wash = 36; break;
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
            default:
              break;
          }
          int by = static_cast<int>(box.y - pane.scroll_y + ui_top);
          int bh = static_cast<int>(box.height);
          if (by + bh < clip_y || by > win_h - 28) continue;
          SDL_SetRenderDrawColor(sdl, r, g, b, a_bar);
          SDL_Rect mark{pane.x + 1, by, 5, bh};
          SDL_RenderFillRect(sdl, &mark);
          SDL_SetRenderDrawColor(sdl, r, g, b, a_wash);
          SDL_Rect wash{pane.x + 6, by, pane.w - 14, bh};
          SDL_RenderFillRect(sdl, &wash);
        }
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      }

      if (i == focus) {
        auto cpos = xanatoo::caret_screen_pos(
            pane.layout, pane.sel.focus, draw_scroll, lh);
        if (cpos.valid) pane.preferred_x = cpos.x;
        Uint32 blink_phase = (SDL_GetTicks() - caret_blink_start) % 1000;
        if (blink_phase < 600) {
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
      if (diff_mode && !shared_doc) {
        int di = pane.doc_index;
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
            int by = static_cast<int>(box.y - pane.scroll_y);
            int bh = static_cast<int>(box.height);
            if (by + bh < 0 || by > win_h - 28) continue;
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
          const auto& ddoc = docs[di];
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
            int gy = static_cast<int>(g.y - pane.scroll_y - lh * 0.85f);
            int gw = 8;
            // Approximate advance from next glyph on same baseline
            // (best-effort; highlight remains readable either way)
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
        int gy = static_cast<int>(gap.y - pane.scroll_y);
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
                     static_cast<int>(box.y - pane.scroll_y),
                     static_cast<int>(box.width),
                     static_cast<int>(box.height)};
          SDL_RenderFillRect(sdl, &r);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        if (drop_line_y >= 0.f) {
          int ly = static_cast<int>(drop_line_y - pane.scroll_y);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
          SDL_Rect line{pane.x + 24, ly - 1, pane.w - 40, 3};
          SDL_RenderFillRect(sdl, &line);
        }
      } else if (i == focus && !dragging_block) {
        // Subtle grip dots on left margin of the box under the cursor
        float adj_y = mouse_y + pane.scroll_y;
        float lx = mouse_x - static_cast<float>(pane.x);
        for (const auto& box : pane.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x || lx > box.x + 20.f) continue;
          const auto& bn = doc_of(pane).get(box.node);
          if (bn.kind == xanatoo::NodeKind::Document ||
              bn.kind == xanatoo::NodeKind::Columns ||
              bn.kind == xanatoo::NodeKind::Column ||
              bn.kind == xanatoo::NodeKind::List)
            continue;
          int gx = pane.x + static_cast<int>(box.x) + 4;
          int gy = static_cast<int>(box.y - pane.scroll_y + box.height * 0.5f);
          SDL_SetRenderDrawColor(sdl, 160, 158, 150, 255);
          for (int d = -6; d <= 6; d += 6) {
            SDL_Rect dot{gx, gy + d, 3, 3};
            SDL_RenderFillRect(sdl, &dot);
          }
          break;
        }
      }

      // Scrollbar thumb (in the content band below the toolbar)
      {
        float view_h = static_cast<float>(win_h) - ui_top - 28.f;
        if (view_h < 1.f) view_h = 1.f;
        float content_h = std::max(view_h, pane.layout.content_height);
        float thumb_h = std::max(24.f, view_h * (view_h / content_h));
        float track = std::max(0.f, view_h - thumb_h);
        float ratio = (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
        float thumb_y = ui_top + ratio * track;
        int sx = pane.x + pane.w - 6;
        SDL_SetRenderDrawColor(sdl, 190, 188, 180, 255);
        SDL_Rect track_r{sx, static_cast<int>(ui_top), 5, static_cast<int>(view_h)};
        SDL_RenderFillRect(sdl, &track_r);
        SDL_SetRenderDrawColor(sdl, 120, 118, 110, 255);
        SDL_Rect thumb{sx, static_cast<int>(thumb_y), 5, static_cast<int>(thumb_h)};
        SDL_RenderFillRect(sdl, &thumb);
      }
    }

    SDL_RenderSetClipRect(sdl, nullptr);

    // Divider between panes
    if (split) {
      int mid = panes[1].x - 3;
      SDL_SetRenderDrawColor(sdl, 210, 208, 200, 255);
      SDL_Rect div{mid, static_cast<int>(ui_top), 6,
                 win_h - 28 - static_cast<int>(ui_top)};
      SDL_RenderFillRect(sdl, &div);
    }

    // Visual link between panes when both carets sit on the same node
    // (e.g. after following an internal link in dual-pane shared view).
    if (split && shared_doc &&
        panes[0].sel.focus.node != xanatoo::INVALID_NODE &&
        panes[0].sel.focus.node == panes[1].sel.focus.node) {
      float lh = static_cast<float>(body_font.line_height());
      auto c0 = xanatoo::caret_screen_pos(panes[0].layout, panes[0].sel.focus,
                                           panes[0].scroll_y, lh);
      auto c1 = xanatoo::caret_screen_pos(panes[1].layout, panes[1].sel.focus,
                                           panes[1].scroll_y, lh);
      if (c0.valid && c1.valid) {
        int x0 = panes[0].x + static_cast<int>(c0.x);
        int y0 = static_cast<int>(c0.y);
        int x1 = panes[1].x + static_cast<int>(c1.x);
        int y1 = static_cast<int>(c1.y);
        // Clamp into content area
        int max_y = win_h - 28;
        if (y0 >= 0 && y0 < max_y && y1 >= 0 && y1 < max_y) {
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
      if (search_mode || save_as_mode || path_in_window_mode || link_edit_mode ||
          summary_edit_mode || image_insert_mode || include_insert_mode) {
        SDL_SetRenderDrawColor(sdl, 30, 30, 40, 240);
      } else {
        SDL_SetRenderDrawColor(sdl, 236, 234, 228, 245);
      }
      SDL_Rect bar{0, win_h - bar_h, win_w, bar_h};
      SDL_RenderFillRect(sdl, &bar);
      // Top edge line
      if (search_mode || save_as_mode || path_in_window_mode || link_edit_mode ||
          summary_edit_mode || image_insert_mode || include_insert_mode) {
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
        label += path_in_window_buf.empty() ? "…" : path_in_window_buf;
      } else if (save_as_mode) {
        label = "Save as: ";
        label += save_as_path.empty() ? "…" : save_as_path;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (image_insert_mode) {
        label = image_insert_as_figure ? "Figure image path: " : "Image path: ";
        label += image_insert_path.empty() ? "…" : image_insert_path;
        label += "    Enter insert · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (include_insert_mode) {
        label = "Include: ";
        label += include_insert_src.empty() ? "…" : include_insert_src;
        label += "    (#Heading · file.md · file.md#Heading)  Enter · Esc";
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
      } else if (dragging_block) {
        label = "Moving block — drop before/after any block (columns & lists OK)";
        tr = 40; tg = 90; tb = 160;
      } else if (dragging_col_gap) {
        label = "Resizing columns — drag to adjust relative widths";
        tr = 40; tg = 90; tb = 160;
      } else if (diff_mode) {
        label = "Diff marks on — orange bars mark passages that differ · Ctrl+Shift+D toggles";
        tr = 160; tg = 90; tb = 30;
      } else if (structure_mode) {
        label = "Structure highlight — colour bars by block kind · Ctrl+Shift+L toggles · right-click for menu";
        tr = 40; tg = 80; tb = 140;
      } else if (outline_mode) {
        label = "Outline — ↑/↓ select · Enter jump · 1-9 quick jump · Esc close";
        tr = 40; tg = 90; tb = 160;
      } else if (!hovered_href.empty()) {
        label = hovered_is_cite ? "Cite: " : "Link: ";
        label += hovered_href;
        if (!hovered_rel.empty()) {
          label += "  rel=";
          label += hovered_rel;
        }
        if (hovered_href[0] == '#')
          label += "    click to jump";
        else
          label += "    click to open";
        tr = 40; tg = 90; tb = 160;
      } else {
        label =
            "F1 help · Ctrl+S save · Ctrl+A block · Ctrl+Shift+A all · "
            "Ctrl+F leap · Ctrl+B/I style · drag to select";
        tr = 70; tg = 68; tb = 62;
      }
      float baseline = static_cast<float>(win_h - 8);
      renderer.draw_text(body_font, 10.f, baseline, label, tr, tg, tb);

      // Document / selection stats on the right (idle status only)
      if (!search_mode && !save_as_mode && !link_edit_mode &&
          !summary_edit_mode && !help_mode && !outline_mode &&
          !image_insert_mode) {
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

    // Heading outline software window (Ctrl+Shift+O)
    if (outline_mode) {
      if (ol_w < 200) ol_w = 200;
      if (ol_h < 140) ol_h = 140;
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

      renderer.draw_text(body_font, static_cast<float>(ol_x + 10),
                         static_cast<float>(ol_y + 6 + body_font.ascender()),
                         "Outline", 25, 90, 170);
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
          line += "  ";
        } else {
          line += "   ";
        }
        for (int ind = 1; ind < e.level; ++ind) line += "  ";
        line += e.text;
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
      renderer.draw_text(body_font, static_cast<float>(sw.x + 10),
                         static_cast<float>(sw.y + 6 + body_font.ascender()),
                         sw.title.c_str(), 40, 55, 35);
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

    // Hover preview tooltip for internal #anchor links
    if (!hovered_preview.empty() && !search_mode && !save_as_mode &&
        !link_edit_mode && !summary_edit_mode && !help_mode) {
      const std::string tip = hovered_preview;
      int tw = body_font.measure(tip);
      const int pad_x = 10, pad_y = 6;
      int box_w = tw + pad_x * 2;
      int box_h = body_font.line_height() + pad_y * 2;
      int bx = static_cast<int>(mouse_x) + 12;
      int by = static_cast<int>(mouse_y) - box_h - 8;
      // Clamp to window (above status bar)
      if (bx + box_w > win_w - 4) bx = win_w - box_w - 4;
      if (bx < 4) bx = 4;
      if (by < 4) by = static_cast<int>(mouse_y) + 16;
      if (by + box_h > win_h - 32) by = win_h - 32 - box_h;

      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 30, 40, 55, 230);
      SDL_Rect box{bx, by, box_w, box_h};
      SDL_RenderFillRect(sdl, &box);
      SDL_SetRenderDrawColor(sdl, 90, 140, 210, 255);
      SDL_RenderDrawRect(sdl, &box);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      float tip_baseline =
          static_cast<float>(by + pad_y + body_font.ascender());
      renderer.draw_text(body_font, static_cast<float>(bx + pad_x), tip_baseline,
                         tip, 230, 235, 245);
    }

    ui.end_frame(sdl);
    renderer.present();
  }

  SDL_StopTextInput();
  ui.shutdown();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
