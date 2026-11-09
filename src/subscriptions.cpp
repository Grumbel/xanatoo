// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/subscriptions.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "xanatoo/paths.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo {
namespace {

std::string path() {
  const std::string dir = state_dir();
  return dir.empty() ? std::string{} :
         (std::filesystem::path(dir) / "subscriptions.txt").string();
}

bool valid_field(const std::string& s) {
  return s.size() <= 2048 && s.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}

}  // namespace

std::vector<Subscription> load_subscriptions() {
  std::vector<Subscription> entries;
  const std::string filename = path();
  if (filename.empty()) return entries;
  std::ifstream in(filename);
  if (!in) return entries;
  std::string line;
  Subscription* current = nullptr;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    char kind = '\0';
    row >> kind;
    if (kind == 'S') {
      current = nullptr;
      Subscription s;
      int interval = 0, initialized = 0;
      if (!(row >> std::quoted(s.address) >> std::quoted(s.title) >> interval >>
            s.checked_at >> s.unread >> initialized) ||
          !valid_field(s.address) || s.address.empty() ||
          !valid_field(s.title) || !subscription_interval_valid(interval) ||
          (initialized != 0 && initialized != 1) || entries.size() >= 1024)
        continue;
      s.interval_minutes = interval;
      s.initialized = initialized != 0;
      if (s.checked_at < 0) s.checked_at = 0;
      if (std::none_of(entries.begin(), entries.end(), [&](const auto& e) {
            return e.address == s.address;
          })) {
        entries.push_back(std::move(s));
        current = &entries.back();
      }
    } else if (kind == 'I' && current) {
      std::string id;
      if (row >> std::quoted(id))
        if (!id.empty() && valid_field(id) &&
            current->recent_ids.size() < kSubscriptionSnapshotLimit)
          current->recent_ids.push_back(std::move(id));
    }
  }
  return entries;
}

bool save_subscriptions(const std::vector<Subscription>& entries) {
  if (no_save()) return true;
  const std::string filename = path();
  if (filename.empty()) return false;
  std::ostringstream out;
  out << "# Xanatoo changing-content subscriptions, v1\n";
  for (const auto& s : entries) {
    if (s.address.empty() || !valid_field(s.address) ||
        !valid_field(s.title)) continue;
    out << "S " << std::quoted(s.address) << ' ' << std::quoted(s.title) << ' '
        << s.interval_minutes << ' ' << s.checked_at << ' ' << s.unread << ' '
        << (s.initialized ? 1 : 0) << '\n';
    for (const auto& id : s.recent_ids)
      if (!id.empty() && valid_field(id)) out << "I " << std::quoted(id) << '\n';
  }
  return write_file_atomic(filename, out.str());
}

}  // namespace xanatoo
