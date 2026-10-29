// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Split browsing: the two panes form a trail. Links in the left pane open in
// the right pane (previewed on hover, kept on click); a link in the right
// pane moves the right page to the left and opens the target on the right.

#include <algorithm>
#include <filesystem>

#include "xanatoo/app.hpp"
#include "xanatoo/ebooks.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/theme.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

bool shown_anywhere_else(App& app, BufferId b, int except_pane) {
  for (int i : live_views(app))
    if (i != except_pane && app.panes[i].buf == b) return true;
  return false;
}

namespace {

constexpr Uint32 kPreviewDwellMs = 250;        // local docs / anchors
constexpr Uint32 kRemotePreviewDwellMs = 450;  // network fetch
constexpr Uint32 kPreviewGraceMs = 350;        // pointer off the link

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
  attach_draft(app, pv.scratch);
  return pv.scratch;
}

// Pane `from`'s page moves to pane `to`; `from` keeps `to`'s old buffer to
// load the next page into, when nothing else needs it.
void move_page(App& app, int from, int to) {
  Pane& f = app.panes[from];
  Pane& t = app.panes[to];
  const BufferId old = t.buf;
  set_pane_view(app, to, f.buf, f.sel, f.scroll_y);
  const DocBuffer* ob = find_buf(app, old);
  if (ob && old != t.buf && !ob->dirty && !shown_anywhere_else(app, old, from))
    set_pane_view(app, from, old, {}, 0.f);
}

// ---- split history ----

bool same_page(const std::string& a, const std::string& b) {
  if (a == b) return true;
  if (a.empty() || b.empty() || is_in_app_remote_url(a) ||
      is_in_app_remote_url(b))
    return false;
  std::error_code ea, eb;
  const fs::path ca = fs::weakly_canonical(a, ea);
  const fs::path cb = fs::weakly_canonical(b, eb);
  return !ea && !eb && ca == cb;
}

void seed_history(App& app) {
  SplitHistory& h = app.split_history;
  if (!h.levels.empty()) return;
  SplitHistoryLevel l;
  l.page.url = path_of(app, app.panes[0]);
  if (const std::string r = path_of(app, app.panes[1]); !r.empty()) {
    l.opened.push_back({});
    l.opened.back().url = r;
    l.cur = 0;
  }
  h.levels.push_back(std::move(l));
  h.top = 0;
}

// Record `url` shown in centre pane `pi`: the right pane adds a page opened
// from the current left page, the left pane starts a new level. Forward
// history past it is dropped. `force` records even a repeat of the current
// page (anchor jumps, a page moving left onto itself).
void note_page(App& app, int pi, std::string url, bool force) {
  if (url.empty()) return;
  seed_history(app);
  SplitHistory& h = app.split_history;
  // Keep the pane's own spelling, so per-frame matching is a string compare.
  if (const std::string shown = path_of(app, app.panes[pi]);
      shown != url && same_page(shown, url))
    url = shown;
  SplitHistoryLevel& l = h.levels[static_cast<size_t>(h.top)];
  if (!force) {
    const SplitHistoryEntry* now =
        pi == 0 ? &l.page
                : (l.cur >= 0 ? &l.opened[static_cast<size_t>(l.cur)]
                              : nullptr);
    if (now && now->url == url) return;
  }
  l.opened.resize(static_cast<size_t>(l.cur + 1));
  h.levels.resize(static_cast<size_t>(h.top + 1));
  if (pi == 1) {
    SplitHistoryEntry e;
    e.url = std::move(url);
    e.href = std::move(h.pending_href);
    h.pending_href.clear();
    l.opened.push_back(std::move(e));
    l.cur = static_cast<int>(l.opened.size()) - 1;
  } else {
    SplitHistoryLevel nl;
    nl.page.url = std::move(url);
    h.levels.push_back(std::move(nl));
    h.top = static_cast<int>(h.levels.size()) - 1;
  }
  ++h.gen;
}

