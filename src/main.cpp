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

#include <SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

static const char* SAMPLE = R"(# The network

The web connects documents, but [hypertext](#hypertext) connects ideas.

:::stretch summary="What is hypertext?"
Hypertext is a system of linked information that can be explored
non-linearly. Passages can expand in place (*stretchtext*), images are
first-class addressable content, and layout is part of the living
document rather than a fixed page prescription.
:::

:::columns
:::column
## Original

A passage from the original document, shown in a column.
:::
:::column
## Commentary

An editable annotation linked in spirit to the original passage.
:::
:::

## Living documents

This is a live document. Click to place the caret, type to insert text,
Backspace to delete. **Enter** splits a paragraph. Shift+arrows select.
Ctrl+C / Ctrl+V / Ctrl+X for clipboard, Ctrl+Z / Ctrl+Y for undo/redo.
**Ctrl+\\** toggles a second view pane; **Tab** switches focus.
Drag the gutter to resize. Open two files for independent documents.
Click a stretchtext marker (▸ / ▾) to expand or collapse.

Inspiration: Ted Nelson's *Xanadu*, Bill Atkinson's **HyperCard**, and
Jef Raskin's *Canon Cat* — a keyboard-driven work processor that treated
text as a continuous, searchable stream.

Images referenced as `![alt](path.png)` load relative to the document
directory (PNG, JPEG, BMP, GIF via stb_image).
)";

static std::string find_system_font() {
  const char* candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/Library/Fonts/Arial.ttf",
      nullptr
  };
  for (const char** p = candidates; *p; ++p) {
    if (fs::exists(*p)) return *p;
  }
  return {};
}

static void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options] [file] [file2]\n"
      << "\n"
      << "Options:\n"
      << "  -f, --font <path>     TTF/OTF font file (body + headings)\n"
      << "  -o, --open <path>     Open a markup document\n"
      << "  -h, --help            Show this help\n"
      << "\n"
      << "One positional path opens that document.\n"
      << "Two paths open dual-pane: left=file1, right=file2.\n"
      << "Ctrl+S saves the focused pane's document.\n"
      << "Ctrl+\\ toggles split; drag the gutter to resize panes.\n"
      << "\n"
      << "Font resolution order: --font, GLYPHTOO_FONT env, system search.\n";
}

struct Options {
  std::string font_path;
  std::string file_path;   // primary document
  std::string file_path2;  // optional second document → opens dual-pane
};

