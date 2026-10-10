// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// glyphtoo — interactive document composition engine
// Stage 01 + early stage 02: markup, layout, caret, basic editing

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

## What is hypertext?

Hypertext is a system of linked information that can be explored non-linearly.
Passages can expand in place (*stretchtext*), images are first-class
addressable content, and layout is part of the living document rather than a
fixed page prescription.

This is a live document. Click to place the caret, type to insert text,
Backspace to delete. Arrow keys move the caret; PageUp / PageDown scroll.

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

  glyphtoo::Caret caret;
  glyphtoo::ensure_valid_caret(doc, caret);

  int win_w = 920, win_h = 720;
  float scroll_y = 0.f;
  bool running = true;
  bool needs_layout = true;
  glyphtoo::LayoutResult layout;

  auto reflow = [&]() {
    layout = layout_engine.layout(doc, static_cast<float>(win_w));
    needs_layout = false;
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

        case SDL_MOUSEWHEEL:
          scroll_y -= ev.wheel.y * 48.f;
          scroll_y = std::max(0.f, scroll_y);
          break;

        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y);
            if (auto hit = glyphtoo::hit_test(layout, mx, my, scroll_y)) {
              caret = *hit;
            }
          }
          break;

        case SDL_TEXTINPUT: {
          glyphtoo::insert_text(doc, caret, ev.text.text);
          needs_layout = true;
          break;
        }

        case SDL_KEYDOWN: {
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          (void)shift;
          switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
              running = false;
              break;
            case SDLK_BACKSPACE:
              glyphtoo::delete_backward(doc, caret);
              needs_layout = true;
              break;
            case SDLK_DELETE:
              glyphtoo::delete_forward(doc, caret);
              needs_layout = true;
              break;
            case SDLK_LEFT:
              if (!glyphtoo::move_left(doc, caret))
                glyphtoo::move_to_prev_node(doc, caret);
              break;
            case SDLK_RIGHT:
              if (!glyphtoo::move_right(doc, caret))
                glyphtoo::move_to_next_node(doc, caret);
              break;
            case SDLK_UP:
              // crude: previous node
              glyphtoo::move_to_prev_node(doc, caret);
              break;
            case SDLK_DOWN:
              glyphtoo::move_to_next_node(doc, caret);
              break;
            case SDLK_PAGEUP:
              scroll_y = std::max(0.f, scroll_y - win_h * 0.8f);
              break;
            case SDLK_PAGEDOWN:
              scroll_y += win_h * 0.8f;
              break;
            case SDLK_HOME:
              caret.span_index = 0;
              caret.char_index = 0;
              break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              // For now insert a space-newline marker; proper block split later
              glyphtoo::insert_text(doc, caret, "\n");
              needs_layout = true;
              break;
            default:
              break;
          }
          break;
        }
      }
    }

    if (needs_layout) reflow();

    // Clamp scroll
    float max_scroll = std::max(0.f, layout.content_height - win_h + 40.f);
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    renderer.clear(248, 246, 240);
    renderer.draw(layout, scroll_y);

    auto cpos = glyphtoo::caret_screen_pos(
        layout, caret, scroll_y, static_cast<float>(body_font.line_height()));
    renderer.draw_caret(cpos);
    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
