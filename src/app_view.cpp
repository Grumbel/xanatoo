// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>

#include "xanatoo/app.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

void apply_view_settings(App& app) {
  app.view_settings.clamp();
  app.layout_engine.set_options(
      xanatoo::LayoutOptions::from_view(app.view_settings, app.ui_scale));
  app.panes[0].height_cache.clear();
  app.panes[1].height_cache.clear();
  app.needs_layout = true;
  if (!xanatoo::save_view_settings(app.view_settings))
    std::cerr << "Could not persist view settings to "
              << xanatoo::view_settings_path() << "\n";
}

void refresh_outline(App& app) {
  // Preamble row label when the document has no title: the tab label.
  std::string label = buffer_tab_label(app, focused_pane(app).buf);
  if (xanatoo::starts_with(label, "*")) label.erase(0, 1);
  xanatoo::collect_outline(doc_of(app, focused_pane(app)), app.outline,
                           &app.folded_sections, label);
  if (app.outline_sel >= static_cast<int>(app.outline.size()))
    app.outline_sel = std::max(0, static_cast<int>(app.outline.size()) - 1);
  if (app.outline_sel < 0) app.outline_sel = 0;
}

void refresh_backlinks(App& app) {
  app.backlinks.clear();
  const auto& d = doc_of(app, focused_pane(app));
  xanatoo::NodeId id = focused_pane(app).sel.focus.node;
  std::string lab = xanatoo::address_label(d, id);
  if (lab.empty()) {
    // Prefer enclosing section's header label, then any # from plain text.
    xanatoo::NodeId sec = xanatoo::enclosing_section(d, id);
    if (sec != xanatoo::INVALID_NODE) lab = xanatoo::address_label(d, sec);
  }
  if (lab.empty() && id != xanatoo::INVALID_NODE) {
    // Match jump_to_href style: use first heading text as soft anchor? skip
    lab = xanatoo::address_label(d, id);
  }
  app.backlinks_label = lab;
  if (!lab.empty()) xanatoo::collect_backlinks(d, lab, app.backlinks);
  if (app.backlinks_sel >= static_cast<int>(app.backlinks.size()))
    app.backlinks_sel = std::max(0, static_cast<int>(app.backlinks.size()) - 1);
  if (app.backlinks_sel < 0) app.backlinks_sel = 0;
}

void invalidate_height_around(App& app, Pane& pane, xanatoo::NodeId id) {
  if (id == xanatoo::INVALID_NODE) return;
  const auto& d = doc_of(app, pane);
  for (xanatoo::NodeId cur = id;
       cur != xanatoo::INVALID_NODE && cur < d.next_id;
       cur = d.get(cur).parent) {
    pane.height_cache.erase(cur);
  }
}

void drop_height_hints_for_buf(App& app, BufferId bi) {
  for (int pi = 0; pi < 2; ++pi) {
    if (app.panes[pi].buf != bi) continue;
    app.panes[pi].height_cache.clear();
    app.panes[pi].materialised_top = 0.f;
    app.panes[pi].materialised_bottom = 0.f;
  }
}

xanatoo::NodeId active_focus_section(App& app) {
  if (!app.section_focus_mode) return xanatoo::INVALID_NODE;
  return app.section_focus_id;
}

void toggle_section_fold(App& app, xanatoo::NodeId section) {
  if (section == xanatoo::INVALID_NODE) return;
  const auto& d = doc_of(app, focused_pane(app));
  if (section >= d.next_id) return;
  const auto& n = d.get(section);
  if (n.kind != xanatoo::NodeKind::Section &&
      n.kind != xanatoo::NodeKind::Article)
    return;
  const bool now_folded = !app.folded_sections.count(section);
  if (now_folded)
    app.folded_sections.insert(section);
  else
    app.folded_sections.erase(section);
  for (int pi = 0; pi < 2; ++pi) {
    if (app.panes[pi].buf == focused_pane(app).buf)
      invalidate_height_around(app, app.panes[pi], section);
  }
  app.needs_layout = true;
  if (app.outline_mode) refresh_outline(app);
  // Persist view-local fold in side SQLite (documents stay immutable).
  std::string doc_key = path_of(app, focused_pane(app));
  if (!doc_key.empty()) {
    std::string sk = xanatoo::section_stable_key(d, section);
    xanatoo::save_section_fold(doc_key, sk, now_folded);
  }
}

