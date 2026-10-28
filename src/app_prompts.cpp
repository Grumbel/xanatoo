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
#include "xanatoo/html_page.hpp"
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

bool prompt_uses_minibuf(PromptKind k) {
  switch (k) {
    case PromptKind::SaveAs:
    case PromptKind::OpenFile:
    case PromptKind::OpenInWindow:
    case PromptKind::UrlBar:
    case PromptKind::InsertImage:
    case PromptKind::InsertFigure:
    case PromptKind::InsertInclude:
      return true;
    default:
      return false;
  }
}

bool minibuf_active(App& app) { return prompt_uses_minibuf(app.prompt.kind); }

const char* prompt_label(PromptKind k) {
  switch (k) {
    case PromptKind::OpenInWindow:
      return "Open in window: ";
    case PromptKind::OpenFile:
      return "Open file: ";
    case PromptKind::UrlBar:
      return "URL: ";
    case PromptKind::SaveAs:
      return "Save as: ";
    case PromptKind::InsertImage:
      return "Image path: ";
    case PromptKind::InsertFigure:
      return "Figure image path: ";
    case PromptKind::InsertInclude:
      return "Include: ";
    default:
      return "";
  }
}

void open_prompt(App& app, PromptKind kind, std::string initial) {
  close_prompt(app);
  app.prompt.kind = kind;
  app.prompt.text = std::move(initial);
  if (prompt_uses_minibuf(kind)) minibuf_begin(app, app.prompt.text);
}

void open_search(App& app, bool replace) {
  std::string q = prompt_is_search(app) ? app.prompt.text : app.last_search;
  std::string r = prompt_is_search(app) ? app.prompt.text2 : app.last_replace;
  open_prompt(app, replace ? PromptKind::Replace : PromptKind::Search,
              std::move(q));
  app.prompt.text2 = std::move(r);
}

void close_prompt(App& app, bool forget_search) {
  if (prompt_is_search(app)) {
    if (forget_search) {
      app.last_search.clear();
      app.last_replace.clear();
    } else {
      app.last_search = app.prompt.text;
      app.last_replace = app.prompt.text2;
    }
  }
  if (prompt_uses_minibuf(app.prompt.kind)) app.minibuf.clear();
  app.prompt = Prompt{};
}

void minibuf_begin(App& app, const std::string& initial) {
  app.minibuf.clear();
  if (!initial.empty()) app.minibuf.set_text(initial);
}

void minibuf_commit_path_hist(App& app) {
  xanatoo::push_minibuffer_history(app.minibuf_hist_path, app.minibuf.text);
  xanatoo::save_minibuffer_history("path", app.minibuf_hist_path);
}

void minibuf_commit_url_hist(App& app) {
  xanatoo::push_minibuffer_history(app.minibuf_hist_url, app.minibuf.text);
  xanatoo::save_minibuffer_history("url", app.minibuf_hist_url);
}

void minibuf_sync_out(App& app) {
  if (minibuf_active(app)) app.prompt.text = app.minibuf.text;
}

