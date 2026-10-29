// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/images.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#include "xanatoo/image_backend.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <utility>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace xanatoo {

ImageCache::ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {
#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO
  backend_ = image_backend_open();
#endif
}

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

namespace {

// Downloaded files are untrusted: refuse absurd dimensions before decoding.
constexpr long long kMaxPixels = 64ll * 1024 * 1024;

unsigned char* load_rgba(const std::string& path, int& w, int& h) {
  int comp = 0;
  if (stbi_info(path.c_str(), &w, &h, &comp) &&
      static_cast<long long>(w) * h > kMaxPixels) {
    std::cerr << "image too large: " << path << " (" << w << "x" << h << ")\n";
    return nullptr;
  }
  return stbi_load(path.c_str(), &w, &h, &comp, 4);
}

}  // namespace

Image ImageCache::load(const std::string& resolved_path) {
#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO
  if (backend_) {
    std::vector<unsigned char> px;
    int pw = 0, ph = 0;
    if (!image_backend_decode(resolved_path, kMaxPixels, px, pw, ph)) return {};
    return to_texture(px.data(), pw, ph);
  }
#endif
  int w = 0, h = 0;
  unsigned char* data = load_rgba(resolved_path, w, h);
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
      resolved + "\n" + std::to_string(size) + "\n" + std::to_string(static_cast<long long>(mtime)));
  char name[64];
  std::snprintf(name, sizeof(name), "%016zx-%d.bmp", h, edge);
  const std::string cache = cache_dir();
  if (cache.empty()) return {};
  return (fs::path(cache) / "thumbs" / name).string();
}

}  // namespace

namespace {

// Decoded thumbnail pixels of `resolved` (longer edge ≤ edge), or empty.
std::vector<unsigned char> thumb_pixels(const std::string& resolved, int edge,
                                        int& tw, int& th) {
  int w = 0, h = 0;
  unsigned char* data = load_rgba(resolved, w, h);
  if (!data) {
    std::cerr << "thumbnail: cannot load " << resolved << " ("
              << stbi_failure_reason() << ")\n";
    return {};
  }
  tw = w;
  th = h;
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
  return small;
}

// Write via a temporary name so a reader never sees half a file.
void save_thumb(const std::string& cached, std::vector<unsigned char>& px,
                int w, int h) {
  std::error_code ec;
  fs::create_directories(fs::path(cached).parent_path(), ec);
  const std::string tmp = cached + ".tmp";
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
      px.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
  if (!surf) return;
  if (SDL_SaveBMP(surf, tmp.c_str()) != 0) {
    std::cerr << "thumbnail: cannot write " << cached << "\n";
  } else {
    fs::rename(tmp, cached, ec);
  }
  SDL_FreeSurface(surf);
}

}  // namespace

bool build_thumb_file(const std::string& resolved, int edge) {
  const std::string cached = thumb_cache_path(resolved, edge);
  if (cached.empty()) return false;
  if (fs::exists(cached)) return true;
  int tw = 0, th = 0;
  std::vector<unsigned char> px = thumb_pixels(resolved, edge, tw, th);
  if (px.empty()) return false;
  save_thumb(cached, px, tw, th);
  return fs::exists(cached);
}

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
  int tw = 0, th = 0;
  std::vector<unsigned char> px = thumb_pixels(resolved, edge, tw, th);
  if (px.empty()) return {};
  if (!cached.empty()) save_thumb(cached, px, tw, th);
  return to_texture(px.data(), tw, th);
}

bool is_remote_image(std::string_view src) {
  return starts_with_ci(src, "https://") || starts_with_ci(src, "http://");
}

std::string remote_image_path(std::string_view url) {
  // FNV-1a: stable across builds, unlike std::hash.
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : url) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char name[32];
  std::snprintf(name, sizeof(name), "%016llx",
                static_cast<unsigned long long>(h));
  const std::string cache = cache_dir();
  if (cache.empty()) return {};
  return (fs::path(cache) / "images" / name).string();
}

void prune_cache_dir(const std::string& dir, uint64_t max_bytes) {
  std::error_code ec;
  struct Entry {
    fs::path path;
    fs::file_time_type mtime;
    uint64_t size;
  };
  std::vector<Entry> files;
  uint64_t total = 0;
  for (const auto& de : fs::directory_iterator(dir, ec)) {
    if (!de.is_regular_file(ec)) continue;
    Entry e{de.path(), de.last_write_time(ec), de.file_size(ec)};
    if (ec) continue;
    total += e.size;
    files.push_back(std::move(e));
  }
  if (total <= max_bytes) return;
  std::sort(files.begin(), files.end(),
            [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
  for (const Entry& e : files) {
    if (total <= max_bytes) break;
    if (fs::remove(e.path, ec)) total -= e.size;
  }
}

std::vector<std::string> ImageCache::take_requests() {
  return std::exchange(requests_, {});
}

void ImageCache::remote_done(const std::string& url, bool ok) {
  pending_.erase(url);
  if (!ok) failed_.insert(url);
}

const Image* ImageCache::remote_placeholder(const std::string& url) {
  static const Image kPending{nullptr, 0, 0, false, true};
  static const Image kFailed{};
  if (failed_.count(url)) return &kFailed;
  if (pending_.count(url)) return &kPending;
  std::error_code ec;
  const std::string p = remote_image_path(url);
  if (p.empty()) return &kFailed;  // no cache directory to download into
  if (fs::exists(p, ec)) return nullptr;
  pending_.insert(url);
  requests_.push_back(url);
  return &kPending;
}

std::string ImageCache::resolve(const std::string& path,
                                const std::string& base) const {
  if (is_remote_image(path)) return remote_image_path(path);
  const std::string& root = !base.empty() ? base : base_dir_;
  fs::path resolved = path;
  if (!resolved.is_absolute() && !root.empty()) resolved = fs::path(root) / path;
  return resolved.string();
}

const Image& ImageCache::thumb(const std::string& path, const std::string& base,
                               int max_px) {
  const int edge = max_px <= 128 ? 128 : max_px <= 256 ? 256 : 512;
  if (is_remote_image(path))
    if (const Image* ph = remote_placeholder(path)) return *ph;
  const std::string resolved = resolve(path, base);
  std::string key = resolved;
  key.push_back('\0');
  key += "#thumb" + std::to_string(edge);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO
  if (backend_) {
    static const Image kBuilding{nullptr, 0, 0, false, true};
    if (thumb_pending_.insert(key).second)
      image_backend_request_thumb(*backend_, key, resolved, edge);
    return kBuilding;
  }
#endif
  auto [ins, _] = cache_.emplace(key, load_thumb(resolved, edge));
  return ins->second;
}

bool ImageCache::poll() {
#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO
  if (!backend_) return false;
  std::vector<DecodedImage> done = image_backend_take_done(*backend_);
  for (DecodedImage& d : done) {
    thumb_pending_.erase(d.key);
    cache_[d.key] = d.rgba.empty() ? Image{} : to_texture(d.rgba.data(), d.w, d.h);
  }
  return !done.empty();
#else
  return false;
#endif
}

const Image& ImageCache::get(const std::string& path, const std::string& base) {
  if (is_remote_image(path)) {
    if (auto it = cache_.find(path); it != cache_.end()) return it->second;
    if (const Image* ph = remote_placeholder(path)) return *ph;
    auto [ins, _] = cache_.emplace(path, load(remote_image_path(path)));
    return ins->second;
  }
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
