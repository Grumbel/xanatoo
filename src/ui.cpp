// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/examples.hpp"
#include "xanatoo/ui.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/view_settings.hpp"
#include <cmath>
#include "xanatoo/font.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/platform.hpp"
#include "xanatoo/reddit_credentials.hpp"
#include "xanatoo/reply.hpp"
#include "xanatoo/util.hpp"
#include "ui_panel_imgui_order.hpp"

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
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define STBI_ONLY_PNG
#include "stb_image.h"

namespace fs = std::filesystem;

namespace xanatoo {

// Fit covers as well as square favicons into the available icon box.
static ImVec2 icon_size(SDL_Texture* texture, float side) {
#if XANATOO_SDL3
  float w = 0, h = 0;
  if (!SDL_GetTextureSize(texture, &w, &h) || w <= 0 || h <= 0)
    return ImVec2(side, side);
#else
  int w = 0, h = 0;
  if (SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0)
    return ImVec2(side, side);
#endif
  const float factor = side / static_cast<float>(std::max(w, h));
  return ImVec2(w * factor, h * factor);
}



namespace {

// The immediate-mode menu helpers below are free functions shared by several
// menus. Keep their current renderer's icon textures available for the frame.
std::unordered_map<std::string, SDL_Texture*> menu_icon_textures;

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

// Toolbar controls use a visible raised face even when idle. Their pressed
// edge reverses like the classic GTK2 buttons; the same treatment is shared
// by icon buttons and the text-only fallback.
int push_toolbar_button_style() {
  const theme::Palette& p = theme::palette();
  ImGuiStyle& s = ImGui::GetStyle();
  auto color = [](theme::Rgb c) {
    return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f);
  };
  ImGui::PushStyleColor(ImGuiCol_Button, color(p.face_light));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(p.accent_soft));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(p.face_dark));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,
                      std::max(1.f, s.FrameBorderSize));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding);
  return 3;
}

void draw_toolbar_button_bevel() {
  const bool pressed = ImGui::IsItemActive();
  const theme::Palette& p = theme::palette();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const int alpha = std::clamp(static_cast<int>(255.f * ImGui::GetStyle().Alpha),
                               0, 255);
  auto rgba = [alpha](theme::Rgb c) {
    return IM_COL32(c.r, c.g, c.b, alpha);
  };
  const ImU32 light = rgba(pressed ? p.shadow : p.highlight);
  const ImU32 dark = rgba(pressed ? p.highlight : p.shadow);
  draw->AddLine(ImVec2(min.x + 1.f, min.y), ImVec2(max.x - 2.f, min.y), light);
  draw->AddLine(ImVec2(min.x, min.y + 1.f), ImVec2(min.x, max.y - 2.f), light);
  draw->AddLine(ImVec2(min.x + 1.f, max.y - 1.f),
                ImVec2(max.x - 1.f, max.y - 1.f), dark);
  draw->AddLine(ImVec2(max.x - 1.f, min.y + 1.f),
                ImVec2(max.x - 1.f, max.y - 1.f), dark);
}

// All ordinary panel actions use the same raised detail and GTK-sized frame,
// rather than mixing ImGui's small (zero vertical padding) and regular buttons.
bool classic_button(const char* label, const ImVec2& size = ImVec2(0.f, 0.f)) {
  const bool clicked = ImGui::Button(label, size);
  draw_toolbar_button_bevel();
  return clicked;
}

void same_line_if_fits(const char* next_label) {
  const ImGuiStyle& s = ImGui::GetStyle();
  const float available = ImGui::GetWindowPos().x +
      ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x;
  const float needed = ImGui::CalcTextSize(next_label).x +
      2.f * s.FramePadding.x + s.ItemSpacing.x;
  if (available >= needed) ImGui::SameLine();
}

void draw_toolbar_menu_marker(float scale) {
  const ImVec2 max = ImGui::GetItemRectMax();
  const float inset = 3.f * scale;
  const float width = 6.f * scale;
  const float height = 4.f * scale;
  const ImVec2 a(max.x - inset - width, max.y - inset - height);
  const ImVec2 b(max.x - inset, max.y - inset - height);
  const ImVec2 c(max.x - inset - width * 0.5f, max.y - inset);
  const theme::Palette& p = theme::palette();
  ImGui::GetWindowDrawList()->AddTriangleFilled(
      a, b, c, IM_COL32(p.shadow.r, p.shadow.g, p.shadow.b, 255));
}

void pop_toolbar_button_style(int colors) {
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(colors);
}

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
  if (ImGui::MenuItem("Manage / edit style sheets..."))
    cmd = UiCommand::ToggleStylesPanel;
  if (ImGui::MenuItem("Reload style sheets"))
    cmd = UiCommand::ReloadStyles;
}

const char* menu_icon_for(UiCommand command) {
  switch (command) {
    case UiCommand::NewDocument:
    case UiCommand::NewTab: return "new";
    case UiCommand::OpenDocument:
    case UiCommand::OpenInWindow: return "open";
    case UiCommand::OpenUrlBar:
    case UiCommand::Find: return "find";
    case UiCommand::CloseTab:
    case UiCommand::Quit: return "stop";
    case UiCommand::PopOut:
    case UiCommand::PopIn:
    case UiCommand::ToggleSplit: return "split";
    case UiCommand::ToggleTrail: return "history";
    case UiCommand::Save:
    case UiCommand::SaveAs: return "save";
    case UiCommand::Undo: return "undo";
    case UiCommand::Redo: return "redo";
    case UiCommand::Cut: return "cut";
    case UiCommand::Copy: return "copy";
    case UiCommand::CopyPageUrl:
    case UiCommand::Link: return "link";
    case UiCommand::Paste: return "paste";
    case UiCommand::Replace:
    case UiCommand::ReplaceAll: return "replace";
    case UiCommand::Bold: return "bold";
    case UiCommand::Italic: return "italic";
    case UiCommand::Strike: return "strike";
    case UiCommand::Code:
    case UiCommand::InsertCodeBlock: return "code";
    case UiCommand::Cite:
    case UiCommand::InsertBiblioEntry: return "cite";
    case UiCommand::InsertParagraph:
    case UiCommand::ParagraphLevel:
    case UiCommand::InsertHr: return "para";
    case UiCommand::InsertImage: return "image";
    case UiCommand::InsertFigure: return "figure";
    case UiCommand::InsertNote: return "note";
    case UiCommand::InsertTable: return "table";
    case UiCommand::InsertDefList: return "lists";
    case UiCommand::ToggleUnorderedList: return "list";
    case UiCommand::ToggleOrderedList: return "olist";
    case UiCommand::ToggleTask: return "task";
    case UiCommand::ToggleBlockQuote: return "quote";
    case UiCommand::MakeHeader:
    case UiCommand::Heading1: return "h1";
    case UiCommand::Heading2: return "h2";
    case UiCommand::Heading3: return "h3";
    case UiCommand::NavBack: return "back";
    case UiCommand::NavForward: return "forward";
    case UiCommand::GoUp: return "up";
    case UiCommand::GoHome: return "home";
    case UiCommand::AddBookmark:
    case UiCommand::BookmarkPage:
    case UiCommand::BookmarkPosition:
    case UiCommand::RemoveBookmark: return "bookmark";
    case UiCommand::ToggleHistory: return "history";
    case UiCommand::ToggleDownloads: return "downloads";
    case UiCommand::ToggleOutline: return "outline";
    case UiCommand::ToggleStructure: return "structure";
    case UiCommand::ToggleDiff: return "diff";
    case UiCommand::ToggleBrowse: return "browse";
    case UiCommand::ToggleNative: return "native";
    case UiCommand::ToggleReader: return "reader";
    case UiCommand::ToggleHelp: return "help";
    case UiCommand::ZoomIn: return "zoomin";
    case UiCommand::ZoomOut: return "zoomout";
    case UiCommand::OpenFormatDoc: return "format";
    default: return nullptr;
  }
}

bool menu_item_with_icon(const char* label, const char* icon_name,
                         const char* shortcut, bool selected, bool enabled) {
  auto it = menu_icon_textures.find(icon_name ? icon_name : "");
  if (it == menu_icon_textures.end() || !it->second)
    return ImGui::MenuItem(label, shortcut, selected, enabled);

  // Reserve ImGui's icon column with spaces, then replace it visually with
  // the matching toolbar texture after MenuItemEx creates the hit area.
  const bool clicked = ImGui::MenuItemEx(label, "    ", shortcut, selected,
                                         enabled);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float side = ImGui::GetFontSize() * 1.05f;
  const float x = min.x + 1.f;
  const float y = min.y + (max.y - min.y - side) * 0.5f;
  const ImU32 tint = enabled ? IM_COL32(255, 255, 255, 255)
                             : IM_COL32(160, 160, 160, 150);
  ImGui::GetWindowDrawList()->AddImage(
      reinterpret_cast<ImTextureID>(it->second), ImVec2(x, y),
      ImVec2(x + side, y + side), ImVec2(0, 0), ImVec2(1, 1), tint);
  return clicked;
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
  s.FramePadding = ImVec2(8.f, 5.f);
  s.ItemSpacing = ImVec2(7.f, 6.f);
  s.ItemInnerSpacing = ImVec2(5.f, 4.f);
  s.ScrollbarSize = 14.f;
  s.GrabMinSize = 16.f;

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
  c[ImGuiCol_ScrollbarGrab] = P(pal.face_light);
  c[ImGuiCol_ScrollbarGrabHovered] = P(pal.accent_soft);
  c[ImGuiCol_ScrollbarGrabActive] = P(pal.face_dark);
  c[ImGuiCol_CheckMark] = sel;
  c[ImGuiCol_SliderGrab] = sel;
  c[ImGuiCol_SliderGrabActive] = sel_hi;
  c[ImGuiCol_Button] = P(pal.face_light);
  c[ImGuiCol_ButtonHovered] = P(pal.accent_soft);
  c[ImGuiCol_ButtonActive] = P(pal.face_dark);
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
  menu_icon_textures.clear();

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
      "back", "forward", "up", "home", "reload", "stop", "bookmark", "history", "downloads", "browse", "native", "reader", "sitemap",
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
    menu_icon_textures[name] = ic.texture;
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
                     UiCommand c, UiCommand& out, bool enabled,
                     bool* right_clicked) {
  const Icon* ic = icon(icon_name);
  bool pressed = false;
  ImGui::BeginDisabled(!enabled);
  const int style_colors = push_toolbar_button_style();
  if (ic) {
    ImGui::PushID(id);
    const ImVec2 size(28.f * scale_, 28.f * scale_);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(4.f * scale_, 4.f * scale_));
    if (ImGui::ImageButton(
            id, reinterpret_cast<ImTextureID>(ic->texture), size,
            ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
            ImVec4(1, 1, 1, enabled ? 1.f : 0.4f))) {
      pressed = true;
      out = c;
    }
    draw_toolbar_button_bevel();
    ImGui::PopStyleVar();
    ImGui::PopID();
  } else {
    // Text fallback when PNG missing (e.g. icons not rasterized yet).
    static const std::unordered_map<std::string, const char*> kShort = {
        {"back", "◀"}, {"forward", "▶"}, {"reload", "↻"}, {"stop", "✕"}, {"bookmark", "★"},
        {"history", "Hist"}, {"downloads", "DL"}, {"browse", "Browse"}, {"native", "Native"},
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
    if (ImGui::Button(label.c_str(), ImVec2(0.f, 36.f * scale_))) {
      pressed = true;
      out = c;
    }
    draw_toolbar_button_bevel();
  }
  pop_toolbar_button_style(style_colors);
  ImGui::EndDisabled();
  const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  if (right_clicked)
    *right_clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
  if (hovered && tip && tip[0]) {
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
  ImVec2 popup_pos;
  const int style_colors = push_toolbar_button_style();
  if (ic) {
    const ImVec2 size(28.f * scale_, 28.f * scale_);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(4.f * scale_, 4.f * scale_));
    if (ImGui::ImageButton(
            "##btn", reinterpret_cast<ImTextureID>(ic->texture), size,
            ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
            ImVec4(1, 1, 1, 1.f)))
      pressed = true;
    draw_toolbar_button_bevel();
    draw_toolbar_menu_marker(scale_);
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
    if (ImGui::Button(label.c_str(), ImVec2(0.f, 36.f * scale_))) pressed = true;
    draw_toolbar_button_bevel();
  }
  const ImVec2 button_min = ImGui::GetItemRectMin();
  const ImVec2 button_max = ImGui::GetItemRectMax();
  popup_pos = ImVec2(button_min.x, button_max.y);
  pop_toolbar_button_style(style_colors);
  if (pressed) ImGui::OpenPopup("##menu");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tip && tip[0])
    ImGui::SetTooltip("%s", tip);
  // Set this after the tooltip: SetTooltip() begins another ImGui window and
  // consumes SetNextWindowPos before the popup gets a chance to use it.
  if (ImGui::IsPopupOpen("##menu"))
    ImGui::SetNextWindowPos(popup_pos, ImGuiCond_Appearing);
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
  // GNOME 2 commonly used Sans 10. Keep its Sans-family look, with a little
  // more pixel size to balance xanatoo's intentionally large 32 px icons.
  const float size_px = std::round(16.0f * scale);
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
  ImGui::GetStyle().ScaleAllSizes(scale_);
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
  menu_icon_textures.clear();
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

