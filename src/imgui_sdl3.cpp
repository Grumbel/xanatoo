// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// The application's SDL3 Dear ImGui integration. We use one SDL_Renderer and
// one window; the general-purpose SDL2 backend stays vendored for the other
// platforms. SDL3's renderer consumes floating-point colours and vertices.

#include "xanatoo/imgui_sdl3.hpp"

#include <imgui.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xanatoo::imgui_sdl3 {
namespace {

SDL_Window* window = nullptr;
SDL_Texture* font_texture = nullptr;
SDL_Cursor* arrow = nullptr;
SDL_Cursor* hand = nullptr;
SDL_Cursor* text_cursor = nullptr;
Uint64 last_counter = 0;
char* clipboard_text = nullptr;

const char* clipboard_get(void*) {
  SDL_free(clipboard_text);
  clipboard_text = SDL_GetClipboardText();
  return clipboard_text ? clipboard_text : "";
}

void clipboard_set(void*, const char* text) {
  SDL_SetClipboardText(text);
}

ImGuiKey key_for(SDL_Scancode scancode) {
  if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    return static_cast<ImGuiKey>(ImGuiKey_A + scancode - SDL_SCANCODE_A);
  if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    return static_cast<ImGuiKey>(ImGuiKey_1 + scancode - SDL_SCANCODE_1);
  switch (scancode) {
    case SDL_SCANCODE_0: return ImGuiKey_0;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
    case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
    case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
    case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
    case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
    case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
    case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
    case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
    case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
    case SDL_SCANCODE_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDL_SCANCODE_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
    case SDL_SCANCODE_PRINTSCREEN: return ImGuiKey_PrintScreen;
    case SDL_SCANCODE_PAUSE: return ImGuiKey_Pause;
    case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
    case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
    case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
    case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
    case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
    case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
    case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
    case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
    case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
    case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
    case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
    case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
    default: break;
  }
  if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
    return static_cast<ImGuiKey>(ImGuiKey_F1 + scancode - SDL_SCANCODE_F1);
  return ImGuiKey_None;
}

void update_modifiers(ImGuiIO& io, SDL_Keymod mod) {
  io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
  io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
  io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
  io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
}

SDL_FColor color_from(ImU32 packed) {
  constexpr float inv = 1.f / 255.f;
  return {((packed >> IM_COL32_R_SHIFT) & 0xff) * inv,
          ((packed >> IM_COL32_G_SHIFT) & 0xff) * inv,
          ((packed >> IM_COL32_B_SHIFT) & 0xff) * inv,
          ((packed >> IM_COL32_A_SHIFT) & 0xff) * inv};
}

}  // namespace

bool init(SDL_Window* win, SDL_Renderer* ren) {
  if (!win || !ren) return false;
  window = win;
  last_counter = 0;
  ImGuiIO& io = ImGui::GetIO();
  io.BackendPlatformName = "xanatoo_sdl3";
  io.BackendRendererName = "xanatoo_sdl3_renderer";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
  io.GetClipboardTextFn = clipboard_get;
  io.SetClipboardTextFn = clipboard_set;

  unsigned char* pixels = nullptr;
  int w = 0, h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  font_texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STATIC, w, h);
  if (!font_texture || !SDL_UpdateTexture(font_texture, nullptr, pixels, w * 4)) {
    shutdown();
    return false;
  }
  SDL_SetTextureBlendMode(font_texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(font_texture, SDL_SCALEMODE_LINEAR);
  io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(font_texture)));
  arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
  hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
  text_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
  return true;
}

void shutdown() {
  if (ImGui::GetCurrentContext()) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->SetTexID(0);
    io.BackendPlatformName = nullptr;
    io.BackendRendererName = nullptr;
    io.GetClipboardTextFn = nullptr;
    io.SetClipboardTextFn = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
  }
  if (font_texture) SDL_DestroyTexture(font_texture);
  if (arrow) SDL_DestroyCursor(arrow);
  if (hand) SDL_DestroyCursor(hand);
  if (text_cursor) SDL_DestroyCursor(text_cursor);
  SDL_free(clipboard_text);
  font_texture = nullptr;
  arrow = hand = text_cursor = nullptr;
  clipboard_text = nullptr;
  window = nullptr;
}