SplitHistoryEntry* right_entry(App& app) {
  SplitHistory& h = app.split_history;
  if (h.top < 0) return nullptr;
  SplitHistoryLevel& l = h.levels[static_cast<size_t>(h.top)];
  return l.cur >= 0 ? &l.opened[static_cast<size_t>(l.cur)] : nullptr;
}

void apply_entry(App& app, int pi, const SplitHistoryEntry& e) {
  Pane& p = app.panes[pi];
  p.sel = e.sel;
  xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.focus);
  xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.anchor);
  p.scroll_y = e.scroll;
  app.needs_layout = true;
}

// Per frame: remember caret / scroll of the pages the panes show (so Back
// returns there), or put them back once a page restored by Back has loaded.
void sync_entry(App& app, int pi, SplitHistoryEntry& e, bool& restore) {
  Pane& p = app.panes[pi];
  if (path_of(app, p) != e.url) return;
  if (restore) {
    const FetchJob* f = pane_fetch(app, pi);
    if (f && f->purpose == FetchPurpose::Navigate) return;
    apply_entry(app, pi, e);
    restore = false;
    return;
  }
  e.sel = p.sel;
  e.scroll = p.scroll_y;
}

// Show entry `e` in pane `pi` (already there: just its caret / scroll).
void show_entry(App& app, int pi, SplitHistoryEntry& e, bool& restore) {
  restore = false;
  if (path_of(app, app.panes[pi]) != e.url &&
      !navigate_pane(app, pi, e.url, /*record_history=*/false))
    return;
  if (path_of(app, app.panes[pi]) == e.url)
    apply_entry(app, pi, e);
  else
    restore = true;  // remote page still loading
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
      // Book download + pandoc import: far too heavy for a hover.
      if (!xanatoo::ebook_download_url(t.url).empty()) return false;
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
      if (xanatoo::is_pandoc_path(t.url)) return false;  // pandoc import: slow
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
  move_page(app, 1, 0);
  const std::string page = path_of(app, app.panes[0]);
  note_page(app, 0, page, /*force=*/true);  // the trail moved left
  push_nav_history(app, 0, page);
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

// Mark `href` in the left pane as what the right pane now shows.
// `lt` is the link just opened in the right pane; its anchor (if any) is
// where the right pane's caret was put.
void set_trail(App& app, const std::string& href, const LinkTarget& lt) {
  SplitTrail& t = app.split_trail;
  t.href = href;
  t.left_page = path_of(app, app.panes[0]);
  t.right_page.clear();  // taken once the right page has loaded
  t.target = xanatoo::INVALID_NODE;
  Pane& r = app.panes[1];
  if (lt.kind == LinkKind::Anchor ||
      (lt.kind == LinkKind::Doc && !lt.fragment.empty() &&
       xanatoo::jump_to_href(doc_of(app, r), lt.fragment)))
    t.target = r.sel.focus.node;
}

// Drop the mark once either pane shows another page.
void check_trail(App& app) {
  SplitTrail& t = app.split_trail;
  if (t.href.empty() || app.split_preview.active) return;
  if (const FetchJob* f = pane_fetch(app, 0);
      split_browsing(app) && f && f->purpose == FetchPurpose::Navigate)
    return;  // Back is reloading the left page
  if (!split_browsing(app) || path_of(app, app.panes[0]) != t.left_page) {
    t = {};
    return;
  }
  Pane& r = app.panes[1];
  if (t.right_page.empty()) {
    const FetchJob* f = pane_fetch(app, 1);
    const DocBuffer* rb = find_buf(app, r.buf);
    if ((f && f->purpose == FetchPurpose::Navigate) ||
        (rb && rb->placeholder))
      return;
    t.right_page = path_of(app, r);
  } else if (path_of(app, r) != t.right_page) {
    t = {};
  }
}

}  // namespace

bool split_browsing(const App& app) { return app.split && !app.diff_mode; }

