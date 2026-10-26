// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Shared classic-desktop chrome drawn on the SDL canvas: floating window
// frames and the status bar. The status *content* is a pure function of the
// world (compute_status); drawing only consumes the resulting record.

#include <algorithm>
#include <string>

#include "xanatoo/app.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

void fill_doc_paper(App& app, const SDL_Rect& rect) {
  const xanatoo::Rgba& c = app.style.paper;
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(app.sdl, c.r, c.g, c.b, 255);
  SDL_RenderFillRect(app.sdl, &rect);
}

void draw_floating_frame(App& app, const SDL_Rect& outer,
                         const std::string& title, bool active,
                         bool resizable) {
  namespace th = xanatoo::theme;
  th::WindowChrome w;
  w.outer = outer;
  w.title_h = help_title_h;
  w.active = active;
  w.resizable = resizable;
  th::draw_window_chrome(app.sdl, w);
  const th::Palette& p = th::palette();
  // Document well: paper surface in a sunken frame under the title strip.
  const SDL_Rect well{outer.x + 4, outer.y + help_title_h + 4, outer.w - 8,
                      outer.h - help_title_h - 8};
  th::fill(app.sdl, well, p.paper);
  th::bevel(app.sdl, well, th::Bevel::Sunken);
  const th::Rgb ink = active ? p.tab_text : p.text_dim;
  SDL_Rect clip{outer.x + 6, outer.y + 1, outer.w - 36, help_title_h - 2};
  SDL_RenderSetClipRect(app.sdl, &clip);
  const int ty = outer.y +
                 (help_title_h - app.fonts.regular.line_height()) / 2 +
                 app.fonts.regular.ascender();
  app.renderer.draw_text(app.fonts.regular, static_cast<float>(outer.x + 10),
                         static_cast<float>(ty), title, ink.r, ink.g, ink.b);
  SDL_RenderSetClipRect(app.sdl, nullptr);
}

void draw_client_scrollbar(App& app, int cx, int cy, int cw, int ch,
                           float scroll, float max_scroll) {
  if (max_scroll <= 1.f) return;
  SDL_Rect trough{cx + cw - 12, cy, 12, ch};
  xanatoo::theme::draw_vscrollbar(app.sdl, trough, scroll, max_scroll,
                                  static_cast<float>(ch));
}

