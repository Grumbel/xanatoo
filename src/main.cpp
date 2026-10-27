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

#include <SDL.h>
#include <cstdlib>
#include <string>
#ifdef __ANDROID__
#include <sys/stat.h>
#endif
#include <iostream>
#include <utility>

int main(int argc, char** argv) {
#ifdef __ANDROID__
  // No HOME / writable /tmp on Android: point them at the app's private
  // storage so config_dir(), view.conf, history and caches just work.
  if (const char* internal = SDL_AndroidGetInternalStoragePath()) {
    setenv("HOME", internal, 1);
    const std::string tmp = std::string(internal) + "/tmp";
    mkdir(tmp.c_str(), 0700);
    setenv("TMPDIR", tmp.c_str(), 1);
  }
#endif
  xanatoo::CliOptions opt;
  if (!xanatoo::parse_cli(argc, argv, opt)) return 1;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) !=
      0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_GameControllerEventState(SDL_ENABLE);
  SDL_StartTextInput();

  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
#ifdef __ANDROID__
  win_flags |= SDL_WINDOW_FULLSCREEN;  // the activity owns the whole screen
#endif
  if (opt.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window* window =
      SDL_CreateWindow("xanatoo", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 1024, 768, win_flags);
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
