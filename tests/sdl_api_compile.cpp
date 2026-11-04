// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Compile this translation unit against *both* SDL2 and SDL3 headers. It
// deliberately has no main(): the flake's SDL3 syntax check does not link.
#include "xanatoo/sdl.hpp"

namespace xanatoo::tests {

void compile_sdl_render_api(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                            SDL_Surface* surface, SDL_Rect* rect,
                            unsigned char* pixels) {
  (void)sdl::init_video_and_gamepads();
  sdl::enable_gamepad_events();
  SDL_Window* created = sdl::create_window("xanatoo", 640, 480, false);
  (void)sdl::create_renderer(created);
  sdl::fill_rect(renderer, rect);
  sdl::draw_rect(renderer, rect);
  sdl::draw_line(renderer, 0, 0, 1, 1);
  sdl::draw_point(renderer, 0, 0);
  sdl::copy(renderer, texture, rect, rect);
  sdl::set_clip_rect(renderer, rect);
  sdl::get_clip_rect(renderer, rect);
  (void)sdl::clip_enabled(renderer);
  (void)sdl::target_supported(renderer);
  (void)sdl::intersect_rect(rect, rect, rect);
  sdl::destroy_surface(surface);
  surface = sdl::rgba_surface_from(pixels, 1, 1, 4);
  (void)sdl::save_bmp(surface, "unused.bmp");
  sdl::destroy_surface(surface);
  int x = 0, y = 0;
  sdl::mouse_position(&x, &y);
  (void)sdl::ticks();
  (void)sdl::ticks_passed(0, 0);
  sdl::renderer_output_size(renderer, &x, &y);
  (void)sdl::fullscreen(window);
  sdl::set_fullscreen(window, false);
  sdl::start_text_input(window);
  sdl::stop_text_input(window);
}

}  // namespace xanatoo::tests
