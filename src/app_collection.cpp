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

void step(Lightbox& lb, int delta) {
  const int n = static_cast<int>(lb.items.size());
  if (n == 0) return;
  lb.index = std::clamp(lb.index + delta, 0, n - 1);
}

void toggle_window_fullscreen(App& app) {
  const bool full =
      (SDL_GetWindowFlags(app.window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
  SDL_SetWindowFullscreen(app.window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
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
    if (im.thumb <= 0) continue;
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

bool lightbox_event(App& app, const SDL_Event& ev) {
  Lightbox& lb = app.lightbox;
  if (!lb.open) return false;
  switch (ev.type) {
    case SDL_KEYDOWN:
      switch (ev.key.keysym.sym) {
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
        case SDLK_f:
        case SDLK_F11:
          toggle_window_fullscreen(app);
          break;
        default:
          break;
      }
      return true;
    case SDL_MOUSEBUTTONDOWN:
      if (ev.button.button == SDL_BUTTON_LEFT)
        step(lb, ev.button.x < app.win_w / 3 ? -1 : 1);
      else
        lb.open = false;
      return true;
    case SDL_MOUSEWHEEL:
      step(lb, ev.wheel.y > 0 ? -1 : 1);
      return true;
    case SDL_CONTROLLERBUTTONDOWN:
      switch (ev.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
          step(lb, -1);
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        case SDL_CONTROLLER_BUTTON_A:
          step(lb, 1);
          break;
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_BACK:
          lb.open = false;
          break;
        default:
          break;
      }
      return true;
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION:
    case SDL_TEXTINPUT:
    case SDL_KEYUP:
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
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
  SDL_Rect all{0, 0, app.win_w, app.win_h};
  SDL_RenderFillRect(app.sdl, &all);

  xanatoo::Font& font = app.ui_font;
  const int line = font.line_height();
  const int pad = static_cast<int>(16.f * app.ui_scale);
  const int bottom = line * 2 + pad;
  const int avail_w = app.win_w - 2 * pad;
  const int avail_h = app.win_h - pad - bottom;

  const xanatoo::Image& img = app.images.get(item.src, lb.base);
  if (img.ok && img.texture && img.width > 0 && img.height > 0) {
    const float sc = std::min({static_cast<float>(avail_w) / img.width,
                               static_cast<float>(avail_h) / img.height, 1.f});
    const int w = static_cast<int>(img.width * sc);
    const int h = static_cast<int>(img.height * sc);
    SDL_Rect dst{(app.win_w - w) / 2, pad + (avail_h - h) / 2, w, h};
    SDL_RenderCopy(app.sdl, img.texture, nullptr, &dst);
  } else {
    const std::string msg =
        std::string(img.pending ? "Loading " : "Cannot load ") + item.src;
    app.renderer.draw_text(font, static_cast<float>(pad),
                           static_cast<float>(app.win_h / 2), msg, 220, 120,
                           120);
  }

  const std::string counter = std::to_string(lb.index + 1) + " / " +
                              std::to_string(lb.items.size());
  const std::string hint = "←/→ step · F fullscreen · Esc close";
  const int base_y = app.win_h - bottom + pad / 2 + font.ascender();
  std::string caption = item.caption.empty()
                            ? std::filesystem::path(item.src).filename().string()
                            : item.caption;
  const int counter_w = font.measure(counter);
  caption = fit_text(font, caption, avail_w - counter_w - pad);
  const int cap_w = font.measure(caption);
  app.renderer.draw_text(font, static_cast<float>((app.win_w - cap_w) / 2),
                         static_cast<float>(base_y), caption, 235, 235, 235);
  app.renderer.draw_text(font, static_cast<float>(app.win_w - pad - counter_w),
                         static_cast<float>(base_y), counter, 160, 160, 165);
  const int hint_w = font.measure(hint);
  app.renderer.draw_text(font, static_cast<float>((app.win_w - hint_w) / 2),
                         static_cast<float>(base_y + line), hint, 120, 120,
                         128);
}

}  // namespace xanatoo::app
