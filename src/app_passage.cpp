// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// The passage a view shows (docs/PASSAGES.md): the selectors of its address
// are view state, the buffer stays the whole document. Pipes cut the layout
// to a node, the `#` part scrolls to a node and marks it.

#include "xanatoo/app.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

bool locator_live(const Pane& p) {
  return p.locator_buf != kNoBuffer && p.locator_buf == p.buf;
}

bool node_alive(const xanatoo::Document& d, xanatoo::NodeId id) {
  return id != xanatoo::INVALID_NODE && id < d.next_id && d.get(id).id == id;
}

bool within(const xanatoo::Document& d, xanatoo::NodeId id,
            xanatoo::NodeId scope) {
  if (scope == xanatoo::INVALID_NODE) return true;
  for (xanatoo::NodeId cur = id; cur != xanatoo::INVALID_NODE;
       cur = d.get(cur).parent)
    if (cur == scope) return true;
  return false;
}

// Selectors → nodes. What cannot be found drops out of `loc`, with all that
// depended on it, and the first such selector lands in `lost`.
void resolve_locator(const xanatoo::Document& d, xanatoo::Locator& loc,
                     xanatoo::NodeId& cut, xanatoo::NodeId& target,
                     std::string& lost) {
  cut = target = xanatoo::INVALID_NODE;
  size_t good = 0;
  for (; good < loc.pipes.size(); ++good) {
    xanatoo::NodeId n = xanatoo::INVALID_NODE;
    if (xanatoo::resolve_node(d, cut, loc.pipes[good], n) ==
        xanatoo::Resolve::Lost)
      break;
    cut = n;
  }
  if (good < loc.pipes.size()) {
    lost = loc.pipes[good].value;
    loc.pipes.resize(good);
    loc.point.reset();
  } else if (loc.point && xanatoo::resolve_node(d, cut, *loc.point, target) ==
                              xanatoo::Resolve::Lost) {
    lost = loc.point->value;
    loc.point.reset();
  }
}

void set_mark(const xanatoo::Document& d, Pane& p, xanatoo::NodeId target) {
  p.mark.clear();
  if (target == xanatoo::INVALID_NODE) return;
  std::vector<xanatoo::NodeId> texts;
  xanatoo::collect_text_nodes(d, target, texts);
  p.mark.insert(texts.begin(), texts.end());
  p.mark.insert(target);  // a text node may not list itself
}

// First text of `node`, where a caret can sit.
xanatoo::Caret caret_in(const xanatoo::Document& d, xanatoo::NodeId node) {
  std::vector<xanatoo::NodeId> texts;
  xanatoo::collect_text_nodes(d, node, texts);
  return {texts.empty() ? node : texts.front(), 0, 0};
}

}  // namespace

void reset_pane_locator(Pane& p) {
  p.locator = {};
  p.locator_buf = kNoBuffer;
  p.cut = xanatoo::INVALID_NODE;
  p.mark.clear();
  p.section_focus = false;
}

xanatoo::NodeId pane_cut(App& app, Pane& p) {
  // Another page came in by a way that knows nothing of passages (tabs,
  // split trail, …). The nodes are of the old document.
  if (p.locator_buf != kNoBuffer && p.locator_buf != p.buf)
    reset_pane_locator(p);
  if (!locator_live(p) || !find_buf(app, p.buf)) return xanatoo::INVALID_NODE;
  return node_alive(doc_of(app, p), p.cut) ? p.cut : xanatoo::INVALID_NODE;
}

std::string pane_address(App& app, Pane& p) {
  std::string page = path_of(app, p);
  if (page.empty()) page = app.gemini_current_url;
  if (!locator_live(p) || page.empty()) return page;
  xanatoo::Locator loc = p.locator;
  loc.base = std::move(page);
  return xanatoo::format_locator(loc);
}

void apply_locator(App& app, int pi, xanatoo::Locator loc) {
  Pane& p = app.panes[pi];
  xanatoo::Document& d = doc_of(app, p);
  const xanatoo::NodeId old_cut = pane_cut(app, p);
  xanatoo::NodeId cut = xanatoo::INVALID_NODE;
  xanatoo::NodeId target = xanatoo::INVALID_NODE;
  std::string lost;
  resolve_locator(d, loc, cut, target, lost);
  if (!lost.empty()) flash_status(app, "Passage not found: " + lost);

  // The cut opens around what it showed: that text stays on its spot,
  // unless the address points somewhere else.
  const bool widens =
      old_cut != xanatoo::INVALID_NODE && cut != old_cut &&
      within(d, old_cut, cut) &&
      (target == xanatoo::INVALID_NODE || within(d, old_cut, target) ||
       within(d, target, old_cut));
  std::optional<ViewAnchor> hold;
  if (widens) hold = view_anchor_at(app, p, 0.f, 0.f);

  const bool focus = p.section_focus;
  loc.base.clear();
  const bool any = loc.has_selectors();
  p.locator = std::move(loc);
  p.locator_buf = any ? p.buf : kNoBuffer;
  p.cut = cut;
  p.section_focus = focus && cut != xanatoo::INVALID_NODE;
  set_mark(d, p, target);
  app.needs_layout = true;
  if (cut != old_cut) drop_height_hints_for_buf(app, p.buf);

  if (widens) {
    p.view_anchor = hold;
  } else if (target != xanatoo::INVALID_NODE || cut != old_cut) {
    const xanatoo::Caret dest =
        caret_in(d, target != xanatoo::INVALID_NODE ? target : cut);
    p.sel = {dest, dest};
    xanatoo::ensure_valid_caret(d, p.sel.focus);
    p.sel.anchor = p.sel.focus;
    p.scroll_y = 0.f;
    p.layout = {};
    if (target != xanatoo::INVALID_NODE) {
      const int saved = app.focus;
      app.focus = pi;  // expand_ancestors_for_node works on the focused pane
      expand_ancestors_for_node(app, dest.node);
      app.focus = saved;
      scroll_caret_to_top(app, p);
    }
  }
}

void set_pane_cut(App& app, Pane& p, xanatoo::NodeId node) {
  if (pane_cut(app, p) == node) return;
  p.mark.clear();
  p.locator = {};
  p.cut = node;
  p.locator_buf = node == xanatoo::INVALID_NODE ? kNoBuffer : p.buf;
  if (node != xanatoo::INVALID_NODE)
    p.locator.pipes.push_back(xanatoo::node_selector(doc_of(app, p), node));
  drop_height_hints_for_buf(app, p.buf);
  app.needs_layout = true;
}

void clear_pane_point(App& app, Pane& p) {
  if (!locator_live(p) || !p.locator.point) return;
  p.locator.point.reset();
  p.mark.clear();
  if (p.locator.pipes.empty()) p.locator_buf = kNoBuffer;
}

void refresh_pane_locator(App& app, Pane& p) {
  if (!locator_live(p)) return;
  const xanatoo::Document& d = doc_of(app, p);
  xanatoo::NodeId target = xanatoo::INVALID_NODE;
  std::string lost;
  resolve_locator(d, p.locator, p.cut, target, lost);
  if (!lost.empty()) flash_status(app, "Passage not found: " + lost);
  set_mark(d, p, target);
  if (!p.locator.has_selectors()) p.locator_buf = kNoBuffer;
}

}  // namespace xanatoo::app