void toggle_section_focus_mode(App& app) {
  app.section_focus_mode = !app.section_focus_mode;
  if (app.section_focus_mode) {
    // Seed from outline selection or caret section.
    app.section_focus_id = xanatoo::INVALID_NODE;
    if (!app.outline.empty() && app.outline_sel >= 0 &&
        app.outline_sel < static_cast<int>(app.outline.size())) {
      app.section_focus_id =
          app.outline[static_cast<size_t>(app.outline_sel)].section;
    }
    if (app.section_focus_id == xanatoo::INVALID_NODE) {
      app.section_focus_id = xanatoo::enclosing_section(
          doc_of(app, focused_pane(app)), focused_pane(app).sel.focus.node);
    }
    if (app.section_focus_id == xanatoo::INVALID_NODE) {
      app.section_focus_mode = false;
      flash_status(app, "Focus: no section under caret / outline");
      return;
    }
    drop_height_hints_for_buf(app, focused_pane(app).buf);
    app.needs_layout = true;
    flash_status(app, "Focus section on — outline picks what you see");
  } else {
    app.section_focus_id = xanatoo::INVALID_NODE;
    drop_height_hints_for_buf(app, focused_pane(app).buf);
    app.needs_layout = true;
    flash_status(app, "Focus section off — full document");
  }
}

void reload_folds_for_focused(App& app) {
  app.folded_sections.clear();
  std::string doc_key = path_of(app, focused_pane(app));
  if (doc_key.empty()) return;
  std::unordered_set<std::string> keys;
  xanatoo::load_section_folds(doc_key, keys);
  xanatoo::folds_keys_to_ids(doc_of(app, focused_pane(app)), keys,
                             app.folded_sections);
  app.needs_layout = true;
  if (app.outline_mode) refresh_outline(app);
}

void toggle_fold_at_caret(App& app) {
  auto& d = doc_of(app, focused_pane(app));
  xanatoo::NodeId id = focused_pane(app).sel.focus.node;
  while (id != xanatoo::INVALID_NODE && id < d.next_id) {
    const auto& n = d.get(id);
    if (n.kind == xanatoo::NodeKind::Section ||
        n.kind == xanatoo::NodeKind::Article) {
      toggle_section_fold(app, id);
      flash_status(app, app.folded_sections.count(id) ? "Section folded"
                                                      : "Section expanded");
      return;
    }
    id = n.parent;
  }
  flash_status(app, "No section under caret");
}

void refresh_diff(App& app) {
  app.diff_marks[0].clear();
  app.diff_marks[1].clear();
  app.diff_words[0].clear();
  app.diff_words[1].clear();
  if (!app.diff_mode || shared_doc(app)) return;
  const BufferId b0 = app.panes[0].buf, b1 = app.panes[1].buf;
  if (!find_buf(app, b0) || !find_buf(app, b1) || b0 == b1) return;
  xanatoo::compute_diff_marks(buf(app, b0).doc,
                              buf(app, b1).doc,
                              app.diff_marks[0], app.diff_marks[1],
                              &app.diff_words[0], &app.diff_words[1]);
}

