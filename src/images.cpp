// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/images.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#include "xanatoo/util.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace xanatoo {

ImageCache::ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {}

ImageCache::~ImageCache() {
  clear();
}

void ImageCache::set_base_dir(const std::string& dir) {
  base_dir_ = dir;
}

void ImageCache::clear() {
  for (auto& [k, img] : cache_) {
    if (img.texture) SDL_DestroyTexture(img.texture);
  }
  cache_.clear();
}

Image ImageCache::to_texture(const unsigned char* rgba, int w, int h) {
  Image img;
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
      const_cast<unsigned char*>(rgba), w, h, 32, w * 4,
      SDL_PIXELFORMAT_RGBA32);
  if (!surf) return img;
  img.texture = SDL_CreateTextureFromSurface(renderer_, surf);
  SDL_FreeSurface(surf);
  if (!img.texture) return img;
  SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
  img.width = w;
  img.height = h;
  img.ok = true;
  return img;
}

Image ImageCache::load(const std::string& resolved_path) {
  int w = 0, h = 0, comp = 0;
  unsigned char* data = stbi_load(resolved_path.c_str(), &w, &h, &comp, 4);
  if (!data) {
    std::cerr << "image load failed: " << resolved_path
              << " (" << stbi_failure_reason() << ")\n";
    return {};
  }
  Image img = to_texture(data, w, h);
  stbi_image_free(data);
  return img;
}

namespace {

// Box filter: each target pixel averages the source pixels it covers.
std::vector<unsigned char> downscale(const unsigned char* src, int sw, int sh,
                                     int dw, int dh) {
  std::vector<unsigned char> out(static_cast<size_t>(dw) * dh * 4);
  for (int y = 0; y < dh; ++y) {
    const int y0 = y * sh / dh;
    const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);
    for (int x = 0; x < dw; ++x) {
      const int x0 = x * sw / dw;
      const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);
      unsigned sum[4] = {0, 0, 0, 0};
      for (int yy = y0; yy < y1; ++yy) {
        const unsigned char* row = src + (static_cast<size_t>(yy) * sw + x0) * 4;
        for (int xx = x0; xx < x1; ++xx, row += 4)
          for (int c = 0; c < 4; ++c) sum[c] += row[c];
      }
      const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
      unsigned char* d = &out[(static_cast<size_t>(y) * dw + x) * 4];
      for (int c = 0; c < 4; ++c) d[c] = static_cast<unsigned char>(sum[c] / n);
    }
  }
  return out;
}

std::string thumb_cache_path(const std::string& resolved, int edge) {
  std::error_code ec;
  const auto size = fs::file_size(resolved, ec);
  if (ec) return {};
  const auto mtime = fs::last_write_time(resolved, ec).time_since_epoch().count();
  if (ec) return {};
  const size_t h = std::hash<std::string>{}(
      resolved + "\n" + std::to_string(size) + "\n" + std::to_string(mtime));
  char name[64];
  std::snprintf(name, sizeof(name), "%016zx-%d.bmp", h, edge);
  return (fs::path(cache_dir()) / "thumbs" / name).string();
}

}  // namespace

Image ImageCache::load_thumb(const std::string& resolved, int edge) {
  const std::string cached = thumb_cache_path(resolved, edge);
  if (!cached.empty() && fs::exists(cached)) {
    int w = 0, h = 0, comp = 0;
    if (unsigned char* data = stbi_load(cached.c_str(), &w, &h, &comp, 4)) {
      Image img = to_texture(data, w, h);
      stbi_image_free(data);
      if (img.ok) return img;
    }
  }
  int w = 0, h = 0, comp = 0;
  unsigned char* data = stbi_load(resolved.c_str(), &w, &h, &comp, 4);
  if (!data) {
    std::cerr << "thumbnail: cannot load " << resolved << " ("
              << stbi_failure_reason() << ")\n";
    return {};
  }
  int tw = w, th = h;
  if (std::max(w, h) > edge) {
    if (w >= h) {
      tw = edge;
      th = std::max(1, h * edge / w);
    } else {
      th = edge;
      tw = std::max(1, w * edge / h);
    }
  }
  std::vector<unsigned char> small =
      (tw == w && th == h)
          ? std::vector<unsigned char>(data, data + static_cast<size_t>(w) * h * 4)
          : downscale(data, w, h, tw, th);
  stbi_image_free(data);
  if (!cached.empty()) {
    std::error_code ec;
    fs::create_directories(fs::path(cached).parent_path(), ec);
    if (SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            small.data(), tw, th, 32, tw * 4, SDL_PIXELFORMAT_RGBA32)) {
      if (SDL_SaveBMP(surf, cached.c_str()) != 0)
        std::cerr << "thumbnail: cannot write " << cached << "\n";
      SDL_FreeSurface(surf);
    }
  }
  return to_texture(small.data(), tw, th);
}

std::string ImageCache::resolve(const std::string& path,
                                const std::string& base) const {
  const std::string& root = !base.empty() ? base : base_dir_;
  fs::path resolved = path;
  if (!resolved.is_absolute() && !root.empty()) resolved = fs::path(root) / path;
  return resolved.string();
}

const Image& ImageCache::thumb(const std::string& path, const std::string& base,
                               int max_px) {
  const int edge = max_px <= 128 ? 128 : max_px <= 256 ? 256 : 512;
  const std::string resolved = resolve(path, base);
  std::string key = resolved;
  key.push_back('\0');
  key += "#thumb" + std::to_string(edge);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  auto [ins, _] = cache_.emplace(key, load_thumb(resolved, edge));
  return ins->second;
}

const Image& ImageCache::get(const std::string& path, const std::string& base) {
  const std::string& root = !base.empty() ? base : base_dir_;
  // Cache key includes root so the same relative name from two docs does not clash.
  // Separator must be a real NUL: root + "\0" is a no-op (C string stops at NUL).
  std::string key = root;
  key.push_back('\0');
  key += path;
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  fs::path resolved = path;
  if (!resolved.is_absolute() && !root.empty()) {
    resolved = fs::path(root) / path;
  }

  Image img = load(resolved.string());
  auto [ins, _] = cache_.emplace(key, std::move(img));
  return ins->second;
}

}  // namespace xanatoo