bool commit_prompt(App& app, bool ctrl, bool shift) {
  if (!prompt_active(app)) return false;
  Pane& p = focused_pane(app);
  xanatoo::Document& d = doc_of(app, p);
  Prompt& pr = app.prompt;
  switch (pr.kind) {
    case PromptKind::None:
      return false;

    case PromptKind::Search:
      return false;  // Enter falls through to the document

    case PromptKind::Replace: {
      if (pr.text.empty()) return false;
      if (ctrl && shift) {
        // Replace all occurrences
        edit(app);
        size_t n = xanatoo::replace_all(d, pr.text, pr.text2);
        app.needs_layout = true;
        std::cout << "Replace all: " << n << " substitution(s)\n";
        reset_blink(app);
        return true;
      }
      if (!p.sel.empty()) {
        edit(app);
        xanatoo::delete_selection(d, p.sel);
        if (!pr.text2.empty()) {
          xanatoo::insert_text(d, p.sel.focus, pr.text2);
          p.sel.anchor = p.sel.focus;
        }
        app.needs_layout = true;
      }
      // Find next
      size_t mlen = 0;
      if (auto hit = xanatoo::find_next(d, pr.text, p.sel.focus, mlen)) {
        p.sel.anchor = *hit;
        p.sel.focus = *hit;
        xanatoo::advance_caret(d, p.sel.focus, mlen);
        scroll_caret_into_view(app, p);
      }
      reset_blink(app);
      return true;
    }

    case PromptKind::EditLink:
    case PromptKind::EditCite: {
      const bool is_cite = pr.kind == PromptKind::EditCite;
      if (pr.node != xanatoo::INVALID_NODE) {
        auto& node = d.get(pr.node);
        // BiblioEntry: edit the entry's label (cite target key)
        if (node.kind == xanatoo::NodeKind::BiblioEntry && is_cite) {
          std::string key = pr.text;
          if (key.rfind("cite:", 0) == 0) key = key.substr(5);
          if (!key.empty() && key[0] == '#') key = key.substr(1);
          if (key.empty()) key = "key";
          node.label = key;
          mark_dirty(app);
          app.needs_layout = true;
          std::cout << "Bibliography key: " << key << "\n";
        } else if (pr.span < node.spans.size()) {
          auto& sp = node.spans[pr.span];
          if (is_cite) {
            // Strip optional cite: / # prefix from the typed key
            std::string key = pr.text;
            if (key.rfind("cite:", 0) == 0) key = key.substr(5);
            if (!key.empty() && key[0] == '#') key = key.substr(1);
            sp.href = key;
            sp.kind = xanatoo::InlineKind::Cite;
            std::cout << "Citation key: " << key << "\n";
          } else {
            // Accept "href rel=glossary" or "href {rel=glossary}"
            std::string raw = pr.text;
            std::string rel;
            auto strip_rel = [&](std::string& s) {
              auto rpos = s.find("rel=");
              if (rpos == std::string::npos) return;
              size_t after = rpos + 4;
              while (after < s.size() && (s[after] == ' ' || s[after] == '\t'))
                ++after;
              size_t end = after;
              while (end < s.size() && s[end] != ' ' && s[end] != '\t' &&
                     s[end] != '}')
                ++end;
              rel = s.substr(after, end - after);
              // Remove the rel clause (and optional braces) from href
              size_t cut = rpos;
              while (cut > 0 && (s[cut - 1] == ' ' || s[cut - 1] == '\t' ||
                                 s[cut - 1] == '{'))
                --cut;
              s = s.substr(0, cut);
              while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            };
            strip_rel(raw);
            sp.href = raw;
            if (!rel.empty()) sp.rel = rel;
            sp.kind = xanatoo::InlineKind::Link;
            std::cout << "Link href: " << sp.href;
            if (!sp.rel.empty()) std::cout << " rel=" << sp.rel;
            std::cout << "\n";
          }
          mark_dirty(app);
          app.needs_layout = true;
        }
      }
      close_prompt(app);
      return true;
    }

    case PromptKind::EditSummary:
      if (pr.node != xanatoo::INVALID_NODE) {
        d.get(pr.node).summary = pr.text;
        mark_dirty(app);
        app.needs_layout = true;
      }
      close_prompt(app);
      return true;

    case PromptKind::OpenInWindow: {
      minibuf_sync_out(app);
      if (!pr.text.empty()) {
        std::string path = xanatoo::expand_user_path(pr.text);
        if (!fs::exists(path)) {
          std::string found = find_repo_file(app, path.c_str());
          if (!found.empty()) path = found;
        }
        open_browse(app, path, path);
        minibuf_commit_path_hist(app);
      }
      close_prompt(app);
      return true;
    }

    case PromptKind::UrlBar: {
      minibuf_sync_out(app);
      std::string target = pr.text;
      if (!target.empty()) minibuf_commit_url_hist(app);
      close_prompt(app);
      navigate_to_url(app, std::move(target));
      return true;
    }

    case PromptKind::OpenFile: {
      minibuf_sync_out(app);
      if (pr.text.empty()) {
        close_prompt(app);
        return true;
      }
      std::string path = xanatoo::expand_user_path(pr.text);
      if (!fs::exists(path)) {
        std::string found = find_repo_file(app, path.c_str());
        if (!found.empty()) path = found;
      }
      if (!fs::exists(path)) {
        flash_status(app, "Open: file not found");
        std::cerr << "Open: file not found: " << path << "\n";
        return true;  // prompt stays open
      }
      const BufferId existing = find_buffer(app, path);
      if (existing != kNoBuffer) {
        // Already open — switch the focused pane to that tab.
        p.buf = existing;
        app.needs_layout = true;
        update_title(app);
        push_nav_history(app, path);
        flash_status(app, "Switched to " + path);
        minibuf_commit_path_hist(app);
        close_prompt(app);
        return true;
      }
      if (!stash_or_confirm(app, p.buf)) return true;
      DocBuffer& b = buf(app, p.buf);
      b.doc = load_doc(app, path);
      b.path = path;
      b.dirty = false;
      attach_draft(app, p.buf);
      xanatoo::ensure_valid_caret(b.doc, p.sel.focus);
      p.sel.anchor = p.sel.focus;
      p.scroll_y = 0.f;
      b.history.reset(b.doc, p.sel);
      if (app.panes[0].buf == app.panes[1].buf) {
        app.panes[1 - app.focus].sel = p.sel;
        app.panes[1 - app.focus].scroll_y = 0.f;
      }
      // Ensure path is on the tab strip
      if (std::find(app.tab_order.begin(), app.tab_order.end(), p.buf) ==
          app.tab_order.end())
        app.tab_order.push_back(p.buf);
      app.needs_layout = true;
      update_title(app);
      push_nav_history(app, path);
      flash_status(app, "Opened " + path);
      std::cout << "Opened: " << path << "\n";
      minibuf_commit_path_hist(app);
      close_prompt(app);
      return true;
    }

    case PromptKind::SaveAs: {
      minibuf_sync_out(app);
      if (!pr.text.empty()) {
        const std::string target = xanatoo::expand_user_path(pr.text);
        if (xanatoo::path_uses_html(target)) {
          flash_status(app, "Cannot write HTML — use .md, .org or .sexp");
          return true;
        }
        std::string text = xanatoo::serialize_document(d, target);
        std::ofstream out(target);
        if (!out) {
          std::cerr << "Failed to save: " << target << "\n";
        } else {
          out << text;
          path_of(app, p) = target;
          buf(app, p.buf).dirty = false;
          note_saved(app, p.buf, text);
          app.discard_armed = false;
          update_title(app);
          flash_status(app, "Saved as " + target);
          std::cout << "Saved as: " << target << "\n";
          minibuf_commit_path_hist(app);
        }
      }
      close_prompt(app);
      return true;
    }

    case PromptKind::InsertImage:
    case PromptKind::InsertFigure: {
      minibuf_sync_out(app);
      if (!pr.text.empty()) {
        const std::string path = xanatoo::expand_user_path(pr.text);
        edit(app);
        const bool figure = pr.kind == PromptKind::InsertFigure;
        const bool ok = figure ? static_cast<bool>(xanatoo::insert_figure(
                                     d, p.sel.focus, path))
                               : static_cast<bool>(xanatoo::insert_image(
                                     d, p.sel.focus, path));
        if (ok) {
          std::cout << (figure ? "Inserted figure: " : "Inserted image: ")
                    << path << "\n";
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
          minibuf_commit_path_hist(app);
        }
      }
      close_prompt(app);
      return true;
    }

    case PromptKind::InsertInclude: {
      minibuf_sync_out(app);
      if (!pr.text.empty()) {
        edit(app);
        if (xanatoo::insert_transclude(d, p.sel.focus, pr.text)) {
          p.sel.anchor = p.sel.focus;
          app.needs_layout = true;
          reset_blink(app);
          std::cout << "Inserted include: " << pr.text << "\n";
          minibuf_commit_path_hist(app);
        }
      }
      close_prompt(app);
      return true;
    }
  }
  return false;
}

void flash_status(App& app, const std::string& msg) {
  app.status_notice = msg;
  app.status_notice_until = SDL_GetTicks() + 4000;
}

}  // namespace xanatoo::app
