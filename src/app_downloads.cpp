// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/app.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace xanatoo::app {
namespace {

int row_height(const App& app) {
  return std::max(58, app.ui_font.line_height() * 2 + 20);
}

std::vector<std::shared_ptr<xanatoo::DownloadProgress>> visible(const App& app) {
  std::vector<std::shared_ptr<xanatoo::DownloadProgress>> result;
  for (auto it = app.downloads.rbegin(); it != app.downloads.rend(); ++it) {
    const auto phase = (*it)->phase.load(std::memory_order_acquire);
    if (phase != xanatoo::DownloadPhase::Pending &&
        phase != xanatoo::DownloadPhase::Skipped)
      result.push_back(*it);
  }
  return result;
}

std::string bytes_label(uint64_t n) {
  char s[48];
  if (n >= 1048576)
    std::snprintf(s, sizeof(s), "%.1f MiB", static_cast<double>(n) / 1048576.0);
  else if (n >= 1024)
    std::snprintf(s, sizeof(s), "%.1f KiB", static_cast<double>(n) / 1024.0);
  else
    std::snprintf(s, sizeof(s), "%llu B", static_cast<unsigned long long>(n));
  return s;
}

std::string status(const xanatoo::DownloadProgress& d,
                   xanatoo::DownloadPhase phase) {
  const uint64_t n = d.received.load(std::memory_order_relaxed);
  const int64_t total = d.total.load(std::memory_order_relaxed);
  if (phase == xanatoo::DownloadPhase::Active) {
    return total > 0 ? bytes_label(n) + " / " + bytes_label(total) :
                       bytes_label(n) + " received";
  }
  if (phase == xanatoo::DownloadPhase::Complete)
    return "Finished · " + bytes_label(n) + " · " + d.path;
  if (phase == xanatoo::DownloadPhase::Cancelled) return "Cancelled";
  return "Failed: " + d.error;
}

std::string folder_uri(const std::string& filename) {
  namespace fs = std::filesystem;
  const std::string folder = fs::path(filename).parent_path().generic_string();
  std::string uri = "file://";
  static constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : folder) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '.' ||
        c == '_' || c == '~')
      uri.push_back(static_cast<char>(c));
    else {
      uri.push_back('%');
      uri.push_back(hex[c >> 4]);
      uri.push_back(hex[c & 15]);
    }
  }
  return uri;
}

}  // namespace

void load_download_history(App& app) {
  if (xanatoo::no_save()) return;
  const std::string dir = xanatoo::state_dir();
  if (dir.empty()) return;
  std::ifstream in(std::filesystem::path(dir) / "downloads.conf");
  std::string line;
  size_t count = 0;
  std::vector<std::shared_ptr<xanatoo::DownloadProgress>> loaded;
  while (count < 100 && std::getline(in, line)) {
    std::istringstream s(line);
    std::string path, url;
    uint64_t bytes = 0;
    if (!(s >> std::quoted(path) >> std::quoted(url) >> bytes)) continue;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) continue;
    auto d = std::make_shared<xanatoo::DownloadProgress>();
    d->path = path;
    d->name = std::filesystem::path(path).filename().string();
    d->final_name = d->name;
    d->url = url;
    d->received.store(bytes);
    d->total.store(static_cast<int64_t>(bytes));
    d->announced.store(true);
    d->recorded.store(true);
    d->phase.store(xanatoo::DownloadPhase::Complete, std::memory_order_release);
    loaded.push_back(std::move(d));
    ++count;
  }
  for (auto it = loaded.rbegin(); it != loaded.rend(); ++it)
    app.downloads.push_back(*it);
}

void save_download_history(const App& app) {
  if (xanatoo::no_save()) return;
  const std::string dir = xanatoo::state_dir();
  if (dir.empty() || !xanatoo::ensure_dir(dir)) return;
  std::ostringstream out;
  size_t count = 0;
  for (auto it = app.downloads.rbegin();
       it != app.downloads.rend() && count < 100; ++it) {
    const auto& d = **it;
    if (d.phase.load(std::memory_order_acquire) !=
            xanatoo::DownloadPhase::Complete || d.path.empty() ||
        d.path.find('\n') != std::string::npos ||
        d.url.find('\n') != std::string::npos) continue;
    out << std::quoted(d.path) << ' ' << std::quoted(d.url) << ' '
        << d.received.load(std::memory_order_relaxed) << '\n';
    ++count;
  }
  xanatoo::write_file_atomic(
      (std::filesystem::path(dir) / "downloads.conf").string(), out.str());
}