StatusLine compute_status(App& app) {
  StatusLine st;
  std::string& label = st.text;
  StatusTone& tone = st.tone;
  tone = StatusTone::Hint;
  if (prompt_active(app)) {
    const Prompt& pr = app.prompt;
    tone = StatusTone::Prompt;
    const std::string shown_or_dots = pr.text.empty() ? "…" : pr.text;
    switch (pr.kind) {
      case PromptKind::OpenInWindow:
      case PromptKind::OpenFile:
      case PromptKind::SaveAs:
      case PromptKind::InsertImage:
      case PromptKind::InsertFigure:
        label = prompt_label(pr.kind);
        label += app.minibuf.display_with_cursor();
        label += "    Tab complete · ↑↓ history · Enter · Esc";
        label += app.minibuf.status_hint();
        break;
      case PromptKind::UrlBar:
        label = prompt_label(pr.kind);
        label += app.minibuf.display_with_cursor();
        label += "    ↑↓ history · Enter · Esc · gemini:// or path";
        label += app.minibuf.status_hint();
        break;
      case PromptKind::InsertInclude:
        label = "Include: ";
        label += app.minibuf.display_with_cursor();
        label += "    Tab · ↑↓ · Enter · Esc  (#H · file.md · file.md#H) · Esc";
        break;
      case PromptKind::EditCite:
        label = "Citation key: " + shown_or_dots;
        label += "    Enter confirm · Esc cancel";
        break;
      case PromptKind::EditLink:
        label = "Link: " + shown_or_dots;
        label += "    (optional: href rel=glossary)";
        label += "    Enter confirm · Esc cancel";
        break;
      case PromptKind::EditSummary:
        label = "Stretch summary: " + shown_or_dots;
        label += "    Enter confirm · Esc cancel";
        break;
      case PromptKind::Replace:
        label = "Find: " + shown_or_dots;
        label += "  →  Replace: ";
        label += pr.text2.empty() ? "…" : pr.text2;
        label += pr.focus_second ? "  [repl] " : "  [find] ";
        label += " Tab switch · Enter replace · Ctrl+Shift+Enter all · Esc";
        break;
      case PromptKind::Search:
        label = "Leap: " + shown_or_dots;
        label += "    F3 next · Shift+F3 prev · Esc cancel";
        break;
      case PromptKind::None:
        break;
    }
  } else if (!app.status_notice.empty() &&
             SDL_GetTicks() < app.status_notice_until) {
    label = app.status_notice;
    tone = StatusTone::Notice;
  } else if (app.dragging_block) {
    label = "Moving block — drop before/after any block (columns & lists OK)";
    tone = StatusTone::Info;
  } else if (app.dragging_col_gap) {
    label = "Resizing columns — drag to adjust relative widths";
    tone = StatusTone::Info;
  } else if (app.diff_mode) {
    label =
        "Diff marks on — orange bars mark passages that differ · "
        "Ctrl+Shift+D toggles";
    tone = StatusTone::Warn;
  } else if (app.browse_mode) {
    std::string page = path_of(app, focused_pane(app));
    if (page.empty()) page = app.gemini_current_url;
    if (!page.empty() && xanatoo::is_gemini_url(page)) {
      label = page;
      if (app.nav_hist_pos >= 0 && !app.nav_history.empty()) {
        label += "  ·  ";
        label += std::to_string(app.nav_hist_pos + 1);
        label += "/";
        label += std::to_string(static_cast<int>(app.nav_history.size()));
      }
      label += "  ·  browse · F2 edit";
    } else {
      label = "Browse (read-only) · arrows / gamepad · A/Space open · F2 edit";
    }
    tone = StatusTone::Browse;
  } else if (!path_of(app, focused_pane(app)).empty() &&
             xanatoo::is_gemini_url(path_of(app, focused_pane(app)))) {
    label = path_of(app, focused_pane(app));
    if (app.nav_hist_pos >= 0 && !app.nav_history.empty()) {
      label += "  ·  ";
      label += std::to_string(app.nav_hist_pos + 1);
      label += "/";
      label += std::to_string(static_cast<int>(app.nav_history.size()));
    }
    tone = StatusTone::Browse;
  } else if (app.structure_mode) {
    label =
        "Structure highlight — colour bars + kind tags on the right · "
        "Ctrl+Shift+L toggles";
    tone = StatusTone::Info;
  } else if (app.section_focus_mode) {
    label =
        "Focus section — body shows one section · outline picks target · "
        "Ctrl+Alt+F off";
    tone = StatusTone::Info;
  } else if (app.outline_mode) {
    label = "Outline — ↑/↓ · ←/→ fold · Enter jump · twisty · Esc";
    tone = StatusTone::Info;
  } else if (app.hover.col_gap) {
    label = "Column gutter — drag to adjust relative widths";
    tone = StatusTone::Info;
  } else if (app.hover.task != xanatoo::INVALID_NODE) {
    std::string st = "task";
    if (app.hover.task_pane >= 0 && app.hover.task_pane < 2) {
      const auto& tn =
          doc_of(app, app.panes[app.hover.task_pane]).get(app.hover.task);
      st = tn.label.empty() ? "bullet" : tn.label;
    }
    if (st == "task-done")
      label = "Task — click to mark unchecked";
    else if (st == "task")
      label = "Task — click to mark done";
    else
      label = "List marker — click to convert to task checkbox";
    tone = StatusTone::Browse;
  } else if (app.hover.grip != xanatoo::INVALID_NODE) {
    label = "Drag block — drop before/after another block (columns & lists OK)";
    tone = StatusTone::Info;
  } else if (app.hover.transclude != xanatoo::INVALID_NODE) {
    label = "Include: ";
    label +=
        app.hover.transclude_src.empty() ? "(empty)" : app.hover.transclude_src;
    label += "    click to jump to source";
    tone = StatusTone::Info;
  } else if (app.hover.image != xanatoo::INVALID_NODE) {
    if (app.hover.image_handle)
      label = "Image resize handle — drag corner to scale";
    else
      label = "Image — drag corner handle to resize · scale saved in markup";
    tone = StatusTone::Info;
  } else if (app.hover.stretch != xanatoo::INVALID_NODE) {
    bool expanded = false;
    if (app.hover.stretch_pane >= 0 && app.hover.stretch_pane < 2) {
      auto& sd = doc_of(app, app.panes[app.hover.stretch_pane]);
      expanded = sd.get(app.hover.stretch).expanded;
    }
    label = expanded ? "Stretchtext — click ▾ to collapse"
                     : "Stretchtext — click ▸ to expand";
    tone = StatusTone::Info;
  } else if (!app.hover.href.empty()) {
    if (app.hover.is_cite)
      label = "Cite: ";
    else if (app.hover.href[0] == '#')
      label = "Anchor: ";
    else if (xanatoo::is_gemini_url(app.hover.href))
      label = "Gemini: ";
    else if (xanatoo::is_reddit_url(app.hover.href))
      label = "Reddit: ";
    else if (xanatoo::is_lemmy_url(app.hover.href))
      label = "Lemmy: ";
    else if (xanatoo::is_nostr_ref(app.hover.href))
      label = "Nostr: ";
    else if (app.hover.href.rfind("http://", 0) == 0 ||
             app.hover.href.rfind("https://", 0) == 0)
      label = "Web: ";
    else if (app.hover.href.rfind("mailto:", 0) == 0)
      label = "Mail: ";
    else
      label = "Document: ";
    label += app.hover.href;
    if (!app.hover.rel.empty()) {
      label += "  rel=";
      label += app.hover.rel;
    }
    if (app.hover.href[0] == '#')
      label += "    click to jump";
    else if (app.hover.href.rfind("http://", 0) == 0 ||
             app.hover.href.rfind("https://", 0) == 0 ||
             app.hover.href.rfind("mailto:", 0) == 0)
      label += "    click to open externally";
    else
      label += "    click to open";
    tone = StatusTone::Info;
  } else if (app.browse_mode) {
    label = app.gamepad
                ? "Browse · R-stick pointer · A click · LB+A caret · "
                  "D-pad nav · L-stick scroll · Start help · F2 edit"
                : "Browse mode — arrows move · Enter open · F2 edit · F1 help";
    tone = StatusTone::Browse;
  } else {
    label =
        "F1 help · Ctrl+S save · Ctrl+A block · Ctrl+Shift+A all · "
        "Ctrl+F leap · Ctrl+B/I style · drag to select";
    tone = StatusTone::Hint;
  }

  // Selection / document statistics shown in the right-hand panel
  // (idle status only).
  if (!prompt_active(app) && !app.help_mode && !app.outline_mode) {
    Pane& fp = focused_pane(app);
    if (!fp.sel.empty()) {
      const std::string t = xanatoo::selected_text(doc_of(app, fp), fp.sel);
      size_t words = 0, chars = 0;
      bool in_word = false;
      for (unsigned char c : t) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
          in_word = false;
        } else {
          ++chars;
          if (!in_word) {
            in_word = true;
            ++words;
          }
        }
      }
      st.stats = std::to_string(words) + " sel words · " +
                 std::to_string(chars) + " sel chars";
    } else {
      const auto ds = xanatoo::document_stats(doc_of(app, fp));
      st.stats = std::to_string(ds.words) + " words · " +
                 std::to_string(ds.chars) + " chars";
      if (ds.headings > 0)
        st.stats += " · " + std::to_string(ds.headings) + " headings";
    }
  }
  return st;
}

