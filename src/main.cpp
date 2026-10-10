// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// glyphtoo — interactive document composition engine

#include "glyphtoo/types.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/layout.hpp"
#include "glyphtoo/render.hpp"
#include "glyphtoo/parse.hpp"
#include "glyphtoo/editor.hpp"
#include "glyphtoo/images.hpp"
#include "glyphtoo/ui.hpp"
#include "glyphtoo/util.hpp"
#include "glyphtoo/cli.hpp"

#include <SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  glyphtoo::CliOptions opt;
  if (!glyphtoo::parse_cli(argc, argv, opt)) {
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_StartTextInput();

  SDL_Window* window = SDL_CreateWindow(
      "glyphtoo",
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
    if (const char* env = std::getenv("GLYPHTOO_FONT")) {
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
  glyphtoo::FontSet fonts;
  if (!fonts.load(sdl, font_path, body_px, heading_px)) {
    std::cerr << "Failed to load fonts via FontConfig. "
                 "Pass a TTF with --font or install DejaVu/Liberation.\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  glyphtoo::Font& body_font = fonts.regular;


  auto load_doc = [&](const std::string& path) -> glyphtoo::Document {
    if (path.empty()) {
      return glyphtoo::parse_document(glyphtoo::fallback_document(), {});
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "Failed to open document: " << path << "\n";
      return glyphtoo::parse_document(glyphtoo::fallback_document(), {});
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    std::cout << "Opened: " << path << "\n";
    return glyphtoo::parse_document(source, path);
  };

  auto parent_dir = [](const std::string& path) -> std::string {
    if (path.empty()) return {};
    std::filesystem::path p(path);
    if (p.has_parent_path()) return p.parent_path().string();
    return {};
  };

  glyphtoo::Document docs[2];
  std::string paths[2];
  paths[0] = opt.file_path;
  paths[1] = opt.file_path2;
  if (paths[0].empty()) {
    paths[0] = glyphtoo::find_default_document();
    if (paths[0].empty())
      std::cerr << "Note: examples/welcome.md not found; starting empty.\n";
  }
  docs[0] = load_doc(paths[0]);
  docs[1] = paths[1].empty() ? docs[0] : load_doc(paths[1]);
  // If only one file, pane 1 shares docs[0] until a second file is used
  bool shared_doc = paths[1].empty();
  bool dirty_flags[2] = {false, false};

  glyphtoo::ImageCache images(sdl);

  auto update_title = [&]() {
    std::string title = "glyphtoo";
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

  glyphtoo::LayoutEngine layout_engine(fonts, &images);
  glyphtoo::Renderer renderer(sdl, fonts, &images);
  glyphtoo::Ui ui;
  if (!ui.init(window, sdl)) {
    std::cerr << "Warning: ImGui UI failed to init; continuing without menus\n";
  }
  float ui_top = 0.f;  // menu + toolbar height (updated each frame)

  struct Pane {
    int doc_index = 0;  // which docs[] entry
    glyphtoo::Selection sel{};
    float scroll_y = 0.f;
    float preferred_x = 0.f;
    glyphtoo::LayoutResult layout{};
    int x = 0;
    int w = 0;
  };

  Pane panes[2];
  panes[0].doc_index = 0;
  panes[1].doc_index = shared_doc ? 0 : 1;
  glyphtoo::ensure_valid_caret(docs[0], panes[0].sel.focus);
  panes[0].sel.anchor = panes[0].sel.focus;
  glyphtoo::ensure_valid_caret(docs[panes[1].doc_index], panes[1].sel.focus);
  panes[1].sel.anchor = panes[1].sel.focus;

  int focus = 0;
  bool split = !paths[1].empty();  // auto-split when two files given
  float split_ratio = 0.5f;        // left pane fraction
  bool dragging_sel = false;
  bool dragging_split = false;
  bool dragging_scroll = false;
  int scroll_drag_pane = 0;
  bool dragging_image = false;
  glyphtoo::NodeId resize_image = glyphtoo::INVALID_NODE;
  float resize_start_x = 0.f;
  float resize_start_scale = 1.f;
  // Block drag-reorder (grab left margin of a layout box)
  bool dragging_block = false;
  glyphtoo::NodeId drag_block_id = glyphtoo::INVALID_NODE;
  glyphtoo::NodeId drop_target_id = glyphtoo::INVALID_NODE;
  bool drop_before = true;
  float drop_line_y = -1.f;  // content y for insertion indicator
  // Column gutter drag (adjust relative display_scale weights)
  bool dragging_col_gap = false;
  glyphtoo::NodeId col_gap_left = glyphtoo::INVALID_NODE;
  glyphtoo::NodeId col_gap_right = glyphtoo::INVALID_NODE;
  float col_gap_start_x = 0.f;
  float col_gap_left_w = 1.f;
  float col_gap_right_w = 1.f;
  Uint32 last_click_ticks = 0;
  float last_click_x = -999.f, last_click_y = -999.f;

  auto doc_of = [&](Pane& p) -> glyphtoo::Document& {
    return docs[p.doc_index];
  };
  auto path_of = [&](Pane& p) -> std::string& {
    return paths[p.doc_index];
  };
  auto base_of = [&](Pane& p) -> std::string {
    return parent_dir(paths[p.doc_index]);
  };

  glyphtoo::History histories[2];
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
  std::string image_insert_path;
  bool link_edit_mode = false;
  std::string link_edit_href;
  glyphtoo::NodeId link_edit_node = glyphtoo::INVALID_NODE;
  size_t link_edit_span = 0;
  bool summary_edit_mode = false;
  std::string summary_edit_text;
  glyphtoo::NodeId summary_edit_node = glyphtoo::INVALID_NODE;
  bool help_mode = false;  // F1 keyboard reference overlay
  bool outline_mode = false;  // Ctrl+Shift+O heading outline
  std::vector<glyphtoo::OutlineEntry> outline;
  int outline_sel = 0;
  // Side-by-side version marks (two independent documents)
  bool diff_mode = false;
  std::vector<glyphtoo::NodeId> diff_marks[2];
  // Hovered link href (shown in status bar when not in a modal mode)
  std::string hovered_href;
  std::string hovered_preview;  // internal #anchor target snippet
  float mouse_x = 0.f, mouse_y = 0.f;

  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto F = [&]() -> Pane& { return panes[focus]; };

  auto refresh_outline = [&]() {
    glyphtoo::collect_outline(doc_of(F()), outline);
    if (outline_sel >= static_cast<int>(outline.size()))
      outline_sel = std::max(0, static_cast<int>(outline.size()) - 1);
  };

  auto refresh_diff = [&]() {
    diff_marks[0].clear();
    diff_marks[1].clear();
    if (!diff_mode || shared_doc || paths[1].empty()) return;
    glyphtoo::compute_diff_marks(docs[0], docs[1], diff_marks[0], diff_marks[1]);
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
    auto cpos = glyphtoo::caret_screen_pos(
        p.layout, p.sel.focus, 0.f,
        static_cast<float>(body_font.line_height()));
    if (!cpos.valid) return;
    float doc_y = cpos.y;
    float margin = 40.f;
    if (doc_y - p.scroll_y < margin) {
      p.scroll_y = std::max(0.f, doc_y - margin);
    } else if (doc_y - p.scroll_y > win_h - margin - 30.f) {
      p.scroll_y = doc_y - (win_h - margin - 30.f);
    }
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

  // Outline panel geometry (updated each frame when open; used for clicks)
  int ol_px = 0, ol_py = 0, ol_pw = 0, ol_ph = 0;
  float ol_step = 20.f;
  int ol_header = 36;

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
            needs_layout = true;
          }
          break;

        case SDL_DROPFILE: {
          // Drag-and-drop image files from the desktop/file manager into a pane.
          char* dropped = ev.drop.file;
          if (!dropped) break;
          std::string fpath(dropped);
          SDL_free(dropped);

          if (!glyphtoo::is_raster_image_path(fpath)) {
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
          float my = static_cast<float>(my_raw) - ui_top;

          // Place caret at drop location when possible; otherwise keep current.
          if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
            p.sel.focus = *hit;
            p.sel.anchor = p.sel.focus;
          }
          glyphtoo::ensure_valid_caret(d, p.sel.focus);
          if (p.sel.focus.node == glyphtoo::INVALID_NODE) {
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
          if (glyphtoo::insert_image(d, p.sel.focus, store, alt)) {
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
          if (imgui_mouse) break;
          int mx = 0, my = 0;
          SDL_GetMouseState(&mx, &my);
          int pi = pane_at(static_cast<float>(mx));
          Pane& p = panes[pi];
          p.scroll_y -= ev.wheel.y * 48.f;
          p.scroll_y = std::max(0.f, p.scroll_y);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
          if (imgui_mouse || static_cast<float>(ev.button.y) < ui_top)
            break;

          if (ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y) - ui_top;

            // Outline panel click → jump to heading
            if (outline_mode && ol_pw > 0) {
              if (mx >= ol_px && mx < ol_px + ol_pw && my >= ol_py &&
                  my < ol_py + ol_ph) {
                int row = static_cast<int>((my - ol_py - ol_header) / ol_step);
                if (row >= 0 && row < static_cast<int>(outline.size())) {
                  outline_sel = row;
                  jump_outline_sel();
                  outline_mode = false;
                }
              } else {
                outline_mode = false;  // click outside closes
              }
              break;
            }

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
                if (bn.kind == glyphtoo::NodeKind::Document ||
                    bn.kind == glyphtoo::NodeKind::Columns ||
                    bn.kind == glyphtoo::NodeKind::Column ||
                    bn.kind == glyphtoo::NodeKind::List)
                  continue;
                if (bn.parent == glyphtoo::INVALID_NODE) continue;
                dragging_block = true;
                drag_block_id = box.node;
                drop_target_id = glyphtoo::INVALID_NODE;
                drop_line_y = -1.f;
                break;
              }
              if (dragging_block) break;
            }

            if (auto sid = glyphtoo::hit_test_stretch(p.layout, lx, my, p.scroll_y)) {
              glyphtoo::NodeId id = *sid;
              if (doc_of(p).get(id).kind == glyphtoo::NodeKind::Stretch) {
                edit();
                doc_of(p).get(id).expanded = !doc_of(p).get(id).expanded;
                needs_layout = true;
                break;
              }
            }

            // Follow links: internal #anchors jump in-document; http(s)/mailto open externally
            if (auto href = glyphtoo::hit_test_link(
                    doc_of(p), p.layout, lx, my, p.scroll_y)) {
              if (auto dest = glyphtoo::jump_to_href(doc_of(p), *href)) {
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
                glyphtoo::open_external_url(*href);
                break;
              }
            }

            // Click a transclusion → jump to its source passage
            {
              bool followed = false;
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + box.width) continue;
                const auto& bn = doc_of(p).get(box.node);
                if (bn.kind != glyphtoo::NodeKind::Transclude) continue;
                if (auto dest = glyphtoo::jump_to_href(doc_of(p), bn.src)) {
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
                }
                break;
              }
              if (followed) break;
            }

            if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              Uint32 now = SDL_GetTicks();
              bool dbl = (now - last_click_ticks) < 400 &&
                         std::abs(mx - last_click_x) < 4.f &&
                         std::abs(my - last_click_y) < 4.f;
              last_click_ticks = now;
              last_click_x = mx;
              last_click_y = my;

              p.sel.focus = *hit;
              if (dbl) {
                // Select word under caret
                glyphtoo::Document& dd = doc_of(p);
                glyphtoo::move_word_left(dd, p.sel.focus);
                p.sel.anchor = p.sel.focus;
                glyphtoo::move_word_right(dd, p.sel.focus);
              } else if (!(SDL_GetModState() & KMOD_SHIFT)) {
                p.sel.anchor = p.sel.focus;
              }
              p.preferred_x = lx;
              dragging_sel = !dbl;
              reset_blink();
            }
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            if (dragging_block && drag_block_id != glyphtoo::INVALID_NODE &&
                drop_target_id != glyphtoo::INVALID_NODE &&
                drop_target_id != drag_block_id) {
              edit();
              if (glyphtoo::relocate_block(doc_of(F()), drag_block_id,
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
            drag_block_id = glyphtoo::INVALID_NODE;
            drop_target_id = glyphtoo::INVALID_NODE;
            drop_line_y = -1.f;
            dragging_col_gap = false;
            col_gap_left = glyphtoo::INVALID_NODE;
            col_gap_right = glyphtoo::INVALID_NODE;
            resize_image = glyphtoo::INVALID_NODE;
          }
          break;

        case SDL_MOUSEMOTION:
          if (imgui_mouse)
            break;

          mouse_x = static_cast<float>(ev.motion.x);
          mouse_y = static_cast<float>(ev.motion.y);
          if (dragging_scroll) {
            float my = static_cast<float>(ev.motion.y);
            Pane& sp = panes[scroll_drag_pane];
            float view_h = static_cast<float>(win_h - 28);
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
          if (dragging_image && resize_image != glyphtoo::INVALID_NODE) {
            float mx = static_cast<float>(ev.motion.x);
            float dx = mx - resize_start_x;
            // 200px drag ≈ 1.0 scale change
            float scale = resize_start_scale + dx / 200.f;
            scale = std::max(0.15f, std::min(3.0f, scale));
            doc_of(F()).get(resize_image).display_scale = scale;
            needs_layout = true;
            break;
          }
          if (dragging_col_gap && col_gap_left != glyphtoo::INVALID_NODE &&
              col_gap_right != glyphtoo::INVALID_NODE) {
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
          if (dragging_block && drag_block_id != glyphtoo::INVALID_NODE) {
            Pane& p = F();
            float adj_y = mouse_y + p.scroll_y;
            drop_target_id = glyphtoo::INVALID_NODE;
            drop_line_y = -1.f;
            for (const auto& box : p.layout.boxes) {
              if (box.node == drag_block_id) continue;
              if (adj_y < box.y || adj_y > box.y + box.height) continue;
              const auto& bn = doc_of(p).get(box.node);
              // Skip structural containers as drop targets
              if (bn.kind == glyphtoo::NodeKind::Document ||
                  bn.kind == glyphtoo::NodeKind::Columns ||
                  bn.kind == glyphtoo::NodeKind::List)
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
            float my = static_cast<float>(ev.motion.y);
            // Keep focus on the pane where drag started
            Pane& p = F();
            float lx = local_x(mx, focus);
            if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              p.sel.focus = *hit;
              p.preferred_x = lx;
            }
          } else {
            // Track link under cursor for status-bar + tooltip hover feedback
            float mx = static_cast<float>(ev.motion.x);
            float my = static_cast<float>(ev.motion.y);
            mouse_x = mx;
            mouse_y = my;
            int hover_pane = pane_at(mx);
            Pane& hp = panes[hover_pane];
            float lx = local_x(mx, hover_pane);
            if (auto href = glyphtoo::hit_test_link(doc_of(hp), hp.layout, lx, my,
                                                    hp.scroll_y)) {
              hovered_href = *href;
              if (auto prev = glyphtoo::link_preview_text(doc_of(hp), *href)) {
                hovered_preview = *prev;
              } else {
                hovered_preview.clear();
              }
            } else {
              hovered_href.clear();
              hovered_preview.clear();
            }
          }
          break;

        case SDL_TEXTINPUT: {
          if (imgui_keys) break;
          if (help_mode || outline_mode) break;
          if (save_as_mode) {
            save_as_path += ev.text.text;
            break;
          }
          if (image_insert_mode) {
            image_insert_path += ev.text.text;
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
            glyphtoo::Caret from = p.sel.focus;
            if (from.char_index > 0) from.char_index -= 1;
            if (auto hit = glyphtoo::find_next(doc_of(p), search_query, from, mlen)) {
              p.sel.anchor = *hit;
              p.sel.focus = *hit;
              glyphtoo::advance_caret(doc_of(p), p.sel.focus, mlen);
              scroll_caret_into_view(p);
            }
            reset_blink();
            break;
          }
          edit();
          Pane& p = F();
          glyphtoo::Document& td = doc_of(p);
          if (!p.sel.empty()) {
            glyphtoo::delete_selection(td, p.sel);
          }
          glyphtoo::insert_text(td, p.sel.focus, ev.text.text);
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
          glyphtoo::Document& d = doc_of(p);

          // Help overlay: only F1 (toggle) and Esc (close)
          if (help_mode) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                ev.key.keysym.sym == SDLK_F1) {
              help_mode = false;
            }
            break;
          }

          // Outline: navigate headings, Enter/click jumps, Esc closes
          if (outline_mode) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                (ev.key.keysym.sym == SDLK_o && ctrl && shift)) {
              outline_mode = false;
              break;
            }
            switch (ev.key.keysym.sym) {
              case SDLK_UP:
                if (outline_sel > 0) --outline_sel;
                break;
              case SDLK_DOWN:
                if (outline_sel + 1 < static_cast<int>(outline.size()))
                  ++outline_sel;
                break;
              case SDLK_HOME:
                outline_sel = 0;
                break;
              case SDLK_END:
                if (!outline.empty())
                  outline_sel = static_cast<int>(outline.size()) - 1;
                break;
              case SDLK_RETURN:
              case SDLK_KP_ENTER:
                jump_outline_sel();
                outline_mode = false;
                break;
              default:
                if (ev.key.keysym.sym >= SDLK_1 && ev.key.keysym.sym <= SDLK_9) {
                  int idx = static_cast<int>(ev.key.keysym.sym - SDLK_1);
                  if (idx < static_cast<int>(outline.size())) {
                    outline_sel = idx;
                    jump_outline_sel();
                    outline_mode = false;
                  }
                }
                break;
            }
            break;
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
                image_insert_path.clear();
              } else if (link_edit_mode) {
                link_edit_mode = false;
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
                if (glyphtoo::insert_sibling_after(
                        d, p.sel.focus, glyphtoo::NodeKind::HorizontalRule)) {
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
                if (node.kind == glyphtoo::NodeKind::BlockQuote) {
                  node.kind = glyphtoo::NodeKind::Paragraph;
                } else if (node.kind == glyphtoo::NodeKind::Paragraph ||
                           node.kind == glyphtoo::NodeKind::Heading ||
                           node.kind == glyphtoo::NodeKind::ListItem) {
                  node.kind = glyphtoo::NodeKind::BlockQuote;
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
                        ? glyphtoo::find_prev(doc_of(pp), search_query,
                                             pp.sel.focus, mlen)
                        : glyphtoo::find_next(doc_of(pp), search_query,
                                             pp.sel.focus, mlen);
                if (hit) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  glyphtoo::advance_caret(doc_of(pp), pp.sel.focus, mlen);
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
              // Tab / Shift+Tab: nest or promote list items
              {
                auto& d = doc_of(F());
                auto& p = F();
                if (p.sel.focus.node != glyphtoo::INVALID_NODE &&
                    d.get(p.sel.focus.node).kind ==
                        glyphtoo::NodeKind::ListItem) {
                  edit();
                  bool ok = shift ? glyphtoo::outdent_list_item(d, p.sel.focus)
                                  : glyphtoo::indent_list_item(d, p.sel.focus);
                  if (ok) {
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
                glyphtoo::delete_selection(d, p.sel);
              } else {
                glyphtoo::delete_backward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_DELETE:
              edit();
              if (!p.sel.empty()) {
                glyphtoo::delete_selection(d, p.sel);
              } else {
                glyphtoo::delete_forward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (replace_mode && search_mode && !search_query.empty()) {
                Pane& pp = F();
                glyphtoo::Document& dd = doc_of(pp);
                if (!pp.sel.empty()) {
                  edit();
                  glyphtoo::delete_selection(dd, pp.sel);
                  if (!replace_text.empty()) {
                    glyphtoo::insert_text(dd, pp.sel.focus, replace_text);
                    pp.sel.anchor = pp.sel.focus;
                  }
                  needs_layout = true;
                }
                // Find next
                size_t mlen = 0;
                if (auto hit = glyphtoo::find_next(dd, search_query, pp.sel.focus, mlen)) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  glyphtoo::advance_caret(dd, pp.sel.focus, mlen);
                  scroll_caret_into_view(pp);
                }
                reset_blink();
                break;
              }
              if (link_edit_mode) {
                if (link_edit_node != glyphtoo::INVALID_NODE) {
                  auto& node = d.get(link_edit_node);
                  if (link_edit_span < node.spans.size()) {
                    node.spans[link_edit_span].href = link_edit_href;
                    node.spans[link_edit_span].kind = glyphtoo::InlineKind::Link;
                    mark_dirty();
                    needs_layout = true;
                    std::cout << "Link href: " << link_edit_href << "\n";
                  }
                }
                link_edit_mode = false;
                link_edit_href.clear();
                break;
              }
              if (summary_edit_mode) {
                if (summary_edit_node != glyphtoo::INVALID_NODE) {
                  d.get(summary_edit_node).summary = summary_edit_text;
                  mark_dirty();
                  needs_layout = true;
                }
                summary_edit_mode = false;
                summary_edit_text.clear();
                break;
              }
              if (save_as_mode) {
                if (!save_as_path.empty()) {
                  std::string text = glyphtoo::serialize_document(d, save_as_path);
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
                  if (glyphtoo::insert_image(d, p.sel.focus, image_insert_path)) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                    std::cout << "Inserted image: " << image_insert_path << "\n";
                  }
                }
                image_insert_mode = false;
                image_insert_path.clear();
                break;
              }
              // Ctrl+Enter: new empty paragraph after current block
              if (ctrl) {
                edit();
                if (glyphtoo::insert_sibling_after(
                        d, p.sel.focus, glyphtoo::NodeKind::Paragraph)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
                break;
              }
              edit();
              if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
              glyphtoo::split_block(d, p.sel.focus);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_LEFT:
              move_and_sel([&] {
                if (ctrl) {
                  glyphtoo::move_word_left(d, p.sel.focus);
                } else if (!glyphtoo::move_left(d, p.sel.focus)) {
                  glyphtoo::move_to_prev_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_RIGHT:
              move_and_sel([&] {
                if (ctrl) {
                  glyphtoo::move_word_right(d, p.sel.focus);
                } else if (!glyphtoo::move_right(d, p.sel.focus)) {
                  glyphtoo::move_to_next_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_UP:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (glyphtoo::move_block_up(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!glyphtoo::move_up(p.layout, p.sel.focus, p.preferred_x))
                    glyphtoo::move_to_prev_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_DOWN:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (glyphtoo::move_block_down(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!glyphtoo::move_down(p.layout, p.sel.focus, p.preferred_x))
                    glyphtoo::move_to_next_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN: {
              const bool up = (ev.key.keysym.sym == SDLK_PAGEUP);
              const float view_h = static_cast<float>(win_h - 28);
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
                glyphtoo::Caret best_c = p.sel.focus;
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
                  glyphtoo::move_to_document_start(d, p.sel.focus);
                } else {
                  // Line start via layout: find leftmost glyph on same baseline
                  auto cpos = glyphtoo::caret_screen_pos(
                      p.layout, p.sel.focus, p.scroll_y,
                      static_cast<float>(body_font.line_height()));
                  if (cpos.valid) {
                    float best_x = 1e9f;
                    glyphtoo::Caret best = p.sel.focus;
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
                  glyphtoo::move_to_document_end(d, p.sel.focus);
                } else {
                  auto cpos = glyphtoo::caret_screen_pos(
                      p.layout, p.sel.focus, p.scroll_y,
                      static_cast<float>(body_font.line_height()));
                  if (cpos.valid) {
                    float best_x = -1e9f;
                    glyphtoo::Caret best = p.sel.focus;
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
              if (ctrl) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                if (!text.empty()) SDL_SetClipboardText(text.c_str());
              }
              break;

            case SDLK_x:
              if (ctrl) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                if (!text.empty()) {
                  SDL_SetClipboardText(text.c_str());
                  edit();
                  glyphtoo::delete_selection(d, p.sel);
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
                    if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
                    glyphtoo::paste_plain_text(d, p.sel.focus, clip);
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
                    glyphtoo::ensure_valid_caret(
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
                    glyphtoo::ensure_valid_caret(
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
                  std::string text = glyphtoo::serialize_document(d, spath);
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
                  help_mode = false;
                  refresh_outline();
                  outline_sel = 0;
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
                glyphtoo::clear_external_doc_cache();
                docs[p.doc_index] = load_doc(rpath);
                dirty_flags[p.doc_index] = false;
                histories[p.doc_index].reset(docs[p.doc_index], p.sel);
                for (int pi = 0; pi < 2; ++pi) {
                  if (panes[pi].doc_index != p.doc_index) continue;
                  glyphtoo::ensure_valid_caret(docs[p.doc_index],
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
                               "(glyphtoo a.md b.md).\n";
                } else {
                  diff_mode = !diff_mode;
                  refresh_diff();
                  std::cout << (diff_mode ? "Diff marks on\n" : "Diff marks off\n");
                }
                break;
              }
              if (ctrl) {
                edit();
                if (auto nid = glyphtoo::duplicate_block(d, p.sel.focus.node)) {
                  p.sel.focus.node = *nid;
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_8:
              // Ctrl+Shift+8 — toggle unordered list (common editor binding)
              if (ctrl && shift) {
                edit();
                if (glyphtoo::toggle_list(d, p.sel.focus, false)) {
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
                if (glyphtoo::toggle_list(d, p.sel.focus, true)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_b:
              if (ctrl && !p.sel.empty()) {
                edit();
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Strong);
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
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Emphasis);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_BACKQUOTE:
              if (ctrl && !p.sel.empty()) {
                edit();
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Code);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_k:
              if (ctrl && !p.sel.empty()) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                std::string href = "#" + text;
                edit();
                glyphtoo::apply_style_to_selection(
                    d, p.sel, glyphtoo::InlineKind::Link, href);
                link_edit_mode = true;
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
                glyphtoo::NodeId sid = glyphtoo::enclosing_stretch(d, p.sel.focus.node);
                if (sid != glyphtoo::INVALID_NODE) {
                  summary_edit_mode = true;
                  summary_edit_node = sid;
                  summary_edit_text = d.get(sid).summary;
                  search_mode = false;
                  save_as_mode = false;
                  link_edit_mode = false;
                }
              } else if (ctrl) {
                edit();
                std::string summary = glyphtoo::selected_text(d, p.sel);
                if (glyphtoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_u:
              if (ctrl && shift) {
                // Unwrap enclosing stretch
                edit();
                if (auto first = glyphtoo::unwrap_stretch(d, p.sel.focus.node)) {
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
                auto& node = d.get(p.sel.focus.node);
                if (level == 0) {
                  if (node.kind == glyphtoo::NodeKind::Heading ||
                      node.kind == glyphtoo::NodeKind::BlockQuote)
                    node.kind = glyphtoo::NodeKind::Paragraph;
                  node.level = 0;
                } else {
                  node.kind = glyphtoo::NodeKind::Heading;
                  node.level = level;
                }
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_LEFTBRACKET:
              if (ctrl && shift) {
                // Collapse all stretch nodes
                edit();
                for (auto& node : d.nodes) {
                  if (node.kind == glyphtoo::NodeKind::Stretch)
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
                  if (node.kind == glyphtoo::NodeKind::Stretch)
                    node.expanded = true;
                }
                needs_layout = true;
              }
              break;

            case SDLK_a:
              if (ctrl && shift) {
                // Select entire document (first text node → last)
                // Walk layout glyphs for bounds
                if (!p.layout.glyphs.empty()) {
                  const auto& first = p.layout.glyphs.front();
                  const auto& last = p.layout.glyphs.back();
                  p.sel.anchor = {first.node, first.span_index, first.char_index};
                  p.sel.focus = {last.node, last.span_index, last.char_index + 1};
                }
              } else if (ctrl) {
                const auto& n = d.get(p.sel.focus.node);
                p.sel.anchor = {p.sel.focus.node, 0, 0};
                p.sel.focus.span_index = n.spans.empty() ? 0 : n.spans.size() - 1;
                if (!n.spans.empty()) {
                  size_t len = 0;
                  const std::string& ts = n.spans.back().text;
                  for (size_t i = 0; i < ts.size(); ) {
                    unsigned char c = static_cast<unsigned char>(ts[i]);
                    if (c < 0x80) { ++i; ++len; }
                    else if ((c & 0xe0) == 0xc0) { i += 2; ++len; }
                    else if ((c & 0xf0) == 0xe0) { i += 3; ++len; }
                    else if ((c & 0xf8) == 0xf0) { i += 4; ++len; }
                    else ++i;
                  }
                  p.sel.focus.char_index = len;
                }
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

    float lh = static_cast<float>(body_font.line_height());
    // ImGui menu + toolbar
    {
      glyphtoo::UiState ui_state;
      ui_state.dirty = dirty_flags[F().doc_index];
      ui_state.has_path = !path_of(F()).empty();
      ui_state.split = split;
      ui_state.outline = outline_mode;
      ui_state.diff = diff_mode;
      ui_state.help = help_mode;
      ui_state.selection = !F().sel.empty();
      ui_state.doc_label = path_of(F()).empty() ? std::string("untitled")
                                                : path_of(F());
      auto ui_frame = ui.begin_frame(ui_state);
      ui_top = ui_frame.top_reserved;

      using C = glyphtoo::UiCommand;
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
            std::string text = glyphtoo::serialize_document(d, spath);
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
          glyphtoo::clear_external_doc_cache();
          docs[p.doc_index] = load_doc(rpath);
          dirty_flags[p.doc_index] = false;
          histories[p.doc_index].reset(docs[p.doc_index], p.sel);
          glyphtoo::ensure_valid_caret(docs[p.doc_index], p.sel.focus);
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
          std::string text = glyphtoo::selected_text(d, p.sel);
          if (!text.empty()) {
            SDL_SetClipboardText(text.c_str());
            edit();
            glyphtoo::delete_selection(d, p.sel);
            needs_layout = true;
          }
          break;
        }
        case C::Copy: {
          std::string text = glyphtoo::selected_text(d, p.sel);
          if (!text.empty()) SDL_SetClipboardText(text.c_str());
          break;
        }
        case C::Paste:
          if (SDL_HasClipboardText()) {
            char* clip = SDL_GetClipboardText();
            if (clip && clip[0]) {
              edit();
              if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
              glyphtoo::paste_plain_text(d, p.sel.focus, clip);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
            }
            if (clip) SDL_free(clip);
          }
          break;
        case C::SelectAll:
          glyphtoo::move_to_document_start(d, p.sel.anchor);
          glyphtoo::move_to_document_end(d, p.sel.focus);
          break;
        case C::Find:
          search_mode = true;
          replace_mode = false;
          break;
        case C::Replace:
          search_mode = true;
          replace_mode = true;
          break;
        case C::Bold:
          if (!p.sel.empty()) {
            edit();
            glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Strong);
            needs_layout = true;
          }
          break;
        case C::Italic:
          if (!p.sel.empty()) {
            edit();
            glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Emphasis);
            needs_layout = true;
          }
          break;
        case C::Code:
          if (!p.sel.empty()) {
            edit();
            glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Code);
            needs_layout = true;
          }
          break;
        case C::Link:
          if (!p.sel.empty()) {
            std::string text = glyphtoo::selected_text(d, p.sel);
            edit();
            glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Link,
                                              "#" + text);
            link_edit_mode = true;
            link_edit_node = p.sel.focus.node;
            link_edit_span = p.sel.focus.span_index;
            link_edit_href = "#" + text;
            needs_layout = true;
          }
          break;
        case C::InsertParagraph:
          edit();
          if (glyphtoo::insert_sibling_after(d, p.sel.focus,
                                             glyphtoo::NodeKind::Paragraph)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertHr:
          edit();
          if (glyphtoo::insert_sibling_after(d, p.sel.focus,
                                             glyphtoo::NodeKind::HorizontalRule)) {
            p.sel.anchor = p.sel.focus;
            needs_layout = true;
          }
          break;
        case C::InsertImage:
          image_insert_mode = true;
          image_insert_path.clear();
          break;
        case C::ToggleUnorderedList:
          edit();
          if (glyphtoo::toggle_list(d, p.sel.focus, false)) needs_layout = true;
          break;
        case C::ToggleOrderedList:
          edit();
          if (glyphtoo::toggle_list(d, p.sel.focus, true)) needs_layout = true;
          break;
        case C::ToggleBlockQuote: {
          edit();
          auto& node = d.get(p.sel.focus.node);
          if (node.kind == glyphtoo::NodeKind::BlockQuote) {
            node.kind = glyphtoo::NodeKind::Paragraph;
          } else if (node.kind == glyphtoo::NodeKind::Paragraph ||
                     node.kind == glyphtoo::NodeKind::Heading ||
                     node.kind == glyphtoo::NodeKind::ListItem) {
            node.kind = glyphtoo::NodeKind::BlockQuote;
            node.level = 0;
          }
          needs_layout = true;
          break;
        }
        case C::ToggleSplit:
          split = !split;
          needs_layout = true;
          break;
        case C::ToggleOutline:
          outline_mode = !outline_mode;
          if (outline_mode) {
            help_mode = false;
            refresh_outline();
            outline_sel = 0;
          }
          break;
        case C::ToggleDiff:
          diff_mode = !diff_mode;
          refresh_diff();
          break;
        case C::ToggleHelp:
          help_mode = !help_mode;
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
      float max_scroll = std::max(0.f, pane.layout.content_height - win_h + 40.f + 28.f);
      if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

      const int clip_y = static_cast<int>(ui_top);
      SDL_Rect clip{pane.x, clip_y, pane.w, win_h - 28 - clip_y};
      SDL_RenderSetClipRect(sdl, &clip);

      float ox = static_cast<float>(pane.x);
      const float draw_scroll = pane.scroll_y - ui_top;

      if (!pane.sel.empty()) {
        auto rects = glyphtoo::selection_rects(
            pane.layout, pane.sel, draw_scroll, lh);
        // Leap-search match uses amber so it is distinct from a user selection.
        if (search_mode && i == focus)
          renderer.draw_selection(rects, ox, 230, 190, 70, 150);
        else
          renderer.draw_selection(rects, ox);
      }

      renderer.draw(pane.layout, draw_scroll, ox);

      if (i == focus) {
        auto cpos = glyphtoo::caret_screen_pos(
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
            for (glyphtoo::NodeId id : marks) {
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
          if (bn.kind == glyphtoo::NodeKind::Document ||
              bn.kind == glyphtoo::NodeKind::Columns ||
              bn.kind == glyphtoo::NodeKind::Column ||
              bn.kind == glyphtoo::NodeKind::List)
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

      // Scrollbar thumb
      {
        float view_h = static_cast<float>(win_h - 28);
        float content_h = std::max(view_h, pane.layout.content_height);
        float thumb_h = std::max(24.f, view_h * (view_h / content_h));
        float track = view_h - thumb_h;
        float ratio = (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
        float thumb_y = ratio * track;
        int sx = pane.x + pane.w - 6;
        SDL_SetRenderDrawColor(sdl, 190, 188, 180, 255);
        SDL_Rect track_r{sx, 0, 5, static_cast<int>(view_h)};
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
      SDL_Rect div{mid, 0, 6, win_h - 28};
      SDL_RenderFillRect(sdl, &div);
    }

    // Visual link between panes when both carets sit on the same node
    // (e.g. after following an internal link in dual-pane shared view).
    if (split && shared_doc &&
        panes[0].sel.focus.node != glyphtoo::INVALID_NODE &&
        panes[0].sel.focus.node == panes[1].sel.focus.node) {
      float lh = static_cast<float>(body_font.line_height());
      auto c0 = glyphtoo::caret_screen_pos(panes[0].layout, panes[0].sel.focus,
                                           panes[0].scroll_y, lh);
      auto c1 = glyphtoo::caret_screen_pos(panes[1].layout, panes[1].sel.focus,
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
      if (search_mode || save_as_mode || link_edit_mode || summary_edit_mode) {
        SDL_SetRenderDrawColor(sdl, 30, 30, 40, 240);
      } else {
        SDL_SetRenderDrawColor(sdl, 236, 234, 228, 245);
      }
      SDL_Rect bar{0, win_h - bar_h, win_w, bar_h};
      SDL_RenderFillRect(sdl, &bar);
      // Top edge line
      if (search_mode || save_as_mode || link_edit_mode || summary_edit_mode) {
        SDL_SetRenderDrawColor(sdl, 80, 160, 255, 255);
      } else {
        SDL_SetRenderDrawColor(sdl, 200, 198, 190, 255);
      }
      SDL_RenderDrawLine(sdl, 0, win_h - bar_h, win_w, win_h - bar_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      std::string label;
      uint8_t tr = 40, tg = 40, tb = 45;
      if (save_as_mode) {
        label = "Save as: ";
        label += save_as_path.empty() ? "…" : save_as_path;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (image_insert_mode) {
        label = "Image path: ";
        label += image_insert_path.empty() ? "…" : image_insert_path;
        label += "    Enter insert · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (link_edit_mode) {
        label = "Link href: ";
        label += link_edit_href.empty() ? "…" : link_edit_href;
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
        label += " Tab switch · Enter replace · Esc cancel";
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
      } else if (outline_mode) {
        label = "Outline — ↑/↓ select · Enter jump · 1-9 quick jump · Esc close";
        tr = 40; tg = 90; tb = 160;
      } else if (!hovered_href.empty()) {
        label = "Link: ";
        label += hovered_href;
        if (hovered_href[0] == '#')
          label += "    click to jump";
        else
          label += "    click to open";
        tr = 40; tg = 90; tb = 160;
      } else {
        label =
            "F1 help · Ctrl+S save · Ctrl+Z/Y undo · Ctrl+Q quit · "
            "Ctrl++/- zoom · Ctrl+B/I/` style · Ctrl+F leap · Ctrl+H replace";
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
          std::string t = glyphtoo::selected_text(doc_of(F()), fsel);
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
          auto st = glyphtoo::document_stats(doc_of(F()));
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

    // Heading outline overlay (Ctrl+Shift+O)
    if (outline_mode) {
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 20, 22, 28, 180);
      SDL_Rect dim{0, 0, win_w, win_h};
      SDL_RenderFillRect(sdl, &dim);

      ol_pw = std::min(480, win_w - 48);
      ol_ph = std::min(win_h - 48, std::max(120, static_cast<int>(outline.size()) * 22 + 56));
      ol_px = (win_w - ol_pw) / 2;
      ol_py = (win_h - ol_ph) / 2;
      ol_header = 40;
      ol_step = static_cast<float>(
          std::max(16, body_font.line_height() + 4));

      SDL_SetRenderDrawColor(sdl, 248, 246, 240, 250);
      SDL_Rect panel{ol_px, ol_py, ol_pw, ol_ph};
      SDL_RenderFillRect(sdl, &panel);
      SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
      SDL_RenderDrawRect(sdl, &panel);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      renderer.draw_text(body_font, static_cast<float>(ol_px + 16),
                         static_cast<float>(ol_py + 12 + body_font.ascender()),
                         "Outline — Enter jump · Esc close", 25, 90, 170);

      float ty = static_cast<float>(ol_py + ol_header + body_font.ascender());
      for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
        if (ty > ol_py + ol_ph - 8) break;
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
          SDL_Rect hi{ol_px + 4,
                      static_cast<int>(ty - body_font.ascender() - 2),
                      ol_pw - 8, static_cast<int>(ol_step)};
          SDL_RenderFillRect(sdl, &hi);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
          renderer.draw_text(body_font, static_cast<float>(ol_px + 16), ty, line,
                             25, 90, 170);
        } else {
          renderer.draw_text(body_font, static_cast<float>(ol_px + 16), ty, line,
                             50, 48, 42);
        }
        ty += ol_step;
      }
      if (outline.empty()) {
        renderer.draw_text(body_font, static_cast<float>(ol_px + 16), ty,
                           "(no headings in this document)", 120, 118, 110);
      }
    }

    // F1 help overlay
    if (help_mode) {
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 20, 22, 28, 210);
      SDL_Rect dim{0, 0, win_w, win_h};
      SDL_RenderFillRect(sdl, &dim);

      const int margin = 28;
      const int panel_w = std::min(720, win_w - margin * 2);
      const int panel_h = std::min(win_h - margin * 2 - 10, win_h - 40);
      const int px = (win_w - panel_w) / 2;
      const int py = (win_h - panel_h) / 2;
      SDL_SetRenderDrawColor(sdl, 248, 246, 240, 250);
      SDL_Rect panel{px, py, panel_w, panel_h};
      SDL_RenderFillRect(sdl, &panel);
      SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
      SDL_RenderDrawRect(sdl, &panel);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      static const char* lines[] = {
          "glyphtoo — keyboard reference",
          "",
          "Files",
          "  Ctrl+S              Save",
          "  Ctrl+Shift+S        Save As",
          "  Ctrl+R / Shift+R    Reload / force reload",
          "  Ctrl+Q              Quit",
          "",
          "Edit",
          "  Enter               Split paragraph / list item",
          "  Ctrl+Enter          New paragraph after block",
          "  Ctrl+Shift+-        Insert horizontal rule",
          "  Ctrl+Shift+I        Insert image (path prompt)",
          "  Drag image file     Drop PNG/JPEG/GIF/BMP into a pane",
          "  Backspace / Delete  Delete (merge at block start)",
          "  Ctrl+Z / Ctrl+Y     Undo / Redo",
          "  Ctrl+C / X / V      Copy / Cut / Paste (multi-para)",
          "  Ctrl+B / I / ` / K  Bold / Italic / Code / Link",
          "  Ctrl+D              Duplicate block",
          "  Alt+Up / Alt+Down   Move block among siblings",
          "  Ctrl+E              Wrap in stretchtext",
          "  Ctrl+Shift+E        Edit stretch summary",
          "  Ctrl+Shift+U        Unwrap stretchtext",
          "  Ctrl+0 … Ctrl+6     Paragraph / heading level",
          "  Ctrl+Shift+Q        Toggle block quote",
          "  Ctrl+Shift+8 / 7    Toggle unordered / ordered list",
          "  Tab / Shift+Tab     Indent / outdent list item",
          "  Ctrl+Shift+[ / ]    Collapse / expand all stretch",
          "",
          "Navigate",
          "  Arrows / Home/End   Move caret",
          "  Ctrl+Home / End     Document start / end",
          "  Page Up / Down      Scroll + move caret",
          "  Ctrl+Left/Right     Word motion",
          "  Ctrl+A / Shift+A    Select block / whole document",
          "  Ctrl+F / F3         Leap search / next match",
          "  Shift+F3            Previous match",
          "  Ctrl+H              Leap replace",
          "  Ctrl+Shift+O        Heading outline",
          "  Click link          Follow #anchor or open URL",
          "  Ctrl+Click link     Open #anchor in other pane",
          "  Click transclusion  Jump to source passage",
          "",
          "View & layout",
          "  Ctrl+\\              Toggle dual pane",
          "  Tab (not on list)   Focus other pane",
          "  Dual-pane link      Syncs both views + connector",
          "  Ctrl+Shift+D        Diff marks (two open files)",
          "  Drag left margin    Reorder / reparent block",
          "  Drag column gutter  Resize column widths",
          "  Drag image corner   Resize image",
          "  Ctrl++ / Ctrl+-     Zoom text",
          "  Click ▸/▾           Expand / collapse stretchtext",
          "",
          "F1 or Esc — close this help",
      };
      const int nlines = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
      float line_h = static_cast<float>(body_font.line_height());
      // Fit text: if too tall, use a slightly tighter step
      float step = line_h;
      if (nlines * step > panel_h - 24.f)
        step = std::max(12.f, (panel_h - 24.f) / static_cast<float>(nlines));

      float ty = static_cast<float>(py + 14 + body_font.ascender());
      for (int i = 0; i < nlines; ++i) {
        const char* s = lines[i];
        bool heading = (s[0] != '\0' && s[0] != ' ');
        bool title = (i == 0);
        if (title)
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             25, 90, 170);
        else if (heading && s[0] != '\0')
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             40, 50, 70);
        else
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             50, 48, 42);
        ty += step;
        if (ty > py + panel_h - 8) break;
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
