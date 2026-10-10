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

static const char* SAMPLE = R"(# The network

The web connects documents, but [hypertext](#hypertext) connects ideas.

:::stretch summary="What is hypertext?"
Hypertext is a system of linked information that can be explored
non-linearly. Passages can expand in place (*stretchtext*), images are
first-class addressable content, and layout is part of the living
document rather than a fixed page prescription.
:::

:::columns
:::column
## Original

A passage from the original document, shown in a column.
:::
:::column
## Commentary

An editable annotation linked in spirit to the original passage.
:::
:::

## Living documents

This is a live document. Click to place the caret, type to insert text,
Backspace to delete. **Enter** splits a paragraph. Shift+arrows select.
Ctrl+C / Ctrl+V / Ctrl+X for clipboard, Ctrl+Z / Ctrl+Y for undo/redo.
**Ctrl+\\** toggles a second view pane; **Tab** switches focus.
Drag the gutter to resize. Open two files for independent documents.
**Ctrl+F** leap search (type to find, F3 next, Esc cancel).
**Ctrl+←/→** move by word. Click blue links to jump to #anchors or
open external http(s) URLs in the system browser.
Drag the blue corner on images to resize. Home/End move on the line.
Click a stretchtext marker (▸ / ▾) to expand or collapse.

Inspiration: Ted Nelson's *Xanadu*, Bill Atkinson's **HyperCard**, and
Jef Raskin's *Canon Cat* — a keyboard-driven work processor that treated
text as a continuous, searchable stream.

Design pillars:

- Addressable passages and stable node IDs
- Stretchtext that reflows the living layout
- Side-by-side views of related material
- Direct manipulation of images and blocks

Ordered steps to explore:

1. Click a stretchtext marker to expand
2. Follow an internal or external link
3. Toggle dual-pane with Ctrl+\\

Try an internal link: [Living documents](#Living documents) — click to jump.
Or an external one: [Xanadu](https://en.wikipedia.org/wiki/Project_Xanadu).

Images referenced as `![alt](path.png)` load relative to the document
directory (PNG, JPEG, BMP, GIF via stb_image).
)";

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
      return glyphtoo::parse_markup(SAMPLE);
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "Failed to open document: " << path << "\n";
      return glyphtoo::parse_markup(SAMPLE);
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
  // Hovered link href (shown in status bar when not in a modal mode)
  std::string hovered_href;

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
            dragging_sel = false;
            dragging_split = false;
            dragging_scroll = false;
            dragging_image = false;
            resize_image = glyphtoo::INVALID_NODE;
          }
          break;

        case SDL_MOUSEMOTION:
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
            // Track link under cursor for status-bar hover feedback
            float mx = static_cast<float>(ev.motion.x);
            float my = static_cast<float>(ev.motion.y);
            int hover_pane = pane_at(mx);
            Pane& hp = panes[hover_pane];
            float lx = local_x(mx, hover_pane);
            if (auto href = glyphtoo::hit_test_link(doc_of(hp), hp.layout, lx, my,
                                                    hp.scroll_y)) {
              hovered_href = *href;
            } else {
              hovered_href.clear();
            }
          }
          break;

        case SDL_TEXTINPUT: {
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
              if (ctrl) {
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
                  if (node.kind == glyphtoo::NodeKind::Heading)
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
            "Ctrl+S save · Ctrl+Shift+S save as · Ctrl+Z/Y undo · "
            "Ctrl+Q quit · Ctrl++/- zoom · Ctrl+B/I/` style · Ctrl+H replace · Ctrl+F leap";
        tr = 70; tg = 68; tb = 62;
      }
      float baseline = static_cast<float>(win_h - 8);
      renderer.draw_text(body_font, 10.f, baseline, label, tr, tg, tb);
    }

    renderer.present();
  }

  SDL_StopTextInput();
  SDL_DestroyRenderer(sdl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
