// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Collections: view-local layout / sort / card size per Collection node,
// and the lightbox — a fullscreen viewer over the whole window that steps
// through a collection's images in view order.

#include <algorithm>
#include <filesystem>

#include "xanatoo/app.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

using xanatoo::CollectionLayout;
using xanatoo::CollectionSort;
using xanatoo::CollectionView;
using xanatoo::NodeId;

NodeId enclosing_collection(const xanatoo::Document& doc, NodeId id) {
  while (id != xanatoo::INVALID_NODE) {
    const xanatoo::Node& n = doc.get(id);
    if (n.id != id) return xanatoo::INVALID_NODE;
    if (n.kind == xanatoo::NodeKind::Collection) return id;
    id = n.parent;
  }
  return xanatoo::INVALID_NODE;
}

CollectionView& view_of(App& app, const xanatoo::Document& doc, NodeId coll) {
  auto it = app.collection_views.find(coll);
  if (it == app.collection_views.end())
    it = app.collection_views
             .emplace(coll, xanatoo::default_collection_view(doc.get(coll)))
             .first;
  return it->second;
}

void open_lightbox(App& app, Pane& pane, NodeId coll, NodeId image) {
  auto& doc = doc_of(app, pane);
  const auto items = xanatoo::collection_items(
      doc, coll, view_of(app, doc, coll).sort);
  Lightbox& lb = app.lightbox;
  lb.items.clear();
  lb.index = 0;
  for (const auto& it : items) {
    if (it.src.empty()) continue;
    if (it.image == image) lb.index = static_cast<int>(lb.items.size());
    lb.items.push_back({it.src, it.caption});
  }
  if (lb.items.empty()) {
    flash_status(app, "Collection has no images");
    return;
  }
  lb.base = base_of(app, pane);
  lb.open = true;
}

// Inline image (no collection): every image of the document, in order.
void open_image_lightbox(App& app, Pane& pane, NodeId image) {
  const auto& doc = doc_of(app, pane);
  Lightbox& lb = app.lightbox;
  lb.items.clear();
  lb.index = 0;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    const NodeId id = stack.back();
    stack.pop_back();
    if (id == xanatoo::INVALID_NODE) continue;
    const xanatoo::Node& n = doc.get(id);
    if (n.kind == xanatoo::NodeKind::Image && !n.src.empty()) {
      if (id == image) lb.index = static_cast<int>(lb.items.size());
      lb.items.push_back({n.src, n.alt});
    }
    stack.insert(stack.end(), n.children.rbegin(), n.children.rend());
  }
  if (lb.items.empty()) return;
  lb.base = base_of(app, pane);
  lb.open = true;
}

void reset_zoom(Lightbox& lb) {
  lb.scale = 0.f;
  lb.pan_x = lb.pan_y = 0.f;
  lb.drag = false;
}

void step(Lightbox& lb, int delta) {
  const int n = static_cast<int>(lb.items.size());
  if (n == 0) return;
  const int old = lb.index;
  lb.index = std::clamp(lb.index + delta, 0, n - 1);
  if (lb.index != old) reset_zoom(lb);
}

void toggle_window_fullscreen(App& app) {
  xanatoo::sdl::set_fullscreen(app.window,
                            !xanatoo::sdl::fullscreen(app.window));
}

}  // namespace

NodeId current_collection(App& app) {
  auto& pane = focused_pane(app);
  auto& doc = doc_of(app, pane);
  if (app.collection_target != xanatoo::INVALID_NODE &&
      enclosing_collection(doc, app.collection_target) ==
          app.collection_target)
    return app.collection_target;
  return enclosing_collection(doc, pane.sel.focus.node);
}

void collection_command(App& app, const std::string& arg) {
  auto& pane = focused_pane(app);
  auto& doc = doc_of(app, pane);
  const NodeId coll = current_collection(app);
  if (coll == xanatoo::INVALID_NODE) {
    flash_status(app, "Not in a collection");
    return;
  }
  if (arg == "view") {
    NodeId first = xanatoo::INVALID_NODE;
    open_lightbox(app, pane, coll, first);
    return;
  }
  CollectionView& v = view_of(app, doc, coll);
  if (arg == "grid") v.layout = CollectionLayout::Grid;
  else if (arg == "list") v.layout = CollectionLayout::List;
  else if (arg == "full") v.layout = CollectionLayout::Full;
  else if (arg == "small") v.thumb_px = 112.f;
  else if (arg == "medium") v.thumb_px = 160.f;
  else if (arg == "large") v.thumb_px = 240.f;
  else if (arg == "sort-doc") v.sort = CollectionSort::Document;
  else if (arg == "sort-name") v.sort = CollectionSort::Name;
  else if (arg == "sort-name-desc") v.sort = CollectionSort::NameReverse;
  else return;
  // Card heights changed: stale hints would misplace skipped blocks.
  keep_views(app);
  for (auto& p : app.panes) p.height_cache.clear();
  app.needs_layout = true;
}

