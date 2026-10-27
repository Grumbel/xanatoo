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

void push_nav_history(App& app, const std::string& url) {
  if (url.empty()) return;
  if (app.nav_hist_pos >= 0 &&
      app.nav_hist_pos < static_cast<int>(app.nav_history.size()) &&
      app.nav_history[static_cast<size_t>(app.nav_hist_pos)] == url)
    return;
  if (app.nav_hist_pos + 1 < static_cast<int>(app.nav_history.size()))
    app.nav_history.resize(static_cast<size_t>(app.nav_hist_pos + 1));
  app.nav_history.push_back(url);
  while (static_cast<int>(app.nav_history.size()) > kMaxNavHistory)
    app.nav_history.erase(app.nav_history.begin());
  app.nav_hist_pos = static_cast<int>(app.nav_history.size()) - 1;
}

bool is_bookmarked(App& app, const std::string& url) {
  if (url.empty()) return false;
  return std::find(app.bookmarks.begin(), app.bookmarks.end(), url) !=
         app.bookmarks.end();
}

void add_bookmark(App& app, const std::string& url) {
  if (url.empty()) {
    flash_status(app, "Nothing to bookmark (no path/URL)");
    return;
  }
  if (is_bookmarked(app, url)) {
    flash_status(app, "Already bookmarked");
    return;
  }
  app.bookmarks.push_back(url);
  if (xanatoo::save_bookmarks(app.bookmarks))
    flash_status(app, "Bookmarked " + url);
  else
    flash_status(app, "Bookmark added (could not persist to disk)");
}

void remove_bookmark(App& app, const std::string& url) {
  if (url.empty()) return;
  auto it = std::find(app.bookmarks.begin(), app.bookmarks.end(), url);
  if (it == app.bookmarks.end()) {
    flash_status(app, "Not in bookmarks");
    return;
  }
  app.bookmarks.erase(it);
  if (xanatoo::save_bookmarks(app.bookmarks))
    flash_status(app, "Removed bookmark");
  else
    flash_status(app, "Bookmark removed (could not persist to disk)");
}

bool is_inapp_doc_href(App& app, const std::string& href) {
  if (href.empty() || href[0] == '#') return false;
  if (is_in_app_remote_url(href)) return false;
  if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0 ||
      href.rfind("mailto:", 0) == 0)
    return false;
  // Scheme with :// → external; bare paths and file-looking names → in-app.
  if (href.find("://") != std::string::npos) return false;
  return true;
}

std::string resolve_doc_href(App& app, const std::string& href,
                             const std::string& base_path) {
  if (href.empty()) return {};
  fs::path hp(href);
  if (hp.is_absolute()) return href;
  std::string base =
      base_path.empty() ? path_of(app, focused_pane(app)) : base_path;
  if (!base.empty() && !xanatoo::is_gemini_url(base)) {
    fs::path parent = fs::path(base).parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      auto canon = fs::weakly_canonical(parent / href, ec);
      if (!ec) return canon.string();
      return (parent / href).string();
    }
  }
  // Fall back to repo search (examples/, cwd, …).
  std::string found = find_repo_file(app, href.c_str());
  if (!found.empty()) return found;
  return href;
}

std::string link_base_url(App& app, int pi) {
  const std::string& page = path_of(app, app.panes[pi]);
  if (xanatoo::is_gemini_url(page)) return page;
  if (!page.empty()) return {};  // local file / other service
  return app.gemini_current_url;
}

// Buffer pane `pi` loads a new page into: its own, unless the other visible
// pane shows the same buffer or it has unsaved edits (then a new tab).
static BufferId page_buffer(App& app, int pi) {
  Pane& p = app.panes[pi];
  const bool shared = app.split && app.panes[1 - pi].buf == p.buf;
  const DocBuffer* b = find_buf(app, p.buf);
  if (b && !shared && !(b->dirty && !b->placeholder)) return p.buf;
  return add_buffer(app, xanatoo::parse_document("", {}), {});
}

