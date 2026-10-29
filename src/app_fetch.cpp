// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Background page fetches: remote documents are fetched and parsed on a
// detached worker thread; the frame loop picks up finished jobs.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <thread>

#include "xanatoo/app.hpp"
#include "xanatoo/ebooks.hpp"
#include "xanatoo/feed.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/hackernews.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/sitemap.hpp"
#include "xanatoo/wikipedia.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

constexpr size_t kPageCacheMax = 16;
constexpr Uint32 kPageCacheTtl = 5 * 60 * 1000;

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
void fetch_gemini(const std::string& url, FetchShared& out) {
  out.service = "Gemini";
  auto resp = xanatoo::gemini_fetch(url, &out.error);
  if (!resp) return;
  out.final_url = resp->final_url;
  if (resp->status == 10 || resp->status == 11) {
    out.input_status = resp->status;
    out.input_meta = resp->meta;
    out.error = "input requested: " + resp->meta;
    return;
  }
  if (resp->status < 20 || resp->status >= 30) {
    out.error = "status " + std::to_string(resp->status) + " " + resp->meta;
    return;
  }
  std::string mime = resp->meta;
  for (char& c : mime)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // Drop parameters: "text/gemini; charset=utf-8"
  if (size_t semi = mime.find(';'); semi != std::string::npos) mime.resize(semi);
  while (!mime.empty() && mime.back() == ' ') mime.pop_back();
  if (mime.find("text/markdown") != std::string::npos ||
      mime.find("text/x-markdown") != std::string::npos) {
    out.doc = xanatoo::parse_document(resp->body, "remote.md");
  } else if (mime.empty() || mime.find("text/gemini") != std::string::npos ||
             mime.find("text/plain") != std::string::npos) {
    out.doc = xanatoo::parse_gemtext(resp->body);
  } else {
    // Binary or unknown — show a short notice instead of mojibake.
    out.doc = xanatoo::parse_gemtext(
        "# Unsupported media\n\nGemini returned MIME type `" + resp->meta +
        "` (" + std::to_string(resp->body.size()) +
        " bytes). Only text/gemini, text/plain, and text/markdown are "
        "rendered in-pane.\n");
  }
}
#endif

// Worker thread body: everything here must stay off App.
void fetch_page(std::string url, std::shared_ptr<FetchShared> out) {
  out->final_url = url;
  // Feeds first: /.rss, /feeds/… and /rss on Reddit / Lemmy / HN are feeds.
  if (xanatoo::is_feed_url(url)) {
#if defined(XANATOO_FEEDS) && XANATOO_FEEDS
    out->service = "Feed";
    out->doc = xanatoo::feed_fetch(url, &out->error);
#endif
  } else if (xanatoo::is_gemini_url(url)) {
#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
    fetch_gemini(url, *out);
#endif
  } else if (xanatoo::is_nostr_ref(url)) {
#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
    out->service = "Nostr";
    out->doc = xanatoo::nostr_fetch_thread(url, &out->error);
#endif
  } else if (xanatoo::is_reddit_ref(url)) {
#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
    out->service = "Reddit";
    out->doc = xanatoo::reddit_fetch(xanatoo::web_url_for(url), &out->error);
    if (out->doc) xanatoo::native_rewrite_links(*out->doc, url);
#endif
  } else if (xanatoo::is_lemmy_ref(url)) {
#if defined(XANATOO_LEMMY) && XANATOO_LEMMY
    out->service = "Lemmy";
    out->doc = xanatoo::lemmy_fetch(xanatoo::web_url_for(url), &out->error);
    if (out->doc) xanatoo::native_rewrite_links(*out->doc, url);
#endif
  } else if (xanatoo::is_hn_ref(url)) {
#if defined(XANATOO_HN) && XANATOO_HN
    out->service = "Hacker News";
    out->doc = xanatoo::hn_fetch(xanatoo::web_url_for(url), &out->error);
    if (out->doc) xanatoo::native_rewrite_links(*out->doc, url);
#endif
  } else if (xanatoo::is_ebook_url(url)) {
#if defined(XANATOO_EBOOKS) && XANATOO_EBOOKS
    out->service = "Ebooks";
    // Books open as their imported Markdown, so images resolve locally.
    std::string local;
    out->doc = xanatoo::ebook_fetch(url, &out->error, &local);
    if (!local.empty()) out->final_url = local;
#endif
  } else if (xanatoo::is_sitemap_ref(url)) {
#if defined(XANATOO_WEB) && XANATOO_WEB
    out->service = "Site map";
    out->doc = xanatoo::sitemap_fetch(url, &out->error);
#endif
  } else if (xanatoo::is_wikipedia_ref(url)) {
#if defined(XANATOO_WIKIPEDIA) && XANATOO_WIKIPEDIA
    out->service = "Wikipedia";
    out->doc = xanatoo::wikipedia_fetch(url, &out->error, &out->final_url);
#endif
  } else if (xanatoo::is_web_url(url)) {
#if defined(XANATOO_WEB) && XANATOO_WEB
    out->service = "Web";
    out->doc = xanatoo::web_fetch(url, &out->error, &out->final_url);
#endif
  }
  if (!out->doc && out->error.empty()) out->error = "no client for " + url;
  out->done.store(true, std::memory_order_release);
}

