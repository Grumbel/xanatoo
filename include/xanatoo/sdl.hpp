// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// SDL's 2D rendering switched to floating-point rectangles in SDL3. Keep
// document/layout geometry in integer pixels, and convert at the boundary.
// This header keeps SDL3 and the explicit SDL2 rollback build on one API.

#pragma once

#if defined(XANATOO_SDL3) && XANATOO_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_misc.h>
// Modifiers and the grave-accent key were renamed in SDL3. Keep the
// application's command-key vocabulary stable while both backends coexist.
#undef KMOD_CTRL
#undef KMOD_SHIFT
#undef KMOD_ALT
#define KMOD_CTRL SDL_KMOD_CTRL
#define KMOD_SHIFT SDL_KMOD_SHIFT
#define KMOD_ALT SDL_KMOD_ALT
#undef SDLK_BACKQUOTE
#define SDLK_BACKQUOTE SDLK_GRAVE
// SDL2 used lowercase key names; SDL3 spells the same keycodes in uppercase.
#undef SDLK_a
#undef SDLK_b
#undef SDLK_c
#undef SDLK_d
#undef SDLK_e
#undef SDLK_f
#undef SDLK_g
#undef SDLK_h
#undef SDLK_i
#undef SDLK_j
#undef SDLK_k
#undef SDLK_l
#undef SDLK_m
#undef SDLK_n
#undef SDLK_o
#undef SDLK_p
#undef SDLK_q
#undef SDLK_r
#undef SDLK_s
#undef SDLK_t
#undef SDLK_u
#undef SDLK_v
#undef SDLK_w
#undef SDLK_x
#undef SDLK_y
#undef SDLK_z
#define SDLK_a SDLK_A
#define SDLK_b SDLK_B
#define SDLK_c SDLK_C
#define SDLK_d SDLK_D
#define SDLK_e SDLK_E
#define SDLK_f SDLK_F
#define SDLK_g SDLK_G
#define SDLK_h SDLK_H
#define SDLK_i SDLK_I
#define SDLK_j SDLK_J
#define SDLK_k SDLK_K
#define SDLK_l SDLK_L
#define SDLK_m SDLK_M
#define SDLK_n SDLK_N
#define SDLK_o SDLK_O
#define SDLK_p SDLK_P
#define SDLK_q SDLK_Q
#define SDLK_r SDLK_R
#define SDLK_s SDLK_S
#define SDLK_t SDLK_T
#define SDLK_u SDLK_U
#define SDLK_v SDLK_V
#define SDLK_w SDLK_W
#define SDLK_x SDLK_X
#define SDLK_y SDLK_Y
#define SDLK_z SDLK_Z
#else
#include <SDL.h>
#if SDL_VERSION_ATLEAST(2, 0, 14)
#include <SDL_misc.h>
#endif
#endif

#include <string>
#include <utility>

