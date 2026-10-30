// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// The "about:" page (Help → About xanatoo): services, formats, images and
// libraries of this build, from the feature switches it was compiled with
// and the library versions it runs against. Every service is listed, also
// the ones that are always built, so it stays the one place that says what
// xanatoo talks to.

#include "xanatoo/about.hpp"
#include "xanatoo/app.hpp"
#include "xanatoo/epub_import.hpp"
#include "xanatoo/image_backend.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/paths.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/version.hpp"

#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <imgui.h>
#include <sqlite3.h>

#if XANATOO_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif
#if XANATOO_THUMTOO
#include <thumtoo/version.hpp>
#endif

namespace xanatoo::app {

namespace {

using xanatoo::AboutItem;
using xanatoo::AboutSection;

#if XANATOO_HTTPS
constexpr bool kHttps = true;
#else
constexpr bool kHttps = false;
#endif

std::string join(const std::vector<std::string>& v, const char* sep) {
  std::string out;
  for (const std::string& s : v) out += (out.empty() ? "" : sep) + s;
  return out;
}

std::string code_list(const std::vector<std::string>& v) {
  std::vector<std::string> q;
  for (const std::string& s : v) q.push_back("`" + s + "`");
  return join(q, ", ");
}

std::string version3(int a, int b, int c) {
  return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c);
}

AboutSection services() {
  AboutSection s{"Services", "Pages xanatoo fetches and maps to its own "
                             "document model. Every one is read-only.",
                 {}};
  s.items.push_back({"Web pages", XANATOO_WEB,
                     "any other http(s) page as a reader view",
                     "https://example.org/", "`-DXANATOO_WEB=ON`"});
  s.items.push_back({"Site maps", XANATOO_WEB,
                     "`sitemap:host` — a site's structure from its XML "
                     "sitemaps (also the Sitemap panel)",
                     "sitemap:www.gnu.org", "`-DXANATOO_WEB=ON`"});
  s.items.push_back({"Wikipedia", XANATOO_WIKIPEDIA,
                     "`wikipedia:Title` articles via the Wikimedia REST API",
                     "wikipedia:Project_Xanadu", "`-DXANATOO_WIKIPEDIA=ON`"});
  s.items.push_back({"Hacker News", XANATOO_HN,
                     "`hn:` lists and threads (Firebase + Algolia APIs)",
                     "hn:", "`-DXANATOO_HN=ON`"});
  s.items.push_back({"Reddit", XANATOO_REDDIT,
                     "`reddit:r/…` subreddits and threads (public .json)",
                     "reddit:r/linux/", "`-DXANATOO_REDDIT=ON`"});
  s.items.push_back({"Lemmy", XANATOO_LEMMY,
                     "`lemmy:instance/c/…` communities and posts (API v3)",
                     "lemmy:lemmy.world/c/technology", "`-DXANATOO_LEMMY=ON`"});
  s.items.push_back({"Nostr", XANATOO_NOSTR,
                     "`note1…` / `npub1…` / `nostr:…` notes, threads and "
                     "profiles over relay WebSockets",
                     "nostr:recent", "`-DXANATOO_NOSTR=ON`"});
  s.items.push_back({"Gemini", XANATOO_GEMINI, "`gemini://` capsules",
                     "gemini://geminiprotocol.net/", "`-DXANATOO_GEMINI=ON`"});
  s.items.push_back({"RSS / Atom feeds", XANATOO_FEEDS,
                     "feed URLs, `feed:` finds a page's feed",
                     "examples/feeds.md", "`-DXANATOO_FEEDS=ON`"});
  s.items.push_back({"Ebooks", XANATOO_EBOOKS,
                     "Standard Ebooks catalogue, Project Gutenberg and "
                     "`.epub` downloads",
                     "examples/books.md", "`-DXANATOO_EBOOKS=ON`"});
  if (kHttps)
    s.items.push_back({"TLS", true,
                       std::string(xanatoo::TlsStream::backend()) +
                           " (peer certificates are not verified yet)",
                       {}, {}});
  else
    s.items.push_back({"TLS", false, "every network service needs it",
                       {}, "a network service"});
  return s;
}

AboutSection formats() {
  AboutSection s{"Document formats", {}, {}};
  s.items.push_back({"Markdown", true, "`.md`, editable", {}, {}});
  s.items.push_back({"Org", true, "`.org`, editable", {}, {}});
  s.items.push_back({"S-expressions", true, "`.sexp`, xanatoo's own tree",
                     "examples/semantic.sexp", {}});
  s.items.push_back({"Gemtext", true, "`.gmi`, `.gemini`",
                     "examples/gemini-sample.gmi", {}});
  s.items.push_back({"HTML", true, "`.html`, read-only", {}, {}});
  s.items.push_back({"EPUB", true, "`.epub`, read-only, built in when pandoc is missing",
                     {}, {}});
  const bool pandoc = xanatoo::pandoc_available();
  s.items.push_back(
      {"Pandoc import", pandoc,
       code_list(xanatoo::pandoc_extensions()) +
           ", read-only through cached Markdown",
       {}, "`pandoc` on PATH"});
  s.items.push_back({"Style sheets", true, "`styles/*.xss` (View → Style sheet)",
                     {}, {}});
  return s;
}

AboutSection images() {
  AboutSection s{"Images", {}, {}};
#if XANATOO_THUMTOO
  s.items.push_back({"thumtoo", true,
                     std::string(thumtoo::version_string()) +
                         "; thumbnails from a tile cache shared with galapix "
                         "and biltoo",
                     {}, {}});
  s.items.push_back({"libvips", true,
                     xanatoo::image_backend_vips_version() + ": " +
                         code_list(xanatoo::image_backend_suffixes()),
                     {}, {}});
  // Only the backends that change what xanatoo shows: thumbnails of
  // documents used as images. Archives / curl / D-Bus are not used here.
  s.items.push_back({"MuPDF", thumtoo::feature_mupdf(),
                     "PDF / EPUB pages as image thumbnails", {},
                     "thumtoo with MuPDF"});
  s.items.push_back({"DjVuLibre", thumtoo::feature_djvu(),
                     "DjVu pages as image thumbnails", {},
                     "thumtoo with DjVuLibre"});
#else
  s.items.push_back({"thumtoo", false, "libvips formats, shared tile cache",
                     {}, "`-DXANATOO_THUMTOO=ON`"});
  s.items.push_back({"stb_image", true, "PNG, JPEG, GIF, BMP", {}, {}});
#endif
  s.items.push_back({"Remote images", kHttps,
                     "http(s) images download in the background", {},
                     "a network service"});
  return s;
}

AboutSection libraries(App& app) {
  AboutSection s{"Platform", {}, {}};
  SDL_version sv;
  SDL_GetVersion(&sv);
  std::string sdl = version3(sv.major, sv.minor, sv.patch) + " on " +
                    SDL_GetPlatform();
  if (const char* v = SDL_GetCurrentVideoDriver()) sdl += ", video " + std::string(v);
  SDL_RendererInfo ri;
  if (app.sdl && SDL_GetRendererInfo(app.sdl, &ri) == 0)
    sdl += ", renderer " + std::string(ri.name);
  s.items.push_back({"SDL", true, sdl, {}, {}});
  s.items.push_back({"FreeType", true,
                     version3(FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH),
                     {}, {}});
#if XANATOO_FONTCONFIG
  const int fc = FcGetVersion();
  s.items.push_back({"FontConfig", true,
                     version3(fc / 10000, fc / 100 % 100, fc % 100) +
                         ", font families by name",
                     {}, {}});
#else
  s.items.push_back({"FontConfig", false, "font families by name", {},
                     "`-DXANATOO_FONTCONFIG=ON`"});
#endif
  s.items.push_back({"SQLite", XANATOO_SQLITE,
                     std::string(sqlite3_libversion()) +
                         ", persisted folds and view state",
                     {}, "`-DXANATOO_SQLITE=ON`"});
  s.items.push_back({"Dear ImGui", true, IMGUI_VERSION ", menus and toolbars",
                     {}, {}});
  s.items.push_back({"Drafts", true, "unsaved edits in the revision store",
                     "drafts:", {}});
  for (const auto& [label, dir] :
       {std::pair<const char*, std::string>{"Settings", xanatoo::config_dir()},
        {"State", xanatoo::state_dir()},
        {"User data", xanatoo::user_data_dir()},
        {"Cache", xanatoo::cache_dir()},
        {"Data", xanatoo::data_dir()}})
    if (!dir.empty()) s.items.push_back({label, true, "`" + dir + "`", {}, {}});
  return s;
}

}  // namespace

void open_about_page(App& app) {
  xanatoo::AboutInfo info;
  info.version = xanatoo::version_string();
  info.sections = {services(), formats(), images(), libraries(app)};
  show_doc_in_pane(app, app.focus,
                   xanatoo::parse_document(xanatoo::about_markdown(info), {}),
                   kAboutPage);
}

}  // namespace xanatoo::app
