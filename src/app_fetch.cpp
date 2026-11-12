// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Background page fetches: remote documents are fetched and parsed on a
// detached worker thread; the frame loop picks up finished jobs.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string_view>
#include <thread>

#include "xanatoo/app.hpp"
#include "xanatoo/fixed_layout.hpp"
#include "xanatoo/ebooks.hpp"
#include "xanatoo/feed.hpp"
#include "xanatoo/gemini.hpp"
#include "xanatoo/hackernews.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/sitemap.hpp"
#include "xanatoo/wikipedia.hpp"
#include "xanatoo/lemmy.hpp"
#include "xanatoo/video_sites.hpp"
#include "xanatoo/archive.hpp"
#include "xanatoo/youtube.hpp"
#include "xanatoo/native.hpp"
#include "xanatoo/nostr.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/reddit.hpp"
#include "xanatoo/search_results.hpp"
#include "xanatoo/theme.hpp"
#include "xanatoo/util.hpp"

namespace xanatoo::app {

namespace {

constexpr size_t kPageCacheMax = 16;
constexpr Uint32 kPageCacheTtl = 5 * 60 * 1000;

#if defined(XANATOO_GEMINI) && XANATOO_GEMINI
void fetch_gemini(const std::string& url, FetchShared& out) {
  out.service = "Gemini";
  xanatoo::GeminiCertificateChange changed;
  xanatoo::GeminiCertificateFirstUse first_use;
  auto resp = xanatoo::gemini_fetch(url, &out.error, &changed, &first_use,
                                   &out.gemini_certificate);
  if (!changed.host.empty()) out.gemini_change = std::move(changed);
  if (!first_use.host.empty()) out.gemini_first_use = std::move(first_use);
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
  out.raw_source = resp->body;
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
void fetch_page(std::string url, std::shared_ptr<FetchShared> out,
                bool allow_download) {
  out->final_url = url;
  // Local PDF / DjVu / comic: thumtoo sizes every page on this worker.
  if (xanatoo::is_fixed_layout_path(url)) {
    out->service = "Pages";
    if (auto src = xanatoo::read_fixed_layout(url, &out->error))
      out->doc = xanatoo::fixed_layout_document(*src);
    out->done.store(true, std::memory_order_release);
    return;
  }
  // A search is its expanded URL, fetched like any page; the address stays.
  if (xanatoo::is_search_ref(url)) {
    const std::string shown = url;
    const auto plan = xanatoo::search_plan(url, xanatoo::load_engines());
    if (!plan) {
      out->error = "Unknown search engine: " + shown;
      out->done.store(true, std::memory_order_release);
      return;
    }
    if (xanatoo::is_json_search_kind(plan->kind)) {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
      out->service = "Search";
      out->doc = xanatoo::search_fetch(*plan, &out->error);
#endif
      if (!out->doc && out->error.empty()) out->error = "no client for " + shown;
      out->done.store(true, std::memory_order_release);
      return;
    }
    fetch_page(xanatoo::search_target(url, xanatoo::load_engines()), out,
               allow_download);
    out->final_url = shown;
    return;
  }
  if (xanatoo::is_media_ref(url)) {
    const auto plan = xanatoo::media_plan(url);
    if (!plan) {
      out->error = "Invalid media record: " + url;
    } else {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
      out->service = "Media";
      out->doc = xanatoo::media_fetch(*plan, &out->error);
#else
      out->error = "Media records require HTTPS support";
#endif
    }
    out->done.store(true, std::memory_order_release);
    return;
  }
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
  } else if (xanatoo::is_youtube_ref(url)) {
#if defined(XANATOO_YOUTUBE) && XANATOO_YOUTUBE
    out->service = "YouTube";
    out->doc = xanatoo::youtube_fetch(url, &out->error);
#endif
  } else if (xanatoo::is_ia_ref(url)) {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
    out->service = "Internet Archive";
    out->doc = xanatoo::ia_fetch(url, &out->error);
#endif
  } else if (xanatoo::is_video_site_ref(url)) {
#if defined(XANATOO_VIDEO) && XANATOO_VIDEO
    out->service = xanatoo::video_site_name(xanatoo::parse_video_site_ref(url)->site);
    out->doc = xanatoo::video_site_fetch(url, &out->error);
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
    std::string body;
    xanatoo::WebPagesFile pages;
    pages.stop = &out->abandoned;
    out->doc = xanatoo::web_fetch(url, &out->error, &out->final_url,
                                  &out->http_status, &body,
                                  allow_download ? out->download.get() : nullptr,
                                  allow_download ? &pages : nullptr,
                                  out->force_download);
    if (out->doc) out->raw_source = std::move(body);
    // PDF / DjVu / comic answer: open the cached copy as pages; the
    // address stays the web URL.
    if (!out->doc && !pages.path.empty()) {
      out->service = "Pages";
      if (auto src = xanatoo::read_fixed_layout(pages.path, &out->error)) {
        src->title = pages.name;
        out->doc = xanatoo::fixed_layout_document(*src);
      }
    }
#endif
  }
  if (!out->doc && out->error.empty() &&
      (!out->download || out->download->phase.load() !=
                             xanatoo::DownloadPhase::Complete))
    out->error = "no client for " + url;
  if (out->download && out->download->phase.load() ==
                            xanatoo::DownloadPhase::Pending)
    out->download->phase.store(xanatoo::DownloadPhase::Skipped);
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
               job.shared->raw_source,
               xanatoo::sdl::ticks()});
  if (c.size() > kPageCacheMax) c.erase(c.begin());
}

