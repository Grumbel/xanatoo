// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/ui.hpp"
#include "xanatoo/font.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

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

Ui::~Ui() { shutdown(); }

// ---------------------------------------------------------------------------
// Classic desktop theme — Clearlooks / Gtk2 greys with BeOS-ish blue selection.
// ---------------------------------------------------------------------------
void Ui::apply_classic_theme() {
  ImGui::StyleColorsLight();
  ImGuiStyle& s = ImGui::GetStyle();

  s.WindowPadding = ImVec2(6.f, 4.f);
  s.FramePadding = ImVec2(6.f, 3.f);
  s.ItemSpacing = ImVec2(6.f, 4.f);
  s.ItemInnerSpacing = ImVec2(4.f, 3.f);
  s.ScrollbarSize = 12.f;
  s.GrabMinSize = 10.f;

  // Soft corners like Clearlooks; not fully flat, not fully modern.
  s.WindowRounding = 3.f;
  s.ChildRounding = 2.f;
  s.FrameRounding = 3.f;
  s.PopupRounding = 3.f;
  s.ScrollbarRounding = 3.f;
  s.GrabRounding = 2.f;
  s.TabRounding = 2.f;

  s.WindowBorderSize = 1.f;
  s.FrameBorderSize = 1.f;
  s.PopupBorderSize = 1.f;

  auto C = [](int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
  };

  // Paper / panel surfaces
  const ImVec4 bg = C(0xEF, 0xEB, 0xE7);       // Clearlooks window
  const ImVec4 bg2 = C(0xF6, 0xF5, 0xF4);      // menu / toolbar
  const ImVec4 bg3 = C(0xE8, 0xE6, 0xE3);      // button face
  const ImVec4 border = C(0x9C, 0x9A, 0x97);
  const ImVec4 text = C(0x2E, 0x34, 0x36);
  const ImVec4 text_dis = C(0x88, 0x88, 0x84);
  const ImVec4 sel = C(0x4A, 0x90, 0xD9);      // BeOS / classic blue
  const ImVec4 sel_hi = C(0x6A, 0xA8, 0xE8);
  const ImVec4 header = C(0xD8, 0xD4, 0xCE);
  const ImVec4 title = C(0xC8, 0xD4, 0xE8);    // soft title blue

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
  c[ImGuiCol_TitleBgActive] = C(0xB0, 0xC4, 0xE0);
  c[ImGuiCol_TitleBgCollapsed] = header;
  c[ImGuiCol_MenuBarBg] = bg2;
  c[ImGuiCol_ScrollbarBg] = C(0xE4, 0xE2, 0xDE);
  c[ImGuiCol_ScrollbarGrab] = C(0xB8, 0xB6, 0xB0);
  c[ImGuiCol_ScrollbarGrabHovered] = C(0xA0, 0x9E, 0x98);
  c[ImGuiCol_ScrollbarGrabActive] = C(0x88, 0x86, 0x80);
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
  c[ImGuiCol_ResizeGrip] = C(0xB8, 0xB6, 0xB0, 180);
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

  // Search order (first hit wins):
  //  1. XANATOO_ICON_DIR env (Nix wrapper / explicit override)
  //  2. Compile-time build-tree path (dev builds before install)
  //  3. Next to the executable: ../share/xanatoo/icons  (installed layout)
  //  4. Cwd-relative fallbacks for in-tree cmake builds
  std::vector<fs::path> roots;
  if (const char* env = std::getenv("XANATOO_ICON_DIR")) {
    if (env[0] != '\0') roots.emplace_back(env);
  }
#ifdef XANATOO_ICON_DIR
  roots.emplace_back(XANATOO_ICON_DIR);
#endif
#ifdef __linux__
  try {
    fs::path exe = fs::read_symlink("/proc/self/exe");
    fs::path dir = exe.parent_path();
    roots.push_back(dir / ".." / "share" / "xanatoo" / "icons");
    roots.push_back(dir / "icons");
  } catch (...) {
  }
#endif
  roots.emplace_back("icons");
  roots.emplace_back("share/xanatoo/icons");
  roots.emplace_back("../icons");
  roots.emplace_back("build/icons");

  const char* names[] = {
      "save", "undo", "redo", "bold", "italic", "code", "link", "list",
      "olist", "quote", "image", "split", "find", "help", "cut",
      "strike", "cite", "h1", "h2", "h3", "para", "task", "table",
      "figure", "note", "replace", "outline", "paste", "copy", "new", "open",
      "zoomin", "zoomout", "structure", "diff",
      "back", "forward", "reload", "bookmark", "history", "browse",
      "format", "insert", "view", "lists",
  };

  // Resolve candidate directories (may mix install tree + build tree).
  std::vector<fs::path> dirs;
  for (const auto& root : roots) {
    std::error_code ec;
    auto canon = fs::weakly_canonical(root, ec);
    const fs::path& probe = ec ? root : canon;
    if (fs::is_directory(probe)) dirs.push_back(probe);
  }
  if (dirs.empty()) {
    std::cerr << "xanatoo: toolbar icons not found "
                 "(set XANATOO_ICON_DIR or install share/xanatoo/icons)\n";
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

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
      stbi_image_free(data);
      continue;
    }
    Icon ic;
    ic.texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    stbi_image_free(data);
    if (!ic.texture) continue;
    SDL_SetTextureBlendMode(ic.texture, SDL_BLENDMODE_BLEND);
    ic.w = w;
    ic.h = h;
    icons_[name] = ic;
  }
  std::cout << "Loaded " << icons_.size() << "/" << (sizeof(names) / sizeof(names[0]))
            << " toolbar icons (searched " << dirs.size() << " dir(s))\n";
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
    ImVec2 size(static_cast<float>(ic->w), static_cast<float>(ic->h));
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
        {"back", "◀"}, {"forward", "▶"}, {"reload", "↻"}, {"bookmark", "★"},
        {"history", "Hist"}, {"browse", "Browse"}, {"format", "Aa▾"},
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
    ImVec2 size(static_cast<float>(ic->w), static_cast<float>(ic->h));
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
        {"table", "+"}, {"h1", "H"}, {"outline", "View"},
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