namespace xanatoo::sdl {

// The SDL3 gamepad API addresses devices by instance ID, while SDL2 opens
// controllers by their enumeration index. Neither difference belongs in the
// reader's navigation code.
#if defined(XANATOO_SDL3) && XANATOO_SDL3
using Gamepad = SDL_Gamepad;
using GamepadAxis = SDL_GamepadAxis;
inline constexpr GamepadAxis AxisLeftY = SDL_GAMEPAD_AXIS_LEFTY;
inline constexpr GamepadAxis AxisRightX = SDL_GAMEPAD_AXIS_RIGHTX;
inline constexpr GamepadAxis AxisRightY = SDL_GAMEPAD_AXIS_RIGHTY;
inline constexpr GamepadAxis AxisLeftTrigger = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
inline constexpr GamepadAxis AxisRightTrigger = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
namespace button {
inline constexpr Uint8 A = SDL_GAMEPAD_BUTTON_SOUTH;
inline constexpr Uint8 B = SDL_GAMEPAD_BUTTON_EAST;
inline constexpr Uint8 X = SDL_GAMEPAD_BUTTON_WEST;
inline constexpr Uint8 Y = SDL_GAMEPAD_BUTTON_NORTH;
inline constexpr Uint8 Back = SDL_GAMEPAD_BUTTON_BACK;
inline constexpr Uint8 Guide = SDL_GAMEPAD_BUTTON_GUIDE;
inline constexpr Uint8 Start = SDL_GAMEPAD_BUTTON_START;
inline constexpr Uint8 LeftStick = SDL_GAMEPAD_BUTTON_LEFT_STICK;
inline constexpr Uint8 RightStick = SDL_GAMEPAD_BUTTON_RIGHT_STICK;
inline constexpr Uint8 LeftShoulder = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
inline constexpr Uint8 RightShoulder = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
inline constexpr Uint8 DpadUp = SDL_GAMEPAD_BUTTON_DPAD_UP;
inline constexpr Uint8 DpadDown = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
inline constexpr Uint8 DpadLeft = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
inline constexpr Uint8 DpadRight = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
}
#else
using Gamepad = SDL_GameController;
using GamepadAxis = SDL_GameControllerAxis;
inline constexpr GamepadAxis AxisLeftY = SDL_CONTROLLER_AXIS_LEFTY;
inline constexpr GamepadAxis AxisRightX = SDL_CONTROLLER_AXIS_RIGHTX;
inline constexpr GamepadAxis AxisRightY = SDL_CONTROLLER_AXIS_RIGHTY;
inline constexpr GamepadAxis AxisLeftTrigger = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
inline constexpr GamepadAxis AxisRightTrigger = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
namespace button {
inline constexpr Uint8 A = SDL_CONTROLLER_BUTTON_A;
inline constexpr Uint8 B = SDL_CONTROLLER_BUTTON_B;
inline constexpr Uint8 X = SDL_CONTROLLER_BUTTON_X;
inline constexpr Uint8 Y = SDL_CONTROLLER_BUTTON_Y;
inline constexpr Uint8 Back = SDL_CONTROLLER_BUTTON_BACK;
inline constexpr Uint8 Guide = SDL_CONTROLLER_BUTTON_GUIDE;
inline constexpr Uint8 Start = SDL_CONTROLLER_BUTTON_START;
inline constexpr Uint8 LeftStick = SDL_CONTROLLER_BUTTON_LEFTSTICK;
inline constexpr Uint8 RightStick = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
inline constexpr Uint8 LeftShoulder = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
inline constexpr Uint8 RightShoulder = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
inline constexpr Uint8 DpadUp = SDL_CONTROLLER_BUTTON_DPAD_UP;
inline constexpr Uint8 DpadDown = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
inline constexpr Uint8 DpadLeft = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
inline constexpr Uint8 DpadRight = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}
#endif

inline Gamepad* open_first_gamepad() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (!ids) return nullptr;
  Gamepad* gamepad = nullptr;
  for (int i = 0; i < count && !gamepad; ++i)
    gamepad = SDL_OpenGamepad(ids[i]);
  SDL_free(ids);
  return gamepad;
#else
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i))
      if (Gamepad* gamepad = SDL_GameControllerOpen(i)) return gamepad;
  return nullptr;
#endif
}

inline const char* gamepad_name(Gamepad* gamepad) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetGamepadName(gamepad);
#else
  return SDL_GameControllerName(gamepad);
#endif
}

inline void close_gamepad(Gamepad* gamepad) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_CloseGamepad(gamepad);
#else
  SDL_GameControllerClose(gamepad);
#endif
}

inline SDL_JoystickID gamepad_id(Gamepad* gamepad) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetGamepadID(gamepad);
#else
  return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
#endif
}

inline Sint16 gamepad_axis(Gamepad* gamepad, GamepadAxis axis) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetGamepadAxis(gamepad, axis);
#else
  return SDL_GameControllerGetAxis(gamepad, axis);
#endif
}

inline bool gamepad_button(Gamepad* gamepad, Uint8 button_id) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button_id));
#else
  return SDL_GameControllerGetButton(
             gamepad, static_cast<SDL_GameControllerButton>(button_id)) != 0;
#endif
}

// SDL3 flattened SDL_KeyboardEvent.keysym. Keep the reader's key logic
// independent of the SDL event ABI.
inline SDL_Keycode keycode(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return event.key.key;
#else
  return event.key.keysym.sym;
#endif
}

inline void set_keycode(SDL_Event& event, SDL_Keycode code) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  event.key.key = code;
#else
  event.key.keysym.sym = code;
#endif
}

inline SDL_Keymod keymod(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return static_cast<SDL_Keymod>(event.key.mod);
#else
  return static_cast<SDL_Keymod>(event.key.keysym.mod);
#endif
}

inline SDL_Scancode scancode(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return event.key.scancode;
#else
  return event.key.keysym.scancode;
#endif
}