xanatoo::Document load_error_doc(const FetchJob& job) {
  const bool certificate_review = job.shared->gemini_change ||
                                  job.shared->gemini_first_use;
  std::string md;
  if (certificate_review) {
    md = "# Gemini certificate review\n\n"
         "The TLS handshake completed, but the request URL was not sent. "
         "Check the certificate details independently before trusting it.\n";
  } else {
    md = "# Could not load page\n\n`" + job.url + "`\n\n" +
         job.shared->error + "\n";
  }
  if (xanatoo::starts_with(job.url, "http://") ||
      xanatoo::starts_with(job.url, "https://"))
    md += "\n[Open in browser](" + std::string(xanatoo::kExternalPrefix) +
          job.url + ")\n";
  xanatoo::Document doc = xanatoo::parse_document(md, "error.md");
  if (certificate_review) {
    auto field = [&](const char* label, std::string value) {
      // Certificate names are peer-controlled. Keep them as literal text
      // spans, never interpolate into Markdown where links could be injected.
      const xanatoo::NodeId para =
          doc.create(xanatoo::NodeKind::Paragraph, doc.root);
      doc.append_text(para, std::string(label) + ": ",
                      xanatoo::InlineKind::Strong);
      doc.append_text(para, value.empty() ? "Unavailable" : std::move(value));
    };
    auto fingerprint = [](std::string_view hash) {
      // Add word-break opportunities without dropping any SHA-256 hex digits.
      std::string grouped;
      for (size_t i = 0; i < hash.size(); i += 8) {
        if (i) grouped += ' ';
        grouped.append(hash.substr(i, 8));
      }
      return grouped;
    };
    field("Requested URL", job.url);
    if (job.shared->gemini_change) {
      const auto& change = *job.shared->gemini_change;
      field("Trust state", "The certificate differs from the saved pin");
      field("Previously trusted SHA-256", fingerprint(change.previous_sha256));
      field("Presented SHA-256", fingerprint(change.received_sha256));
    } else if (job.shared->gemini_first_use) {
      field("Trust state", "First contact; no certificate is pinned yet");
      field("Presented SHA-256",
            fingerprint(job.shared->gemini_first_use->fingerprint_sha256));
    }
    if (job.shared->gemini_certificate) {
      const auto& cert = *job.shared->gemini_certificate;
      field("Subject", cert.subject);
      field("Issuer", cert.issuer);
      field("Valid from", cert.not_before);
      field("Valid until", cert.not_after);
      if (cert.self_signed.has_value())
        field("Self-signed", *cert.self_signed ?
              "Yes (self-signature verified)" : "No");
      else
        field("Self-signed", cert.self_issued ?
              "Unknown (subject and issuer match; signature not verified)" :
              "Unknown");
    } else {
      field("Certificate metadata", "Unavailable from the TLS backend");
    }
    field("Action", "Enter: trust this certificate and retry; Esc: cancel");
  }
  if (std::string_view(job.shared->service) == "Reddit" &&
      xanatoo::starts_with(job.shared->error, "HTTP 403")) {
    const std::string url = xanatoo::is_reddit_ref(job.url) ?
        xanatoo::web_url_for(job.url) : job.url;
    const std::string rss = xanatoo::reddit_rss_url(url);
    if (!rss.empty()) {
      // Build the link as an inline span: URLs from a user's address bar need
      // not be safe to interpolate into Markdown's (...) link syntax.
      const xanatoo::NodeId para =
          doc.create(xanatoo::NodeKind::Paragraph, doc.root);
      doc.append_text(para, "The Reddit JSON endpoint is blocked. ");
      xanatoo::InlineSpan link;
      link.kind = xanatoo::InlineKind::Link;
      link.text = "Try the Reddit RSS feed";
      link.href = rss;
      doc.get(para).spans.push_back(std::move(link));
      doc.append_text(para, " (if still available).");
    }
  }
  return doc;
}

