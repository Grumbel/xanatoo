// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include <algorithm>
#include <cassert>
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

std::string resolve_open_path(App& app, std::string path) {
  if (path.empty() || !xanatoo::is_epub_path(path)) return path;
  auto imp = xanatoo::import_epub_via_pandoc(path);
  if (!imp.ok) {
    std::cerr << "EPUB import failed: " << imp.error << "\n";
    return {};
  }
  return imp.markdown_path;
}

xanatoo::Document load_doc(App& app, std::string path) {
  if (path.empty()) {
    return xanatoo::parse_document(xanatoo::fallback_document(), {});
  }
  if (xanatoo::is_epub_path(path)) {
    std::string md = resolve_open_path(app, path);
    if (md.empty()) {
      return xanatoo::parse_document(
          "# EPUB import failed\n\nInstall **pandoc** and try again.\n", {});
    }
    path = std::move(md);
  }
  std::ifstream in(path);
  if (!in) {
    std::cerr << "Failed to open document: " << path << "\n";
    return xanatoo::parse_document(xanatoo::fallback_document(), {});
  }
  std::string source((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  std::cout << "Opened: " << path << "\n";
  return xanatoo::parse_document(source, path);
}

std::string parent_dir(const std::string& path) {
  if (path.empty()) return {};
  std::filesystem::path p(path);
  if (p.has_parent_path()) return p.parent_path().string();
  return {};
}

bool is_in_app_remote_url(std::string_view s) {
  return xanatoo::is_gemini_url(s) || xanatoo::is_reddit_url(s) ||
         xanatoo::is_lemmy_url(s) || xanatoo::is_nostr_ref(s);
}

xanatoo::Document load_doc_or_placeholder(App& app, const std::string& path) {
  if (is_in_app_remote_url(path)) {
    // Real fetch happens after navigate_to_url is defined.
    return xanatoo::parse_document("# Loading…\n", {});
  }
  return load_doc(app, path);
}

DocBuffer* find_buf(App& app, BufferId id) {
  if (id == kNoBuffer) return nullptr;
  for (auto& b : app.buffers)
    if (b.id == id) return &b;
  return nullptr;
}

BufferId add_buffer(App& app, xanatoo::Document doc, std::string path) {
  DocBuffer b;
  b.id = app.next_buffer_id++;
  b.doc = std::move(doc);
  b.path = std::move(path);
  app.buffers.push_back(std::move(b));
  return app.buffers.back().id;
}

DocBuffer& buf(App& app, BufferId id) {
  DocBuffer* b = find_buf(app, id);
  assert(b && "buffer id not found");
  if (!b) {
    // Release-build safety net: never crash on a stale id.
    if (app.buffers.empty()) {
      add_buffer(app, xanatoo::parse_document(xanatoo::fallback_document(), {}),
                 {});
    }
    return app.buffers.front();
  }
  return *b;
}

// Drop `id` from the tab strip (keeping at least one tab) and repoint panes
// that showed it at the neighbouring tab. The buffer itself is untouched.
static void leave_tab(App& app, BufferId id) {
  auto tab = std::find(app.tab_order.begin(), app.tab_order.end(), id);
  const size_t tab_i = tab == app.tab_order.end()
                           ? 0
                           : static_cast<size_t>(tab - app.tab_order.begin());
  if (tab != app.tab_order.end()) app.tab_order.erase(tab);
  if (app.tab_order.empty()) {
    BufferId nb = add_buffer(
        app, xanatoo::parse_document(xanatoo::fallback_document(), {}), {});
    buf(app, nb).history.reset(buf(app, nb).doc, xanatoo::Selection{});
    app.tab_order.push_back(nb);
  }
  const BufferId next =
      app.tab_order[std::min(tab_i, app.tab_order.size() - 1)];
  for (int pi = 0; pi < 2; ++pi) {
    Pane& p = app.panes[pi];
    if (p.buf != id && find_buf(app, p.buf)) continue;
    p.buf = next;
    p.sel = {};
    p.scroll_y = 0.f;
    p.layout = {};
    p.height_cache.clear();
    xanatoo::ensure_valid_caret(buf(app, next).doc, p.sel.focus);
    p.sel.anchor = p.sel.focus;
  }
  app.needs_layout = true;
  update_title(app);
}

void remove_buffer(App& app, BufferId id) {
  if (!find_buf(app, id)) return;
  app.soft_wins.erase(
      std::remove_if(app.soft_wins.begin(), app.soft_wins.end(),
                     [&](const FloatWindow& sw) { return sw.buf == id; }),
      app.soft_wins.end());
  leave_tab(app, id);
  app.buffers.erase(
      std::remove_if(app.buffers.begin(), app.buffers.end(),
                     [&](const DocBuffer& b) { return b.id == id; }),
      app.buffers.end());
  update_title(app);
}

void close_tab(App& app, BufferId id) {
  const bool floated =
      std::any_of(app.soft_wins.begin(), app.soft_wins.end(),
                  [&](const FloatWindow& sw) { return sw.buf == id; });
  if (floated)
    leave_tab(app, id);  // MDI: floating windows keep the buffer alive
  else
    remove_buffer(app, id);
}

std::string buffer_tab_label(App& app, BufferId bi) {
  const DocBuffer* b = find_buf(app, bi);
  if (!b) return "?";
  std::string s;
  if (b->dirty) s += "*";
  const std::string& path = b->path;
  if (path.empty()) {
    s += "untitled";
  } else if (is_in_app_remote_url(path)) {
    // URL: host + last path segment ("geminiprotocol.net/.../x.gmi"); ASCII
    // dots because the ImGui tab font has no "…".
    std::string u = path;
    if (size_t sch = u.find("://"); sch != std::string::npos)
      u.erase(0, sch + 3);
    while (u.size() > 1 && u.back() == '/') u.pop_back();
    const size_t slash = u.find('/');
    const size_t last = u.rfind('/');
    if (slash != std::string::npos && last != slash)
      u = u.substr(0, slash) + "/..." + u.substr(last);
    if (u.size() > 40) u = u.substr(0, 37) + "...";
    s += u;
  } else {
    try {
      s += fs::path(path).filename().string();
    } catch (...) {
      s += path;
    }
  }
  return s;
}

void update_title(App& app) {
  std::string title = "xanatoo";
  if (!app.buffers.empty()) {
    title += " — ";
    title += buffer_tab_label(app, app.buffers.front().id);
    if (app.buffers.size() > 1) {
      title += " (+";
      title += std::to_string(app.buffers.size() - 1);
      title += " more)";
    }
  }
  SDL_SetWindowTitle(app.window, title.c_str());
}

xanatoo::Document& doc_of(App& app, Pane& p) { return buf(app, p.buf).doc; }

std::string& path_of(App& app, Pane& p) { return buf(app, p.buf).path; }

std::string base_of(App& app, Pane& p) {
  return parent_dir(buf(app, p.buf).path);
}

bool shared_doc(App& app) { return app.panes[0].buf == app.panes[1].buf; }

Pane& focused_pane(App& app) { return app.panes[app.focus]; }

void checkpoint(App& app) {
  Pane& p = focused_pane(app);
  buf(app, p.buf).history.push(doc_of(app, p), p.sel);
}

void mark_dirty(App& app) {
  const BufferId di = focused_pane(app).buf;
  app.discard_armed = false;  // new edits require a fresh discard warning
  if (find_buf(app, di)) {
    if (!buf(app, di).dirty) {
      buf(app, di).dirty = true;
      update_title(app);
    }
    // Soft windows sharing this buffer must re-layout.
    for (auto& sw : app.soft_wins)
      if (sw.buf == di) sw.needs_layout = true;
    // Virtualization: drop height hints for the edited node and ancestors
    // so the next layout remeasures only the affected spine (not the world).
    for (int pi = 0; pi < 2; ++pi) {
      if (app.panes[pi].buf == di)
        invalidate_height_around(app, app.panes[pi],
                                 focused_pane(app).sel.focus.node);
    }
  }
}

bool any_dirty(App& app) {
  for (const auto& b : app.buffers)
    if (b.dirty) return true;
  return false;
}

bool allow_discard_dirty(App& app, BufferId di) {
  const DocBuffer* b = find_buf(app, di);
  if (!b || !b->dirty) return true;
  if (app.discard_armed) {
    app.discard_armed = false;
    return true;
  }
  app.discard_armed = true;
  flash_status(app, "Unsaved changes — Ctrl+S to save, or repeat to discard");
  std::cerr
      << "Unsaved changes — save first, or repeat the command to discard.\n";
  return false;
}

void edit(App& app) {
  checkpoint(app);
  mark_dirty(app);
}

std::string find_help_path(App& app) {
  const char* candidates[] = {
      "examples/help.md",
      "../examples/help.md",
      "../../examples/help.md",
      nullptr,
  };
  for (const char** c = candidates; *c; ++c) {
    if (fs::exists(*c)) return fs::absolute(*c).string();
  }
#ifdef __linux__
  try {
    fs::path exe = fs::read_symlink("/proc/self/exe");
    fs::path dir = exe.parent_path();
    for (const auto& c : {
             dir / "examples" / "help.md",
             dir / ".." / "examples" / "help.md",
             dir / ".." / "share" / "xanatoo" / "examples" / "help.md",
         }) {
      std::error_code ec;
      auto canon = fs::weakly_canonical(c, ec);
      if (!ec && fs::exists(canon)) return canon.string();
    }
  } catch (...) {
  }
#endif
  return {};
}

std::string find_repo_file(App& app, const char* name) {
  const char* prefixes[] = {"",       "examples/",       "../",  "../examples/",
                            "../../", "../../examples/", nullptr};
  for (const char** pre = prefixes; *pre; ++pre) {
    std::string pth = std::string(*pre) + name;
    if (fs::exists(pth)) return fs::absolute(pth).string();
  }
  // PortMaster / installed layout: XANATOO_DATA_DIR points at the port
  // folder that holds examples/, icons/, fonts/.
  if (const char* data = std::getenv("XANATOO_DATA_DIR")) {
    if (data[0] != '\0') {
      for (const auto& c : {
               fs::path(data) / name,
               fs::path(data) / "examples" / name,
           }) {
        std::error_code ec;
        auto canon = fs::weakly_canonical(c, ec);
        if (!ec && fs::exists(canon)) return canon.string();
      }
    }
  }
#ifdef __linux__
  try {
    fs::path exe = fs::read_symlink("/proc/self/exe");
    fs::path dir = exe.parent_path();
    for (const auto& c : {
             dir / name,
             dir / "examples" / name,
             dir / ".." / name,
             dir / ".." / "examples" / name,
             dir / ".." / "share" / "xanatoo" / "examples" / name,
         }) {
      std::error_code ec;
      auto canon = fs::weakly_canonical(c, ec);
      if (!ec && fs::exists(canon)) return canon.string();
    }
  } catch (...) {
  }
#endif
  return {};
}

std::string canon_path(App& app, const std::string& path) {
  try {
    return fs::weakly_canonical(path).string();
  } catch (...) {
    return path;
  }
}

BufferId find_buffer(App& app, const std::string& path) {
  if (path.empty()) return kNoBuffer;
  std::string c = canon_path(app, path);
  for (const auto& b : app.buffers) {
    if (b.path.empty()) continue;
    if (canon_path(app, b.path) == c) return b.id;
  }
  return kNoBuffer;
}

BufferId ensure_buffer(App& app, const std::string& path) {
  BufferId bi = find_buffer(app, path);
  if (bi != kNoBuffer) {
    if (std::find(app.tab_order.begin(), app.tab_order.end(), bi) ==
        app.tab_order.end())
      app.tab_order.push_back(bi);
    return bi;
  }
  bi = add_buffer(app, load_doc(app, path), path);
  app.tab_order.push_back(bi);
  update_title(app);
  return bi;
}

void pop_out_focused(App& app) {
  Pane& p = focused_pane(app);
  const BufferId bi = p.buf;
  for (size_t i = 0; i < app.soft_wins.size(); ++i) {
    if (app.soft_wins[i].buf == bi) {
      raise_soft(app, i);
      flash_status(app, "Already in a window");
      return;
    }
  }
  FloatWindow sw;
  sw.buf = bi;
  sw.sel = p.sel;
  sw.scroll = p.scroll_y;
  sw.needs_layout = true;
  const int cascade = static_cast<int>(app.soft_wins.size()) * 28;
  const int top = static_cast<int>(app.ui_top);
  sw.x = app.help.x + 30 + cascade;
  sw.y = std::max(app.help.y, top) + 30 + cascade;
  if (sw.x + sw.w > app.win_w - 20) sw.x = 40 + (cascade % 60);
  if (sw.y < top) sw.y = top + 20 + (cascade % 40);
  if (sw.y + sw.h > app.win_h - 40) sw.y = top + 20 + (cascade % 40);
  snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
  app.soft_wins.push_back(std::move(sw));
  flash_status(app, "Popped out to window");
}

void pop_in_soft(App& app, size_t si) {
  if (si >= app.soft_wins.size()) return;
  const BufferId bi = app.soft_wins[si].buf;
  if (!find_buf(app, bi)) {
    app.soft_wins.erase(app.soft_wins.begin() +
                        static_cast<std::ptrdiff_t>(si));
    return;
  }
  app.panes[app.focus].buf = bi;
  app.panes[app.focus].sel = app.soft_wins[si].sel;
  app.panes[app.focus].scroll_y = app.soft_wins[si].scroll;
  xanatoo::ensure_valid_caret(doc_of(app, app.panes[app.focus]),
                              app.panes[app.focus].sel.focus);
  app.panes[app.focus].sel.anchor = app.panes[app.focus].sel.focus;
  app.panes[app.focus].layout = {};
  app.soft_wins.erase(app.soft_wins.begin() + static_cast<std::ptrdiff_t>(si));
  app.needs_layout = true;
  update_title(app);
  flash_status(app, "Docked to tab");
}

void open_browse(App& app, const std::string& path, const std::string& title) {
  if (path.empty() || !fs::exists(path)) {
    std::cerr << "Cannot open in window: " << path << "\n";
    return;
  }
  const BufferId bi = ensure_buffer(app, path);
  // Focus existing floating window for this buffer.
  for (size_t i = 0; i < app.soft_wins.size(); ++i) {
    if (app.soft_wins[i].buf == bi) {
      raise_soft(app, i);
      std::cout << "Software window (focus): " << path << "\n";
      return;
    }
  }
  FloatWindow sw;
  sw.buf = bi;
  if (!title.empty()) sw.title = title;
  sw.needs_layout = true;
  sw.scroll = 0.f;
  xanatoo::ensure_valid_caret(buf(app, bi).doc, sw.sel.focus);
  sw.sel.anchor = sw.sel.focus;
  const int cascade = static_cast<int>(app.soft_wins.size()) * 28;
  const int top = static_cast<int>(app.ui_top);
  sw.x = app.help.x + 30 + cascade;
  sw.y = std::max(app.help.y, top) + 30 + cascade;
  if (sw.x + sw.w > app.win_w - 20) sw.x = 40 + (cascade % 60);
  if (sw.y < top) sw.y = top + 20 + (cascade % 40);
  if (sw.y + sw.h > app.win_h - 40) sw.y = top + 20 + (cascade % 40);
  snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
  app.soft_wins.push_back(std::move(sw));
  std::cout << "Software window: " << path << "\n";
}

void ensure_help_doc(App& app) {
  if (app.help_loaded) return;
  std::string hp = find_help_path(app);
  if (!hp.empty()) {
    app.help_doc = load_doc(app, hp);
    std::cout << "Help document: " << hp << "\n";
  } else {
    // Minimal built-in so F1 always works offline
    app.help_doc = xanatoo::parse_document(
        "# xanatoo help\n\n"
        "Place `examples/help.md` next to the binary for the full "
        "reference.\n\n"
        "Press **F1** or **Esc** to close.\n",
        {});
    std::cerr << "examples/help.md not found; using short built-in help.\n";
  }
  app.help_loaded = true;
  app.help_needs_layout = true;
  app.help.scroll = 0.f;
}

void layout_help(App& app) {
  if (!app.help_loaded) return;
  float mw = static_cast<float>(std::max(120, app.help.w - help_pad * 2));
  app.help_layout = app.layout_engine.layout(app.help_doc, mw);
  app.help_needs_layout = false;
}

std::tuple<int, int, int, int> help_client_rect(App& app) {
  return std::tuple{app.help.x + help_pad, app.help.y + help_title_h + 4,
                    app.help.w - help_pad * 2,
                    app.help.h - help_title_h - help_pad - 4};
}

}  // namespace xanatoo::app
