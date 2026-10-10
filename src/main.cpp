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

## Living documents

This is a live document. Click to place the caret, type to insert text,
Backspace to delete. **Enter** splits a paragraph. Shift+arrows select.
Ctrl+C / Ctrl+V / Ctrl+X for clipboard, Ctrl+Z / Ctrl+Y for undo/redo.
Click a stretchtext marker (▸ / ▾) to expand or collapse.

Inspiration: Ted Nelson's *Xanadu*, Bill Atkinson's **HyperCard**, and
Jef Raskin's *Canon Cat* — a keyboard-driven work processor that treated
text as a continuous, searchable stream.
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
      << "Usage: " << argv0 << " [options] [file]\n"
      << "\n"
      << "Options:\n"
      << "  -f, --font <path>     TTF/OTF font file (body + headings)\n"
      << "  -o, --open <path>     Open a markup document\n"
      << "  -h, --help            Show this help\n"
      << "\n"
      << "A positional path is treated as the document to open.\n"
      << "Ctrl+S saves back to that path.\n"
      << "\n"
      << "Font resolution order: --font, GLYPHTOO_FONT env, system search.\n";
}

struct Options {
  std::string font_path;
  std::string file_path;  // open this markup file (empty = built-in sample)
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
      // positional: document path
      if (opt.file_path.empty()) {
        opt.file_path = a;
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

  std::string doc_path = opt.file_path;
  glyphtoo::Document doc;
  if (!doc_path.empty()) {
    std::ifstream in(doc_path);
    if (!in) {
      std::cerr << "Failed to open document: " << doc_path << "\n";
      SDL_DestroyRenderer(sdl);
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    doc = glyphtoo::parse_markup(source);
    std::cout << "Opened: " << doc_path << "\n";
  } else {
    doc = glyphtoo::parse_markup(SAMPLE);
  }
  {
    std::string title = "glyphtoo";
    if (!doc_path.empty()) title += " — " + doc_path;
    SDL_SetWindowTitle(window, title.c_str());
  }

  glyphtoo::LayoutEngine layout_engine(body_font, heading_font);
  glyphtoo::Renderer renderer(sdl, body_font, heading_font);

  glyphtoo::Selection sel;
  glyphtoo::ensure_valid_caret(doc, sel.focus);
  sel.anchor = sel.focus;

  glyphtoo::History history;
  history.reset(doc, sel);

  int win_w = 920, win_h = 720;
  float scroll_y = 0.f;
  float preferred_x = 0.f;  // for vertical movement
  bool running = true;
  bool needs_layout = true;
  bool dragging = false;
  glyphtoo::LayoutResult layout;

  // Caret blink
  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto checkpoint = [&]() {
    history.push(doc, sel);
  };

  auto reflow = [&]() {
    layout = layout_engine.layout(doc, static_cast<float>(win_w));
    needs_layout = false;
  };

  auto clear_sel = [&]() { sel.anchor = sel.focus; };

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

        case SDL_MOUSEWHEEL:
          scroll_y -= ev.wheel.y * 48.f;
          scroll_y = std::max(0.f, scroll_y);
          break;

        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y);

            // Stretchtext toggle?
            if (auto sid = glyphtoo::hit_test_stretch(layout, mx, my, scroll_y)) {
              glyphtoo::NodeId id = *sid;
              if (doc.get(id).kind == glyphtoo::NodeKind::Stretch) {
                checkpoint();
                doc.get(id).expanded = !doc.get(id).expanded;
                needs_layout = true;
                break;
              }
            }

            if (auto hit = glyphtoo::hit_test(layout, mx, my, scroll_y)) {
              sel.focus = *hit;
              if (!(SDL_GetModState() & KMOD_SHIFT)) {
                sel.anchor = sel.focus;
              }
              preferred_x = mx;
              dragging = true;
              reset_blink();
            }
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) dragging = false;
          break;

        case SDL_MOUSEMOTION:
          if (dragging) {
            float mx = static_cast<float>(ev.motion.x);
            float my = static_cast<float>(ev.motion.y);
            if (auto hit = glyphtoo::hit_test(layout, mx, my, scroll_y)) {
              sel.focus = *hit;
              preferred_x = mx;
            }
          }
          break;

        case SDL_TEXTINPUT: {
          checkpoint();
          if (!sel.empty()) {
            glyphtoo::delete_selection(doc, sel);
          }
          glyphtoo::insert_text(doc, sel.focus, ev.text.text);
          sel.anchor = sel.focus;
          needs_layout = true;
          reset_blink();
          break;
        }