// Error page in the pane, like a browser's: the address stays the one that
// failed. Back returns to the page before.
bool show_load_error(App& app, const FetchJob& job) {
  if (!stash_or_confirm(app, app.panes[job.pane].buf)) return false;
  show_doc_in_pane(app, job.pane, load_error_doc(job), job.url);
  if (job.record_history)
    push_nav_history(app, job.pane, pane_address(app, app.panes[job.pane]));
  return true;
}

// A site's own 4xx / 5xx page, under the official reason.
void add_http_banner(xanatoo::Document& doc, const FetchJob& job) {
  if (doc.root == xanatoo::INVALID_NODE) return;
  const xanatoo::NodeId n =
      doc.create(xanatoo::NodeKind::BlockQuote, doc.root);
  doc.append_text(n, job.shared->error, xanatoo::InlineKind::Strong);
  doc.append_text(n, " \u2014 " + job.url +
                         " could not be loaded; this is the page the server "
                         "sent instead.");
  auto& kids = doc.get(doc.root).children;
  std::rotate(kids.begin(), kids.end() - 1, kids.end());
}

// A background tab's page arrived: fill its stand-in buffer, unless the tab
// was closed or navigated elsewhere meanwhile.
void finish_tab(App& app, const FetchJob& job) {
  FetchShared& r = *job.shared;
  DocBuffer* b = find_buf(app, job.buf);
  if (!b || !b->placeholder) return;
  if (r.download && r.download->phase.load() != xanatoo::DownloadPhase::Skipped) {
    remove_buffer(app, job.buf);
    app.downloads_panel_open = true;
    flash_status(app, "Download: " + r.download->name);
    return;
  }
  if (r.doc) cache_page(app, job);
  if (!r.doc && r.input_status != 0 && r.error.empty())
    r.error = "The page asks for input: " + r.input_meta;
  b->doc = r.doc ? std::move(*r.doc) : load_error_doc(job);
  b->raw_source = std::move(r.raw_source);
  b->path = r.doc ? r.final_url : job.url;
  b->placeholder = false;
  b->dirty = false;
  const std::string path = b->path;
  attach_draft(app, job.buf);
  DocBuffer& nb = buf(app, job.buf);
  xanatoo::Selection sel;
  xanatoo::ensure_valid_caret(nb.doc, sel.focus);
  sel.anchor = sel.focus;
  nb.history.reset(nb.doc, sel);
  drop_height_hints_for_buf(app, job.buf);
  if (r.doc) note_page_seen(app, path, nb.doc);
  refresh_views_of_buf(app, job.buf, -1, true);
  app.needs_layout = true;
  update_title(app);
  flash_status(app, (r.doc ? "Loaded in tab: " : "Tab failed: ") + path);
}

