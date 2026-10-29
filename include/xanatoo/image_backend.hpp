// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Image pixels through thumtoo (images_thumtoo.cpp, XANATOO_THUMTOO): libvips
// decodes every format it was built with, and thumbnails are composed from
// the shared thumtoo cache's durable tiles (coarse scales only), built once
// on its worker threads. ImageCache falls back to stb_image without it.

#include <memory>
#include <string>
#include <vector>

namespace xanatoo {

struct ImageBackend;

// Opens the thumtoo cache (shared with galapix / biltoo on desktop); null when
// built without thumtoo or the cache cannot be opened.
std::shared_ptr<ImageBackend> image_backend_open();

// RGBA pixels of a whole image file (EXIF orientation applied), or false.
// Synchronous; refuses images above `max_pixels`.
bool image_backend_decode(const std::string& path, long long max_pixels,
                          std::vector<unsigned char>& rgba, int& w, int& h);

// Ask for the thumbnail of `path` with longer edge ≤ `edge`; it arrives
// later from image_backend_take_done under `key`.
void image_backend_request_thumb(ImageBackend& b, const std::string& key,
                                 const std::string& path, int edge);

struct DecodedImage {
  std::string key;
  std::vector<unsigned char> rgba;  // empty: failed
  int w = 0;
  int h = 0;
};

// Thumbnails finished since the last call (any thread → caller).
std::vector<DecodedImage> image_backend_take_done(ImageBackend& b);

// For the about: page: libvips version and the file suffixes its loaders
// accept (".jpg", ".webp", …, sorted).
std::string image_backend_vips_version();
std::vector<std::string> image_backend_suffixes();

}  // namespace xanatoo