bool Ui::pointer_obscured(float x, float y,
                          std::optional<UiPanelLayer> native_top) const {
  if (!ready_ || !native_top) return false;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                 ImGuiPopupFlags_AnyPopupLevel)) return false;
  ImGuiWindow* hovered = nullptr;
  ImGui::FindHoveredWindowEx(ImVec2(x, y), true, &hovered, nullptr);
  return hovered && ui_panel_obscured(ui_panel_layer(hovered->Name), native_top);
}

bool Ui::captures_point(float x, float y,
                         std::optional<UiPanelLayer> native_top) const {
  if (!ready_) return false;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                 ImGuiPopupFlags_AnyPopupLevel))
    return true;
  ImGuiWindow* hovered = nullptr;
  ImGui::FindHoveredWindowEx(ImVec2(x, y), true, &hovered, nullptr);
  return hovered && !ui_panel_obscured(ui_panel_layer(hovered->Name), native_top);
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

void Ui::open_tile_menu(float x, float y) {
  tile_menu_open_ = true;
  tile_menu_x_ = x;
  tile_menu_y_ = y;
}

void Ui::open_bookmark_menu(float x, float y) {
  bookmark_menu_open_ = true;
  context_x_ = x;
  context_y_ = y;
}

static bool menu_item_cmd(const char* label, UiCommand c, UiCommand& out,
                          bool enabled = true, const char* shortcut = nullptr) {
  if (menu_item_with_icon(label, menu_icon_for(c), shortcut, false, enabled)) {
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
  if (ImGui::MenuItem("Compare tiles", "Ctrl+Shift+D", state.diff, state.can_diff))
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
  auto toolbar_separator = [&] {
    ImGui::SameLine(0.f, 4.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float row_h = 32.f * scale_ + 6.f;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x + 3.f, p.y + 4.f),
        ImVec2(p.x + 3.f, p.y + row_h - 4.f),
        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(7.f, row_h));
    ImGui::SameLine(0.f, 4.f);
  };

  // -- Browse / navigation row (browser chrome) --
  const float button_row_h = 32.f * scale_ + 16.f;
  // Keep a useful location field when the navigation controls crowd it.
  const float min_url_w = 200.f * scale_;
  const float button_w = 32.f * scale_ + 8.f + ImGui::GetStyle().ItemSpacing.x;
  const int right_buttons = state.browse ? 7 : 6;
  const int url_extras = (state.has_chrome ? 1 : 0) +
                         (state.has_nav ? 1 : 0) + (state.has_site ? 1 : 0);
  const bool url_own_row = vp->Size.x <
      12.f + (6 + right_buttons) * button_w + 15.f + min_url_w +
          url_extras * (30.f * scale_ + 2.f);
  const float browse_h = button_row_h + (url_own_row ? button_row_h : 0.f);
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, browse_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
  if (ImGui::Begin("##toolbar_browse", nullptr, tb_flags)) {
    auto history_menu = [&](const char* id,
                            const std::vector<HistoryMenuItem>& entries,
                            const char* empty_label) {
      if (!ImGui::BeginPopup(id)) return;
      if (entries.empty()) ImGui::TextDisabled("%s", empty_label);
      for (const HistoryMenuItem& e : entries) {
        const std::string label = elide_middle(e.url, 68);
        ImGui::PushID(e.steps);
        if (ImGui::MenuItem(label.c_str())) {
          cmd = UiCommand::NavJump;
          result.arg = std::to_string(e.steps);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", e.url.c_str());
        ImGui::PopID();
      }
      ImGui::EndPopup();
    };
    bool back_context = false;
    tool_button("tb_back", "back", "Back (Alt+Left)", UiCommand::NavBack, cmd,
                state.can_nav_back, &back_context);
    if (back_context) ImGui::OpenPopup("##back_history");
    history_menu("##back_history", state.back_history, "No previous pages");
    bool forward_context = false;
    tool_button("tb_fwd", "forward", "Forward (Alt+Right)", UiCommand::NavForward,
                cmd, state.can_nav_forward, &forward_context);
    if (forward_context) ImGui::OpenPopup("##forward_history");
    history_menu("##forward_history", state.forward_history, "No next pages");
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

    if (!url_own_row) toolbar_separator();

    // Location / URL field (path or gemini://)
    auto location_field = [&] {
      const float row_y = ImGui::GetCursorPosY();
      const float button_h = 32.f * scale_ + 6.f;
      const float field_h = 32.f * scale_;
      std::string want = state.doc_label.empty() ? "untitled" : state.doc_label;
      if (state.dirty) {
        ImGui::SetCursorPosY(row_y +
                             (button_h - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextDisabled("*");
        ImGui::SameLine(0.f, 2.f);
      }
      // Context controls stay with the address on either toolbar row.
      const float right_pad = url_extras ? 2.f * scale_ : 0.f;
      const float extra_w = url_extras * 28.f * scale_ + right_pad;
      // Leave room for the buttons right of the field (measured last frame).
      float field_w =
          ImGui::GetContentRegionAvail().x -
          (url_own_row ? 0.f : url_right_w_ + 6.f);
      if (field_w < 40.f + extra_w) field_w = 40.f + extra_w;
      ImGui::SetNextItemWidth(std::max(40.f, field_w - extra_w));
      ImGui::SetCursorPosY(row_y + (button_h - field_h) * 0.5f);
      const ImVec2 field_pos = ImGui::GetCursorScreenPos();
      const ImVec2 field_end(field_pos.x + field_w, field_pos.y + field_h);
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(field_pos, field_end, IM_COL32(242, 242, 247, 255),
                          ImGui::GetStyle().FrameRounding);
      draw->AddRect(field_pos, field_end, ImGui::GetColorU32(ImGuiCol_Border),
                    ImGui::GetStyle().FrameRounding);

      if (focus_url_bar_) {
        ImGui::SetKeyboardFocusHere();
        std::snprintf(url_input_, sizeof(url_input_), "%s", want.c_str());
        url_synced_from_ = want;
        focus_url_bar_ = false;
      } else if (select_url_bar_) {
        // AutoSelectAll on InputText selects the existing edited address.
        ImGui::SetKeyboardFocusHere();
        select_url_bar_ = false;
      } else if (want != url_synced_from_ && !url_bar_editing_ &&
                 !ImGui::IsPopupOpen("##url_context")) {
        std::snprintf(url_input_, sizeof(url_input_), "%s", want.c_str());
        url_synced_from_ = want;
      }

      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.f, 0.f, 0.f, 0.f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
      ImGui::PushStyleVar(
          ImGuiStyleVar_FramePadding,
          ImVec2((state.location_icon ? 30.f : 6.f) * scale_,
                 std::max(0.f, (field_h - ImGui::GetFontSize()) * 0.5f)));
      bool enter = ImGui::InputText(
          "##urlbar", url_input_, sizeof(url_input_),
          ImGuiInputTextFlags_EnterReturnsTrue |
              ImGuiInputTextFlags_AutoSelectAll);
      const ImGuiID url_id = ImGui::GetItemID();
      const ImGuiInputTextState* input = ImGui::GetInputTextState(url_id);
      const bool has_selection = input && input->HasSelection();
      const int selection_start = has_selection ? input->GetSelectionStart() : 0;
      const int selection_end = has_selection ? input->GetSelectionEnd() : 0;
      const int cursor = input ? input->GetCursorPos()
                               : static_cast<int>(std::strlen(url_input_));
      const bool url_active = ImGui::IsItemActive();
      const bool url_right_clicked = ImGui::IsItemHovered() &&
          ImGui::IsMouseReleased(ImGuiMouseButton_Right);
      if (url_right_clicked) {
        // Keep the selection while the popup takes keyboard focus.
        url_context_selection_ = has_selection;
        url_context_begin_ = selection_start;
        url_context_end_ = selection_end;
        url_context_cursor_ = cursor;
      }
      if (state.location_icon) {
        const ImVec2 p = ImGui::GetItemRectMin();
        const float side = 20.f * scale_;
        const ImVec2 size = icon_size(state.location_icon, side);
        const float y = p.y + (field_h - size.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(state.location_icon),
            ImVec2(p.x + 5.f * scale_ + (side - size.x) * 0.5f, y),
            ImVec2(p.x + 5.f * scale_ + (side + size.x) * 0.5f, y + size.y));
      }
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(3);
      url_bar_editing_ = url_active;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(
            "Location — path or gemini:// URL · Enter to open · Ctrl+L focuses");
      if (url_right_clicked) ImGui::OpenPopup("##url_context");
      if (ImGui::BeginPopup("##url_context")) {
        const std::string value = url_input_;
        const bool selected = url_context_selection_;
        const int from = url_context_begin_;
        const int to = url_context_end_;
        const char* clip = ImGui::GetClipboardText();
        const std::string clipboard = clip ? clip : "";
        auto edit_url = [&](const std::string& replacement) {
          std::string updated = value;
          const size_t begin = static_cast<size_t>(selected ? from : url_context_cursor_);
          const size_t end = static_cast<size_t>(selected ? to : url_context_cursor_);
          if (begin > updated.size() || end > updated.size() || end < begin ||
              updated.size() - (end - begin) + replacement.size() >= sizeof(url_input_))
            return;
          updated.replace(begin, end - begin, replacement);
          std::snprintf(url_input_, sizeof(url_input_), "%s", updated.c_str());
          if (ImGuiInputTextState* st = ImGui::GetInputTextState(url_id))
            st->ReloadUserBufAndMoveToEnd();
          url_bar_editing_ = true;
        };
        if (ImGui::MenuItem("Cut", nullptr, false, selected)) {
          ImGui::SetClipboardText(value.substr(from, to - from).c_str());
          edit_url("");
        }
        if (ImGui::MenuItem("Copy", nullptr, false, selected))
          ImGui::SetClipboardText(value.substr(from, to - from).c_str());
        if (ImGui::MenuItem("Paste", nullptr, false, !clipboard.empty()))
          edit_url(clipboard);
        if (ImGui::MenuItem("Select All")) select_url_bar_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Address", nullptr, false, !value.empty()))
          ImGui::SetClipboardText(value.c_str());
        if (ImGui::MenuItem("Open Address", nullptr, false, !value.empty())) {
          cmd = UiCommand::NavigateUrl;
          result.arg = value;
        }
        if (ImGui::MenuItem("Paste and Go", nullptr, false, !clipboard.empty())) {
          cmd = UiCommand::NavigateUrl;
          result.arg = clipboard;
        }
        if (state.has_path) {
          ImGui::Separator();
          if (state.page_bookmarked) {
            if (ImGui::MenuItem("Remove Page Bookmark"))
              cmd = UiCommand::RemoveBookmark;
          } else if (ImGui::MenuItem("Bookmark This Page")) {
            cmd = UiCommand::BookmarkPage;
          }
        }
        ImGui::EndPopup();
      }
      if (enter) {
        cmd = UiCommand::NavigateUrl;
        result.arg = url_input_;
        url_synced_from_ = result.arg;
        url_bar_editing_ = false;
        ImGui::ClearActiveID();
      }
      auto extra = [&](const char* id, const char* icon_name, const char* fallback,
                       const char* tip, UiCommand action, bool on) {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x,
                                       field_pos.y + 3.f * scale_));
        const theme::Palette& pal = theme::palette();
        const theme::Rgb face = on ? pal.accent_soft : pal.face_light;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(face.r / 255.f, face.g / 255.f,
                                     face.b / 255.f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f * scale_, 3.f * scale_));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f * scale_);
        const Icon* ic = icon(icon_name);
        bool pressed;
        if (ic) {
          pressed = ImGui::ImageButton(id, reinterpret_cast<ImTextureID>(ic->texture),
                                       ImVec2(20.f * scale_, 20.f * scale_));
          draw_toolbar_button_bevel();
        } else {
          ImGui::PushID(id);
          pressed = classic_button(fallback, ImVec2(28.f * scale_, 26.f * scale_));
          ImGui::PopID();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (pressed) cmd = action;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tip);
      };
      if (state.has_chrome)
        extra("url_reader", "reader", "R", state.reader ? "Show the whole page (F9)" : "Reader mode (F9)",
              UiCommand::ToggleReader, state.reader);
      if (state.has_nav)
        extra("url_nav", "outline", "N", state.page_panel ? "Close page navigation" : "Open page navigation",
              UiCommand::TogglePage, state.page_panel);
      if (state.has_site)
        extra("url_sitemap", "sitemap", "S", state.sitemap ? "Close site map" : "Find and open site map",
              UiCommand::ToggleSitemap, state.sitemap);
      if (!url_own_row) {
        ImGui::SameLine(0.f, 6.f + right_pad);
        ImGui::SetCursorPosY(row_y);
      }
    };
    if (!url_own_row) location_field();
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
      toggle_button("tb_split", "split", "Independent tiles (Ctrl+\\)",
                    UiCommand::ToggleSplit,
                    state.split && !state.trail && !state.diff);
      toggle_button("tb_trail", "history", "Linked navigation trail",
                    UiCommand::ToggleTrail, state.trail);
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
      if (state.downloads_open) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.7f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.4f, 0.5f, 0.75f, 1.f));
      }
      tool_button("tb_downloads", "downloads", "Downloads",
                  UiCommand::ToggleDownloads, cmd);
      if (state.downloads_open) ImGui::PopStyleColor(2);
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
    if (url_own_row) {
      ImGui::SetCursorPos(ImVec2(6.f, 4.f + button_row_h));
      location_field();
    }
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
      if (classic_button(badge.c_str(), ImVec2(40.f * scale_, 0.f)))
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
      const bool go = classic_button("Go", ImVec2(go_w, 0.f));
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
    constexpr int kEditGroups[] = {5, 3, 4, 1, 1, 1, 4};
    const float button_step = 32.f * scale_ + 10.f;
    const float separator_step = 15.f;
    const float edit_width = std::max(1.f, vp->Size.x - 14.f);
    int edit_rows = 1;
    float row_used = 0.f;
    for (int count : kEditGroups) {
      const float group_width = count * button_step;
      if (row_used > 0.f &&
          (group_width > edit_width ||
           row_used + separator_step + group_width > edit_width)) {
        ++edit_rows;
        row_used = 0.f;
      } else if (row_used > 0.f) {
        row_used += separator_step;
      }
      if (group_width <= edit_width) {
        row_used += group_width;
      } else {
        for (int i = 0; i < count; ++i) {
          if (row_used > 0.f && row_used + button_step > edit_width) {
            ++edit_rows;
            row_used = 0.f;
          }
          row_used += button_step;
        }
      }
    }
    const float row_h = 32.f * scale_ + 6.f;
    const float edit_h = edit_rows * row_h + 10.f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, edit_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    if (ImGui::Begin("##toolbar_edit", nullptr, tb_flags)) {
      bool line_has_group = false;
      bool wrap_inside_group = false;
      bool line_has_item = false;
      auto start_group = [&](int count) {
        const float group_width = count * button_step;
        if (line_has_group &&
            ImGui::GetContentRegionAvail().x <
                separator_step + group_width) {
          ImGui::NewLine();
          line_has_group = false;
        }
        if (line_has_group) {
          toolbar_separator();
        }
        wrap_inside_group = group_width > edit_width;
        line_has_item = false;
        line_has_group = true;
      };
      auto before_group_item = [&] {
        if (wrap_inside_group && line_has_item &&
            ImGui::GetContentRegionAvail().x < button_step) {
          ImGui::NewLine();
          line_has_item = false;
        }
        line_has_item = true;
      };
      auto edit_button = [&](const char* id, const char* icon, const char* tip,
                             UiCommand action, bool enabled = true) {
        before_group_item();
        tool_button(id, icon, tip, action, cmd, enabled);
      };
      auto edit_menu_begin = [&](const char* id, const char* icon,
                                 const char* tip) {
        before_group_item();
        return tool_menu_begin(id, icon, tip);
      };

      // File
      start_group(5);
      edit_button("tb_new", "new", "New document (Ctrl+N)",
                  UiCommand::NewDocument);
      edit_button("tb_open", "open", "Open… (Ctrl+O)",
                  UiCommand::OpenDocument);
      edit_button("tb_save", "save", "Save (Ctrl+S)", UiCommand::Save);
      edit_button("tb_undo", "undo", "Undo (Ctrl+Z)", UiCommand::Undo);
      edit_button("tb_redo", "redo", "Redo (Ctrl+Y)", UiCommand::Redo);

      // Clipboard
      start_group(3);
      edit_button("tb_cut", "cut", "Cut (Ctrl+X)", UiCommand::Cut,
                  state.selection);
      edit_button("tb_copy", "copy", "Copy (Ctrl+C)", UiCommand::Copy,
                  state.selection);
      edit_button("tb_paste", "paste", "Paste (Ctrl+V)", UiCommand::Paste);

      // Common inline styles
      start_group(4);
      edit_button("tb_bold", "bold", "Bold (Ctrl+B)", UiCommand::Bold,
                  state.selection);
      edit_button("tb_italic", "italic", "Italic (Ctrl+I)", UiCommand::Italic,
                  state.selection);
      edit_button("tb_link", "link", "Link (Ctrl+Shift+K)", UiCommand::Link,
                  state.selection);

      // More format styles
      if (edit_menu_begin("tb_format", "format", "More formatting")) {
        menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
        menu_item_cmd("Strikethrough", UiCommand::Strike, cmd, state.selection,
                      "Ctrl+Shift+X");
        menu_item_cmd("Citation…", UiCommand::Cite, cmd, state.selection,
                      "Ctrl+Shift+C");
        tool_menu_end();
      }

      // Structure group
      start_group(1);
      if (edit_menu_begin("tb_structmenu", "structure", "Structure")) {
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
      start_group(1);
      if (edit_menu_begin("tb_lists", "lists", "Lists & blocks")) {
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
      start_group(1);
      if (edit_menu_begin("tb_insert", "insert", "Insert")) {
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

      start_group(4);
      edit_button("tb_find", "find", "Find (Ctrl+F)", UiCommand::Find);
      before_group_item();
      toggle_button("tb_split", "split", "Independent tiles (Ctrl+\\)",
                    UiCommand::ToggleSplit,
                    state.split && !state.trail && !state.diff);
      before_group_item();
      toggle_button("tb_trail", "history", "Linked navigation trail",
                    UiCommand::ToggleTrail, state.trail);
      before_group_item();
      toggle_button("tb_outline", "outline",
                    "Heading outline (Ctrl+Shift+O; Ctrl+Alt+O docks it)",
                    UiCommand::ToggleOutline, state.outline);
      if (edit_menu_begin("tb_viewmenu", "view", "View tools")) {
        view_tools_items(state, cmd, result.arg);
        tool_menu_end();
      }

    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    result.top_reserved += edit_h;
  }

  // ---- Document tab strip (browser-style) ----
  if (!state.tab_labels.empty()) {
    const float tab_h = 32.f * scale_;
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, tab_h));
    ImGuiWindowFlags tab_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f * scale_, 3.f * scale_));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * scale_, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.f * scale_, 4.f * scale_));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.f * scale_);
    if (ImGui::Begin("##tabs", nullptr, tab_flags)) {
      // Dragging reorders; the new order goes back as ReorderTabs. Ids are
      // the buffer ids, so a tab keeps its place when the order changes.
      std::vector<std::pair<ImGuiID, uint32_t>> tab_item_ids;
      const float strip_top = vp->Pos.y + result.top_reserved;
      const float button_w = 26.f * scale_;
      const float available = ImGui::GetContentRegionAvail().x;
      const float tabs_w = std::max(1.f, available - button_w - 4.f * scale_);
      ImGui::BeginChild("##tab-scroll-area", ImVec2(tabs_w, tab_h - 6.f * scale_), false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
          SDL_Texture* icon = i < static_cast<int>(state.tab_icons.size())
                                  ? state.tab_icons[static_cast<size_t>(i)] : nullptr;
          std::string id = icon ? "    " : "";
          id += state.tab_labels[static_cast<size_t>(i)];
          const uint32_t bid = i < static_cast<int>(state.tab_ids.size())
                                   ? state.tab_ids[static_cast<size_t>(i)]
                                   : static_cast<uint32_t>(i);
          id += "###tab";
          id += std::to_string(bid);
          if (ImGui::BeginTabItem(id.c_str(), &open, flags)) {
            ImGui::EndTabItem();
          }
          if (icon) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const float side = 16.f * scale_;
            const ImVec2 size = icon_size(icon, side);
            const float x = min.x + 8.f * scale_ + (side - size.x) * 0.5f;
            const float y = min.y + (max.y - min.y - size.y) * 0.5f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImGuiTabBar* bar = ImGui::GetCurrentTabBar();
            draw->PushClipRect(ImVec2(bar->ScrollingRectMinX, min.y),
                               ImVec2(bar->ScrollingRectMaxX, max.y), true);
            draw->AddImage(reinterpret_cast<ImTextureID>(icon), ImVec2(x, y),
                           ImVec2(x + size.x, y + size.y));
            draw->PopClipRect();
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
      ImGui::EndChild();
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
      const int button_colors = push_toolbar_button_style();
      if (classic_button("+##new-tab", ImVec2(button_w, tab_h - 6.f * scale_)))
        cmd = UiCommand::NewTab;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("New tab (Ctrl+T)");
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(button_colors);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    result.top_reserved += tab_h;
  }
}