void finish_navigate(App& app, const FetchJob& job) {
  FetchShared& r = *job.shared;
  const std::string service = r.service;
  if (r.download && r.download->phase.load() != xanatoo::DownloadPhase::Skipped) {
    app.panes[job.pane].nav.pending_link = {};
    app.panes[job.pane].nav.restoring = -1;
    app.downloads_panel_open = true;
    flash_status(app, "Download: " + r.download->name);
    return;  // keep the page visible; no navigation to a binary file
  }
  if (r.input_status != 0) {
    app.panes[job.pane].nav.pending_link = {};
    app.panes[job.pane].nav.restoring = -1;
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
    app.panes[job.pane].nav.pending_link = {};
    app.panes[job.pane].nav.restoring = -1;
    flash_status(app, service + " failed: " + r.error);
    std::cerr << service << ": " << r.error << "\n";
    if (show_load_error(app, job) &&
        (r.gemini_change || r.gemini_first_use)) {
      // The error document shows the presented SHA-256 fingerprint (and
      // the old pin on replacements). No URL was sent to an unknown peer.
      focus_view(app, job.pane);
      open_prompt(app, PromptKind::GeminiTrust);
      app.prompt.gemini_change = r.gemini_change;
      app.prompt.gemini_first_use = r.gemini_first_use;
      app.prompt.gemini_retry_url = job.url;
      app.prompt.gemini_trust_pane = job.pane;
    }
    return;
  }
  const bool http_error = r.http_status != 0;
  if (http_error) {
    add_http_banner(*r.doc, job);
    r.final_url = job.url;  // the address stays the one that failed
  } else {
    cache_page(app, job);
  }
  show_doc_in_pane(app, job.pane, std::move(*r.doc), r.final_url,
                   std::move(r.raw_source));
  // Redirected: the sitemap lists the requested URL.
  if (job.url != r.final_url)
    note_page_seen(app, job.url, buf(app, app.panes[job.pane].buf).doc);
  jump_to_url_fragment(app, job.pane, job.url);
  if (job.locator.has_selectors())
    apply_locator(app, job.pane, job.locator);
  if (xanatoo::is_gemini_url(r.final_url)) {
    app.gemini_current_url = r.final_url;
    app.edit_mode = false;
  }
  if (job.record_history)
    push_nav_history(app, job.pane,
                     pane_address(app, app.panes[job.pane]));
  char secs[32];
  std::snprintf(secs, sizeof(secs), "  (%.1f s)",
                static_cast<double>(xanatoo::sdl::ticks() - job.started) / 1000.0);
  flash_status(app, http_error ? service + ": " + r.error
                               : "Opened " + r.final_url + secs);
}

}  // namespace

