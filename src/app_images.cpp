// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Remote images: ImageCache queues the http(s) sources layout asks for;
// a few detached workers download each one into cache_dir()/images/ and
// prebuild its inline thumbnail (stb builds; thumtoo makes it on request),
// then the main thread marks it done and relayouts (the image's size
// changes from placeholder to real). Thumbnails finished by thumtoo also
// arrive here (ImageCache::poll).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "xanatoo/app.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
#include "xanatoo/https.hpp"
#endif

namespace fs = std::filesystem;

namespace xanatoo::app {

namespace {

constexpr int kMaxWorkers = 4;
constexpr uint64_t kImageCacheBytes = 256ull * 1024 * 1024;
constexpr uint64_t kThumbCacheBytes = 128ull * 1024 * 1024;

// Download `url` to its cache file and build the inline thumbnail.
bool download_image(const std::string& url) {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
  std::string fetch = url;
  if (starts_with_ci(fetch, "http://")) fetch = "https://" + fetch.substr(7);
  std::string err;
  auto r = xanatoo::https_get_follow(fetch, "xanatoo/0.1 (image fetch)", &err);
  if (!r || r->status != 200 || r->body.empty()) {
    std::cerr << "image: " << url << ": "
              << (r ? "HTTP " + std::to_string(r->status) : err) << "\n";
    return false;
  }
  // https_request stops reading at 12 MiB: anything that size is cut off.
  if (r->body.size() >= 12 * 1024 * 1024) {
    std::cerr << "image: " << url << ": too large\n";
    return false;
  }
  const std::string path = xanatoo::remote_image_path(url);
  if (path.empty()) return false;  // no cache directory
  const std::string tmp = path + ".part";
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  {
    std::ofstream out(tmp, std::ios::binary);
    out.write(r->body.data(), static_cast<std::streamsize>(r->body.size()));
    if (!out) {
      std::cerr << "image: cannot write " << tmp << "\n";
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) return false;
#if defined(XANATOO_THUMTOO) && XANATOO_THUMTOO
  return true;  // thumtoo builds the thumbnail when layout asks for it
#else
  return xanatoo::build_thumb_file(path, xanatoo::kRemoteThumbEdge);
#endif
#else
  (void)url;
  return false;
#endif
}

void image_worker(std::shared_ptr<ImageFetchShared> sh) {
  for (;;) {
    std::string url;
    {
      std::lock_guard<std::mutex> lock(sh->mutex);
      if (sh->queue.empty()) {
        --sh->workers;
        return;
      }
      url = std::move(sh->queue.front());
      sh->queue.pop_front();
    }
    const bool ok = download_image(url);
    std::lock_guard<std::mutex> lock(sh->mutex);
    sh->done.emplace_back(std::move(url), ok);
  }
}

}  // namespace

void poll_images(App& app) {
  ImageFetchShared& sh = *app.image_fetch;
  std::vector<std::pair<std::string, bool>> done;
  {
    std::lock_guard<std::mutex> lock(sh.mutex);
    for (std::string& url : app.images.take_requests())
      sh.queue.push_back(std::move(url));
    while (sh.workers < kMaxWorkers &&
           static_cast<size_t>(sh.workers) < sh.queue.size()) {
      ++sh.workers;
      std::thread(image_worker, app.image_fetch).detach();
    }
    done.swap(sh.done);
  }
  const bool thumbs = app.images.poll();
  if (done.empty() && !thumbs) return;
  for (const auto& [url, ok] : done) app.images.remote_done(url, ok);
  // Placeholders became images (or vanished): heights changed.
  for (Pane& p : app.panes) p.height_cache.clear();
  app.needs_layout = true;
}

void prune_image_caches() {
  const std::string dir = cache_dir();
  if (dir.empty()) return;
  std::thread([dir] {
    prune_cache_dir((fs::path(dir) / "images").string(), kImageCacheBytes);
    prune_cache_dir((fs::path(dir) / "thumbs").string(), kThumbCacheBytes);
  }).detach();
}

}  // namespace xanatoo::app
