// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// glyphtoo — interactive document composition engine
// Stage 01: document model + layout + basic FreeType rendering

#include "glyphtoo/types.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/layout.hpp"
#include "glyphtoo/render.hpp"

#include <SDL.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// Build a small sample document inspired by the proposal
glyphtoo::Document make_sample() {
  using namespace glyphtoo;
  Document doc;

  NodeId root = doc.create(NodeKind::Document);

  // Heading
  NodeId h1 = doc.create(NodeKind::Heading, root);
  doc.get(h1).level = 1;
  doc.append_text(h1, "The network");

  // Paragraph
  NodeId p1 = doc.create(NodeKind::Paragraph, root);
  doc.append_text(p1, "The web connects documents, but ");
  doc.append_text(p1, "hypertext", InlineKind::Link);
  doc.append_text(p1, " connects ideas.");

  // Another heading
  NodeId h2 = doc.create(NodeKind::Heading, root);
  doc.get(h2).level = 2;
  doc.append_text(h2, "What is hypertext?");

  NodeId p2 = doc.create(NodeKind::Paragraph, root);
  doc.append_text(p2,
      "Hypertext is a system of linked information that can be "
      "explored non-linearly. Passages can expand in place "
      "(stretchtext), images are first-class addressable content, "
      "and layout is part of the living document rather than a "
      "fixed page prescription.");

  NodeId p3 = doc.create(NodeKind::Paragraph, root);
  doc.append_text(p3,
      "This is the first runnable skeleton of glyphtoo. "
      "It demonstrates a data-oriented document model with stable "
      "node IDs, a simple line-breaking layout engine, and FreeType "
      "glyph rendering through SDL2. Editing, caret, stretchtext and "
      "parallel views will arrive in later stages.");

  NodeId p4 = doc.create(NodeKind::Paragraph, root);
  doc.append_text(p4,
      "Inspiration: Ted Nelson's Xanadu, Bill Atkinson's HyperCard, "
      "and Jef Raskin's Canon Cat — a keyboard-driven work processor "
      "that treated text as a continuous, searchable stream.");

  return doc;
}

static std::string find_font() {
  // Common locations for a readable system font
  const char* candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",  // macOS
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

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow(
      "glyphtoo — interactive document composition",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      900, 700,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* sdl_renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!sdl_renderer) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  std::string font_path = find_font();
  if (font_path.empty()) {
    std::cerr << "No suitable system font found. Install DejaVu or Liberation.\n";
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "Using font: " << font_path << "\n";

  glyphtoo::Font font;
  if (!font.load(font_path, 18, sdl_renderer)) {
    std::cerr << "Failed to load font\n";
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  glyphtoo::Document doc = make_sample();
  glyphtoo::LayoutEngine layout_engine(font);
  glyphtoo::Renderer renderer(sdl_renderer, font);

  int win_w = 900, win_h = 700;
  float scroll_y = 0.f;
  bool running = true;
  bool needs_layout = true;
  glyphtoo::LayoutResult layout;

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
          scroll_y -= ev.wheel.y * 40.f;
          if (scroll_y < 0.f) scroll_y = 0.f;
          break;
        case SDL_KEYDOWN:
          if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
          if (ev.key.keysym.sym == SDLK_UP) scroll_y = std::max(0.f, scroll_y - 30.f);
          if (ev.key.keysym.sym == SDLK_DOWN) scroll_y += 30.f;
          break;
      }
    }

    if (needs_layout) {
      layout = layout_engine.layout(doc, static_cast<float>(win_w));
      needs_layout = false;
    }

    // Paper-like background
    renderer.clear(248, 246, 240);
    renderer.draw(layout, scroll_y);
    renderer.present();
  }

  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
