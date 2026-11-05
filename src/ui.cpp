// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/examples.hpp"
#include "xanatoo/ui.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/view_settings.hpp"
#include <cmath>
#include "xanatoo/font.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/platform.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#if defined(XANATOO_SDL3) && XANATOO_SDL3
#include "xanatoo/imgui_sdl3.hpp"
#else
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#endif

#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define STBI_ONLY_PNG
#include "stb_image.h"

namespace fs = std::filesystem;

namespace xanatoo {


namespace {

// First and last `keep` bytes (on character boundaries) around an ellipsis.
std::string elide_middle(const std::string& s, size_t keep) {
  auto cont = [&](size_t i) {
    return i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80;
  };
  size_t head = keep, tail = s.size() - keep;
  while (head > 0 && cont(head)) --head;
  while (cont(tail)) ++tail;
  return s.substr(0, head) + "…" + s.substr(tail);
}

bool near_eq(float a, float b, float eps) { return std::fabs(a - b) < eps; }

// Shared View / toolbar items for display prefs (not document format).
void fill_line_spacing_items(const UiState& state, UiCommand& cmd) {
  using VS = xanatoo::ViewSettings;
  const float ls = state.view_line_spacing;
  if (ImGui::MenuItem("Tight", nullptr, near_eq(ls, VS::kLineTight, 0.05f)))
    cmd = UiCommand::ViewLineSpacingTight;
  if (ImGui::MenuItem("Normal", nullptr, near_eq(ls, VS::kLineNormal, 0.05f)))
    cmd = UiCommand::ViewLineSpacingNormal;
  if (ImGui::MenuItem("Relaxed", nullptr, near_eq(ls, VS::kLineRelaxed, 0.05f)))
    cmd = UiCommand::ViewLineSpacingRelaxed;
  if (ImGui::MenuItem("Loose", nullptr, near_eq(ls, VS::kLineLoose, 0.05f)))
    cmd = UiCommand::ViewLineSpacingLoose;
}

void fill_margins_items(const UiState& state, UiCommand& cmd,
                        std::string& arg) {
  for (float pct : xanatoo::ViewSettings::kMarginSteps) {
    char label[16];
    std::snprintf(label, sizeof label, "%g%%", pct);
    if (ImGui::MenuItem(label, nullptr,
                        near_eq(state.view_margin_pct, pct, 0.5f))) {
      cmd = UiCommand::ViewMargins;
      arg = std::to_string(static_cast<int>(pct));
    }
  }
}

void fill_measure_items(const UiState& state, UiCommand& cmd,
                        std::string& arg) {
  for (int chars : xanatoo::ViewSettings::kMeasureSteps) {
    std::string label =
        chars == 0 ? "Full width" : std::to_string(chars) + " characters";
    if (ImGui::MenuItem(label.c_str(), nullptr,
                        state.view_measure_chars == chars)) {
      cmd = UiCommand::ViewMeasure;
      arg = std::to_string(chars);
    }
  }
}

// Document style sheets (styles/*.xss) — radio list + reload.
void fill_style_items(const UiState& state, UiCommand& cmd, std::string& arg) {
  for (const auto& [name, title] : state.styles) {
    if (ImGui::MenuItem(title.c_str(), nullptr, name == state.style)) {
      cmd = UiCommand::SelectStyle;
      arg = name;
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reload style sheets"))
    cmd = UiCommand::ReloadStyles;
}

}  // namespace


Ui::~Ui() { shutdown(); }

// ---------------------------------------------------------------------------
// Classic desktop theme — Clearlooks / Gtk2 greys with BeOS-ish blue selection.
// ---------------------------------------------------------------------------
void Ui::apply_classic_theme() {
  ImGui::StyleColorsLight();
  ImGuiStyle& s = ImGui::GetStyle();

  s.WindowPadding = ImVec2(8.f, 8.f);  // canvas windows: help_pad well inset
  s.FramePadding = ImVec2(6.f, 3.f);
  s.ItemSpacing = ImVec2(6.f, 4.f);
  s.ItemInnerSpacing = ImVec2(4.f, 3.f);
  s.ScrollbarSize = 12.f;  // same as draw_client_scrollbar
  s.GrabMinSize = 10.f;

  // Soft corners like Clearlooks; not fully flat, not fully modern.
  // Windows and scrollbars are square like the canvas windows (theme.cpp).
  s.WindowRounding = 0.f;
  s.ChildRounding = 2.f;
  s.FrameRounding = 3.f;
  s.PopupRounding = 3.f;
  s.ScrollbarRounding = 0.f;
  s.GrabRounding = 2.f;
  s.TabRounding = 2.f;

  s.WindowBorderSize = 1.f;
  s.FrameBorderSize = 1.f;
  s.PopupBorderSize = 1.f;

  auto C = [](int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
  };

  // Colours come from the shared classic palette (theme.hpp) so canvas
  // windows and ImGui chrome stay in step.
  const theme::Palette& pal = theme::palette();
  auto P = [](theme::Rgb c) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f); };
  const ImVec4 bg = P(pal.face);
  const ImVec4 bg2 = P(pal.face_light);
  const ImVec4 bg3 = P(pal.face_dark);
  const ImVec4 border = P(pal.frame);
  const ImVec4 text = P(pal.text);
  const ImVec4 text_dis = P(pal.text_dim);
  const ImVec4 sel = P(pal.accent);
  const ImVec4 sel_hi = C(0x6A, 0xA8, 0xE8);
  const ImVec4 header = P(pal.face_dark);
  const ImVec4 title = P(pal.tab_idle_top);

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = text_dis;
  c[ImGuiCol_WindowBg] = bg2;
  c[ImGuiCol_ChildBg] = bg;
  c[ImGuiCol_PopupBg] = C(0xFA, 0xF9, 0xF7);
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = C(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = C(0xFF, 0xFF, 0xFF);
  c[ImGuiCol_FrameBgHovered] = C(0xF0, 0xF6, 0xFC);
  c[ImGuiCol_FrameBgActive] = C(0xE0, 0xEC, 0xF8);
  c[ImGuiCol_TitleBg] = title;
  c[ImGuiCol_TitleBgActive] = P(pal.tab_top);
  c[ImGuiCol_TitleBgCollapsed] = header;
  c[ImGuiCol_MenuBarBg] = bg2;
  c[ImGuiCol_ScrollbarBg] = P(pal.scroll_trough);  // = theme::draw_vscrollbar
  c[ImGuiCol_ScrollbarGrab] = P(pal.face);
  c[ImGuiCol_ScrollbarGrabHovered] = P(pal.accent_soft);
  c[ImGuiCol_ScrollbarGrabActive] = P(pal.face_dark);
  c[ImGuiCol_CheckMark] = sel;
  c[ImGuiCol_SliderGrab] = sel;
  c[ImGuiCol_SliderGrabActive] = sel_hi;
  c[ImGuiCol_Button] = bg3;
  c[ImGuiCol_ButtonHovered] = C(0xD0, 0xE4, 0xF8);
  c[ImGuiCol_ButtonActive] = C(0xA8, 0xC8, 0xE8);
  c[ImGuiCol_Header] = header;
  c[ImGuiCol_HeaderHovered] = C(0xC0, 0xD8, 0xF0);
  c[ImGuiCol_HeaderActive] = sel;
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = sel;
  c[ImGuiCol_SeparatorActive] = sel_hi;
  c[ImGuiCol_ResizeGrip] = P(pal.shadow);
  c[ImGuiCol_ResizeGripHovered] = sel_hi;
  c[ImGuiCol_ResizeGripActive] = sel;
  c[ImGuiCol_Tab] = bg3;
  c[ImGuiCol_TabHovered] = C(0xD0, 0xE4, 0xF8);
  c[ImGuiCol_TabActive] = C(0xE8, 0xF0, 0xF8);
  c[ImGuiCol_TabUnfocused] = bg3;
  c[ImGuiCol_TabUnfocusedActive] = bg2;
  c[ImGuiCol_TableHeaderBg] = header;
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = C(0xC8, 0xC6, 0xC2);
  c[ImGuiCol_TextSelectedBg] = C(0x4A, 0x90, 0xD9, 90);
  c[ImGuiCol_NavHighlight] = sel;
}

void Ui::load_icons(SDL_Renderer* renderer) {
  if (!renderer) return;
  renderer_ = renderer;

  // Toolbar icons are installed data: <data dir>/icons (paths.hpp).
  std::vector<fs::path> roots;
  if (const std::string data = xanatoo::data_dir(); !data.empty())
    roots.push_back(fs::path(data) / "icons");

  const char* names[] = {
      "save", "undo", "redo", "bold", "italic", "code", "link", "list",
      "olist", "quote", "image", "split", "find", "help", "cut",
      "strike", "cite", "h1", "h2", "h3", "para", "task", "table",
      "figure", "note", "replace", "outline", "paste", "copy", "new", "open",
      "zoomin", "zoomout", "structure", "diff",
      "back", "forward", "up", "home", "reload", "stop", "bookmark", "history", "browse", "native", "reader",
      "linespacing", "measure", "style",
      "format", "insert", "view", "lists",
  };

  // Keep the candidate directories that exist.
  std::vector<fs::path> dirs;
  for (const auto& root : roots) {
    std::error_code ec;
    auto canon = fs::weakly_canonical(root, ec);
    const fs::path& probe = ec ? root : canon;
    if (fs::is_directory(probe)) dirs.push_back(probe);
  }
  if (dirs.empty()) {
    std::cerr << "xanatoo: toolbar icons not found "
                 "(no icons/ in the data directory, see --data-dir)\n";
    return;
  }

  // Per-icon search so an older dir with save.png does not hide newer icons
  // that only exist in the build output.
  for (const char* name : names) {
    fs::path path;
    for (const auto& dir : dirs) {
      fs::path cand = dir / (std::string(name) + ".png");
      if (fs::exists(cand)) {
        path = cand;
        break;
      }
    }
    if (path.empty()) continue;
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
    if (!data) continue;

    SDL_Surface* surf = xanatoo::sdl::rgba_surface_from(
        data, w, h, w * 4);
    if (!surf) {
      stbi_image_free(data);
      continue;
    }
    Icon ic;
    ic.texture = SDL_CreateTextureFromSurface(renderer, surf);
    xanatoo::sdl::destroy_surface(surf);
    stbi_image_free(data);
    if (!ic.texture) continue;
    SDL_SetTextureBlendMode(ic.texture, SDL_BLENDMODE_BLEND);
    ic.w = w;
    ic.h = h;
    icons_[name] = ic;
  }
  std::cerr << "icons: " << icons_.size() << "/" << (sizeof(names) / sizeof(names[0]))
            << " from";
  for (const auto& dir : dirs) std::cerr << " " << dir.string();
  std::cerr << "\n";
}

const Ui::Icon* Ui::icon(const char* name) const {
  auto it = icons_.find(name);
  if (it == icons_.end() || !it->second.texture) return nullptr;
  return &it->second;
}

bool Ui::tool_button(const char* id, const char* icon_name, const char* tip,
                     UiCommand c, UiCommand& out, bool enabled) {
  const Icon* ic = icon(icon_name);
  bool pressed = false;
  ImGui::BeginDisabled(!enabled);
  if (ic) {
    ImGui::PushID(id);
    ImVec2 size(ic->w * scale_, ic->h * scale_);
    // Pad hit target a little for classic toolbar feel
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 3.f));
    if (ImGui::ImageButton(
            id, reinterpret_cast<ImTextureID>(ic->texture), size,
            ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
            ImVec4(1, 1, 1, enabled ? 1.f : 0.4f))) {
      pressed = true;
      out = c;
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
  } else {
    // Text fallback when PNG missing (e.g. icons not rasterized yet).
    static const std::unordered_map<std::string, const char*> kShort = {
        {"back", "◀"}, {"forward", "▶"}, {"reload", "↻"}, {"stop", "✕"}, {"bookmark", "★"},
        {"history", "Hist"}, {"browse", "Browse"}, {"native", "Native"},
        {"reader", "Reader"},
        {"linespacing", "≡↕"}, {"measure", "▣|"},
        {"format", "Aa▾"},
        {"insert", "+▾"}, {"view", "View▾"}, {"lists", "List▾"},
        {"new", "New"}, {"open", "Open"}, {"save", "Save"}, {"undo", "Undo"},
        {"redo", "Redo"}, {"cut", "Cut"}, {"copy", "Copy"}, {"paste", "Paste"},
        {"bold", "B"}, {"italic", "I"}, {"link", "Link"}, {"find", "Find"},
        {"code", "Code"}, {"list", "List"}, {"structure", "Struct"},
    };
    std::string label;
    auto it = kShort.find(icon_name ? icon_name : "");
    if (it != kShort.end())
      label = it->second;
    else if (tip && tip[0]) {
      label = tip;
      size_t cut = label.find(" (");
      if (cut != std::string::npos) label.resize(cut);
      if (label.size() > 8) label.resize(8);
    } else {
      label = id;
    }
    label += "##";
    label += id;
    if (ImGui::SmallButton(label.c_str())) {
      pressed = true;
      out = c;
    }
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tip && tip[0]) {
    ImGui::SetTooltip("%s", tip);
  }
  ImGui::SameLine(0.f, 2.f);
  return pressed;
}



// Icon (or text) button that opens a popup menu. When true, fill items then tool_menu_end().
bool Ui::tool_menu_begin(const char* id, const char* icon_name, const char* tip) {
  const Icon* ic = icon(icon_name);
  ImGui::PushID(id);
  bool pressed = false;
  if (ic) {
    ImVec2 size(ic->w * scale_, ic->h * scale_);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 3.f));
    if (ImGui::ImageButton(
            "##btn", reinterpret_cast<ImTextureID>(ic->texture), size,
            ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
            ImVec4(1, 1, 1, 1.f)))
      pressed = true;
    ImGui::PopStyleVar();
  } else {
    static const std::unordered_map<std::string, const char*> kShort = {
        {"format", "Aa"}, {"insert", "+"}, {"view", "View"}, {"lists", "List"},
        {"structure", "Struct"}, {"list", "List"}, {"code", "Aa"},
        {"table", "+"}, {"h1", "H"}, {"outline", "Outline"},
        {"linespacing", "Sp"}, {"measure", "Me"}, {"style", "Style"},
    };
    std::string label;
    auto it = kShort.find(icon_name ? icon_name : "");
    if (it != kShort.end())
      label = it->second;
    else if (tip && tip[0]) {
      label = tip;
      size_t cut = label.find(" (");
      if (cut != std::string::npos) label.resize(cut);
      if (label.size() > 10) label.resize(10);
    } else {
      label = id;
    }
    label += " ▾";
    if (ImGui::SmallButton(label.c_str())) pressed = true;
  }
  if (pressed) ImGui::OpenPopup("##menu");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tip && tip[0])
    ImGui::SetTooltip("%s", tip);
  // Advance toolbar cursor before opening the popup window.
  ImGui::SameLine(0.f, 2.f);
  bool open = ImGui::BeginPopup("##menu");
  if (!open) {
    ImGui::PopID();
    return false;
  }
  // Popup is open; keep ID pushed until tool_menu_end().
  return true;
}

