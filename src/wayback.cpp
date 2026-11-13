// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/wayback.hpp"

#include "xanatoo/util.hpp"

#include <cctype>

namespace xanatoo {

namespace {

constexpr std::string_view kPrefix = "wayback:";
constexpr std::string_view kSnapshotHost = "web.archive.org";

bool all_digits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

bool is_http_url(std::string_view s) {
  return (starts_with_ci(s, "https://") && s.size() > 8) ||
         (starts_with_ci(s, "http://") && s.size() > 7);
}

bool is_archive_host(std::string_view url) {
  size_t h = url.find("://");
  if (h == std::string_view::npos) return false;
  std::string_view host = url.substr(h + 3);
  host = host.substr(0, host.find_first_of("/?#:"));
  const std::string low = ascii_lower(host);
  return low == "archive.org" || ends_with(low, ".archive.org");
}

}  // namespace

std::optional<WaybackRef> parse_wayback_ref(std::string_view ref) {
  if (!starts_with(ref, kPrefix)) return std::nullopt;
  std::string_view rest = ref.substr(kPrefix.size());
  WaybackRef r;
  const size_t slash = rest.find('/');
  if (slash != std::string_view::npos && slash >= 4 && slash <= 14 &&
      all_digits(rest.substr(0, slash))) {
    r.timestamp = std::string(rest.substr(0, slash));
    rest = rest.substr(slash + 1);
  }
  if (!is_http_url(rest)) return std::nullopt;
  r.url = std::string(rest);
  return r;
}

bool is_wayback_ref(std::string_view ref) {
  return parse_wayback_ref(ref).has_value();
}

std::string wayback_ref_string(const WaybackRef& r) {
  return std::string(kPrefix) + (r.timestamp.empty() ? "" : r.timestamp + "/") +
         r.url;
}

std::string wayback_snapshot_url(const WaybackRef& r) {
  // "2" is the Wayback Machine's own spelling of "latest capture".
  return "https://web.archive.org/web/" +
         (r.timestamp.empty() ? std::string("2") : r.timestamp) + "id_/" + r.url;
}

std::optional<WaybackRef> wayback_ref_from_snapshot_url(std::string_view url) {
  std::string_view rest;
  for (std::string_view p : {"https://web.archive.org/web/",
                             "http://web.archive.org/web/"})
    if (starts_with_ci(url, p)) rest = url.substr(p.size());
  if (rest.empty()) return std::nullopt;
  size_t i = 0;
  while (i < rest.size() && std::isdigit(static_cast<unsigned char>(rest[i]))) ++i;
  if (i < 4 || i > 14) return std::nullopt;
  WaybackRef r;
  r.timestamp = std::string(rest.substr(0, i));
  size_t j = i;  // optional modifier: id_, if_, im_, js_, cs_ …
  while (j < rest.size() && std::isalpha(static_cast<unsigned char>(rest[j]))) ++j;
  if (j < rest.size() && rest[j] == '_') ++j;
  else j = i;
  if (j >= rest.size() || rest[j] != '/') return std::nullopt;
  rest = rest.substr(j + 1);
  if (!is_http_url(rest)) return std::nullopt;
  r.url = std::string(rest);
  return r;
}

std::string wayback_lookup_ref(std::string_view page_url) {
  if (!is_http_url(page_url) || is_archive_host(page_url)) return {};
  std::string_view u = page_url.substr(0, page_url.find('#'));
  return wayback_ref_string({"", std::string(u)});
}

std::string wayback_timestamp_label(std::string_view t) {
  if (!all_digits(t) || t.size() < 4 || t.size() > 14) return {};
  std::string full(t);
  static constexpr const char* kDefault = "00000101000000";
  full += (kDefault + full.size());
  return full.substr(0, 4) + "-" + full.substr(4, 2) + "-" + full.substr(6, 2) +
         " " + full.substr(8, 2) + ":" + full.substr(10, 2) + ":" +
         full.substr(12, 2);
}

}  // namespace xanatoo
