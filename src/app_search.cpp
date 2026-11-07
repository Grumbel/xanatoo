// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Search bar (docs/SEARCH.md): the hidden-by-default row under the
// navigation toolbar. It builds a `search:<engine>:<query>` address and
// navigates to it; fetching, history and session restore are the ordinary
// page machinery. The Ctrl+L bar understands "<keyword> <query>" too.

#include "xanatoo/app.hpp"

#include <filesystem>
#include <iostream>

namespace xanatoo::app {

void init_search(App& app) {
  app.search.engines = xanatoo::load_engines();
  std::cerr << "search: " << app.search.engines.size() << " engines, in use "
            << search_engine_id(app) << "\n";
}

// The engine in use: the saved one, else the first.
std::string search_engine_id(const App& app) {
  for (const xanatoo::SearchEngine& e : app.search.engines)
    if (e.id == app.view_settings.search_engine) return e.id;
  return app.search.engines.empty() ? std::string() : app.search.engines[0].id;
}

void show_search_bar(App& app) {
  if (!app.search.open) app.needs_layout = true;
  app.search.open = true;
  close_prompt(app);
  app.ui.focus_search();
}

void toggle_search_bar(App& app) {
  app.search.open = !app.search.open;
  if (app.search.open)
    app.ui.focus_search();
  else
    app.ui.release_input_capture();  // keys go back to the document
  app.needs_layout = true;
}

void select_search_engine(App& app, const std::string& id) {
  for (const xanatoo::SearchEngine& e : app.search.engines) {
    if (e.id != id) continue;
    app.view_settings.search_engine = id;
    xanatoo::save_view_settings(app.view_settings);
    app.ui.focus_search();
    flash_status(app, "Search engine: " + e.name);
    return;
  }
}

void run_search(App& app, const std::string& text) {
  const std::string ref = xanatoo::search_ref(search_engine_id(app), text);
  if (ref.empty()) {
    flash_status(app, "Type something to search for");
    return;
  }
  app.ui.release_input_capture();
  navigate_to_url(app, ref);
}

std::string search_from_location(const App& app, const std::string& text) {
  std::string ref = xanatoo::search_from_keyword(text, app.search.engines);
  if (ref.empty()) return {};
  // "w notes.md" must still open a file of that name.
  std::error_code ec;
  if (std::filesystem::exists(text, ec)) return {};
  return ref;
}

}  // namespace xanatoo::app