static bool parse_args(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      print_usage(argv[0]);
      return false;
    }
    if (a == "-f" || a == "--font") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.font_path = argv[++i];
      continue;
    }
    if (a.rfind("--font=", 0) == 0) {
      opt.font_path = a.substr(7);
      continue;
    }
    if (a == "-o" || a == "--open" || a == "--file") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.file_path = argv[++i];
      continue;
    }
    if (!a.empty() && a[0] != '-') {
      if (opt.file_path.empty()) {
        opt.file_path = a;
        continue;
      }
      if (opt.file_path2.empty()) {
        opt.file_path2 = a;
        continue;
      }
      std::cerr << "error: unexpected argument: " << a << "\n";
      print_usage(argv[0]);
      return false;
    }
    std::cerr << "error: unknown argument: " << a << "\n";
    print_usage(argv[0]);
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, opt)) {
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
  if (font_path.empty()) {
    font_path = find_system_font();
  }
  if (font_path.empty()) {
    std::cerr << "No font found. Pass one with --font <path.ttf>\n"
              << "(tried common DejaVu / Liberation / FreeSans locations)\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  if (!fs::exists(font_path)) {
    std::cerr << "Font file does not exist: " << font_path << "\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "Font: " << font_path << "\n";

  glyphtoo::Font body_font, heading_font;
  if (!body_font.load(font_path, 17, sdl) ||
      !heading_font.load(font_path, 26, sdl)) {
    std::cerr << "Failed to load font: " << font_path << "\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }


  auto load_doc = [&](const std::string& path) -> glyphtoo::Document {
    if (path.empty()) {
      return glyphtoo::parse_markup(SAMPLE);
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "Failed to open document: " << path << "\n";
      return glyphtoo::parse_markup(SAMPLE);
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    std::cout << "Opened: " << path << "\n";
    return glyphtoo::parse_markup(source);
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
  docs[0] = load_doc(paths[0]);
  docs[1] = paths[1].empty() ? docs[0] : load_doc(paths[1]);
  // If only one file, pane 1 shares docs[0] until a second file is used
  bool shared_doc = paths[1].empty();

  glyphtoo::ImageCache images(sdl);

  auto update_title = [&](bool dirty_flag) {
    std::string title = "glyphtoo";
    auto label = [&](int i) {
      std::string s;
      if (paths[i].empty()) s = "untitled";
      else s = paths[i];
      return s;
    };
    if (shared_doc || paths[1].empty()) {
      title += " — ";
      if (dirty_flag) title += "*";
      title += label(0);
    } else {
      title += " — ";
      if (dirty_flag) title += "*";
      title += label(0) + " | " + label(1);
    }
    SDL_SetWindowTitle(window, title.c_str());
  };
  update_title(false);

  glyphtoo::LayoutEngine layout_engine(body_font, heading_font, &images);
  glyphtoo::Renderer renderer(sdl, body_font, heading_font, &images);

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

  auto doc_of = [&](Pane& p) -> glyphtoo::Document& {
    return docs[p.doc_index];
  };
  auto path_of = [&](Pane& p) -> std::string& {
    return paths[p.doc_index];
  };
  auto base_of = [&](Pane& p) -> std::string {
    return parent_dir(paths[p.doc_index]);
  };

  glyphtoo::History history;
  history.reset(docs[0], panes[0].sel);

  int win_w = 920, win_h = 720;
  bool running = true;
  bool needs_layout = true;
  bool dirty = false;

  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto F = [&]() -> Pane& { return panes[focus]; };

  auto checkpoint = [&]() {
    history.push(doc_of(F()), F().sel);
  };
  auto mark_dirty = [&]() {
    if (!dirty) {
      dirty = true;
      update_title(true);
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
  };


  auto pane_at = [&](float mx) -> int {
    if (!split) return 0;
    return (mx >= static_cast<float>(panes[1].x)) ? 1 : 0;
  };

  auto local_x = [&](float mx, int pi) {
    return mx - static_cast<float>(panes[pi].x);
  };

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
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

        case SDL_MOUSEWHEEL: {
          Pane& p = F();
          p.scroll_y -= ev.wheel.y * 48.f;
          p.scroll_y = std::max(0.f, p.scroll_y);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y);

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

            if (auto sid = glyphtoo::hit_test_stretch(p.layout, lx, my, p.scroll_y)) {
              glyphtoo::NodeId id = *sid;
              if (doc_of(p).get(id).kind == glyphtoo::NodeKind::Stretch) {
                edit();
                doc_of(p).get(id).expanded = !doc_of(p).get(id).expanded;
                needs_layout = true;
                break;
              }
            }

            if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              p.sel.focus = *hit;
              if (!(SDL_GetModState() & KMOD_SHIFT)) {
                p.sel.anchor = p.sel.focus;
              }
              p.preferred_x = lx;
              dragging_sel = true;
              reset_blink();
            }
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            dragging_sel = false;
            dragging_split = false;
          }
          break;

        case SDL_MOUSEMOTION:
          if (dragging_split && split) {
            float mx = static_cast<float>(ev.motion.x);
            split_ratio = mx / static_cast<float>(win_w);
            split_ratio = std::max(0.15f, std::min(0.85f, split_ratio));
            needs_layout = true;
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
          }
          break;

        case SDL_TEXTINPUT: {
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
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
          Pane& p = F();
          glyphtoo::Document& d = doc_of(p);

          auto clear_sel = [&]() { p.sel.anchor = p.sel.focus; };
          auto move_and_sel = [&](auto mover) {
            if (!shift) clear_sel();
            mover();
            if (!shift) p.sel.anchor = p.sel.focus;
          };

          switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
              running = false;
              break;

            case SDLK_TAB:
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
              edit();
              if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
              glyphtoo::split_block(d, p.sel.focus);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_LEFT:
              move_and_sel([&] {
                if (!glyphtoo::move_left(d, p.sel.focus))
                  glyphtoo::move_to_prev_node(d, p.sel.focus);
              });
              reset_blink();
              break;

            case SDLK_RIGHT:
              move_and_sel([&] {
                if (!glyphtoo::move_right(d, p.sel.focus))
                  glyphtoo::move_to_next_node(d, p.sel.focus);
              });
              reset_blink();
              break;

            case SDLK_UP:
              move_and_sel([&] {
                if (!glyphtoo::move_up(p.layout, p.sel.focus, p.preferred_x))
                  glyphtoo::move_to_prev_node(d, p.sel.focus);
              });
              reset_blink();
              break;

            case SDLK_DOWN:
              move_and_sel([&] {
                if (!glyphtoo::move_down(p.layout, p.sel.focus, p.preferred_x))
                  glyphtoo::move_to_next_node(d, p.sel.focus);
              });
              reset_blink();
              break;

            case SDLK_PAGEUP:
              p.scroll_y = std::max(0.f, p.scroll_y - win_h * 0.8f);
              break;
            case SDLK_PAGEDOWN:
              p.scroll_y += win_h * 0.8f;
              break;

            case SDLK_HOME:
              move_and_sel([&] {
                p.sel.focus.span_index = 0;
                p.sel.focus.char_index = 0;
              });
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
                    glyphtoo::insert_text(d, p.sel.focus, clip);
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
                if (history.undo(d, p.sel)) {
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
                if (history.redo(d, p.sel)) {
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
              if (ctrl) {
                std::string& spath = path_of(p);
                if (spath.empty()) {
                  std::cerr << "No file path — open with: glyphtoo file.md\n";
                } else {
                  std::string text = glyphtoo::serialize_markup(d);
                  std::ofstream out(spath);
                  if (!out) {
                    std::cerr << "Failed to save: " << spath << "\n";
                  } else {
                    out << text;
                    std::cout << "Saved: " << spath << "\n";
                    dirty = false;
                    update_title(false);
                  }
                }
              }
              break;

            case SDLK_a:
              if (ctrl) {
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
    renderer.clear(248, 246, 240);

    int pane_count = split ? 2 : 1;
    for (int i = 0; i < pane_count; ++i) {
      Pane& pane = panes[i];
      float max_scroll = std::max(0.f, pane.layout.content_height - win_h + 40.f);
      if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

      SDL_Rect clip{pane.x, 0, pane.w, win_h};
      SDL_RenderSetClipRect(sdl, &clip);

      float ox = static_cast<float>(pane.x);

      if (!pane.sel.empty()) {
        auto rects = glyphtoo::selection_rects(
            pane.layout, pane.sel, pane.scroll_y, lh);
        renderer.draw_selection(rects, ox);
      }

      renderer.draw(pane.layout, pane.scroll_y, ox);

      if (i == focus) {
        auto cpos = glyphtoo::caret_screen_pos(
            pane.layout, pane.sel.focus, pane.scroll_y, lh);
        if (cpos.valid) pane.preferred_x = cpos.x;
        Uint32 blink_phase = (SDL_GetTicks() - caret_blink_start) % 1000;
        if (blink_phase < 600) {
          renderer.draw_caret(cpos, ox);
        }
      }

      // Focus indicator: thin bar at top of focused pane
      if (i == focus) {
        SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
        SDL_Rect bar{pane.x, 0, pane.w, 3};
        SDL_RenderFillRect(sdl, &bar);
      }
    }

    SDL_RenderSetClipRect(sdl, nullptr);

    // Divider between panes
    if (split) {
      int mid = panes[1].x - 3;
      SDL_SetRenderDrawColor(sdl, 210, 208, 200, 255);
      SDL_Rect div{mid, 0, 6, win_h};
      SDL_RenderFillRect(sdl, &div);
    }

    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