UiFrameResult Ui::begin_frame(const UiState& state) {
  UiFrameResult result;
  if (!ready_) return result;
  render_prepared_ = false;

#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::new_frame();
#else
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
#endif
  // The SDL backend may have queued the physical mouse over a form obscured
  // by a native-only (or different) FloatWindow. Keep hidden widgets from
  // receiving hover, wheel and activation while the native frame is above.
  // Button/wheel events at such positions are also filtered at event entry.
  const bool pointer_hidden = pointer_obscured(
      state.pointer_x, state.pointer_y, state.pointer_native_top);
  if (pointer_hidden)
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  ImGui::NewFrame();
  // Stick is an *extra* pointer. Only drive ImGui from it while the stick is
  // actively used (axis motion or A/B held); otherwise leave the real mouse.
  if (state.stick_cursor && state.stick_active) {
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = pointer_hidden ? ImVec2(-FLT_MAX, -FLT_MAX)
                                 : ImVec2(state.stick_cx, state.stick_cy);
    io.MouseDown[0] = !pointer_hidden && state.stick_lmb;
    io.MouseDown[1] = !pointer_hidden && state.stick_rmb;
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
      menu_item_cmd("Publish locally", UiCommand::Publish, cmd,
                    state.can_publish);
      menu_item_cmd("Edit local copy", UiCommand::EditLocalCopy, cmd,
                    state.can_make_local_copy);
      menu_item_cmd("View original", UiCommand::ViewOriginal, cmd,
                    state.can_publish);
      menu_item_cmd("Export a copy…", UiCommand::SaveAs, cmd, true, "Ctrl+Shift+S");
      menu_item_cmd("Export as S-expression…", UiCommand::ExportSexpr, cmd, true);
      menu_item_cmd("Export as XML (experimental)…", UiCommand::ExportXml, cmd, true);
      menu_item_cmd("Export as Markdown…", UiCommand::ExportMarkdown, cmd, true);
      ImGui::Separator();
      menu_item_cmd("Reload", UiCommand::Reload, cmd, state.has_path, "Ctrl+R");
      menu_item_cmd("Force Reload", UiCommand::ForceReload, cmd, state.has_path,
                    "Ctrl+Shift+R");
      menu_item_cmd("Revert to Saved", UiCommand::RevertToSaved, cmd,
                    state.has_path && state.dirty);
      menu_item_cmd("Drafts", UiCommand::OpenDrafts, cmd);
      menu_item_cmd("Local documents", UiCommand::OpenStoredDocuments, cmd);
      menu_item_cmd("Document revisions…", UiCommand::ToggleRevisionsPanel,
                    cmd, state.can_show_revisions);
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
      menu_item_cmd("Link…", UiCommand::Link, cmd, state.selection, "Ctrl+Shift+K");
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
      if (ImGui::MenuItem("Independent tiles", "Ctrl+\\",
                          state.split && !state.trail && !state.diff))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Trail (linked navigation)", nullptr, state.trail))
        cmd = UiCommand::ToggleTrail;
      if (ImGui::MenuItem("Side by side", nullptr, !state.tiles_stacked))
        cmd = UiCommand::TilesColumns;
      if (ImGui::MenuItem("Stacked", nullptr, state.tiles_stacked))
        cmd = UiCommand::TilesRows;
      if (ImGui::MenuItem("Focus current section", "Ctrl+Alt+F", state.section_focus))
        cmd = UiCommand::ToggleSectionFocus;
      if (ImGui::MenuItem("Compare tiles", "Ctrl+Shift+D", state.diff,
                          state.can_diff))
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
    force_menu("Panels");
    if (ImGui::BeginMenu("Panels")) {
      if (ImGui::MenuItem("Keyboard Reference", "F1", state.help))
        cmd = UiCommand::ToggleHelp;
      ImGui::Separator();
      if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      if (ImGui::MenuItem("Backlinks", "Ctrl+Alt+B", state.backlinks))
        cmd = UiCommand::ToggleBacklinks;
      if (ImGui::MenuItem("Site Map", "Ctrl+Alt+M", state.sitemap))
        cmd = UiCommand::ToggleSitemap;
      if (ImGui::MenuItem("Page Navigation", "Ctrl+Alt+P", state.page_panel))
        cmd = UiCommand::TogglePage;
      if (ImGui::MenuItem("History", "Ctrl+Shift+H", state.history_open))
        cmd = UiCommand::ToggleHistory;
      if (ImGui::MenuItem("Downloads", nullptr, state.downloads_open))
        cmd = UiCommand::ToggleDownloads;
      if (ImGui::MenuItem("Subscriptions", nullptr, state.subscriptions_open))
        cmd = UiCommand::ToggleSubscriptions;
      if (ImGui::MenuItem("Style Sheets", nullptr, state.styles_open))
        cmd = UiCommand::ToggleStylesPanel;
      if (ImGui::MenuItem("Annotations", nullptr, state.annotations_panel_open))
        cmd = UiCommand::ToggleAnnotationsPanel;
      if (ImGui::MenuItem("Document revisions", nullptr,
                          state.revisions_panel_open,
                          state.can_show_revisions || state.revisions_panel_open))
        cmd = UiCommand::ToggleRevisionsPanel;
      ImGui::Separator();
      if (ImGui::MenuItem("Nostr Explorer", nullptr, state.nostr_explorer_open))
        cmd = UiCommand::ToggleNostrExplorer;
      if (ImGui::MenuItem("Lemmy Explorer", nullptr, state.lemmy_explorer_open,
                          state.lemmy_supported || state.lemmy_explorer_open))
        cmd = UiCommand::ToggleLemmyExplorer;
      if (ImGui::MenuItem("Nostr Accounts", nullptr, state.nostr_accounts_open))
        cmd = UiCommand::ToggleNostrAccounts;
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
      if (ImGui::MenuItem("Search Bar", "Ctrl+K", state.searchbar))
        cmd = state.searchbar ? UiCommand::ToggleSearchBar
                              : UiCommand::FocusSearchBar;
      if (ImGui::MenuItem("History", "Ctrl+Shift+H", state.history_open))
        cmd = UiCommand::ToggleHistory;
      ImGui::EndMenu();
    }
    force_menu("Bookmarks");
    if (ImGui::BeginMenu("Bookmarks")) {
      if (ImGui::MenuItem(state.current_subscribed ? "Unsubscribe current feed" :
                          "Subscribe to current feed", nullptr, false,
                          state.current_subscribed || state.current_subscribable)) {
        cmd = state.current_subscribed ? UiCommand::Unsubscribe : UiCommand::Subscribe;
        result.arg = state.subscription_address;
      }
      if (ImGui::MenuItem("Show Subscriptions…", nullptr, state.subscriptions_open))
        cmd = UiCommand::ToggleSubscriptions;
      ImGui::Separator();
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
          if (static_cast<size_t>(i) < state.bookmark_icons.size() && state.bookmark_icons[i]) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.bookmark_icons[i]),
                         icon_size(state.bookmark_icons[i], ImGui::GetTextLineHeight()));
            ImGui::SameLine();
          }
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

  // The divider owns the relationship between tiles. Its badge opens this
  // menu on left click; right-clicking anywhere on the divider does likewise.
  if (tile_menu_open_) {
    ImGui::SetNextWindowPos(ImVec2(tile_menu_x_, tile_menu_y_),
                            ImGuiCond_Appearing);
    ImGui::OpenPopup("##tile_menu");
    tile_menu_open_ = false;
  }
  if (ImGui::BeginPopup("##tile_menu")) {
    if (ImGui::MenuItem("Independent tiles", nullptr,
                        state.split && !state.trail && !state.diff))
      cmd = UiCommand::TilesIndependent;
    if (ImGui::MenuItem("Trail (linked navigation)", nullptr, state.trail))
      cmd = UiCommand::TilesTrail;
    if (ImGui::MenuItem("Compare tiles", nullptr, state.diff, state.can_diff))
      cmd = UiCommand::TilesCompare;
    ImGui::Separator();
    if (ImGui::MenuItem("Side by side", nullptr, !state.tiles_stacked))
      cmd = UiCommand::TilesColumns;
    if (ImGui::MenuItem("Stacked", nullptr, state.tiles_stacked))
      cmd = UiCommand::TilesRows;
    ImGui::EndPopup();
  }

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
    if (state.context_can_reply) {
      menu_item_cmd("Reply…", UiCommand::OpenReply, cmd);
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
      menu_item_cmd("Link…", UiCommand::Link, cmd, state.selection, "Ctrl+Shift+K");
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
    if (state.has_path) {
      if (state.selection)
        menu_item_cmd("Bookmark Selection", UiCommand::AddBookmark, cmd);
      menu_item_cmd(state.page_bookmarked ? "Remove Page Bookmark"
                                         : "Bookmark This Page",
                    state.page_bookmarked ? UiCommand::RemoveBookmark
                                          : UiCommand::BookmarkPage, cmd);
      menu_item_cmd("Bookmark Reading Position", UiCommand::BookmarkPosition,
                    cmd);
      ImGui::Separator();
    }
    if (state.selection)
      menu_item_cmd("Highlight Selection", UiCommand::AddAnnotationHighlight, cmd);
    if (state.selection)
      menu_item_cmd("Annotate Selection…", UiCommand::ToggleAnnotationsPanel,
                    cmd, !state.annotations_panel_open);
    if (state.selection) ImGui::Separator();
    menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
    if (!state.browse)
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
    ImGui::EndPopup();
  }

  // ---- Reply composer: custom floating/dockable frame underneath, ImGui
  // text widget and Markdown tools above it. Nothing here sends a request.
  if (state.reply_open) {
    if (reply_generation_seen_ != state.reply_generation) {
      reply_generation_seen_ = state.reply_generation;
      std::snprintf(reply_input_, sizeof(reply_input_), "%s",
                    state.reply_text.c_str());
      reply_selection_ = {};
      reply_focus_ = true;
      reply_restore_selection_ = false;
    }
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(state.reply_x + 4),
                                   static_cast<float>(state.reply_y + 4)));
    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(std::max(40, state.reply_w - 8)),
        static_cast<float>(std::max(40, state.reply_h - 8))));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(7.f * scale_, 6.f * scale_));
    if (ImGui::Begin("##reply_editor", nullptr, flags)) {
      auto muted_wrapped = [](const char* label) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", label);
        ImGui::PopStyleColor();
      };
      std::string title = "Reply to ";
      title += state.reply_author.empty() ? "comment" : "@" + state.reply_author;
      ImGui::TextWrapped("%s", title.c_str());
      if (!state.reply_label.empty() &&
          ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Comment: %s", state.reply_label.c_str());
      muted_wrapped("Markdown · preview only, no account connected");
      const char* format_before = nullptr;
      const char* format_after = nullptr;
      auto format = [&](const char* label, const char* before,
                        const char* after) {
        if (classic_button(label)) {
          format_before = before;
          format_after = after;
        }
      };
      auto next_format_on_same_line = [&](const char* label) {
        const float next_width = ImGui::CalcTextSize(label).x +
                                 2.f * ImGui::GetStyle().FramePadding.x;
        const float right = ImGui::GetWindowPos().x +
                            ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
                next_width < right)
          ImGui::SameLine();
      };
      format("Bold", "**", "**");
      next_format_on_same_line("Italic");
      format("Italic", "*", "*");
      next_format_on_same_line("Code");
      format("Code", "`", "`");
      next_format_on_same_line("Link");
      format("Link", "[", "](https://)");
      next_format_on_same_line("Quote");
      format("Quote", "> ", "");

      if (reply_focus_) {
        ImGui::SetKeyboardFocusHere();
        reply_focus_ = false;
      }
      const auto track_selection = [](ImGuiInputTextCallbackData* cb) -> int {
        auto* ui = static_cast<Ui*>(cb->UserData);
        if (ui->reply_restore_selection_) {
          cb->SelectionStart = ui->reply_selection_.begin;
          cb->SelectionEnd = ui->reply_selection_.end;
          cb->CursorPos = ui->reply_selection_.end;
          ui->reply_restore_selection_ = false;
        }
        ui->reply_selection_.begin = cb->SelectionStart;
        ui->reply_selection_.end = cb->SelectionEnd;
        return 0;
      };
      // Reserve room for the Send control and a wrapped status line. Docking
      // to a narrow side column may turn the tools and text into more lines;
      // use the remaining space instead of assuming a one-row toolbar.
      const float footer_h = ImGui::GetFrameHeightWithSpacing() +
                             3.f * ImGui::GetTextLineHeightWithSpacing();
      const float edit_h = std::max(48.f * scale_,
                                    ImGui::GetContentRegionAvail().y - footer_h);
      if (ImGui::InputTextMultiline(
              "##reply_text", reply_input_, sizeof(reply_input_),
              ImVec2(-1.f, edit_h), ImGuiInputTextFlags_CallbackAlways,
              track_selection, this)) {
        cmd = UiCommand::EditReply;
        result.arg = reply_input_;
      }
      // Apply formatting *after* InputText has finished its frame, so its
      // retained editor buffer cannot overwrite the newly inserted markup.
      if (format_before) {
        const auto wrapped = wrap_reply_markup(
            reply_input_, static_cast<size_t>(std::max(0, reply_selection_.begin)),
            static_cast<size_t>(std::max(0, reply_selection_.end)),
            format_before, format_after);
        if (wrapped.text.size() < sizeof(reply_input_)) {
          std::memcpy(reply_input_, wrapped.text.c_str(), wrapped.text.size() + 1);
          reply_selection_.begin = static_cast<int>(wrapped.selection_begin);
          reply_selection_.end = static_cast<int>(wrapped.selection_end);
          reply_restore_selection_ = true;
          reply_focus_ = true;
          cmd = UiCommand::EditReply;
          result.arg = reply_input_;
        }
      }
      const bool can_send = reply_input_[0] != '\0';
      ImGui::BeginDisabled(!can_send);
      if (classic_button("Send reply")) {
        cmd = UiCommand::SendReply;
        result.arg = reply_input_;
      }
      ImGui::EndDisabled();
      muted_wrapped("Mockup: Send does not post anything");
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // ---- Preferences ----
  if (state.preferences_open) {
    bool open = true;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.f * scale_, 560.f * scale_),
                            ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Preferences", &open)) {
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
#ifdef __ANDROID__
      ImGui::SeparatorText("Updates");
      toggle("Automatic update checks", state.auto_updates,
             UiCommand::ToggleAutoUpdates,
             "Notify when a new Android version is available");
