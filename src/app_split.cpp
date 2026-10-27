// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Split browsing: the two panes form a trail. Links in the left pane open in
// the right pane (previewed on hover, kept on click); a link in the right
// pane moves the right page to the left and opens the target on the right.

#include <algorithm>
#include <filesystem>

#include "xanatoo/app.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/theme.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

namespace {

constexpr Uint32 kPreviewDwellMs = 250;        // local docs / anchors
constexpr Uint32 kRemotePreviewDwellMs = 450;  // network fetch
constexpr Uint32 kPreviewGraceMs = 350;        // pointer off the link

enum class LinkKind : uint8_t { None, Anchor, Doc, Remote, External };

struct LinkTarget {
  LinkKind kind = LinkKind::None;
  std::string url;       // Doc: path without fragment; Remote: absolute URL
  std::string fragment;  // Doc: "#heading" (may be empty)
  xanatoo::Caret dest{};  // Anchor: target in the pane's own document
};

LinkTarget resolve_link(App& app, int pi, const std::string& href) {
  LinkTarget t;
  Pane& p = app.panes[pi];
  if (auto dest = xanatoo::jump_to_href(doc_of(app, p), href)) {
    t.kind = LinkKind::Anchor;
    t.dest = *dest;
    return t;
  }
  if (href.empty() || href[0] == '#') return t;
  std::string abs = href;
  const std::string base = link_base_url(app, pi);
  if (!xanatoo::is_gemini_url(abs) && !base.empty()) {
    std::string r = xanatoo::resolve_gemini_url(base, abs);
    if (!r.empty()) abs = std::move(r);
  }
  if (is_in_app_remote_url(abs)) {
    t.kind = LinkKind::Remote;
    t.url = std::move(abs);
    return t;
  }
  if (is_inapp_doc_href(app, abs)) {
    std::string path = abs;
    if (size_t hash = path.find('#'); hash != std::string::npos) {
      t.fragment = path.substr(hash);
      path.resize(hash);
    }
    t.kind = LinkKind::Doc;
    t.url = resolve_doc_href(app, path, path_of(app, p));
    return t;
  }
  t.kind = LinkKind::External;
  t.url = href;
  return t;
}

// Point pane `pi` at buffer `b` with a fresh layout.
void set_pane_view(App& app, int pi, BufferId b, const xanatoo::Selection& sel,
                   float scroll) {
  Pane& p = app.panes[pi];
  p.buf = b;
  p.sel = sel;
  xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.focus);
  xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.anchor);
  p.scroll_y = scroll;
  p.layout = {};
  p.height_cache.clear();
  app.needs_layout = true;
}

void show_anchor(App& app, int pi, BufferId b, const xanatoo::Caret& dest) {
  set_pane_view(app, pi, b, xanatoo::Selection{dest, dest}, 0.f);
  const int saved = app.focus;
  app.focus = pi;  // expand_ancestors_for_node works on the focused pane
  expand_ancestors_for_node(app, dest.node);
  app.focus = saved;
  scroll_caret_to_top(app, app.panes[pi]);
}

void jump_to_fragment(App& app, int pi, const std::string& fragment) {
  if (fragment.empty()) return;
  Pane& p = app.panes[pi];
  if (auto dest = xanatoo::jump_to_href(doc_of(app, p), fragment))
    show_anchor(app, pi, p.buf, *dest);
}

bool shown_anywhere_else(App& app, BufferId b, int except_pane) {
  for (int i = 0; i < 2; ++i)
    if (i != except_pane && app.panes[i].buf == b) return true;
  return std::any_of(app.soft_wins.begin(), app.soft_wins.end(),
                     [&](const FloatWindow& sw) { return sw.buf == b; });
}