void expand_ancestors_for_node(App& app, xanatoo::NodeId target) {
  if (target == xanatoo::INVALID_NODE) return;
  const auto& d = doc_of(app, focused_pane(app));
  bool any = false;
  std::string doc_key = path_of(app, focused_pane(app));
  for (xanatoo::NodeId id = target;
       id != xanatoo::INVALID_NODE && id < d.next_id; id = d.get(id).parent) {
    const auto& n = d.get(id);
    if (n.kind != xanatoo::NodeKind::Section &&
        n.kind != xanatoo::NodeKind::Article)
      continue;
    if (!app.folded_sections.count(id)) continue;
    app.folded_sections.erase(id);
    any = true;
    if (!doc_key.empty()) {
      std::string sk = xanatoo::section_stable_key(d, id);
      xanatoo::save_section_fold(doc_key, sk, false);
    }
    for (int pi = 0; pi < 2; ++pi) {
      if (app.panes[pi].buf == focused_pane(app).buf)
        invalidate_height_around(app, app.panes[pi], id);
    }
  }
  if (any) {
    app.needs_layout = true;
    if (app.outline_mode) refresh_outline(app);
  }
}

void layout_panes(App& app) {
  // Docked window columns take their width from the pane area.
  const int dock = dock_column_w(app, Dock::Left);
  const int ww =
      std::max(64, app.win_w - dock - dock_column_w(app, Dock::Right));
  const int gutter = app.split ? 6 : 0;
  if (app.split) {
    int left_w = static_cast<int>(app.split_ratio * (ww - gutter));
    left_w = std::max(120, std::min(ww - gutter - 120, left_w));
    app.panes[0].x = dock;
    app.panes[0].w = left_w;
    app.panes[1].x = dock + left_w + gutter;
    app.panes[1].w = ww - left_w - gutter;
  } else {
    app.panes[0].x = dock;
    app.panes[0].w = ww;
    app.panes[1].x = dock;
    app.panes[1].w = 0;
  }
  // Clamp pane buffer indices if tabs were closed out from under them.
  for (int pi = 0; pi < 2; ++pi) {
    if (!find_buf(app, app.panes[pi].buf) && !app.tab_order.empty()) {
      app.panes[pi].buf = app.tab_order.front();
    }
  }
  // Block heights depend on width; drop hints when this pane's width changed.
  for (int pi = 0; pi < 2; ++pi) {
    if (app.panes[pi].w != app.panes[pi].last_layout_pane_w) {
      app.panes[pi].height_cache.clear();
      app.panes[pi].last_layout_pane_w = app.panes[pi].w;
    }
  }
  auto layout_one = [&](Pane& pane) {
    try {
      xanatoo::ensure_valid_caret(doc_of(app, pane), pane.sel.focus);
      xanatoo::ensure_valid_caret(doc_of(app, pane), pane.sel.anchor);
      float view_h = static_cast<float>(app.win_h - status_bar_h) - app.ui_top -
                     app.ui_bottom;
      if (view_h < 80.f) view_h = 80.f;
      xanatoo::LayoutViewport vp;
      vp.top = pane.scroll_y;
      vp.bottom = pane.scroll_y + view_h;
      vp.overscan = std::max(800.f, view_h * 1.5f);
      const std::unordered_map<xanatoo::NodeId, float>* hints =
          pane.height_cache.empty() ? nullptr : &pane.height_cache;
      pane.layout = app.layout_engine.layout(
          doc_of(app, pane),
          static_cast<float>(std::max(40, pane.w - kSbLayoutGutter)),
          base_of(app, pane), vp, hints, &app.folded_sections,
          active_focus_section(app));
      // Refresh height hints from measured (and skipped-stub) boxes.
      for (const auto& b : pane.layout.boxes) {
        if (b.node != xanatoo::INVALID_NODE && b.height > 0.f)
          pane.height_cache[b.node] = b.height;
      }
      if (pane.layout.virtualized) {
        pane.materialised_top = pane.layout.materialised_top;
        pane.materialised_bottom = pane.layout.materialised_bottom;
      } else if (vp.active()) {
        pane.materialised_top = vp.top - vp.overscan;
        pane.materialised_bottom = vp.bottom + vp.overscan;
      } else {
        pane.materialised_top = 0.f;
        pane.materialised_bottom = pane.layout.content_height;
      }
    } catch (const std::exception& e) {
      std::cerr << "pane layout failed: " << e.what() << "\n";
      pane.layout = {};
    }
  };
  layout_one(app.panes[0]);
  if (app.split) layout_one(app.panes[1]);
  app.needs_layout = false;
  app.last_layout_w = app.win_w;
  app.last_layout_h = app.win_h;
  refresh_diff(app);
}