// Gamepad A/B emit real SDL mouse events so the document and ImGui receive
// identical clicks. SDL3 uses floating-point positions and a `down` flag.
inline void push_mouse_button(SDL_Window* window, Uint8 button_id, bool down,
                              int x, int y) {
  SDL_Event event{};
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.down = down;
  event.button.x = static_cast<float>(x);
  event.button.y = static_cast<float>(y);
#else
  event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  event.button.x = x;
  event.button.y = y;
#endif
  event.button.button = button_id;
  event.button.clicks = 1;
  event.button.windowID = SDL_GetWindowID(window);
  SDL_PushEvent(&event);
}

inline void push_mouse_motion(SDL_Window* window, int x, int y,
                              bool left_down, bool right_down) {
  SDL_Event event{};
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.x = static_cast<float>(x);
  event.motion.y = static_cast<float>(y);
#else
  event.type = SDL_MOUSEMOTION;
  event.motion.x = x;
  event.motion.y = y;
#endif
  event.motion.xrel = 0;
  event.motion.yrel = 0;
  event.motion.windowID = SDL_GetWindowID(window);
  event.motion.state = 0;
  if (left_down) event.motion.state |= SDL_BUTTON_LMASK;
  if (right_down) event.motion.state |= SDL_BUTTON_RMASK;
  SDL_PushEvent(&event);
}

enum class EventKind {
  Other,
  Quit,
  WindowResized,
  DropFile,
  DropText,
  MouseWheel,
  MouseButtonDown,
  MouseButtonUp,
  MouseMotion,
  TextInput,
  FingerDown,
  FingerMotion,
  FingerUp,
  GamepadAdded,
  GamepadRemoved,
  GamepadButtonDown,
  GamepadButtonUp,
  KeyDown,
  KeyUp,
  RenderReset,
};

enum class SystemCursor { Arrow, Hand, ResizeEW, ResizeNS, ResizeNWSE, Move };

inline SDL_Cursor* create_system_cursor(SystemCursor kind) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  switch (kind) {
    case SystemCursor::Arrow: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    case SystemCursor::Hand: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    case SystemCursor::ResizeEW: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    case SystemCursor::ResizeNS: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    case SystemCursor::ResizeNWSE: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    case SystemCursor::Move: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
  }
#else
  switch (kind) {
    case SystemCursor::Arrow: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    case SystemCursor::Hand: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    case SystemCursor::ResizeEW: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    case SystemCursor::ResizeNS: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    case SystemCursor::ResizeNWSE: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    case SystemCursor::Move: return SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
  }
#endif
  return nullptr;
}

inline void destroy_cursor(SDL_Cursor* cursor) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  SDL_DestroyCursor(cursor);
#else
  SDL_FreeCursor(cursor);
#endif
}

inline std::string base_path() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  // SDL3 caches the path internally and retains ownership.
  const char* path = SDL_GetBasePath();
  return path ? path : "";
#else
  char* path = SDL_GetBasePath();
  std::string result = path ? path : "";
  SDL_free(path);
  return result;
#endif
}

inline bool open_url(const char* url) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_OpenURL(url);
#elif SDL_VERSION_ATLEAST(2, 0, 14)
  return SDL_OpenURL(url) == 0;
#else
  (void)url;
  SDL_SetError("Opening URLs requires SDL 2.0.14 or newer");
  return false;
#endif
}

inline std::string runtime_version() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  const int version = SDL_GetVersion();
  return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
         std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
         std::to_string(SDL_VERSIONNUM_MICRO(version));
#else
  SDL_version version{};
  SDL_GetVersion(&version);
  return std::to_string(version.major) + "." +
         std::to_string(version.minor) + "." +
         std::to_string(version.patch);
#endif
}

inline const char* renderer_name(SDL_Renderer* renderer) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetRendererName(renderer);
#else
  SDL_RendererInfo info{};
  return SDL_GetRendererInfo(renderer, &info) == 0 ? info.name : nullptr;
#endif
}

inline int touch_device_count() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  int count = 0;
  SDL_TouchID* devices = SDL_GetTouchDevices(&count);
  SDL_free(devices);
  return count;
#else
  return SDL_GetNumTouchDevices();
#endif
}

