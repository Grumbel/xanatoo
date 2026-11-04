// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// SDL's 2D rendering switched to floating-point rectangles in SDL3. Keep
// document/layout geometry in integer pixels, and convert at the boundary.
// This header is shared by the legacy SDL2 and future native SDL3 backends.

#pragma once

#if defined(XANATOO_SDL3) && XANATOO_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

namespace xanatoo::sdl {

inline bool init_video_and_gamepads() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
#else
  return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) == 0;
#endif
}

inline void enable_gamepad_events() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_SetGamepadEventsEnabled(true);
#else
  SDL_GameControllerEventState(SDL_ENABLE);
#endif
}

inline SDL_Window* create_window(const char* title, int w, int h,
                                 bool fullscreen) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  SDL_Window* window = SDL_CreateWindow(title, w, h, flags);
  if (window && !fullscreen)
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  return window;
#else
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED, w, h, flags);
#endif
}

inline SDL_Renderer* create_renderer(SDL_Window* window) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer) SDL_SetRenderVSync(renderer, 1);
  return renderer;
#else
  return SDL_CreateRenderer(window, -1,
                            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
}

#if defined(XANATOO_SDL3) && XANATOO_SDL3
inline SDL_FRect float_rect(const SDL_Rect& r) {
  return {static_cast<float>(r.x), static_cast<float>(r.y),
          static_cast<float>(r.w), static_cast<float>(r.h)};
}
#endif

inline void fill_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  const SDL_FRect r = rect ? float_rect(*rect) : SDL_FRect{};
  SDL_RenderFillRect(renderer, rect ? &r : nullptr);
#else
  SDL_RenderFillRect(renderer, rect);
#endif
}

inline void draw_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  const SDL_FRect r = rect ? float_rect(*rect) : SDL_FRect{};
  SDL_RenderRect(renderer, rect ? &r : nullptr);
#else
  SDL_RenderDrawRect(renderer, rect);
#endif
}

inline void draw_line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_RenderLine(renderer, static_cast<float>(x1), static_cast<float>(y1),
                 static_cast<float>(x2), static_cast<float>(y2));
#else
  SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
#endif
}

inline void draw_point(SDL_Renderer* renderer, int x, int y) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_RenderPoint(renderer, static_cast<float>(x), static_cast<float>(y));
#else
  SDL_RenderDrawPoint(renderer, x, y);
#endif
}

inline void copy(SDL_Renderer* renderer, SDL_Texture* texture,
                 const SDL_Rect* src, const SDL_Rect* dst) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  const SDL_FRect s = src ? float_rect(*src) : SDL_FRect{};
  const SDL_FRect d = dst ? float_rect(*dst) : SDL_FRect{};
  SDL_RenderTexture(renderer, texture, src ? &s : nullptr, dst ? &d : nullptr);
#else
  SDL_RenderCopy(renderer, texture, src, dst);
#endif
}

inline void set_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_SetRenderClipRect(renderer, rect);
#else
  SDL_RenderSetClipRect(renderer, rect);
#endif
}

inline void get_clip_rect(SDL_Renderer* renderer, SDL_Rect* rect) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_GetRenderClipRect(renderer, rect);
#else
  SDL_RenderGetClipRect(renderer, rect);
#endif
}

inline bool clip_enabled(SDL_Renderer* renderer) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_RenderClipEnabled(renderer);
#else
  return SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
#endif
}

inline bool target_supported(SDL_Renderer* renderer) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  // SDL3 removed the capability query. Try creating the target texture;
  // a failure there is already handled by the caller.
  (void)renderer;
  return true;
#else
  return SDL_RenderTargetSupported(renderer) == SDL_TRUE;
#endif
}

inline bool intersect_rect(const SDL_Rect* a, const SDL_Rect* b,
                           SDL_Rect* result) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetRectIntersection(a, b, result);
#else
  return SDL_IntersectRect(a, b, result) == SDL_TRUE;
#endif
}

inline SDL_Surface* rgba_surface_from(void* pixels, int w, int h, int pitch) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, pitch);
#else
  return SDL_CreateRGBSurfaceWithFormatFrom(
      pixels, w, h, 32, pitch, SDL_PIXELFORMAT_RGBA32);
#endif
}

inline void destroy_surface(SDL_Surface* surface) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_DestroySurface(surface);
#else
  SDL_FreeSurface(surface);
#endif
}

inline bool save_bmp(SDL_Surface* surface, const char* filename) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_SaveBMP(surface, filename);
#else
  return SDL_SaveBMP(surface, filename) == 0;
#endif
}

// SDL3 reports mouse position in floating-point window coordinates. The
// current hit-testing code still uses integer window coordinates.
inline void mouse_position(int* x, int* y) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  float fx = 0.f, fy = 0.f;
  SDL_GetMouseState(&fx, &fy);
  if (x) *x = static_cast<int>(fx);
  if (y) *y = static_cast<int>(fy);
#else
  SDL_GetMouseState(x, y);
#endif
}

// Keep existing 32-bit timing fields, and SDL2's wrap-safe deadline test.
// SDL3's SDL_GetTicks() returns 64 bits; narrowing preserves the same modulo.
inline Uint32 ticks() { return static_cast<Uint32>(SDL_GetTicks()); }

inline bool ticks_passed(Uint32 now, Uint32 deadline) {
  return static_cast<Sint32>(deadline - now) <= 0;
}

inline void renderer_output_size(SDL_Renderer* renderer, int* w, int* h) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_GetCurrentRenderOutputSize(renderer, w, h);
#else
  SDL_GetRendererOutputSize(renderer, w, h);
#endif
}

inline bool fullscreen(SDL_Window* window) {
  return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
}

inline void set_fullscreen(SDL_Window* window, bool enabled) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_SetWindowFullscreen(window, enabled);
#else
  SDL_SetWindowFullscreen(window, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
}

inline void start_text_input(SDL_Window* window) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_StartTextInput(window);
#else
  (void)window;
  SDL_StartTextInput();
#endif
}

inline void stop_text_input(SDL_Window* window) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_StopTextInput(window);
#else
  (void)window;
  SDL_StopTextInput();
#endif
}

}  // namespace xanatoo::sdl
