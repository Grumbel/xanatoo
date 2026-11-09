// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/lemmy_directory.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace xanatoo {
namespace {

constexpr size_t kMaxEntries = 10000;

struct LemmyCatalogue {
  std::mutex mutex;
  bool loaded = false;
  std::vector<LemmyDirectoryEntry> entries;
};

LemmyCatalogue& catalogue() {
  static LemmyCatalogue cat;
  return cat;
}

std::string directory_path() {
  const std::string dir = state_dir();
  return dir.empty() ? std::string() :
                       (std::filesystem::path(dir) / "lemmy-directory.txt").string();
}

bool valid_entry(const LemmyDirectoryEntry& entry) {
  if (entry.instance.empty() ||
      lemmy_instance_host(entry.instance) != entry.instance ||
      entry.name.empty() || entry.name.size() > 250 ||
      entry.title.size() > 400 || entry.ref.size() > 1024) return false;
  const std::string prefix = "lemmy:" + entry.instance + "/";
  return entry.kind == LemmyEntryKind::Instance ?
      entry.ref.rfind("lemmy:", 0) == 0 : entry.ref.rfind(prefix, 0) == 0;
}

bool merge_entry(std::vector<LemmyDirectoryEntry>& dst,
                 const LemmyDirectoryEntry& entry) {
  if (!valid_entry(entry)) return false;
  for (auto& existing : dst) {
    if (existing.kind != entry.kind || existing.instance != entry.instance ||
        existing.name != entry.name) continue;
    bool changed = false;
    if (!entry.title.empty() && existing.title != entry.title) {
      existing.title = entry.title;
      changed = true;
    }
    if (entry.score != 0 && existing.score != entry.score) {
      existing.score = entry.score;
      changed = true;
    }
    if (entry.ref != existing.ref) {
      existing.ref = entry.ref;
      changed = true;
    }
    return changed;
  }
  if (dst.size() >= kMaxEntries) return false;
  dst.push_back(entry);
  return true;
}

LemmyDirectoryEntry make_instance(const std::string& host) {
  return {LemmyEntryKind::Instance, host, host, host,
          "lemmy:" + host + "/api/v3/post/list?limit=25&sort=Hot", 0};
}

void ensure_loaded(LemmyCatalogue& c) {
  if (c.loaded) return;
  c.loaded = true;
  // A useful offline starting set, even before any remote API responses.
  // These are examples, not an assertion that each server is online now.
  constexpr const char* known[] = {
      "lemmy.world", "lemmy.ml", "programming.dev", "sh.itjust.works",
      "lemmy.zip", "lemmy.ca", "lemmy.sdf.org", "sopuli.xyz",
      "feddit.org", "feddit.uk", "lemmy.dbzer0.com", "aussie.zone",
      "lemmy.nz", "lemmy.today", "lemmy.one", "lemmygrad.ml"};
  for (const char* host : known) merge_entry(c.entries, make_instance(host));

  const std::string path = directory_path();
  if (path.empty()) return;
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > 8 * 1024 * 1024) return;
  std::ifstream in(path);
  if (!in) return;
  std::string line;
  while (c.entries.size() < kMaxEntries && std::getline(in, line)) {
    if (line.size() > 1800 || line.empty() || line[0] == '#') continue;
    std::istringstream s(line);
    LemmyDirectoryEntry item;
    int type = -1;
    if (!(s >> type >> std::quoted(item.instance) >> std::quoted(item.name) >>
          std::quoted(item.title) >> std::quoted(item.ref) >> item.score) ||
        type < 0 || type > 3) continue;
    item.kind = static_cast<LemmyEntryKind>(type);
    merge_entry(c.entries, item);
  }
}

void save_entries(const LemmyCatalogue& c) {
  if (no_save() || !ensure_dir(state_dir())) return;
  const std::string path = directory_path();
  if (path.empty()) return;
  std::ostringstream out;
  out << "# Xanatoo observed Lemmy directory v1 (no private accounts)\n";
  for (const auto& item : c.entries)
    out << static_cast<int>(item.kind) << ' '
        << std::quoted(item.instance) << ' ' << std::quoted(item.name) << ' '
        << std::quoted(item.title) << ' ' << std::quoted(item.ref) << ' '
        << item.score << '\n';
  write_file_atomic(path, out.str());
}

}  // namespace

std::vector<LemmyDirectoryEntry> lemmy_directory_list() {
  auto& c = catalogue();
  std::lock_guard lock(c.mutex);
  ensure_loaded(c);
  auto entries = c.entries;
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.instance != b.instance) return a.instance < b.instance;
    if (a.title != b.title) return a.title < b.title;
    return a.name < b.name;
  });
  return entries;
}

bool lemmy_directory_add_instance(std::string_view input) {
  const std::string host = lemmy_instance_host(input);
  if (host.empty()) return false;
  auto& c = catalogue();
  std::lock_guard lock(c.mutex);
  ensure_loaded(c);
  if (merge_entry(c.entries, make_instance(host))) save_entries(c);
  return true;
}

void lemmy_directory_observe(std::string_view instance, std::string_view json) {
  const auto observed = lemmy_discover_payload(instance, json);
  if (observed.empty()) return;
  auto& c = catalogue();
  std::lock_guard lock(c.mutex);
  ensure_loaded(c);
  bool changed = false;
  for (const auto& item : observed)
    changed = merge_entry(c.entries, item) || changed;
  if (changed) save_entries(c);
}

}  // namespace xanatoo
