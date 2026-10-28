// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Drafts: unsaved edits go to the local revision store (revstore.hpp)
// instead of being guarded by "discard?" confirmations.

#include "xanatoo/app.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/revstore.hpp"
#include "xanatoo/util.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

namespace xanatoo::app {

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool regular_file(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

// Label for the drafts page: file name, else the first line of the text.
std::string draft_title(const DocBuffer& b, const std::string& text) {
  if (!b.path.empty()) return path_title(b.path);
  std::string_view rest = text;
  while (!rest.empty()) {
    const size_t nl = rest.find('\n');
    std::string_view line = rest.substr(0, nl);
    rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
    while (!line.empty() && (line.front() == '#' || line.front() == '*' ||
                             line.front() == ' '))
      line.remove_prefix(1);
    trim_ascii(line);
    if (line.empty() || line == "---") continue;
    std::string t(line.substr(0, 60));
    if (line.size() > 60) t += "…";
    return t;
  }
  return "untitled";
}

// Write `b`'s current text as its draft (no-op when unchanged). Undoing
// every edit drops the draft and makes the buffer clean again.
bool write_draft(App& app, DocBuffer& b) {
  b.draft_due = 0;
  const std::string text = serialize_document(b.doc, b.path);
  const std::string cid = content_id(text);
  if (cid == b.draft_id) return true;
  if (!b.base_id.empty() && cid == b.base_id) {
    if (!b.draft_id.empty()) drop_draft(b.doc_key);
    b.draft_id.clear();
    b.dirty = false;
    update_title(app);
    return true;
  }
  if (b.doc_key.empty())
    b.doc_key = b.path.empty() ? new_doc_key() : file_doc_key(b.path);
  // The first draft of a file version also stores what is on disk.
  const std::string base_text =
      b.draft_id.empty() && !b.path.empty() ? read_file(b.path) : std::string();
  const std::string id = put_draft(b.doc_key, text, base_text, draft_title(b, text));
  if (id.empty()) {
    std::cerr << "Draft store unavailable — unsaved edits are only in memory\n";
    return false;
  }
  b.draft_id = id;
  return true;
}

std::string format_time(int64_t t) {
  const std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  localtime_r(&tt, &tm);
  char s[32];
  std::strftime(s, sizeof s, "%Y-%m-%d %H:%M", &tm);
  return s;
}

}  // namespace

bool attach_draft(App& app, BufferId id) {
  DocBuffer* b = find_buf(app, id);
  if (!b) return false;
  b->doc_key.clear();
  b->base_id.clear();
  b->draft_id.clear();
  b->draft_due = 0;
  if (b->path.empty() || b->placeholder || is_foreign_path(b->path) ||
      !regular_file(b->path))
    return false;
  b->doc_key = file_doc_key(b->path);
  b->base_id = content_id(serialize_document(b->doc, b->path));
  const auto d = find_draft(b->doc_key);
  if (!d) return false;
  const auto body = load_object(d->content);
  if (!body) return false;
  b->doc = parse_document(*body, b->path);
  b->draft_id = d->content;
  b->dirty = true;
  update_title(app);
  if (d->base != content_id(read_file(b->path)))
    flash_status(app, "Restored unsaved changes — the file has changed on "
                      "disk since (File → Revert to Saved shows it)");
  else
    flash_status(app, "Restored unsaved changes — Ctrl+S saves, "
                      "File → Revert to Saved drops them");
  return true;
}

bool stash_draft(App& app, BufferId id) {
  DocBuffer* b = find_buf(app, id);
  if (!b || !b->dirty || b->placeholder) return true;
  // Foreign pages are read-only; only view toggles can dirty them.
  if (!b->path.empty() && is_foreign_path(b->path)) return true;
  return write_draft(app, *b);
}

bool stash_or_confirm(App& app, BufferId id) {
  if (stash_draft(app, id) || app.discard_armed) {
    app.discard_armed = false;
    return true;
  }
  app.discard_armed = true;
  flash_status(app, "Unsaved changes (no draft store) — Ctrl+S to save, "
                    "or repeat to discard");
  return false;
}

bool stash_all_or_confirm(App& app) {
  bool ok = true;
  for (const DocBuffer& b : app.buffers)
    if (!stash_draft(app, b.id)) ok = false;
  if (ok || app.discard_armed) {
    app.discard_armed = false;
    return true;
  }
  app.discard_armed = true;
  flash_status(app, "Unsaved changes (no draft store) — Ctrl+S to save, "
                    "or repeat to discard");
  return false;
}

void autosave_drafts(App& app) {
  const Uint32 now = SDL_GetTicks();
  for (DocBuffer& b : app.buffers)
    if (b.draft_due && SDL_TICKS_PASSED(now, b.draft_due)) {
      if (b.dirty && !b.placeholder &&
          (b.path.empty() || !is_foreign_path(b.path)))
        write_draft(app, b);
      b.draft_due = 0;
    }
}

void note_saved(App& app, BufferId id, const std::string& text) {
  DocBuffer* b = find_buf(app, id);
  if (!b || b->path.empty()) return;
  const std::string key = file_doc_key(b->path);
  if (!b->doc_key.empty() && b->doc_key != key && !b->draft_id.empty())
    drop_draft(b->doc_key);  // Save As: the draft lived under the old key
  b->doc_key = key;
  record_save(key, text);
  b->base_id = content_id(text);
  b->draft_id.clear();
  b->draft_due = 0;
}

void revert_buffer(App& app, BufferId id) {
  DocBuffer* b = find_buf(app, id);
  if (!b) return;
  if (!b->doc_key.empty()) drop_draft(b->doc_key);
  b->draft_id.clear();
  b->draft_due = 0;
  b->dirty = false;
}

void open_drafts_page(App& app) {
  std::string md = "# Unsaved drafts\n\n";
  md += "Edits are kept here until they are saved (Ctrl+S) or dropped "
        "(File → Revert to Saved). Older versions stay in the revision "
        "store.\n\n";
  const auto drafts = list_drafts();
  if (drafts.empty()) md += "No unsaved drafts.\n";
  for (const DraftHead& d : drafts) {
    const bool file = starts_with(d.key, "file:");
    const std::string target = file ? d.key.substr(5) : "draft:" + d.key;
    md += "- [" + (d.title.empty() ? std::string("untitled") : d.title) +
          "](" + target + ") — ";
    md += file ? "`" + target + "`" : std::string("no file");
    md += ", " + format_time(d.time) + "\n";
  }
  show_doc_in_pane(app, app.focus, parse_document(md, {}), kDraftsPage);
}

bool open_draft(App& app, int pi, const std::string& ref) {
  const std::string key = ref.substr(6);  // after "draft:"
  if (starts_with(key, "file:")) return navigate_pane(app, pi, key.substr(5));
  for (const DocBuffer& b : app.buffers)
    if (b.doc_key == key) {
      app.panes[pi].buf = b.id;
      app.panes[pi].layout = {};
      app.needs_layout = true;
      update_title(app);
      return true;
    }
  const auto d = find_draft(key);
  const auto body = d ? load_object(d->content) : std::nullopt;
  if (!body) {
    flash_status(app, "Draft not found");
    return false;
  }
  show_doc_in_pane(app, pi, parse_document(*body, {}), {});
  DocBuffer& b = buf(app, app.panes[pi].buf);
  b.doc_key = key;
  b.draft_id = d->content;
  b.dirty = true;
  update_title(app);
  flash_status(app, "Draft restored — Save As gives it a file");
  return true;
}

}  // namespace xanatoo::app
