// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

// Status-bar path/URL prompt: cursor editing, Tab path completion, history.
struct Minibuffer {
  std::string text;
  size_t cursor = 0;  // byte offset into text (0..size)

  // Up/Down history (caller owns the ring; we track index into it).
  int hist_index = -1;  // -1 = editing draft; 0..n-1 = history entry
  std::string draft;    // text saved when leaving the draft line

  // Tab completion state (reset on any non-Tab edit).
  std::vector<std::string> candidates;
  int candidate_i = -1;
  std::string cycle_stem;  // text before first Tab in a cycle

  void clear();
  void set_text(std::string s);
  void insert_utf8(std::string_view bytes);
  void backspace();
  void delete_forward();
  void move_left();
  void move_right();
  void move_home();
  void move_end();
  void kill_to_end();       // C-k
  void kill_line();         // C-u
  void kill_word_back();    // C-w / M-Backspace

  // Filesystem path completion. dirs get a trailing '/'.
  // First Tab: expand common prefix or fill unique match.
  // Further Tab: cycle candidates. Returns true if text changed.
  bool tab_complete_path();

  // History navigation (hist is newest-last or newest-first; we treat
  // index 0 as oldest for Up-from-draft → last entry).
  void history_older(const std::vector<std::string>& hist);
  void history_newer(const std::vector<std::string>& hist);

  // Display: text with a block cursor marker for status-bar rendering.
  std::string display_with_cursor() const;

  // Hint suffix after the field (completion count, etc.).
  std::string status_hint() const;
};

// Expand leading ~/ to $HOME (or leave unchanged).
std::string expand_user_path(std::string_view path);

// Load/save one history line per entry (newest last). Caps at max_entries.
std::vector<std::string> load_minibuffer_history(std::string_view name);
void save_minibuffer_history(std::string_view name,
                             const std::vector<std::string>& entries,
                             size_t max_entries = 200);
void push_minibuffer_history(std::vector<std::string>& hist,
                             std::string_view entry, size_t max_entries = 200);

}  // namespace xanatoo