// Free the preview's scratch buffer unless something displays it.
void drop_scratch(App& app) {
  SplitPreview& pv = app.split_preview;
  if (pv.scratch == kNoBuffer) return;
  const BufferId s = pv.scratch;
  pv.scratch = kNoBuffer;
  if (!find_buf(app, s) || shown_anywhere_else(app, s, -1)) return;
  if (std::find(app.tab_order.begin(), app.tab_order.end(), s) !=
      app.tab_order.end())
    return;
  remove_buffer(app, s);
}

BufferId scratch_buffer(App& app, xanatoo::Document doc,
                        const std::string& label) {
  SplitPreview& pv = app.split_preview;
  if (!find_buf(app, pv.scratch))
    pv.scratch = add_buffer(app, xanatoo::parse_document("", {}), {});
  DocBuffer& b = buf(app, pv.scratch);
  b.doc = std::move(doc);
  b.path = label;
  b.dirty = false;
  b.placeholder = false;
  return pv.scratch;
}

// Show `href` (hovered in the left pane) in the right pane. False when there
// is nothing to show yet (external link, missing file, fetch running).
bool show_preview(App& app, const std::string& href) {
  SplitPreview& pv = app.split_preview;
  const LinkTarget t = resolve_link(app, 0, href);
  BufferId show = kNoBuffer;
  xanatoo::Caret dest{};
  bool has_dest = false;
  switch (t.kind) {
    case LinkKind::None:
    case LinkKind::External:
      return false;
    case LinkKind::Anchor:
      show = app.panes[0].buf;
      dest = t.dest;
      has_dest = true;
      break;
    case LinkKind::Remote: {
      const CachedPage* c = find_cached_page(app, t.url);
      if (!c) {
        if (pv.pending != t.url) {
          start_fetch(app, FetchPurpose::Preview, 1, t.url);
          pv.pending = t.url;
        }
        return false;
      }
      show = scratch_buffer(app, c->doc, c->final_url);
      break;
    }
    case LinkKind::Doc: {
      if (xanatoo::is_epub_path(t.url)) return false;  // pandoc import: slow
      show = find_buffer(app, t.url);
      if (show == kNoBuffer) {
        std::error_code ec;
        if (!fs::is_regular_file(t.url, ec)) return false;
        show = scratch_buffer(app, load_doc(app, t.url), t.url);
      }
      if (!t.fragment.empty())
        if (auto d = xanatoo::jump_to_href(buf(app, show).doc, t.fragment)) {
          dest = *d;
          has_dest = true;
        }
      break;
    }
  }
  if (!pv.active) {
    pv.saved_buf = app.panes[1].buf;
    pv.saved_sel = app.panes[1].sel;
    pv.saved_scroll = app.panes[1].scroll_y;
  }
  pv.active = true;
  pv.shown = href;
  pv.shown_buf = show;
  if (has_dest)
    show_anchor(app, 1, show, dest);
  else
    set_pane_view(app, 1, show, {}, 0.f);
  return true;
}

// The right page moves to the left; the right pane gets a buffer to load the
// next page into (the old left page's, when nothing else needs it).
void shift_left(App& app) {
  Pane& l = app.panes[0];
  Pane& r = app.panes[1];
  const BufferId old_left = l.buf;
  set_pane_view(app, 0, r.buf, r.sel, r.scroll_y);
  const DocBuffer* ob = find_buf(app, old_left);
  if (ob && old_left != l.buf && !ob->dirty &&
      !shown_anywhere_else(app, old_left, 1))
    set_pane_view(app, 1, old_left, {}, 0.f);
}

