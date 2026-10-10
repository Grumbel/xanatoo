// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// glyphtoo — interactive document composition engine

#include "glyphtoo/types.hpp"
#include "glyphtoo/font.hpp"
#include "glyphtoo/layout.hpp"
#include "glyphtoo/render.hpp"
#include "glyphtoo/parse.hpp"
#include "glyphtoo/editor.hpp"
#include "glyphtoo/images.hpp"

#include <SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

// Open an external URL in the system browser / handler.
// Only http(s) and mailto: are allowed (avoids shell injection / local file surprises).
static void open_external_url(const std::string& url) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0 &&
      url.rfind("mailto:", 0) != 0) {
    std::cerr << "Refusing to open non-http(s)/mailto URL: " << url << "\n";
    return;
  }

#if defined(__linux__)
  pid_t pid = fork();
  if (pid == 0) {
    // Double-fork so the browser is reparented and does not leave a zombie.
    if (fork() > 0) _exit(0);
    setsid();
    execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  } else if (pid > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
  } else {
    std::cerr << "fork failed for xdg-open\n";
  }
#elif defined(__APPLE__)
  pid_t pid = fork();
  if (pid == 0) {
    if (fork() > 0) _exit(0);
    setsid();
    execlp("open", "open", url.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  } else if (pid > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
  }
#else
  std::cout << "Open URL not implemented on this platform: " << url << "\n";
#endif
}

// Fallback when examples/welcome.md cannot be found on disk.
static const char* FALLBACK_DOC =
    "# Untitled\n\n"
    "Open a document with `glyphtoo file.md`, or place "
    "`examples/welcome.md` next to the binary.\n\n"
    "Press **F1** for the keyboard reference.\n";

static std::string find_default_document() {
  // Prefer a real file so relative image paths resolve.
  const char* candidates[] = {
      "examples/welcome.md",
      "../examples/welcome.md",
      "../../examples/welcome.md",
      nullptr,
  };
  for (const char** p = candidates; *p; ++p) {
    if (fs::exists(*p)) return fs::absolute(*p).string();
  }
#ifdef __linux__
  // Relative to the executable: <prefix>/bin/glyphtoo → <prefix>/share/glyphtoo/
  // and also sibling examples/ when running from a build tree.
  try {
    fs::path exe = fs::read_symlink("/proc/self/exe");
    fs::path dir = exe.parent_path();
    const fs::path near[] = {
        dir / "examples" / "welcome.md",
        dir / ".." / "examples" / "welcome.md",
        dir / ".." / "share" / "glyphtoo" / "welcome.md",
        dir / ".." / "share" / "glyphtoo" / "examples" / "welcome.md",
    };
    for (const auto& c : near) {
      std::error_code ec;
      auto canon = fs::weakly_canonical(c, ec);
      if (!ec && fs::exists(canon)) return canon.string();
    }
  } catch (...) {
  }
#endif
  return {};
}

static std::string find_system_font() {
  const char* candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/Library/Fonts/Arial.ttf",
      nullptr
  };
  for (const char** p = candidates; *p; ++p) {
    if (fs::exists(*p)) return *p;
  }
  return {};
}

static void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options] [file] [file2]\n"
      << "\n"
      << "Options:\n"
      << "  -f, --font <path>     TTF/OTF font file (body + headings)\n"
      << "  -o, --open <path>     Open a markup document\n"
      << "  -h, --help            Show this help\n"
      << "\n"
      << "With no file, opens examples/welcome.md when found.\n"
      << "One positional path opens that document.\n"
      << "Two paths open dual-pane: left=file1, right=file2.\n"
      << "Ctrl+S saves the focused pane's document.\n"
      << "Ctrl+\\ toggles split; drag the gutter to resize panes.\n"
      << "\n"
      << "Font resolution order: --font, GLYPHTOO_FONT env, system search.\n";
}

struct Options {
  std::string font_path;
  std::string file_path;   // primary document
  std::string file_path2;  // optional second document → opens dual-pane
};