void draw_downloads_panel(App& app) {
  if (!app.downloads_panel_open) return;
  FloatWindow& w = app.downloads_win;
  const auto entries = visible(app);
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  const int step = row_height(app);
  const float max_scroll = std::max(0.f, static_cast<float>(entries.size() * step + 8 - ch));
  w.scroll = std::clamp(w.scroll, 0.f, max_scroll);
  draw_floating_frame(app, SDL_Rect{w.x, w.y, w.w, w.h}, "Downloads", false,
                      w.dock == Dock::None, 0, w.dock == Dock::Max);
  const auto& pal = xanatoo::theme::palette();
  SDL_Rect clip{cx, cy, cw, ch};
  xanatoo::sdl::set_clip_rect(app.sdl, &clip);
  if (entries.empty()) {
    app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 10),
        static_cast<float>(cy + 14 + app.ui_font.ascender()),
        "No downloads this session", pal.text_dim.r, pal.text_dim.g,
        pal.text_dim.b);
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    const int ry = cy + static_cast<int>(i * step - w.scroll);
    if (ry + step < cy || ry > cy + ch) continue;
    const auto& d = *entries[i];
    const auto phase = d.phase.load(std::memory_order_acquire);
    if (i % 2 == 1)
      xanatoo::theme::fill(app.sdl, SDL_Rect{cx, ry, cw, step}, pal.face_dark);
    const int textw = std::max(24, cw - 72);
    const std::string name = phase == xanatoo::DownloadPhase::Complete ?
        d.final_name : d.name;
    app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 10),
        static_cast<float>(ry + 8 + app.ui_font.ascender()),
        fit_text(app.ui_font, name, textw), pal.text.r, pal.text.g, pal.text.b);
    const std::string state = status(d, phase);
    app.renderer.draw_text(app.ui_font, static_cast<float>(cx + 10),
        static_cast<float>(ry + 12 + app.ui_font.line_height() + app.ui_font.ascender()),
        fit_text(app.ui_font, state, textw), pal.text_dim.r, pal.text_dim.g,
        pal.text_dim.b);
    if (phase == xanatoo::DownloadPhase::Active) {
      const int width = std::max(12, cw - 32);
      const int y = ry + step - 7;
      xanatoo::theme::fill(app.sdl, SDL_Rect{cx + 9, y, width, 4}, pal.face_dark);
      const int64_t total = d.total.load(std::memory_order_relaxed);
      const uint64_t bytes = d.received.load(std::memory_order_relaxed);
      if (total > 0) {
        const float fraction = std::clamp(
            static_cast<float>(bytes) / static_cast<float>(total), 0.f, 1.f);
        xanatoo::theme::fill(app.sdl, SDL_Rect{cx + 9, y,
            std::max(1, static_cast<int>(width * fraction)), 4}, pal.accent);
      } else {
        const int segment = std::max(10, width / 5);
        const int start = static_cast<int>((xanatoo::sdl::ticks() / 15) %
                          static_cast<Uint32>(width + segment)) - segment;
        const int left = std::max(0, start);
        const int right = std::min(width, start + segment);
        if (right > left)
          xanatoo::theme::fill(app.sdl,
              SDL_Rect{cx + 9 + left, y, right - left, 4}, pal.accent);
      }
    }
    if (phase == xanatoo::DownloadPhase::Active ||
        phase == xanatoo::DownloadPhase::Complete) {
      const char* label = phase == xanatoo::DownloadPhase::Active ? "×" : "Dir";
      const int right = cx + cw - 49;
      xanatoo::theme::fill(app.sdl,
          SDL_Rect{right, ry + 8, 38, std::min(step - 15, 30)}, pal.face_dark);
      app.renderer.draw_text(app.ui_font, static_cast<float>(right + 6),
          static_cast<float>(ry + 10 + app.ui_font.ascender()), label,
          pal.text.r, pal.text.g, pal.text.b);
    }
  }
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
  draw_client_scrollbar(app, cx, cy, cw, ch, w.scroll, max_scroll);
}

void click_downloads_panel(App& app, int mx, int my) {
  FloatWindow& w = app.downloads_win;
  auto [cx, cy, cw, ch] = float_client_rect(app, w);
  if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;
  const int row = static_cast<int>((my - cy + w.scroll) / row_height(app));
  const auto entries = visible(app);
  if (row < 0 || row >= static_cast<int>(entries.size())) return;
  auto& d = *entries[static_cast<size_t>(row)];
  const auto phase = d.phase.load(std::memory_order_acquire);
  if (phase == xanatoo::DownloadPhase::Active && mx >= cx + cw - 58) {
    d.cancel.store(true, std::memory_order_relaxed);
    flash_status(app, "Cancelling " + d.name);
  } else if (phase == xanatoo::DownloadPhase::Complete && !d.path.empty()) {
    if (mx >= cx + cw - 58) {
      const std::string uri = folder_uri(d.path);
      if (!xanatoo::sdl::open_url(uri.c_str()))
        flash_status(app, "Could not open Downloads folder");
    } else {
      SDL_SetClipboardText(d.path.c_str());
      flash_status(app, "Copied path: " + d.path);
    }
  } else if (phase == xanatoo::DownloadPhase::Failed) {
    flash_status(app, d.error);
  }
}

}  // namespace xanatoo::app
