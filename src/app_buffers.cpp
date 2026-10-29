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
#include "xanatoo/ebooks.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/feed.hpp"
#include "xanatoo/hackernews.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/sitemap.hpp"
#include "xanatoo/wikipedia.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

namespace fs = std::filesystem;

namespace xanatoo::app {

std::string resolve_open_path(App& app, std::string path) {
  if (path.empty() || !xanatoo::is_pandoc_path(path)) return path;
  auto imp = xanatoo::import_via_pandoc(path);
  if (!imp.ok) {
    std::cerr << "Import failed: " << imp.error << "\n";
    return {};
  }
  return imp.markdown_path;
}

// Read-only Markdown index of directory `dir`: parent link, then
// sub-directories and files (dot files hidden), each linked by absolute path
// so the links resolve whatever the buffer path looks like.
static std::string directory_listing(const fs::path& dir) {
  std::vector<std::pair<std::string, bool>> entries;  // name, is directory
  std::error_code ec;
  for (auto it = fs::directory_iterator(dir, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    std::string name = it->path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    std::error_code ec2;
    entries.emplace_back(std::move(name), it->is_directory(ec2));
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second;
    return xanatoo::ascii_lower(a.first) < xanatoo::ascii_lower(b.first);
  });
  std::string md = "# " + dir.string() + "\n\n";
  if (ec) md += "Cannot read directory: " + ec.message() + "\n\n";
  const fs::path parent = dir.parent_path();
  if (!parent.empty() && parent != dir)
    md += "- [../](" + parent.string() + "/)\n";
  for (const auto& [name, is_dir] : entries) {
    const std::string label = is_dir ? name + "/" : name;
    const std::string target = (dir / name).string() + (is_dir ? "/" : "");
    // The link syntax has no escapes: such names are listed unlinked.
    if (target.find_first_of("()[]") != std::string::npos)
      md += "- `" + label + "`\n";
    else
      md += "- [" + label + "](" + target + ")\n";
  }
  if (entries.empty() && !ec) md += "*(empty)*\n";
  return md;
}

std::string path_title(const std::string& path) {
  try {
    fs::path p(path);
    if (p.has_filename()) return p.filename().string();
    p = p.parent_path();
    if (p.has_filename()) return p.filename().string() + "/";
  } catch (...) {
  }
  return path;
}

xanatoo::Document load_doc(App& app, std::string path) {
  if (path.empty()) {
    return xanatoo::parse_document(xanatoo::fallback_document(), {});
  }
  if (xanatoo::is_pandoc_path(path)) {
    std::string md = resolve_open_path(app, path);
    if (md.empty()) {
      return xanatoo::parse_document(
          "# Import failed\n\n`" + path +
              "` is read through **pandoc**: install it, or see the "
              "console for pandoc's error.\n",
          {});
    }
    path = std::move(md);
  }
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    fs::path dir = fs::weakly_canonical(path, ec);
    if (ec) dir = fs::absolute(path);
    std::cout << "Listed: " << dir.string() << "\n";
    return xanatoo::parse_document(directory_listing(dir), {});
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

// Other http(s) pages open as reader views when built with XANATOO_WEB;
// otherwise they go to the system browser.
static bool web_in_app(std::string_view s) {
#if defined(XANATOO_WEB) && XANATOO_WEB
  return xanatoo::is_web_url(s);
#else
  (void)s;
  return false;
#endif
}

bool is_in_app_remote_url(std::string_view s) {
  return xanatoo::is_gemini_url(s) || xanatoo::is_reddit_ref(s) ||
         xanatoo::is_lemmy_ref(s) || xanatoo::is_nostr_ref(s) ||
         xanatoo::is_hn_ref(s) || xanatoo::is_ebook_url(s) ||
         xanatoo::is_feed_url(s) || xanatoo::is_wikipedia_ref(s) ||
         xanatoo::is_sitemap_ref(s) || web_in_app(s);
}

bool is_foreign_path(std::string_view path) {
  if (path.empty()) return false;
  if (is_in_app_remote_url(path) || xanatoo::starts_with(path, "http://") ||
      xanatoo::starts_with(path, "https://"))
    return true;
  if (xanatoo::path_uses_html(path) || xanatoo::is_pandoc_path(path)) return true;
  if (path == kDraftsPage) return true;
  // Directory listings (navigate_pane gives them a trailing '/').
  std::error_code ec;
  if (xanatoo::ends_with(path, "/") ||
      fs::is_directory(fs::path(std::string(path)), ec))
    return true;
  const std::string cache = xanatoo::cache_dir();
  return !cache.empty() && xanatoo::starts_with(path, cache + "/");
}

bool focused_read_only(App& app) {
  const DocBuffer* b = find_buf(app, focused_pane(app).buf);
  return b && is_foreign_path(b->path);
}

bool browsing(App& app) { return app.browse_mode || focused_read_only(app); }

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
  const BufferId id = app.buffers.back().id;
  attach_draft(app, id);
  return id;
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
  stash_draft(app, id);
  for (size_t i = app.soft_wins.size(); i-- > 0;) {
    if (app.panes[app.soft_wins[i].view].buf != id) continue;
    release_view(app, app.soft_wins[i].view);
    app.soft_wins.erase(app.soft_wins.begin() +
                        static_cast<std::ptrdiff_t>(i));
  }
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
                  [&](const FloatWindow& sw) {
                    return app.panes[sw.view].buf == id;
                  });
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
    s += path_title(path);
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

bool view_live(const App& app, int pi) {
  if (pi == 0) return true;
  if (pi == 1) return app.split;
  if (pi < kCentreViews || pi >= kMaxViews) return false;
  return std::any_of(app.soft_wins.begin(), app.soft_wins.end(),
                     [&](const FloatWindow& w) { return w.view == pi; });
}

std::vector<int> live_views(const App& app) {
  std::vector<int> v{0};
  if (app.split) v.push_back(1);
  for (const FloatWindow& w : app.soft_wins)
    if (w.view >= kCentreViews) v.push_back(w.view);
  return v;
}

int other_pane(const App& app) {
  return app.focus < kCentreViews ? 1 - app.focus : app.centre_focus;
}

void focus_view(App& app, int pi) {
  if (pi < 0 || pi >= kMaxViews) return;
  app.focus = pi;
  if (pi < kCentreViews) app.centre_focus = pi;
}

int alloc_view(App& app) {
  for (int pi = kCentreViews; pi < kMaxViews; ++pi)
    if (!view_live(app, pi)) return pi;
  return -1;
}

void refresh_views_of_buf(App& app, BufferId bi, int except, bool top) {
  DocBuffer* b = find_buf(app, bi);
  if (!b) return;
  for (int pi : live_views(app)) {
    Pane& v = app.panes[pi];
    if (pi == except || v.buf != bi) continue;
    xanatoo::ensure_valid_caret(b->doc, v.sel.focus);
    v.sel.anchor = v.sel.focus;
    v.layout = {};
    if (top) v.scroll_y = 0.f;
  }
}

void release_view(App& app, int pi) {
  if (pi < kCentreViews || pi >= kMaxViews) return;
  cancel_fetches(app, pi);
  clear_interaction_hover(app);  // may point into this view
  Pane& p = app.panes[pi];
  release_raster(p.raster);
  p = Pane{};
  if (app.focus == pi) app.focus = app.centre_focus;
  if (app.scroll_drag_pane == pi) app.dragging_scroll = false;
}

void checkpoint(App& app) {
  Pane& p = focused_pane(app);
  buf(app, p.buf).history.push(doc_of(app, p), p.sel);
}

void mark_dirty(App& app) {
  const BufferId di = focused_pane(app).buf;
  app.discard_armed = false;  // new edits require a fresh discard warning
  if (find_buf(app, di)) {
    note_edit(app, buf(app, di));
    // Virtualization: drop height hints for the edited node and ancestors
    // so the next layout remeasures only the affected spine (not the world).
    for (int pi : live_views(app)) {
      if (app.panes[pi].buf == di)
        invalidate_height_around(app, app.panes[pi],
                                 focused_pane(app).sel.focus.node);
    }
  }
}

void note_edit(App& app, DocBuffer& b) {
  if (!b.draft_due) b.draft_due = SDL_GetTicks() + kDraftDelayMs;
  if (!b.dirty) {
    b.dirty = true;
    update_title(app);
  }
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

namespace {

// New document window showing `bi` in a fresh view (cascaded from the help
// window's spot); nullptr when every view slot is taken.
FloatWindow* new_doc_window(App& app, BufferId bi) {
  const int view = alloc_view(app);
  if (view < 0) {
    flash_status(app, "Too many windows");
    return nullptr;
  }
  Pane& p = app.panes[view];
  p = Pane{};
  p.buf = bi;
  push_nav_history(app, view, buf(app, bi).path);
  xanatoo::ensure_valid_caret(buf(app, bi).doc, p.sel.focus);
  p.sel.anchor = p.sel.focus;
  FloatWindow sw;
  sw.view = view;
  const int cascade = static_cast<int>(app.soft_wins.size()) * 28;
  const int top = static_cast<int>(app.ui_top);
  sw.x = app.help.x + 30 + cascade;
  sw.y = std::max(app.help.y, top) + 30 + cascade;
  if (sw.x + sw.w > app.win_w - 20) sw.x = 40 + (cascade % 60);
  if (sw.y < top) sw.y = top + 20 + (cascade % 40);
  if (sw.y + sw.h > app.win_h - 40) sw.y = top + 20 + (cascade % 40);
  snap_soft_geom(app, sw.x, sw.y, sw.w, sw.h);
  sw.restore_x = sw.x;
  sw.restore_y = sw.y;
  app.soft_wins.push_back(std::move(sw));
  app.needs_layout = true;
  place_windows(app);
  return &app.soft_wins.back();
}

// Raise the window already showing `bi`; false when there is none.
bool raise_window_of(App& app, BufferId bi) {
  for (size_t i = 0; i < app.soft_wins.size(); ++i) {
    if (app.panes[app.soft_wins[i].view].buf != bi) continue;
    const int view = app.soft_wins[i].view;
    raise_soft(app, i);
    focus_view(app, view);
    return true;
  }
  return false;
}

}  // namespace

void pop_out_focused(App& app) {
  if (app.focus >= kCentreViews) {
    flash_status(app, "Already in a window");
    return;
  }
  Pane& p = focused_pane(app);
  const BufferId bi = p.buf;
  if (raise_window_of(app, bi)) {
    flash_status(app, "Already in a window");
    return;
  }
  const xanatoo::Selection sel = p.sel;
  const float scroll = p.scroll_y;
  FloatWindow* w = new_doc_window(app, bi);
  if (!w) return;
  Pane& v = app.panes[w->view];
  v.nav = p.nav;  // the window takes the tab's trail along
  v.sel = sel;
  v.scroll_y = scroll;
  focus_view(app, w->view);
  flash_status(app, "Popped out to window");
}

void pop_in_soft(App& app, size_t si) {
  if (si >= app.soft_wins.size()) return;
  const int view = app.soft_wins[si].view;
  Pane& v = app.panes[view];
  const int to = app.centre_focus;
  if (find_buf(app, v.buf)) {
    Pane& c = app.panes[to];
    c.buf = v.buf;
    c.sel = v.sel;
    c.scroll_y = v.scroll_y;
    c.layout = {};
    c.height_cache.clear();
    xanatoo::ensure_valid_caret(doc_of(app, c), c.sel.focus);
    if (std::find(app.tab_order.begin(), app.tab_order.end(), c.buf) ==
        app.tab_order.end())
      app.tab_order.push_back(c.buf);
    push_nav_history(app, to, path_of(app, c));
  }
  release_view(app, view);
  app.soft_wins.erase(app.soft_wins.begin() + static_cast<std::ptrdiff_t>(si));
  focus_view(app, to);
  app.needs_layout = true;
  update_title(app);
  flash_status(app, "Moved window to tab");
}

void open_browse(App& app, const std::string& path, const std::string& title) {
  if (path.empty() || !fs::exists(path)) {
    std::cerr << "Cannot open in window: " << path << "\n";
    return;
  }
  const BufferId bi = ensure_buffer(app, path);
  if (raise_window_of(app, bi)) return;
  FloatWindow* w = new_doc_window(app, bi);
  if (!w) return;
  if (!title.empty()) w->title = title;
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
