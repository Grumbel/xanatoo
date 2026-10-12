// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/minibuffer.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace xanatoo {
namespace {

namespace fs = std::filesystem;

size_t utf8_prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  --pos;
  while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80)
    --pos;
  return pos;
}

size_t utf8_next(const std::string& s, size_t pos) {
  if (pos >= s.size()) return s.size();
  unsigned char c = static_cast<unsigned char>(s[pos]);
  size_t len = 1;
  if ((c & 0xe0) == 0xc0) len = 2;
  else if ((c & 0xf0) == 0xe0) len = 3;
  else if ((c & 0xf8) == 0xf0) len = 4;
  if (pos + len > s.size()) return s.size();
  return pos + len;
}


void reset_completion(Minibuffer& mb) {
  mb.candidates.clear();
  mb.candidate_i = -1;
  mb.cycle_stem.clear();
}

std::string common_prefix(const std::vector<std::string>& items) {
  if (items.empty()) return {};
  std::string p = items[0];
  for (size_t i = 1; i < items.size(); ++i) {
    size_t n = 0;
    while (n < p.size() && n < items[i].size() && p[n] == items[i][n]) ++n;
    p.resize(n);
    if (p.empty()) break;
  }
  return p;
}

// List filesystem matches for `typed` under directory `list_dir`.
// Results are paths as the user should see them (relative names, or
// preserving a "~/…" / absolute prefix from `typed`).
std::vector<std::string> list_path_matches(std::string_view typed,
                                           const fs::path& list_dir) {
  std::error_code ec;
  if (!fs::is_directory(list_dir, ec)) return {};

  std::string expanded = expand_user_path(typed);
  std::string dir_prefix;  // including trailing '/' when present in typed form
  std::string file_part;

  if (expanded.empty()) {
    file_part = {};
  } else if (expanded.back() == '/') {
    dir_prefix = std::string(typed);  // keep user form (e.g. "~/" or "ex/")
    if (!dir_prefix.empty() && dir_prefix.back() != '/') dir_prefix.push_back('/');
    // Prefer expanded for listing, but prefix from typed for display
    if (starts_with(typed, "~/"))
      dir_prefix = std::string(typed);
    else if (!typed.empty() && typed.back() == '/')
      dir_prefix = std::string(typed);
    else
      dir_prefix = expanded;
    file_part = {};
  } else {
    auto slash = typed.find_last_of('/');
    if (slash == std::string_view::npos) {
      file_part = std::string(typed);
      dir_prefix = {};
    } else {
      dir_prefix = std::string(typed.substr(0, slash + 1));
      file_part = std::string(typed.substr(slash + 1));
    }
  }

  std::vector<std::string> out;
  for (auto it = fs::directory_iterator(list_dir, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const auto& entry = *it;
    std::string name = entry.path().filename().string();
    if (!file_part.empty() && !starts_with(name, file_part)) continue;
    if (!name.empty() && name[0] == '.' &&
        (file_part.empty() || file_part[0] != '.'))
      continue;
    std::string full = dir_prefix + name;
    std::error_code ec2;
    if (entry.is_directory(ec2)) full.push_back('/');
    out.push_back(std::move(full));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<std::string> path_candidates(std::string_view typed) {
  std::string expanded = expand_user_path(typed);

  // Directory to list: parent of the partial path, or the path itself if it
  // ends with '/', or cwd / base for a bare name.
  fs::path list_dir = ".";
  if (typed == "~") {
    // Bare tilde → complete to "~/…" under $HOME
    list_dir = expanded.empty() ? fs::path(".") : fs::path(expanded);
    // Rewrite typed form so matches come back as "~/name"
    std::vector<std::string> home_hits =
        list_path_matches("~/", list_dir);
    return home_hits;
  } else if (expanded.empty()) {
    list_dir = ".";
  } else if (expanded.back() == '/') {
    list_dir = expanded;
  } else {
    auto slash = expanded.find_last_of('/');
    if (slash == std::string::npos) {
      list_dir = ".";
    } else if (slash == 0) {
      list_dir = "/";
    } else {
      list_dir = expanded.substr(0, slash);
    }
  }

  std::vector<std::string> out = list_path_matches(typed, list_dir);
  if (!out.empty()) return out;

  // Relative bare names: also search project data/source (examples/, …).
  if (!expanded.empty() && expanded[0] != '/' && expanded[0] != '~') {
    auto try_base = [&](const char* env) {
      if (!env || !*env) return std::vector<std::string>{};
      fs::path base(env);
      fs::path dir = base;
      if (expanded.back() == '/') {
        dir = base / expanded;
      } else {
        auto slash = expanded.find_last_of('/');
        if (slash == std::string::npos)
          dir = base;
        else
          dir = base / expanded.substr(0, slash);
      }
      return list_path_matches(typed, dir);
    };
    out = try_base(std::getenv("XANATOO_DATA_DIR"));
    if (!out.empty()) return out;
    out = try_base(std::getenv("XANATOO_SOURCE"));
  }
  return out;
}

}  // namespace

void Minibuffer::clear() {
  text.clear();
  cursor = 0;
  hist_index = -1;
  draft.clear();
  reset_completion(*this);
}

void Minibuffer::set_text(std::string s) {
  text = std::move(s);
  cursor = text.size();
  hist_index = -1;
  draft.clear();
  reset_completion(*this);
}

void Minibuffer::insert_utf8(std::string_view bytes) {
  if (bytes.empty()) return;
  reset_completion(*this);
  text.insert(cursor, bytes);
  cursor += bytes.size();
}

void Minibuffer::backspace() {
  if (cursor == 0) return;
  reset_completion(*this);
  size_t prev = utf8_prev(text, cursor);
  text.erase(prev, cursor - prev);
  cursor = prev;
}

void Minibuffer::delete_forward() {
  if (cursor >= text.size()) return;
  reset_completion(*this);
  size_t next = utf8_next(text, cursor);
  text.erase(cursor, next - cursor);
}

void Minibuffer::move_left() {
  cursor = utf8_prev(text, cursor);
}

void Minibuffer::move_right() {
  cursor = utf8_next(text, cursor);
}

void Minibuffer::move_home() { cursor = 0; }

void Minibuffer::move_end() { cursor = text.size(); }

void Minibuffer::kill_to_end() {
  reset_completion(*this);
  if (cursor < text.size()) text.erase(cursor);
}

void Minibuffer::kill_line() {
  reset_completion(*this);
  text.clear();
  cursor = 0;
}

void Minibuffer::kill_word_back() {
  if (cursor == 0) return;
  reset_completion(*this);
  size_t i = cursor;
  // skip trailing separators
  while (i > 0) {
    size_t p = utf8_prev(text, i);
    char c = text[p];
    if (c != ' ' && c != '\t' && c != '/') break;
    i = p;
  }
  while (i > 0) {
    size_t p = utf8_prev(text, i);
    char c = text[p];
    if (c == ' ' || c == '\t' || c == '/') break;
    i = p;
  }
  text.erase(i, cursor - i);
  cursor = i;
}

bool Minibuffer::tab_complete_path() {
  if (candidate_i >= 0 && !candidates.empty()) {
    candidate_i = (candidate_i + 1) % static_cast<int>(candidates.size());
    text = candidates[static_cast<size_t>(candidate_i)];
    cursor = text.size();
    return true;
  }

  candidates = path_candidates(text);
  if (candidates.empty()) {
    candidate_i = -1;
    return false;
  }
  cycle_stem = text;
  if (candidates.size() == 1) {
    text = candidates[0];
    cursor = text.size();
    candidate_i = 0;
    return true;
  }
  // Expand shared prefix when it is strictly longer than what was typed.
  std::string cp = common_prefix(candidates);
  if (cp.size() > text.size()) {
    text = std::move(cp);
    cursor = text.size();
    // Keep candidates; next Tab cycles from the first match.
    candidate_i = 0;
    return true;
  }
  // Already at the shared prefix (or none) → cycle from the first match.
  candidate_i = 0;
  text = candidates[0];
  cursor = text.size();
  return true;
}

void Minibuffer::history_older(const std::vector<std::string>& hist) {
  if (hist.empty()) return;
  reset_completion(*this);
  if (hist_index < 0) {
    draft = text;
    hist_index = static_cast<int>(hist.size()) - 1;
  } else if (hist_index > 0) {
    --hist_index;
  } else {
    return;
  }
  text = hist[static_cast<size_t>(hist_index)];
  cursor = text.size();
}

void Minibuffer::history_newer(const std::vector<std::string>& hist) {
  if (hist_index < 0) return;
  reset_completion(*this);
  if (hist_index + 1 >= static_cast<int>(hist.size())) {
    hist_index = -1;
    text = draft;
    cursor = text.size();
    return;
  }
  ++hist_index;
  text = hist[static_cast<size_t>(hist_index)];
  cursor = text.size();
}

std::string Minibuffer::display_with_cursor() const {
  std::string out = text.substr(0, cursor);
  out += "▌";
  out += text.substr(cursor);
  return out;
}

std::string Minibuffer::status_hint() const {
  if (candidates.size() > 1 && candidate_i >= 0) {
    return "  Tab " + std::to_string(candidate_i + 1) + "/" +
           std::to_string(candidates.size());
  }
  if (candidates.size() > 1 && candidate_i < 0) {
    return "  Tab " + std::to_string(candidates.size()) + " matches";
  }
  return {};
}

std::string expand_user_path(std::string_view path) {
  if (path == "~") {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string(path);
  }
  if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
    const char* home = std::getenv("HOME");
    if (!home) return std::string(path);
    return std::string(home) + std::string(path.substr(1));
  }
  return std::string(path);
}

std::vector<std::string> load_minibuffer_history(std::string_view name) {
  std::vector<std::string> out;
  std::string dir = config_dir();
  if (dir.empty()) return out;
  std::ifstream in(dir + "/minibuf-" + std::string(name) + ".history");
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    out.push_back(line);
  }
  return out;
}

void save_minibuffer_history(std::string_view name,
                             const std::vector<std::string>& entries,
                             size_t max_entries) {
  std::string dir = config_dir();
  if (dir.empty()) return;
  size_t start = entries.size() > max_entries ? entries.size() - max_entries : 0;
  std::ofstream out(dir + "/minibuf-" + std::string(name) + ".history",
                    std::ios::trunc);
  for (size_t i = start; i < entries.size(); ++i) out << entries[i] << '\n';
}

void push_minibuffer_history(std::vector<std::string>& hist,
                             std::string_view entry, size_t max_entries) {
  if (entry.empty()) return;
  std::string e(entry);
  hist.erase(std::remove(hist.begin(), hist.end(), e), hist.end());
  hist.push_back(std::move(e));
  if (hist.size() > max_entries)
    hist.erase(hist.begin(),
               hist.begin() + static_cast<std::ptrdiff_t>(hist.size() -
                                                          max_entries));
}

}  // namespace xanatoo
