// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Fixed-layout pages drawn from thumtoo tiles (docs/FIXED_LAYOUT.md).
//
// thumtoo's LOD layer does the planning and request bookkeeping: one
// TileLoader per page URI (request state, shared by all views), one
// TileSession per (view, page) that turns the visible part of the page and
// the zoom into demand and a draw plan, and the process-wide TileScheduler
// that issues requests. This file only feeds viewports in, pumps the
// scheduler from the frame loop and turns ready cells into SDL textures.
//
// Pages are painted over the pane raster every frame (the raster holds the
// sheet and frame only), so an arriving tile never invalidates the raster.

#include "xanatoo/app.hpp"

#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO

#include "xanatoo/fixed_layout.hpp"

#include <thumtoo/client.hpp>
#include <thumtoo/djvu.hpp>
#include <thumtoo/lod/client_tile_backend.hpp>
#include <thumtoo/lod/tile_loader.hpp>
#include <thumtoo/lod/tile_scheduler.hpp>
#include <thumtoo/lod/tile_session.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>

namespace lod = thumtoo::lod;

namespace xanatoo::app {

namespace {

// Frames a source / view / texture may go unused before it is dropped.
constexpr uint64_t kTextureIdleFrames = 90;
constexpr uint64_t kViewIdleFrames = 300;
// Finest live scale for PDF pages: 576 dpi, thumtoo's durable floor.
constexpr int kPdfMinScale = -2;
constexpr int64_t kNoWake = std::numeric_limits<int64_t>::max();

struct TileTexture {
  SDL_Texture* tex = nullptr;
  const void* pixels = nullptr;  // the cell bitmap it was uploaded from
  uint64_t used = 0;
};

struct PageSource {
  std::shared_ptr<lod::TileLoader> loader;
  std::map<lod::TileKey, TileTexture> textures;
  int min_scale = 0;
  uint64_t used = 0;
};

struct TileView {
  std::unique_ptr<lod::TileSession> session;
  uint64_t used = 0;
};

}  // namespace

struct PageTiles {
  std::shared_ptr<thumtoo::Client> client;
  std::unordered_map<std::string, PageSource> sources;
  std::map<std::pair<int, std::string>, TileView> views;  // (pane, uri)
  std::shared_ptr<std::atomic<int64_t>> wake_at =
      std::make_shared<std::atomic<int64_t>>(kNoWake);
  uint64_t frame = 0;
};

namespace {

void drop_texture(TileTexture& t) {
  if (t.tex) SDL_DestroyTexture(t.tex);
  t.tex = nullptr;
}

PageTiles* tiles(App& app) {
  if (!app.page_tiles) {
    auto pt = std::make_shared<PageTiles>();
    pt->client = page_client();
    if (!pt->client) return nullptr;
    // The hook may run on any thread: it only records when to pump.
    std::weak_ptr<std::atomic<int64_t>> wake = pt->wake_at;
    lod::TileScheduler::instance().set_wake_hook([wake](int64_t delay_ms) {
      auto w = wake.lock();
      if (!w) return;
      const int64_t at =
          lod::TileScheduler::instance().now_ms() + std::max<int64_t>(0, delay_ms);
      int64_t cur = w->load();
      while (at < cur && !w->compare_exchange_weak(cur, at)) {
      }
    });
    app.page_tiles = std::move(pt);
  }
  return app.page_tiles.get();
}

int min_scale_for(const std::string& uri) {
  // DjVu and archive members: the layout grid is the native pixel grid.
  if (auto dj = thumtoo::parse_djvu_uri(uri);
      dj && thumtoo::is_likely_djvu_path(dj->djvu_path))
    return 0;
  return thumtoo::is_pdf_page_uri(uri) ? kPdfMinScale : 0;
}

PageSource& source_for(PageTiles& pt, const PositionedImage& im) {
  PageSource& s = pt.sources[im.src];
  if (!s.loader) {
    auto backend = std::make_shared<lod::ClientTileBackend>(pt.client, im.src);
    s.loader = std::make_shared<lod::TileLoader>(backend);
    s.loader->set_content_size(im.page_w, im.page_h);
    s.min_scale = min_scale_for(im.src);
    lod::TileScheduler::instance().register_loader(s.loader);
  }
  return s;
}

lod::TileSession& session_for(PageTiles& pt, PageSource& s, int pi,
                              const PositionedImage& im) {
  TileView& v = pt.views[{pi, im.src}];
  if (!v.session) {
    v.session = std::make_unique<lod::TileSession>(s.loader);
    v.session->set_content_size(im.page_w, im.page_h, s.min_scale);
  }
  v.used = pt.frame;
  return *v.session;
}

SDL_Texture* texture_for(App& app, PageSource& s, const lod::TileKey& key,
                         const lod::TileCell& cell, uint64_t frame) {
  TileTexture& t = s.textures[key];
  t.used = frame;
  // A re-produced cell (retry, eviction and reload) has new pixels.
  if (t.tex && t.pixels == cell.bitmap.bytes.data()) return t.tex;
  drop_texture(t);
  const lod::TileBitmap& bm = cell.bitmap;
  if (!bm.valid() || (!bm.codec.empty() && bm.codec != "rgba8")) return nullptr;
  SDL_Surface* surf = xanatoo::sdl::rgba_surface_from(
      const_cast<std::uint8_t*>(bm.bytes.data()), bm.width, bm.height,
      bm.width * 4);
  if (!surf) return nullptr;
  t.tex = SDL_CreateTextureFromSurface(app.sdl, surf);
  xanatoo::sdl::destroy_surface(surf);
  t.pixels = bm.bytes.data();
  return t.tex;
}

}  // namespace

void pump_page_tiles(App& app) {
  if (!app.page_tiles) return;
  PageTiles& pt = *app.page_tiles;
  ++pt.frame;
  auto& sched = lod::TileScheduler::instance();
  if (sched.now_ms() >= pt.wake_at->load()) {
    pt.wake_at->store(kNoWake);
    sched.pump();
  }
  // Views not drawn lately give up their demand, then go.
  for (auto it = pt.views.begin(); it != pt.views.end();) {
    if (it->second.used + 1 < pt.frame) it->second.session->clear_demand();
    if (it->second.used + kViewIdleFrames < pt.frame)
      it = pt.views.erase(it);
    else
      ++it;
  }
  for (auto it = pt.sources.begin(); it != pt.sources.end();) {
    PageSource& s = it->second;
    for (auto t = s.textures.begin(); t != s.textures.end();) {
      if (t->second.used + kTextureIdleFrames < pt.frame) {
        drop_texture(t->second);
        t = s.textures.erase(t);
      } else {
        ++t;
      }
    }
    if (s.used + kViewIdleFrames < pt.frame && s.loader.use_count() == 1) {
      s.loader->cancel_outstanding();
      it = pt.sources.erase(it);
    } else {
      ++it;
    }
  }
}

void draw_page_tiles(App& app, int pi, const SDL_Rect& clip,
                     float draw_scroll) {
  Pane& pane = app.panes[pi];
  bool any = false;
  for (const PositionedImage& im : pane.layout.images) any |= im.page_w > 0;
  if (!any) return;
  PageTiles* ptp = tiles(app);
  if (!ptp) return;
  PageTiles& pt = *ptp;
  for (const PositionedImage& im : pane.layout.images) {
    if (im.page_w <= 0 || im.page_h <= 0 || im.width <= 0.f) continue;
    const float sx = static_cast<float>(pane.x) + im.x;
    const float sy = im.y - draw_scroll;
    const float k = im.width / static_cast<float>(im.page_w);  // screen / content
    const float vx0 = std::max(sx, static_cast<float>(clip.x));
    const float vy0 = std::max(sy, static_cast<float>(clip.y));
    const float vx1 = std::min(sx + im.width, static_cast<float>(clip.x + clip.w));
    const float vy1 = std::min(sy + im.height, static_cast<float>(clip.y + clip.h));
    if (vx1 <= vx0 || vy1 <= vy0) continue;

    PageSource& src = source_for(pt, im);
    src.used = pt.frame;
    lod::TileSession& session = session_for(pt, src, pi, im);
    session.set_priority_class(pi == app.focus ? 3 : 2);
    lod::Viewport vp;
    vp.content_rect = {(vx0 - sx) / k, (vy0 - sy) / k, (vx1 - vx0) / k,
                       (vy1 - vy0) / k};
    vp.device_per_content = k;
    // Prefetch half a screen of the page around what is visible.
    session.set_viewport(vp, 0.5 * static_cast<double>(clip.h) / k);

    const lod::DrawPlan plan = session.draw_plan();
    // Coarser stand-ins first, so exact tiles end up on top.
    for (int pass = 0; pass < 2; ++pass) {
      for (const lod::DrawCommand& cmd : plan.commands) {
        const bool exact = cmd.kind == lod::DrawKind::ExactTile;
        if (cmd.kind != lod::DrawKind::CoarserTile && !exact) continue;
        if (exact != (pass == 1)) continue;
        const lod::TileCell* cell = session.find(cmd.src_key);
        if (!cell || !cell->ready()) continue;
        SDL_Texture* tex = texture_for(app, src, cmd.src_key, *cell, pt.frame);
        if (!tex) continue;
        src.loader->touch(cmd.src_key);
        // Rounded edges: neighbouring tiles share them, no seams.
        const int x0 = static_cast<int>(std::lround(sx + cmd.dst_content.x * k));
        const int y0 = static_cast<int>(std::lround(sy + cmd.dst_content.y * k));
        const int x1 = static_cast<int>(
            std::lround(sx + cmd.dst_content.right() * k));
        const int y1 = static_cast<int>(
            std::lround(sy + cmd.dst_content.bottom() * k));
        if (x1 <= x0 || y1 <= y0) continue;
        const SDL_Rect s{static_cast<int>(cmd.src_uv.x),
                         static_cast<int>(cmd.src_uv.y),
                         std::max(1, static_cast<int>(std::lround(cmd.src_uv.w))),
                         std::max(1, static_cast<int>(std::lround(cmd.src_uv.h)))};
        const SDL_Rect d{x0, y0, x1 - x0, y1 - y0};
        xanatoo::sdl::copy(app.sdl, tex, &s, &d);
      }
    }
  }
}

void release_page_tiles(App& app) {
  if (!app.page_tiles) return;
  for (auto& [uri, s] : app.page_tiles->sources) {
    for (auto& [key, t] : s.textures) drop_texture(t);
    s.textures.clear();
    if (s.loader) s.loader->cancel_outstanding();
  }
  app.page_tiles->views.clear();
  app.page_tiles->sources.clear();
  app.page_tiles.reset();
}

}  // namespace xanatoo::app

#else  // no thumtoo: pages show their sheet only

namespace xanatoo::app {
void pump_page_tiles(App&) {}
void draw_page_tiles(App&, int, const SDL_Rect&, float) {}
void release_page_tiles(App&) {}
}  // namespace xanatoo::app

#endif