void ensure_virtual_layout(App& app, Pane& p) {
  if (p.height_cache.empty()) return;
  float view_h =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  if (view_h < 80.f) view_h = 80.f;
  const float margin = 64.f;
  const float vis_top = p.scroll_y;
  const float vis_bot = p.scroll_y + view_h;
  if (vis_top < p.materialised_top + margin ||
      vis_bot > p.materialised_bottom - margin) {
    app.needs_layout = true;
  }
}

std::optional<float> caret_doc_y(App& app, Pane& p) {
  const float lh = static_cast<float>(app.fonts.regular.line_height());
  auto cpos = xanatoo::caret_screen_pos(p.layout, p.sel.focus, 0.f, lh);
  if (cpos.valid) return cpos.y;
  const auto& d = doc_of(app, p);
  for (xanatoo::NodeId id = p.sel.focus.node;
       id != xanatoo::INVALID_NODE && id < d.next_id; id = d.get(id).parent) {
    for (const auto& box : p.layout.boxes) {
      if (box.node == id) return box.y;
    }
  }
  return std::nullopt;
}

void force_full_pane_layout(App& app, Pane& p) {
  try {
    xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.focus);
    xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.anchor);
    p.layout = app.layout_engine.layout(
        doc_of(app, p), static_cast<float>(std::max(40, p.w - kSbLayoutGutter)),
        base_of(app, p), {}, nullptr, &app.folded_sections,
        active_focus_section(app));
    for (const auto& b : p.layout.boxes) {
      if (b.node != xanatoo::INVALID_NODE && b.height > 0.f)
        p.height_cache[b.node] = b.height;
    }
    p.materialised_top = 0.f;
    p.materialised_bottom = p.layout.content_height;
  } catch (const std::exception& e) {
    std::cerr << "force full layout failed: " << e.what() << "\n";
  }
}

void scroll_caret_into_view(App& app, Pane& p) {
  auto yopt = caret_doc_y(app, p);
  if (!yopt) {
    // Target not in the current (virtualized) band — full pass then retry.
    force_full_pane_layout(app, p);
    yopt = caret_doc_y(app, p);
  }
  if (!yopt) return;
  float doc_y = *yopt;
  float margin = 40.f;
  // Visible content height excludes menu/toolbar and status bar.
  float view_h =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  if (view_h < 80.f) view_h = 80.f;
  if (doc_y - p.scroll_y < margin) {
    p.scroll_y = std::max(0.f, doc_y - margin);
  } else if (doc_y - p.scroll_y > view_h - margin) {
    p.scroll_y = doc_y - (view_h - margin);
  }
  // After a long jump, re-virtualize around the new scroll position.
  app.needs_layout = true;
  ensure_virtual_layout(app, p);
}

void scroll_caret_to_top(App& app, Pane& p) {
  auto box_top_of = [&](xanatoo::NodeId id) -> std::optional<float> {
    if (id == xanatoo::INVALID_NODE) return std::nullopt;
    for (const auto& box : p.layout.boxes) {
      if (box.node == id) return box.y;
    }
    return std::nullopt;
  };

  auto resolve_top = [&]() -> std::optional<float> {
    const auto& d = doc_of(app, p);
    // Header/Heading box, then Section, then any ancestor box.
    if (auto y = box_top_of(p.sel.focus.node)) return y;
    xanatoo::NodeId sec = xanatoo::enclosing_section(d, p.sel.focus.node);
    if (auto y = box_top_of(sec)) return y;
    for (xanatoo::NodeId id = p.sel.focus.node;
         id != xanatoo::INVALID_NODE && id < d.next_id; id = d.get(id).parent) {
      if (auto y = box_top_of(id)) return y;
    }
    // Last resort: caret baseline minus roughly one line so glyphs fit.
    if (auto cy = caret_doc_y(app, p)) {
      const float lh = static_cast<float>(app.fonts.regular.line_height());
      return std::max(0.f, *cy - lh);
    }
    return std::nullopt;
  };

  auto yopt = resolve_top();
  if (!yopt) {
    force_full_pane_layout(app, p);
    yopt = resolve_top();
  }
  if (!yopt) return;
  // Small pad so the box edge is not flush under the chrome.
  const float margin = 8.f;
  float view_h =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  if (view_h < 80.f) view_h = 80.f;
  float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
  p.scroll_y = std::max(0.f, std::min(max_s, *yopt - margin));
  app.needs_layout = true;
  ensure_virtual_layout(app, p);
}

