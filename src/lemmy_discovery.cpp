// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/lemmy_directory.hpp"
#include "xanatoo/json_min.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {
namespace {

std::string lower(std::string_view s) {
  std::string o(s);
  for (char& c : o)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

std::string clean_text(std::string_view s, size_t max) {
  std::string out;
  for (char c : s) {
    if (out.size() == max) break;
    out += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
  }
  return out;
}

std::string handle(std::string_view name, std::string_view actor,
                   std::string_view home) {
  if (name.empty() || name.size() > 160) return {};
  for (unsigned char c : name)
    if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != '@')
      return {};
  std::string out(name);
  const std::string remote = lemmy_instance_host(actor);
  if (out.find('@') == std::string::npos && !remote.empty() && remote != home)
    out += "@" + remote;
  return out;
}

LemmyDirectoryEntry instance_entry(std::string_view instance) {
  const std::string host = lemmy_instance_host(instance);
  return {LemmyEntryKind::Instance, host, host, host,
          "lemmy:" + host + "/api/v3/post/list?limit=25&sort=Hot", 0};
}

}  // namespace

std::string lemmy_instance_host(std::string_view input) {
  if (input.substr(0, 8) == "https://") input.remove_prefix(8);
  else if (input.substr(0, 7) == "http://") input.remove_prefix(7);
  else if (input.find("://") != std::string_view::npos) return {};
  input = input.substr(0, input.find_first_of("/?#"));
  if (input.size() < 4 || input.size() > 253 || input.front() == '.' ||
      input.back() == '.' || input.find('.') == std::string_view::npos)
    return {};
  bool digit_only = true;
  size_t label_start = 0;
  for (size_t i = 0; i <= input.size(); ++i) {
    if (i == input.size() || input[i] == '.') {
      const size_t len = i - label_start;
      if (len == 0 || len > 63 || input[label_start] == '-' ||
          input[i - 1] == '-') return {};
      label_start = i + 1;
      continue;
    }
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (!std::isalnum(c) && c != '-') return {};
    if (!std::isdigit(c)) digit_only = false;
    if (c >= 128) return {};  // IDN hostnames must be punycoded
  }
  if (digit_only) return {}; // never auto-discover raw IP addresses
  return lower(input);
}

std::string lemmy_query_escape(std::string_view input) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : input) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::vector<LemmyDirectoryEntry> lemmy_discover_payload(
    std::string_view instance, std::string_view json) {
  const std::string host = lemmy_instance_host(instance);
  if (host.empty()) return {};
  std::vector<LemmyDirectoryEntry> entries;
  entries.push_back(instance_entry(host));
  auto person = [&](std::string_view object, int64_t count = 0) {
    const std::string name = handle(json_str_field(object, "name"),
                                     json_str_field(object, "actor_id"), host);
    if (name.empty()) return;
    std::string title = clean_text(json_str_field(object, "display_name"), 240);
    if (title.empty()) title = name;
    entries.push_back({LemmyEntryKind::Person, host, name, title,
                       "lemmy:" + host + "/u/" + name, count});
  };
  auto community = [&](std::string_view object, int64_t subscribers = 0) {
    const std::string name = handle(json_str_field(object, "name"),
                                     json_str_field(object, "actor_id"), host);
    if (name.empty()) return;
    std::string title = clean_text(json_str_field(object, "title"), 240);
    if (title.empty()) title = name;
    entries.push_back({LemmyEntryKind::Community, host, name, title,
                       "lemmy:" + host + "/c/" + name, subscribers});
  };
  auto post_view = [&](std::string_view view) {
    const std::string_view post = json_object_after_key(view, "post");
    const std::string_view post_data = post.empty() ? view : post;
    const int64_t id = json_int_field(post_data, "id");
    const std::string title = clean_text(json_str_field(post_data, "name"), 240);
    const std::string_view counts = json_object_after_key(view, "counts");
    if (id > 0 && !title.empty()) {
      const std::string id_text = std::to_string(id);
      entries.push_back({LemmyEntryKind::Post, host, id_text, title,
                         "lemmy:" + host + "/post/" + id_text,
                         json_int_field(counts, "score")});
    }
    person(json_object_after_key(view, "creator"));
    community(json_object_after_key(view, "community"));
  };
  auto comment_view = [&](std::string_view view) {
    person(json_object_after_key(view, "creator"));
    community(json_object_after_key(view, "community"));
  };
  auto community_view = [&](std::string_view view) {
    const auto obj = json_object_after_key(view, "community");
    const auto counts = json_object_after_key(view, "counts");
    community(obj.empty() ? view : obj, json_int_field(counts, "subscribers"));
  };
  auto person_view = [&](std::string_view view) {
    const auto obj = json_object_after_key(view, "person");
    const auto counts = json_object_after_key(view, "counts");
    person(obj.empty() ? view : obj, json_int_field(counts, "post_count"));
  };
  for (const auto& p : json_object_array_after_key(json, "posts")) post_view(p);
  if (const auto p = json_object_after_key(json, "post_view"); !p.empty())
    post_view(p);
  for (const auto& c : json_object_array_after_key(json, "comments")) comment_view(c);
  for (const auto& c : json_object_array_after_key(json, "communities")) community_view(c);
  if (const auto c = json_object_after_key(json, "community_view"); !c.empty())
    community_view(c);
  for (const auto& p : json_object_array_after_key(json, "users")) person_view(p);
  if (const auto p = json_object_after_key(json, "person_view"); !p.empty())
    person_view(p);
  for (const auto& c : json_object_array_after_key(json, "moderates")) community_view(c);
  // GET /api/v3/federated_instances returns linked/allowed/blocked peers.
  // Blocked peers are deliberately not added to the directory.
  const auto peers = json_object_after_key(json, "federated_instances");
  for (const auto& peer : json_object_array_after_key(peers, "linked")) {
    const std::string domain = lemmy_instance_host(json_str_field(peer, "domain"));
    if (!domain.empty()) entries.push_back(instance_entry(domain));
  }
  for (const auto& peer : json_object_array_after_key(peers, "allowed")) {
    const std::string domain = lemmy_instance_host(json_str_field(peer, "domain"));
    if (!domain.empty()) entries.push_back(instance_entry(domain));
  }
  return entries;
}

}  // namespace xanatoo
