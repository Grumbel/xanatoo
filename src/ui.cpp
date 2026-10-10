// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/ui.hpp"
#include "xanatoo/font.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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
  };

  fs::path found_dir;
  for (const auto& root : roots) {
    std::error_code ec;
    auto canon = fs::weakly_canonical(root, ec);
    const fs::path& probe = ec ? root : canon;
    if (fs::exists(probe / "save.png")) {
      found_dir = probe;
      break;
    }
  }
  if (found_dir.empty()) {
    std::cerr << "xanatoo: toolbar icons not found "
                 "(set XANATOO_ICON_DIR or install share/xanatoo/icons)\n";
    return;
  }

  for (const char* name : names) {
    fs::path path = found_dir / (std::string(name) + ".png");
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
  std::cout << "Loaded " << icons_.size() << " toolbar icons from " << found_dir
            << "\n";
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
    // Text fallback when icons failed to load
    if (ImGui::SmallButton(id)) {
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

static void load_ui_fonts(ImGuiIO& io) {
  // ~10–11 pt at 96 dpi — close to GNOME 2 menu/toolbar size
  const float size_px = 14.0f;
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

bool Ui::init(SDL_Window* window, SDL_Renderer* renderer) {
  if (ready_) return true;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;

  load_ui_fonts(io);
  apply_classic_theme();
  // Slightly roomier chrome once a proportional face is loaded
  {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding = ImVec2(7.f, 4.f);
    s.ItemSpacing = ImVec2(7.f, 5.f);
    s.ScrollbarSize = 14.f;
  }
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
  return ImGui::GetIO().WantCaptureKeyboard;
}

bool Ui::want_capture_mouse() const {
  if (!ready_) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

static bool menu_item_cmd(const char* label, UiCommand c, UiCommand& out,
                          bool enabled = true, const char* shortcut = nullptr) {
  if (ImGui::MenuItem(label, shortcut, false, enabled)) {
    out = c;
    return true;
  }
  return false;
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
    if (ImGui::BeginMenu("File")) {
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
    if (ImGui::BeginMenu("Edit")) {
      menu_item_cmd("Undo", UiCommand::Undo, cmd, true, "Ctrl+Z");
      menu_item_cmd("Redo", UiCommand::Redo, cmd, true, "Ctrl+Y");
      ImGui::Separator();
      menu_item_cmd("Cut", UiCommand::Cut, cmd, state.selection, "Ctrl+X");
      menu_item_cmd("Copy", UiCommand::Copy, cmd, state.selection, "Ctrl+C");
      menu_item_cmd("Paste", UiCommand::Paste, cmd, true, "Ctrl+V");
      menu_item_cmd("Select All", UiCommand::SelectAll, cmd, true, "Ctrl+Shift+A");
      ImGui::Separator();
      menu_item_cmd("Find…", UiCommand::Find, cmd, true, "Ctrl+F");
      menu_item_cmd("Replace…", UiCommand::Replace, cmd, true, "Ctrl+H");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Format")) {
      menu_item_cmd("Bold", UiCommand::Bold, cmd, state.selection, "Ctrl+B");
      menu_item_cmd("Italic", UiCommand::Italic, cmd, state.selection, "Ctrl+I");
      menu_item_cmd("Code", UiCommand::Code, cmd, state.selection, "Ctrl+`");
      menu_item_cmd("Link…", UiCommand::Link, cmd, state.selection, "Ctrl+K");
      ImGui::Separator();
      menu_item_cmd("Paragraph", UiCommand::InsertParagraph, cmd);
      menu_item_cmd("Horizontal Rule", UiCommand::InsertHr, cmd);
      menu_item_cmd("Image…", UiCommand::InsertImage, cmd, true, "Ctrl+Shift+I");
      menu_item_cmd("Note", UiCommand::InsertNote, cmd);
      menu_item_cmd("Code Block", UiCommand::InsertCodeBlock, cmd);
      menu_item_cmd("Table", UiCommand::InsertTable, cmd);
      menu_item_cmd("Definition List", UiCommand::InsertDefList, cmd);
      ImGui::Separator();
      menu_item_cmd("Unordered List", UiCommand::ToggleUnorderedList, cmd, true,
                    "Ctrl+Shift+8");
      menu_item_cmd("Ordered List", UiCommand::ToggleOrderedList, cmd, true,
                    "Ctrl+Shift+7");
      menu_item_cmd("Block Quote", UiCommand::ToggleBlockQuote, cmd);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Split Pane", "Ctrl+\\", state.split))
        cmd = UiCommand::ToggleSplit;
      if (ImGui::MenuItem("Heading Outline", "Ctrl+Shift+O", state.outline))
        cmd = UiCommand::ToggleOutline;
      if (ImGui::MenuItem("Diff Marks", "Ctrl+Shift+D", state.diff))
        cmd = UiCommand::ToggleDiff;
      ImGui::Separator();
      menu_item_cmd("Zoom In", UiCommand::ZoomIn, cmd, true, "Ctrl++");
      menu_item_cmd("Zoom Out", UiCommand::ZoomOut, cmd, true, "Ctrl+-");
      ImGui::Separator();
      if (ImGui::MenuItem("Keyboard Help", "F1", state.help))
        cmd = UiCommand::ToggleHelp;
      ImGui::Separator();
      menu_item_cmd("FORMAT.md in window", UiCommand::OpenFormatDoc, cmd);
      menu_item_cmd("semantic.sexp in window", UiCommand::OpenSemanticDoc, cmd);
      menu_item_cmd("Open path in window…", UiCommand::OpenInWindow, cmd);
      ImGui::EndMenu();
    }

    // Document label on the right of the menu bar
    if (!state.doc_label.empty()) {
      std::string right = state.doc_label;
      if (state.dirty) right = "*" + right;
      float w = ImGui::CalcTextSize(right.c_str()).x;
      float avail = ImGui::GetWindowWidth();
      if (avail > w + 24.f) {
        ImGui::SameLine(avail - w - 12.f);
        ImGui::TextDisabled("%s", right.c_str());
      }
    }

    result.top_reserved = ImGui::GetFrameHeight();
    ImGui::EndMainMenuBar();
  }

  // ---- Toolbar under the menu (icon strip) ----
  const float toolbar_h = 32.f;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + result.top_reserved));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, toolbar_h));
  ImGuiWindowFlags tb_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
  if (ImGui::Begin("##toolbar", nullptr, tb_flags)) {
    tool_button("tb_save", "save", "Save (Ctrl+S)", UiCommand::Save, cmd);
    tool_button("tb_undo", "undo", "Undo (Ctrl+Z)", UiCommand::Undo, cmd);
    tool_button("tb_redo", "redo", "Redo (Ctrl+Y)", UiCommand::Redo, cmd);

    ImGui::TextDisabled("|");
    ImGui::SameLine(0.f, 4.f);

    tool_button("tb_cut", "cut", "Cut (Ctrl+X)", UiCommand::Cut, cmd,
                state.selection);
    tool_button("tb_bold", "bold", "Bold (Ctrl+B)", UiCommand::Bold, cmd,
                state.selection);
    tool_button("tb_italic", "italic", "Italic (Ctrl+I)", UiCommand::Italic,
                cmd, state.selection);
    tool_button("tb_code", "code", "Code (Ctrl+`)", UiCommand::Code, cmd,
                state.selection);
    tool_button("tb_link", "link", "Link (Ctrl+K)", UiCommand::Link, cmd,
                state.selection);

    ImGui::TextDisabled("|");
    ImGui::SameLine(0.f, 4.f);

    tool_button("tb_list", "list", "Unordered list", UiCommand::ToggleUnorderedList,
                cmd);
    tool_button("tb_olist", "olist", "Ordered list", UiCommand::ToggleOrderedList,
                cmd);
    tool_button("tb_quote", "quote", "Block quote", UiCommand::ToggleBlockQuote,
                cmd);
    tool_button("tb_image", "image", "Insert image (Ctrl+Shift+I)",
                UiCommand::InsertImage, cmd);

    ImGui::TextDisabled("|");
    ImGui::SameLine(0.f, 4.f);

    tool_button("tb_split", "split", "Split pane (Ctrl+\\)", UiCommand::ToggleSplit,
                cmd);
    tool_button("tb_find", "find", "Find (Ctrl+F)", UiCommand::Find, cmd);
    tool_button("tb_help", "help", "Help (F1)", UiCommand::ToggleHelp, cmd);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  result.top_reserved += toolbar_h;
  result.cmd = cmd;
  return result;
}

void Ui::end_frame(SDL_Renderer* renderer) {
  if (!ready_) return;
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

}  // namespace xanatoo