void show_doc_in_pane(App& app, int pi, xanatoo::Document doc,
                      const std::string& label) {
  Pane& p = app.panes[pi];
  p.buf = page_buffer(app, pi);
  DocBuffer& b = buf(app, p.buf);
  b.doc = std::move(doc);
  b.path = label;
  b.dirty = false;
  b.placeholder = false;
  xanatoo::Document& nd = b.doc;
  p.sel = {};
  xanatoo::ensure_valid_caret(nd, p.sel.focus);
  p.sel.anchor = p.sel.focus;
  p.scroll_y = 0.f;
  // Drop stale layout boxes that still reference the previous tree —
  // otherwise the next frame can call doc.get() with dead node ids.
  p.layout = {};
  drop_height_hints_for_buf(app, p.buf);
  b.history.reset(nd, p.sel);
  Pane& o = app.panes[1 - pi];
  if (o.buf == p.buf) {
    o.sel = p.sel;
    o.scroll_y = 0.f;
    o.layout = {};
  }
  for (auto& sw : app.soft_wins)
    if (sw.buf == p.buf) {
      sw.needs_layout = true;
      sw.scroll = 0.f;
      sw.layout = {};
      xanatoo::ensure_valid_caret(nd, sw.sel.focus);
      sw.sel.anchor = sw.sel.focus;
    }
  if (std::find(app.tab_order.begin(), app.tab_order.end(), p.buf) ==
      app.tab_order.end())
    app.tab_order.push_back(p.buf);
  app.needs_layout = true;
  update_title(app);
}

bool navigate_to_url(App& app, std::string target, bool record_history) {
  return navigate_pane(app, app.focus, std::move(target), record_history);
}

bool navigate_pane(App& app, int pi, std::string target, bool record_history) {
  while (!target.empty() && (target.back() == ' ' || target.back() == '\t'))
    target.pop_back();
  if (target.empty()) return false;
  Pane& p = app.panes[pi];

  // Remote pages load on a worker thread; poll_fetches shows them.
  if (is_in_app_remote_url(target)) {
    std::string why;
    if (remote_fetch_supported(target, &why)) {
      if (!allow_discard_dirty(app, p.buf)) return false;
      if (const CachedPage* c = find_cached_page(app, target)) {
        // Fresh from a hover preview: no second fetch.
        const std::string final_url = c->final_url;
        show_doc_in_pane(app, pi, c->doc, final_url);
        if (xanatoo::is_gemini_url(final_url)) {
          app.gemini_current_url = final_url;
          app.browse_mode = true;
        }
        if (record_history) push_nav_history(app, final_url);
        flash_status(app, "Opened " + final_url);
        return true;
      }
      start_fetch(app, FetchPurpose::Navigate, pi, target, record_history);
      return true;
    }
    flash_status(app, why);
    if (!xanatoo::is_gemini_url(target)) return false;
    // No TLS client in this build — still offer the local gemtext sample.
    std::string sample = find_repo_file(app, "gemini-sample.gmi");
    if (sample.empty()) sample = find_repo_file(app, "examples/gemini-sample.gmi");
    if (sample.empty() || !fs::exists(sample)) return false;
    target = sample;
  }

  // Local path (or offline fallback for gemini:// when TLS is disabled)
  std::string path = target;
  if (!fs::exists(path)) {
    std::string found = find_repo_file(app, path.c_str());
    if (!found.empty()) path = found;
  }
  if (!fs::exists(path)) {
    flash_status(app, "Not found: " + target);
    return false;
  }
  app.gemini_current_url.clear();
  // Prefer an already-open buffer for this path (avoid double-load).
  {
    std::string canon = path;
    try {
      canon = fs::weakly_canonical(path).string();
    } catch (...) {
    }
    for (const DocBuffer& b : app.buffers) {
      if (b.path.empty()) continue;
      const BufferId i = b.id;
      std::string bp = b.path;
      try {
        bp = fs::weakly_canonical(bp).string();
      } catch (...) {
      }
      if (bp == canon) {
        p.buf = i;
        if (std::find(app.tab_order.begin(), app.tab_order.end(), i) ==
            app.tab_order.end())
          app.tab_order.push_back(i);
        xanatoo::ensure_valid_caret(doc_of(app, p), p.sel.focus);
        p.sel.anchor = p.sel.focus;
        p.layout = {};
        app.needs_layout = true;
        update_title(app);
        if (record_history) push_nav_history(app, path);
        flash_status(app, "Switched to " + path);
        return true;
      }
    }
  }
  if (!allow_discard_dirty(app, p.buf)) return false;
  {
    const bool was_epub = xanatoo::is_epub_path(path);
    std::string open_path = resolve_open_path(app, path);
    if (open_path.empty()) {
      flash_status(app, "EPUB import failed (need pandoc on PATH)");
      return false;
    }
    show_doc_in_pane(app, pi, load_doc(app, open_path), open_path);
    if (record_history) push_nav_history(app, open_path);
    flash_status(app, was_epub ? ("Imported EPUB → " + open_path)
                               : ("Opened " + open_path));
  }
  return true;
}