        case SDL_KEYDOWN: {
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
          (void)ctrl;

          auto move_and_sel = [&](auto mover) {
            if (!shift) clear_sel();
            mover();
            if (!shift) sel.anchor = sel.focus;
          };

          switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
              running = false;
              break;

            case SDLK_BACKSPACE:
              checkpoint();
              if (!sel.empty()) {
                glyphtoo::delete_selection(doc, sel);
              } else {
                glyphtoo::delete_backward(doc, sel.focus);
                sel.anchor = sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_DELETE:
              checkpoint();
              if (!sel.empty()) {
                glyphtoo::delete_selection(doc, sel);
              } else {
                glyphtoo::delete_forward(doc, sel.focus);
                sel.anchor = sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              checkpoint();
              if (!sel.empty()) glyphtoo::delete_selection(doc, sel);
              glyphtoo::split_block(doc, sel.focus);
              sel.anchor = sel.focus;
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_LEFT:
              move_and_sel([&] {
                if (!glyphtoo::move_left(doc, sel.focus))
                  glyphtoo::move_to_prev_node(doc, sel.focus);
              });
              break;

            case SDLK_RIGHT:
              move_and_sel([&] {
                if (!glyphtoo::move_right(doc, sel.focus))
                  glyphtoo::move_to_next_node(doc, sel.focus);
              });
              break;

            case SDLK_UP:
              move_and_sel([&] {
                if (!glyphtoo::move_up(layout, sel.focus, preferred_x))
                  glyphtoo::move_to_prev_node(doc, sel.focus);
              });
              break;

            case SDLK_DOWN:
              move_and_sel([&] {
                if (!glyphtoo::move_down(layout, sel.focus, preferred_x))
                  glyphtoo::move_to_next_node(doc, sel.focus);
              });
              break;

            case SDLK_PAGEUP:
              scroll_y = std::max(0.f, scroll_y - win_h * 0.8f);
              break;
            case SDLK_PAGEDOWN:
              scroll_y += win_h * 0.8f;
              break;

            case SDLK_HOME:
              move_and_sel([&] {
                sel.focus.span_index = 0;
                sel.focus.char_index = 0;
              });
              break;

            case SDLK_c:
              if (ctrl) {
                std::string text = glyphtoo::selected_text(doc, sel);
                if (!text.empty()) {
                  SDL_SetClipboardText(text.c_str());
                }
              }
              break;

            case SDLK_x:
              if (ctrl) {
                std::string text = glyphtoo::selected_text(doc, sel);
                if (!text.empty()) {
                  SDL_SetClipboardText(text.c_str());
                  checkpoint();
                  glyphtoo::delete_selection(doc, sel);
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
                    checkpoint();
                    if (!sel.empty()) glyphtoo::delete_selection(doc, sel);
                    glyphtoo::insert_text(doc, sel.focus, clip);
                    sel.anchor = sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  if (clip) SDL_free(clip);
                }
              }
              break;

            case SDLK_z:
              if (ctrl) {
                if (history.undo(doc, sel)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_y:
              if (ctrl) {
                if (history.redo(doc, sel)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_s:
              if (ctrl) {
                if (doc_path.empty()) {
                  std::cerr << "No file path — open with: glyphtoo file.md\n";
                } else {
                  std::string text = glyphtoo::serialize_markup(doc);
                  std::ofstream out(doc_path);
                  if (!out) {
                    std::cerr << "Failed to save: " << doc_path << "\n";
                  } else {
                    out << text;
                    std::cout << "Saved: " << doc_path << "\n";
                  }
                }
              }
              break;

            case SDLK_a:
              if (ctrl) {
                // Select all text nodes — set anchor to first, focus to last char
                // Simple: select current node entirely
                const auto& n = doc.get(sel.focus.node);
                sel.anchor = {sel.focus.node, 0, 0};
                sel.focus.span_index = n.spans.empty() ? 0 : n.spans.size() - 1;
                if (!n.spans.empty()) {
                  // end of last span
                  size_t len = 0;
                  const std::string& t = n.spans.back().text;
                  for (size_t i = 0; i < t.size(); ) {
                    unsigned char c = static_cast<unsigned char>(t[i]);
                    if (c < 0x80) { ++i; ++len; }
                    else if ((c & 0xe0) == 0xc0) { i += 2; ++len; }
                    else if ((c & 0xf0) == 0xe0) { i += 3; ++len; }
                    else if ((c & 0xf8) == 0xf0) { i += 4; ++len; }
                    else ++i;
                  }
                  sel.focus.char_index = len;
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

    if (needs_layout) reflow();

    float max_scroll = std::max(0.f, layout.content_height - win_h + 40.f);
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    float lh = static_cast<float>(body_font.line_height());
    renderer.clear(248, 246, 240);

    if (!sel.empty()) {
      auto rects = glyphtoo::selection_rects(layout, sel, scroll_y, lh);
      renderer.draw_selection(rects);
    }

    renderer.draw(layout, scroll_y);

    auto cpos = glyphtoo::caret_screen_pos(layout, sel.focus, scroll_y, lh);
    // Keep preferred_x in sync when moving horizontally
    if (cpos.valid) preferred_x = cpos.x;
    // Blink: visible 600ms, hidden 400ms
    Uint32 blink_phase = (SDL_GetTicks() - caret_blink_start) % 1000;
    if (blink_phase < 600) {
      renderer.draw_caret(cpos);
    }
    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
