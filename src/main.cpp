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
Click a stretchtext marker (▸ / ▾) to expand or collapse.

Inspiration: Ted Nelson's *Xanadu*, Bill Atkinson's **HyperCard**, and
Jef Raskin's *Canon Cat* — a keyboard-driven work processor that treated
text as a continuous, searchable stream.
)";

static std::string find_font() {
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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

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

  std::string font_path = find_font();
  if (font_path.empty()) {
    std::cerr << "No system font found (need DejaVu or Liberation).\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "Font: " << font_path << "\n";

  glyphtoo::Font body_font, heading_font;
  if (!body_font.load(font_path, 17, sdl) ||
      !heading_font.load(font_path, 26, sdl)) {
    std::cerr << "Font load failed\n";
    return 1;
  }

  glyphtoo::Document doc = glyphtoo::parse_markup(SAMPLE);
  glyphtoo::LayoutEngine layout_engine(body_font, heading_font);
  glyphtoo::Renderer renderer(sdl, body_font, heading_font);

  glyphtoo::Selection sel;
  glyphtoo::ensure_valid_caret(doc, sel.focus);
  sel.anchor = sel.focus;

  int win_w = 920, win_h = 720;
  float scroll_y = 0.f;
  float preferred_x = 0.f;  // for vertical movement
  bool running = true;
  bool needs_layout = true;
  bool dragging = false;
  glyphtoo::LayoutResult layout;

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
              NodeId id = *sid;
              if (doc.get(id).kind == glyphtoo::NodeKind::Stretch) {
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
          if (!sel.empty()) {
            glyphtoo::delete_selection(doc, sel);
          }
          glyphtoo::insert_text(doc, sel.focus, ev.text.text);
          sel.anchor = sel.focus;
          needs_layout = true;
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
              if (!sel.empty()) {
                glyphtoo::delete_selection(doc, sel);
              } else {
                glyphtoo::delete_backward(doc, sel.focus);
                sel.anchor = sel.focus;
              }
              needs_layout = true;
              break;

            case SDLK_DELETE:
              if (!sel.empty()) {
                glyphtoo::delete_selection(doc, sel);
              } else {
                glyphtoo::delete_forward(doc, sel.focus);
                sel.anchor = sel.focus;
              }
              needs_layout = true;
              break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (!sel.empty()) glyphtoo::delete_selection(doc, sel);
              glyphtoo::split_block(doc, sel.focus);
              sel.anchor = sel.focus;
              needs_layout = true;
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
    renderer.draw_caret(cpos);
    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