float pane_max_scroll(App& app, const Pane& p) {
  const float view_h =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  return std::max(0.f, p.layout.content_height - view_h + 40.f);
}

void zoom_by(App& app, int steps) {
  if (steps == 0) return;
  for (; steps > 0; --steps) {
    app.body_px = std::min(48, app.body_px + 1);
    app.heading_px = std::min(64, app.heading_px + 2);
  }
  for (; steps < 0; ++steps) {
    app.body_px = std::max(10, app.body_px - 1);
    app.heading_px = std::max(14, app.heading_px - 2);
  }
  apply_font_sizes(app);
  app.needs_layout = true;
}

void apply_font_sizes(App& app) {
  const int body = std::max(
      8, static_cast<int>(std::lround(app.body_px * app.style.body_scale)));
  const int heading =
      app.style.heading_scale > 0.f
          ? static_cast<int>(std::lround(body * app.style.heading_scale))
          : static_cast<int>(std::lround(app.heading_px * app.style.body_scale));
  app.fonts.set_sizes(body, heading);
}

std::string body_font_path(const App& app) {
  if (!app.font_path.empty()) return app.font_path;
  return app.style.fonts.body.empty() ? app.env_font_path : std::string{};
}

void apply_style(App& app, const std::string& name) {
  xanatoo::DocStyle next;
  std::string err;
  if (!xanatoo::load_style(name, next, &err)) {
    flash_status(app, "Style: " + err);
    return;
  }
  for (const std::string& w : next.warnings)
    std::cerr << "style warning: " << w << "\n";
  const bool fonts_changed = !(next.fonts == app.style.fonts);
  app.style = std::move(next);
  if (fonts_changed &&
      !app.fonts.load(app.sdl, body_font_path(app), app.body_px,
                      app.heading_px, app.style.fonts)) {
    std::cerr << "Style fonts failed to load; using defaults\n";
    app.style.fonts = {};
    app.fonts.load(app.sdl, body_font_path(app), app.body_px, app.heading_px);
  }
  apply_font_sizes(app);
  app.help_needs_layout = true;
  for (FloatWindow& sw : app.soft_wins) sw.needs_layout = true;
  app.view_settings.style = app.style.name;
  apply_view_settings(app);  // drops height hints, relayouts, saves view.conf
  std::string msg = "Style: " + app.style.title;
  if (!app.style.warnings.empty())
    msg += " (" + std::to_string(app.style.warnings.size()) +
           " warning(s), see stderr)";
  flash_status(app, msg);
}

void reload_styles(App& app) {
  app.styles = xanatoo::list_styles();
  apply_style(app, app.style.name);
}

void nav_page(App& app, bool up) {
  Pane& p = focused_pane(app);
  const float view_h =
      static_cast<float>(app.win_h - status_bar_h) - app.ui_top - app.ui_bottom;
  const float step = view_h * 0.85f;
  float max_s = std::max(0.f, p.layout.content_height - view_h + 40.f);
  if (up)
    p.scroll_y = std::max(0.f, p.scroll_y - step);
  else
    p.scroll_y = std::min(max_s, p.scroll_y + step);
  ensure_virtual_layout(app, p);
}