// GNOME 2–era UI faces: Bitstream Vera → DejaVu Sans was the usual desktop default.
static std::string resolve_ui_font_path() {
  const char* families[] = {
      "DejaVu Sans",
      "Bitstream Vera Sans",
      "Liberation Sans",
      "FreeSans",
      "Nimbus Sans L",
      "Sans",
      nullptr,
  };
  for (const char** f = families; *f; ++f) {
    std::string path = fontconfig_find(*f, 0, 0);
    if (!path.empty()) return path;
  }
  const char* files[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/ttf-bitstream-vera/Vera.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      nullptr,
  };
  for (const char** f = files; *f; ++f) {
    if (fs::exists(*f)) return *f;
  }
  return {};
}

static void load_ui_fonts(ImGuiIO& io, float size_px) {
  // Desktop ~14px; handhelds pass a larger size for readability.
  std::string path = resolve_ui_font_path();
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH = true;  // crisper menus on integer positions
  if (!path.empty()) {
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg,
                                                io.Fonts->GetGlyphRangesDefault());
    if (font) {
      std::cout << "UI font: " << path << " @ " << size_px << "px\n";
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
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;

  // Resolve UI scale: explicit arg → env → auto from window width.
  float scale = ui_scale;
  if (scale <= 0.f) {
    if (const char* e = std::getenv("XANATOO_UI_SCALE")) {
      if (e[0] != '\0') scale = std::strtof(e, nullptr);
    }
  }
  if (scale <= 0.f && window) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    if (ww > 0 && ww <= 720)
      scale = 1.35f;  // R36S / similar handhelds
    else
      scale = 1.f;
  }
  if (scale < 0.75f) scale = 0.75f;
  if (scale > 2.5f) scale = 2.5f;

  const float font_px = 14.0f * (scale > 1.f ? scale : 1.f);
  load_ui_fonts(io, font_px);
  apply_classic_theme();
  {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding = ImVec2(7.f, 4.f);
    s.ItemSpacing = ImVec2(7.f, 5.f);
    s.ScrollbarSize = 14.f;
    if (scale != 1.f) s.ScaleAllSizes(scale);
  }
  if (scale != 1.f)
    std::cout << "UI scale: " << scale << " (font " << font_px << "px)\n";
  load_icons(renderer);

  if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) return false;
  if (!ImGui_ImplSDLRenderer2_Init(renderer)) return false;
  ready_ = true;
  return true;
}