void Ui::tool_menu_end() {
  ImGui::EndPopup();
  ImGui::PopID();
}

static void load_ui_fonts(ImGuiIO& io, float scale) {
  // ~10–11 pt at 96 dpi — close to GNOME 2 menu/toolbar size
  const float size_px = std::round(14.0f * scale);
  std::string path = ui_font_path();
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH = true;  // crisper menus on integer positions
  if (!path.empty()) {
    // Latin-1 plus the punctuation, arrows and shapes menu labels use
    // ("…", "—", "→", "▸"); the default range alone draws them as "?".
    static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E,
                                      0x2190, 0x21FF, 0x25A0, 0x25FF, 0};
    ImFont* font =
        io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg, kRanges);
    if (font) {
      std::cerr << "menu font: " << path << " @ " << size_px << " px\n";
      return;
    }
    std::cerr << "UI font load failed: " << path << " — using ImGui default\n";
  } else {
    std::cerr << "No DejaVu/Vera/Liberation Sans found — using ImGui default\n";
  }
  io.Fonts->AddFontDefault();
}

bool Ui::init(SDL_Window* window, SDL_Renderer* renderer, float ui_scale) {
  if (ready_) return true;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Gamepad navigates ImGui menus when a controller is present.
#if !defined(XANATOO_SDL3) || !XANATOO_SDL3
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#endif
  io.IniFilename = nullptr;
  scale_ = ui_scale > 0.f ? ui_scale : 1.f;
  window_ = window;

  load_ui_fonts(io, scale_);
  apply_classic_theme();
  // Slightly roomier chrome once a proportional face is loaded
  {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding = ImVec2(7.f, 4.f);
    s.ItemSpacing = ImVec2(7.f, 5.f);
    s.ScrollbarSize = 14.f;
    s.ScaleAllSizes(scale_);
  }
  load_icons(renderer);

#if defined(XANATOO_SDL3) && XANATOO_SDL3
  if (!xanatoo::imgui_sdl3::init(window, renderer)) return false;
#else
  if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) return false;
  if (!ImGui_ImplSDLRenderer2_Init(renderer)) return false;
  if (ImGui_ImplSDLRenderer2_UsesTriangleFallback()) {
    // Old SDL (R36S): triangles are drawn as their bounding rectangles, so
    // keep every shape an axis-aligned quad (no fringes, square corners).
    ImGuiStyle& s = ImGui::GetStyle();
    s.AntiAliasedLines = s.AntiAliasedLinesUseTex = s.AntiAliasedFill = false;
    s.WindowRounding = s.ChildRounding = s.FrameRounding = 0.f;
    s.PopupRounding = s.ScrollbarRounding = s.GrabRounding = 0.f;
    s.TabRounding = 0.f;
  }
#endif
  ready_ = true;
  return true;
}

void Ui::shutdown() {
  if (!ready_) return;
  for (auto& [k, ic] : icons_) {
    if (ic.texture) SDL_DestroyTexture(ic.texture);
  }
  icons_.clear();
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::shutdown();
#else
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
#endif
  ImGui::DestroyContext();
  ready_ = false;
}

void Ui::process_event(const SDL_Event& ev) {
  if (!ready_) return;
  // Ctrl+Tab / Ctrl+Shift+Tab belong to the document tab strip; keep ImGui's
  // built-in window switcher overlay from popping up over the page.
  const auto kind = xanatoo::sdl::event_kind(ev);
  if ((kind == xanatoo::sdl::EventKind::KeyDown ||
       kind == xanatoo::sdl::EventKind::KeyUp) &&
      xanatoo::sdl::keycode(ev) == SDLK_TAB &&
      (xanatoo::sdl::keymod(ev) & KMOD_CTRL)) {
    return;
  }
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::process_event(ev);
#else
  ImGui_ImplSDL2_ProcessEvent(&ev);
#endif
}

bool Ui::want_capture_keyboard() const {
  if (!ready_) return false;
  // Claim keys only when ImGui has a real reason: text input, popup/menu, or
  // an active widget. Full WantCaptureKeyboard stays true while the menu bar /
  // toolbar holds focus and would block document shortcuts and status prompts.
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return true;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return true;
  if (ImGui::IsAnyItemActive()) return true;
  return false;
}