void nav_move_block(App& app, bool prev) {
  Pane& p = focused_pane(app);
  auto& d = doc_of(app, p);
  if (prev)
    xanatoo::move_to_prev_node(d, p.sel.focus);
  else
    xanatoo::move_to_next_node(d, p.sel.focus);
  p.sel.anchor = p.sel.focus;
  scroll_caret_into_view(app, p);
  reset_blink(app);
}

void nav_move_line(App& app, bool up) {
  Pane& p = focused_pane(app);
  auto& d = doc_of(app, p);
  if (up) {
    if (!xanatoo::move_up(p.layout, p.sel.focus, p.preferred_x))
      xanatoo::move_to_prev_node(d, p.sel.focus);
  } else {
    if (!xanatoo::move_down(p.layout, p.sel.focus, p.preferred_x))
      xanatoo::move_to_next_node(d, p.sel.focus);
  }
  p.sel.anchor = p.sel.focus;
  scroll_caret_into_view(app, p);
  reset_blink(app);
}

void nav_header(App& app, bool prev) {
  refresh_outline(app);
  if (app.outline.empty()) return;
  if (prev) {
    if (app.outline_sel > 0) --app.outline_sel;
  } else {
    if (app.outline_sel + 1 < static_cast<int>(app.outline.size()))
      ++app.outline_sel;
  }
  Pane& p = focused_pane(app);
  p.sel.focus.node = app.outline[static_cast<size_t>(app.outline_sel)].node;
  p.sel.focus.span_index = 0;
  p.sel.focus.char_index = 0;
  p.sel.anchor = p.sel.focus;
  scroll_caret_into_view(app, p);
  reset_blink(app);
}

bool toggle_stretch_at_caret(App& app) {
  Pane& p = focused_pane(app);
  auto& d = doc_of(app, p);
  for (xanatoo::NodeId id = p.sel.focus.node; id != xanatoo::INVALID_NODE;
       id = d.get(id).parent) {
    if (d.get(id).kind == xanatoo::NodeKind::Stretch) {
      d.get(id).expanded = !d.get(id).expanded;
      app.needs_layout = true;
      flash_status(app, d.get(id).expanded ? "Expanded" : "Collapsed");
      return true;
    }
  }
  // Also toggle Stretch under the caret's layout box.
  auto cpos = xanatoo::caret_screen_pos(
      p.layout, p.sel.focus, p.scroll_y,
      static_cast<float>(app.fonts.regular.line_height()));
  if (cpos.valid) {
    for (const auto& box : p.layout.boxes) {
      if (box.node == xanatoo::INVALID_NODE) continue;
      if (d.get(box.node).kind != xanatoo::NodeKind::Stretch) continue;
      if (cpos.y + p.scroll_y < box.y ||
          cpos.y + p.scroll_y > box.y + box.height)
        continue;
      d.get(box.node).expanded = !d.get(box.node).expanded;
      app.needs_layout = true;
      flash_status(app, d.get(box.node).expanded ? "Expanded" : "Collapsed");
      return true;
    }
  }
  return false;
}

void collapse_or_expand_all(App& app, bool expand) {
  auto& d = doc_of(app, focused_pane(app));
  for (auto& node : d.nodes) {
    if (node.kind == xanatoo::NodeKind::Stretch) node.expanded = expand;
  }
  app.needs_layout = true;
  flash_status(app, expand ? "Expanded all" : "Collapsed all");
}

