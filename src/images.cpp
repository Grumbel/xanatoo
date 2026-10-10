// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "glyphtoo/images.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace glyphtoo {

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

Image ImageCache::load(const std::string& resolved_path) {
  Image img;
  int w = 0, h = 0, comp = 0;
  unsigned char* data = stbi_load(resolved_path.c_str(), &w, &h, &comp, 4);
  if (!data) {
    std::cerr << "image load failed: " << resolved_path
              << " (" << stbi_failure_reason() << ")\n";
    return img;
  }

  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
      data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
  if (!surf) {
    stbi_image_free(data);
    return img;
  }

  img.texture = SDL_CreateTextureFromSurface(renderer_, surf);
  SDL_FreeSurface(surf);
  stbi_image_free(data);

  if (!img.texture) return img;

  SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
  img.width = w;
  img.height = h;
  img.ok = true;
  return img;
}

const Image& ImageCache::get(const std::string& path) {
  auto it = cache_.find(path);
  if (it != cache_.end()) return it->second;

  fs::path resolved = path;
  if (!resolved.is_absolute() && !base_dir_.empty()) {
    resolved = fs::path(base_dir_) / path;
  }

  Image img = load(resolved.string());
  auto [ins, _] = cache_.emplace(path, std::move(img));
  return ins->second;
}

}  // namespace glyphtoo