void open_transclude_in_other_pane(App& app) {
  auto& p = focused_pane(app);
  const auto& d = doc_of(app, p);
  xanatoo::NodeId id = p.sel.focus.node;
  // Walk up to a Transclude node under the caret.
  xanatoo::NodeId tid = xanatoo::INVALID_NODE;
  for (xanatoo::NodeId cur = id; cur != xanatoo::INVALID_NODE;
       cur = d.get(cur).parent) {
    if (d.get(cur).kind == xanatoo::NodeKind::Transclude) {
      tid = cur;
      break;
    }
  }
  if (tid == xanatoo::INVALID_NODE) {
    // Fallback: first visible transclude under caret y — skip, require caret
    flash_status(app, "No include under caret");
    return;
  }
  const auto& tn = d.get(tid);
  xanatoo::NodeId local = xanatoo::resolve_transclude_local(d, tn.src);
  if (!app.split) {
    app.split = true;
    app.needs_layout = true;
  }
  int other = 1 - app.focus;
  if (local != xanatoo::INVALID_NODE) {
    app.panes[other].buf = p.buf;  // same doc
    app.panes[other].sel.focus = {local, 0, 0};
    app.panes[other].sel.anchor = app.panes[other].sel.focus;
    scroll_caret_into_view(app, app.panes[other]);
    flash_status(app, "Include → other pane: " + tn.src);
    return;
  }
  // External path
  std::string path_part = tn.src;
  size_t hash = path_part.find('#');
  std::string anchor;
  if (hash != std::string::npos) {
    anchor = path_part.substr(hash);
    path_part = path_part.substr(0, hash);
  }
  if (path_part.empty()) {
    flash_status(app, "Broken include: " + tn.src);
    return;
  }
  std::filesystem::path resolved = path_part;
  std::string base = path_of(app, p);
  if (!base.empty() && resolved.is_relative())
    resolved = std::filesystem::path(base).parent_path() / path_part;
  if (!std::filesystem::exists(resolved)) {
    flash_status(app, "Broken include (missing file): " + resolved.string());
    return;
  }
  // Load into other pane's buffer if possible — navigate_to_url uses focus.
  int saved = app.focus;
  app.focus = other;
  navigate_to_url(app, resolved.string() + (anchor.empty() ? "" : anchor));
  if (!anchor.empty()) {
    if (auto dest =
            xanatoo::jump_to_href(doc_of(app, app.panes[other]), anchor)) {
      app.panes[other].sel.focus = *dest;
      app.panes[other].sel.anchor = *dest;
      scroll_caret_into_view(app, app.panes[other]);
    }
  }
  app.focus = saved;
  flash_status(app, "Opened include in other pane: " + resolved.string());
}

void activate_at_caret(App& app) {
  if (toggle_stretch_at_caret(app)) return;
  Pane& p = focused_pane(app);
  auto& d = doc_of(app, p);
  auto cpos = xanatoo::caret_screen_pos(
      p.layout, p.sel.focus, p.scroll_y,
      static_cast<float>(app.fonts.regular.line_height()));
  if (!cpos.valid) return;
  if (auto href =
          xanatoo::hit_test_link(d, p.layout, cpos.x, cpos.y, p.scroll_y)) {
    if (follow_link_split(app, app.focus, *href)) return;
    if (auto dest = xanatoo::jump_to_href(d, *href)) {
      expand_ancestors_for_node(app, dest->node);
      p.sel.focus = *dest;
      p.sel.anchor = p.sel.focus;
      scroll_caret_into_view(app, p);
      flash_status(app, "Followed " + *href);
      reset_blink(app);
    } else if (!href->empty() && (*href)[0] != '#') {
      std::string abs = *href;
      const std::string base = link_base_url(app, app.focus);
      if (!xanatoo::is_gemini_url(abs) && !base.empty()) {
        std::string resolved = xanatoo::resolve_gemini_url(base, abs);
        if (!resolved.empty()) abs = resolved;
      }
      if (is_in_app_remote_url(abs)) {
        navigate_to_url(app, std::move(abs));
      } else if (is_inapp_doc_href(app, abs)) {
        navigate_to_url(app, resolve_doc_href(app, abs));
      } else {
        xanatoo::open_external_url(*href);
        flash_status(app, "Opening " + *href);
      }
    }
  }
}

}  // namespace xanatoo::app
