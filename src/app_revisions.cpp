// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Database revision browser: an ordinary dockable Xanatoo panel. Selected
// snapshots are inspected without changing any head; Restore appends a new
// working revision, with an optimistic concurrency check.

#include "xanatoo/app.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/revstore.hpp"
#include "xanatoo/theme.hpp"

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace xanatoo::app {
namespace {

int row_height(const App& app) {
  return std::max(20, app.ui_font.line_height() + 8);
}

std::string short_id(const std::string& id) {
  return id.size() > 18 ? id.substr(7, 11) : id;
}

std::vector<std::string> lines_of(const std::string& text, size_t limit) {
  std::vector<std::string> lines;
  for (size_t from = 0; from < text.size() && lines.size() < limit;) {
    const size_t end = text.find('\n', from);
    const size_t length = (end == std::string::npos ? text.size() : end) - from;
    lines.push_back(text.substr(from, std::min(length, size_t{320})));
    from = end == std::string::npos ? text.size() : end + 1;
  }
  if (lines.empty()) lines.push_back("(empty document)");
  if (static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1 > limit)
    lines.push_back("… preview truncated");
  return lines;
}

std::string readable(const std::string& source) {
  try {
    return xanatoo::serialize_document(xanatoo::parse_sexpr(source), {});
  } catch (...) {
    return "(snapshot cannot be rendered as a document)";
  }
}

// Bounded LCS so a large imported document cannot freeze a render frame.
// A space-prefixed line is unchanged, '-' was removed, '+' was added.
std::vector<std::string> line_diff(const std::string& old_text,
                                   const std::string& current_text) {
  const auto a = lines_of(old_text, 160);
  const auto b = lines_of(current_text, 160);
  const size_t n = a.size(), m = b.size();
  std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
  for (size_t i = n; i-- > 0;)
    for (size_t j = m; j-- > 0;)
      lcs[i][j] = a[i] == b[j] ? lcs[i + 1][j + 1] + 1
                  : std::max(lcs[i + 1][j], lcs[i][j + 1]);

  std::vector<std::string> out;
  size_t i = 0, j = 0, unchanged = 0;
  auto flush = [&] {
    if (unchanged > 0) {
      out.push_back("  … " + std::to_string(unchanged) + " unchanged lines");
      unchanged = 0;
    }
  };
  while (i < n || j < m) {
    if (i < n && j < m && a[i] == b[j]) {
      ++unchanged;
      ++i;
      ++j;
    } else if (i < n && (j == m || lcs[i + 1][j] >= lcs[i][j + 1])) {
      flush();
      out.push_back("- " + a[i++]);
    } else {
      flush();
      out.push_back("+ " + b[j++]);
    }
  }
  flush();
  if (out.empty()) out.push_back("(no textual differences)");
  return out;
}

void rebuild_preview(App& app) {
  const auto selected = std::find_if(app.revision_history.begin(),
                                     app.revision_history.end(),
                                     [&](const auto& r) {
    return r.id == app.revision_selected;
  });
  if (selected == app.revision_history.end()) {
    app.revision_preview_lines = {"Select a revision to inspect"};
    return;
  }
  const std::string source = readable(selected->data);
  if (!app.revision_show_diff) {
    app.revision_preview_lines = lines_of(source, 160);
    return;
  }
  const auto current = load_stored_document_revision(
      app.revision_document_key, app.revision_working);
  if (!current) {
    app.revision_preview_lines = {"Working revision unavailable"};
    return;
  }
  app.revision_preview_lines = line_diff(source, readable(*current));
}

void draw_line(App& app, int cx, int cy, int cw,
               const std::string& text, const xanatoo::theme::Rgb& ink,
               bool highlighted = false) {
  const int h = row_height(app);
  if (cy + h < 0 || cy > app.win_h) return;
  if (highlighted)
    xanatoo::theme::fill(app.sdl, SDL_Rect{cx + 1, cy, cw - 12, h},
                         xanatoo::theme::palette().face_dark);
  const float baseline = static_cast<float>(
      cy + (h - app.ui_font.line_height()) / 2 + app.ui_font.ascender());
  app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 7), baseline,
                         fit_text(app.ui_font, text, cw - 25),
                         ink.r, ink.g, ink.b);
}

}  // namespace

void refresh_revision_panel(App& app) {
  if (!app.revision_panel_open || app.revision_document_key.empty()) return;
  const auto head = find_stored_document(app.revision_document_key);
  if (!head) {
    app.revision_history.clear();
    app.revision_preview_lines = {"Document is no longer available"};
    return;
  }
  if (head->working == app.revision_working &&
      head->published == app.revision_published &&
      !app.revision_history.empty()) return;
  app.revision_working = head->working;
  app.revision_published = head->published;
  app.revision_history = stored_document_history(head->key, 64);
  if (std::none_of(app.revision_history.begin(), app.revision_history.end(),
                   [&](const auto& r) { return r.id == app.revision_selected; }))
    app.revision_selected = head->working;
  rebuild_preview(app);
}

float revision_panel_max_scroll(App& app, int client_h) {
  const int rows = 6 + static_cast<int>(app.revision_history.size()) +
                   static_cast<int>(app.revision_preview_lines.size());
  return std::max(0.f, static_cast<float>(rows * row_height(app) + 8 - client_h));
}

