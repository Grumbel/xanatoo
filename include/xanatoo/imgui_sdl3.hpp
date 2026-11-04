// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#pragma once

#include <SDL3/SDL.h>

struct ImDrawData;

namespace xanatoo::imgui_sdl3 {

// Small single-window Dear ImGui adapter for SDL3. SDL2 keeps its vendored
// backend. The SDL3 backend has no secondary viewports or gamepad navigation.
bool init(SDL_Window* window, SDL_Renderer* renderer);
void shutdown();
void process_event(const SDL_Event& event);
void new_frame();
void render(ImDrawData* data, SDL_Renderer* renderer);

}  // namespace xanatoo::imgui_sdl3
