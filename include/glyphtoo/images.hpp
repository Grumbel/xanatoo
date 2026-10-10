// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace glyphtoo {

struct Image {
  SDL_Texture* texture = nullptr;
  int width = 0;
  int height = 0;
  bool ok = false;
};

// Loads and caches images by path. Paths are resolved relative to a base
// directory (typically the document's parent folder).
class ImageCache {
public:
  explicit ImageCache(SDL_Renderer* renderer);
  ~ImageCache();

  ImageCache(const ImageCache&) = delete;
  ImageCache& operator=(const ImageCache&) = delete;

  void set_base_dir(const std::string& dir);
  const std::string& base_dir() const { return base_dir_; }

  // Returns a cached entry (may have ok=false if load failed).
  // If base is non-empty, relative paths resolve against it instead of base_dir_.
  const Image& get(const std::string& path, const std::string& base = {});

  void clear();

private:
  SDL_Renderer* renderer_ = nullptr;
  std::string base_dir_;
  std::unordered_map<std::string, Image> cache_;

  Image load(const std::string& resolved_path);
};

}  // namespace glyphtoo