void draw_revision_panel(App& app) {
  if (!app.revision_panel_open) return;
  refresh_revision_panel(app);
  FloatWindow& w = app.revision_win;
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  w.scroll = std::clamp(w.scroll, 0.f, revision_panel_max_scroll(app, ch));
  const SDL_Rect outer{w.x, w.y, w.w, w.h};
  draw_floating_frame(app, outer, "Document Revisions", true,
                      w.dock == Dock::None, 0, w.dock == Dock::Max);
  const SDL_Rect clip{cx, cy, cw, ch};
  xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  const auto& pal = xanatoo::theme::palette();
  const int step = row_height(app);
  auto y_of = [&](int row) {
    return cy + row * step - static_cast<int>(w.scroll);
  };
  const auto head = find_stored_document(app.revision_document_key);
  draw_line(app, cx, y_of(0), cw, head ? head->title : "Unknown document",
            pal.text);
  draw_line(app, cx, y_of(1), cw,
            "Working " + short_id(app.revision_working) +
              "   Published " + (app.revision_published.empty() ? "none" :
                                short_id(app.revision_published)), pal.text_dim);
  for (int button = 0; button < 3; ++button) {
    const int x1 = cx + button * (cw - 12) / 3 + 4;
    const int x2 = cx + (button + 1) * (cw - 12) / 3 + 4;
    const bool active = button == (app.revision_show_diff ? 1 : 0);
    const bool enabled = button != 2 ||
        (!app.revision_selected.empty() &&
         app.revision_selected != app.revision_working);
    xanatoo::theme::fill(app.sdl,
                         SDL_Rect{x1, y_of(2) + 2, x2 - x1 - 3, step - 4},
                         active ? pal.accent : pal.face_dark);
    const char* label = button == 0 ? "Preview" : button == 1 ? "Diff" : "Restore";
    const auto& ink = !enabled ? pal.text_dim : active ? pal.highlight : pal.text;
    app.renderer.draw_text(app.ui_font, static_cast<float>(x1 + 8),
        static_cast<float>(y_of(2) + (step - app.ui_font.line_height()) / 2 +
                           app.ui_font.ascender()),
        label, ink.r, ink.g, ink.b);
  }
  draw_line(app, cx, y_of(3), cw, "Snapshots (newest working first)",
            pal.text_dim);
  for (size_t i = 0; i < app.revision_history.size(); ++i) {
    const auto& r = app.revision_history[i];
    const std::time_t seconds = static_cast<std::time_t>(r.time);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &seconds);
#else
    localtime_r(&seconds, &tm);
#endif
    char date[32] = {};
    std::strftime(date, sizeof date, "%Y-%m-%d %H:%M", &tm);
    std::string label = std::string(r.id == app.revision_working ? "● " : "  ") +
        r.kind + "  " + date + "  " + short_id(r.id);
    draw_line(app, cx, y_of(4 + static_cast<int>(i)), cw,
              label, pal.text, r.id == app.revision_selected);
  }
  const int preview_header = 5 + static_cast<int>(app.revision_history.size());
  draw_line(app, cx, y_of(preview_header), cw,
            app.revision_show_diff ? "Changes: selected → working" :
                                     "Selected snapshot (read-only)",
            pal.text_dim);
  for (size_t i = 0; i < app.revision_preview_lines.size(); ++i) {
    const auto& line = app.revision_preview_lines[i];
    const auto& ink = line.starts_with("+ ") ? pal.accent :
                      line.starts_with("- ") ? pal.text_dim : pal.text;
    draw_line(app, cx, y_of(preview_header + 1 + static_cast<int>(i)), cw,
              line, ink);
  }
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll,
                        revision_panel_max_scroll(app, ch));
}

void click_revision_panel(App& app, int mx, int my) {
  refresh_revision_panel(app);
  auto [cx, cy, cw, ch] = float_client_rect(app, app.revision_win);
  if (mx < cx || mx >= cx + cw - 12 || my < cy || my >= cy + ch) return;
  const int row = static_cast<int>((my - cy + app.revision_win.scroll) /
                                    row_height(app));
  if (row >= 4 && row < 4 + static_cast<int>(app.revision_history.size())) {
    app.revision_selected = app.revision_history[static_cast<size_t>(row - 4)].id;
    rebuild_preview(app);
    return;
  }
  if (row != 2) return;
  const int button = std::clamp((mx - cx - 4) * 3 / std::max(1, cw - 12), 0, 2);
  if (button < 2) {
    app.revision_show_diff = button == 1;
    rebuild_preview(app);
    return;
  }
  if (app.revision_selected.empty() ||
      app.revision_selected == app.revision_working) return;
  // Do not overwrite unsaved edits in any open copy of this document.
  for (const DocBuffer& b : app.buffers)
    if (b.doc_key == app.revision_document_key && b.dirty) {
      flash_status(app, "Save pending edits before restoring a revision");
      return;
    }
  const auto head = find_stored_document(app.revision_document_key);
  if (!head || head->working != app.revision_working) {
    flash_status(app, "Document changed elsewhere; refresh history before restoring");
    refresh_revision_panel(app);
    return;
  }
  const auto restored = restore_stored_document_revision(
      head->key, app.revision_selected, head->working, head->title);
  if (!restored) {
    flash_status(app, "Could not restore revision (stale head or missing snapshot)");
    refresh_revision_panel(app);
    return;
  }
  // Refresh visible copies. All existing snapshots remain in the database;
  // a non-visible tab reloads when it is reopened.
  const std::string ref = "db:" + head->key;
  for (int pi : live_views(app)) {
    const DocBuffer* b = find_buf(app, app.panes[pi].buf);
    if (b && b->doc_key == head->key && is_stored_document_ref(b->path))
      open_stored_document(app, pi, ref);
  }
  refresh_revision_panel(app);
  flash_status(app, "Restored snapshot as a new working revision; publication unchanged");
}

}  // namespace xanatoo::app
