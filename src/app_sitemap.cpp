// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Sitemap panel: the path tree of the focused page's site (sitemap.hpp),
// loaded once per host on a worker thread. A tool view: it follows the
// focused pane and has no document of its own; "Page" opens the same tree
// as a `sitemap:host` document.

#include <algorithm>
#include <cmath>
#include <thread>

#include "xanatoo/app.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/sitemap.hpp"
#include "xanatoo/theme.hpp"

namespace xanatoo::app {

namespace {

constexpr const char* kPageButton = "Page";

// Web URL of the focused page: http(s) as is, native refs mapped back,
// a `sitemap:host` page counts as the site's front page.
std::string focused_web_url(App& app) {
  const DocBuffer* b = find_buf(app, focused_pane(app).buf);
  if (!b) return {};
  const std::string& p = b->path;
  if (xanatoo::is_sitemap_ref(p))
    return "https://" + xanatoo::sitemap_ref_host(p) + "/";
  if (!xanatoo::url_host(p).empty()) return p;
  return xanatoo::web_url_for(p);
}

SiteCache* find_site(App& app, const std::string& host) {
  for (SiteCache& c : app.sitemap.sites)
    if (c.host == host) return &c;
  return nullptr;
}

SiteCache* current_site(App& app) {
  return app.sitemap.host.empty() ? nullptr : find_site(app, app.sitemap.host);
}

void start_load(SiteCache& c) {
  c.error.clear();
  c.pending = std::make_shared<SitemapLoad>();
#if defined(XANATOO_WEB) && XANATOO_WEB
  // Detached like page fetches: a dropped cache entry orphans the state.
  std::thread(
      [host = c.host](std::shared_ptr<SitemapLoad> out) {
        out->tree = xanatoo::sitemap_fetch_tree(host, &out->error);
        out->done.store(true, std::memory_order_release);
      },
      c.pending)
      .detach();
#else
  c.pending->error = "site maps need build with -DXANATOO_WEB=ON";
  c.pending->done.store(true, std::memory_order_release);
#endif
}

void poll_loads(App& app) {
  for (SiteCache& c : app.sitemap.sites) {
    if (!c.pending || !c.pending->done.load(std::memory_order_acquire))
      continue;
    c.tree = std::move(c.pending->tree);
    c.error = c.pending->error;
    c.pending.reset();
    if (c.tree) {
      c.open.assign(c.tree->nodes.size(), 0);
      c.open[0] = 1;
    }
    // A new tree: the current page is revealed again.
    if (c.host == app.sitemap.host) app.sitemap.page.clear();
  }
}

void collect_rows(const SiteCache& c, int id, std::vector<int>& rows) {
  rows.push_back(id);
  if (!c.open[static_cast<size_t>(id)]) return;
  for (int k : c.tree->nodes[static_cast<size_t>(id)].kids)
    collect_rows(c, k, rows);
}

void rebuild_rows(App& app) {
  auto& rows = app.sitemap.rows;
  rows.clear();
  const SiteCache* c = current_site(app);
  if (c && c->tree && !c->tree->nodes.empty()) collect_rows(*c, 0, rows);
  app.sitemap.sel = std::clamp(app.sitemap.sel, 0,
                               std::max(0, static_cast<int>(rows.size()) - 1));
}

int selected_node(App& app) {
  const auto& rows = app.sitemap.rows;
  if (app.sitemap.sel < 0 || app.sitemap.sel >= static_cast<int>(rows.size()))
    return -1;
  return rows[static_cast<size_t>(app.sitemap.sel)];
}

void select_node(App& app, int node) {
  const auto& rows = app.sitemap.rows;
  auto it = std::find(rows.begin(), rows.end(), node);
  if (it != rows.end()) app.sitemap.sel = static_cast<int>(it - rows.begin());
}

void scroll_sel_into_view(App& app) {
  auto [cx, cy, cw, ch] = sitemap_client_rect(app);
  const float step = outline_row_step(app);
  const float top = static_cast<float>(app.sitemap.sel) * step;
  if (top < app.sm.scroll) app.sm.scroll = top;
  if (top + step > app.sm.scroll + static_cast<float>(ch))
    app.sm.scroll = top + step - static_cast<float>(ch);
  app.sm.scroll = std::clamp(app.sm.scroll, 0.f, sitemap_max_scroll(app));
}

// Expand the path to the focused page and select it.
void reveal_page(App& app, SiteCache& c, const std::string& url) {
  const int n = xanatoo::site_tree_find(*c.tree, url);
  if (n < 0) return;
  for (int p = c.tree->nodes[static_cast<size_t>(n)].parent; p >= 0;
       p = c.tree->nodes[static_cast<size_t>(p)].parent)
    c.open[static_cast<size_t>(p)] = 1;
  rebuild_rows(app);
  select_node(app, n);
  scroll_sel_into_view(app);
}

void set_open(App& app, int node, bool open) {
  SiteCache* c = current_site(app);
  if (!c || !c->tree || node < 0) return;
  c->open[static_cast<size_t>(node)] = open ? 1 : 0;
  rebuild_rows(app);
  select_node(app, node);
}

void open_node_page(App& app, int node) {
  const SiteCache* c = current_site(app);
  if (!c || !c->tree || node < 0) return;
  const std::string& url = c->tree->nodes[static_cast<size_t>(node)].url;
  if (url.empty()) {
    flash_status(app, "No page of its own — expand it with →");
    return;
  }
  // The selection already shows it; don't jump back on arrival.
  app.sitemap.page = url;
  navigate_to_url(app, url);
}

std::string status_line(App& app) {
  if (app.sitemap.host.empty()) return "No web page in this pane";
  const SiteCache* c = current_site(app);
  if (!c || c->pending) return "Reading the site map of " + app.sitemap.host + "…";
  if (!c->tree) return c->error.empty() ? "No site map" : c->error;
  return {};
}

}  // namespace

void toggle_sitemap(App& app) {
  app.sitemap.shown = !app.sitemap.shown;
  if (!app.sitemap.shown) {
    if (app.key_focus == KeyFocus::Sitemap) app.key_focus = KeyFocus::Document;
    return;
  }
  app.key_focus = KeyFocus::Sitemap;
  // Reopening retries failed hosts and reveals the current page again.
  auto& sites = app.sitemap.sites;
  sites.erase(std::remove_if(sites.begin(), sites.end(),
                             [](const SiteCache& c) {
                               return !c.tree && !c.pending;
                             }),
              sites.end());
  app.sitemap.page.clear();
}

void update_sitemap(App& app) {
  if (!app.sitemap.shown) return;
  poll_loads(app);
  const std::string url = focused_web_url(app);
  const std::string host = xanatoo::url_host(url);
  if (host != app.sitemap.host) {
    app.sitemap.host = host;
    app.sitemap.page.clear();
    app.sitemap.sel = 0;
    app.sm.scroll = 0.f;
  }
  if (host.empty()) {
    app.sitemap.rows.clear();
    return;
  }
  SiteCache* c = find_site(app, host);
  if (!c) {
    app.sitemap.sites.emplace_back();
    c = &app.sitemap.sites.back();
    c->host = host;
    start_load(*c);
  }
  rebuild_rows(app);
  if (c->tree && url != app.sitemap.page) {
    app.sitemap.page = url;
    reveal_page(app, *c, url);
  }
}

std::tuple<int, int, int, int> sitemap_client_rect(App& app) {
  return {app.sm.x + help_pad, app.sm.y + help_title_h + 4,
          app.sm.w - help_pad * 2, app.sm.h - help_title_h - help_pad - 4};
}

float sitemap_max_scroll(App& app) {
  auto [cx, cy, cw, ch] = sitemap_client_rect(app);
  const float content_h =
      static_cast<float>(app.sitemap.rows.size()) * outline_row_step(app) + 8.f;
  return std::max(0.f, content_h - static_cast<float>(ch));
}

void click_sitemap(App& app, int mx, int my) {
  const SDL_Rect outer{app.sm.x, app.sm.y, app.sm.w, app.sm.h};
  if (my < app.sm.y + help_title_h) {
    const SDL_Rect b = title_button_rect(app, outer, kPageButton);
    if (mx >= b.x && mx < b.x + b.w) open_sitemap_page(app);
    return;
  }
  auto [cx, cy, cw, ch] = sitemap_client_rect(app);
  if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;
  const float step = outline_row_step(app);
  const int row =
      static_cast<int>((static_cast<float>(my - cy) + app.sm.scroll) / step);
  if (row < 0 || row >= static_cast<int>(app.sitemap.rows.size())) return;
  app.sitemap.sel = row;
  const SiteCache* c = current_site(app);
  const int node = app.sitemap.rows[static_cast<size_t>(row)];
  const auto& n = c->tree->nodes[static_cast<size_t>(node)];
  const OutlineRowGeom g = outline_row_geom(app, n.depth);
  const int local_x = mx - cx;
  const bool on_twist = local_x < g.twist_x + g.twist_w + 2;
  // Twisty (or a row without a page of its own) folds; text opens.
  if (!n.kids.empty() && (on_twist || n.url.empty()))
    set_open(app, node, !c->open[static_cast<size_t>(node)]);
  else
    open_node_page(app, node);
}

bool sitemap_key(App& app, SDL_Keycode sym) {
  SiteCache* c = current_site(app);
  if (!c || !c->tree || app.sitemap.rows.empty()) return false;
  const int last = static_cast<int>(app.sitemap.rows.size()) - 1;
  const int node = selected_node(app);
  const auto& nodes = c->tree->nodes;
  switch (sym) {
    case SDLK_UP:
      app.sitemap.sel = std::max(0, app.sitemap.sel - 1);
      break;
    case SDLK_DOWN:
      app.sitemap.sel = std::min(last, app.sitemap.sel + 1);
      break;
    case SDLK_HOME:
      app.sitemap.sel = 0;
      break;
    case SDLK_END:
      app.sitemap.sel = last;
      break;
    case SDLK_LEFT:
      // Fold, or step out to the parent.
      if (!nodes[static_cast<size_t>(node)].kids.empty() &&
          c->open[static_cast<size_t>(node)])
        set_open(app, node, false);
      else if (nodes[static_cast<size_t>(node)].parent >= 0)
        select_node(app, nodes[static_cast<size_t>(node)].parent);
      break;
    case SDLK_RIGHT:
      // Unfold, or step into the first child.
      if (!nodes[static_cast<size_t>(node)].kids.empty()) {
        if (!c->open[static_cast<size_t>(node)])
          set_open(app, node, true);
        else
          select_node(app, nodes[static_cast<size_t>(node)].kids.front());
      }
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      open_node_page(app, node);
      break;
    default:
      return false;
  }
  scroll_sel_into_view(app);
  return true;
}

void open_sitemap_page(App& app) {
  if (app.sitemap.host.empty()) {
    flash_status(app, "Site map: no web page in this pane");
    return;
  }
  navigate_to_url(app, std::string(xanatoo::kSitemapPrefix) + app.sitemap.host);
}

void draw_sitemap_panel(App& app) {
  if (!app.sitemap.shown) return;
  FloatWindow& w = app.sm;
  if (w.dock == Dock::None) {
    w.w = std::max(w.w, 200);
    w.h = std::max(w.h, 140);
  }
  namespace th = xanatoo::theme;
  const th::Palette& pal = th::palette();
  xanatoo::Font& font = app.ui_font;
  xanatoo::Font& bold =
      app.ui_font_bold.loaded() ? app.ui_font_bold : app.ui_font;
  const bool keys = panel_has_keys(app, KeyFocus::Sitemap);
  const SDL_Rect outer{w.x, w.y, w.w, w.h};
  const SDL_Rect pb = title_button_rect(app, outer, kPageButton);
  const std::string title =
      app.sitemap.host.empty() ? "Site map" : "Site map — " + app.sitemap.host;
  draw_floating_frame(app, outer, title, keys, w.dock == Dock::None,
                      outer.x + outer.w - float_close_w - pb.x);
  draw_title_button(app, pb, kPageButton, false);

  auto [cx, cy, cw, ch] = sitemap_client_rect(app);
  w.scroll = std::min(w.scroll, sitemap_max_scroll(app));
  SDL_Rect clip{cx, cy, cw, ch};
  SDL_RenderSetClipRect(app.sdl, &clip);
  const std::string status = status_line(app);
  if (!status.empty()) {
    // Word-wrapped: errors carry the URL and HTTP status.
    float y = static_cast<float>(cy + 6 + font.ascender());
    std::string line;
    size_t i = 0;
    while (i <= status.size()) {
      size_t sp = status.find(' ', i);
      if (sp == std::string::npos) sp = status.size();
      const std::string word = status.substr(i, sp - i);
      const std::string next = line.empty() ? word : line + " " + word;
      if (!line.empty() && font.measure(next) > cw - 16) {
        app.renderer.draw_text(font, static_cast<float>(cx + 8), y, line,
                               pal.text_dim.r, pal.text_dim.g, pal.text_dim.b);
        y += static_cast<float>(font.line_height());
        line = word;
      } else {
        line = next;
      }
      i = sp + 1;
    }
    app.renderer.draw_text(font, static_cast<float>(cx + 8), y,
                           fit_text(font, line, cw - 16), pal.text_dim.r,
                           pal.text_dim.g, pal.text_dim.b);
    SDL_RenderSetClipRect(app.sdl, nullptr);
    return;
  }
  const SiteCache& c = *current_site(app);
  const auto& nodes = c.tree->nodes;
  const int here = xanatoo::site_tree_find(*c.tree, app.sitemap.page);
  const float step = outline_row_step(app);
  const bool scrolls = sitemap_max_scroll(app) > 0.f;
  const int text_right = cw - (scrolls ? 16 : 4);
  // Row i spans [cy + i·step − scroll, +step) — click_sitemap relies on it.
  float row_y = static_cast<float>(cy) - w.scroll;
  for (int i = 0; i < static_cast<int>(app.sitemap.rows.size());
       ++i, row_y += step) {
    if (row_y > static_cast<float>(cy + ch)) break;
    if (row_y + step < static_cast<float>(cy)) continue;
    const int id = app.sitemap.rows[static_cast<size_t>(i)];
    const xanatoo::SiteNode& n = nodes[static_cast<size_t>(id)];
    const bool sel = i == app.sitemap.sel;
    const bool lit = sel && keys;
    const int ry = static_cast<int>(row_y);
    const int rh = static_cast<int>(step);
    // No number column: indent from the left edge.
    OutlineRowGeom g = outline_row_geom(app, n.depth + 1);
    const int shift = g.num_right;
    g.twist_x -= shift;
    g.text_x -= shift;
    if (sel)
      th::fill(app.sdl, SDL_Rect{cx + 1, ry, text_right - 1, rh},
               keys ? pal.accent : pal.face_dark);
    const th::Rgb ink = lit ? pal.highlight : pal.text;
    const th::Rgb dim = lit ? pal.accent_soft : pal.text_dim;
    xanatoo::Font& f = id == here || id == 0 ? bold : font;
    const float baseline =
        static_cast<float>(ry + (rh - f.line_height()) / 2 + f.ascender());
    if (!n.kids.empty()) {
      const bool open = c.open[static_cast<size_t>(id)];
      const int tcx = cx + g.twist_x + g.twist_w / 2;
      const int tcy = ry + rh / 2;
      SDL_SetRenderDrawColor(app.sdl, dim.r, dim.g, dim.b, 255);
      const int t = std::max(4, g.twist_w * 2 / 7);
      for (int k = 0; k <= t; ++k) {
        if (!open)
          SDL_RenderDrawLine(app.sdl, tcx - t / 2 + k, tcy - t + k,
                             tcx - t / 2 + k, tcy + t - k);
        else
          SDL_RenderDrawLine(app.sdl, tcx - t + k, tcy - t / 2 + k,
                             tcx + t - k, tcy - t / 2 + k);
      }
    }
    // Page count of a directory, right-aligned.
    int count_w = 0;
    if (!n.kids.empty()) {
      const std::string count = std::to_string(n.pages);
      count_w = font.measure(count) + 8;
      app.renderer.draw_text(
          font, static_cast<float>(cx + text_right - count_w + 4), baseline,
          count, dim.r, dim.g, dim.b);
    }
    const std::string label = id == 0 ? c.host : n.label;
    // A directory without its own page is not a link: dimmed.
    const th::Rgb tink = n.url.empty() ? dim : ink;
    app.renderer.draw_text(
        f, static_cast<float>(cx + g.text_x), baseline,
        fit_text(f, label, text_right - g.text_x - count_w - 4), tink.r,
        tink.g, tink.b);
  }
  SDL_RenderSetClipRect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll,
                        sitemap_max_scroll(app));
}

}  // namespace xanatoo::app
