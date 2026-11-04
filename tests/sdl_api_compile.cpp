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
  sdl::Gamepad* pad = sdl::open_first_gamepad();
  (void)sdl::gamepad_name(pad);
  (void)sdl::gamepad_id(pad);
  (void)sdl::gamepad_axis(pad, sdl::AxisRightX);
  (void)sdl::gamepad_button(pad, sdl::button::A);
  sdl::close_gamepad(pad);
  SDL_Event event{};
  (void)sdl::keycode(event);
  sdl::set_keycode(event, SDLK_ESCAPE);
  (void)sdl::keymod(event);
  (void)sdl::scancode(event);
  sdl::push_mouse_button(window, SDL_BUTTON_LEFT, true, 1, 2);
  sdl::push_mouse_motion(window, 1, 2, true, false);
  (void)sdl::event_kind(event);
  (void)sdl::gamepad_event_button(event);
  (void)sdl::gamepad_event_id(event);
  (void)sdl::take_drop_filename(event);
  SDL_TouchFingerEvent finger{};
  (void)sdl::touch_id(finger);
  (void)sdl::finger_id(finger);
  SDL_Cursor* cursor = sdl::create_system_cursor(sdl::SystemCursor::Hand);
  sdl::destroy_cursor(cursor);
  (void)sdl::base_path();
  (void)sdl::open_url("https://example.org");
  (void)sdl::runtime_version();
  (void)sdl::renderer_name(renderer);
  (void)sdl::touch_device_count();
}

}  // namespace xanatoo::tests