#endif
      ImGui::SeparatorText("Interface");
      static constexpr float scales[] = {
          0.75f, 0.9f, 1.f, 1.1f, 1.25f, 1.4f, 1.6f, 1.8f, 2.f};
      static constexpr const char* labels[] = {
          "75%", "90%", "100%", "110%", "125%", "140%", "160%",
          "180%", "200%"};
      int selected_scale = 0;
      for (int i = 1; i < IM_ARRAYSIZE(scales); ++i)
        if (std::abs(state.ui_scale - scales[i]) <
            std::abs(state.ui_scale - scales[selected_scale]))
          selected_scale = i;
      if (ImGui::BeginCombo("UI scale", labels[selected_scale])) {
        for (int i = 0; i < IM_ARRAYSIZE(scales); ++i) {
          if (ImGui::Selectable(labels[i], i == selected_scale)) {
            cmd = UiCommand::SetUiScale;
            result.arg = std::to_string(scales[i]);
          }
        }
        ImGui::EndCombo();
      }
      ImGui::TextDisabled("32 px icons at the platform default; text scales too.");
#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
      ImGui::SeparatorText("Reddit API (optional)");
      if (!reddit_credentials_loaded_) {
        const RedditCredentials c = load_reddit_credentials();
        std::snprintf(reddit_client_id_, sizeof(reddit_client_id_), "%s",
                      c.client_id.c_str());
        std::snprintf(reddit_client_secret_, sizeof(reddit_client_secret_), "%s",
                      c.client_secret.c_str());
        reddit_credentials_loaded_ = true;
      }
      ImGui::TextWrapped("Reddit may block anonymous requests (HTTP 403). "
                         "Approved Data API clients can use app-only OAuth credentials.");
      if (reddit_credentials_from_environment()) {
        ImGui::TextDisabled("Environment variables override saved credentials.");
      } else if (load_reddit_credentials().complete()) {
        ImGui::TextDisabled("Saved credentials configured locally.");
      } else {
        ImGui::TextDisabled("No local credentials configured.");
      }
      ImGui::InputText("Client ID", reddit_client_id_, sizeof(reddit_client_id_));
      ImGui::InputText("Client secret", reddit_client_secret_,
                       sizeof(reddit_client_secret_),
                       reddit_secret_visible_ ? 0 : ImGuiInputTextFlags_Password);
      ImGui::SameLine();
      ImGui::Checkbox("Show", &reddit_secret_visible_);
      if (no_save()) ImGui::TextDisabled("Saving is disabled for this session.");
      const bool complete = reddit_client_id_[0] && reddit_client_secret_[0];
      ImGui::BeginDisabled(!complete || no_save());
      if (classic_button("Save credentials")) {
        cmd = UiCommand::SaveRedditCredentials;
        result.arg = std::string(reddit_client_id_) + "\n" +
                     reddit_client_secret_;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(no_save());
      if (classic_button("Clear saved credentials")) {
        cmd = UiCommand::ClearRedditCredentials;
        reddit_client_id_[0] = reddit_client_secret_[0] = '\0';
      }
      ImGui::EndDisabled();
      if (ImGui::TreeNode("How to obtain credentials")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 455.f * scale_);
        ImGui::TextWrapped("1. Sign in to Reddit and request Data API access "
                           "for Xanatoo (a read-only desktop client). "
                           "Reddit requires approval for external apps.");
        ImGui::TextWrapped("2. If approved, create a personal 'script' OAuth "
                           "app (not 'installed') in app settings. Copy the ID "
                           "beneath its name and the client secret.");
        ImGui::TextWrapped("3. Paste both values above and click Save. "
                           "You do not need to enter a Reddit password here.");
        ImGui::TextWrapped("Devvit apps do not provide credentials for "
                           "external clients. Reddit plans to end legacy "
                           "public API access in March 2027.");
        ImGui::PopTextWrapPos();
        if (classic_button("Request API access"))
          xanatoo::sdl::open_url(
              "https://support.reddithelp.com/hc/en-us/requests/new?"
              "ticket_form_id=14868593862164");
        ImGui::SameLine();
        if (classic_button("OAuth app settings"))
          xanatoo::sdl::open_url("https://www.reddit.com/prefs/apps");
        ImGui::TreePop();
      }