void jump_outline_sel(App& app) {
  if (app.outline.empty() || app.outline_sel < 0 ||
      app.outline_sel >= static_cast<int>(app.outline.size()))
    return;
  Pane& p = focused_pane(app);
  const auto& er = app.outline[static_cast<size_t>(app.outline_sel)];
  xanatoo::Document& d = doc_of(app, p);
  if (er.preamble) {
    // Title row: back to the very top (cover image, byline, …).
    if (app.section_focus_mode && app.section_focus_id != xanatoo::INVALID_NODE) {
      app.section_focus_id = xanatoo::INVALID_NODE;
      drop_height_hints_for_buf(app, p.buf);
      app.needs_layout = true;
    }
    if (app.needs_layout) layout_panes(app);
    p.sel = {};
    xanatoo::move_to_document_start(d, p.sel.focus);
    p.sel.anchor = p.sel.focus;
    p.scroll_y = 0.f;
    ensure_virtual_layout(app, p);  // the band may not reach the top
    reset_blink(app);
    return;
  }
  // Prefer the title Header; if the row only has a Section id (no header
  // child), land on the first text node under that section so
  // ensure_valid_caret does not relocate to the top of the document.
  xanatoo::NodeId dest = er.node;
  if (dest != xanatoo::INVALID_NODE && dest < d.next_id) {
    const auto& n = d.get(dest);
    if (n.id != dest || !xanatoo::is_text_node_kind(n.kind))
      dest = xanatoo::INVALID_NODE;
  } else {
    dest = xanatoo::INVALID_NODE;
  }
  if (dest == xanatoo::INVALID_NODE && er.section != xanatoo::INVALID_NODE) {
    std::vector<xanatoo::NodeId> texts;
    xanatoo::collect_text_nodes(d, er.section, texts);
    if (!texts.empty())
      dest = texts.front();
    else
      dest = er.section;
  }
  if (dest == xanatoo::INVALID_NODE) return;
  // Keep section-focus filter in sync with outline selection.
  if (app.section_focus_mode) {
    xanatoo::NodeId sec = er.section;
    if (sec == xanatoo::INVALID_NODE) {
      sec = xanatoo::enclosing_section(d, dest);
    }
    if (sec != app.section_focus_id) {
      app.section_focus_id = sec;
      drop_height_hints_for_buf(app, p.buf);
      app.needs_layout = true;
    }
  }
  expand_ancestors_for_node(app, dest);
  if (app.needs_layout) layout_panes(app);
  p.sel.focus = {dest, 0, 0};
  xanatoo::ensure_valid_caret(d, p.sel.focus);
  p.sel.anchor = p.sel.focus;
  // Outline jumps need a complete materialisation so box tops exist for
  // the target (virtualized bands often omit it until after scroll).
  if (!p.layout.boxes.empty()) {
    bool have_box = false;
    for (const auto& b : p.layout.boxes) {
      if (b.node == p.sel.focus.node) {
        have_box = true;
        break;
      }
    }
    if (!have_box) force_full_pane_layout(app, p);
  } else {
    force_full_pane_layout(app, p);
  }
  scroll_caret_to_top(app, p);
  reset_blink(app);
}

int pane_at(App& app, float mx) {
  if (!app.split) return 0;
  return (mx >= static_cast<float>(app.panes[1].x)) ? 1 : 0;
}

float local_x(App& app, float mx, int pi) {
  return mx - static_cast<float>(app.panes[pi].x);
}

float content_y(App& app, float window_y) { return window_y - app.ui_top; }

std::tuple<int, int, int, int> outline_client_rect(App& app) {
  return std::tuple{app.ol.x + help_pad, app.ol.y + help_title_h + 4,
                    app.ol.w - help_pad * 2,
                    app.ol.h - help_title_h - help_pad - 4};
}

float outline_row_step(App& app) {
  return static_cast<float>(std::max(18, app.ui_font.line_height() + 7));
}

OutlineRowGeom outline_row_geom(App& app, int level) {
  auto px = [&](int v) { return static_cast<int>(std::lround(v * app.ui_scale)); };
  OutlineRowGeom g;
  g.num_right = px(6) + app.ui_font.measure("9");
  g.twist_w = px(14);
  g.twist_x = g.num_right + px(6) + std::max(0, level - 1) * px(14);
  g.text_x = g.twist_x + g.twist_w + px(2);
  return g;
}

}  // namespace xanatoo::app