bool remote_fetch_supported(std::string_view url, std::string* why) {
  auto no = [&](const char* msg) {
    if (why) *why = msg;
    return false;
  };
  if (xanatoo::is_search_ref(url)) {
    const auto plan = xanatoo::search_plan(url, xanatoo::load_engines());
    if (!plan) return no("unknown search engine");
    if (xanatoo::is_json_search_kind(plan->kind)) {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
      return true;
#else
      return no("this search needs HTTPS (build with a network feature)");
#endif
    }
    return remote_fetch_supported(xanatoo::search_target(url, xanatoo::load_engines()),
                                  why);
  }
  if (xanatoo::is_media_ref(url)) {
    if (!xanatoo::media_plan(url)) return no("invalid media record");
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
    return true;
#else
    return no("media records require HTTPS support");
#endif
  }
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
  if (xanatoo::is_youtube_ref(url)) {
#if defined(XANATOO_YOUTUBE) && XANATOO_YOUTUBE
    return true;
#else
    return no("YouTube needs build with -DXANATOO_YOUTUBE=ON");
#endif
  }
  if (xanatoo::is_ia_ref(url)) {
#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
    return true;
#else
    return no("The Internet Archive needs a build with a network feature");
#endif
  }
  if (xanatoo::is_video_site_ref(url)) {
#if defined(XANATOO_VIDEO) && XANATOO_VIDEO
    return true;
#else
    return no("Video sites need build with -DXANATOO_VIDEO=ON");
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
                     const std::string& url, bool record_history,
                     BufferId tab_buf) {
  if (purpose == FetchPurpose::Navigate)
    cancel_fetches(app, pane);
  else if (purpose == FetchPurpose::Preview)
    cancel_preview_fetch(app);
  FetchJob job;
  job.id = app.next_fetch_id++;
  job.purpose = purpose;
  job.pane = pane;
  job.url = url;
  job.record_history = record_history;
  job.buf = tab_buf;
  job.started = xanatoo::sdl::ticks();
  job.shared = std::make_shared<FetchShared>();
  job.shared->force_download = purpose == FetchPurpose::Download;
  if (purpose != FetchPurpose::Preview && purpose != FetchPurpose::Subscription) {
    job.shared->download = std::make_shared<xanatoo::DownloadProgress>();
    job.shared->download->url = url;
    app.downloads.push_back(job.shared->download);
  }
  // There is no pthread runtime in the initial browser build. Its network
  // services are disabled, so resolve unsupported fetches synchronously.
#ifdef __EMSCRIPTEN__
  fetch_page(url, job.shared, purpose != FetchPurpose::Preview &&
                              purpose != FetchPurpose::Subscription);
#else
  // Detached: a cancelled job just finishes into its (orphaned) shared state.
  std::thread(fetch_page, url, job.shared,
              purpose != FetchPurpose::Preview &&
              purpose != FetchPurpose::Subscription).detach();
#endif
  app.fetches.push_back(std::move(job));
  return app.fetches.back().id;
}

void start_download(App& app, const std::string& url) {
  if (!xanatoo::starts_with_ci(url, "https://") &&
      !xanatoo::starts_with_ci(url, "http://")) {
    flash_status(app, "Only web links can be downloaded: " + url);
    return;
  }
  start_fetch(app, FetchPurpose::Download, -1, url, false);
  flash_status(app, "Downloading " + url);
}

void poll_fetches(App& app) {
  for (const auto& d : app.downloads)
    if (d->phase.load(std::memory_order_acquire) !=
            xanatoo::DownloadPhase::Pending &&
        d->phase.load(std::memory_order_acquire) !=
            xanatoo::DownloadPhase::Skipped &&
        !d->announced.exchange(true))
      app.downloads_panel_open = true;
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
    // Workers may have added Nostr events and newly seen public keys. Update
    // the explorer from the main thread after the fetch, not on every frame.
    if (std::string_view(job.shared->service) == "Nostr" &&
        (app.nostr_explorer_open || app.nostr_accounts_open))
      refresh_nostr_catalogue(app);
    if (job.shared->download && job.shared->download->phase.load() ==
                                    xanatoo::DownloadPhase::Pending)
      job.shared->download->phase.store(xanatoo::DownloadPhase::Skipped);
    if (job.purpose == FetchPurpose::Subscription) {
      finish_subscription_fetch(app, job);
    } else if (job.purpose == FetchPurpose::Navigate) {
      finish_navigate(app, job);
    } else if (job.purpose == FetchPurpose::Tab) {
      finish_tab(app, job);
    } else if (job.purpose == FetchPurpose::Download) {
      // Success shows in the Downloads panel; say why when nothing came.
      const auto& d = job.shared->download;
      if (!d || d->phase.load() == xanatoo::DownloadPhase::Pending ||
          d->phase.load() == xanatoo::DownloadPhase::Skipped)
        flash_status(app, "Download failed: " +
                              (job.shared->error.empty() ? job.url
                                                         : job.shared->error));
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
  app.downloads.erase(std::remove_if(app.downloads.begin(), app.downloads.end(),
      [](const auto& d) {
        return d->phase.load(std::memory_order_acquire) ==
               xanatoo::DownloadPhase::Skipped;
      }), app.downloads.end());
  bool save_history = false;
  for (const auto& d : app.downloads)
    if (d->phase.load(std::memory_order_acquire) ==
            xanatoo::DownloadPhase::Complete && !d->recorded.exchange(true))
      save_history = true;
  if (save_history) save_download_history(app);
  // Keep a bounded in-session list without losing active transfers.
  for (auto it = app.downloads.begin();
       app.downloads.size() > 200 && it != app.downloads.end();) {
    const auto phase = (*it)->phase.load(std::memory_order_acquire);
    if (phase == xanatoo::DownloadPhase::Complete ||
        phase == xanatoo::DownloadPhase::Failed ||
        phase == xanatoo::DownloadPhase::Cancelled)
      it = app.downloads.erase(it);
    else
      ++it;
  }
}

bool cancel_fetches(App& app, int pi) {
  const size_t before = app.fetches.size();
  app.fetches.erase(
      std::remove_if(app.fetches.begin(), app.fetches.end(),
                     [&](const FetchJob& j) {
                       const bool drop = j.purpose == FetchPurpose::Navigate &&
                                         (pi < 0 || j.pane == pi);
                       if (drop) j.shared->abandoned.store(true);
                       return drop;
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
    if (j.purpose == FetchPurpose::Subscription) continue;
    if (j.pane != pi) continue;
    if (j.purpose == FetchPurpose::Navigate) return &j;
    preview = &j;
  }
  return preview;
}

const CachedPage* find_cached_page(App& app, const std::string& url) {
  const Uint32 now = xanatoo::sdl::ticks();
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
  const Uint32 now = xanatoo::sdl::ticks();
  const float t = static_cast<float>(now - job->started) / 1000.f;
  // ImGui's tab strip (drawn last) overlaps the pane's first pixels.
  const int top = p.y + 2;
  const int w = std::max(1, p.w - sb_reserve(p));
  xanatoo::sdl::set_clip_rect(app.sdl, nullptr);
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
  xanatoo::sdl::draw_rect(app.sdl, &pill);
  app.renderer.draw_text(font, static_cast<float>(pill.x + pad),
                         static_cast<float>(pill.y + 3 + font.ascender()),
                         label, pal.notice_text.r, pal.notice_text.g,
                         pal.notice_text.b);
}

}  // namespace xanatoo::app
