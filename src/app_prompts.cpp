// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/app.hpp"

#include "xanatoo/epub_import.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/sidestore.hpp"
#include "xanatoo/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>

namespace fs = std::filesystem;

namespace xanatoo::app {

bool minibuf_active(App& app) {return app.open_file_mode || app.save_as_mode || app.path_in_window_mode ||
           app.url_bar_mode || app.image_insert_mode || app.include_insert_mode;
}

void minibuf_begin(App& app, const std::string& initial) {app.minibuf.clear();
    if (!initial.empty()) app.minibuf.set_text(initial);
}

void minibuf_commit_path_hist(App& app) {xanatoo::push_minibuffer_history(app.minibuf_hist_path, app.minibuf.text);
    xanatoo::save_minibuffer_history("path", app.minibuf_hist_path);
}

void minibuf_commit_url_hist(App& app) {xanatoo::push_minibuffer_history(app.minibuf_hist_url, app.minibuf.text);
    xanatoo::save_minibuffer_history("url", app.minibuf_hist_url);
}

void minibuf_sync_out(App& app) {if (app.open_file_mode) app.open_file_buf = app.minibuf.text;
    if (app.save_as_mode) app.save_as_path = app.minibuf.text;
    if (app.path_in_window_mode) app.path_in_window_buf = app.minibuf.text;
    if (app.url_bar_mode) app.url_bar_buf = app.minibuf.text;
    if (app.image_insert_mode) app.image_insert_path = app.minibuf.text;
    if (app.include_insert_mode) app.include_insert_src = app.minibuf.text;
}

void flash_status(App& app, const std::string& msg) {app.status_notice = msg;
    app.status_notice_until = SDL_GetTicks() + 4000;
}

}  // namespace xanatoo::app