// Open link target `t` (resolved in the left pane) in the right pane.
void open_in_right(App& app, const LinkTarget& t, bool placeholder) {
  switch (t.kind) {
    case LinkKind::Anchor:
      show_anchor(app, 1, app.panes[0].buf, t.dest);
      break;
    case LinkKind::Doc:
      if (navigate_pane(app, 1, t.url)) jump_to_fragment(app, 1, t.fragment);
      break;
    case LinkKind::Remote:
      if (placeholder && !find_cached_page(app, t.url)) {
        // The right page just moved left: show a stand-in while loading.
        show_doc_in_pane(app, 1,
                         xanatoo::parse_document("# Loading…\n\n`" + t.url +
                                                     "`\n",
                                                 {}),
                         t.url);
        buf(app, app.panes[1].buf).placeholder = true;
      }
      navigate_pane(app, 1, t.url);
      break;
    case LinkKind::None:
    case LinkKind::External:
      break;
  }
}

}  // namespace

bool split_browsing(const App& app) { return app.split && !app.diff_mode; }

bool follow_link_split(App& app, int pi, const std::string& href) {
  if (!split_browsing(app)) return false;
  SplitPreview& pv = app.split_preview;
  const LinkTarget t = resolve_link(app, pi, href);
  if (t.kind == LinkKind::None || t.kind == LinkKind::External) return false;
  if (pi == 0) {
    if (pv.active && pv.shown == href) {
      commit_split_preview(app);
    } else {
      revert_split_preview(app);
      open_in_right(app, t, /*placeholder=*/false);
    }
    pv.shown = href;  // no fresh preview while the pointer stays on it
    app.focus = 0;
    if (t.kind != LinkKind::Remote) flash_status(app, "→ right pane: " + href);
    return true;
  }
  commit_split_preview(app);
  // An anchor's caret refers to the right document, which moves left.
  shift_left(app);
  open_in_right(app, t, /*placeholder=*/true);
  pv.shown.clear();
  // The pointer's hover data belongs to the page that just moved away.
  app.hover.href.clear();
  app.hover.link_pane = -1;
  app.focus = 1;
  if (t.kind != LinkKind::Remote) flash_status(app, "Trail ← " + href);
  return true;
}

void commit_split_preview(App& app) {
  SplitPreview& pv = app.split_preview;
  if (!pv.active) return;
  pv.active = false;
  Pane& r = app.panes[1];
  if (r.buf == pv.scratch && find_buf(app, pv.scratch)) {
    // Move the previewed page into a real page buffer (recycling the right
    // pane's previous one, like a click would).
    DocBuffer& sb = buf(app, pv.scratch);
    xanatoo::Document doc = std::move(sb.doc);
    const std::string label = sb.path;
    const xanatoo::Selection sel = r.sel;
    const float scroll = r.scroll_y;
    if (find_buf(app, pv.saved_buf)) r.buf = pv.saved_buf;
    show_doc_in_pane(app, 1, std::move(doc), label);
    r.sel = sel;
    r.scroll_y = scroll;
    if (r.buf == pv.scratch) pv.scratch = kNoBuffer;  // became the page
    drop_scratch(app);
    if (xanatoo::is_gemini_url(label)) {
      app.gemini_current_url = label;
      app.browse_mode = true;
    }
    push_nav_history(app, label);
  } else if (!path_of(app, r).empty() && r.buf != app.panes[0].buf) {
    push_nav_history(app, path_of(app, r));
  }
  pv.saved_buf = kNoBuffer;
}

void revert_split_preview(App& app) {
  SplitPreview& pv = app.split_preview;
  cancel_preview_fetch(app);
  if (!pv.active) return;
  pv.active = false;
  pv.shown.clear();
  if (find_buf(app, pv.saved_buf))
    set_pane_view(app, 1, pv.saved_buf, pv.saved_sel, pv.saved_scroll);
  pv.saved_buf = kNoBuffer;
  drop_scratch(app);
}