bool Ui::want_capture_mouse() const {
  if (!ready_) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

bool Ui::captures_point(float x, float y) const {
  if (!ready_) return false;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                 ImGuiPopupFlags_AnyPopupLevel))
    return true;
  ImGuiWindow* hovered = nullptr;
  ImGui::FindHoveredWindowEx(ImVec2(x, y), true, &hovered, nullptr);
  return hovered != nullptr;
}

void Ui::release_input_capture() {
  if (!ready_) return;
  // Drop the active widget (menu item / toolbar button) and clear keyboard
  // nav focus so the next keystroke is not eaten by ImGui chrome.
  // Field names match Dear ImGui 1.91+ (HoveredId, NavCursorVisible).
  if (GImGui->ActiveId != 0) ImGui::ClearActiveID();
  if (GImGui->HoveredId != 0) GImGui->HoveredId = 0;
  ImGui::FocusWindow(nullptr);
  GImGui->NavId = 0;
  GImGui->NavCursorVisible = false;
}

void Ui::request_menu(const char* name) {
  if (name && name[0]) menu_request_ = name;
}

void Ui::focus_url_bar() { focus_url_bar_ = true; }
void Ui::focus_search() { focus_search_ = true; }

void Ui::open_context_menu(float x, float y) {
  context_open_ = true;
  context_x_ = x;
  context_y_ = y;
}

void Ui::open_bookmark_menu(float x, float y) {
  bookmark_menu_open_ = true;
  context_x_ = x;
  context_y_ = y;
}

static bool menu_item_cmd(const char* label, UiCommand c, UiCommand& out,
                          bool enabled = true, const char* shortcut = nullptr) {
  if (ImGui::MenuItem(label, shortcut, false, enabled)) {
    out = c;
    return true;
  }
  return false;
}