// "page#id" → caret to the node labelled / titled "id".
void jump_to_url_fragment(App& app, int pi, const std::string& url) {
  const size_t hash = url.find('#');
  if (hash == std::string::npos || hash + 1 == url.size()) return;
  Pane& p = app.panes[pi];
  if (auto dest = xanatoo::jump_to_href(doc_of(app, p), url.substr(hash))) {
    expand_ancestors_for_node(app, dest->node);
    p.sel.focus = *dest;
    p.sel.anchor = *dest;
    scroll_caret_into_view(app, p);
  }
}

// Host (or a short prefix) of `url` for the loading label.
std::string short_target(const std::string& url) {
  size_t s = url.find("://");
  if (s == std::string::npos) return url.substr(0, 32);
  s += 3;
  size_t e = url.find('/', s);
  return url.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

void cache_page(App& app, const FetchJob& job) {
  auto& c = app.page_cache;
  c.erase(std::remove_if(c.begin(), c.end(),
                         [&](const CachedPage& e) { return e.url == job.url; }),
          c.end());
  c.push_back({job.url, job.shared->final_url, *job.shared->doc,
               SDL_GetTicks()});
  if (c.size() > kPageCacheMax) c.erase(c.begin());
}

// Error page for a pane that only showed the "Loading …" stand-in.
void show_load_error(App& app, const FetchJob& job) {
  Pane& p = app.panes[job.pane];
  const DocBuffer* b = find_buf(app, p.buf);
  if (!b || !b->placeholder) return;
  std::string md = "# Could not load page\n\n`" + job.url + "`\n\n" +
                   job.shared->error + "\n";
  if (xanatoo::starts_with(job.url, "http://") ||
      xanatoo::starts_with(job.url, "https://"))
    md += "\n[Open in browser](" + std::string(xanatoo::kExternalPrefix) +
          job.url + ")\n";
  show_doc_in_pane(app, job.pane, xanatoo::parse_document(md, "error.md"),
                   job.url);
}

void finish_navigate(App& app, const FetchJob& job) {
  FetchShared& r = *job.shared;
  const std::string service = r.service;
  if (r.input_status != 0) {
    // Input request — open URL bar with query for the user to complete.
    std::string q = r.final_url;
    if (!q.empty() && q.back() != '?') q.push_back('?');
    focus_view(app, job.pane);
    open_prompt(app, PromptKind::UrlBar, std::move(q));
    flash_status(app, std::string(r.input_status == 11 ? "Sensitive input: "
                                                       : "Input: ") +
                          r.input_meta);
    return;
  }
  if (!r.doc) {
    flash_status(app, service + " failed: " + r.error);
    std::cerr << service << ": " << r.error << "\n";
    show_load_error(app, job);
    return;
  }
  cache_page(app, job);
  show_doc_in_pane(app, job.pane, std::move(*r.doc), r.final_url);
  jump_to_url_fragment(app, job.pane, job.url);
  if (xanatoo::is_gemini_url(r.final_url)) {
    app.gemini_current_url = r.final_url;
    app.browse_mode = true;
  }
  if (job.record_history) push_nav_history(app, r.final_url);
  char secs[32];
  std::snprintf(secs, sizeof(secs), "  (%.1f s)",
                static_cast<double>(SDL_GetTicks() - job.started) / 1000.0);
  flash_status(app, "Opened " + r.final_url + secs);
}

}  // namespace