inline EventKind event_kind(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return EventKind::Quit;
    case SDL_EVENT_WINDOW_RESIZED: return EventKind::WindowResized;
    case SDL_EVENT_DROP_FILE: return EventKind::DropFile;
    case SDL_EVENT_DROP_TEXT: return EventKind::DropText;
    case SDL_EVENT_MOUSE_WHEEL: return EventKind::MouseWheel;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: return EventKind::MouseButtonDown;
    case SDL_EVENT_MOUSE_BUTTON_UP: return EventKind::MouseButtonUp;
    case SDL_EVENT_MOUSE_MOTION: return EventKind::MouseMotion;
    case SDL_EVENT_TEXT_INPUT: return EventKind::TextInput;
    case SDL_EVENT_FINGER_DOWN: return EventKind::FingerDown;
    case SDL_EVENT_FINGER_MOTION: return EventKind::FingerMotion;
    case SDL_EVENT_FINGER_UP: return EventKind::FingerUp;
    case SDL_EVENT_GAMEPAD_ADDED: return EventKind::GamepadAdded;
    case SDL_EVENT_GAMEPAD_REMOVED: return EventKind::GamepadRemoved;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: return EventKind::GamepadButtonDown;
    case SDL_EVENT_GAMEPAD_BUTTON_UP: return EventKind::GamepadButtonUp;
    case SDL_EVENT_KEY_DOWN: return EventKind::KeyDown;
    case SDL_EVENT_KEY_UP: return EventKind::KeyUp;
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET: return EventKind::RenderReset;
    default: return EventKind::Other;
  }
#else
  switch (event.type) {
    case SDL_QUIT: return EventKind::Quit;
    case SDL_WINDOWEVENT:
      return event.window.event == SDL_WINDOWEVENT_RESIZED ||
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                 ? EventKind::WindowResized
                 : EventKind::Other;
    case SDL_DROPFILE: return EventKind::DropFile;
    case SDL_DROPTEXT: return EventKind::DropText;
    case SDL_MOUSEWHEEL: return EventKind::MouseWheel;
    case SDL_MOUSEBUTTONDOWN: return EventKind::MouseButtonDown;
    case SDL_MOUSEBUTTONUP: return EventKind::MouseButtonUp;
    case SDL_MOUSEMOTION: return EventKind::MouseMotion;
    case SDL_TEXTINPUT: return EventKind::TextInput;
    case SDL_FINGERDOWN: return EventKind::FingerDown;
    case SDL_FINGERMOTION: return EventKind::FingerMotion;
    case SDL_FINGERUP: return EventKind::FingerUp;
    case SDL_CONTROLLERDEVICEADDED: return EventKind::GamepadAdded;
    case SDL_CONTROLLERDEVICEREMOVED: return EventKind::GamepadRemoved;
    case SDL_CONTROLLERBUTTONDOWN: return EventKind::GamepadButtonDown;
    case SDL_CONTROLLERBUTTONUP: return EventKind::GamepadButtonUp;
    case SDL_KEYDOWN: return EventKind::KeyDown;
    case SDL_KEYUP: return EventKind::KeyUp;
    case SDL_RENDER_TARGETS_RESET:
    case SDL_RENDER_DEVICE_RESET: return EventKind::RenderReset;
    default: return EventKind::Other;
  }
#endif
}

inline Uint8 gamepad_event_button(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return event.gbutton.button;
#else
  return event.cbutton.button;
#endif
}

inline SDL_JoystickID gamepad_event_id(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return event.gdevice.which;
#else
  return event.cdevice.which;
#endif
}

inline std::string take_drop_data(SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  // SDL3 owns the data for file and text drops, and frees it after the event.
  return event.drop.data ? event.drop.data : "";
#else
  // Both SDL_DROPFILE and SDL_DROPTEXT use the `file` payload in SDL2.
  char* data = event.drop.file;
  std::string result = data ? data : "";
  SDL_free(data);
  event.drop.file = nullptr;
  return result;
#endif
}

inline std::string take_drop_filename(SDL_Event& event) {
  return take_drop_data(event);
}

// SDL2's SDL_DropEvent has no x/y; SDL3 records the actual drop position.
inline std::pair<int, int> drop_position(const SDL_Event& event) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return {static_cast<int>(event.drop.x), static_cast<int>(event.drop.y)};
#else
  (void)event;
  int x = 0, y = 0;
  mouse_position(&x, &y);
  return {x, y};
#endif
}

inline SDL_TouchID touch_id(const SDL_TouchFingerEvent& finger) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return finger.touchID;
#else
  return finger.touchId;
#endif
}

inline SDL_FingerID finger_id(const SDL_TouchFingerEvent& finger) {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return finger.fingerID;
#else
  return finger.fingerId;
#endif
}

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

inline int cpu_count() {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  return SDL_GetNumLogicalCPUCores();
#else
  return SDL_GetCPUCount();
#endif
}

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
