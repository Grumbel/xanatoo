// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Touchscreen gestures, browser style: tap = left click (follow links,
// place caret), drag = scroll with momentum, long press = select word and
// drag to extend, two-finger pinch = zoom. Touches on ImGui chrome are left
// to ImGui, which sees SDL's touch-synthesised mouse events.

#include <algorithm>
#include <cmath>

#include "xanatoo/app.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/layout.hpp"

namespace xanatoo::app {

namespace {

constexpr float kSlopPx = 10.f;         // movement before a touch is a pan
constexpr Uint32 kLongPressMs = 500;    // hold this long → selection
constexpr Uint32 kFlingMaxIdleMs = 80;  // finger rested → no fling
constexpr float kFlingMinSpeed = 60.f;  // px/s to start a fling
constexpr float kFlingStopSpeed = 15.f;
constexpr float kFlingTau = 0.4f;  // s; velocity decays as exp(-t/tau)

TouchPoint* find_finger(Touch& t, SDL_FingerID id) {
  for (int i = 0; i < t.count; ++i)
    if (t.fingers[i].id == id) return &t.fingers[i];
  return nullptr;
}

float finger_distance(const Touch& t) {
  return std::hypot(t.fingers[0].x - t.fingers[1].x,
                    t.fingers[0].y - t.fingers[1].y);
}

// Scroll the gesture target by dy; false when it is at an end (or gone).
bool scroll_target(App& app, float dy) {
  Touch& t = app.touch;
  if (t.on_float) {
    FloatWindow* w = float_at(app, static_cast<int>(t.start_x),
                              static_cast<int>(t.start_y));
    if (!w) return false;
    const float s =
        std::clamp(w->scroll + dy, 0.f, float_max_scroll(app, *w));
    const bool moved = s != w->scroll;
    w->scroll = s;
    return moved;
  }
  Pane& p = app.panes[t.pane];
  const float s = std::clamp(p.scroll_y + dy, 0.f, pane_max_scroll(app, p));
  const bool moved = s != p.scroll_y;
  p.scroll_y = s;
  ensure_virtual_layout(app, p);
  return moved;
}

// Decide what a first finger at (x, y) is for.
void begin_gesture(App& app, float x, float y) {
  Touch& t = app.touch;
  t.flinging = false;
  t.velocity = 0.f;
  t.start_x = x;
  t.start_y = y;
  t.start_ticks = SDL_GetTicks();
  t.last_move_ticks = t.start_ticks;
  t.on_float = false;
  const int xi = static_cast<int>(x), yi = static_cast<int>(y);
  if (app.ui.captures_point(x, y) || y < app.ui_top ||
      yi >= app.win_h - status_bar_h) {
    t.mode = TouchMode::Ignored;
    return;
  }
  if (FloatWindow* w = float_at(app, xi, yi)) {
    // Client area scrolls/taps; title, frame and scrollbar strip drag.
    auto [cx, cy, cw, ch] = float_client_rect(app, *w);
    constexpr int sb_w = 10;
    if (xi >= cx && xi < cx + cw - sb_w && yi >= cy && yi < cy + ch) {
      t.on_float = true;
      t.mode = TouchMode::Pending;
    } else {
      t.mode = TouchMode::Direct;
      pointer_left_down(app, xi, yi, false);
    }
    return;
  }
  t.pane = pane_at(app, x);
  const Pane& p = app.panes[t.pane];
  const bool on_scrollbar = xi >= p.x + p.w - kSbW;
  const bool on_gutter = app.split && std::abs(x - app.panes[1].x) <= 3.f;
  if (on_scrollbar || on_gutter) {
    t.mode = TouchMode::Direct;
    pointer_left_down(app, xi, yi, false);
    return;
  }
  t.mode = TouchMode::Pending;
}

// Long press on pane text: select the word and let the drag extend it.
void begin_select(App& app) {
  Touch& t = app.touch;
  Pane& p = app.panes[t.pane];
  const float lx = local_x(app, t.start_x, t.pane);
  const float my = content_y(app, t.start_y);
  auto hit = xanatoo::hit_test(p.layout, lx, my, p.scroll_y);
  if (!hit) {
    t.mode = TouchMode::Pan;  // nothing to select; keep scrolling
    return;
  }
  if (prompt_active(app) && !prompt_is_search(app)) close_prompt(app);
  app.focus = t.pane;
  p.sel.focus = *hit;
  p.sel.anchor = *hit;
  xanatoo::select_word_at(doc_of(app, p), p.sel);
  p.preferred_x = lx;
  app.dragging_sel = true;
  reset_blink(app);
  t.mode = TouchMode::Select;
}

void begin_pinch(App& app) {
  Touch& t = app.touch;
  t.flinging = false;
  t.pinch_dist = finger_distance(t);
  t.pinch_body_px = app.body_px;
  t.mode = TouchMode::Pinch;
}

}  // namespace

bool is_touch_mouse_event(const SDL_Event& ev) {
  switch (ev.type) {
    case SDL_MOUSEMOTION:
      return ev.motion.which == SDL_TOUCH_MOUSEID;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      return ev.button.which == SDL_TOUCH_MOUSEID;
    case SDL_MOUSEWHEEL:
      return ev.wheel.which == SDL_TOUCH_MOUSEID;
    default:
      return false;
  }
}

// Only touchscreens: touchpad coordinates are not window positions, and
// mouse-synthesised touches would duplicate the mouse handlers.
static bool is_screen_touch(const SDL_TouchFingerEvent& tf) {
  return tf.touchId != SDL_MOUSE_TOUCHID &&
         SDL_GetTouchDeviceType(tf.touchId) == SDL_TOUCH_DEVICE_DIRECT;
}

void touch_finger_down(App& app, const SDL_TouchFingerEvent& tf) {
  if (!is_screen_touch(tf)) return;
  Touch& t = app.touch;
  if (t.count >= 2 || find_finger(t, tf.fingerId)) return;
  const float x = tf.x * static_cast<float>(app.win_w);
  const float y = tf.y * static_cast<float>(app.win_h);
  t.fingers[t.count++] = TouchPoint{tf.fingerId, x, y};
  if (t.count == 1) {
    begin_gesture(app, x, y);
  } else if (t.mode == TouchMode::Pending || t.mode == TouchMode::Pan) {
    begin_pinch(app);
  }
}

void touch_finger_motion(App& app, const SDL_TouchFingerEvent& tf) {
  if (!is_screen_touch(tf)) return;
  Touch& t = app.touch;
  TouchPoint* f = find_finger(t, tf.fingerId);
  if (!f) return;
  const float x = tf.x * static_cast<float>(app.win_w);
  const float y = tf.y * static_cast<float>(app.win_h);
  const float dy = y - f->y;
  f->x = x;
  f->y = y;
  const bool primary = f == &t.fingers[0];
  switch (t.mode) {
    case TouchMode::Pending:
      if (std::hypot(x - t.start_x, y - t.start_y) < kSlopPx) break;
      t.mode = TouchMode::Pan;
      // Start from the origin so the content stays under the finger.
      scroll_target(app, t.start_y - y);
      t.last_move_ticks = SDL_GetTicks();
      break;
    case TouchMode::Pan: {
      if (!primary) break;
      scroll_target(app, -dy);
      const Uint32 now = SDL_GetTicks();
      const float dt =
          static_cast<float>(std::max<Uint32>(1, now - t.last_move_ticks)) /
          1000.f;
      // Bias toward the latest movement; a fling follows the final swipe.
      t.velocity = 0.8f * (-dy / dt) + 0.2f * t.velocity;
      t.last_move_ticks = now;
      break;
    }
    case TouchMode::Select:
    case TouchMode::Direct:
      if (primary)
        pointer_motion(app, static_cast<int>(x), static_cast<int>(y), false);
      break;
    case TouchMode::Pinch:
      if (t.pinch_dist > 0.f) {
        const float scale = finger_distance(t) / t.pinch_dist;
        const int target = static_cast<int>(
            std::lround(static_cast<float>(t.pinch_body_px) * scale));
        zoom_by(app, target - app.body_px);
      }
      break;
    case TouchMode::Idle:
    case TouchMode::Ignored:
      break;
  }
}

void touch_finger_up(App& app, const SDL_TouchFingerEvent& tf) {
  if (!is_screen_touch(tf)) return;
  Touch& t = app.touch;
  TouchPoint* f = find_finger(t, tf.fingerId);
  if (!f) return;
  const bool primary = f == &t.fingers[0];
  const int xi = static_cast<int>(f->x), yi = static_cast<int>(f->y);
  switch (t.mode) {
    case TouchMode::Pending:
      if (primary) {  // tap
        pointer_left_down(app, static_cast<int>(t.start_x),
                          static_cast<int>(t.start_y), false);
        pointer_left_up(app, static_cast<int>(t.start_x),
                        static_cast<int>(t.start_y));
      }
      break;
    case TouchMode::Pan:
      if (primary) {
        const Uint32 now = SDL_GetTicks();
        t.flinging = now - t.last_move_ticks <= kFlingMaxIdleMs &&
                     std::abs(t.velocity) >= kFlingMinSpeed;
        t.fling_ticks = now;
      }
      break;
    case TouchMode::Select:
    case TouchMode::Direct:
      if (primary) pointer_left_up(app, xi, yi);
      break;
    case TouchMode::Pinch:  // the remaining finger does nothing
      t.mode = TouchMode::Ignored;
      break;
    case TouchMode::Idle:
    case TouchMode::Ignored:
      break;
  }
  // A lifted primary ends single-finger gestures; ignore the other finger.
  if (primary) t.mode = TouchMode::Ignored;
  *f = t.fingers[t.count - 1];
  --t.count;
  if (t.count == 0) t.mode = TouchMode::Idle;
}

void update_touch(App& app) {
  Touch& t = app.touch;
  const Uint32 now = SDL_GetTicks();
  if (t.mode == TouchMode::Pending && !t.on_float &&
      now - t.start_ticks >= kLongPressMs)
    begin_select(app);
  if (!t.flinging) return;
  const float dt = static_cast<float>(now - t.fling_ticks) / 1000.f;
  if (dt <= 0.f) return;
  t.fling_ticks = now;
  if (!scroll_target(app, t.velocity * dt)) {
    t.flinging = false;
    return;
  }
  t.velocity *= std::exp(-dt / kFlingTau);
  if (std::abs(t.velocity) < kFlingStopSpeed) t.flinging = false;
}

}  // namespace xanatoo::app