bool remote_fetch_supported(std::string_view url, std::string* why) {
  auto no = [&](const char* msg) {
    if (why) *why = msg;
    return false;
  };
  if (xanatoo::is_feed_url(url)) {
#if defined(XANATOO_FEEDS) && XANATOO_FEEDS
    return true;
#else
    return no("RSS / Atom feeds need build with -DXANATOO_FEEDS=ON");
#endif
  }
  if (xanatoo::is_gemini_url(url)) {
#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
    return true;
#else
    return no("gemini:// needs TLS (build with -DXANATOO_GEMINI=ON)");
#endif
  }
  if (xanatoo::is_nostr_ref(url)) {
#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
    return true;
#else
    return no("Nostr needs build with -DXANATOO_NOSTR=ON");
#endif
  }
  if (xanatoo::is_reddit_ref(url)) {
#if defined(XANATOO_REDDIT) && XANATOO_REDDIT
    return true;
#else
    return no("Reddit needs build with -DXANATOO_REDDIT=ON");
#endif
  }
  if (xanatoo::is_lemmy_ref(url)) {
#if defined(XANATOO_LEMMY) && XANATOO_LEMMY
    return true;
#else
    return no("Lemmy needs build with -DXANATOO_LEMMY=ON");
#endif
  }
  if (xanatoo::is_hn_ref(url)) {
#if defined(XANATOO_HN) && XANATOO_HN
    return true;
#else
    return no("Hacker News needs build with -DXANATOO_HN=ON");
#endif
  }
  if (xanatoo::is_ebook_url(url)) {
#if defined(XANATOO_EBOOKS) && XANATOO_EBOOKS
    return true;
#else
    return no("Ebook downloads need build with -DXANATOO_EBOOKS=ON");
#endif
  }
  if (xanatoo::is_sitemap_ref(url)) {
#if defined(XANATOO_WEB) && XANATOO_WEB
    return true;
#else
    return no("sitemap: needs build with -DXANATOO_WEB=ON");
#endif
  }
  if (xanatoo::is_wikipedia_ref(url)) {
#if defined(XANATOO_WIKIPEDIA) && XANATOO_WIKIPEDIA
    return true;
#else
    return no("wikipedia: needs build with -DXANATOO_WIKIPEDIA=ON");
#endif
  }
  if (xanatoo::is_web_url(url)) {
#if defined(XANATOO_WEB) && XANATOO_WEB
    return true;
#else
    return no("Web pages need build with -DXANATOO_WEB=ON");
#endif
  }
  return no("not a remote URL");
}

uint64_t start_fetch(App& app, FetchPurpose purpose, int pane,
                     const std::string& url, bool record_history) {
  if (purpose == FetchPurpose::Navigate) {
    cancel_fetches(app, pane);
  } else {
    cancel_preview_fetch(app);
  }
  FetchJob job;
  job.id = app.next_fetch_id++;
  job.purpose = purpose;
  job.pane = pane;
  job.url = url;
  job.record_history = record_history;
  job.started = SDL_GetTicks();
  job.shared = std::make_shared<FetchShared>();
  // Detached: a cancelled job just finishes into its (orphaned) shared state.
  std::thread(fetch_page, url, job.shared).detach();
  app.fetches.push_back(std::move(job));
  return app.fetches.back().id;
}

void poll_fetches(App& app) {
  // Collect first: finishing a job can start or cancel others.
  std::vector<FetchJob> done;
  for (auto it = app.fetches.begin(); it != app.fetches.end();) {
    if (it->shared->done.load(std::memory_order_acquire)) {
      done.push_back(std::move(*it));
      it = app.fetches.erase(it);
    } else {
      ++it;
    }
  }
  for (FetchJob& job : done) {
    if (job.purpose == FetchPurpose::Navigate) {
      finish_navigate(app, job);
    } else {
      if (job.shared->doc) cache_page(app, job);
      // update_split_preview shows it if the link is still hovered.
      if (app.split_preview.pending == job.url) {
        app.split_preview.pending.clear();
        if (!job.shared->doc)
          flash_status(app, std::string("Preview failed: ") +
                                job.shared->error);
      }
    }
  }
}

