// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Fixed-layout documents through thumtoo (docs/FIXED_LAYOUT.md): page list
// from `expand_media_uris`, real page sizes from the size probes (cached in
// thumtoo's Store, so a second open is fast), outline for PDF / DjVu. Runs on
// a fetch worker; blocks until every page is sized.

#include "xanatoo/fixed_layout.hpp"

#include "xanatoo/paths.hpp"

#include <thumtoo/archive.hpp>
#include <thumtoo/client.hpp>
#include <thumtoo/expand.hpp>
#include <thumtoo/image.hpp>
#include <thumtoo/text.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

namespace xanatoo {

namespace {

constexpr int kMaxPages = 100000;
constexpr unsigned kWorkers = 4;
// A probe that never answers is a thumtoo bug; do not wait forever.
constexpr auto kProbeTimeout = std::chrono::minutes(10);

std::mutex g_client_mutex;
std::shared_ptr<thumtoo::Client> g_client;
bool g_client_failed = false;

// Same root as the image backend: $XDG_CACHE_HOME/thumtoo.
fs::path cache_root() {
  const std::string own = cache_dir();
  if (own.empty()) return {};
  return fs::path(own).parent_path() / "thumtoo";
}

struct Probes {
  std::mutex mutex;
  std::condition_variable cv;
  size_t answered = 0;
};

// Size every uri that the Store does not know yet. False on timeout.
bool probe_sizes(thumtoo::Client& client, const std::vector<std::string>& uris) {
  auto probes = std::make_shared<Probes>();
  size_t asked = 0;
  for (const std::string& uri : uris) {
    if (client.get_size(uri)) continue;
    ++asked;
    client.request_size(uri, [probes](std::string, thumtoo::SizeReply) {
      std::lock_guard<std::mutex> lock(probes->mutex);
      ++probes->answered;
      probes->cv.notify_all();
    });
  }
  std::unique_lock<std::mutex> lock(probes->mutex);
  return probes->cv.wait_for(lock, kProbeTimeout,
                             [&] { return probes->answered == asked; });
}

}  // namespace

std::shared_ptr<thumtoo::Client> page_client() {
  std::lock_guard<std::mutex> lock(g_client_mutex);
  if (g_client || g_client_failed) return g_client;
  const fs::path root = cache_root();
  if (root.empty()) {
    g_client_failed = true;
    return nullptr;
  }
  try {
    g_client = thumtoo::Client::open(root, {}, kWorkers);
    thumtoo::image_library_init();
    std::cerr << "pages: thumtoo cache " << root.string() << "\n";
  } catch (const std::exception& ex) {
    std::cerr << "pages: thumtoo cache " << root.string() << ": " << ex.what()
              << "\n";
    g_client_failed = true;
  }
  return g_client;
}

void release_page_client() {
  std::shared_ptr<thumtoo::Client> last;
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    last = std::move(g_client);
  }
}

std::optional<FixedLayoutSource> read_fixed_layout(const std::string& path,
                                                   std::string* error) {
  auto fail = [&](std::string why) -> std::optional<FixedLayoutSource> {
    if (error) *error = std::move(why);
    return std::nullopt;
  };
  std::error_code ec;
  const fs::path abs = fs::absolute(path, ec).lexically_normal();
  if (ec || !fs::is_regular_file(abs, ec)) return fail("no such file: " + path);
  const std::shared_ptr<thumtoo::Client> client = page_client();
  if (!client) return fail("no thumtoo cache directory");

  std::vector<std::string> uris = thumtoo::expand_media_uris(abs, kMaxPages);
  if (uris.empty()) return fail("thumtoo finds no pages in " + abs.string());

  FixedLayoutSource src;
  src.title = abs.filename().string();
  const bool archive = thumtoo::is_archive_uri(uris.front());
  std::vector<std::string> labels(uris.size());
  if (archive) {
    // Archive order is whatever the packer did; readers expect name order.
    std::vector<std::pair<std::string, std::string>> members;  // member, uri
    for (std::string& u : uris) {
      auto parsed = thumtoo::parse_archive_uri(u);
      members.emplace_back(parsed ? parsed->member_path : u, std::move(u));
    }
    std::stable_sort(members.begin(), members.end(),
                     [](const auto& a, const auto& b) {
                       return natural_less(a.first, b.first);
                     });
    for (size_t i = 0; i < members.size(); ++i) {
      uris[i] = std::move(members[i].second);
      labels[i] = fs::path(members[i].first).filename().string();
    }
    // One bulk pass: thumtoo extracts a solid archive once, not per member.
    auto probes = std::make_shared<Probes>();
    const size_t asked = client->prepare_paths(
        {abs}, [probes](std::string, thumtoo::SizeReply) {
          std::lock_guard<std::mutex> lock(probes->mutex);
          ++probes->answered;
          probes->cv.notify_all();
        });
    std::unique_lock<std::mutex> lock(probes->mutex);
    probes->cv.wait_for(lock, kProbeTimeout,
                        [&] { return probes->answered >= asked; });
  } else {
    if (thumtoo::Client::is_pdf_path(abs)) client->ensure_pdf_page_sizes(abs);
    for (size_t i = 0; i < labels.size(); ++i) labels[i] = std::to_string(i + 1);
  }
  // Whatever the bulk paths left out (size caps, DjVu) is probed one by one.
  if (!probe_sizes(*client, uris))
    std::cerr << "pages: size probes of " << abs.string() << " timed out\n";

  src.pages.reserve(uris.size());
  for (size_t i = 0; i < uris.size(); ++i) {
    FixedPage page;
    page.uri = uris[i];
    page.label = labels[i];
    if (auto size = client->get_size(page.uri);
        size && size->width > 0 && size->height > 0) {
      page.width = size->width;
      page.height = size->height;
    } else {
      page.error = "thumtoo has no size for it";
    }
    src.pages.push_back(std::move(page));
  }

  if (!archive) {
    if (auto outline = client->ensure_document_outline(uris.front())) {
      for (const thumtoo::OutlineItem& item : outline->items)
        src.outline.push_back({item.level, item.title, item.page_1based});
    }
  }
  std::cerr << "pages: " << abs.string() << ": " << src.pages.size()
            << " pages, " << src.outline.size() << " outline entries\n";
  return src;
}

}  // namespace xanatoo