// Shared insert/format items used by Format menu and context menu.
// Help → Examples: local pages under examples/ (OpenExample, arg = file).
static void menu_example_items(UiCommand& cmd, std::string& arg) {
  auto ex = [&](const char* label, const char* file) {
    if (ImGui::MenuItem(label)) {
      cmd = UiCommand::OpenExample;
      arg = file;
    }
  };
  for (const xanatoo::ExampleEntry& e : xanatoo::kExamples) {
    if (e.label) ex(e.label, e.file);
    else ImGui::Separator();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Diff demo (v1 | v2, marks on)"))
    cmd = UiCommand::LoadDiffDemo;
}

static void menu_insert_items(UiCommand& cmd) {
  menu_item_cmd("Paragraph", UiCommand::InsertParagraph, cmd);
  menu_item_cmd("Horizontal Rule", UiCommand::InsertHr, cmd);
  menu_item_cmd("Image…", UiCommand::InsertImage, cmd, true, "Ctrl+Shift+I");
  menu_item_cmd("Figure…", UiCommand::InsertFigure, cmd);
  menu_item_cmd("Note", UiCommand::InsertNote, cmd);
  menu_item_cmd("Code Block", UiCommand::InsertCodeBlock, cmd);
  menu_item_cmd("Table", UiCommand::InsertTable, cmd);
  menu_item_cmd("Definition List", UiCommand::InsertDefList, cmd);
  menu_item_cmd("Bibliography Entry", UiCommand::InsertBiblioEntry, cmd);
  menu_item_cmd("Columns (2)", UiCommand::InsertColumns, cmd);
  menu_item_cmd("Include / Transclude…", UiCommand::InsertTransclude, cmd);
}

// Collection view: layout, card size, sort, fullscreen viewer.
static void menu_collection_items(const UiState& state, UiCommand& cmd,
                                  std::string& arg) {
  auto item = [&](const char* label, const char* a, bool checked,
                  const char* shortcut = nullptr) {
    if (ImGui::MenuItem(label, shortcut, checked, state.collection)) {
      cmd = UiCommand::CollectionView;
      arg = a;
    }
  };
  item("View Fullscreen", "view", false);
  ImGui::Separator();
  item("Grid", "grid", state.collection_layout == 0);
  item("List", "list", state.collection_layout == 1);
  item("Full Size", "full", state.collection_layout == 2);
  ImGui::Separator();
  item("Small Cards", "small", state.collection_thumb < 140.f);
  item("Medium Cards", "medium",
       state.collection_thumb >= 140.f && state.collection_thumb < 200.f);
  item("Large Cards", "large", state.collection_thumb >= 200.f);
  ImGui::Separator();
  item("Document Order", "sort-doc", state.collection_sort == 0);
  item("Sort by Name", "sort-name", state.collection_sort == 1);
  item("Sort by Name (Reverse)", "sort-name-desc", state.collection_sort == 2);
}

static void menu_structure_items(const UiState& state, UiCommand& cmd) {
  menu_item_cmd("Unordered List", UiCommand::ToggleUnorderedList, cmd, true,
                "Ctrl+Shift+8");
  menu_item_cmd("Ordered List", UiCommand::ToggleOrderedList, cmd, true,
                "Ctrl+Shift+7");
  menu_item_cmd("Task Item", UiCommand::ToggleTask, cmd, true, "Ctrl+Shift+T");
  menu_item_cmd("Block Quote", UiCommand::ToggleBlockQuote, cmd);
  menu_item_cmd("Indent List Item", UiCommand::IndentList, cmd,
                state.on_list_item, "Tab");
  menu_item_cmd("Outdent List Item", UiCommand::OutdentList, cmd,
                state.on_list_item, "Shift+Tab");
  ImGui::Separator();
  menu_item_cmd("Wrap in Stretch", UiCommand::WrapStretch, cmd, true, "Ctrl+E");
  menu_item_cmd("Edit Stretch Summary…", UiCommand::EditStretchSummary, cmd,
                state.in_stretch, "Ctrl+Shift+E");
  menu_item_cmd("Unwrap Stretch", UiCommand::UnwrapStretch, cmd, state.in_stretch,
                "Ctrl+Shift+U");
  ImGui::Separator();
  menu_item_cmd("Header", UiCommand::MakeHeader, cmd, true, "Ctrl+1");
  menu_item_cmd("Wrap in Section", UiCommand::WrapSection, cmd, true,
                "Ctrl+Shift+1");
  menu_item_cmd("Wrap in Article", UiCommand::WrapArticle, cmd, true,
                "Ctrl+Shift+2");
  menu_item_cmd("Unwrap Section/Article", UiCommand::UnwrapSectioning, cmd);
  menu_item_cmd("Promote Section", UiCommand::PromoteSection, cmd, true,
                "Ctrl+Alt+]");
  menu_item_cmd("Demote Section", UiCommand::DemoteSection, cmd, true,
                "Ctrl+Alt+[");
  menu_item_cmd("Move Section Up", UiCommand::MoveSectionUp, cmd, true,
                "Alt+Shift+Up");
  menu_item_cmd("Move Section Down", UiCommand::MoveSectionDown, cmd, true,
                "Alt+Shift+Down");
  ImGui::Separator();
  menu_item_cmd("Header (depth 2)", UiCommand::Heading2, cmd, true, "Ctrl+2");
  menu_item_cmd("Header (depth 3)", UiCommand::Heading3, cmd, true, "Ctrl+3");
  menu_item_cmd("Paragraph (demote)", UiCommand::ParagraphLevel, cmd, true,
                "Ctrl+0");
}

// Toolbar "View tools" menu. While browsing it also holds the display
// menus (line spacing, text width, style sheet) of the browse row, which
// is then the only toolbar row.
static void view_tools_items(const UiState& state, UiCommand& cmd,
                             std::string& arg) {
  if (state.browse) {
    if (ImGui::BeginMenu("Line spacing")) {
      fill_line_spacing_items(state, cmd);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Text width")) {
      fill_measure_items(state, cmd, arg);
      ImGui::Separator();
      ImGui::TextDisabled("Margins");
      fill_margins_items(state, cmd, arg);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Style sheet")) {
      fill_style_items(state, cmd, arg);
      ImGui::EndMenu();
    }
  } else {
    menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Backlinks", "Ctrl+Alt+B", state.backlinks))
    cmd = UiCommand::ToggleBacklinks;
  if (ImGui::MenuItem("Site map", "Ctrl+Alt+M", state.sitemap))
    cmd = UiCommand::ToggleSitemap;
  if (ImGui::MenuItem("Page navigation", "Ctrl+Alt+P", state.page_panel))
    cmd = UiCommand::TogglePage;
  menu_item_cmd("Open include → other pane", UiCommand::OpenTranscludeOther,
                cmd);
  if (ImGui::MenuItem("Structure highlight", "Ctrl+Shift+L", state.structure))
    cmd = UiCommand::ToggleStructure;
  if (ImGui::MenuItem("Diff marks", "Ctrl+Shift+D", state.diff, state.can_diff))
    cmd = UiCommand::ToggleDiff;
  ImGui::Separator();
  menu_item_cmd("Zoom in", UiCommand::ZoomIn, cmd, true, "Ctrl++");
  menu_item_cmd("Zoom out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
  ImGui::Separator();
  menu_item_cmd("Content only", UiCommand::ToggleChrome, cmd, true, "F11");
  menu_item_cmd("Help", UiCommand::ToggleHelp, cmd, true, "F1");
  if (ImGui::MenuItem("Virtual keyboard", "F3", state.virtual_keyboard))
    cmd = UiCommand::ToggleVirtualKeyboard;
}

// Browse (navigation) row, edit row (not while browsing) and tab strip.
void Ui::toolbars(const UiState& state, UiFrameResult& result, UiCommand& cmd) {
  // Frequently used panel toggles: highlighted while on.
  auto toggle_button = [&](const char* id, const char* icon, const char* tip,
                           UiCommand c, bool on) {
    if (on) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.4f, 0.5f, 0.75f, 1.f));
    }
    tool_button(id, icon, tip, c, cmd);
    if (on) ImGui::PopStyleColor(2);
  };

  // ---- Two toolbars: browse (navigation) then edit ----
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiWindowFlags tb_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollWithMouse;

  // -- Browse / navigation row (browser chrome) --
  const float browse_h = 34.f * scale_;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, browse_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
  if (ImGui::Begin("##toolbar_browse", nullptr, tb_flags)) {
    tool_button("tb_back", "back", "Back (Alt+Left)", UiCommand::NavBack, cmd,
                state.can_nav_back);
    tool_button("tb_fwd", "forward", "Forward (Alt+Right)", UiCommand::NavForward,
                cmd, state.can_nav_forward);
    {
      const std::string tip = state.up_url.empty()
                                  ? std::string("Up (at the top)")
                                  : "Up (Alt+Up when read-only): " + state.up_url;
      tool_button("tb_up", "up", tip.c_str(), UiCommand::GoUp, cmd,
                  !state.up_url.empty());
    }
    tool_button("tb_home", "home", "Home (Alt+Home)", UiCommand::GoHome, cmd);
    if (state.loading)
      tool_button("tb_stop", "stop", "Stop loading (Esc)",
                  UiCommand::StopLoading, cmd, true);
    else
      tool_button("tb_reload", "reload", "Reload (Ctrl+R)", UiCommand::Reload,
                  cmd, state.has_path);
    {
      // Pressed while showing the native form; a click flips to the other.
      const std::string tip =
          state.alternate_url.empty()
              ? "Native view (only for pages xanatoo has a native form of: "
                "Wikipedia, Hacker News, Reddit, Lemmy)"
          : state.native_view ? "Back to the web page: " + state.alternate_url
                              : "Native view: " + state.alternate_url;
      if (state.native_view) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.4f, 0.5f, 0.75f, 1.f));
      }
      tool_button("tb_native", "native", tip.c_str(), UiCommand::ToggleNative,
                  cmd, !state.alternate_url.empty());
      if (state.native_view) ImGui::PopStyleColor(2);
    }
    {
      // Pressed in reader mode; enabled while there is chrome to hide.
      const char* tip = !state.has_chrome && !state.reader
                            ? "Reader mode (F9; only for web pages with "
                              "navigation, sidebars or footers)"
                        : state.reader ? "Reader mode on: show the whole page (F9)"
                                       : "Reader mode: hide navigation, sidebars "
                                         "and footers (F9)";
      if (state.reader) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.4f, 0.5f, 0.75f, 1.f));
      }
      tool_button("tb_reader", "reader", tip, UiCommand::ToggleReader, cmd,
                  state.has_chrome || state.reader);
      if (state.reader) ImGui::PopStyleColor(2);
    }

    ImGui::TextDisabled("|");
    ImGui::SameLine(0.f, 4.f);

    // Location / URL field (path or gemini://)
    {
      std::string want = state.doc_label.empty() ? "untitled" : state.doc_label;
      if (state.dirty) {
        ImGui::TextDisabled("*");
        ImGui::SameLine(0.f, 2.f);
      }
      // Leave room for the buttons right of the field (measured last frame).
      float field_w =
          ImGui::GetContentRegionAvail().x - url_right_w_ - 6.f;
      if (field_w < 100.f) field_w = 100.f;
      ImGui::SetNextItemWidth(field_w);

      if (focus_url_bar_) {
        ImGui::SetKeyboardFocusHere();
        std::snprintf(url_input_, sizeof(url_input_), "%s", want.c_str());
        url_synced_from_ = want;
        focus_url_bar_ = false;
      } else if (want != url_synced_from_ && !url_bar_editing_) {
        std::snprintf(url_input_, sizeof(url_input_), "%s", want.c_str());
        url_synced_from_ = want;
      }

      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.95f, 0.95f, 0.97f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                            ImVec4(0.98f, 0.98f, 1.f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.f, 1.f, 1.f, 1.f));
      bool enter = ImGui::InputText(
          "##urlbar", url_input_, sizeof(url_input_),
          ImGuiInputTextFlags_EnterReturnsTrue |
              ImGuiInputTextFlags_AutoSelectAll);
      ImGui::PopStyleColor(3);
      url_bar_editing_ = ImGui::IsItemActive();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(
            "Location — path or gemini:// URL · Enter to open · Ctrl+L focuses");
      if (enter) {
        cmd = UiCommand::NavigateUrl;
        result.arg = url_input_;
        url_synced_from_ = result.arg;
        url_bar_editing_ = false;
        ImGui::ClearActiveID();
      }
      ImGui::SameLine(0.f, 6.f);
    }
    const float right_x0 = ImGui::GetCursorPosX();

    {
      std::string bm_tip =
          state.current_bookmarked ? "Remove bookmark (Ctrl+Shift+B)"
          : state.selection        ? "Bookmark the selection (Ctrl+Shift+B)"
                                   : "Bookmark this page (Ctrl+Shift+B)";
      if (state.page_bookmarks > 0)
        bm_tip += state.page_bookmarks == 1
                      ? " · 1 bookmark in this page"
                      : " · " + std::to_string(state.page_bookmarks) +
                            " bookmarks in this page";
      // Lit while this address or a passage of the page is bookmarked.
      const bool lit = state.current_bookmarked || state.page_bookmarks > 0;
      if (lit) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.4f, 0.5f, 0.75f, 1.f));
      }
      tool_button("tb_bookmark", "bookmark", bm_tip.c_str(),
                  state.current_bookmarked ? UiCommand::RemoveBookmark
                                           : UiCommand::AddBookmark,
                  cmd, state.has_path || !state.doc_label.empty());
      if (lit) ImGui::PopStyleColor(2);
    }
    if (state.browse) {
      // The edit row is hidden: its reading tools move here, the display
      // menus into View tools so the row does not grow.
      tool_button("tb_find", "find", "Find (Ctrl+F)", UiCommand::Find, cmd);
      toggle_button("tb_split", "split", "Split pane (Ctrl+\\)",
                    UiCommand::ToggleSplit, state.split);
      toggle_button("tb_outline", "outline", "Heading outline (Ctrl+Shift+O)",
                    UiCommand::ToggleOutline, state.outline);
      if (tool_menu_begin("tb_viewmenu", "view", "View tools")) {
        view_tools_items(state, cmd, result.arg);
        tool_menu_end();
      }
    } else {
      // Display: line spacing + measure (view-only, not document format)
      if (tool_menu_begin("tb_linespacing", "linespacing",
                          "Line spacing (view)")) {
        fill_line_spacing_items(state, cmd);
        tool_menu_end();
      }
      if (tool_menu_begin("tb_measure", "measure",
                          "Text width / margins (view)")) {
        fill_measure_items(state, cmd, result.arg);
        ImGui::Separator();
        ImGui::TextDisabled("Margins");
        fill_margins_items(state, cmd, result.arg);
        tool_menu_end();
      }
      if (tool_menu_begin("tb_style", "style", "Style sheet (view)")) {
        fill_style_items(state, cmd, result.arg);
        tool_menu_end();
      }
    }
    {
      if (state.history_open) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.4f, 0.5f, 0.75f, 1.f));
      }
      tool_button("tb_history", "history", "Session history (Ctrl+Shift+H)",
                  UiCommand::ToggleHistory, cmd);
      if (state.history_open) ImGui::PopStyleColor(2);
    }
    {
      if (state.browse) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.55f, 0.4f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.35f, 0.6f, 0.45f, 1.f));
      }
      tool_button("tb_browse", "browse",
                  state.browse ? "Browse mode on — F2 to edit"
                               : "Browse mode (read-only, F2)",
                  UiCommand::ToggleBrowse, cmd);
      if (state.browse) ImGui::PopStyleColor(2);
    }
    // tool_button ends with SameLine, so the cursor sits after the group.
    url_right_w_ = ImGui::GetCursorPosX() - right_x0;
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  result.top_reserved += browse_h;

  // -- Search bar (hidden by default): engine button + field, like the old
  // Firefox search box. The engine only changes which search: address a
  // search builds (docs/SEARCH.md).
  if (state.searchbar) {
    const float search_h = 32.f * scale_;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, search_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    if (ImGui::Begin("##toolbar_search", nullptr, tb_flags)) {
      const UiSearchEngine* cur = nullptr;
      for (const UiSearchEngine& e : state.search_engines)
        if (e.id == state.search_engine) cur = &e;
      if (!cur && !state.search_engines.empty()) cur = &state.search_engines[0];

      const std::string badge = (cur ? cur->badge : std::string("?")) + "##eng";
      if (ImGui::Button(badge.c_str(), ImVec2(40.f * scale_, 0.f)))
        ImGui::OpenPopup("##search_engines");
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Search engine: %s · click to change",
                          cur ? cur->name.c_str() : "none");
      if (ImGui::BeginPopup("##search_engines")) {
        for (const UiSearchEngine& e : state.search_engines) {
          std::string label = e.badge + "   " + e.name;
          if (!e.keyword.empty()) label += "   (" + e.keyword + ")";
          if (ImGui::Selectable(label.c_str(), cur && e.id == cur->id)) {
            cmd = UiCommand::SelectSearchEngine;
            result.arg = e.id;
          }
        }
        ImGui::TextDisabled("(keyword): Ctrl+L, e.g. \"w xanadu\"");
        ImGui::EndPopup();
      }
      ImGui::SameLine(0.f, 4.f);

      const float go_w = 44.f * scale_;
      ImGui::SetNextItemWidth(std::max(
          100.f, ImGui::GetContentRegionAvail().x - go_w - 8.f * scale_));
      if (focus_search_) {
        ImGui::SetKeyboardFocusHere();
        focus_search_ = false;
      }
      const std::string hint =
          std::string("Search ") + (cur ? cur->name : "") + " …";
      const bool enter = ImGui::InputTextWithHint(
          "##searchfield", hint.c_str(), search_input_, sizeof(search_input_),
          ImGuiInputTextFlags_EnterReturnsTrue);
      if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        cmd = UiCommand::ToggleSearchBar;
      ImGui::SameLine(0.f, 4.f);
      const bool go = ImGui::Button("Go", ImVec2(go_w, 0.f));
      if (enter || go) {
        cmd = UiCommand::Search;
        result.arg = search_input_;
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    result.top_reserved += search_h;
  }

  // -- Edit row (compact: primary icons + dropdown groups) --
  // Not while browsing: its reading tools are on the browse row then.
  if (!state.browse) {
    const float edit_h = 36.f * scale_;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, edit_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    if (ImGui::Begin("##toolbar_edit", nullptr, tb_flags)) {
      ImGui::BeginChild("##toolbar_edit_scroll", ImVec2(0.f, 0.f), false,
                        ImGuiWindowFlags_HorizontalScrollbar);

      // File
      tool_button("tb_new", "new", "New document (Ctrl+N)", UiCommand::NewDocument,
                  cmd);
      tool_button("tb_open", "open", "Open… (Ctrl+O)", UiCommand::OpenDocument, cmd);
      tool_button("tb_save", "save", "Save (Ctrl+S)", UiCommand::Save, cmd);
      tool_button("tb_undo", "undo", "Undo (Ctrl+Z)", UiCommand::Undo, cmd);
      tool_button("tb_redo", "redo", "Redo (Ctrl+Y)", UiCommand::Redo, cmd);

      ImGui::TextDisabled("|");
      ImGui::SameLine(0.f, 4.f);

      // Clipboard
      tool_button("tb_cut", "cut", "Cut (Ctrl+X)", UiCommand::Cut, cmd,
                  state.selection);
      tool_button("tb_copy", "copy", "Copy (Ctrl+C)", UiCommand::Copy, cmd,
                  state.selection);
      tool_button("tb_paste", "paste", "Paste (Ctrl+V)", UiCommand::Paste, cmd);

      ImGui::TextDisabled("|");
      ImGui::SameLine(0.f, 4.f);

      // Common inline styles
      tool_button("tb_bold", "bold", "Bold (Ctrl+B)", UiCommand::Bold, cmd,
                  state.selection);
      tool_button("tb_italic", "italic", "Italic (Ctrl+I)", UiCommand::Italic,
                  cmd, state.selection);
      tool_button("tb_link", "link", "Link (Ctrl+K)", UiCommand::Link, cmd,
                  state.selection);

      // More format styles
      if (tool_menu_begin("tb_format", "format", "More formatting")) {
        menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
        menu_item_cmd("Strikethrough", UiCommand::Strike, cmd, state.selection,
                      "Ctrl+Shift+X");
        menu_item_cmd("Citation…", UiCommand::Cite, cmd, state.selection,
                      "Ctrl+Shift+C");
        tool_menu_end();
      }

      ImGui::TextDisabled("|");
      ImGui::SameLine(0.f, 4.f);

      // Structure group
      if (tool_menu_begin("tb_structmenu", "structure", "Structure")) {
        menu_item_cmd("Header", UiCommand::MakeHeader, cmd, true, "Ctrl+1");
        menu_item_cmd("Wrap Section", UiCommand::WrapSection, cmd, true,
                      "Ctrl+Shift+1");
        menu_item_cmd("Wrap Article", UiCommand::WrapArticle, cmd, true,
                      "Ctrl+Shift+2");
        menu_item_cmd("Promote Section", UiCommand::PromoteSection, cmd, true,
                      "Ctrl+Alt+]");
        menu_item_cmd("Demote Section", UiCommand::DemoteSection, cmd, true,
                      "Ctrl+Alt+[");
        menu_item_cmd("Paragraph", UiCommand::ParagraphLevel, cmd, true, "Ctrl+0");
        ImGui::Separator();
        menu_item_cmd("Wrap Stretch…", UiCommand::WrapStretch, cmd, true, "Ctrl+E");
        menu_item_cmd("Unwrap Stretch", UiCommand::UnwrapStretch, cmd, state.in_stretch,
                      "Ctrl+Shift+U");
        tool_menu_end();
      }

      // Lists / blocks group
      if (tool_menu_begin("tb_lists", "lists", "Lists & blocks")) {
        menu_item_cmd("Unordered list", UiCommand::ToggleUnorderedList, cmd);
        menu_item_cmd("Ordered list", UiCommand::ToggleOrderedList, cmd);
        menu_item_cmd("Task item", UiCommand::ToggleTask, cmd, true, "Ctrl+Shift+T");
        menu_item_cmd("Block quote", UiCommand::ToggleBlockQuote, cmd);
        ImGui::Separator();
        menu_item_cmd("Indent", UiCommand::IndentList, cmd, state.on_list_item);
        menu_item_cmd("Outdent", UiCommand::OutdentList, cmd, state.on_list_item);
        tool_menu_end();
      }

      // Insert group
      if (tool_menu_begin("tb_insert", "insert", "Insert")) {
        menu_item_cmd("Table", UiCommand::InsertTable, cmd);
        menu_item_cmd("Image…", UiCommand::InsertImage, cmd, true, "Ctrl+Shift+I");
        menu_item_cmd("Figure…", UiCommand::InsertFigure, cmd);
        menu_item_cmd("Note", UiCommand::InsertNote, cmd);
        menu_item_cmd("Code block", UiCommand::InsertCodeBlock, cmd);
        menu_item_cmd("Horizontal rule", UiCommand::InsertHr, cmd);
        menu_item_cmd("Columns (2)", UiCommand::InsertColumns, cmd);
        menu_item_cmd("Transclude…", UiCommand::InsertTransclude, cmd);
        menu_item_cmd("Bibliography entry", UiCommand::InsertBiblioEntry, cmd);
        tool_menu_end();
      }

      ImGui::TextDisabled("|");
      ImGui::SameLine(0.f, 4.f);

      tool_button("tb_find", "find", "Find (Ctrl+F)", UiCommand::Find, cmd);
      toggle_button("tb_split", "split", "Split pane (Ctrl+\\)",
                    UiCommand::ToggleSplit, state.split);
      toggle_button("tb_outline", "outline",
                    "Heading outline (Ctrl+Shift+O; Ctrl+Alt+O docks it)",
                    UiCommand::ToggleOutline, state.outline);
      if (tool_menu_begin("tb_viewmenu", "view", "View tools")) {
        view_tools_items(state, cmd, result.arg);
        tool_menu_end();
      }

      ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    result.top_reserved += edit_h;
  }

  // ---- Document tab strip (browser-style) ----
  if (!state.tab_labels.empty()) {
    const float tab_h = 28.f * scale_;
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, tab_h));
    ImGuiWindowFlags tab_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 2.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f, 0.f));
    if (ImGui::Begin("##tabs", nullptr, tab_flags)) {
      // Dragging reorders; the new order goes back as ReorderTabs. Ids are
      // the buffer ids, so a tab keeps its place when the order changes.
      std::vector<std::pair<ImGuiID, uint32_t>> tab_item_ids;
      const float strip_top = vp->Pos.y + result.top_reserved;
      if (ImGui::BeginTabBar("##doctabs",
                             ImGuiTabBarFlags_FittingPolicyScroll |
                                 ImGuiTabBarFlags_Reorderable)) {
        const bool force_sel = tab_force_select_;
        tab_force_select_ = false;
        for (int i = 0; i < static_cast<int>(state.tab_labels.size()); ++i) {
          // One-shot SetSelected when main changed active_tab (Next/Prev tab).
          // Continuous SetSelected fights ImGui clicks → flicker / stacked docs.
          ImGuiTabItemFlags flags = 0;
          if (force_sel && i == state.active_tab)
            flags |= ImGuiTabItemFlags_SetSelected;
          bool open = true;
          std::string id = state.tab_labels[static_cast<size_t>(i)];
          const uint32_t bid = i < static_cast<int>(state.tab_ids.size())
                                   ? state.tab_ids[static_cast<size_t>(i)]
                                   : static_cast<uint32_t>(i);
          id += "###tab";
          id += std::to_string(bid);
          if (ImGui::BeginTabItem(id.c_str(), &open, flags)) {
            ImGui::EndTabItem();
          }
          tab_item_ids.emplace_back(ImGui::GetItemID(), bid);
          // Dragged well above / below the strip: a tear-off on release.
          if (ImGui::IsItemActive() &&
              ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.f)) {
            const float my = ImGui::GetMousePos().y;
            if (my > strip_top + tab_h * 1.5f || my < strip_top - tab_h * 0.5f) {
              tab_tear_id_ = bid;
              result.tab_drag_out = true;
            } else if (tab_tear_id_ == bid) {
              tab_tear_id_ = 0;
            }
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
              i != state.active_tab && cmd == UiCommand::None) {
            cmd = UiCommand::SelectTab;
            result.arg = std::to_string(i);
          }
          if (!open && cmd == UiCommand::None) {
            cmd = UiCommand::CloseTab;
            result.arg = std::to_string(i);
          }
        }
        if (ImGuiTabBar* tb = ImGui::GetCurrentTabBar();
            tb && cmd == UiCommand::None && !result.tab_drag_out &&
            tab_tear_id_ == 0 &&
            static_cast<int>(tab_item_ids.size()) == tb->Tabs.Size) {
          std::string order;
          bool known = true, changed = false;
          for (int k = 0; k < tb->Tabs.Size; ++k) {
            uint32_t bid = 0;
            known = false;
            for (const auto& [iid, b] : tab_item_ids)
              if (iid == tb->Tabs[k].ID) {
                bid = b;
                known = true;
              }
            if (!known) break;
            if (k < static_cast<int>(state.tab_ids.size()) &&
                state.tab_ids[static_cast<size_t>(k)] != bid)
              changed = true;
            order += (k ? "," : "") + std::to_string(bid);
          }
          if (known && changed) {
            cmd = UiCommand::ReorderTabs;
            result.arg = order;
          }
        }
        ImGui::EndTabBar();
        // After main applies SelectTab/NextTab, active_tab changes → sync ImGui.
        if (state.active_tab != tab_synced_active_) {
          tab_synced_active_ = state.active_tab;
          tab_force_select_ = true;
        }
      }
      if (tab_tear_id_ != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 mp = ImGui::GetMousePos();
        cmd = UiCommand::TearTab;
        result.arg = std::to_string(tab_tear_id_) + "," +
                     std::to_string(static_cast<int>(mp.x)) + "," +
                     std::to_string(static_cast<int>(mp.y));
        tab_tear_id_ = 0;
      } else if (tab_tear_id_ != 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        tab_tear_id_ = 0;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+")) cmd = UiCommand::NewTab;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("New tab (Ctrl+T)");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    result.top_reserved += tab_h;
  }
}