void Ui::shutdown() {
  if (!ready_) return;
  for (auto& [k, ic] : icons_) {
    if (ic.texture) SDL_DestroyTexture(ic.texture);
  }
  icons_.clear();
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ready_ = false;
}

void Ui::process_event(const SDL_Event& ev) {
  if (!ready_) return;
  ImGui_ImplSDL2_ProcessEvent(&ev);
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

void Ui::open_context_menu(float x, float y) {
  context_open_ = true;
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
  ImGui::Separator();
  menu_item_cmd("Header (depth 2)", UiCommand::Heading2, cmd, true, "Ctrl+2");
  menu_item_cmd("Header (depth 3)", UiCommand::Heading3, cmd, true, "Ctrl+3");
  menu_item_cmd("Paragraph (demote)", UiCommand::ParagraphLevel, cmd, true,
                "Ctrl+0");
}

UiFrameResult Ui::begin_frame(const UiState& state) {
  UiFrameResult result;
  if (!ready_) return result;

  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  UiCommand cmd = UiCommand::None;

  // ---- Main menu bar ----
  if (ImGui::BeginMainMenuBar()) {
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
      menu_item_cmd("Pop Out to Window", UiCommand::PopOut, cmd, true,
                    "Ctrl+Shift+P");
      menu_item_cmd("Dock Window to Tab", UiCommand::PopIn, cmd, state.has_floating,
                    "Ctrl+Shift+I");
      ImGui::Separator();
      menu_item_cmd("Save", UiCommand::Save, cmd, true, "Ctrl+S");
      menu_item_cmd("Save As…", UiCommand::SaveAs, cmd, true, "Ctrl+Shift+S");
      menu_item_cmd("Export as S-expression…", UiCommand::ExportSexpr, cmd, true);
      menu_item_cmd("Export as Markdown…", UiCommand::ExportMarkdown, cmd, true);
      ImGui::Separator();
      menu_item_cmd("Reload", UiCommand::Reload, cmd, state.has_path, "Ctrl+R");
      menu_item_cmd("Force Reload", UiCommand::ForceReload, cmd, state.has_path,
                    "Ctrl+Shift+R");
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
      ImGui::EndMenu();
    }
    force_menu("Format");
    if (ImGui::BeginMenu("Format")) {
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
    force_menu("Insert");
    if (ImGui::BeginMenu("Insert")) {
      menu_insert_items(cmd);
      ImGui::EndMenu();
    }
    force_menu("View");
    if (ImGui::BeginMenu("View")) {
      menu_item_cmd("Back", UiCommand::NavBack, cmd, state.can_nav_back, "Alt+Left");
      menu_item_cmd("Forward", UiCommand::NavForward, cmd, state.can_nav_forward,
                    "Alt+Right");
      ImGui::Separator();
      if (ImGui::MenuItem("Split Pane", "Ctrl+\\", state.split))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff, state.can_diff))
        cmd = UiCommand::ToggleDiff;
      if (ImGui::MenuItem("Structure Highlight", "Ctrl+Shift+L", state.structure))
        cmd = UiCommand::ToggleStructure;
      if (ImGui::MenuItem("Browse Mode (read-only)", "F2", state.browse))
        cmd = UiCommand::ToggleBrowse;
      if (ImGui::MenuItem("History", "Ctrl+Shift+H", state.history_open))
        cmd = UiCommand::ToggleHistory;
      ImGui::Separator();
      menu_item_cmd("Zoom In", UiCommand::ZoomIn, cmd, true, "Ctrl++");
      menu_item_cmd("Zoom Out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
      ImGui::Separator();
      menu_item_cmd("FORMAT.md in window", UiCommand::OpenFormatDoc, cmd);
      menu_item_cmd("semantic.sexp in window", UiCommand::OpenSemanticDoc, cmd);
      menu_item_cmd("Open path in window…", UiCommand::OpenInWindow, cmd);
      ImGui::Separator();
      if (ImGui::BeginMenu("Examples")) {
        auto ex = [&](const char* label, const char* file) {
          if (ImGui::MenuItem(label)) {
            cmd = UiCommand::OpenExample;
            result.arg = file;
          }
        };
        ex("Welcome", "welcome.md");
        ex("Keyboard help", "help.md");
        ex("Thread / RFC discussion", "thread-rfc.md");
        ex("Article / section / header", "sectioning.md");
        ex("Gemini gemtext sample", "gemini-sample.gmi");
        ex("Org-mode sample", "sample.org");
        ex("Semantic format (sexpr)", "semantic.sexp");
        ex("Sample (sexpr)", "sample.sexp");
        ex("Diff demo — left (v1)", "diff-v1.md");
        ex("Diff demo — right (v2)", "diff-v2.md");
        ImGui::Separator();
        if (ImGui::MenuItem("Diff demo (v1 | v2, marks on)"))
          cmd = UiCommand::LoadDiffDemo;
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    force_menu("Bookmarks");
    if (ImGui::BeginMenu("Bookmarks")) {
      const char* add_label =
          state.current_bookmarked ? "Remove Bookmark" : "Bookmark This Page";
      if (ImGui::MenuItem(add_label, "Ctrl+Shift+B", false, state.has_path)) {
        cmd = state.current_bookmarked ? UiCommand::RemoveBookmark
                                       : UiCommand::AddBookmark;
      }
      if (ImGui::MenuItem("Show Bookmarks…", nullptr, state.bookmarks_open))
        cmd = UiCommand::ToggleBookmarks;
      if (!state.bookmarks.empty()) {
        ImGui::Separator();
        // Newest bookmarks last; show up to 30 in the menu.
        const int n = static_cast<int>(state.bookmarks.size());
        const int start = n > 30 ? n - 30 : 0;
        for (int i = start; i < n; ++i) {
          const std::string& b = state.bookmarks[static_cast<size_t>(i)];
          // Truncate long labels for the menu.
          std::string label = b;
          if (label.size() > 64) {
            label = label.substr(0, 28) + "…" + label.substr(label.size() - 28);
          }
          if (ImGui::MenuItem(label.c_str())) {
            cmd = UiCommand::NavigateUrl;
            result.arg = b;
          }
        }
      }
      ImGui::EndMenu();
    }
    force_menu("Help");
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Keyboard Reference", "F1", state.help))
        cmd = UiCommand::ToggleHelp;
      ImGui::Separator();
      menu_item_cmd("FORMAT.md", UiCommand::OpenFormatDoc, cmd);
      menu_item_cmd("Semantic format demo", UiCommand::OpenSemanticDoc, cmd);
      ImGui::Separator();
      if (ImGui::BeginMenu("Examples")) {
        auto ex = [&](const char* label, const char* file) {
          if (ImGui::MenuItem(label)) {
            cmd = UiCommand::OpenExample;
            result.arg = file;
          }
        };
        ex("Welcome", "welcome.md");
        ex("Keyboard help", "help.md");
        ex("Thread / RFC discussion", "thread-rfc.md");
        ex("Article / section / header", "sectioning.md");
        ex("Gemini gemtext sample", "gemini-sample.gmi");
        ex("Org-mode sample", "sample.org");
        ex("Semantic format (sexpr)", "semantic.sexp");
        ex("Sample (sexpr)", "sample.sexp");
        ex("Diff demo — left (v1)", "diff-v1.md");
        ex("Diff demo — right (v2)", "diff-v2.md");
        ImGui::Separator();
        if (ImGui::MenuItem("Diff demo (v1 | v2, marks on)"))
          cmd = UiCommand::LoadDiffDemo;
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    // Unmatched Alt+letter request (e.g. after menus reordered) — drop it.
    menu_request_.clear();
    result.top_reserved = ImGui::GetFrameHeight();
    ImGui::EndMainMenuBar();
  }

  // ---- Two toolbars: browse (navigation) then edit ----
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiWindowFlags tb_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollWithMouse;

  // -- Browse / navigation row (browser chrome) --
  const float browse_h = 34.f;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, browse_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
  if (ImGui::Begin("##toolbar_browse", nullptr, tb_flags)) {
    tool_button("tb_back", "back", "Back (Alt+Left)", UiCommand::NavBack, cmd,
                state.can_nav_back);
    tool_button("tb_fwd", "forward", "Forward (Alt+Right)", UiCommand::NavForward,
                cmd, state.can_nav_forward);
    tool_button("tb_reload", "reload", "Reload (Ctrl+R)", UiCommand::Reload, cmd,
                state.has_path);

    ImGui::TextDisabled("|");
    ImGui::SameLine(0.f, 4.f);

    // Location / URL field (path or gemini://)
    {
      std::string want = state.doc_label.empty() ? "untitled" : state.doc_label;
      if (state.dirty) {
        ImGui::TextDisabled("*");
        ImGui::SameLine(0.f, 2.f);
      }
      // Leave room for bookmark / history / browse buttons on the right.
      float right_reserve = 168.f;
      float field_w = ImGui::GetContentRegionAvail().x - right_reserve;
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

    {
      const char* bm_tip =
          state.current_bookmarked ? "Remove bookmark (Ctrl+Shift+B)"
                                   : "Bookmark this page (Ctrl+Shift+B)";
      tool_button("tb_bookmark", "bookmark", bm_tip,
                  state.current_bookmarked ? UiCommand::RemoveBookmark
                                           : UiCommand::AddBookmark,
                  cmd, state.has_path || !state.doc_label.empty());
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
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  result.top_reserved += browse_h;

  // -- Edit row (compact: primary icons + dropdown groups) --
  const float edit_h = 36.f;
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
    if (tool_menu_begin("tb_viewmenu", "view", "View tools")) {
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
      ImGui::Separator();
      if (ImGui::MenuItem("Split pane", "Ctrl+\\", state.split))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Heading outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      if (ImGui::MenuItem("Structure highlight", "Ctrl+Shift+L", state.structure))
        cmd = UiCommand::ToggleStructure;
      if (ImGui::MenuItem("Diff marks", "Ctrl+Shift+D", state.diff, state.can_diff))
        cmd = UiCommand::ToggleDiff;
      ImGui::Separator();
      menu_item_cmd("Zoom in", UiCommand::ZoomIn, cmd, true, "Ctrl++");
      menu_item_cmd("Zoom out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
      ImGui::Separator();
      menu_item_cmd("Help", UiCommand::ToggleHelp, cmd, true, "F1");
      if (ImGui::MenuItem("Virtual keyboard", "F3", state.virtual_keyboard))
        cmd = UiCommand::ToggleVirtualKeyboard;
      tool_menu_end();
    }

    ImGui::EndChild();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  result.top_reserved += edit_h;

  // ---- Document tab strip (browser-style) ----
  if (!state.tab_labels.empty()) {
    const float tab_h = 28.f;
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
      // Reorderable left off until tab_order is updated on drag.
      if (ImGui::BeginTabBar("##doctabs",
                             ImGuiTabBarFlags_FittingPolicyScroll)) {
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
          id += "###tab";
          id += std::to_string(i);
          if (ImGui::BeginTabItem(id.c_str(), &open, flags)) {
            ImGui::EndTabItem();
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
        ImGui::EndTabBar();
        // After main applies SelectTab/NextTab, active_tab changes → sync ImGui.
        if (state.active_tab != tab_synced_active_) {
          tab_synced_active_ = state.active_tab;
          tab_force_select_ = true;
        }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+")) cmd = UiCommand::NewTab;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("New tab (Ctrl+T)");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    result.top_reserved += tab_h;
  }

  // ---- Context menu (right-click in document) ----
  if (context_open_) {
    ImGui::SetNextWindowPos(ImVec2(context_x_, context_y_), ImGuiCond_Appearing);
    ImGui::OpenPopup("##doc_context");
    context_open_ = false;
  }
  if (ImGui::BeginPopup("##doc_context")) {
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
    if (ImGui::MenuItem("Structure Highlight", "Ctrl+Shift+L", state.structure))
      cmd = UiCommand::ToggleStructure;
    if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
      cmd = UiCommand::ToggleOutline;
    if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff, state.can_diff))
      cmd = UiCommand::ToggleDiff;
    menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
    menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
    ImGui::EndPopup();
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
      ImGui::TextDisabled("Persistent bookmarks (~/.config/xanatoo/bookmarks)");
      ImGui::Separator();
      if (ImGui::Button(state.current_bookmarked ? "Remove current page"
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
          const std::string& b = state.bookmarks[static_cast<size_t>(i)];
          ImGui::PushID(i);
          if (ImGui::Selectable(b.c_str(), false,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
              cmd = UiCommand::NavigateUrl;
              result.arg = b;
            }
          }
          if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(1)) {
            ImGui::OpenPopup("##bm_ctx");
          }
          if (ImGui::BeginPopup("##bm_ctx")) {
            if (ImGui::MenuItem("Open")) {
              cmd = UiCommand::NavigateUrl;
              result.arg = b;
            }
            if (ImGui::MenuItem("Remove")) {
              cmd = UiCommand::RemoveBookmark;
              result.arg = b;
            }
            ImGui::EndPopup();
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
    const float vk_h = vk_vp->Size.y < 500.f ? 200.f : 220.f;
    ImGui::SetNextWindowPos(
        ImVec2(vk_vp->Pos.x, vk_vp->Pos.y + vk_vp->Size.y - vk_h));
    ImGui::SetNextWindowSize(ImVec2(vk_vp->Size.x, vk_h));
    ImGuiWindowFlags vk_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##vkeyboard", nullptr, vk_flags)) {
      auto keybtn = [&](const char* label, const char* payload, float w) {
        if (ImGui::Button(label, ImVec2(w, 34.f))) {
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
      if (ImGui::Button(shift ? "SHIFT" : "Shift", ImVec2(kw * 1.6f, 34.f)))
        shift = !shift;
      ImGui::SameLine(0.f, 2.f);
      for (const char* c = row3; *c; ++c) {
        char lab[2] = {*c, 0};
        char pay[2] = {*c, 0};
        keybtn(lab, pay, kw);
      }
      if (ImGui::Button("Bksp", ImVec2(kw * 1.8f, 34.f))) {
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
      if (ImGui::Button(",", ImVec2(kw, 34.f))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ",";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      const float space_w = std::max(100.f, vk_vp->Size.x - kw * 6.f);
      if (ImGui::Button("Space", ImVec2(space_w, 34.f))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = " ";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button(".", ImVec2(kw, 34.f))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = ".";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button("Enter", ImVec2(kw * 2.f, 34.f))) {
        if (cmd == UiCommand::None) {
          cmd = UiCommand::VirtualKey;
          result.arg = "\n";
        }
      }
      ImGui::SameLine(0.f, 2.f);
      if (ImGui::Button("Hide", ImVec2(kw * 1.6f, 34.f)))
        cmd = UiCommand::ToggleVirtualKeyboard;
    }
    ImGui::End();
    result.bottom_reserved = vk_h;
  }

  return result;
}

void Ui::end_frame(SDL_Renderer* renderer) {
  if (!ready_) return;
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

}  // namespace xanatoo
