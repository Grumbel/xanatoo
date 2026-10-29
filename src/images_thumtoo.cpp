// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// thumtoo image backend (see image_backend.hpp). A thumbnail request runs on
// thumtoo's workers: size probe (cached), then the cache-only TileSynth
// lookup, and on a miss one tile pyramid from the scale that still covers
// kMaxThumbEdge up; every waiting edge of that file is then composed from
// the tiles. Callbacks run inline on the worker (default Executor); results
// are handed to the main thread through `done`.

#include "xanatoo/image_backend.hpp"

#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include <thumtoo/client.hpp>
#include <thumtoo/constants.hpp>
#include <thumtoo/image.hpp>
#include <thumtoo/uri.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <set>

namespace fs = std::filesystem;

namespace xanatoo {

namespace {

// Largest thumbnail edge ImageCache asks for (its 128 / 256 / 512 steps).
constexpr int kMaxThumbEdge = 512;
constexpr unsigned kWorkers = 2;

// sRGB + alpha, 8 bit, EXIF orientation applied; consumes `img`.
bool vips_to_rgba(VipsImage* img, std::vector<unsigned char>& rgba, int& w,
                  int& h) {
  // Replaces `img` by the operation's output (read after the call).
  VipsImage* out = nullptr;
  auto step = [&img, &out](int rc) {
    g_object_unref(img);
    img = out;
    out = nullptr;
    return rc == 0 && img;
  };
  if (!step(vips_autorot(img, &out, nullptr))) return false;
  if (!step(vips_colourspace(img, &out, VIPS_INTERPRETATION_sRGB, nullptr)))
    return false;
  if (vips_image_get_format(img) != VIPS_FORMAT_UCHAR &&
      !step(vips_cast_uchar(img, &out, nullptr)))
    return false;
  if (vips_image_get_bands(img) == 3 &&
      !step(vips_bandjoin_const1(img, &out, 255.0, nullptr)))
    return false;
  if (vips_image_get_bands(img) != 4) {
    g_object_unref(img);
    return false;
  }
  w = vips_image_get_width(img);
  h = vips_image_get_height(img);
  size_t len = 0;
  void* mem = vips_image_write_to_memory(img, &len);
  g_object_unref(img);
  if (!mem) return false;
  auto* p = static_cast<unsigned char*>(mem);
  rgba.assign(p, p + len);
  g_free(mem);
  return len == static_cast<size_t>(w) * h * 4;
}

// Shared root of the thumtoo cache: $XDG_CACHE_HOME/thumtoo (beside
// xanatoo's own cache dir, so Android keeps it app-private).
fs::path thumtoo_cache_root() {
  const std::string own = cache_dir();
  if (own.empty()) return {};
  return fs::path(own).parent_path() / "thumtoo";
}

}  // namespace

struct ImageBackend {
  struct Waiter {
    std::string key;
    int edge = 0;
  };

  std::mutex mutex;
  std::map<std::string, std::vector<Waiter>> inflight;  // uri → waiting thumbs
  std::vector<DecodedImage> done;
  // Last member: destroyed first, joining the workers that use the rest.
  std::unique_ptr<thumtoo::Client> client;

  void finish(const std::string& uri) {
    std::vector<Waiter> waiters;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = inflight.find(uri);
      if (it == inflight.end()) return;
      waiters = std::move(it->second);
      inflight.erase(it);
    }
    std::vector<DecodedImage> out;
    for (const Waiter& wt : waiters) {
      DecodedImage d;
      d.key = wt.key;
      if (auto px = client->get_pixels_from_tiles(uri, wt.edge)) {
        if (VipsImage* img = vips_image_new_from_buffer(
                px->bytes.data(), px->bytes.size(), nullptr, nullptr)) {
          if (!vips_to_rgba(img, d.rgba, d.w, d.h)) d.rgba.clear();
        }
      }
      if (d.rgba.empty()) std::cerr << "thumbnail: cannot load " << uri << "\n";
      out.push_back(std::move(d));
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (DecodedImage& d : out) done.push_back(std::move(d));
  }