#endif
      ImGui::Spacing();
#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
      ImGui::TextDisabled("Display options: view.conf; Reddit keys: reddit.conf.");
#else
      ImGui::TextDisabled("Saved to view.conf; layout options are in View.");
#endif
      if (classic_button("Close")) open = false;
    }
    ImGui::End();
    if (!open) cmd = UiCommand::TogglePreferences;
  } else {
    reddit_credentials_loaded_ = false;
    reddit_secret_visible_ = false;
  }

  // ---- Style sheet manager / editor, embedded in its native FloatWindow ----
  if (state.styles_open) {
    if (state.style_save_generation != style_save_seen_) {
      style_save_seen_ = state.style_save_generation;
      style_editor_dirty_ = false;
      style_editor_loaded_.clear();
    }
    ImGui::SetNextWindowPos(ImVec2(float(state.styles_x + 3),
                                  float(state.styles_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(float(std::max(1, state.styles_w - 6)),
                                   float(std::max(1, state.styles_h - 6))));
    constexpr ImGuiWindowFlags style_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("##styles_panel_body", nullptr, style_flags)) {
      ImGui::TextWrapped("Reader themes / personal .xss files / automatic assignments");
      ImGui::Separator();
      const float available_width = ImGui::GetContentRegionAvail().x;
      const bool narrow_panel = available_width < 520.f;
      const float list_width = narrow_panel
          ? available_width
          : std::clamp(available_width * 0.31f, 145.f, 260.f);
      // A side-docked panel may not have room for two columns. Put the
      // theme list above the editor instead of squeezing the font controls.
      ImGui::BeginChild("##styles_list", ImVec2(list_width, narrow_panel ? 145.f : 0), true);
      ImGui::TextDisabled("Themes");
      ImGui::Separator();
      if (style_editor_sheet_.empty()) style_editor_sheet_ = state.style;
      for (const StyleEntry& entry : state.style_entries) {
        ImGui::PushID(entry.name.c_str());
        const bool selected = style_editor_sheet_ == entry.name;
        const bool active = state.style == entry.name;
        std::string label = (active ? "● " : "  ") + entry.title;
        if (ImGui::Selectable(label.c_str(), selected)) {
          if (!style_editor_dirty_) {
            style_editor_sheet_ = entry.name;
            style_editor_[0] = '\0';  // file contents loaded below
            style_delete_confirm_ = false;
            if (!active) {
              cmd = UiCommand::SelectStyle;
              result.arg = entry.name;
            }
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s%s", entry.name.c_str(),
                            active ? " (currently applied)" : "");
        ImGui::PopID();
      }
      if (style_editor_dirty_)
        ImGui::TextWrapped("Save or discard edits before switching themes.");
      ImGui::Separator();
      if (classic_button("Reload")) cmd = UiCommand::ReloadStyles;
      ImGui::TextDisabled("Default: %s", state.default_style.c_str());
      ImGui::EndChild();
      if (!narrow_panel) ImGui::SameLine();
      ImGui::BeginGroup();
      const StyleEntry* chosen = nullptr;
      for (const StyleEntry& entry : state.style_entries)
        if (entry.name == style_editor_sheet_) { chosen = &entry; break; }
      if (!chosen && !state.style_entries.empty()) {
        // A failed Create command must not strand the editor on a missing
        // name. On success the newly created sheet appears in this list.
        style_editor_sheet_ = state.style;
        for (const StyleEntry& entry : state.style_entries)
          if (entry.name == style_editor_sheet_) { chosen = &entry; break; }
        if (!chosen) {
          chosen = &state.style_entries.front();
          style_editor_sheet_ = chosen->name;
        }
      }
      if (chosen) {
        const fs::path user_styles = fs::path(user_data_dir()) / "styles";
        std::error_code ec;
        const bool writable = !chosen->path.empty() &&
            fs::weakly_canonical(fs::path(chosen->path).parent_path(), ec) ==
            fs::weakly_canonical(user_styles, ec);
        // Editor changes are held in the ImGui state until explicitly saved;
        // switching themes is disabled while there are unsaved changes.
        if (style_editor_sheet_ != style_editor_loaded_) {
          style_editor_loaded_ = style_editor_sheet_;
          std::ifstream file(chosen->path);
          std::ostringstream contents;
          if (file) contents << file.rdbuf();
          const std::string source = contents.str();
          std::snprintf(style_editor_, sizeof(style_editor_), "%s", source.c_str());
          style_editor_dirty_ = false;
          DocStyle resolved;
          if (load_style(chosen->name, resolved)) {
            auto first = [](const std::vector<std::string>& names,
                            const char* fallback) {
              return names.empty() ? std::string(fallback) : names.front();
            };
            std::snprintf(style_body_font_, sizeof(style_body_font_), "%s",
                          first(resolved.fonts.body, "sans-serif").c_str());
            std::snprintf(style_heading_font_, sizeof(style_heading_font_), "%s",
                          first(resolved.fonts.heading, "sans-serif").c_str());
            std::snprintf(style_code_font_, sizeof(style_code_font_), "%s",
                          first(resolved.fonts.code, "monospace").c_str());
          }
        }
        ImGui::Text("%s", chosen->title.c_str());
        ImGui::SeparatorText("Create / duplicate");
        ImGui::BeginDisabled(style_editor_dirty_);
        ImGui::SetNextItemWidth(narrow_panel ? -FLT_MIN :
            std::min(190.f, ImGui::GetContentRegionAvail().x * 0.60f));
        ImGui::InputTextWithHint("##new_style", "my-theme", style_new_name_,
                                 sizeof(style_new_name_));
        same_line_if_fits("Create from selected");
        if (classic_button("Create from selected")) {
          std::string name = style_new_name_;
          if (name.empty()) name = chosen->name + "-custom";
          cmd = UiCommand::StyleCreate;
          result.arg = name + "\n" + chosen->name;
          style_editor_sheet_ = name;
          style_editor_loaded_.clear();
          style_editor_dirty_ = false;
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Source editor");
        if (writable) {
          ImGui::TextDisabled("%s", chosen->path.c_str());
          if (ImGui::InputTextMultiline("##style_source", style_editor_,
                                        sizeof(style_editor_),
                                        ImVec2(-FLT_MIN, std::max(115.f, ImGui::GetContentRegionAvail().y * 0.26f)),
                                        ImGuiInputTextFlags_AllowTabInput))
            style_editor_dirty_ = true;
          if (classic_button(style_editor_dirty_ ? "Save changes *" : "Save")) {
            cmd = UiCommand::StyleSave;
            result.arg = chosen->name + "\n" + style_editor_;
          }
          same_line_if_fits("Discard changes");
          if (classic_button("Discard changes")) {
            style_editor_dirty_ = false;
            style_editor_loaded_.clear();
          }
          same_line_if_fits(style_delete_confirm_ ? "Confirm delete" : "Delete...");
          if (style_delete_confirm_) {
            if (classic_button("Confirm delete")) {
              cmd = UiCommand::StyleDelete;
              result.arg = chosen->name;
              style_editor_sheet_ = state.default_style;
              style_editor_loaded_.clear();
              style_editor_dirty_ = false;
              style_delete_confirm_ = false;
            }
          } else if (classic_button("Delete...")) {
            style_delete_confirm_ = true;
          }
        } else {
          ImGui::TextWrapped("Bundled preset (read-only). Create a personal copy above to edit it.");
          ImGui::TextDisabled("Your files are saved under XDG_DATA_HOME/xanatoo/styles.");
        }
        ImGui::SeparatorText("Font selection");
        if (writable) {
          // List the actual installed FontConfig families, not a guessed
          // handful of presets that might silently resolve to other faces.
          const auto& faces = fontconfig_list_families();
          ImGui::SetNextItemWidth(-FLT_MIN);
          ImGui::InputTextWithHint("##font_filter", "Search fonts...",
                                   style_font_filter_, sizeof(style_font_filter_));
          auto font_field = [&](const char* label, char* value, size_t cap) {
            ImGui::PushID(label);
            // InputText renders a visible label *after* the input. With a
            // full-width field that label ends up outside the panel, so put
            // it on its own line and use hidden IDs for both controls.
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##family", value, cap);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##installed_fonts", "Browse fonts...")) {
              std::string filter(style_font_filter_);
              std::transform(filter.begin(), filter.end(), filter.begin(),
                             [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
              for (const std::string& face : faces) {
                if (!filter.empty()) {
                  std::string lower = face;
                  std::transform(lower.begin(), lower.end(), lower.begin(),
                                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                  if (lower.find(filter) == std::string::npos) continue;
                }
                if (ImGui::Selectable(face.c_str(), std::string_view(value) == face))
                  std::snprintf(value, cap, "%s", face.c_str());
                if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s", face.c_str());
              }
              ImGui::EndCombo();
            }
            ImGui::PopID();
          };
          font_field("Body", style_body_font_, sizeof(style_body_font_));
          font_field("Headings", style_heading_font_, sizeof(style_heading_font_));
          font_field("Code", style_code_font_, sizeof(style_code_font_));
          if (classic_button("Insert font overrides")) {
            auto quoted = [](const char* in) {
              std::string safe;
              for (char c : std::string_view(in)) {
                if (c == '"' || c == '\\') safe.push_back('\\');
                if (c != '\n' && c != '\r') safe.push_back(c);
              }
              return "\"" + safe + "\"";
            };
            const std::string rule =
                "\n(fonts (body " + quoted(style_body_font_) + ")" +
                " (heading " + quoted(style_heading_font_) + ")" +
                " (code " + quoted(style_code_font_) + "))\n";
            std::string source(style_editor_);
            // Parenthesized sheets must keep the extra rule inside the
            // (stylesheet ...) wrapper. Our new templates are unwrapped.
            const auto forms = SExprParser(source).parse_all();
            if (forms.size() == 1 && is_form(forms.front(), "stylesheet")) {
              const auto close = source.find_last_of(')');
              if (close != std::string::npos) source.insert(close, rule);
            } else source += rule;
            if (source.size() < sizeof(style_editor_)) {
              std::snprintf(style_editor_, sizeof(style_editor_), "%s", source.c_str());
              style_editor_dirty_ = true;
            }
          }
        } else {
          ImGui::TextDisabled("Duplicate this preset to customize its fonts.");
        }
        ImGui::SeparatorText("Automatic styles");
        for (const std::string& key : state.style_pin_candidates) {
          ImGui::PushID(key.c_str());
          const auto it = state.style_pins.find(key);
          ImGui::TextWrapped("%s: %s", key.c_str(),
                            it == state.style_pins.end() ? "(not assigned)" : it->second.c_str());
          same_line_if_fits("Pin here");
          if (classic_button("Pin here")) {
            cmd = UiCommand::StylePin;
            result.arg = key + "\n" + chosen->name;
          }
          if (it != state.style_pins.end()) {
            same_line_if_fits("Unpin");
            if (classic_button("Unpin")) {
              cmd = UiCommand::StyleUnpin;
              result.arg = key;
            }
          }
          ImGui::PopID();
        }
        if (state.style_pin_candidates.empty())
          ImGui::TextDisabled("Open a web domain or service to assign its style.");
        ImGui::SetNextItemWidth(narrow_panel ? -FLT_MIN : -105.f);
        ImGui::InputTextWithHint("##manual_style_pin", "domain:example.org or service:lemmy",
                                 style_pin_input_, sizeof(style_pin_input_));
        same_line_if_fits("Pin target");
        if (classic_button("Pin target")) {
          cmd = UiCommand::StylePin;
          result.arg = std::string(style_pin_input_) + "\n" + chosen->name;
        }
        if (ImGui::TreeNode("All saved assignments")) {
          for (const auto& [key, name] : state.style_pins) {
            ImGui::PushID(key.c_str());
            ImGui::Text("%s = %s", key.c_str(), name.c_str());
            same_line_if_fits("Remove");
            if (classic_button("Remove")) {
              cmd = UiCommand::StyleUnpin;
              result.arg = key;
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
      }
      ImGui::EndGroup();
    }
    ImGui::End();
  } else {
    style_editor_sheet_.clear();
    style_editor_loaded_.clear();
    style_editor_dirty_ = false;
  }

  // ---- Bookmarks manager panel ----
  if (state.bookmarks_open) {
    // Like Reply / Annotations, this is an editor *inside* the native
    // FloatWindow. It must never have an independent ImGui title or geometry.
    ImGui::SetNextWindowPos(ImVec2(float(state.bookmarks_x + 3),
                                  float(state.bookmarks_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(float(std::max(1, state.bookmarks_w - 6)),
                                   float(std::max(1, state.bookmarks_h - 6))));
    constexpr ImGuiWindowFlags bookmarks_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("##bookmarks_panel_body", nullptr, bookmarks_flags)) {
      ImGui::TextDisabled("Persistent bookmarks (state dir: bookmarks)");
      ImGui::Separator();
      if (classic_button(state.current_bookmarked ? "Remove current page"
                        : state.selection        ? "Bookmark selection"
                                                 : "Bookmark current page")) {
        cmd = state.current_bookmarked ? UiCommand::RemoveBookmark
                                       : UiCommand::AddBookmark;
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
          if (static_cast<size_t>(i) < state.bookmark_icons.size() && state.bookmark_icons[i]) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.bookmark_icons[i]),
                         icon_size(state.bookmark_icons[i], ImGui::GetTextLineHeight()));
            ImGui::SameLine();
          }
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
  }

  // ---- Subscriptions: changing remote feeds (distinct from static bookmarks) ----
  if (state.subscriptions_open) {
    ImGui::SetNextWindowPos(ImVec2(float(state.subscriptions_x + 3),
                                  float(state.subscriptions_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(float(std::max(1, state.subscriptions_w - 6)),
                                   float(std::max(1, state.subscriptions_h - 6))));
    constexpr ImGuiWindowFlags subscriptions_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("##subscriptions_panel_body", nullptr, subscriptions_flags)) {
      int unread_total = 0;
      for (const auto& s : state.subscriptions)
        unread_total += std::min<uint32_t>(s.unread, 9999);
      ImGui::Text("%zu watched sources", state.subscriptions.size());
      if (unread_total > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.18f, 0.40f, 0.75f, 1.f),
                           "  %d new", unread_total);
      }
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("Lemmy communities/people, Nostr authors/groups, RSS/Atom and more");
      ImGui::PopStyleColor();
      ImGui::Separator();
      if (state.current_subscribable && !state.current_subscribed &&
          classic_button("Subscribe to current page")) {
        cmd = UiCommand::Subscribe;
        result.arg = state.subscription_address;
      }
      if (state.current_subscribed && classic_button("Unsubscribe current page")) {
        cmd = UiCommand::Unsubscribe;
        result.arg = state.subscription_address;
      }
      ImGui::TextUnformatted("Add a feed or community address:");
      ImGui::SetNextItemWidth(-1.f);
      ImGui::InputText("##subscription_url", subscription_input_,
                       sizeof(subscription_input_));
      if (classic_button("Add subscription")) {
        cmd = UiCommand::Subscribe;
        result.arg = subscription_input_;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("For example: lemmy:lemmy.world/c/technology,\n"
                          "nostr:npub1..., or https://example.org/feed.xml");
      same_line_if_fits("Refresh all");
      if (classic_button("Refresh all")) cmd = UiCommand::SubscriptionRefresh;
      ImGui::Separator();
      if (state.subscriptions.empty()) {
        ImGui::TextWrapped("No subscriptions yet. Add a source above or navigate "
                           "to a Lemmy community or Nostr profile and subscribe.");
      }
      for (const auto& s : state.subscriptions) {
        ImGui::PushID(s.address.c_str());
        const std::string name = s.title.empty() ? s.address : s.title;
        if (s.unread) {
          ImGui::TextColored(ImVec4(0.17f, 0.41f, 0.76f, 1.f),
                             "● %u new", s.unread);
          ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", name.c_str());
        if (name != s.address) {
          ImGui::PushStyleColor(ImGuiCol_Text,
                                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          ImGui::TextWrapped("%s", s.address.c_str());
          ImGui::PopStyleColor();
        }
        if (s.pending) {
          ImGui::TextDisabled("Checking for new content…");
        } else if (!s.error.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.27f, 0.14f, 1.f));
          ImGui::TextWrapped("Check failed: %s", s.error.c_str());
          ImGui::PopStyleColor();
        } else if (!s.initialized) {
          ImGui::TextDisabled("Waiting for first successful check");
        } else {
          const auto now = static_cast<int64_t>(std::time(nullptr));
          const int64_t mins = std::max<int64_t>(0, (now - s.checked_at) / 60);
          if (mins < 60) ImGui::TextDisabled("Last checked %lld min ago",
                                            static_cast<long long>(mins));
          else ImGui::TextDisabled("Last checked %lld h ago",
                                   static_cast<long long>(mins / 60));
        }
        if (classic_button("Open")) {
          cmd = UiCommand::SubscriptionOpen;
          result.arg = s.address;
        }
        same_line_if_fits("Refresh");
        if (classic_button("Refresh")) {
          cmd = UiCommand::SubscriptionRefresh;
          result.arg = s.address;
        }
        if (s.unread) {
          same_line_if_fits("Mark read");
          if (classic_button("Mark read")) {
            cmd = UiCommand::SubscriptionMarkRead;
            result.arg = s.address;
          }
        }
        same_line_if_fits("Remove…");
        if (subscription_remove_confirm_ == s.address) {
          if (classic_button("Confirm removal")) {
            cmd = UiCommand::Unsubscribe;
            result.arg = s.address;
            subscription_remove_confirm_.clear();
          }
        } else if (classic_button("Remove…")) {
          subscription_remove_confirm_ = s.address;
        }
        static constexpr int intervals[] = {5, 15, 30, 60, 180, 720, 1440};
        ImGui::SetNextItemWidth(-1.f);
        std::string frequency = "Every " + std::to_string(s.interval_minutes) + " min";
        if (ImGui::BeginCombo("##subscription_frequency", frequency.c_str())) {
          for (int mins : intervals) {
            std::string option = "Every " + std::to_string(mins) + " min";
            if (ImGui::Selectable(option.c_str(), mins == s.interval_minutes)) {
              cmd = UiCommand::SubscriptionInterval;
              result.arg = s.address + "\n" + std::to_string(mins);
            }
          }
          ImGui::EndCombo();
        }
        ImGui::Separator();
        ImGui::PopID();
      }
    }
    ImGui::End();
  }

  // ---- Independent annotation layers (source documents remain untouched) ----
  if (state.annotations_panel_open) {
    if (annotation_note_created_seen_ != state.annotation_note_created_gen) {
      annotation_note_created_seen_ = state.annotation_note_created_gen;
      annotation_new_note_[0] = '\0';
    }
    // The title, close/maximize buttons, docking, resize and stacking belong
    // to FloatWindow::Annotations. ImGui provides only the form controls in
    // that native panel's client area (as in the Reply composer).
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(state.annotation_x + 3),
                                   static_cast<float>(state.annotation_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(std::max(1, state.annotation_w - 6)),
        static_cast<float>(std::max(1, state.annotation_h - 6))));
    constexpr ImGuiWindowFlags annotation_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("##annotation_panel_body", nullptr, annotation_flags)) {
      if (classic_button("New personal layer"))
        cmd = UiCommand::NewAnnotationLayer;
      ImGui::SameLine();
      if (classic_button("Highlight selection"))
        cmd = UiCommand::AddAnnotationHighlight;
      if (!state.selection && ImGui::IsItemHovered())
        ImGui::SetTooltip("Select text in a document first");
      ImGui::Separator();
      if (state.annotation_layers.empty()) {
        ImGui::TextWrapped("No annotation layers yet. Create one to start highlighting.");
      }
      for (const auto& layer : state.annotation_layers) {
        ImGui::PushID(layer.id.c_str());
        bool visible = layer.visible;
        if (ImGui::Checkbox("##visible", &visible)) {
          cmd = UiCommand::ToggleAnnotationLayer;
          result.arg = layer.id;
        }
        ImGui::SameLine();
        const std::string label = layer.name + " (" + layer.owner + ", " +
                                  std::to_string(layer.count) + ")";
        if (layer.owner == "local") {
          if (ImGui::RadioButton(label.c_str(), layer.active)) {
            cmd = UiCommand::SelectAnnotationLayer;
            result.arg = layer.id;
          }
        } else {
          ImGui::TextUnformatted(label.c_str());
        }
        ImGui::PopID();
      }
      ImGui::TextDisabled("Checkbox: visibility  ·  radio: local edit target");
      ImGui::Separator();
      ImGui::TextUnformatted("Attach a note to selected text");
      ImGui::InputTextMultiline("##new_annotation_note", annotation_new_note_,
                                sizeof(annotation_new_note_), ImVec2(-1.f, 66.f));
      ImGui::BeginDisabled(!state.selection || annotation_new_note_[0] == '\0');
      if (classic_button("Attach note")) {
        cmd = UiCommand::AddAnnotationNote;
        result.arg = annotation_new_note_;
      }
      ImGui::EndDisabled();
      if (!state.selection) {
        ImGui::SameLine();
        ImGui::TextDisabled("Select a passage first");
      }
      ImGui::Separator();
      ImGui::TextUnformatted("Annotations on this page");
      if (state.annotations.empty())
        ImGui::TextDisabled("None in visible layers");
      const UiState::AnnotationItem* selected = nullptr;
      for (const auto& item : state.annotations) {
        ImGui::PushID(item.id.c_str());
        const char* label = item.kind == "note" ? "Note" : "Highlight";
        if (ImGui::Selectable(label, item.selected)) {
          cmd = UiCommand::SelectAnnotation;
          result.arg = item.id;
        }
        if (item.selected) selected = &item;
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", item.owner.c_str(),
                            item.resolved ? "" : " (anchor not found)");
        if (!item.body.empty()) {
          ImGui::Indent();
          ImGui::TextWrapped("%s", item.body.c_str());
          ImGui::Unindent();
        }
        ImGui::PopID();
      }
      const std::string selected_id = selected ? selected->id : std::string();
      const std::string selected_body = selected ? selected->body : std::string();
      if (selected_id != annotation_edit_synced_id_ ||
          selected_body != annotation_edit_synced_body_) {
        annotation_edit_synced_id_ = selected_id;
        annotation_edit_synced_body_ = selected_body;
        std::snprintf(annotation_edit_note_, sizeof(annotation_edit_note_),
                      "%s", selected_body.c_str());
      }
      bool pending_note_changes = false;
      if (selected) {
        ImGui::Separator();
        ImGui::Text("Selected: %s", selected->kind.c_str());
        if (selected->owner == "local") {
          if (selected->kind == "note") {
            const bool too_long = selected->body.size() >=
                                  sizeof(annotation_edit_note_) ||
                                  selected->body.find('\0') != std::string::npos;
            const bool changed = ImGui::InputTextMultiline(
                "##edit_annotation_note", annotation_edit_note_,
                sizeof(annotation_edit_note_), ImVec2(-1.f, 88.f));
            if (changed) annotation_note_last_edit_ = ImGui::GetTime();
            const bool pending = selected->body != annotation_edit_note_;
            pending_note_changes = pending;
            // Commit after typing pauses or the editor loses focus. Each
            // commit creates a layer revision, never a source document edit.
            if (!too_long && pending &&
                (ImGui::IsItemDeactivatedAfterEdit() ||
                 (annotation_note_last_edit_ > 0.0 &&
                  ImGui::GetTime() - annotation_note_last_edit_ > 0.8))) {
              cmd = UiCommand::UpdateAnnotationNote;
              result.arg = annotation_edit_note_;
              annotation_note_last_edit_ = 0.0;
            }
            if (too_long) ImGui::TextDisabled("Note too long to edit here");
            ImGui::BeginDisabled(too_long || !pending);
            if (classic_button("Save note")) {
              cmd = UiCommand::UpdateAnnotationNote;
              result.arg = annotation_edit_note_;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
          }
          if (classic_button("Remove annotation")) {
            cmd = UiCommand::RemoveAnnotation;
            result.arg = selected->id;
          }
          ImGui::TextDisabled("Previous versions remain in layer history");
        } else {
          ImGui::TextDisabled("Other people's layers are read-only");
        }
      }
      if (!state.annotation_history_layer.empty() &&
          ImGui::CollapsingHeader("Local layer history")) {
        ImGui::TextDisabled("Restore creates a new version; it does not erase history");
        if (pending_note_changes)
          ImGui::TextDisabled("Finish saving the note before restoring a version");
        for (size_t i = 0; i < state.annotation_history.size(); ++i) {
          const auto& revision = state.annotation_history[i];
          ImGui::PushID(revision.id.c_str());
          const std::time_t seconds = static_cast<std::time_t>(revision.time);
          char when[40] = {};
          if (const std::tm* local = std::localtime(&seconds))
            std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", local);
          ImGui::Text("%s · %zu annotations · %s",
                      i == 0 ? "Current" : "Earlier",
                      revision.count, when);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", revision.id.c_str());
          if (i != 0) {
            ImGui::SameLine();
            ImGui::BeginDisabled(pending_note_changes);
            if (classic_button("Restore")) {
              cmd = UiCommand::RestoreAnnotationLayerVersion;
              result.arg = state.annotation_history_layer + "\n" + revision.id;
            }
            ImGui::EndDisabled();
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::End();
  }

  // ---- Nostr catalogue, embedded in a native FloatWindow ----
  // Geometry, title, stacking, docking and clipping belong to Xanatoo.
  // ImGui supplies only the searchable list controls within its client well.
  constexpr ImGuiWindowFlags embedded_panel_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
  if (state.nostr_explorer_open) {
    ImGui::SetNextWindowPos(ImVec2(float(state.nostr_explorer_x + 3),
                                  float(state.nostr_explorer_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(float(std::max(1, state.nostr_explorer_w - 6)),
                                   float(std::max(1, state.nostr_explorer_h - 6))));
    if (ImGui::Begin("##nostr_explorer_body", nullptr, embedded_panel_flags)) {
      ImGui::Text("Observed: %lld events, %lld notes, %lld people",
                  static_cast<long long>(state.nostr_stats.events),
                  static_cast<long long>(state.nostr_stats.notes),
                  static_cast<long long>(state.nostr_stats.pubkeys));
      ImGui::TextDisabled("%lld profiles, %lld relays (local sample)",
                          static_cast<long long>(state.nostr_stats.profiles),
                          static_cast<long long>(state.nostr_stats.relays));
      ImGui::TextDisabled("%lld signed, %lld hash-only, %lld legacy unverified",
                          static_cast<long long>(state.nostr_stats.signed_events),
                          static_cast<long long>(state.nostr_stats.events -
                                                 state.nostr_stats.signed_events),
                          static_cast<long long>(state.nostr_stats.legacy_events));
      ImGui::TextDisabled("%lld relay-scoped NIP-29 groups observed",
                          static_cast<long long>(state.nostr_stats.groups));
      ImGui::SetNextItemWidth(-80.f * scale_);
      if (ImGui::InputText("##nostr_search", nostr_local_search_,
                           sizeof nostr_local_search_)) {
        cmd = UiCommand::NostrSearchLocal;
        result.arg = nostr_local_search_;
      }
      ImGui::SameLine();
      if (classic_button("Refresh")) cmd = UiCommand::NostrRefreshLocal;
      if (ImGui::RadioButton("People", state.nostr_tab == 0))
        cmd = UiCommand::NostrShowPeople;
      ImGui::SameLine();
      if (ImGui::RadioButton("Events", state.nostr_tab == 1))
        cmd = UiCommand::NostrShowEvents;
      ImGui::SameLine();
      if (ImGui::RadioButton("Groups", state.nostr_tab == 2))
        cmd = UiCommand::NostrShowGroups;
      if (state.nostr_tab == 2) {
        ImGui::TextDisabled("Discover automatically, or specify a group relay:");
        ImGui::SetNextItemWidth(-95.f * scale_);
        ImGui::InputTextWithHint("##nostr_group_relay", "relay host or wss:// URL (optional)",
                                 nostr_group_relay_, sizeof nostr_group_relay_);
        ImGui::SameLine();
        if (classic_button("Discover")) {
          cmd = UiCommand::NavigateUrl;
          result.arg = "nostr:groups";
          if (nostr_group_relay_[0] != '\0') {
            result.arg += "@";
            result.arg += nostr_group_relay_;
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Empty: scan known NIP-29 hosts and relays from public group lists.\n"
                            "A hostname is enough for a specific relay (TLS is always used).");
      } else if (classic_button("Fetch recent")) {
        cmd = UiCommand::NavigateUrl;
        result.arg = "nostr:recent";
      }
      ImGui::BeginDisabled(state.nostr_page == 0);
      if (classic_button("Previous")) cmd = UiCommand::NostrPrevPage;
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::Text("Page %d", state.nostr_page + 1);
      ImGui::SameLine();
      ImGui::BeginDisabled(!(state.nostr_tab == 0 ? state.nostr_people_more :
                            state.nostr_tab == 1 ? state.nostr_events_more :
                                                   state.nostr_groups_more));
      if (classic_button("Next")) cmd = UiCommand::NostrNextPage;
      ImGui::EndDisabled();
      ImGui::Separator();
      // A child scroll area avoids producing thousands of text widgets when
      // an explorer panel is shrunk or docked beneath another panel.
      if (ImGui::BeginChild("##nostr_results", ImVec2(0.f, 0.f), false)) {
        if (state.nostr_tab == 0) {
          if (state.nostr_people.empty())
            ImGui::TextWrapped("No matching public keys cached yet. Fetch recent notes, or open an npub1 / note1 address.");
          for (const auto& person : state.nostr_people) {
            ImGui::PushID(person.pubkey.c_str());
            const std::string label = (person.name.empty() ?
                person.pubkey.substr(0, 14) + "…" : person.name) +
                "  ·  " + std::to_string(person.post_count) + " notes" +
                (person.profile_verification == 2 ? " [sig]" : "");
            if (ImGui::Selectable(label.c_str())) {
              cmd = UiCommand::NavigateUrl;
              result.arg = "nostr:" + nostr_bech32("npub", person.pubkey);
            }
            if (ImGui::BeginPopupContextItem("##nostr_person_sub")) {
              if (ImGui::MenuItem("Subscribe to author")) {
                cmd = UiCommand::Subscribe;
                result.arg = "nostr:" + nostr_bech32("npub", person.pubkey);
              }
              ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("%s\n%lld unique events observed\n%s\n%s",
                                person.pubkey.c_str(),
                                static_cast<long long>(person.event_count),
                                person.nip05.c_str(),
                                person.profile_verification == 2 ?
                                  "Profile signature checked" : "Profile not signature verified");
            ImGui::PopID();
          }
        } else if (state.nostr_tab == 1) {
          if (state.nostr_events.empty())
            ImGui::TextWrapped("No matching cached events. Only events downloaded by this client appear here.");
          for (const auto& ev : state.nostr_events) {
            ImGui::PushID(ev.id.c_str());
            const std::string name = ev.author_name.empty() ?
                ev.pubkey.substr(0, 12) + "…" : ev.author_name;
            std::string summary = ev.content;
            for (char& c : summary) if (c == '\n' || c == '\r') c = ' ';
            if (summary.size() > 110) summary = summary.substr(0, 110) + "…";
            const std::string label = std::string(ev.verification == 2 ? "[sig] " : "[hash] ") +
                "kind " + std::to_string(ev.kind) +
                " · " + name + " · " + summary;
            if (ImGui::Selectable(label.c_str(), false,
                                  (ev.kind == 1 || ev.kind == 9 || ev.kind == 11) ?
                                    0 : ImGuiSelectableFlags_Disabled)) {
              cmd = UiCommand::NavigateUrl;
              result.arg = "nostr:" + nostr_bech32("note", ev.id);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("%s\n%s", ev.id.c_str(), ev.pubkey.c_str());
            ImGui::PopID();
          }
        } else {
          if (state.nostr_groups.empty())
            ImGui::TextWrapped("No NIP-29 groups cached yet. Use Discover to scan known group relays, or enter a specific host.");
          for (const auto& group : state.nostr_groups) {
            ImGui::PushID(group.relay.c_str());
            ImGui::PushID(group.group_id.c_str());
            const std::string href = nostr_group_ref(group.relay, group.group_id);
            const std::string name = group.name.empty() ? group.group_id : group.name;
            const std::string label = (group.relay_master_verified ? "[relay] " :
                                       group.metadata_verification == 2 ? "[sig] " :
                                       group.metadata_verification == 1 ? "[hash] " : "[?] ") +
                name + " · " + std::to_string(group.message_count) +
                " messages · " + group.relay;
            if (ImGui::Selectable(label.c_str(), false,
                                  href.empty() ? ImGuiSelectableFlags_Disabled : 0)) {
              cmd = UiCommand::NavigateUrl;
              result.arg = href;
            }
            if (!href.empty() && ImGui::BeginPopupContextItem("##nostr_group_sub")) {
              if (ImGui::MenuItem("Subscribe to group")) {
                cmd = UiCommand::Subscribe;
                result.arg = href;
              }
              ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Group: %s\nRelay: %s\nParent: %s\n%s\n"
                                "%lld unique group events\n%s%s%s%s\n"
                                "%s",
                                group.group_id.c_str(), group.relay.c_str(),
                                group.parent_id.empty() ? "(root/unknown)" : group.parent_id.c_str(),
                                group.about.c_str(),
                                static_cast<long long>(group.event_count),
                                group.private_group ? "private " : "",
                                group.hidden ? "hidden " : "",
                                group.restricted ? "restricted " : "",
                                group.closed ? "closed" : "",
                                group.relay_master_verified ?
                                  "Relay master: event signature matches NIP-11 self (verified HTTPS)" :
                                group.metadata_verification == 2 ?
                                  "Event signature valid; relay master not authenticated" :
                                  "Relay master not authenticated (signature unverified)");
            ImGui::PopID();
            ImGui::PopID();
          }
        }
      }
      ImGui::EndChild();
    }
    ImGui::End();
  }

  // ---- Lemmy discovery catalogue, inside its native dockable panel ----
  if (state.lemmy_explorer_open) {
    ImGui::SetNextWindowPos(ImVec2(float(state.lemmy_explorer_x + 3),
                                  float(state.lemmy_explorer_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(
        float(std::max(1, state.lemmy_explorer_w - 6)),
        float(std::max(1, state.lemmy_explorer_h - 6))));
    if (ImGui::Begin("##lemmy_explorer_body", nullptr, embedded_panel_flags)) {
      int counts[4] = {};
      for (const auto& entry : state.lemmy_entries)
        ++counts[static_cast<int>(entry.kind)];
      ImGui::TextDisabled("%d servers · %d communities · %d people · %d posts",
                          counts[0], counts[1], counts[2], counts[3]);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Public read-only catalogue stored locally; no login required");
      ImGui::SetNextItemWidth(-1.f);
      if (ImGui::BeginCombo("##lemmy_instance", state.lemmy_instance.c_str())) {
        for (const auto& entry : state.lemmy_entries) {
          if (entry.kind != LemmyEntryKind::Instance) continue;
          const bool selected = entry.name == state.lemmy_instance;
          if (ImGui::Selectable(entry.name.c_str(), selected)) {
            cmd = UiCommand::LemmySetInstance;
            result.arg = entry.name;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("API instance for Feed, Communities, Search and Peer discovery");

      ImGui::SetNextItemWidth(-100.f * scale_);
      if (ImGui::InputTextWithHint("##lemmy_filter", "Filter observed entries…",
                                   lemmy_search_input_, sizeof lemmy_search_input_)) {
        cmd = UiCommand::LemmySetSearch;
        result.arg = lemmy_search_input_;
      }
      ImGui::SameLine();
      bool only_this_server = state.lemmy_instance_only;
      if (ImGui::Checkbox("This server", &only_this_server))
        cmd = UiCommand::LemmyToggleInstanceOnly;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Filter discovered communities, people and posts to the selected server");

      if (ImGui::RadioButton("Servers", state.lemmy_tab == 0)) {
        cmd = UiCommand::LemmySetTab; result.arg = "0";
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Communities", state.lemmy_tab == 1)) {
        cmd = UiCommand::LemmySetTab; result.arg = "1";
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("People", state.lemmy_tab == 2)) {
        cmd = UiCommand::LemmySetTab; result.arg = "2";
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Posts", state.lemmy_tab == 3)) {
        cmd = UiCommand::LemmySetTab; result.arg = "3";
      }

      const std::string base = "lemmy:" + state.lemmy_instance + "/api/v3/";
      ImGui::BeginDisabled(!state.lemmy_supported);
      if (classic_button("Feed")) {
        cmd = UiCommand::NavigateUrl;
        result.arg = base + "post/list?limit=25&sort=" + state.lemmy_sort;
      }
      ImGui::SameLine();
      if (classic_button("Find communities")) {
        cmd = UiCommand::NavigateUrl;
        result.arg = base + "community/list?limit=50&sort=Active";
      }
      ImGui::SameLine();
      if (classic_button("Federated peers")) {
        cmd = UiCommand::NavigateUrl;
        result.arg = base + "federated_instances";
      }
      if (lemmy_search_input_[0] != '\0') {
        if (classic_button("Search server")) {
          cmd = UiCommand::NavigateUrl;
          result.arg = base + "search?q=" + lemmy_query_escape(lemmy_search_input_) +
                       "&type_=All&limit=50";
        }
        ImGui::SameLine();
      }
      ImGui::SetNextItemWidth(114.f * scale_);
      if (ImGui::BeginCombo("Feed sort", state.lemmy_sort.c_str())) {
        for (const char* option : {"Hot", "Active", "New", "TopDay", "TopWeek"}) {
          if (ImGui::Selectable(option, state.lemmy_sort == option)) {
            cmd = UiCommand::LemmySetSort;
            result.arg = option;
          }
        }
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();

      ImGui::SetNextItemWidth(-1.f);
      ImGui::InputTextWithHint("##lemmy_add_instance", "Add server hostname or URL",
                               lemmy_instance_input_, sizeof lemmy_instance_input_);
      if (classic_button("Add instance")) {
        cmd = UiCommand::LemmyAddInstance;
        result.arg = lemmy_instance_input_;
      }
      ImGui::SameLine();
      if (classic_button("Directory")) {
        cmd = UiCommand::NavigateUrl;
        result.arg = "https://join-lemmy.org/instances";
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Official Lemmy server directory on join-lemmy.org");
      ImGui::Separator();

      if (ImGui::BeginChild("##lemmy_results", ImVec2(0.f, 0.f), false)) {
        const LemmyEntryKind current = static_cast<LemmyEntryKind>(state.lemmy_tab);
        const std::string query = ascii_lower(state.lemmy_search);
        int matching = 0;
        for (const auto& item : state.lemmy_entries) {
          if (item.kind != current) continue;
          if (state.lemmy_instance_only && current != LemmyEntryKind::Instance &&
              item.instance != state.lemmy_instance) continue;
          if (!query.empty() &&
              ascii_lower(item.title).find(query) == std::string::npos &&
              ascii_lower(item.name).find(query) == std::string::npos &&
              ascii_lower(item.instance).find(query) == std::string::npos)
            continue;
          if (++matching > 250) continue; // bounded widget count for large catalogues
          ImGui::PushID(item.ref.c_str());
          std::string label = item.title;
          if (item.title != item.name) label += " · " + item.name;
          if (current != LemmyEntryKind::Instance)
            label += " · " + item.instance;
          if (item.score) {
            label += " · " + std::to_string(item.score);
            label += current == LemmyEntryKind::Community ? " subscribers" :
                     current == LemmyEntryKind::Person ? " posts" : " points";
          }
          const bool selected = current == LemmyEntryKind::Instance &&
                                item.name == state.lemmy_instance;
          if (ImGui::Selectable(label.c_str(), selected)) {
            if (current == LemmyEntryKind::Instance) {
              cmd = UiCommand::LemmySetInstance;
              result.arg = item.name;
            } else {
              cmd = UiCommand::NavigateUrl;
              result.arg = item.ref;
            }
          }
          if ((current == LemmyEntryKind::Community ||
               current == LemmyEntryKind::Person) &&
              ImGui::BeginPopupContextItem("##lemmy_sub")) {
            if (ImGui::MenuItem("Subscribe")) {
              cmd = UiCommand::Subscribe;
              result.arg = item.ref;
            }
            ImGui::EndPopup();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", current == LemmyEntryKind::Instance ?
                "Select this server for feed, community and search actions" :
                item.ref.c_str());
          ImGui::PopID();
        }
        if (matching == 0) {
          ImGui::TextWrapped(current == LemmyEntryKind::Instance ?
              "No matching instances. Add a server or discover federated peers." :
              "No cached matches yet. Browse a server feed, search it, or find communities to collect public names and posts.");
        } else if (matching > 250) {
          ImGui::TextDisabled("Showing first 250 of %d matches. Narrow the filter to find more.", matching);
        }
      }
      ImGui::EndChild();
    }
    ImGui::End();
  }

  // ---- Account aliases: separate native panel, deliberately watch-only ----
  if (state.nostr_accounts_open) {
    ImGui::SetNextWindowPos(ImVec2(float(state.nostr_accounts_x + 3),
                                  float(state.nostr_accounts_y + 3)));
    ImGui::SetNextWindowSize(ImVec2(float(std::max(1, state.nostr_accounts_w - 6)),
                                   float(std::max(1, state.nostr_accounts_h - 6))));
    if (ImGui::Begin("##nostr_accounts_body", nullptr, embedded_panel_flags)) {
      ImGui::TextWrapped("Watch-only identities (public keys only). These cannot sign or publish events.");
      if (state.nostr_identities.empty())
        ImGui::TextDisabled("No local identities configured.");
      for (const auto& identity : state.nostr_identities) {
        ImGui::PushID(identity.pubkey.c_str());
        if (ImGui::RadioButton(identity.label.c_str(),
                               identity.pubkey == state.nostr_selected_identity)) {
          cmd = UiCommand::NostrSelectIdentity;
          result.arg = identity.pubkey;
        }
        ImGui::SameLine();
        if (classic_button("View profile")) {
          cmd = UiCommand::NavigateUrl;
          result.arg = "nostr:" + nostr_bech32("npub", identity.pubkey);
        }
        ImGui::TextDisabled("%s…", identity.pubkey.substr(0, 24).c_str());
        ImGui::PopID();
      }
      ImGui::Separator();
      ImGui::TextUnformatted("Add or rename a public identity");
      ImGui::InputText("Label", nostr_identity_label_,
                       sizeof nostr_identity_label_);
      ImGui::InputText("npub / hex", nostr_identity_pubkey_,
                       sizeof nostr_identity_pubkey_);
      if (classic_button("Save watch-only identity")) {
        cmd = UiCommand::NostrAddWatchIdentity;
        result.arg = std::string(nostr_identity_label_) + "\n" +
                     nostr_identity_pubkey_;
      }
      ImGui::Separator();
      ImGui::TextWrapped("Creating an account with a signing key needs secure NIP-46 remote signing or encrypted NIP-49 key storage. No private keys are accepted here.");
    }
    ImGui::End();
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
        if (classic_button(label, ImVec2(w, 34.f * scale_))) {
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
      if (classic_button(shift ? "SHIFT" : "Shift", ImVec2(kw * 1.6f, 34.f * scale_)))
        shift = !shift;
      ImGui::SameLine(0.f, 2.f);
      for (const char* c = row3; *c; ++c) {
        char lab[2] = {*c, 0};
        char pay[2] = {*c, 0};
        keybtn(lab, pay, kw);
      }
      if (classic_button("Bksp", ImVec2(kw * 1.8f, 34.f * scale_))) {
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
      if (classic_button(",", ImVec2(kw, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ",";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      const float space_w = std::max(100.f, vk_vp->Size.x - kw * 6.f);
      if (classic_button("Space", ImVec2(space_w, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = " ";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (classic_button(".", ImVec2(kw, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ".";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (classic_button("Enter", ImVec2(kw * 2.f, 34.f * scale_))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = "\n";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (classic_button("Hide", ImVec2(kw * 1.6f, 34.f * scale_)))
        cmd = UiCommand::ToggleVirtualKeyboard;
    }
    ImGui::End();
    result.bottom_reserved = vk_h;
  }

  // Match the native paint stack for ImGui input, including child widgets.
  sync_embedded_imgui_order(state.native_panel_order);
  return result;
}

void Ui::prepare_render() {
  if (!ready_) return;
  if (render_prepared_) return;
  // A text field (URL bar) just took focus: on-screen keyboard (Android).
  const bool wants_text = ImGui::GetIO().WantTextInput;
  if (wants_text && !wanted_text_input_) platform_request_keyboard(window_);
  wanted_text_input_ = wants_text;
  ImGui::Render();
  render_prepared_ = true;
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  layered_panels_ = true;
#else
  // The pre-2.0.18 SDL fallback caches a *whole* UI render in one texture;
  // rendering individual lists would invalidate that cache repeatedly. Keep
  // the historical single-pass path for those older R36S SDL libraries.
  layered_panels_ = !ImGui_ImplSDLRenderer2_UsesTriangleFallback();
#endif
}

// ImGui produces one draw list per window (and separate lists for child
// windows). Partition those existing lists without rebuilding widgets or
// changing their input state; each panel's lists can then be composited in
// exactly the same order as its native FloatWindow frame.
static void render_ui_layer(SDL_Renderer* renderer, UiPanelLayer layer,
                            bool chrome_only) {
  ImDrawData* full = ImGui::GetDrawData();
  if (!full) return;
  std::vector<ImDrawList*> lists;
  int vertices = 0, indices = 0;
  for (int i = 0; i < full->CmdListsCount; ++i) {
    ImDrawList* list = full->CmdLists[i];
    const UiPanelLayer owner = ui_panel_layer(
        list->_OwnerName ? std::string_view(list->_OwnerName) : std::string_view());
    if ((chrome_only && owner != UiPanelLayer::Chrome) ||
        (!chrome_only && owner != layer))
      continue;
    lists.push_back(list);
    vertices += list->VtxBuffer.Size;
    indices += list->IdxBuffer.Size;
  }
  if (lists.empty()) return;
  // ImDrawData owns the pointer array (ImVector), so never shallow-copy it:
  // a copied array would be freed twice at the end of this temporary pass.
  ImDrawData selected;
  selected.Valid = full->Valid;
  selected.DisplayPos = full->DisplayPos;
  selected.DisplaySize = full->DisplaySize;
  selected.FramebufferScale = full->FramebufferScale;
  selected.CmdLists.resize(static_cast<int>(lists.size()));
  for (int i = 0; i < static_cast<int>(lists.size()); ++i)
    selected.CmdLists[i] = lists[static_cast<size_t>(i)];
  selected.CmdListsCount = static_cast<int>(lists.size());
  selected.TotalVtxCount = vertices;
  selected.TotalIdxCount = indices;
#if defined(XANATOO_SDL3) && XANATOO_SDL3
  xanatoo::imgui_sdl3::render(&selected, renderer);
#else
  ImGui_ImplSDLRenderer2_RenderDrawData(&selected, renderer);
#endif
}

void Ui::render_embedded_panel(SDL_Renderer* renderer, UiPanelLayer panel) {
  if (!ready_ || !render_prepared_ || !layered_panels_ ||
      panel == UiPanelLayer::Chrome) return;
  render_ui_layer(renderer, panel, false);
}

void Ui::end_frame(SDL_Renderer* renderer) {
  if (!ready_) return;
  prepare_render();
  if (layered_panels_) {
    render_ui_layer(renderer, UiPanelLayer::Chrome, true);
  } else {
#if defined(XANATOO_SDL3) && XANATOO_SDL3
    xanatoo::imgui_sdl3::render(ImGui::GetDrawData(), renderer);
#else
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
#endif
  }
  render_prepared_ = false;
}

}  // namespace xanatoo