bool click_collection_card(App& app, int pi, float lx, float my) {
  Pane& p = app.panes[pi];
  for (const auto& im : p.layout.images) {
    if (im.thumb <= 0 || im.in_text) continue;  // inline: a link (editor.cpp)
    const float y0 = im.y - p.scroll_y;
    if (lx >= im.x && lx <= im.x + im.width && my >= y0 &&
        my <= y0 + im.height) {
      if (im.group != xanatoo::INVALID_NODE)
        open_lightbox(app, p, im.group, im.node);
      else
        open_image_lightbox(app, p, im.node);
      return app.lightbox.open;
    }
  }
  return false;
}

bool open_link_image(App& app, int pi, const std::string& href) {
  if (!xanatoo::is_image_url(href)) return false;
  // Relative links of a Gemini page are gemini:// resources.
  if (!link_base_url(app, pi).empty() && !xanatoo::starts_with_ci(href, "http"))
    return false;
  Pane& pane = app.panes[pi];
  const auto& doc = doc_of(app, pane);
  Lightbox& lb = app.lightbox;
  lb.items.clear();
  lb.index = 0;
  bool found = false;
  std::vector<NodeId> stack{doc.root};
  while (!stack.empty()) {
    const NodeId id = stack.back();
    stack.pop_back();
    if (id == xanatoo::INVALID_NODE) continue;
    const xanatoo::Node& n = doc.get(id);
    for (const auto& s : n.spans) {
      // Image links, and inline images (their picture, not their link).
      const bool inline_img = s.kind == xanatoo::InlineKind::Image;
      const std::string& src = inline_img ? s.src : s.href;
      if ((!inline_img && s.kind != xanatoo::InlineKind::Link) ||
          !xanatoo::is_image_url(src))
        continue;
      if (std::any_of(lb.items.begin(), lb.items.end(),
                      [&](const LightboxItem& it) { return it.src == src; }))
        continue;
      if (!found && src == href) {
        lb.index = static_cast<int>(lb.items.size());
        found = true;
      }
      // "[image: alt]" labels: the alt text; "[image]": the file name.
      std::string caption = inline_img ? s.alt : s.text;
      if (xanatoo::starts_with(caption, "[image: ") && caption.back() == ']')
        caption = caption.substr(8, caption.size() - 9);
      else if (xanatoo::starts_with(caption, "[image"))
        caption.clear();
      lb.items.push_back({src, std::move(caption)});
    }
    stack.insert(stack.end(), n.children.rbegin(), n.children.rend());
  }
  if (!found) {  // link from outside the spans (e.g. a Message header)
    lb.index = static_cast<int>(lb.items.size());
    lb.items.push_back({href, {}});
  }
  lb.base = base_of(app, pane);
  lb.open = true;
  return true;
}

void note_collection_target(App& app, int pi, float lx, float my) {
  Pane& p = app.panes[pi];
  app.collection_target = xanatoo::INVALID_NODE;
  for (const auto& im : p.layout.images) {
    if (im.group == xanatoo::INVALID_NODE) continue;
    const float y0 = im.y - p.scroll_y;
    if (lx >= im.x && lx <= im.x + im.width && my >= y0 &&
        my <= y0 + im.height) {
      app.collection_target = im.group;
      return;
    }
  }
}

// The viewer fills the client area; only Content only mode gives it the
// whole window (menus, toolbars and status line stay usable otherwise).
static SDL_Rect lightbox_rect(const App& app) {
  if (app.chrome_hidden) return {0, 0, app.win_w, app.win_h};
  const int top = static_cast<int>(app.ui_top);
  return {0, top, app.win_w, std::max(0, app.win_h - status_bar_h - top)};
}

// Edge zones of the viewer: narrow full-height sides step; the middle of the
// top strip closes the viewer.
enum class LbZone { None, Close, Prev, Next };
static int lightbox_side_width(const SDL_Rect& r) {
  return std::min(std::max(40, r.w / 8), std::max(1, r.w / 3));
}

