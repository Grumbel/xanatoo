// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Page panel: the navigation and side bars of the focused web page
// (Chrome nodes `margin_chrome` accepts), shown in a dockable, resizable
// tool window like the heading outline instead of in the text. A tool view
// of the focused pane: no document of its own. While it is open the panes
// leave those regions out (`LayoutEngine::set_hide_panel_chrome`).

#include <algorithm>

#include "xanatoo/app.hpp"
#include "xanatoo/editor.hpp"
#include "xanatoo/layout.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

constexpr size_t kMaxRows = 3000;

std::string trimmed(std::string s) {
  std::string_view v = s;
  xanatoo::trim_ascii(v);
  return std::string(v);
}

std::string capitalised(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
    s[0] = static_cast<char>(s[0] - 'a' + 'A');
  return s;
}

void collect(const xanatoo::Document& doc, xanatoo::NodeId id, int depth,
             std::vector<PageRow>& rows) {
  if (rows.size() >= kMaxRows || id == xanatoo::INVALID_NODE ||
      id >= doc.next_id)
    return;
  const xanatoo::Node& n = doc.get(id);
  if (n.id != id) return;
  using K = xanatoo::NodeKind;
  const bool group = n.kind == K::Header || n.kind == K::Heading;
  bool has_link = false;
  for (const xanatoo::InlineSpan& sp : n.spans)
    if (sp.kind == xanatoo::InlineKind::Link && !sp.href.empty())
      has_link = true;
  if (has_link) {
    for (const xanatoo::InlineSpan& sp : n.spans) {
      if (sp.kind != xanatoo::InlineKind::Link || sp.href.empty()) continue;
      std::string t = trimmed(sp.text);
      if (t.empty() || t == xanatoo::kInlineImageText) t = sp.href;
      rows.push_back({PageRow::Link, depth, std::move(t), sp.href});
    }
  } else if (!n.spans.empty()) {
    std::string t = trimmed(xanatoo::plain_text(doc, id));
    if (!t.empty())
      rows.push_back({group ? PageRow::Group : PageRow::Text, depth,
                      std::move(t), {}});
  }
  const int sub = n.kind == K::List ? depth + 1 : depth;
  for (xanatoo::NodeId c : n.children) collect(doc, c, sub, rows);
}

// The regions the panel shows, in document order. They may sit inside a
// bigger region (a page header that holds the navigation).
std::vector<xanatoo::NodeId> panel_regions(const xanatoo::Document& doc) {
  std::vector<xanatoo::NodeId> out;
  if (doc.root == xanatoo::INVALID_NODE) return out;
  std::vector<xanatoo::NodeId> stack{doc.root};
  while (!stack.empty()) {
    const xanatoo::NodeId id = stack.back();
    stack.pop_back();
    if (id == xanatoo::INVALID_NODE || id >= doc.next_id) continue;
    const xanatoo::Node& n = doc.get(id);
    if (n.id != id) continue;
    if (xanatoo::margin_chrome(doc, n)) {
      out.push_back(id);
      continue;
    }
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
      stack.push_back(*it);
  }
  return out;
}

void rebuild(App& app) {
  PagePanel& pg = app.page;
  Pane& p = focused_pane(app);
  const xanatoo::Document& doc = doc_of(app, p);
  const uint64_t key = (static_cast<uint64_t>(p.buf) << 32) ^
                       (static_cast<uint64_t>(doc.next_id) * 2654435761u) ^
                       doc.nodes.size();
  if (key == pg.key && !pg.rows.empty()) return;
  if (key == pg.key && pg.built) return;
  pg.key = key;
  pg.built = true;
  pg.rows.clear();
  for (xanatoo::NodeId region : panel_regions(doc)) {
    const xanatoo::Node& n = doc.get(region);
    pg.rows.push_back({PageRow::Label, 0, capitalised(n.summary), {}});
    const size_t before = pg.rows.size();
    for (xanatoo::NodeId c : n.children) collect(doc, c, 0, pg.rows);
    if (pg.rows.size() == before) pg.rows.pop_back();
  }
  pg.sel = std::clamp(pg.sel, 0, std::max(0, static_cast<int>(pg.rows.size()) - 1));
}

