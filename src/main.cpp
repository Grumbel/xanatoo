// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// xanatoo — interactive document composition engine.
//
// main() only owns the platform handles (SDL window/renderer) and drives the
// frame loop; all state lives in xanatoo::app::App and all behaviour in the
// free-function systems under src/app_*.cpp.

#include "xanatoo/app.hpp"
#include "xanatoo/cli.hpp"
#include "xanatoo/platform.hpp"

#include <SDL.h>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

#include "stb_image.h"

namespace {

// Window icon for X11 (Wayland takes it from the .desktop file matched by
// app_id instead). Build tree first, then the installed hicolor theme.
void set_window_icon(SDL_Window* window) {
#ifndef __ANDROID__
  namespace fs = std::filesystem;
  std::vector<fs::path> cands;
#ifdef XANATOO_APP_ICON
  cands.emplace_back(XANATOO_APP_ICON);
#endif
#ifdef __linux__
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec)
    cands.push_back(exe.parent_path() / ".." / "share" / "icons" / "hicolor" /
                    "64x64" / "apps" / "xanatoo.png");
#endif
  for (const auto& path : cands) {
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
    if (!data) continue;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
      SDL_SetWindowIcon(window, surf);
      SDL_FreeSurface(surf);
    }
    stbi_image_free(data);
    return;
  }
#else
  (void)window;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  xanatoo::platform_pre_init();
  xanatoo::CliOptions opt;
  if (!xanatoo::parse_cli(argc, argv, opt)) return 1;

  // App ID for window matching: Nix's wrapper renames the binary to
  // .xanatoo-wrapped, which SDL would otherwise use as WM_CLASS / app_id.
  // SDL2's headers have no macros for the WM class hints, and sdl2-compat
  // (nixpkgs SDL2) takes SDL3's SDL_APP_ID, so set them by name.
  SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "xanatoo");
  SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "xanatoo");
  SDL_SetHint("SDL_APP_ID", "xanatoo");
#ifdef SDL_HINT_APP_NAME
  SDL_SetHint(SDL_HINT_APP_NAME, "xanatoo");
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) !=
      0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_GameControllerEventState(SDL_ENABLE);
  xanatoo::platform_init();
  if (xanatoo::platform_text_input_at_start())
    SDL_StartTextInput();
  else
    SDL_StopTextInput();

  // Android: not fullscreen, so the activity's adjustResize shrinks the
  // surface when the on-screen keyboard opens (fullscreen windows ignore it)
  // and the caret stays visible.
  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
  if (opt.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window* window =
      SDL_CreateWindow("xanatoo", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 1024, 768, win_flags);
  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }
  set_window_icon(window);
  SDL_Renderer* sdl = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!sdl) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  int rc = 0;
  {
    xanatoo::app::App app(window, sdl);
    app.opt = std::move(opt);
    if (xanatoo::app::app_init(app)) {
      while (app.running) xanatoo::app::run_frame(app);
      xanatoo::app::app_shutdown(app);
    } else {
      rc = 1;
    }
  }  // App (textures, fonts) must die before the renderer.

  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return rc;
}