static LbZone lightbox_zone(const App& app, int x, int y) {
  const SDL_Rect r = lightbox_rect(app);
  if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h)
    return LbZone::None;
  const int side_w = lightbox_side_width(r);
  if (x - r.x < side_w) return LbZone::Prev;
  if (x - r.x >= r.w - side_w) return LbZone::Next;
  if (y - r.y < std::max(48, r.h / 7)) return LbZone::Close;
  return LbZone::None;
}

// Where the picture lives: the viewer minus padding and the caption lines.
struct LbArea {
  int x, y, w, h;
  float cx() const { return x + w / 2.f; }
  float cy() const { return y + h / 2.f; }
};
static LbArea lightbox_area(const App& app) {
  const SDL_Rect all = lightbox_rect(app);
  const int line = const_cast<App&>(app).ui_font.line_height();
  const int pad = static_cast<int>(16.f * app.ui_scale);
  const int bottom = line * 2 + pad;
  return {all.x + pad, all.y + pad, std::max(1, all.w - 2 * pad),
          std::max(1, all.h - pad - bottom)};
}

static float lightbox_fit(const LbArea& a, const xanatoo::Image& img) {
  return std::min({static_cast<float>(a.w) / img.width,
                   static_cast<float>(a.h) / img.height, 1.f});
}

// Keep the picture covering the area when larger, centred when smaller.
static void lightbox_clamp_pan(Lightbox& lb, const LbArea& a, float w, float h) {
  const float mx = std::max(0.f, (w - a.w) / 2.f);
  const float my = std::max(0.f, (h - a.h) / 2.f);
  lb.pan_x = std::clamp(lb.pan_x, -mx, mx);
  lb.pan_y = std::clamp(lb.pan_y, -my, my);
}

// Zoom by `factor` around (px, py); scale <= fit goes back to fit.
static void lightbox_zoom(App& app, float factor, float px, float py) {
  Lightbox& lb = app.lightbox;
  if (lb.items.empty()) return;
  const xanatoo::Image& img =
      app.images.get(lb.items[static_cast<size_t>(lb.index)].src, lb.base);
  if (!img.ok || img.width <= 0 || img.height <= 0) return;
  const LbArea a = lightbox_area(app);
  const float fit = lightbox_fit(a, img);
  const float cur = lb.scale > 0.f ? lb.scale : fit;
  const float next = std::clamp(cur * factor, 0.02f, 64.f);
  if (next <= fit * 1.001f) {
    reset_zoom(lb);
    return;
  }
  const float f = next / cur;
  const float ox = px - (a.cx() + lb.pan_x), oy = py - (a.cy() + lb.pan_y);
  lb.pan_x = px - a.cx() - ox * f;
  lb.pan_y = py - a.cy() - oy * f;
  lb.scale = next;
  lightbox_clamp_pan(lb, a, img.width * next, img.height * next);
}