// Relayout of every pane: they show or leave out the regions.
void relayout_all(App& app) {
  for (int pi : live_views(app))
    app.panes[static_cast<size_t>(pi)].height_cache.clear();
  keep_views(app);
  app.needs_layout = true;
}

void scroll_sel_into_view(App& app) {
  auto [cx, cy, cw, ch] = page_client_rect(app);
  const float step = outline_row_step(app);
  const float top = static_cast<float>(app.page.sel) * step;
  if (top < app.pg.scroll) app.pg.scroll = top;
  if (top + step > app.pg.scroll + static_cast<float>(ch))
    app.pg.scroll = top + step - static_cast<float>(ch);
  app.pg.scroll = std::clamp(app.pg.scroll, 0.f, page_max_scroll(app));
}

// A row's link, as a click on it in the text would follow it.
void activate(App& app, const PageRow& r) {
  if (r.kind != PageRow::Link || r.href.empty()) return;
  const int pi = app.focus;
  Pane& p = app.panes[static_cast<size_t>(pi)];
  if (auto dest = xanatoo::jump_to_href(doc_of(app, p), r.href)) {
    expand_ancestors_for_node(app, dest->node);
    p.sel.focus = *dest;
    p.sel.anchor = *dest;
    scroll_caret_into_view(app, p);
    return;
  }
  if (r.href[0] == '#') return;
  std::string abs = r.href;
  const std::string base = link_base_url(app, pi);
  if (!xanatoo::is_gemini_url(abs) && !base.empty()) {
    const std::string resolved = xanatoo::resolve_gemini_url(base, abs);
    if (!resolved.empty()) abs = resolved;
  }
  if (is_in_app_remote_url(abs)) {
    open_address(app, abs);
  } else if (is_inapp_doc_href(app, abs)) {
    open_address(app, resolve_doc_href(app, abs));
  } else {
    flash_status(app, "Opening " + r.href);
    xanatoo::open_external_url(r.href);
  }
}

}  // namespace

bool page_has_navigation(App& app) {
  return !panel_regions(doc_of(app, focused_pane(app))).empty();
}

void toggle_page_panel(App& app) {
  app.page.shown = !app.page.shown;
  app.page.built = false;
  if (app.page.shown) {
    app.key_focus = KeyFocus::Page;
    app.pg.scroll = 0.f;
    app.page.sel = 0;
  } else if (app.key_focus == KeyFocus::Page) {
    app.key_focus = KeyFocus::Document;
  }
  relayout_all(app);
}

void update_page_panel(App& app) {
  app.layout_engine.set_hide_panel_chrome(app.page.shown);
  if (!app.page.shown) return;
  rebuild(app);
}

std::tuple<int, int, int, int> page_client_rect(App& app) {
  return window_client_rect(app.pg.x, app.pg.y, app.pg.w, app.pg.h);
}

float page_max_scroll(App& app) {
  auto [cx, cy, cw, ch] = page_client_rect(app);
  const float content_h =
      static_cast<float>(app.page.rows.size()) * outline_row_step(app) + 8.f;
  return std::max(0.f, content_h - static_cast<float>(ch));
}

void click_page(App& app, int mx, int my) {
  auto [cx, cy, cw, ch] = page_client_rect(app);
  if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;
  const int row = static_cast<int>(
      (static_cast<float>(my - cy) + app.pg.scroll) / outline_row_step(app));
  if (row < 0 || row >= static_cast<int>(app.page.rows.size())) return;
  app.page.sel = row;
  activate(app, app.page.rows[static_cast<size_t>(row)]);
}