bool follow_link_split(App& app, int pi, const std::string& href) {
  if (!split_browsing(app) || pi >= kCentreViews) return false;
  SplitPreview& pv = app.split_preview;
  const LinkTarget t = resolve_link(app, pi, href);
  if (t.kind == LinkKind::None || t.kind == LinkKind::External) return false;
  SplitHistory& h = app.split_history;
  // The right page's history entry: remote pages are recorded when they
  // land (taking `pending_href`), anchors here since nothing loads.
  auto record = [&](uint64_t gen0) {
    const bool anchor = t.kind == LinkKind::Anchor ||
                        (t.kind == LinkKind::Doc && !t.fragment.empty());
    if (anchor && h.gen == gen0)
      note_page(app, 1, path_of(app, app.panes[1]), /*force=*/true);
    if (h.gen != gen0)
      if (SplitHistoryEntry* e = right_entry(app))
        e->target = app.split_trail.target;
    if (t.kind != LinkKind::Remote) h.pending_href.clear();
  };
  if (pi == 0) {
    seed_history(app);
    h.pending_href = href;
    const uint64_t gen0 = h.gen;
    if (pv.active && pv.shown == href) {
      commit_split_preview(app);
    } else {
      revert_split_preview(app);
      open_in_right(app, t, /*placeholder=*/false);
    }
    pv.shown = href;  // no fresh preview while the pointer stays on it
    set_trail(app, href, t);
    record(gen0);
    focus_view(app, 0);
    if (t.kind != LinkKind::Remote) flash_status(app, "→ right pane: " + href);
    return true;
  }
  commit_split_preview(app);
  // An anchor's caret refers to the right document, which moves left.
  shift_left(app);
  h.pending_href = href;
  const uint64_t gen0 = h.gen;
  open_in_right(app, t, /*placeholder=*/true);
  set_trail(app, href, t);  // the clicked page is the left one now
  record(gen0);
  pv.shown.clear();
  // The pointer's hover data belongs to the page that just moved away.
  app.hover.href.clear();
  app.hover.link_pane = -1;
  focus_view(app, 1);
  if (t.kind != LinkKind::Remote) flash_status(app, "Trail ← " + href);
  return true;
}

