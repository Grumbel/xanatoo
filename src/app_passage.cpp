// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// The passage a view shows (docs/PASSAGES.md): the selectors of its address
// are view state, the buffer stays the whole document. Pipes cut the layout
// to a node, the `#` part scrolls to a node and marks it.

#include "xanatoo/app.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"

#include <ctime>

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

// Selectors → what the view shows. What cannot be found drops out of `loc`,
// with all that depended on it, and the first such selector lands in `lost`.
struct Resolved {
  xanatoo::NodeId cut = xanatoo::INVALID_NODE;
  xanatoo::NodeId target = xanatoo::INVALID_NODE;  // where to scroll
  std::optional<xanatoo::Selection> mark;          // the `#` passage
  std::optional<xanatoo::Selection> cut_text;      // a text pipe, marked too
};

// Lowest node holding both ends of a text range.
xanatoo::NodeId common_block(const xanatoo::Document& d,
                             const xanatoo::Selection& s) {
  for (xanatoo::NodeId cur = s.anchor.node; cur != xanatoo::INVALID_NODE;
       cur = d.get(cur).parent)
    if (within(d, s.focus.node, cur)) return cur;
  return d.root;
}

bool resolve_one(const xanatoo::Document& d, xanatoo::NodeId scope,
                 const xanatoo::Selector& sel, xanatoo::NodeId& node,
                 std::optional<xanatoo::Selection>& text) {
  if (sel.kind == xanatoo::SelectorKind::Text) {
    xanatoo::Selection s;
    if (xanatoo::resolve_text(d, scope, sel, s) == xanatoo::Resolve::Lost)
      return false;
    text = s;
    node = s.anchor.node;
    return true;
  }
  return xanatoo::resolve_node(d, scope, sel, node) == xanatoo::Resolve::Exact;
}

Resolved resolve_locator(const xanatoo::Document& d, xanatoo::Locator& loc,
                         std::string& lost) {
  Resolved r;
  size_t good = 0;
  for (; good < loc.pipes.size(); ++good) {
    xanatoo::NodeId n = xanatoo::INVALID_NODE;
    std::optional<xanatoo::Selection> text;
    // Text cuts the blocks around the range (not clipped yet), so nothing
    // can follow it.
    if (r.cut_text || !resolve_one(d, r.cut, loc.pipes[good], n, text)) break;
    r.cut = text ? common_block(d, *text) : n;
    r.cut_text = text;
  }
  if (good < loc.pipes.size()) {
    lost = loc.pipes[good].value;
    loc.pipes.resize(good);
    loc.point.reset();
    r.cut_text.reset();
  } else if (loc.point) {
    std::optional<xanatoo::Selection> text;
    if (resolve_one(d, r.cut, *loc.point, r.target, text)) {
      r.mark = text ? *text : xanatoo::node_range(d, r.target);
    } else {
      lost = loc.point->value;
      loc.point.reset();
    }
  }
  if (!r.mark && r.cut_text) {
    r.mark = r.cut_text;
    r.target = r.cut_text->anchor.node;
  }
  return r;
}

void set_marks(Pane& p, const Resolved& r) {
  p.marks.clear();
  if (r.mark) p.marks.push_back(*r.mark);
}

// First text of `node`, where a caret can sit.
xanatoo::Caret caret_in(const xanatoo::Document& d, xanatoo::NodeId node) {
  std::vector<xanatoo::NodeId> texts;
  xanatoo::collect_text_nodes(d, node, texts);
  return {texts.empty() ? node : texts.front(), 0, 0};
}

// Whitespace collapsed, at most `max` bytes (whole UTF-8 characters).
std::string one_line(std::string_view s, size_t max) {
  std::string out;
  for (char c : s) {
    const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (space && (out.empty() || out.back() == ' ')) continue;
    out.push_back(space ? ' ' : c);
  }
  if (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > max) {
    out.resize(max + 1);
    xanatoo::utf8_pop_back(out);
    out += "…";
  }
  return out;
}

std::string today() {
  const std::time_t now = std::time(nullptr);
  char buf[16] = {};
  if (const std::tm* tm = std::localtime(&now))
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
  return buf;
}

}  // namespace

void reset_pane_locator(Pane& p) {
  p.locator = {};
  p.locator_buf = kNoBuffer;
  p.cut = xanatoo::INVALID_NODE;
  p.marks.clear();
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
  std::string lost;
  const Resolved r = resolve_locator(d, loc, lost);
  if (!lost.empty()) flash_status(app, "Passage not found: " + lost);
  const xanatoo::NodeId cut = r.cut, target = r.target;

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
  set_marks(p, r);
  app.needs_layout = true;
  if (cut != old_cut) drop_height_hints_for_buf(app, p.buf);

  if (widens) {
    p.view_anchor = hold;
  } else if (target != xanatoo::INVALID_NODE || cut != old_cut) {
    const xanatoo::Caret dest =
        r.mark ? r.mark->anchor
               : caret_in(d, target != xanatoo::INVALID_NODE ? target : cut);
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
  p.marks.clear();
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
  p.marks.clear();
  if (p.locator.pipes.empty()) p.locator_buf = kNoBuffer;
}

void refresh_pane_locator(App& app, Pane& p) {
  if (!locator_live(p)) return;
  std::string lost;
  const Resolved r = resolve_locator(doc_of(app, p), p.locator, lost);
  if (!lost.empty()) flash_status(app, "Passage not found: " + lost);
  p.cut = r.cut;
  set_marks(p, r);
  if (!p.locator.has_selectors()) p.locator_buf = kNoBuffer;
}

xanatoo::Bookmark bookmark_for_view(App& app, Pane& p, bool position) {
  xanatoo::Bookmark b;
  b.address = pane_address(app, p);
  b.created = today();
  if (b.address.empty()) return b;
  if (p.sel.empty() && !position) return b;
  xanatoo::Document& d = doc_of(app, p);
  const xanatoo::NodeId sec = xanatoo::enclosing_section(d, p.sel.focus.node);
  if (sec != xanatoo::INVALID_NODE)
    for (xanatoo::NodeId c : d.get(sec).children)
      if (d.get(c).kind == xanatoo::NodeKind::Header ||
          d.get(c).kind == xanatoo::NodeKind::Heading) {
        b.title = one_line(xanatoo::plain_text(d, c), 80);
        break;
      }
  // The passage keeps the cut it was made in, its text is the point.
  xanatoo::Locator loc = xanatoo::parse_locator(b.address);
  const xanatoo::Selector sel =
      xanatoo::text_selector(d, pane_cut(app, p), p.sel);
  loc.point = sel;
  b.address = xanatoo::format_locator(loc);
  if (!p.sel.empty()) {
    b.quote = one_line(xanatoo::selected_text(d, p.sel), 160);
  } else if (const auto q = xanatoo::parse_text_quote(sel.value)) {
    b.quote = one_line(q->prefix, 60) + " ‸ " + one_line(q->suffix, 60);
  }
  return b;
}

}  // namespace xanatoo::app
