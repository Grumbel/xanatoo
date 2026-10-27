// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Footnote overlay: the notes of the footnote marks visible in a pane show in
// a strip along the pane's bottom edge, laid out with the document style like
// the foot of a printed page. Notes whose own block is on screen are left
// out; a press on a note jumps to it.

#include <algorithm>

#include "xanatoo/app.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

constexpr float kMaxShare = 0.4f;  // of the pane height
constexpr float kPadPx = 8.f;      // above the first note (× ui scale)
constexpr int kFadePx = 12;        // fade where notes are cut off

// Lay out the notes for `keys` at the pane's text width.
void build_overlay(App& app, Pane& pane, std::vector<std::string> keys,
                   int width) {
  FootnoteOverlay& fo = pane.footnotes;
  fo.notes.clear();
  fo.note_y.clear();
  xanatoo::Document notes =
      xanatoo::extract_footnotes(doc_of(app, pane), keys, &fo.notes);
  const xanatoo::LayoutOptions saved = app.layout_engine.options();
  xanatoo::LayoutOptions o = saved;
  o.margin_y = kPadPx * app.ui_scale;
  app.layout_engine.set_options(o);
  fo.layout = app.layout_engine.layout(notes, static_cast<float>(width),
                                       base_of(app, pane));
  app.layout_engine.set_options(saved);

  // Top of each note (top-level blocks of `notes`, in order); the strip ends
  // one pad below the last line, not after the blocks' trailing space.
  const auto& tops = notes.get(notes.root).children;
  fo.note_y.assign(tops.size(), 0.f);
  for (const auto& b : fo.layout.boxes) {
    auto it = std::find(tops.begin(), tops.end(), b.node);
    if (it != tops.end()) fo.note_y[it - tops.begin()] = b.y;
  }
  float last_baseline = 0.f;
  for (const auto& g : fo.layout.glyphs)
    last_baseline = std::max(last_baseline, g.y);
  fo.content_h = tops.empty() ? 0.f
                              : last_baseline +
                                    static_cast<float>(
                                        -app.fonts.regular.descender()) +
                                    o.margin_y;
  fo.keys = std::move(keys);
  fo.layout_gen = pane.layout.generation;
  fo.width = width;
}

}  // namespace

void draw_footnote_overlay(App& app, int pi, const SDL_Rect& clip) {
  Pane& pane = app.panes[pi];
  FootnoteOverlay& fo = pane.footnotes;
  fo.h = 0.f;
  if (!app.view_settings.footnote_overlay) return;

  const float top = pane.scroll_y;
  const float bottom = top + static_cast<float>(clip.h);
  std::vector<std::string> keys = xanatoo::visible_footnote_keys(
      doc_of(app, pane), pane.layout, top, bottom);
  if (keys.empty()) return;
  const int width = std::max(40, pane.w - kSbLayoutGutter);
  if (keys != fo.keys || fo.layout_gen != pane.layout.generation ||
      fo.width != width)
    build_overlay(app, pane, std::move(keys), width);
  if (fo.content_h < 1.f) return;

  const int h = static_cast<int>(
      std::min(fo.content_h, static_cast<float>(clip.h) * kMaxShare));
  const SDL_Rect strip{clip.x, clip.y + clip.h - h, clip.w, h};

  // Step aside while the caret is under the strip so typing stays visible.
  if (!app.browse_mode && pi == app.focus) {
    const auto cp = xanatoo::caret_screen_pos(
        pane.layout, pane.sel.focus, pane.scroll_y - app.ui_top,
        static_cast<float>(app.fonts.regular.line_height()));
    if (cp.valid && cp.y + 4.f > static_cast<float>(strip.y)) return;
  }

  const xanatoo::DocStyle& st = app.style;
  SDL_RenderSetClipRect(app.sdl, &clip);
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
  for (int k = 1; k <= 4; ++k) {  // soft shadow onto the text above
    SDL_SetRenderDrawColor(app.sdl, 0, 0, 0, static_cast<Uint8>(32 / k));
    SDL_RenderDrawLine(app.sdl, strip.x, strip.y - k, strip.x + strip.w - 1,
                       strip.y - k);
  }
  SDL_SetRenderDrawColor(app.sdl, st.paper.r, st.paper.g, st.paper.b, 255);
  SDL_RenderFillRect(app.sdl, &strip);
  const xanatoo::Rgba rule = st[xanatoo::BlockRole::Footnote].border;
  if (rule.visible()) {
    SDL_SetRenderDrawColor(app.sdl, rule.r, rule.g, rule.b, rule.a);
    SDL_RenderDrawLine(app.sdl, strip.x, strip.y, strip.x + strip.w - 1,
                       strip.y);
  }

  SDL_RenderSetClipRect(app.sdl, &strip);
  app.renderer.draw(fo.layout, -static_cast<float>(strip.y),
                    static_cast<float>(pane.x));
  if (fo.content_h > static_cast<float>(h)) {
    // More notes than fit: fade the cut edge into the paper.
    SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
    const int y0 = strip.y + strip.h - kFadePx;
    for (int k = 0; k < kFadePx; ++k) {
      SDL_SetRenderDrawColor(app.sdl, st.paper.r, st.paper.g, st.paper.b,
                             static_cast<Uint8>(255 * (k + 1) / kFadePx));
      SDL_RenderDrawLine(app.sdl, strip.x, y0 + k, strip.x + strip.w - 1,
                         y0 + k);
    }
  }
  SDL_RenderSetClipRect(app.sdl, &clip);

  fo.y = static_cast<float>(strip.y) - app.ui_top;
  fo.h = static_cast<float>(h);
  fo.w = static_cast<float>(strip.w);
}

bool in_footnote_overlay(const App& app, int pi, float lx, float my) {
  const FootnoteOverlay& fo = app.panes[pi].footnotes;
  return fo.h > 0.f && lx >= 0.f && lx < fo.w && my >= fo.y &&
         my < fo.y + fo.h;
}

bool click_footnote_overlay(App& app, int pi, float lx, float my) {
  if (!in_footnote_overlay(app, pi, lx, my)) return false;
  Pane& pane = app.panes[pi];
  const FootnoteOverlay& fo = pane.footnotes;
  const float cy = my - fo.y;  // y inside the laid-out notes
  size_t k = 0;
  while (k + 1 < fo.note_y.size() && fo.note_y[k + 1] <= cy) ++k;
  if (k >= fo.notes.size()) return true;
  const xanatoo::Document& doc = doc_of(app, pane);
  const xanatoo::Node& fn = doc.get(fo.notes[k]);
  if (fn.id != fo.notes[k] || fn.kind != xanatoo::NodeKind::Footnote)
    return true;
  const std::string key = fn.label;
  if (auto dest = xanatoo::jump_to_href(doc, xanatoo::footnote_href(key))) {
    expand_ancestors_for_node(app, dest->node);
    pane.sel.focus = *dest;
    pane.sel.anchor = *dest;
    scroll_caret_into_view(app, pane);
    flash_status(app, "Footnote " + key);
  }
  return true;
}

}  // namespace xanatoo::app
