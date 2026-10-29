// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <vector>

namespace xanatoo {

struct ImageBackend;  // image_backend.hpp

struct Image {
  SDL_Texture* texture = nullptr;
  int width = 0;
  int height = 0;
  bool ok = false;
  bool pending = false;  // remote source downloading / thumbnail building
};

// Remote (http / https) image sources are downloaded by the app's image
// fetcher (app_images.cpp) into cache_dir()/images/ and then served like
// local files. ImageCache itself never touches the network: it queues the
// URLs it is asked for (take_requests) and reports them pending until the
// fetcher calls remote_done.
bool is_remote_image(std::string_view src);
// Download target of a remote image (cache_dir()/images/<hash>).
std::string remote_image_path(std::string_view url);
// Thumbnail edge used for inline remote images; the fetcher prebuilds it.
constexpr int kRemoteThumbEdge = 512;

// Decode `resolved` and write its thumbnail cache file (see
// ImageCache::thumb). No SDL renderer involved: safe on a worker thread.
bool build_thumb_file(const std::string& resolved, int edge);

// Delete the oldest files (by mtime) under `dir` until it holds at most
// `max_bytes`. For cache directories only.
void prune_cache_dir(const std::string& dir, uint64_t max_bytes);

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

  // Downscaled copy whose longer edge is at most `max_px` (rounded up to
  // 128 / 256 / 512). Thumbnails are a cache, never document data. With
  // thumtoo they come from its tile cache and arrive asynchronously
  // (pending until poll); else built from the original on first use and
  // kept under cache_dir()/thumbs/, keyed by path + size + mtime.
  // width/height are the thumbnail's.
  const Image& thumb(const std::string& path, const std::string& base,
                     int max_px);

  // Resolved filesystem path for `path` (relative → base or base_dir_,
  // remote → its download target).
  std::string resolve(const std::string& path, const std::string& base = {}) const;

  // Remote URLs requested since the last call (each URL once).
  std::vector<std::string> take_requests();
  // The fetcher finished `url`: its file is in place (ok) or never will be.
  void remote_done(const std::string& url, bool ok);

  // Thumbnails finished in the background → textures. True when any
  // arrived (their size changed from the placeholder: relayout).
  bool poll();

  void clear();

private:
  SDL_Renderer* renderer_ = nullptr;
  std::shared_ptr<ImageBackend> backend_;   // null: stb_image only
  std::unordered_set<std::string> thumb_pending_;  // keys of building thumbs
  std::string base_dir_;
  std::unordered_map<std::string, Image> cache_;
  std::unordered_set<std::string> pending_;  // remote URLs being fetched
  std::unordered_set<std::string> failed_;   // remote URLs that failed
  std::vector<std::string> requests_;

  // Remote `url` not ready yet: the pending / failed placeholder, else null.
  const Image* remote_placeholder(const std::string& url);

  Image load(const std::string& resolved_path);
  Image load_thumb(const std::string& resolved_path, int edge);
  Image to_texture(const unsigned char* rgba, int w, int h);
};

}  // namespace xanatoo