static bool in_rect(const SDL_Rect& r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

bool lightbox_touch_target(const App& app, float x, float y) {
  if (!app.lightbox.open) return false;
  const SDL_Rect r = lightbox_rect(app);
  return x >= static_cast<float>(r.x) && y >= static_cast<float>(r.y) &&
         x < static_cast<float>(r.x + r.w) &&
         y < static_cast<float>(r.y + r.h);
}

void lightbox_touch_zoom(App& app, float factor, float x, float y) {
  lightbox_zoom(app, factor, x, y);
}

bool lightbox_event(App& app, const SDL_Event& ev) {
  Lightbox& lb = app.lightbox;
  if (!lb.open) return false;
  using xanatoo::sdl::EventKind;
  const EventKind kind = xanatoo::sdl::event_kind(ev);
  // SDL mirrors the primary touch as mouse input; during a two-finger pinch
  // it must not also drag the image while the raw finger events zoom it.
  if (app.touch.mode == TouchMode::LightboxPinch && is_touch_mouse_event(ev))
    return true;
  // Pointer events outside the viewer belong to the menus and toolbars.
  {
    const SDL_Rect r = lightbox_rect(app);
    int px = 0, py = 0;
    bool pointer = true;
    switch (kind) {
      case EventKind::MouseButtonDown:
      case EventKind::MouseButtonUp:
        px = ev.button.x;
        py = ev.button.y;
        break;
      case EventKind::MouseMotion:
        px = ev.motion.x;
        py = ev.motion.y;
        break;
      case EventKind::MouseWheel:
        xanatoo::sdl::mouse_position(&px, &py);
        break;
      default:
        pointer = false;
    }
    if (pointer && !in_rect(r, px, py)) return false;
  }
  switch (kind) {
    case EventKind::KeyDown:
      switch (xanatoo::sdl::keycode(ev)) {
        case SDLK_ESCAPE:
        case SDLK_q:
          lb.open = false;
          break;
        case SDLK_RIGHT:
        case SDLK_DOWN:
        case SDLK_SPACE:
        case SDLK_PAGEDOWN:
        case SDLK_n:
          step(lb, 1);
          break;
        case SDLK_LEFT:
        case SDLK_UP:
        case SDLK_BACKSPACE:
        case SDLK_PAGEUP:
        case SDLK_p:
          step(lb, -1);
          break;
        case SDLK_HOME:
          lb.index = 0;
          break;
        case SDLK_END:
          lb.index = static_cast<int>(lb.items.size()) - 1;
          break;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
          lightbox_zoom(app, 1.25f, lightbox_area(app).cx(),
                        lightbox_area(app).cy());
          break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
          lightbox_zoom(app, 0.8f, lightbox_area(app).cx(),
                        lightbox_area(app).cy());
          break;
        case SDLK_0:
        case SDLK_KP_0:
          reset_zoom(lb);
          break;
        case SDLK_1:
        case SDLK_KP_1: {  // actual size
          const auto& im =
              app.images.get(lb.items[static_cast<size_t>(lb.index)].src, lb.base);
          if (im.ok && im.width > 0) {
            reset_zoom(lb);
            const LbArea a = lightbox_area(app);
            if (lightbox_fit(a, im) < 1.f)
              lightbox_zoom(app, 1.f / lightbox_fit(a, im), a.cx(), a.cy());
          }
          break;
        }
        case SDLK_f:
        case SDLK_F11:
          toggle_window_fullscreen(app);
          break;
        default:
          break;
      }
      return true;
    case EventKind::MouseButtonDown:
      if (ev.button.button == SDL_BUTTON_LEFT) {
        switch (lightbox_zone(app, ev.button.x, ev.button.y)) {
          case LbZone::Close: lb.open = false; break;
          case LbZone::Prev: step(lb, -1); break;
          case LbZone::Next: step(lb, 1); break;
          default: lb.drag = true; break;
        }
      }
      else
        lb.open = false;
      return true;
    case EventKind::MouseWheel: {
      int px = 0, py = 0;
      xanatoo::sdl::mouse_position(&px, &py);
      if (ev.wheel.y != 0)
        lightbox_zoom(app, ev.wheel.y > 0 ? 1.25f : 0.8f,
                      static_cast<float>(px), static_cast<float>(py));
      return true;
    }
    case EventKind::MouseMotion:
      if (lb.drag && lb.scale > 0.f) {
        lb.pan_x += static_cast<float>(ev.motion.xrel);
        lb.pan_y += static_cast<float>(ev.motion.yrel);
      }
      return true;
    case EventKind::MouseButtonUp:
      lb.drag = false;
      return true;
    case EventKind::GamepadButtonDown:
      switch (xanatoo::sdl::gamepad_event_button(ev)) {
        case xanatoo::sdl::button::DpadLeft:
        case xanatoo::sdl::button::LeftShoulder:
          step(lb, -1);
          break;
        case xanatoo::sdl::button::DpadRight:
        case xanatoo::sdl::button::RightShoulder:
        case xanatoo::sdl::button::A:
          step(lb, 1);
          break;
        case xanatoo::sdl::button::B:
        case xanatoo::sdl::button::Back:
          lb.open = false;
          break;
        default:
          break;
      }
      return true;
    case EventKind::TextInput:
    case EventKind::KeyUp:
    case EventKind::FingerDown:
    case EventKind::FingerUp:
    case EventKind::FingerMotion:
      return true;
    default:
      return false;  // quit, window, render resets keep their handlers
  }
}

void draw_lightbox(App& app) {
  Lightbox& lb = app.lightbox;
  if (!lb.open || lb.items.empty()) return;
  lb.index = std::clamp(lb.index, 0, static_cast<int>(lb.items.size()) - 1);
  const LightboxItem& item = lb.items[static_cast<size_t>(lb.index)];

  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(app.sdl, 18, 18, 20, 255);
  const SDL_Rect all = lightbox_rect(app);
  xanatoo::sdl::fill_rect(app.sdl, &all);

  xanatoo::Font& font = app.ui_font;
  const int line = font.line_height();
  const int pad = static_cast<int>(16.f * app.ui_scale);
  const int bottom = line * 2 + pad;
  const int avail_w = all.w - 2 * pad;
  const int bot = all.y + all.h;  // bottom edge of the viewer

  const xanatoo::Image& img = app.images.get(item.src, lb.base);
  std::string dims;
  if (img.ok && img.texture && img.width > 0 && img.height > 0) {
    const LbArea area = lightbox_area(app);
    const float sc = lb.scale > 0.f ? lb.scale : lightbox_fit(area, img);
    const float fw = img.width * sc, fh = img.height * sc;
    lightbox_clamp_pan(lb, area, fw, fh);
    const SDL_Rect clip{area.x, area.y, area.w, area.h};
    xanatoo::sdl::set_clip_rect(app.sdl, &clip);
    SDL_Rect dst{static_cast<int>(area.cx() + lb.pan_x - fw / 2),
                 static_cast<int>(area.cy() + lb.pan_y - fh / 2),
                 std::max(1, static_cast<int>(fw)),
                 std::max(1, static_cast<int>(fh))};
    xanatoo::sdl::copy(app.sdl, img.texture, nullptr, &dst);
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
    dims = std::to_string(img.width) + "×" + std::to_string(img.height) + " · " +
           std::to_string(static_cast<int>(sc * 100.f + 0.5f)) + "%";
  } else {
    const std::string msg =
        std::string(img.pending ? "Loading " : "Cannot load ") + item.src;
    app.renderer.draw_text(font, static_cast<float>(pad),
                           static_cast<float>(all.y + all.h / 2), msg, 220, 120,
                           120);
  }

  const std::string counter = std::to_string(lb.index + 1) + " / " +
                              std::to_string(lb.items.size());
  const std::string hint =
      "←/→ step · wheel / + − zoom · 0 fit · 1 100% · drag pan · F fullscreen · Esc close";
  const int base_y = bot - bottom + pad / 2 + font.ascender();
  std::string caption = item.caption.empty()
                            ? std::filesystem::path(item.src).filename().string()
                            : item.caption;
  const int counter_w = font.measure(counter);
  if (!dims.empty()) caption += "  (" + dims + ")";
  caption = fit_text(font, caption, avail_w - counter_w - pad);
  const int cap_w = font.measure(caption);
  app.renderer.draw_text(font, static_cast<float>(all.x + (all.w - cap_w) / 2),
                         static_cast<float>(base_y), caption, 235, 235, 235);
  app.renderer.draw_text(font, static_cast<float>(all.x + all.w - pad - counter_w),
                         static_cast<float>(base_y), counter, 160, 160, 165);
  const int hint_w = font.measure(hint);
  app.renderer.draw_text(font, static_cast<float>(all.x + (all.w - hint_w) / 2),
                         static_cast<float>(base_y + line), hint, 120, 120,
                         128);

  // Hover hints: the edge zone under the pointer shows its icon.
  int mx = 0, my = 0;
  xanatoo::sdl::mouse_position(&mx, &my);
  const LbZone z = lightbox_zone(app, mx, my);
  if (z != LbZone::None) {
    const int zh = std::max(48, all.h / 7);
    SDL_Rect zr;
    const char* glyph;
    if (z == LbZone::Close) {
      const int side_w = lightbox_side_width(all);
      zr = {all.x + side_w, all.y, all.w - 2 * side_w, zh};
      glyph = "✕";
    } else {
      const int side_w = lightbox_side_width(all);
      zr = {z == LbZone::Prev ? all.x : all.x + all.w - side_w,
            all.y, side_w, all.h};
      glyph = z == LbZone::Prev ? "◀" : "▶";
    }
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.sdl, 255, 255, 255, 22);
    xanatoo::sdl::fill_rect(app.sdl, &zr);
    const int d = static_cast<int>(40.f * app.ui_scale);
    const int cx = zr.x + zr.w / 2, cy = zr.y + zr.h / 2;
    SDL_Rect bg{cx - d / 2, cy - d / 2, d, d};
    SDL_SetRenderDrawColor(app.sdl, 0, 0, 0, 150);
    xanatoo::sdl::fill_rect(app.sdl, &bg);
    const std::string g = glyph;
    app.renderer.draw_text(
        font, static_cast<float>(cx - font.measure(g) / 2),
        static_cast<float>(cy - line / 2 + font.ascender()), g, 240, 240, 240);
  }
}

}  // namespace xanatoo::app