  void on_size(const std::string& uri, const thumtoo::SizeReply& reply) {
    if (!reply.size || reply.size->width <= 0 || reply.size->height <= 0) {
      finish(uri);
      return;
    }
    if (client->get_pixels_from_tiles(uri, kMaxThumbEdge)) {
      finish(uri);
      return;
    }
    // Coarsest scale whose long edge still covers the largest thumbnail.
    const int native = std::max(reply.size->width, reply.size->height);
    int min_scale = 0;
    while (thumtoo::dim_at_tile_scale(native, min_scale + 1) >=
           kMaxThumbEdge * 9 / 10)
      ++min_scale;
    client->request_tile_pyramid(
        uri, min_scale, -1,
        [this](std::string u, int, int, int, std::optional<thumtoo::TileBlob>) {
          finish(u);
        });
  }
};

std::shared_ptr<ImageBackend> image_backend_open() {
  const fs::path root = thumtoo_cache_root();
  if (root.empty()) return nullptr;
  auto b = std::make_shared<ImageBackend>();
  try {
    b->client = thumtoo::Client::open(root, {}, kWorkers);
  } catch (const std::exception& ex) {
    std::cerr << "thumtoo cache " << root.string() << ": " << ex.what()
              << " (images without it)\n";
    return nullptr;
  }
  if (!b->client) return nullptr;
  thumtoo::image_library_init();
  return b;
}

bool image_backend_decode(const std::string& path, long long max_pixels,
                          std::vector<unsigned char>& rgba, int& w, int& h) {
  thumtoo::image_library_init();
  VipsImage* img = vips_image_new_from_file(path.c_str(), "access",
                                            VIPS_ACCESS_SEQUENTIAL, nullptr);
  if (!img) {
    std::cerr << "image load failed: " << path << " (" << vips_error_buffer()
              << ")\n";
    vips_error_clear();
    return false;
  }
  if (static_cast<long long>(vips_image_get_width(img)) *
          vips_image_get_height(img) > max_pixels) {
    std::cerr << "image too large: " << path << "\n";
    g_object_unref(img);
    return false;
  }
  return vips_to_rgba(img, rgba, w, h);
}

void image_backend_request_thumb(ImageBackend& b, const std::string& key,
                                 const std::string& path, int edge) {
  const std::string uri = thumtoo::file_uri_from_path(fs::absolute(path));
  {
    std::lock_guard<std::mutex> lock(b.mutex);
    auto [it, fresh] = b.inflight.try_emplace(uri);
    it->second.push_back({key, std::min(edge, kMaxThumbEdge)});
    if (!fresh) return;  // that file's pyramid is already on its way
  }
  ImageBackend* bp = &b;
  b.client->request_size(uri, [bp](std::string u, thumtoo::SizeReply r) {
    bp->on_size(u, r);
  });
}

std::vector<DecodedImage> image_backend_take_done(ImageBackend& b) {
  std::lock_guard<std::mutex> lock(b.mutex);
  return std::exchange(b.done, {});
}

std::string image_backend_vips_version() { return vips_version_string(); }

std::vector<std::string> image_backend_suffixes() {
  std::set<std::string> suffs;
  if (VIPS_INIT("xanatoo") == 0) {  // no-op once thumtoo started it
    vips_foreign_map(
        "VipsForeignLoad",
        [](void* cls, void* a, void*) -> void* {
          auto& out = *static_cast<std::set<std::string>*>(a);
          if (const char** s = static_cast<VipsForeignClass*>(cls)->suffs)
            for (; *s; ++s) out.insert(ascii_lower(std::string_view(*s)));
          return nullptr;
        },
        &suffs, nullptr);
  }
  return {suffs.begin(), suffs.end()};
}

}  // namespace xanatoo