void process_event(const SDL_Event& event) {
  ImGuiIO& io = ImGui::GetIO();
  switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
      io.AddMousePosEvent(event.motion.x, event.motion.y);
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      io.AddMouseWheelEvent(-event.wheel.x, event.wheel.y);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      int button = -1;
      switch (event.button.button) {
        case SDL_BUTTON_LEFT: button = 0; break;
        case SDL_BUTTON_RIGHT: button = 1; break;
        case SDL_BUTTON_MIDDLE: button = 2; break;
        case SDL_BUTTON_X1: button = 3; break;
        case SDL_BUTTON_X2: button = 4; break;
      }
      if (button >= 0)
        io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
      break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      update_modifiers(io, static_cast<SDL_Keymod>(event.key.mod));
      const ImGuiKey key = key_for(event.key.scancode);
      if (key != ImGuiKey_None)
        io.AddKeyEvent(key, event.type == SDL_EVENT_KEY_DOWN);
      break;
    }
    case SDL_EVENT_TEXT_INPUT:
      io.AddInputCharactersUTF8(event.text.text);
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      io.AddFocusEvent(true);
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      io.AddFocusEvent(false);
      break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
      break;
  }
}

void new_frame() {
  if (!window) return;
  ImGuiIO& io = ImGui::GetIO();
  int w = 0, h = 0, pw = 0, ph = 0;
  SDL_GetWindowSize(window, &w, &h);
  SDL_GetWindowSizeInPixels(window, &pw, &ph);
  io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
  io.DisplayFramebufferScale = ImVec2(w > 0 ? static_cast<float>(pw) / w : 1.f,
                                       h > 0 ? static_cast<float>(ph) / h : 1.f);
  const Uint64 now = SDL_GetPerformanceCounter();
  const Uint64 freq = SDL_GetPerformanceFrequency();
  io.DeltaTime = last_counter && freq ? static_cast<float>(now - last_counter) / freq
                                      : 1.f / 60.f;
  last_counter = now;

  const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
  SDL_Cursor* desired = cursor == ImGuiMouseCursor_Hand ? hand
                        : cursor == ImGuiMouseCursor_TextInput ? text_cursor
                        : arrow;
  if (io.MouseDrawCursor || cursor == ImGuiMouseCursor_None) {
    SDL_HideCursor();
  } else {
    SDL_ShowCursor();
    if (desired) SDL_SetCursor(desired);
  }
}

void render(ImDrawData* data, SDL_Renderer* ren) {
  if (!data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return;
  SDL_Rect old_clip{};
  const bool old_clipping = SDL_RenderClipEnabled(ren);
  if (old_clipping) SDL_GetRenderClipRect(ren, &old_clip);

  const ImVec2 offset = data->DisplayPos;
  const ImVec2 scale = data->FramebufferScale;
  for (int n = 0; n < data->CmdListsCount; ++n) {
    const ImDrawList* list = data->CmdLists[n];
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(static_cast<size_t>(list->VtxBuffer.Size));
    for (const ImDrawVert& v : list->VtxBuffer) {
      vertices.push_back({{(v.pos.x - offset.x) * scale.x,
                           (v.pos.y - offset.y) * scale.y},
                          color_from(v.col), {v.uv.x, v.uv.y}});
    }
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback) {
        if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
          cmd.UserCallback(list, &cmd);
        continue;
      }
      SDL_Rect clip{
          static_cast<int>(std::floor((cmd.ClipRect.x - offset.x) * scale.x)),
          static_cast<int>(std::floor((cmd.ClipRect.y - offset.y) * scale.y)),
          static_cast<int>(std::ceil((cmd.ClipRect.z - cmd.ClipRect.x) * scale.x)),
          static_cast<int>(std::ceil((cmd.ClipRect.w - cmd.ClipRect.y) * scale.y))};
      if (clip.w <= 0 || clip.h <= 0) continue;
      SDL_SetRenderClipRect(ren, &clip);
      std::vector<int> indices;
      indices.reserve(cmd.ElemCount);
      for (unsigned int i = 0; i < cmd.ElemCount; ++i)
        indices.push_back(static_cast<int>(list->IdxBuffer[cmd.IdxOffset + i] +
                                           cmd.VtxOffset));
      SDL_Texture* texture = reinterpret_cast<SDL_Texture*>(
          static_cast<intptr_t>(cmd.GetTexID()));
      SDL_RenderGeometry(ren, texture, vertices.data(),
                         static_cast<int>(vertices.size()), indices.data(),
                         static_cast<int>(indices.size()));
    }
  }
  SDL_SetRenderClipRect(ren, old_clipping ? &old_clip : nullptr);
}

}  // namespace xanatoo::imgui_sdl3