void draw_status_bar(App& app) {
  namespace th = xanatoo::theme;
  const th::Palette& p = th::palette();
  const StatusLine st = compute_status(app);
  const int bar_h = status_bar_h;
  const SDL_Rect bar{0, app.win_h - bar_h - static_cast<int>(app.ui_bottom),
                     app.win_w, bar_h};
  th::fill(app.sdl, bar, p.face);
  th::hline(app.sdl, bar.x, bar.x + bar.w - 1, bar.y, p.highlight);

  Font& font = app.fonts.regular;
  const int pad_x = 8;
  const int stats_w = st.stats.empty() ? 0 : font.measure(st.stats) + 2 * pad_x;
  SDL_Rect left{bar.x + 3, bar.y + 3, bar.w - 6 - (stats_w ? stats_w + 3 : 0),
                bar_h - 5};
  SDL_Rect right{bar.x + bar.w - 3 - stats_w, left.y, stats_w, left.h};

  th::Rgb ink = p.text_dim;
  th::Rgb well = p.face;
  switch (st.tone) {
    case StatusTone::Prompt:
      well = p.field;
      ink = p.text;
      break;
    case StatusTone::Notice:
      well = p.notice;
      ink = p.notice_text;
      break;
    case StatusTone::Info:
      ink = p.accent_dark;
      break;
    case StatusTone::Browse:
      ink = th::rgb(0x2E, 0x6B, 0x3C);
      break;
    case StatusTone::Warn:
      ink = th::rgb(0x9A, 0x55, 0x10);
      break;
    case StatusTone::Hint:
      break;
  }
  th::fill(app.sdl, left, well);
  th::bevel(app.sdl, left, th::Bevel::Sunken);
  if (st.tone == StatusTone::Prompt) {
    // Focus ring on the active entry field.
    SDL_Rect ring{left.x + 1, left.y + 1, left.w - 2, left.h - 2};
    SDL_SetRenderDrawColor(app.sdl, p.accent.r, p.accent.g, p.accent.b, 255);
    SDL_RenderDrawRect(app.sdl, &ring);
  }

  const int ty = left.y + (left.h - font.line_height()) / 2 + font.ascender();
  SDL_Rect clip{left.x + 2, left.y + 1, left.w - 4, left.h - 2};
  SDL_RenderSetClipRect(app.sdl, &clip);
  app.renderer.draw_text(font, static_cast<float>(left.x + pad_x),
                         static_cast<float>(ty), st.text, ink.r, ink.g, ink.b);
  SDL_RenderSetClipRect(app.sdl, nullptr);

  if (stats_w) {
    th::bevel(app.sdl, right, th::Bevel::Sunken);
    app.renderer.draw_text(font, static_cast<float>(right.x + pad_x),
                           static_cast<float>(ty), st.stats, p.text_dim.r,
                           p.text_dim.g, p.text_dim.b);
  }
}

}  // namespace xanatoo::app