void commit_split_preview(App& app) {
  SplitPreview& pv = app.split_preview;
  if (!pv.active) return;
  pv.active = false;
  if (app.split_history.pending_href.empty())
    app.split_history.pending_href = pv.shown;
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
    push_nav_history(app, 1, label);
  } else if (!path_of(app, r).empty() && r.buf != app.panes[0].buf) {
    push_nav_history(app, 1, path_of(app, r));
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
    app.split_trail = {};
    app.split_history = {};
    return;
  }
  // Something else changed the right pane (tab switch, navigation): the
  // preview is no longer what it shows, so keep whatever is there now.
  if (pv.active && app.panes[1].buf != pv.shown_buf) {
    pv.active = false;
    pv.saved_buf = kNoBuffer;
    drop_scratch(app);
  }
  check_trail(app);
  seed_history(app);
  {
    SplitHistory& h = app.split_history;
    SplitHistoryLevel& l = h.levels[static_cast<size_t>(h.top)];
    sync_entry(app, 0, l.page, h.restore[0]);
    if (SplitHistoryEntry* e = right_entry(app); e && !pv.active)
      sync_entry(app, 1, *e, h.restore[1]);
  }

  const Uint32 now = SDL_GetTicks();
  const int mx = static_cast<int>(app.mouse_x);
  const int my = static_cast<int>(app.mouse_y);
  const bool in_content = SDL_GetMouseFocus() == app.window &&
                          my >= static_cast<int>(app.ui_top) &&
                          my < app.win_h - status_bar_h &&
                          !over_float_chrome(app, mx, my) &&
                          !app.ui.want_capture_mouse();
  const bool busy = app.dragging_sel || app.dragging_scroll ||
                    app.dragging_split || app.dragging_block ||
                    app.dragging_image || app.dragging_col_gap ||
                    app.touch.mode != TouchMode::Idle;
  std::string want;
  if (app.view_settings.hover_preview && in_content && !busy &&
      app.hover.link_pane == 0 &&
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

void split_history_note(App& app, int pi, const std::string& url) {
  if (!split_browsing(app) || pi < 0 || pi >= kCentreViews) return;
  note_page(app, pi, url, /*force=*/false);
}

bool split_history_can_step(const App& app, int dir) {
  const SplitHistory& h = app.split_history;
  if (h.top < 0) return false;
  const SplitHistoryLevel& l = h.levels[static_cast<size_t>(h.top)];
  if (dir < 0) return l.cur > 0 || h.top > 0;
  return l.cur + 1 < static_cast<int>(l.opened.size()) ||
         h.top + 1 < static_cast<int>(h.levels.size());
}

bool split_history_step(App& app, int dir) {
  if (!split_history_can_step(app, dir)) return false;
  revert_split_preview(app);
  SplitHistory& h = app.split_history;
  SplitHistoryLevel* l = &h.levels[static_cast<size_t>(h.top)];
  const bool within = dir < 0 ? l->cur > 0
                              : l->cur + 1 < static_cast<int>(l->opened.size());
  if (within) {
    l->cur += dir;
  } else {
    // Slide the two-pane window over the trail: back puts the left page
    // on the right again, forward moves the right page left.
    h.top += dir;
    l = &h.levels[static_cast<size_t>(h.top)];
    if (dir > 0) l->cur = l->opened.empty() ? -1 : 0;
    // Move the page that stays in view instead of loading it again.
    const SplitHistoryEntry* e = right_entry(app);
    if (dir < 0 && e && path_of(app, app.panes[0]) == e->url)
      move_page(app, 0, 1);
    else if (dir > 0 && path_of(app, app.panes[1]) == l->page.url)
      move_page(app, 1, 0);
    show_entry(app, 0, l->page, h.restore[0]);
  }
  if (SplitHistoryEntry* e = right_entry(app)) {
    show_entry(app, 1, *e, h.restore[1]);
    SplitTrail& t = app.split_trail;
    t = {};
    if (!e->href.empty()) {
      t.href = e->href;
      t.left_page = l->page.url;
      t.target = e->target;
    }
  } else {
    app.split_trail = {};
  }
  app.split_preview.shown.clear();
  return true;
}

bool split_history_position(const App& app, int& pos, int& total) {
  const SplitHistory& h = app.split_history;
  if (h.top < 0) return false;
  // Every Back / Forward stop: one per page opened on the right, or one
  // for a left page that opened none.
  pos = total = 0;
  for (int i = 0; i < static_cast<int>(h.levels.size()); ++i) {
    const SplitHistoryLevel& l = h.levels[static_cast<size_t>(i)];
    const int stops = std::max(1, static_cast<int>(l.opened.size()));
    if (i < h.top) pos += stops;
    if (i == h.top) pos += std::max(0, l.cur) + 1;
    total += stops;
  }
  return true;
}

const std::string& split_trail_href(const App& app, int pi) {
  static const std::string none;
  if (pi != 0 || !split_browsing(app) || app.split_preview.active)
    return none;
  return app.split_trail.href;
}

xanatoo::NodeId split_trail_target(App& app, int pi) {
  const SplitTrail& t = app.split_trail;
  if (pi != 1 || t.href.empty() || t.right_page.empty() ||
      t.target == xanatoo::INVALID_NODE || !split_browsing(app) ||
      app.split_preview.active)
    return xanatoo::INVALID_NODE;
  const xanatoo::Document& d = doc_of(app, app.panes[1]);
  return d.get(t.target).id == t.target ? t.target : xanatoo::INVALID_NODE;
}

void draw_preview_badge(App& app, int pi) {
  const SplitPreview& pv = app.split_preview;
  if (pi != 1 || !pv.active) return;
  namespace th = xanatoo::theme;
  const th::Palette& pal = th::palette();
  const Pane& p = app.panes[1];
  const int top = p.y;
  const int w = std::max(1, p.w - kSbW);
  const int h = p.h;
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