void update_split_preview(App& app) {
  SplitPreview& pv = app.split_preview;
  if (!app.split) cancel_fetches(app, 1);  // right pane hidden
  if (!split_browsing(app)) {
    if (pv.active || !pv.pending.empty()) revert_split_preview(app);
    pv.hover.clear();
    pv.shown.clear();
    return;
  }
  // Something else changed the right pane (tab switch, navigation): the
  // preview is no longer what it shows, so keep whatever is there now.
  if (pv.active && app.panes[1].buf != pv.shown_buf) {
    pv.active = false;
    pv.saved_buf = kNoBuffer;
    drop_scratch(app);
  }

  const Uint32 now = SDL_GetTicks();
  const int mx = static_cast<int>(app.mouse_x);
  const int my = static_cast<int>(app.mouse_y);
  const bool in_content = SDL_GetMouseFocus() == app.window &&
                          my >= static_cast<int>(app.ui_top) &&
                          my < app.win_h - status_bar_h &&
                          !float_at(app, mx, my) &&
                          !app.ui.want_capture_mouse();
  const bool busy = app.dragging_sel || app.dragging_scroll ||
                    app.dragging_split || app.dragging_block ||
                    app.dragging_image || app.dragging_col_gap ||
                    app.touch.mode != TouchMode::Idle;
  std::string want;
  if (in_content && !busy && app.hover.link_pane == 0 &&
      mx < app.panes[0].x + app.panes[0].w)
    want = app.hover.href;

  if (want != pv.hover) {
    pv.hover = want;
    pv.hover_since = now;
  }
  if (!want.empty()) {
    pv.off_since = 0;
    if (want == pv.shown) return;
    const bool remote = is_in_app_remote_url(want) ||
                        xanatoo::is_gemini_url(link_base_url(app, 0));
    const Uint32 dwell = remote ? kRemotePreviewDwellMs : kPreviewDwellMs;
    if (now - pv.hover_since >= dwell) show_preview(app, want);
    return;
  }
  // Off the link. Holding still over the right pane keeps the preview, so
  // it can be read and scrolled; back in the left pane it goes away.
  if (!pv.active) {
    pv.shown.clear();
    if (!pv.pending.empty()) cancel_preview_fetch(app);
    return;
  }
  const bool over_right = in_content && mx >= app.panes[1].x;
  if (over_right) {
    pv.off_since = 0;
    return;
  }
  if (pv.off_since == 0) pv.off_since = now;
  if (now - pv.off_since >= kPreviewGraceMs) revert_split_preview(app);
}

void draw_preview_badge(App& app, int pi) {
  const SplitPreview& pv = app.split_preview;
  if (pi != 1 || !pv.active) return;
  namespace th = xanatoo::theme;
  const th::Palette& pal = th::palette();
  const Pane& p = app.panes[1];
  const int top = static_cast<int>(app.ui_top);
  const int w = std::max(1, p.w - kSbW);
  const int h = app.win_h - status_bar_h - top - static_cast<int>(app.ui_bottom);
  SDL_RenderSetClipRect(app.sdl, nullptr);
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.sdl, pal.accent.r, pal.accent.g, pal.accent.b,
                         200);
  for (int k = 0; k < 2; ++k) {
    SDL_Rect fr{p.x + k, top + k, w - 2 * k, h - 2 * k};
    SDL_RenderDrawRect(app.sdl, &fr);
  }
  Font& font = app.ui_font;
  const int pad = 8;
  const std::string label =
      fit_text(font, "Preview · click the link to keep it", w - 4 * pad);
  const int lh = font.line_height() + 6;
  SDL_Rect pill{p.x + pad, top + h - lh - pad, font.measure(label) + 2 * pad,
                lh};
  th::fill(app.sdl, pill, pal.accent_soft, 235);
  SDL_SetRenderDrawColor(app.sdl, pal.accent.r, pal.accent.g, pal.accent.b,
                         255);
  SDL_RenderDrawRect(app.sdl, &pill);
  app.renderer.draw_text(font, static_cast<float>(pill.x + pad),
                         static_cast<float>(pill.y + 3 + font.ascender()),
                         label, pal.accent_dark.r, pal.accent_dark.g,
                         pal.accent_dark.b);
}

}  // namespace xanatoo::app