static bool parse_args(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      print_usage(argv[0]);
      return false;
    }
    if (a == "-f" || a == "--font") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.font_path = argv[++i];
      continue;
    }
    if (a.rfind("--font=", 0) == 0) {
      opt.font_path = a.substr(7);
      continue;
    }
    if (a == "-o" || a == "--open" || a == "--file") {
      if (i + 1 >= argc) {
        std::cerr << "error: " << a << " requires a path argument\n";
        print_usage(argv[0]);
        return false;
      }
      opt.file_path = argv[++i];
      continue;
    }
    if (!a.empty() && a[0] != '-') {
      if (opt.file_path.empty()) {
        opt.file_path = a;
        continue;
      }
      if (opt.file_path2.empty()) {
        opt.file_path2 = a;
        continue;
      }
      std::cerr << "error: unexpected argument: " << a << "\n";
      print_usage(argv[0]);
      return false;
    }
    std::cerr << "error: unknown argument: " << a << "\n";
    print_usage(argv[0]);
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, opt)) {
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_StartTextInput();

  SDL_Window* window = SDL_CreateWindow(
      "glyphtoo",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      920, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* sdl = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!sdl) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  std::string font_path = opt.font_path;
  if (font_path.empty()) {
    if (const char* env = std::getenv("GLYPHTOO_FONT")) {
      if (env[0] != '\0') font_path = env;
    }
  }
  if (font_path.empty()) {
    font_path = find_system_font();
  }
  if (font_path.empty()) {
    std::cerr << "No font found. Pass one with --font <path.ttf>\n"
              << "(tried common DejaVu / Liberation / FreeSans locations)\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  if (!fs::exists(font_path)) {
    std::cerr << "Font file does not exist: " << font_path << "\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "Font: " << font_path << "\n";

  int body_px = 17;
  int heading_px = 26;
  glyphtoo::Font body_font, heading_font;
  if (!body_font.load(font_path, body_px, sdl) ||
      !heading_font.load(font_path, heading_px, sdl)) {
    std::cerr << "Failed to load font: " << font_path << "\n";
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }


  auto load_doc = [&](const std::string& path) -> glyphtoo::Document {
    if (path.empty()) {
      return glyphtoo::parse_markup(FALLBACK_DOC);
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "Failed to open document: " << path << "\n";
      return glyphtoo::parse_markup(FALLBACK_DOC);
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    std::cout << "Opened: " << path << "\n";
    return glyphtoo::parse_markup(source);
  };

  auto parent_dir = [](const std::string& path) -> std::string {
    if (path.empty()) return {};
    std::filesystem::path p(path);
    if (p.has_parent_path()) return p.parent_path().string();
    return {};
  };

  glyphtoo::Document docs[2];
  std::string paths[2];
  paths[0] = opt.file_path;
  paths[1] = opt.file_path2;
  if (paths[0].empty()) {
    paths[0] = find_default_document();
    if (paths[0].empty())
      std::cerr << "Note: examples/welcome.md not found; starting empty.\n";
  }
  docs[0] = load_doc(paths[0]);
  docs[1] = paths[1].empty() ? docs[0] : load_doc(paths[1]);
  // If only one file, pane 1 shares docs[0] until a second file is used
  bool shared_doc = paths[1].empty();
  bool dirty_flags[2] = {false, false};

  glyphtoo::ImageCache images(sdl);

  auto update_title = [&]() {
    std::string title = "glyphtoo";
    auto label = [&](int i) {
      std::string s;
      if (dirty_flags[i]) s += "*";
      if (paths[i].empty()) s += "untitled";
      else s += paths[i];
      return s;
    };
    if (shared_doc || paths[1].empty()) {
      title += " — " + label(0);
    } else {
      title += " — " + label(0) + " | " + label(1);
    }
    SDL_SetWindowTitle(window, title.c_str());
  };
  update_title();

  glyphtoo::LayoutEngine layout_engine(body_font, heading_font, &images);
  glyphtoo::Renderer renderer(sdl, body_font, heading_font, &images);

  struct Pane {
    int doc_index = 0;  // which docs[] entry
    glyphtoo::Selection sel{};
    float scroll_y = 0.f;
    float preferred_x = 0.f;
    glyphtoo::LayoutResult layout{};
    int x = 0;
    int w = 0;
  };

  Pane panes[2];
  panes[0].doc_index = 0;
  panes[1].doc_index = shared_doc ? 0 : 1;
  glyphtoo::ensure_valid_caret(docs[0], panes[0].sel.focus);
  panes[0].sel.anchor = panes[0].sel.focus;
  glyphtoo::ensure_valid_caret(docs[panes[1].doc_index], panes[1].sel.focus);
  panes[1].sel.anchor = panes[1].sel.focus;

  int focus = 0;
  bool split = !paths[1].empty();  // auto-split when two files given
  float split_ratio = 0.5f;        // left pane fraction
  bool dragging_sel = false;
  bool dragging_split = false;
  bool dragging_scroll = false;
  int scroll_drag_pane = 0;
  bool dragging_image = false;
  glyphtoo::NodeId resize_image = glyphtoo::INVALID_NODE;
  float resize_start_x = 0.f;
  float resize_start_scale = 1.f;
  // Block drag-reorder (grab left margin of a layout box)
  bool dragging_block = false;
  glyphtoo::NodeId drag_block_id = glyphtoo::INVALID_NODE;
  glyphtoo::NodeId drop_target_id = glyphtoo::INVALID_NODE;
  bool drop_before = true;
  float drop_line_y = -1.f;  // content y for insertion indicator
  // Column gutter drag (adjust relative display_scale weights)
  bool dragging_col_gap = false;
  glyphtoo::NodeId col_gap_left = glyphtoo::INVALID_NODE;
  glyphtoo::NodeId col_gap_right = glyphtoo::INVALID_NODE;
  float col_gap_start_x = 0.f;
  float col_gap_left_w = 1.f;
  float col_gap_right_w = 1.f;
  Uint32 last_click_ticks = 0;
  float last_click_x = -999.f, last_click_y = -999.f;

  auto doc_of = [&](Pane& p) -> glyphtoo::Document& {
    return docs[p.doc_index];
  };
  auto path_of = [&](Pane& p) -> std::string& {
    return paths[p.doc_index];
  };
  auto base_of = [&](Pane& p) -> std::string {
    return parent_dir(paths[p.doc_index]);
  };

  glyphtoo::History histories[2];
  histories[0].reset(docs[0], panes[0].sel);
  histories[1].reset(docs[panes[1].doc_index], panes[1].sel);

  int win_w = 920, win_h = 720;
  bool running = true;
  bool needs_layout = true;

  // Leap-style search (Canon Cat inspiration)
  bool search_mode = false;
  std::string search_query;
  size_t search_match_len = 0;
  bool replace_mode = false;
  bool replace_focus = false;  // false=edit find, true=edit replacement
  std::string replace_text;
  bool save_as_mode = false;
  std::string save_as_path;
  bool link_edit_mode = false;
  std::string link_edit_href;
  glyphtoo::NodeId link_edit_node = glyphtoo::INVALID_NODE;
  size_t link_edit_span = 0;
  bool summary_edit_mode = false;
  std::string summary_edit_text;
  glyphtoo::NodeId summary_edit_node = glyphtoo::INVALID_NODE;
  bool help_mode = false;  // F1 keyboard reference overlay
  // Hovered link href (shown in status bar when not in a modal mode)
  std::string hovered_href;
  std::string hovered_preview;  // internal #anchor target snippet
  float mouse_x = 0.f, mouse_y = 0.f;

  Uint32 caret_blink_start = SDL_GetTicks();
  auto reset_blink = [&]() { caret_blink_start = SDL_GetTicks(); };

  auto F = [&]() -> Pane& { return panes[focus]; };

  auto checkpoint = [&]() {
    Pane& p = F();
    histories[p.doc_index].push(doc_of(p), p.sel);
  };
  auto mark_dirty = [&]() {
    int di = F().doc_index;
    if (!dirty_flags[di]) {
      dirty_flags[di] = true;
      update_title();
    }
  };
  auto edit = [&]() {
    checkpoint();
    mark_dirty();
  };

  auto layout_panes = [&]() {
    const int gutter = split ? 6 : 0;
    if (split) {
      int left_w = static_cast<int>(split_ratio * (win_w - gutter));
      left_w = std::max(120, std::min(win_w - gutter - 120, left_w));
      panes[0].x = 0;
      panes[0].w = left_w;
      panes[1].x = left_w + gutter;
      panes[1].w = win_w - panes[1].x;
    } else {
      panes[0].x = 0;
      panes[0].w = win_w;
      panes[1].x = 0;
      panes[1].w = 0;
    }
    panes[0].layout = layout_engine.layout(
        doc_of(panes[0]), static_cast<float>(panes[0].w), base_of(panes[0]));
    if (split) {
      panes[1].layout = layout_engine.layout(
          doc_of(panes[1]), static_cast<float>(panes[1].w), base_of(panes[1]));
    }
    needs_layout = false;
  };

  auto scroll_caret_into_view = [&](Pane& p) {
    auto cpos = glyphtoo::caret_screen_pos(
        p.layout, p.sel.focus, 0.f,
        static_cast<float>(body_font.line_height()));
    if (!cpos.valid) return;
    float doc_y = cpos.y;
    float margin = 40.f;
    if (doc_y - p.scroll_y < margin) {
      p.scroll_y = std::max(0.f, doc_y - margin);
    } else if (doc_y - p.scroll_y > win_h - margin - 30.f) {
      p.scroll_y = doc_y - (win_h - margin - 30.f);
    }
  };

  auto pane_at = [&](float mx) -> int {
    if (!split) return 0;
    return (mx >= static_cast<float>(panes[1].x)) ? 1 : 0;
  };

  auto local_x = [&](float mx, int pi) {
    return mx - static_cast<float>(panes[pi].x);
  };

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_WINDOWEVENT:
          if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            win_w = ev.window.data1;
            win_h = ev.window.data2;
            needs_layout = true;
          }
          break;

        case SDL_MOUSEWHEEL: {
          int mx = 0, my = 0;
          SDL_GetMouseState(&mx, &my);
          int pi = pane_at(static_cast<float>(mx));
          Pane& p = panes[pi];
          p.scroll_y -= ev.wheel.y * 48.f;
          p.scroll_y = std::max(0.f, p.scroll_y);
          break;
        }

        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y);

            // Drag scrollbar (right edge of a pane)?
            {
              int pi = pane_at(mx);
              int sx = panes[pi].x + panes[pi].w - 8;
              if (mx >= sx && my < win_h - 28) {
                focus = pi;
                dragging_scroll = true;
                scroll_drag_pane = pi;
                // Jump scroll so thumb centres under cursor
                float view_h = static_cast<float>(win_h - 28);
                float content_h = std::max(view_h, panes[pi].layout.content_height);
                float max_s = std::max(0.f, content_h - view_h + 40.f);
                float ratio = (view_h > 0.f) ? (my / view_h) : 0.f;
                panes[pi].scroll_y = std::max(0.f, std::min(max_s, ratio * max_s));
                break;
              }
            }

            // Drag the split gutter?
            if (split) {
              int gutter_x = panes[1].x - 3;
              if (mx >= gutter_x && mx <= gutter_x + 6) {
                dragging_split = true;
                break;
              }
            }

            focus = pane_at(mx);
            Pane& p = F();
            float lx = local_x(mx, focus);

            // Image resize handle (bottom-right 12px square)?
            {
              const float hs = 12.f;
              for (const auto& pi : p.layout.images) {
                float hx = pi.x + pi.width - hs;
                float hy = pi.y + pi.height - hs - p.scroll_y;
                if (lx >= hx && lx <= hx + hs && my >= hy && my <= hy + hs) {
                  edit();
                  dragging_image = true;
                  resize_image = pi.node;
                  resize_start_x = mx;
                  resize_start_scale = doc_of(p).get(pi.node).display_scale;
                  if (resize_start_scale < 0.05f) resize_start_scale = 1.f;
                  break;
                }
              }
              if (dragging_image) break;
            }

            // Column gutter drag (between side-by-side columns)
            {
              float adj_y = my + p.scroll_y;
              for (const auto& gap : p.layout.column_gaps) {
                if (adj_y < gap.y || adj_y > gap.y + gap.height) continue;
                if (std::abs(lx - gap.x) > 8.f) continue;
                edit();
                dragging_col_gap = true;
                col_gap_left = gap.left;
                col_gap_right = gap.right;
                col_gap_start_x = mx;
                float lw = doc_of(p).get(gap.left).display_scale;
                float rw = doc_of(p).get(gap.right).display_scale;
                if (lw < 0.1f) lw = 1.f;
                if (rw < 0.1f) rw = 1.f;
                col_gap_left_w = lw;
                col_gap_right_w = rw;
                break;
              }
              if (dragging_col_gap) break;
            }

            // Block drag: grab left margin (16px) of a layout box
            {
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + 16.f) continue;
                const auto& bn = doc_of(p).get(box.node);
                // Only reorder movable siblings (not structural containers)
                if (bn.kind == glyphtoo::NodeKind::Document ||
                    bn.kind == glyphtoo::NodeKind::Columns ||
                    bn.kind == glyphtoo::NodeKind::Column ||
                    bn.kind == glyphtoo::NodeKind::List)
                  continue;
                if (bn.parent == glyphtoo::INVALID_NODE) continue;
                dragging_block = true;
                drag_block_id = box.node;
                drop_target_id = glyphtoo::INVALID_NODE;
                drop_line_y = -1.f;
                break;
              }
              if (dragging_block) break;
            }

            if (auto sid = glyphtoo::hit_test_stretch(p.layout, lx, my, p.scroll_y)) {
              glyphtoo::NodeId id = *sid;
              if (doc_of(p).get(id).kind == glyphtoo::NodeKind::Stretch) {
                edit();
                doc_of(p).get(id).expanded = !doc_of(p).get(id).expanded;
                needs_layout = true;
                break;
              }
            }

            // Follow links: internal #anchors jump in-document; http(s)/mailto open externally
            if (auto href = glyphtoo::hit_test_link(
                    doc_of(p), p.layout, lx, my, p.scroll_y)) {
              if (auto dest = glyphtoo::jump_to_href(doc_of(p), *href)) {
                p.sel.focus = *dest;
                p.sel.anchor = p.sel.focus;
                scroll_caret_into_view(p);
                reset_blink();
                std::cout << "Followed link: " << *href << "\n";
                break;
              } else if (!href->empty() && (*href)[0] != '#') {
                std::cout << "Opening external link: " << *href << "\n";
                open_external_url(*href);
                break;
              }
            }

            // Click a transclusion → jump to its source passage
            {
              bool followed = false;
              float adj_y = my + p.scroll_y;
              for (const auto& box : p.layout.boxes) {
                if (adj_y < box.y || adj_y > box.y + box.height) continue;
                if (lx < box.x || lx > box.x + box.width) continue;
                const auto& bn = doc_of(p).get(box.node);
                if (bn.kind != glyphtoo::NodeKind::Transclude) continue;
                if (auto dest = glyphtoo::jump_to_href(doc_of(p), bn.src)) {
                  p.sel.focus = *dest;
                  p.sel.anchor = p.sel.focus;
                  scroll_caret_into_view(p);
                  reset_blink();
                  std::cout << "Followed transclusion: " << bn.src << "\n";
                  followed = true;
                }
                break;
              }
              if (followed) break;
            }

            if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              Uint32 now = SDL_GetTicks();
              bool dbl = (now - last_click_ticks) < 400 &&
                         std::abs(mx - last_click_x) < 4.f &&
                         std::abs(my - last_click_y) < 4.f;
              last_click_ticks = now;
              last_click_x = mx;
              last_click_y = my;

              p.sel.focus = *hit;
              if (dbl) {
                // Select word under caret
                glyphtoo::Document& dd = doc_of(p);
                glyphtoo::move_word_left(dd, p.sel.focus);
                p.sel.anchor = p.sel.focus;
                glyphtoo::move_word_right(dd, p.sel.focus);
              } else if (!(SDL_GetModState() & KMOD_SHIFT)) {
                p.sel.anchor = p.sel.focus;
              }
              p.preferred_x = lx;
              dragging_sel = !dbl;
              reset_blink();
            }
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) {
            if (dragging_block && drag_block_id != glyphtoo::INVALID_NODE &&
                drop_target_id != glyphtoo::INVALID_NODE &&
                drop_target_id != drag_block_id) {
              edit();
              if (glyphtoo::relocate_block(doc_of(F()), drag_block_id,
                                          drop_target_id, drop_before)) {
                needs_layout = true;
                F().sel.focus.node = drag_block_id;
                F().sel.focus.span_index = 0;
                F().sel.focus.char_index = 0;
                F().sel.anchor = F().sel.focus;
              }
            }
            dragging_sel = false;
            dragging_split = false;
            dragging_scroll = false;
            dragging_image = false;
            dragging_block = false;
            drag_block_id = glyphtoo::INVALID_NODE;
            drop_target_id = glyphtoo::INVALID_NODE;
            drop_line_y = -1.f;
            dragging_col_gap = false;
            col_gap_left = glyphtoo::INVALID_NODE;
            col_gap_right = glyphtoo::INVALID_NODE;
            resize_image = glyphtoo::INVALID_NODE;
          }
          break;

        case SDL_MOUSEMOTION:
          mouse_x = static_cast<float>(ev.motion.x);
          mouse_y = static_cast<float>(ev.motion.y);
          if (dragging_scroll) {
            float my = static_cast<float>(ev.motion.y);
            Pane& sp = panes[scroll_drag_pane];
            float view_h = static_cast<float>(win_h - 28);
            float content_h = std::max(view_h, sp.layout.content_height);
            float max_s = std::max(0.f, content_h - view_h + 40.f);
            float ratio = (view_h > 0.f) ? (my / view_h) : 0.f;
            ratio = std::max(0.f, std::min(1.f, ratio));
            sp.scroll_y = ratio * max_s;
            break;
          }
          if (dragging_split && split) {
            float mx = static_cast<float>(ev.motion.x);
            split_ratio = mx / static_cast<float>(win_w);
            split_ratio = std::max(0.15f, std::min(0.85f, split_ratio));
            needs_layout = true;
            break;
          }
          if (dragging_image && resize_image != glyphtoo::INVALID_NODE) {
            float mx = static_cast<float>(ev.motion.x);
            float dx = mx - resize_start_x;
            // 200px drag ≈ 1.0 scale change
            float scale = resize_start_scale + dx / 200.f;
            scale = std::max(0.15f, std::min(3.0f, scale));
            doc_of(F()).get(resize_image).display_scale = scale;
            needs_layout = true;
            break;
          }
          if (dragging_col_gap && col_gap_left != glyphtoo::INVALID_NODE &&
              col_gap_right != glyphtoo::INVALID_NODE) {
            float mx = static_cast<float>(ev.motion.x);
            float dx = mx - col_gap_start_x;
            // Shift weight from one column to the other; keep total constant
            float total = col_gap_left_w + col_gap_right_w;
            float delta = dx / 200.f * total;
            float nl = col_gap_left_w + delta;
            float nr = col_gap_right_w - delta;
            if (nl < 0.2f) {
              nl = 0.2f;
              nr = total - nl;
            }
            if (nr < 0.2f) {
              nr = 0.2f;
              nl = total - nr;
            }
            doc_of(F()).get(col_gap_left).display_scale = nl;
            doc_of(F()).get(col_gap_right).display_scale = nr;
            needs_layout = true;
            break;
          }
          if (dragging_block && drag_block_id != glyphtoo::INVALID_NODE) {
            Pane& p = F();
            float adj_y = mouse_y + p.scroll_y;
            drop_target_id = glyphtoo::INVALID_NODE;
            drop_line_y = -1.f;
            for (const auto& box : p.layout.boxes) {
              if (box.node == drag_block_id) continue;
              if (adj_y < box.y || adj_y > box.y + box.height) continue;
              const auto& bn = doc_of(p).get(box.node);
              // Skip structural containers as drop targets
              if (bn.kind == glyphtoo::NodeKind::Document ||
                  bn.kind == glyphtoo::NodeKind::Columns ||
                  bn.kind == glyphtoo::NodeKind::List)
                continue;
              // Top half → before, bottom half → after
              drop_before = (adj_y < box.y + box.height * 0.5f);
              drop_target_id = box.node;
              drop_line_y = drop_before ? box.y : (box.y + box.height);
              break;
            }
            break;
          }
          if (dragging_sel) {
            float mx = static_cast<float>(ev.motion.x);
            float my = static_cast<float>(ev.motion.y);
            // Keep focus on the pane where drag started
            Pane& p = F();
            float lx = local_x(mx, focus);
            if (auto hit = glyphtoo::hit_test(p.layout, lx, my, p.scroll_y)) {
              p.sel.focus = *hit;
              p.preferred_x = lx;
            }
          } else {
            // Track link under cursor for status-bar + tooltip hover feedback
            float mx = static_cast<float>(ev.motion.x);
            float my = static_cast<float>(ev.motion.y);
            mouse_x = mx;
            mouse_y = my;
            int hover_pane = pane_at(mx);
            Pane& hp = panes[hover_pane];
            float lx = local_x(mx, hover_pane);
            if (auto href = glyphtoo::hit_test_link(doc_of(hp), hp.layout, lx, my,
                                                    hp.scroll_y)) {
              hovered_href = *href;
              if (auto prev = glyphtoo::link_preview_text(doc_of(hp), *href)) {
                hovered_preview = *prev;
              } else {
                hovered_preview.clear();
              }
            } else {
              hovered_href.clear();
              hovered_preview.clear();
            }
          }
          break;

        case SDL_TEXTINPUT: {
          if (help_mode) break;
          if (save_as_mode) {
            save_as_path += ev.text.text;
            break;
          }
          if (link_edit_mode) {
            link_edit_href += ev.text.text;
            break;
          }
          if (summary_edit_mode) {
            summary_edit_text += ev.text.text;
            break;
          }
          if (search_mode) {
            if (replace_mode && !search_query.empty() && replace_focus) {
              replace_text += ev.text.text;
              break;
            }
            search_query += ev.text.text;
            Pane& p = F();
            size_t mlen = 0;
            glyphtoo::Caret from = p.sel.focus;
            if (from.char_index > 0) from.char_index -= 1;
            if (auto hit = glyphtoo::find_next(doc_of(p), search_query, from, mlen)) {
              p.sel.anchor = *hit;
              p.sel.focus = *hit;
              for (size_t i = 0; i < mlen; ++i)
                glyphtoo::move_right(doc_of(p), p.sel.focus);
              search_match_len = mlen;
              scroll_caret_into_view(p);
            }
            reset_blink();
            break;
          }
          edit();
          Pane& p = F();
          glyphtoo::Document& td = doc_of(p);
          if (!p.sel.empty()) {
            glyphtoo::delete_selection(td, p.sel);
          }
          glyphtoo::insert_text(td, p.sel.focus, ev.text.text);
          p.sel.anchor = p.sel.focus;
          needs_layout = true;
          reset_blink();
          break;
        }

        case SDL_KEYDOWN: {
          bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
          bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
          Pane& p = F();
          glyphtoo::Document& d = doc_of(p);

          // Help overlay: only F1 (toggle) and Esc (close)
          if (help_mode) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                ev.key.keysym.sym == SDLK_F1) {
              help_mode = false;
            }
            break;
          }

          auto clear_sel = [&]() { p.sel.anchor = p.sel.focus; };
          auto move_and_sel = [&](auto mover) {
            if (!shift) clear_sel();
            mover();
            if (!shift) p.sel.anchor = p.sel.focus;
          };

          switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
              if (save_as_mode) {
                save_as_mode = false;
                save_as_path.clear();
              } else if (link_edit_mode) {
                link_edit_mode = false;
                link_edit_href.clear();
              } else if (summary_edit_mode) {
                summary_edit_mode = false;
                summary_edit_text.clear();
              } else if (search_mode) {
                search_mode = false;
                replace_mode = false;
                replace_focus = false;
                search_query.clear();
                replace_text.clear();
                reset_blink();
              } else if (!p.sel.empty()) {
                p.sel.anchor = p.sel.focus;
                reset_blink();
              }
              // Esc no longer quits — use Ctrl+Q
              break;

            case SDLK_F1:
              help_mode = !help_mode;
              break;

            case SDLK_EQUALS:
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
              if (ctrl) {
                body_px = std::min(48, body_px + 1);
                heading_px = std::min(64, heading_px + 2);
                body_font.set_size(body_px);
                heading_font.set_size(heading_px);
                needs_layout = true;
              }
              break;

            case SDLK_MINUS:
            case SDLK_KP_MINUS:
              if (ctrl) {
                body_px = std::max(10, body_px - 1);
                heading_px = std::max(14, heading_px - 2);
                body_font.set_size(body_px);
                heading_font.set_size(heading_px);
                needs_layout = true;
              }
              break;

            case SDLK_q:
              if (ctrl && shift) {
                // Toggle block quote on the current text block
                edit();
                auto& node = d.get(p.sel.focus.node);
                if (node.kind == glyphtoo::NodeKind::BlockQuote) {
                  node.kind = glyphtoo::NodeKind::Paragraph;
                } else if (node.kind == glyphtoo::NodeKind::Paragraph ||
                           node.kind == glyphtoo::NodeKind::Heading ||
                           node.kind == glyphtoo::NodeKind::ListItem) {
                  node.kind = glyphtoo::NodeKind::BlockQuote;
                  node.level = 0;
                }
                needs_layout = true;
                reset_blink();
              } else if (ctrl) {
                if (dirty_flags[0] || dirty_flags[1]) {
                  std::cerr << "Unsaved changes — Ctrl+S to save, "
                               "Ctrl+Q again after saving, or save and quit.\n";
                  // Soft warning once: if already warned, allow quit
                  static bool quit_warned = false;
                  if (!quit_warned) {
                    quit_warned = true;
                    break;
                  }
                }
                running = false;
              }
              break;

            case SDLK_f:
              if (ctrl) {
                search_mode = true;
                replace_mode = false;
                search_query.clear();
                search_match_len = 0;
                std::cout << "Leap search: type to find, F3 next, Esc cancel\n";
                reset_blink();
              }
              break;

            case SDLK_h:
              if (ctrl) {
                // Leap replace: find with current query (or enter search), then type replacement
                search_mode = true;
                replace_mode = true;
                replace_text.clear();
                if (search_query.empty()) {
                  std::cout << "Leap replace: type find text, Tab to edit replacement, Enter replaces\n";
                } else {
                  std::cout << "Leap replace: type replacement for \"" << search_query << "\"\n";
                }
                reset_blink();
              }
              break;

            case SDLK_F3:
            case SDLK_g:
              if (ev.key.keysym.sym == SDLK_F3 || ctrl) {
                if (search_query.empty() && !search_mode) break;
                search_mode = true;
                Pane& pp = F();
                size_t mlen = 0;
                if (auto hit = glyphtoo::find_next(
                        doc_of(pp), search_query, pp.sel.focus, mlen)) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  for (size_t i = 0; i < mlen; ++i)
                    glyphtoo::move_right(doc_of(pp), pp.sel.focus);
                  search_match_len = mlen;
                  scroll_caret_into_view(pp);
                  reset_blink();
                }
              }
              break;

            case SDLK_TAB:
              if (replace_mode) {
                replace_focus = !replace_focus;
                break;
              }
              // Tab / Shift+Tab: nest or promote list items
              {
                auto& d = doc_of(F());
                auto& p = F();
                if (p.sel.focus.node != glyphtoo::INVALID_NODE &&
                    d.get(p.sel.focus.node).kind ==
                        glyphtoo::NodeKind::ListItem) {
                  edit();
                  bool ok = shift ? glyphtoo::outdent_list_item(d, p.sel.focus)
                                  : glyphtoo::indent_list_item(d, p.sel.focus);
                  if (ok) {
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  break;
                }
              }
              if (split) {
                focus = 1 - focus;
                reset_blink();
              }
              break;

            case SDLK_BACKSLASH:
              if (ctrl) {
                split = !split;
                if (split) {
                  if (shared_doc) {
                    panes[1].doc_index = 0;
                  }
                  panes[1].sel = panes[0].sel;
                  panes[1].scroll_y = panes[0].scroll_y;
                } else {
                  focus = 0;
                }
                needs_layout = true;
              }
              break;

            case SDLK_BACKSPACE:
              if (save_as_mode) {
                if (!save_as_path.empty()) {
                  while (!save_as_path.empty() &&
                         (static_cast<unsigned char>(save_as_path.back()) & 0xc0) == 0x80)
                    save_as_path.pop_back();
                  if (!save_as_path.empty()) save_as_path.pop_back();
                }
                break;
              }
              if (link_edit_mode) {
                if (!link_edit_href.empty()) {
                  while (!link_edit_href.empty() &&
                         (static_cast<unsigned char>(link_edit_href.back()) & 0xc0) == 0x80)
                    link_edit_href.pop_back();
                  if (!link_edit_href.empty()) link_edit_href.pop_back();
                }
                break;
              }
              if (summary_edit_mode) {
                if (!summary_edit_text.empty()) {
                  while (!summary_edit_text.empty() &&
                         (static_cast<unsigned char>(summary_edit_text.back()) & 0xc0) == 0x80)
                    summary_edit_text.pop_back();
                  if (!summary_edit_text.empty()) summary_edit_text.pop_back();
                }
                break;
              }
              if (search_mode) {
                std::string& target = (replace_mode && replace_focus) ? replace_text : search_query;
                if (!target.empty()) {
                  while (!target.empty() &&
                         (static_cast<unsigned char>(target.back()) & 0xc0) == 0x80)
                    target.pop_back();
                  if (!target.empty()) target.pop_back();
                }
                reset_blink();
                break;
              }
              edit();
              if (!p.sel.empty()) {
                glyphtoo::delete_selection(d, p.sel);
              } else {
                glyphtoo::delete_backward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_DELETE:
              edit();
              if (!p.sel.empty()) {
                glyphtoo::delete_selection(d, p.sel);
              } else {
                glyphtoo::delete_forward(d, p.sel.focus);
                p.sel.anchor = p.sel.focus;
              }
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (replace_mode && search_mode && !search_query.empty()) {
                Pane& pp = F();
                glyphtoo::Document& dd = doc_of(pp);
                if (!pp.sel.empty()) {
                  edit();
                  glyphtoo::delete_selection(dd, pp.sel);
                  if (!replace_text.empty()) {
                    glyphtoo::insert_text(dd, pp.sel.focus, replace_text);
                    pp.sel.anchor = pp.sel.focus;
                  }
                  needs_layout = true;
                }
                // Find next
                size_t mlen = 0;
                if (auto hit = glyphtoo::find_next(dd, search_query, pp.sel.focus, mlen)) {
                  pp.sel.anchor = *hit;
                  pp.sel.focus = *hit;
                  for (size_t i = 0; i < mlen; ++i)
                    glyphtoo::move_right(dd, pp.sel.focus);
                  search_match_len = mlen;
                  scroll_caret_into_view(pp);
                }
                reset_blink();
                break;
              }
              if (link_edit_mode) {
                if (link_edit_node != glyphtoo::INVALID_NODE) {
                  auto& node = d.get(link_edit_node);
                  if (link_edit_span < node.spans.size()) {
                    node.spans[link_edit_span].href = link_edit_href;
                    node.spans[link_edit_span].kind = glyphtoo::InlineKind::Link;
                    mark_dirty();
                    needs_layout = true;
                    std::cout << "Link href: " << link_edit_href << "\n";
                  }
                }
                link_edit_mode = false;
                link_edit_href.clear();
                break;
              }
              if (summary_edit_mode) {
                if (summary_edit_node != glyphtoo::INVALID_NODE) {
                  d.get(summary_edit_node).summary = summary_edit_text;
                  mark_dirty();
                  needs_layout = true;
                }
                summary_edit_mode = false;
                summary_edit_text.clear();
                break;
              }
              if (save_as_mode) {
                if (!save_as_path.empty()) {
                  std::string text = glyphtoo::serialize_markup(d);
                  std::ofstream out(save_as_path);
                  if (!out) {
                    std::cerr << "Failed to save: " << save_as_path << "\n";
                  } else {
                    out << text;
                    path_of(p) = save_as_path;
                    dirty_flags[p.doc_index] = false;
                    update_title();
                    std::cout << "Saved as: " << save_as_path << "\n";
                  }
                }
                save_as_mode = false;
                save_as_path.clear();
                break;
              }
              edit();
              if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
              glyphtoo::split_block(d, p.sel.focus);
              p.sel.anchor = p.sel.focus;
              needs_layout = true;
              reset_blink();
              break;

            case SDLK_LEFT:
              move_and_sel([&] {
                if (ctrl) {
                  glyphtoo::move_word_left(d, p.sel.focus);
                } else if (!glyphtoo::move_left(d, p.sel.focus)) {
                  glyphtoo::move_to_prev_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_RIGHT:
              move_and_sel([&] {
                if (ctrl) {
                  glyphtoo::move_word_right(d, p.sel.focus);
                } else if (!glyphtoo::move_right(d, p.sel.focus)) {
                  glyphtoo::move_to_next_node(d, p.sel.focus);
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_UP:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (glyphtoo::move_block_up(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!glyphtoo::move_up(p.layout, p.sel.focus, p.preferred_x))
                    glyphtoo::move_to_prev_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_DOWN:
              if ((ev.key.keysym.mod & KMOD_ALT) != 0) {
                edit();
                if (glyphtoo::move_block_down(d, p.sel.focus.node)) {
                  needs_layout = true;
                  reset_blink();
                }
              } else {
                move_and_sel([&] {
                  if (!glyphtoo::move_down(p.layout, p.sel.focus, p.preferred_x))
                    glyphtoo::move_to_next_node(d, p.sel.focus);
                });
                scroll_caret_into_view(p);
                reset_blink();
              }
              break;

            case SDLK_PAGEUP:
              p.scroll_y = std::max(0.f, p.scroll_y - win_h * 0.8f);
              break;
            case SDLK_PAGEDOWN:
              p.scroll_y += win_h * 0.8f;
              break;

            case SDLK_HOME:
              move_and_sel([&] {
                if (ctrl) {
                  // Document start
                  glyphtoo::ensure_valid_caret(d, p.sel.focus);
                  // walk to first text node
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                } else {
                  // Line start via layout: find leftmost glyph on same baseline
                  auto cpos = glyphtoo::caret_screen_pos(
                      p.layout, p.sel.focus, p.scroll_y,
                      static_cast<float>(body_font.line_height()));
                  if (cpos.valid) {
                    float best_x = 1e9f;
                    glyphtoo::Caret best = p.sel.focus;
                    for (const auto& g : p.layout.glyphs) {
                      if (g.node != p.sel.focus.node) continue;
                      if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
                      if (g.x < best_x) {
                        best_x = g.x;
                        best = {g.node, g.span_index, g.char_index};
                      }
                    }
                    p.sel.focus = best;
                  } else {
                    p.sel.focus.span_index = 0;
                    p.sel.focus.char_index = 0;
                  }
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_END:
              move_and_sel([&] {
                auto cpos = glyphtoo::caret_screen_pos(
                    p.layout, p.sel.focus, p.scroll_y,
                    static_cast<float>(body_font.line_height()));
                if (cpos.valid) {
                  float best_x = -1e9f;
                  glyphtoo::Caret best = p.sel.focus;
                  for (const auto& g : p.layout.glyphs) {
                    if (g.node != p.sel.focus.node) continue;
                    if (std::abs(g.y - (cpos.y + p.scroll_y)) > 2.f) continue;
                    if (g.x >= best_x) {
                      best_x = g.x;
                      best = {g.node, g.span_index, g.char_index + 1};
                    }
                  }
                  p.sel.focus = best;
                }
              });
              scroll_caret_into_view(p);
              reset_blink();
              break;

            case SDLK_c:
              if (ctrl) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                if (!text.empty()) SDL_SetClipboardText(text.c_str());
              }
              break;

            case SDLK_x:
              if (ctrl) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                if (!text.empty()) {
                  SDL_SetClipboardText(text.c_str());
                  edit();
                  glyphtoo::delete_selection(d, p.sel);
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_v:
              if (ctrl) {
                if (SDL_HasClipboardText()) {
                  char* clip = SDL_GetClipboardText();
                  if (clip && clip[0]) {
                    edit();
                    if (!p.sel.empty()) glyphtoo::delete_selection(d, p.sel);
                    glyphtoo::insert_text(d, p.sel.focus, clip);
                    p.sel.anchor = p.sel.focus;
                    needs_layout = true;
                    reset_blink();
                  }
                  if (clip) SDL_free(clip);
                }
              }
              break;

            case SDLK_z:
              if (ctrl) {
                if (histories[p.doc_index].undo(d, p.sel)) {
                  if (panes[0].doc_index == panes[1].doc_index) {
                    glyphtoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_y:
              if (ctrl) {
                if (histories[p.doc_index].redo(d, p.sel)) {
                  if (panes[0].doc_index == panes[1].doc_index) {
                    glyphtoo::ensure_valid_caret(
                        doc_of(panes[1 - focus]), panes[1 - focus].sel.focus);
                    panes[1 - focus].sel.anchor = panes[1 - focus].sel.focus;
                  }
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_s:
              if (ctrl && shift) {
                save_as_mode = true;
                search_mode = false;
                save_as_path = path_of(p).empty() ? "untitled.md" : path_of(p);
                break;
              }
              if (ctrl) {
                std::string& spath = path_of(p);
                if (spath.empty()) {
                  std::string candidate = "untitled.md";
                  for (int n = 1; n < 1000; ++n) {
                    if (!std::filesystem::exists(candidate)) break;
                    candidate = "untitled-" + std::to_string(n) + ".md";
                  }
                  spath = candidate;
                  std::cout << "Saving untitled buffer as: " << spath << "\n";
                }
                {
                  std::string text = glyphtoo::serialize_markup(d);
                  std::ofstream out(spath);
                  if (!out) {
                    std::cerr << "Failed to save: " << spath << "\n";
                  } else {
                    out << text;
                    std::cout << "Saved: " << spath << "\n";
                    dirty_flags[p.doc_index] = false;
                    update_title();
                  }
                }
              }
              break;

            case SDLK_d:
              if (ctrl) {
                edit();
                if (auto nid = glyphtoo::duplicate_block(d, p.sel.focus.node)) {
                  p.sel.focus.node = *nid;
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_8:
              // Ctrl+Shift+8 — toggle unordered list (common editor binding)
              if (ctrl && shift) {
                edit();
                if (glyphtoo::toggle_list(d, p.sel.focus, false)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_7:
              // Ctrl+Shift+7 — toggle ordered list
              if (ctrl && shift) {
                edit();
                if (glyphtoo::toggle_list(d, p.sel.focus, true)) {
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_b:
              if (ctrl && !p.sel.empty()) {
                edit();
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Strong);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_i:
              if (ctrl && !p.sel.empty()) {
                edit();
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Emphasis);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_BACKQUOTE:
              if (ctrl && !p.sel.empty()) {
                edit();
                glyphtoo::apply_style_to_selection(d, p.sel, glyphtoo::InlineKind::Code);
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_k:
              if (ctrl && !p.sel.empty()) {
                std::string text = glyphtoo::selected_text(d, p.sel);
                std::string href = "#" + text;
                edit();
                glyphtoo::apply_style_to_selection(
                    d, p.sel, glyphtoo::InlineKind::Link, href);
                link_edit_mode = true;
                search_mode = false;
                save_as_mode = false;
                link_edit_node = p.sel.focus.node;
                link_edit_span = p.sel.focus.span_index;
                link_edit_href = href;
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_e:
              if (ctrl && shift) {
                // Edit summary of enclosing stretch
                glyphtoo::NodeId sid = glyphtoo::enclosing_stretch(d, p.sel.focus.node);
                if (sid != glyphtoo::INVALID_NODE) {
                  summary_edit_mode = true;
                  summary_edit_node = sid;
                  summary_edit_text = d.get(sid).summary;
                  search_mode = false;
                  save_as_mode = false;
                  link_edit_mode = false;
                }
              } else if (ctrl) {
                edit();
                std::string summary = glyphtoo::selected_text(d, p.sel);
                if (glyphtoo::wrap_block_in_stretch(d, p.sel.focus.node, summary)) {
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_u:
              if (ctrl && shift) {
                // Unwrap enclosing stretch
                edit();
                if (auto first = glyphtoo::unwrap_stretch(d, p.sel.focus.node)) {
                  p.sel.focus.node = *first;
                  p.sel.focus.span_index = 0;
                  p.sel.focus.char_index = 0;
                  p.sel.anchor = p.sel.focus;
                  needs_layout = true;
                  reset_blink();
                }
              }
              break;

            case SDLK_0:
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
            case SDLK_5:
            case SDLK_6:
              if (ctrl && !shift) {
                int level = static_cast<int>(ev.key.keysym.sym - SDLK_0);
                edit();
                auto& node = d.get(p.sel.focus.node);
                if (level == 0) {
                  if (node.kind == glyphtoo::NodeKind::Heading ||
                      node.kind == glyphtoo::NodeKind::BlockQuote)
                    node.kind = glyphtoo::NodeKind::Paragraph;
                  node.level = 0;
                } else {
                  node.kind = glyphtoo::NodeKind::Heading;
                  node.level = level;
                }
                needs_layout = true;
                reset_blink();
              }
              break;

            case SDLK_LEFTBRACKET:
              if (ctrl && shift) {
                // Collapse all stretch nodes
                edit();
                for (auto& node : d.nodes) {
                  if (node.kind == glyphtoo::NodeKind::Stretch)
                    node.expanded = false;
                }
                needs_layout = true;
              }
              break;

            case SDLK_RIGHTBRACKET:
              if (ctrl && shift) {
                // Expand all stretch nodes
                edit();
                for (auto& node : d.nodes) {
                  if (node.kind == glyphtoo::NodeKind::Stretch)
                    node.expanded = true;
                }
                needs_layout = true;
              }
              break;

            case SDLK_a:
              if (ctrl && shift) {
                // Select entire document (first text node → last)
                // Walk layout glyphs for bounds
                if (!p.layout.glyphs.empty()) {
                  const auto& first = p.layout.glyphs.front();
                  const auto& last = p.layout.glyphs.back();
                  p.sel.anchor = {first.node, first.span_index, first.char_index};
                  p.sel.focus = {last.node, last.span_index, last.char_index + 1};
                }
              } else if (ctrl) {
                const auto& n = d.get(p.sel.focus.node);
                p.sel.anchor = {p.sel.focus.node, 0, 0};
                p.sel.focus.span_index = n.spans.empty() ? 0 : n.spans.size() - 1;
                if (!n.spans.empty()) {
                  size_t len = 0;
                  const std::string& ts = n.spans.back().text;
                  for (size_t i = 0; i < ts.size(); ) {
                    unsigned char c = static_cast<unsigned char>(ts[i]);
                    if (c < 0x80) { ++i; ++len; }
                    else if ((c & 0xe0) == 0xc0) { i += 2; ++len; }
                    else if ((c & 0xf0) == 0xe0) { i += 3; ++len; }
                    else if ((c & 0xf8) == 0xf0) { i += 4; ++len; }
                    else ++i;
                  }
                  p.sel.focus.char_index = len;
                }
              }
              break;

            default:
              break;
          }
          break;
        }
      }
    }

    if (needs_layout) layout_panes();

    float lh = static_cast<float>(body_font.line_height());
    renderer.clear(248, 246, 240);

    int pane_count = split ? 2 : 1;
    for (int i = 0; i < pane_count; ++i) {
      Pane& pane = panes[i];
      float max_scroll = std::max(0.f, pane.layout.content_height - win_h + 40.f + 28.f);
      if (pane.scroll_y > max_scroll) pane.scroll_y = max_scroll;

      SDL_Rect clip{pane.x, 0, pane.w, win_h - 28};
      SDL_RenderSetClipRect(sdl, &clip);

      float ox = static_cast<float>(pane.x);

      if (!pane.sel.empty()) {
        auto rects = glyphtoo::selection_rects(
            pane.layout, pane.sel, pane.scroll_y, lh);
        renderer.draw_selection(rects, ox);
      }

      renderer.draw(pane.layout, pane.scroll_y, ox);

      if (i == focus) {
        auto cpos = glyphtoo::caret_screen_pos(
            pane.layout, pane.sel.focus, pane.scroll_y, lh);
        if (cpos.valid) pane.preferred_x = cpos.x;
        Uint32 blink_phase = (SDL_GetTicks() - caret_blink_start) % 1000;
        if (blink_phase < 600) {
          renderer.draw_caret(cpos, ox);
        }
      }

      // Focus indicator: thin bar at top of focused pane
      if (i == focus) {
        SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
        SDL_Rect bar{pane.x, 0, pane.w, 3};
        SDL_RenderFillRect(sdl, &bar);
      }

      // Column gutters (subtle vertical handles)
      for (const auto& gap : pane.layout.column_gaps) {
        int gx = pane.x + static_cast<int>(gap.x);
        int gy = static_cast<int>(gap.y - pane.scroll_y);
        int gh = static_cast<int>(gap.height);
        bool active = dragging_col_gap && gap.left == col_gap_left;
        SDL_SetRenderDrawColor(sdl, active ? 25 : 190, active ? 90 : 188,
                               active ? 170 : 180, 255);
        SDL_Rect gbar{gx - 1, gy, 3, gh};
        SDL_RenderFillRect(sdl, &gbar);
      }

      // Block drag-drop: insertion line + source highlight
      if (dragging_block && i == focus) {
        // Dim the dragged block
        for (const auto& box : pane.layout.boxes) {
          if (box.node != drag_block_id) continue;
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(sdl, 80, 140, 220, 60);
          SDL_Rect r{pane.x + static_cast<int>(box.x),
                     static_cast<int>(box.y - pane.scroll_y),
                     static_cast<int>(box.width),
                     static_cast<int>(box.height)};
          SDL_RenderFillRect(sdl, &r);
          SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        if (drop_line_y >= 0.f) {
          int ly = static_cast<int>(drop_line_y - pane.scroll_y);
          SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
          SDL_Rect line{pane.x + 24, ly - 1, pane.w - 40, 3};
          SDL_RenderFillRect(sdl, &line);
        }
      } else if (i == focus && !dragging_block) {
        // Subtle grip dots on left margin of the box under the cursor
        float adj_y = mouse_y + pane.scroll_y;
        float lx = mouse_x - static_cast<float>(pane.x);
        for (const auto& box : pane.layout.boxes) {
          if (adj_y < box.y || adj_y > box.y + box.height) continue;
          if (lx < box.x || lx > box.x + 20.f) continue;
          const auto& bn = doc_of(pane).get(box.node);
          if (bn.kind == glyphtoo::NodeKind::Document ||
              bn.kind == glyphtoo::NodeKind::Columns ||
              bn.kind == glyphtoo::NodeKind::Column ||
              bn.kind == glyphtoo::NodeKind::List)
            continue;
          int gx = pane.x + static_cast<int>(box.x) + 4;
          int gy = static_cast<int>(box.y - pane.scroll_y + box.height * 0.5f);
          SDL_SetRenderDrawColor(sdl, 160, 158, 150, 255);
          for (int d = -6; d <= 6; d += 6) {
            SDL_Rect dot{gx, gy + d, 3, 3};
            SDL_RenderFillRect(sdl, &dot);
          }
          break;
        }
      }

      // Scrollbar thumb
      {
        float view_h = static_cast<float>(win_h - 28);
        float content_h = std::max(view_h, pane.layout.content_height);
        float thumb_h = std::max(24.f, view_h * (view_h / content_h));
        float track = view_h - thumb_h;
        float ratio = (max_scroll > 0.f) ? (pane.scroll_y / max_scroll) : 0.f;
        float thumb_y = ratio * track;
        int sx = pane.x + pane.w - 6;
        SDL_SetRenderDrawColor(sdl, 190, 188, 180, 255);
        SDL_Rect track_r{sx, 0, 5, static_cast<int>(view_h)};
        SDL_RenderFillRect(sdl, &track_r);
        SDL_SetRenderDrawColor(sdl, 120, 118, 110, 255);
        SDL_Rect thumb{sx, static_cast<int>(thumb_y), 5, static_cast<int>(thumb_h)};
        SDL_RenderFillRect(sdl, &thumb);
      }
    }

    SDL_RenderSetClipRect(sdl, nullptr);

    // Divider between panes
    if (split) {
      int mid = panes[1].x - 3;
      SDL_SetRenderDrawColor(sdl, 210, 208, 200, 255);
      SDL_Rect div{mid, 0, 6, win_h - 28};
      SDL_RenderFillRect(sdl, &div);
    }

    // Bottom status bar — shortcuts, or leap query when searching
    {
      const int bar_h = 28;
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      if (search_mode || save_as_mode || link_edit_mode || summary_edit_mode) {
        SDL_SetRenderDrawColor(sdl, 30, 30, 40, 240);
      } else {
        SDL_SetRenderDrawColor(sdl, 236, 234, 228, 245);
      }
      SDL_Rect bar{0, win_h - bar_h, win_w, bar_h};
      SDL_RenderFillRect(sdl, &bar);
      // Top edge line
      if (search_mode || save_as_mode || link_edit_mode || summary_edit_mode) {
        SDL_SetRenderDrawColor(sdl, 80, 160, 255, 255);
      } else {
        SDL_SetRenderDrawColor(sdl, 200, 198, 190, 255);
      }
      SDL_RenderDrawLine(sdl, 0, win_h - bar_h, win_w, win_h - bar_h);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      std::string label;
      uint8_t tr = 40, tg = 40, tb = 45;
      if (save_as_mode) {
        label = "Save as: ";
        label += save_as_path.empty() ? "…" : save_as_path;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (link_edit_mode) {
        label = "Link href: ";
        label += link_edit_href.empty() ? "…" : link_edit_href;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (summary_edit_mode) {
        label = "Stretch summary: ";
        label += summary_edit_text.empty() ? "…" : summary_edit_text;
        label += "    Enter confirm · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (search_mode && replace_mode) {
        label = "Find: ";
        label += search_query.empty() ? "…" : search_query;
        label += "  →  Replace: ";
        label += replace_text.empty() ? "…" : replace_text;
        label += replace_focus ? "  [repl] " : "  [find] ";
        label += " Tab switch · Enter replace · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (search_mode) {
        label = "Leap: ";
        label += search_query.empty() ? "…" : search_query;
        label += "    F3 next · Esc cancel";
        tr = 220; tg = 230; tb = 245;
      } else if (dragging_block) {
        label = "Moving block — drop before/after any block (columns & lists OK)";
        tr = 40; tg = 90; tb = 160;
      } else if (dragging_col_gap) {
        label = "Resizing columns — drag to adjust relative widths";
        tr = 40; tg = 90; tb = 160;
      } else if (!hovered_href.empty()) {
        label = "Link: ";
        label += hovered_href;
        if (hovered_href[0] == '#')
          label += "    click to jump";
        else
          label += "    click to open";
        tr = 40; tg = 90; tb = 160;
      } else {
        label =
            "F1 help · Ctrl+S save · Ctrl+Z/Y undo · Ctrl+Q quit · "
            "Ctrl++/- zoom · Ctrl+B/I/` style · Ctrl+F leap · Ctrl+H replace";
        tr = 70; tg = 68; tb = 62;
      }
      float baseline = static_cast<float>(win_h - 8);
      renderer.draw_text(body_font, 10.f, baseline, label, tr, tg, tb);
    }

    // F1 help overlay
    if (help_mode) {
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 20, 22, 28, 210);
      SDL_Rect dim{0, 0, win_w, win_h};
      SDL_RenderFillRect(sdl, &dim);

      const int margin = 28;
      const int panel_w = std::min(720, win_w - margin * 2);
      const int panel_h = std::min(win_h - margin * 2 - 10, win_h - 40);
      const int px = (win_w - panel_w) / 2;
      const int py = (win_h - panel_h) / 2;
      SDL_SetRenderDrawColor(sdl, 248, 246, 240, 250);
      SDL_Rect panel{px, py, panel_w, panel_h};
      SDL_RenderFillRect(sdl, &panel);
      SDL_SetRenderDrawColor(sdl, 25, 90, 170, 255);
      SDL_RenderDrawRect(sdl, &panel);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

      static const char* lines[] = {
          "glyphtoo — keyboard reference",
          "",
          "Files",
          "  Ctrl+S              Save",
          "  Ctrl+Shift+S        Save As",
          "  Ctrl+Q              Quit",
          "",
          "Edit",
          "  Enter               Split paragraph / list item",
          "  Backspace / Delete  Delete (merge at block start)",
          "  Ctrl+Z / Ctrl+Y     Undo / Redo",
          "  Ctrl+C / X / V      Copy / Cut / Paste",
          "  Ctrl+B / I / ` / K  Bold / Italic / Code / Link",
          "  Ctrl+D              Duplicate block",
          "  Alt+Up / Alt+Down   Move block among siblings",
          "  Ctrl+E              Wrap in stretchtext",
          "  Ctrl+Shift+E        Edit stretch summary",
          "  Ctrl+Shift+U        Unwrap stretchtext",
          "  Ctrl+0 … Ctrl+6     Paragraph / heading level",
          "  Ctrl+Shift+Q        Toggle block quote",
          "  Ctrl+Shift+8 / 7    Toggle unordered / ordered list",
          "  Tab / Shift+Tab     Indent / outdent list item",
          "  Ctrl+Shift+[ / ]    Collapse / expand all stretch",
          "",
          "Navigate",
          "  Arrows / Home/End   Move caret",
          "  Ctrl+Left/Right     Word motion",
          "  Ctrl+A / Shift+A    Select block / whole document",
          "  Ctrl+F / F3         Leap search / next match",
          "  Ctrl+H              Leap replace",
          "  Click link          Follow #anchor or open URL",
          "  Click transclusion  Jump to source passage",
          "",
          "View & layout",
          "  Ctrl+\\              Toggle dual pane",
          "  Tab (not on list)   Focus other pane",
          "  Drag left margin    Reorder / reparent block",
          "  Drag column gutter  Resize column widths",
          "  Drag image corner   Resize image",
          "  Ctrl++ / Ctrl+-     Zoom text",
          "  Click ▸/▾           Expand / collapse stretchtext",
          "",
          "F1 or Esc — close this help",
      };
      const int nlines = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
      float line_h = static_cast<float>(body_font.line_height());
      // Fit text: if too tall, use a slightly tighter step
      float step = line_h;
      if (nlines * step > panel_h - 24.f)
        step = std::max(12.f, (panel_h - 24.f) / static_cast<float>(nlines));

      float ty = static_cast<float>(py + 14 + body_font.ascender());
      for (int i = 0; i < nlines; ++i) {
        const char* s = lines[i];
        bool heading = (s[0] != '\0' && s[0] != ' ');
        bool title = (i == 0);
        if (title)
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             25, 90, 170);
        else if (heading && s[0] != '\0')
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             40, 50, 70);
        else
          renderer.draw_text(body_font, static_cast<float>(px + 18), ty, s,
                             50, 48, 42);
        ty += step;
        if (ty > py + panel_h - 8) break;
      }
    }

    // Hover preview tooltip for internal #anchor links
    if (!hovered_preview.empty() && !search_mode && !save_as_mode &&
        !link_edit_mode && !summary_edit_mode && !help_mode) {
      const std::string tip = hovered_preview;
      int tw = body_font.measure(tip);
      const int pad_x = 10, pad_y = 6;
      int box_w = tw + pad_x * 2;
      int box_h = body_font.line_height() + pad_y * 2;
      int bx = static_cast<int>(mouse_x) + 12;
      int by = static_cast<int>(mouse_y) - box_h - 8;
      // Clamp to window (above status bar)
      if (bx + box_w > win_w - 4) bx = win_w - box_w - 4;
      if (bx < 4) bx = 4;
      if (by < 4) by = static_cast<int>(mouse_y) + 16;
      if (by + box_h > win_h - 32) by = win_h - 32 - box_h;

      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(sdl, 30, 40, 55, 230);
      SDL_Rect box{bx, by, box_w, box_h};
      SDL_RenderFillRect(sdl, &box);
      SDL_SetRenderDrawColor(sdl, 90, 140, 210, 255);
      SDL_RenderDrawRect(sdl, &box);
      SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
      float tip_baseline =
          static_cast<float>(by + pad_y + body_font.ascender());
      renderer.draw_text(body_font, static_cast<float>(bx + pad_x), tip_baseline,
                         tip, 230, 235, 245);
    }

    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