bool cancel_fetches(App& app, int pi) {
  const size_t before = app.fetches.size();
  app.fetches.erase(
      std::remove_if(app.fetches.begin(), app.fetches.end(),
                     [&](const FetchJob& j) {
                       return j.purpose == FetchPurpose::Navigate &&
                              (pi < 0 || j.pane == pi);
                     }),
      app.fetches.end());
  return app.fetches.size() != before;
}

void cancel_preview_fetch(App& app) {
  app.fetches.erase(std::remove_if(app.fetches.begin(), app.fetches.end(),
                                   [](const FetchJob& j) {
                                     return j.purpose == FetchPurpose::Preview;
                                   }),
                    app.fetches.end());
  app.split_preview.pending.clear();
}

const FetchJob* pane_fetch(const App& app, int pi) {
  const FetchJob* preview = nullptr;
  for (const FetchJob& j : app.fetches) {
    if (j.pane != pi) continue;
    if (j.purpose == FetchPurpose::Navigate) return &j;
    preview = &j;
  }
  return preview;
}

const CachedPage* find_cached_page(App& app, const std::string& url) {
  const Uint32 now = SDL_GetTicks();
  for (auto it = app.page_cache.rbegin(); it != app.page_cache.rend(); ++it)
    if (it->url == url && now - it->ticks < kPageCacheTtl) return &*it;
  return nullptr;
}

void forget_cached_page(App& app, const std::string& url) {
  auto& c = app.page_cache;
  c.erase(std::remove_if(c.begin(), c.end(),
                         [&](const CachedPage& e) {
                           return e.url == url || e.final_url == url;
                         }),
          c.end());
}

void draw_load_indicator(App& app, int pi) {
  const FetchJob* job = pane_fetch(app, pi);
  if (!job) return;
  namespace th = xanatoo::theme;
  const th::Palette& pal = th::palette();
  const Pane& p = app.panes[pi];
  const Uint32 now = SDL_GetTicks();
  const float t = static_cast<float>(now - job->started) / 1000.f;
  // ImGui's tab strip (drawn last) overlaps the pane's first pixels.
  const int top = p.y + 2;
  const int w = std::max(1, p.w - kSbW);
  SDL_RenderSetClipRect(app.sdl, nullptr);
  SDL_SetRenderDrawBlendMode(app.sdl, SDL_BLENDMODE_BLEND);

  // Indeterminate bar: a segment sweeping across the pane's top edge.
  const int bar_h = std::max(3, static_cast<int>(3.f * app.ui_scale));
  th::fill(app.sdl, SDL_Rect{p.x, top, w, bar_h}, pal.accent_soft, 200);
  const float phase = std::fmod(t * 0.8f, 1.f);
  const int seg_w = std::max(40, w / 4);
  const int seg_x = p.x - seg_w + static_cast<int>(phase * (w + seg_w));
  SDL_Rect seg{std::max(p.x, seg_x), top,
               std::min(p.x + w, seg_x + seg_w) - std::max(p.x, seg_x), bar_h};
  if (seg.w > 0) th::fill(app.sdl, seg, pal.accent);

  // Label pill below the bar, top-right of the pane.
  static const char* kSpin[] = {"◐", "◓", "◑", "◒"};
  const char* spin = kSpin[(now / 150) % 4];
  char secs[32];
  std::snprintf(secs, sizeof(secs), "  %.1f s", static_cast<double>(t));
  std::string label = std::string(spin) + "  " +
                      (job->purpose == FetchPurpose::Preview ? "Preview "
                                                             : "Loading ") +
                      short_target(job->url) + "…" + secs;
  Font& font = app.ui_font;
  const int pad = 8;
  label = fit_text(font, label, std::max(40, w - 4 * pad));
  const int lw = font.measure(label) + 2 * pad;
  const int lh = font.line_height() + 6;
  SDL_Rect pill{p.x + w - lw - pad, top + bar_h + pad, lw, lh};
  th::fill(app.sdl, pill, pal.notice, 235);
  SDL_SetRenderDrawColor(app.sdl, pal.shadow.r, pal.shadow.g, pal.shadow.b,
                         255);
  SDL_RenderDrawRect(app.sdl, &pill);
  app.renderer.draw_text(font, static_cast<float>(pill.x + pad),
                         static_cast<float>(pill.y + 3 + font.ascender()),
                         label, pal.notice_text.r, pal.notice_text.g,
                         pal.notice_text.b);
}

}  // namespace xanatoo::app