UiFrameResult Ui::begin_frame(const UiState& state) {
  UiFrameResult result;
  if (!ready_) return result;

#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::new_frame();
#else
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
#endif
  ImGui::NewFrame();
  // Stick is an *extra* pointer. Only drive ImGui from it while the stick is
  // actively used (axis motion or A/B held); otherwise leave the real mouse.
  if (state.stick_cursor && state.stick_active) {
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(state.stick_cx, state.stick_cy);
    io.MouseDown[0] = state.stick_lmb;
    io.MouseDown[1] = state.stick_rmb;
  }

  UiCommand cmd = UiCommand::None;

  // ---- Main menu bar ----
  if (!state.chrome_hidden && ImGui::BeginMainMenuBar()) {
    // Alt+letter: OpenPopup must run inside the menu-bar window so IDs match
    // BeginMenu()'s GetID(label). Consume the request this frame.
    auto force_menu = [&](const char* name) {
      if (menu_request_ == name) {
        ImGui::OpenPopup(name);
        menu_request_.clear();
      }
    };

    force_menu("File");
    if (ImGui::BeginMenu("File")) {
      menu_item_cmd("New", UiCommand::NewDocument, cmd, true, "Ctrl+N");
      menu_item_cmd("Open…", UiCommand::OpenDocument, cmd, true, "Ctrl+O");
      menu_item_cmd("Open URL…", UiCommand::OpenUrlBar, cmd, true, "Ctrl+L");
      menu_item_cmd("New Tab", UiCommand::NewTab, cmd, true, "Ctrl+T");
      menu_item_cmd("Close Tab", UiCommand::CloseTab, cmd, true, "Ctrl+W");
      ImGui::Separator();
      menu_item_cmd("Open in Window…", UiCommand::OpenInWindow, cmd);
      menu_item_cmd("Pop Out to Window", UiCommand::PopOut, cmd, true,
                    "Ctrl+Shift+P");
      menu_item_cmd("Move Window to Tab", UiCommand::PopIn, cmd, state.has_floating,
                    "Ctrl+Shift+P");
      ImGui::Separator();
      menu_item_cmd("Save", UiCommand::Save, cmd, true, "Ctrl+S");
      menu_item_cmd("Save As…", UiCommand::SaveAs, cmd, true, "Ctrl+Shift+S");
      menu_item_cmd("Export as S-expression…", UiCommand::ExportSexpr, cmd, true);
      menu_item_cmd("Export as Markdown…", UiCommand::ExportMarkdown, cmd, true);
      ImGui::Separator();
      menu_item_cmd("Reload", UiCommand::Reload, cmd, state.has_path, "Ctrl+R");
      menu_item_cmd("Force Reload", UiCommand::ForceReload, cmd, state.has_path,
                    "Ctrl+Shift+R");
      menu_item_cmd("Revert to Saved", UiCommand::RevertToSaved, cmd,
                    state.has_path && state.dirty);
      menu_item_cmd("Drafts", UiCommand::OpenDrafts, cmd);
      ImGui::Separator();
      menu_item_cmd("Quit", UiCommand::Quit, cmd, true, "Ctrl+Q");
      ImGui::EndMenu();
    }
    force_menu("Edit");
    if (ImGui::BeginMenu("Edit")) {
      menu_item_cmd("Undo", UiCommand::Undo, cmd, true, "Ctrl+Z");
      menu_item_cmd("Redo", UiCommand::Redo, cmd, true, "Ctrl+Y");
      ImGui::Separator();
      menu_item_cmd("Cut", UiCommand::Cut, cmd, state.selection, "Ctrl+X");
      menu_item_cmd("Copy", UiCommand::Copy, cmd, state.selection, "Ctrl+C");
      menu_item_cmd("Copy Page URL", UiCommand::CopyPageUrl, cmd, true,
                    "Ctrl+Shift+U");
      menu_item_cmd("Paste", UiCommand::Paste, cmd, true, "Ctrl+V");
      menu_item_cmd("Select Block", UiCommand::SelectBlock, cmd, true, "Ctrl+A");
      menu_item_cmd("Select All", UiCommand::SelectAll, cmd, true, "Ctrl+Shift+A");
      menu_item_cmd("Duplicate Block", UiCommand::DuplicateBlock, cmd, true,
                    "Ctrl+D");
      menu_item_cmd("Move Block Up", UiCommand::MoveBlockUp, cmd, true,
                    "Alt+Up");
      menu_item_cmd("Move Block Down", UiCommand::MoveBlockDown, cmd, true,
                    "Alt+Down");
      ImGui::Separator();
      menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
      menu_item_cmd("Replace All", UiCommand::ReplaceAll, cmd, true,
                    "Ctrl+Shift+Enter");
      ImGui::Separator();
      menu_item_cmd("Preferences…", UiCommand::TogglePreferences, cmd);
      ImGui::EndMenu();
    }
    // Format / Insert only edit: not shown while browsing.
    if (!state.browse) force_menu("Format");
    if (!state.browse && ImGui::BeginMenu("Format")) {
      menu_item_cmd("Bold", UiCommand::Bold, cmd, state.selection, "Ctrl+B");
      menu_item_cmd("Italic", UiCommand::Italic, cmd, state.selection, "Ctrl+I");
      menu_item_cmd("Strikethrough", UiCommand::Strike, cmd, state.selection,
                    "Ctrl+Shift+X");
      menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
      menu_item_cmd("Link…", UiCommand::Link, cmd, state.selection, "Ctrl+K");
      menu_item_cmd("Citation…", UiCommand::Cite, cmd, state.selection,
                    "Ctrl+Shift+C");
      ImGui::Separator();
      if (ImGui::BeginMenu("Structure")) {
        menu_structure_items(state, cmd);
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (!state.browse) force_menu("Insert");
    if (!state.browse && ImGui::BeginMenu("Insert")) {
      menu_insert_items(cmd);
      ImGui::EndMenu();
    }
    force_menu("View");
    if (ImGui::BeginMenu("View")) {
      menu_item_cmd(state.native_view ? "Web Page View" : "Native View",
                    UiCommand::ToggleNative, cmd, !state.alternate_url.empty());
      if (ImGui::MenuItem("Reader Mode", "F9", state.reader,
                          state.has_chrome || state.reader))
        cmd = UiCommand::ToggleReader;
      if (ImGui::MenuItem("Paged Reading", "F8", state.paged))
        cmd = UiCommand::TogglePaged;
      if (ImGui::BeginMenu("Page columns", state.paged)) {
        static const struct { const char* label; const char* arg; int n; } cols[] = {
            {"Fit the width", "0", 0}, {"1 column", "1", 1},
            {"2 columns", "2", 2}, {"3 columns", "3", 3}};
        for (const auto& c : cols)
          if (ImGui::MenuItem(c.label, nullptr, state.page_columns == c.n)) {
            cmd = UiCommand::PageColumns;
            result.arg = c.arg;
          }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Collection", state.collection)) {
        menu_collection_items(state, cmd, result.arg);
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Split Pane", "Ctrl+\\", state.split))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Focus current section", "Ctrl+Alt+F", state.section_focus))
        cmd = UiCommand::ToggleSectionFocus;
      if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff, state.can_diff))
        cmd = UiCommand::ToggleDiff;
      if (ImGui::MenuItem("Structure Highlight", "Ctrl+Shift+L", state.structure))
        cmd = UiCommand::ToggleStructure;
      if (ImGui::MenuItem("Browse Mode (read-only)", "F2", state.browse))
        cmd = UiCommand::ToggleBrowse;
      menu_item_cmd("Content Only (hide menus, toolbars)",
                    UiCommand::ToggleChrome, cmd, true, "F11");
      ImGui::Separator();
      menu_item_cmd("Zoom In", UiCommand::ZoomIn, cmd, true, "Ctrl++");
      menu_item_cmd("Zoom Out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
      if (ImGui::BeginMenu("Line spacing")) {
        fill_line_spacing_items(state, cmd);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Margins")) {
        fill_margins_items(state, cmd, result.arg);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Text width")) {
        fill_measure_items(state, cmd, result.arg);
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Justify lines", nullptr, state.justify))
        cmd = UiCommand::ToggleJustify;
      if (ImGui::MenuItem("Footnote overlay", nullptr, state.footnote_overlay))
        cmd = UiCommand::ToggleFootnoteOverlay;
      if (ImGui::MenuItem("Bookmarks in the page", nullptr,
                          state.bookmark_marks))
        cmd = UiCommand::ToggleBookmarkMarks;
      if (ImGui::BeginMenu("Style sheet")) {
        fill_style_items(state, cmd, result.arg);
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    // Go: moving through and between pages, plus the panels that do so.
    force_menu("Go");
    if (ImGui::BeginMenu("Go")) {
      menu_item_cmd("Back", UiCommand::NavBack, cmd, state.can_nav_back, "Alt+Left");
      menu_item_cmd("Forward", UiCommand::NavForward, cmd, state.can_nav_forward,
                    "Alt+Right");
      menu_item_cmd("Up", UiCommand::GoUp, cmd, !state.up_url.empty(), "Alt+Up");
      menu_item_cmd("Home", UiCommand::GoHome, cmd, true, "Alt+Home");
      ImGui::Separator();
      menu_item_cmd("Previous Page in Site", UiCommand::SitemapPrev, cmd, true,
                    "Alt+PgUp");
      menu_item_cmd("Next Page in Site", UiCommand::SitemapNext, cmd, true,
                    "Alt+PgDn");
      ImGui::Separator();
      if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      ImGui::TextDisabled("  Ctrl+Alt+O docks outline | body");
      if (ImGui::MenuItem("Backlinks", "Ctrl+Alt+B", state.backlinks))
        cmd = UiCommand::ToggleBacklinks;
      if (ImGui::MenuItem("Site Map", "Ctrl+Alt+M", state.sitemap))
        cmd = UiCommand::ToggleSitemap;
      if (ImGui::MenuItem("Page Navigation", "Ctrl+Alt+P", state.page_panel))
        cmd = UiCommand::TogglePage;
      if (ImGui::MenuItem("Search Bar", "Ctrl+Alt+K", state.searchbar))
        cmd = state.searchbar ? UiCommand::ToggleSearchBar
                              : UiCommand::FocusSearchBar;
      if (ImGui::MenuItem("History", "Ctrl+Shift+H", state.history_open))
        cmd = UiCommand::ToggleHistory;
      ImGui::EndMenu();
    }
    force_menu("Bookmarks");
    if (ImGui::BeginMenu("Bookmarks")) {
      const char* add_label = state.current_bookmarked ? "Remove Bookmark"
                              : state.selection        ? "Bookmark Selection"
                                                       : "Bookmark This Page";
      if (ImGui::MenuItem(add_label, "Ctrl+Shift+B", false, state.has_path)) {
        cmd = state.current_bookmarked ? UiCommand::RemoveBookmark
                                       : UiCommand::AddBookmark;
      }
      if (ImGui::MenuItem("Bookmark Reading Position", nullptr, false,
                          state.has_path))
        cmd = UiCommand::BookmarkPosition;
      if (ImGui::MenuItem("Show Bookmarks…", nullptr, state.bookmarks_open))
        cmd = UiCommand::ToggleBookmarks;
      if (!state.bookmarks.empty()) {
        ImGui::Separator();
        // Newest bookmarks last; show up to 30 in the menu.
        const int n = static_cast<int>(state.bookmarks.size());
        const int start = n > 30 ? n - 30 : 0;
        for (int i = start; i < n; ++i) {
          const Bookmark& b = state.bookmarks[static_cast<size_t>(i)];
          // Truncate long labels for the menu.
          std::string label = bookmark_label(b);
          if (label.size() > 64) label = elide_middle(label, 28);
          ImGui::PushID(i);
          if (ImGui::MenuItem(label.c_str())) {
            cmd = UiCommand::NavigateUrl;
            result.arg = b.address;
          }
          ImGui::PopID();
        }
      }
      ImGui::EndMenu();
    }
    force_menu("Services");
    if (ImGui::BeginMenu("Services")) {
      for (const UiService& s : state.services) {
        if (s.separator_before) ImGui::Separator();
        if (ImGui::MenuItem(s.label.c_str(), nullptr, false, s.enabled)) {
          cmd = s.example ? UiCommand::OpenExample : UiCommand::NavigateUrl;
          result.arg = s.target;
        }
        if (!s.tip.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                 ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", s.tip.c_str());
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Open URL…", "Ctrl+L")) cmd = UiCommand::OpenUrlBar;
      ImGui::EndMenu();
    }
    force_menu("Help");
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Keyboard Reference", "F1", state.help))
        cmd = UiCommand::ToggleHelp;
      ImGui::Separator();
      menu_item_cmd("Format Reference (FORMAT.md)", UiCommand::OpenFormatDoc, cmd);
      ImGui::Separator();
      if (ImGui::BeginMenu("Examples")) {
        menu_example_items(cmd, result.arg);
        ImGui::EndMenu();
      }
      ImGui::Separator();
      menu_item_cmd("About xanatoo", UiCommand::OpenAbout, cmd);
      ImGui::EndMenu();
    }

    // Unmatched Alt+letter request (e.g. after menus reordered) — drop it.
    menu_request_.clear();
    result.top_reserved = ImGui::GetFrameHeight();
    ImGui::EndMainMenuBar();
  }

  // ---- Toolbars + tab strip (all hidden while content only) ----
  if (!state.chrome_hidden) toolbars(state, result, cmd);

  // ---- Menu of a bookmark's margin icon ----
  if (bookmark_menu_open_) {
    ImGui::SetNextWindowPos(ImVec2(context_x_, context_y_), ImGuiCond_Appearing);
    ImGui::OpenPopup("##bookmark_menu");
    bookmark_menu_open_ = false;
  }
  if (ImGui::BeginPopup("##bookmark_menu")) {
    const auto& bm = state.bookmark_menu;
    if (bm.address.empty()) {
      ImGui::CloseCurrentPopup();
    } else {
      std::string label = bm.label;
      if (label.size() > 64) label = elide_middle(label, 28);
      ImGui::TextDisabled("%s", label.c_str());
      ImGui::Separator();
      auto item = [&](const char* text, UiCommand c, const std::string& arg) {
        if (ImGui::MenuItem(text)) {
          cmd = c;
          result.arg = arg;
        }
      };
      item("Select Passage", UiCommand::SelectBookmark, bm.address);
      item("Go to Bookmark", UiCommand::NavigateUrl, bm.address);
      item("Show Only This", UiCommand::NavigateUrl, bm.cut);
      item("Copy Address", UiCommand::CopyText, bm.address);
      ImGui::Separator();
      item("Remove Bookmark", UiCommand::RemoveBookmark, bm.address);
      if (ImGui::MenuItem("Show Bookmarks…", nullptr, state.bookmarks_open))
        cmd = UiCommand::ToggleBookmarks;
    }
    ImGui::EndPopup();
  }

  // ---- Context menu (right-click in document) ----
  if (context_open_) {
    ImGui::SetNextWindowPos(ImVec2(context_x_, context_y_), ImGuiCond_Appearing);
    ImGui::OpenPopup("##doc_context");
    context_open_ = false;
  }
  if (ImGui::BeginPopup("##doc_context")) {
    if (state.collection) {
      if (ImGui::BeginMenu("Collection")) {
        menu_collection_items(state, cmd, result.arg);
        ImGui::EndMenu();
      }
      ImGui::Separator();
    }
    if (state.browse) {
      // Read-only: no edit / format / insert / structure items.
      menu_item_cmd("Copy", UiCommand::Copy, cmd, state.selection, "Ctrl+C");
      menu_item_cmd("Copy Page URL", UiCommand::CopyPageUrl, cmd, true,
                    "Ctrl+Shift+U");
      if (!state.context_comment_native.empty()) {
        if (ImGui::MenuItem("Copy Link to Comment")) {
          cmd = UiCommand::CopyText;
          result.arg = state.context_comment_native;
        }
        if (ImGui::MenuItem("Copy Web Permalink")) {
          cmd = UiCommand::CopyText;
          result.arg = state.context_comment_web;
        }
      }
      menu_item_cmd("Select Block", UiCommand::SelectBlock, cmd, true, "Ctrl+A");
      ImGui::Separator();
    } else {
      menu_item_cmd("Cut", UiCommand::Cut, cmd, state.selection, "Ctrl+X");
      menu_item_cmd("Copy", UiCommand::Copy, cmd, state.selection, "Ctrl+C");
      menu_item_cmd("Paste", UiCommand::Paste, cmd, true, "Ctrl+V");
      ImGui::Separator();
      menu_item_cmd("Bold", UiCommand::Bold, cmd, state.selection, "Ctrl+B");
      menu_item_cmd("Italic", UiCommand::Italic, cmd, state.selection, "Ctrl+I");
      menu_item_cmd("Strikethrough", UiCommand::Strike, cmd, state.selection,
                    "Ctrl+Shift+X");
      menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
      menu_item_cmd("Link…", UiCommand::Link, cmd, state.selection, "Ctrl+K");
      menu_item_cmd("Citation…", UiCommand::Cite, cmd, state.selection,
                    "Ctrl+Shift+C");
      ImGui::Separator();
      if (ImGui::BeginMenu("Insert")) {
        menu_insert_items(cmd);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Structure")) {
        menu_structure_items(state, cmd);
        ImGui::EndMenu();
      }
      ImGui::Separator();
      menu_item_cmd("Select Block", UiCommand::SelectBlock, cmd, true, "Ctrl+A");
      menu_item_cmd("Duplicate Block", UiCommand::DuplicateBlock, cmd, true,
                    "Ctrl+D");
      menu_item_cmd("Move Block Up", UiCommand::MoveBlockUp, cmd, true, "Alt+Up");
      menu_item_cmd("Move Block Down", UiCommand::MoveBlockDown, cmd, true,
                    "Alt+Down");
      ImGui::Separator();
    }
    if (ImGui::MenuItem("Structure Highlight", "Ctrl+Shift+L", state.structure))
      cmd = UiCommand::ToggleStructure;
    if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
      cmd = UiCommand::ToggleOutline;
    if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff, state.can_diff))
      cmd = UiCommand::ToggleDiff;
    menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
    if (!state.browse)
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
    if (state.chrome_hidden)
      menu_item_cmd("Show Menus and Toolbars", UiCommand::ToggleChrome, cmd,
                    true, "F11");
    ImGui::EndPopup();
  }

  // ---- Preferences ----
  if (state.preferences_open) {
    bool open = true;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Preferences", &open,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      // Checkboxes show the state; a click issues the toggle command.
      auto toggle = [&](const char* label, bool on, UiCommand c,
                        const char* tip) {
        if (ImGui::Checkbox(label, &on)) cmd = c;
        ImGui::TextDisabled("  %s", tip);
      };
      ImGui::SeparatorText("Browsing");
      toggle("Preview links on hover", state.hover_preview,
             UiCommand::ToggleHoverPreview,
             "Split view: a link hovered on the left shows on the right");
      toggle("Reader mode by default", state.reader_default,
             UiCommand::ToggleReaderDefault,
             "Web pages without navigation, sidebars and footers (F9 per view)");
      toggle("Paged reading by default", state.paged_default,
             UiCommand::TogglePagedDefault,
             "Ebook style pages instead of scrolling (F8 per view)");
      ImGui::SeparatorText("Reading");
      toggle("Footnote overlay", state.footnote_overlay,
             UiCommand::ToggleFootnoteOverlay,
             "Notes of on-screen footnote marks at the pane bottom");
      toggle("Mark bookmarks in the page", state.bookmark_marks,
             UiCommand::ToggleBookmarkMarks,
             "Wash over bookmarked passages, a ribbon at reading positions");
      toggle("Justify text", state.justify, UiCommand::ToggleJustify,
             "Wrapped body lines fill the column");
      ImGui::Spacing();
      ImGui::TextDisabled("Saved to view.conf; layout options are in View.");
      if (ImGui::Button("Close")) open = false;
    }
    ImGui::End();
    if (!open) cmd = UiCommand::TogglePreferences;
  }

  // ---- Session History panel ----
  if (state.history_open) {
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(480.f, 360.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History", &open)) {
      ImGui::TextDisabled("Session navigation history · click to open");
      ImGui::Separator();
      if (state.history_entries.empty()) {
        ImGui::TextWrapped("No pages yet. Open a file or gemini:// URL to start.");
      } else {
        // Show newest first.
        for (int i = static_cast<int>(state.history_entries.size()) - 1; i >= 0;
             --i) {
          const std::string& e = state.history_entries[static_cast<size_t>(i)];
          bool current = (i == state.history_pos);
          if (current) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.35f, 0.7f, 1.f));
          std::string label = e;
          if (current) label = "▸ " + label;
          if (ImGui::Selectable(label.c_str(), current)) {
            cmd = UiCommand::NavigateUrl;
            result.arg = e;
          }
          if (current) ImGui::PopStyleColor();
        }
      }
    }
    ImGui::End();
    if (!open) cmd = UiCommand::ToggleHistory;
  }

  // ---- Bookmarks manager panel ----
  if (state.bookmarks_open) {
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(480.f, 360.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Bookmarks", &open)) {
      ImGui::TextDisabled("Persistent bookmarks (state dir: bookmarks)");
      ImGui::Separator();
      if (ImGui::Button(state.current_bookmarked ? "Remove current page"
                        : state.selection        ? "Bookmark selection"
                                                 : "Bookmark current page")) {
        cmd = state.current_bookmarked ? UiCommand::RemoveBookmark
                                       : UiCommand::AddBookmark;
      }
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        open = false;
      }
      ImGui::Separator();
      if (state.bookmarks.empty()) {
        ImGui::TextWrapped("No bookmarks yet.");
      } else {
        for (int i = 0; i < static_cast<int>(state.bookmarks.size()); ++i) {
          const Bookmark& b = state.bookmarks[static_cast<size_t>(i)];
          ImGui::PushID(i);
          // The quote gets a wrapped line of its own below.
          const std::string label =
              b.quote.empty() || b.title.empty() ? bookmark_label(b) : b.title;
          if (ImGui::Selectable(label.c_str(), false,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
              cmd = UiCommand::NavigateUrl;
              result.arg = b.address;
            }
          }
          if (ImGui::IsItemHovered() && label != b.address)
            ImGui::SetTooltip("%s", b.address.c_str());
          if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(1)) {
            ImGui::OpenPopup("##bm_ctx");
          }
          if (ImGui::BeginPopup("##bm_ctx")) {
            if (ImGui::MenuItem("Open")) {
              cmd = UiCommand::NavigateUrl;
              result.arg = b.address;
            }
            if (ImGui::MenuItem("Remove")) {
              cmd = UiCommand::RemoveBookmark;
              result.arg = b.address;
            }
            ImGui::EndPopup();
          }
          if (!b.quote.empty() && !b.title.empty()) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", b.quote.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent();
          }
          ImGui::PopID();
        }
        ImGui::TextDisabled("Double-click to open · right-click for menu");
      }
    }
    ImGui::End();
    if (!open) cmd = UiCommand::ToggleBookmarks;
  }

  result.cmd = cmd;

  // ---- On-screen keyboard (handheld / no physical keyboard) ----
  if (state.virtual_keyboard) {
    ImGuiViewport* vk_vp = ImGui::GetMainViewport();
    const float vk_h = (vk_vp->Size.y < 500.f ? 200.f : 220.f) * scale_;
    ImGui::SetNextWindowPos(
        ImVec2(vk_vp->Pos.x, vk_vp->Pos.y + vk_vp->Size.y - vk_h));
    ImGui::SetNextWindowSize(ImVec2(vk_vp->Size.x, vk_h));
    ImGuiWindowFlags vk_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##vkeyboard", nullptr, vk_flags)) {
      auto keybtn = [&](const char* label, const char* payload, float w) {
        if (ImGui::Button(label, ImVec2(w, 34.f * scale_))) {
          if (cmd == UiCommand::None) {
            cmd = UiCommand::VirtualKey;
            result.arg = payload;
          }
        }
        ImGui::SameLine(0.f, 2.f);
      };
      static bool shift = false;
      const float kw = std::max(26.f, (vk_vp->Size.x - 40.f) / 11.f);
      const char* row1 = shift ? "QWERTYUIOP" : "qwertyuiop";
      const char* row2 = shift ? "ASDFGHJKL" : "asdfghjkl";
      const char* row3 = shift ? "ZXCVBNM" : "zxcvbnm";
      ImGui::SetCursorPosX(4.f);
      for (const char* c = row1; *c; ++c) {
        char lab[2] = {*c, 0};
        char pay[2] = {*c, 0};
        keybtn(lab, pay, kw);
      }
      ImGui::NewLine();
      ImGui::SetCursorPosX(4.f + kw * 0.5f);
      for (const char* c = row2; *c; ++c) {
        char lab[2] = {*c, 0};
        char pay[2] = {*c, 0};
        keybtn(lab, pay, kw);
      }
      ImGui::NewLine();
      if (ImGui::Button(shift ? "SHIFT" : "Shift", ImVec2(kw * 1.6f, 34.f * scale_)))
        shift = !shift;
      ImGui::SameLine(0.f, 2.f);
      for (const char* c = row3; *c; ++c) {
        char lab[2] = {*c, 0};
        char pay[2] = {*c, 0};
        keybtn(lab, pay, kw);
      }
      if (ImGui::Button("Bksp", ImVec2(kw * 1.8f, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = "\b";
        }
      }
      ImGui::NewLine();
      ImGui::SetCursorPosX(4.f);
      for (const char* d = "1234567890"; *d; ++d) {
        char lab[2] = {*d, 0};
        char pay[2] = {*d, 0};
        keybtn(lab, pay, kw * 0.95f);
      }
      ImGui::NewLine();
      if (ImGui::Button(",", ImVec2(kw, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ",";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      const float space_w = std::max(100.f, vk_vp->Size.x - kw * 6.f);
      if (ImGui::Button("Space", ImVec2(space_w, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = " ";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button(".", ImVec2(kw, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ".";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button("Enter", ImVec2(kw * 2.f, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = "\n";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button("Hide", ImVec2(kw * 1.6f, 34.f * scale_)))
        cmd = UiCommand::ToggleVirtualKeyboard;
    }
    ImGui::End();
    result.bottom_reserved = vk_h;
  }

  return result;
}

void Ui::end_frame(SDL_Renderer* renderer) {
  if (!ready_) return;
  // A text field (URL bar) just took focus: on-screen keyboard (Android).
  const bool wants_text = ImGui::GetIO().WantTextInput;
  if (wants_text && !wanted_text_input_) platform_request_keyboard(window_);
  wanted_text_input_ = wants_text;
  ImGui::Render();
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::render(ImGui::GetDrawData(), renderer);
#else
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
#endif
}

}  // namespace xanatoo
