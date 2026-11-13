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
#include "xanatoo/paths.hpp"
#include "xanatoo/platform.hpp"

#include "xanatoo/sdl.hpp"
#if defined(XANATOO_SDL3) && XANATOO_SDL3
#include <SDL3/SDL_main.h>
#endif
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "stb_image.h"

namespace {

// Window icon for X11 (Wayland takes it from the .desktop file matched by
// app_id instead): the hicolor theme beside the data directory
// (<prefix>/share/icons next to <prefix>/share/xanatoo).
void set_window_icon(SDL_Window* window) {
#ifndef __ANDROID__
  namespace fs = std::filesystem;
  std::vector<fs::path> cands;
  if (const std::string data = xanatoo::data_dir(); !data.empty())
    cands.push_back(fs::path(data) / ".." / "icons" / "hicolor" / "64x64" /
                    "apps" / "xanatoo.png");
  for (const auto& path : cands) {
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
    if (!data) continue;
    SDL_Surface* surf = xanatoo::sdl::rgba_surface_from(
        data, w, h, w * 4);
    if (surf) {
      SDL_SetWindowIcon(window, surf);
      xanatoo::sdl::destroy_surface(surf);
    }
    stbi_image_free(data);
    return;
  }
#else
  (void)window;
#endif
}

#ifdef __EMSCRIPTEN__
// The browser owns the frame schedule. All SDL resources must outlive main(),
// because emscripten_set_main_loop_arg(..., simulateInfiniteLoop=0) returns.
struct BrowserRuntime {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  std::unique_ptr<xanatoo::app::App> app;
};

// A file the page wrote into the virtual filesystem (shell.html: the Open
// button and drops on the canvas); opened by the next frame, on this thread.
std::string g_web_open_path;  // NOLINT: set from JavaScript on the main thread

void browser_frame(void* ctx) {
  auto* runtime = static_cast<BrowserRuntime*>(ctx);
  if (!g_web_open_path.empty()) {
    const std::string path = std::move(g_web_open_path);
    g_web_open_path.clear();
    xanatoo::app::open_browse(*runtime->app, path,
                              std::filesystem::path(path).filename().string());
  }
  xanatoo::app::run_frame(*runtime->app);
  if (runtime->app->running) return;
  emscripten_cancel_main_loop();
  xanatoo::app::app_shutdown(*runtime->app);
  runtime->app.reset();
  SDL_DestroyRenderer(runtime->renderer);
  SDL_DestroyWindow(runtime->window);
  SDL_Quit();
  delete runtime;
}
#endif

}  // namespace

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void xanatoo_web_open(const char* path) {
  if (path) g_web_open_path = path;
}
#endif

int main(int argc, char** argv) {
  // Logs are usually a pipe (PortMaster's tee, a terminal paste): keep
  // stdout lines in order with stderr instead of flushing them at exit.
  std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
  xanatoo::platform_pre_init();
  xanatoo::CliOptions opt;
  xanatoo::CliResult cli = xanatoo::parse_cli(argc, argv, opt);
#ifdef __EMSCRIPTEN__
  // The initial web edition reads from the preloaded virtual filesystem.
  // It is deliberately ephemeral until IndexedDB persistence is implemented.
  if (opt.data_dir.empty()) opt.data_dir = "/data";
  opt.no_save = true;
  opt.new_session = true;
#endif
  if (cli == xanatoo::CliResult::Run) cli = xanatoo::apply_path_options(opt);
  switch (cli) {
    case xanatoo::CliResult::Run: break;
    case xanatoo::CliResult::Exit: return 0;
    case xanatoo::CliResult::Error: return 1;
  }

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

  if (!xanatoo::sdl::init_video_and_gamepads()) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  xanatoo::sdl::enable_gamepad_events();
  xanatoo::platform_init();

  // Android: not fullscreen, so the activity's adjustResize shrinks the
  // surface when the on-screen keyboard opens (fullscreen windows ignore it)
  // and the caret stays visible.
  SDL_Window* window = xanatoo::sdl::create_window(
      "xanatoo", opt.width, opt.height, opt.fullscreen);
  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }
  if (xanatoo::platform_text_input_at_start())
    xanatoo::sdl::start_text_input(window);
  else
    xanatoo::sdl::stop_text_input(window);
  set_window_icon(window);
  SDL_Renderer* sdl = xanatoo::sdl::create_renderer(window);
  if (!sdl) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

#ifdef __EMSCRIPTEN__
  auto runtime = std::make_unique<BrowserRuntime>();
  runtime->window = window;
  runtime->renderer = sdl;
  runtime->app = std::make_unique<xanatoo::app::App>(window, sdl);
  runtime->app->opt = std::move(opt);
  if (!xanatoo::app::app_init(*runtime->app)) {
    runtime->app.reset();
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  emscripten_set_main_loop_arg(browser_frame, runtime.release(), 0, 0);
  return 0;
#else
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
#endif
}