bool page_key(App& app, SDL_Keycode sym) {
  const auto& rows = app.page.rows;
  if (rows.empty()) return false;
  const int last = static_cast<int>(rows.size()) - 1;
  // Up / Down skip nothing: labels and text rows can be selected too, so
  // the whole list can be read with the keys.
  switch (sym) {
    case SDLK_UP:
      app.page.sel = std::max(0, app.page.sel - 1);
      break;
    case SDLK_DOWN:
      app.page.sel = std::min(last, app.page.sel + 1);
      break;
    case SDLK_HOME:
      app.page.sel = 0;
      break;
    case SDLK_END:
      app.page.sel = last;
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      activate(app, rows[static_cast<size_t>(app.page.sel)]);
      break;
    default:
      return false;
  }
  scroll_sel_into_view(app);
  return true;
}

void draw_page_panel(App& app) {
  if (!app.page.shown) return;
  FloatWindow& w = app.pg;
  if (w.dock == Dock::None) {
    w.w = std::max(w.w, 200);
    w.h = std::max(w.h, 120);
  }
  namespace th = xanatoo::theme;
  const th::Palette& pal = th::palette();
  xanatoo::Font& font = app.ui_font;
  xanatoo::Font& bold =
      app.ui_font_bold.loaded() ? app.ui_font_bold : app.ui_font;
  const bool keys = panel_has_keys(app, KeyFocus::Page);
  const SDL_Rect outer{w.x, w.y, w.w, w.h};
  draw_floating_frame(app, outer, "Page", keys, w.dock == Dock::None, 0,
                      w.dock == Dock::Max);

  auto [cx, cy, cw, ch] = page_client_rect(app);
  w.scroll = std::min(w.scroll, page_max_scroll(app));
  SDL_Rect clip{cx, cy, cw, ch};
  xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  if (app.page.rows.empty()) {
    app.renderer.draw_text(
        font, static_cast<float>(cx + 8),
        static_cast<float>(cy + 6 + font.ascender()),
        fit_text(font, "No navigation on this page", cw - 16),
        pal.text_dim.r, pal.text_dim.g, pal.text_dim.b);
    xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
    return;
  }
  const float step = outline_row_step(app);
  const bool scrolls = page_max_scroll(app) > 0.f;
  const int text_right = cw - (scrolls ? 16 : 4);
  const int indent_px = std::max(10, font.line_height() / 2);
  float row_y = static_cast<float>(cy) - w.scroll;
  for (int i = 0; i < static_cast<int>(app.page.rows.size());
       ++i, row_y += step) {
    if (row_y > static_cast<float>(cy + ch)) break;
    if (row_y + step < static_cast<float>(cy)) continue;
    const PageRow& r = app.page.rows[static_cast<size_t>(i)];
    const bool sel = i == app.page.sel;
    const bool lit = sel && keys;
    const int ry = static_cast<int>(row_y);
    const int rh = static_cast<int>(step);
    if (sel)
      th::fill(app.sdl, SDL_Rect{cx + 1, ry, text_right - 1, rh},
               keys ? pal.accent : pal.face_dark);
    xanatoo::Font& f =
        r.kind == PageRow::Label || r.kind == PageRow::Group ? bold : font;
    th::Rgb ink = lit ? pal.highlight : pal.text;
    if (r.kind == PageRow::Text || r.kind == PageRow::Label)
      ink = lit ? pal.accent_soft : pal.text_dim;
    else if (r.kind == PageRow::Link && !sel)
      ink = pal.accent;  // links read as links
    const int x = cx + 6 + (r.kind == PageRow::Label ? 0 : indent_px) +
                  r.depth * indent_px;
    const float baseline =
        static_cast<float>(ry + (rh - f.line_height()) / 2 + f.ascender());
    app.renderer.draw_text(f, static_cast<float>(x), baseline,
                           fit_text(f, r.text, cx + text_right - x - 4),
                           ink.r, ink.g, ink.b);
  }
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll, page_max_scroll(app));
}

}  // namespace xanatoo::app
