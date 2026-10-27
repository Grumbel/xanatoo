// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/wikipedia.hpp"

#include "xanatoo/html_min.hpp"
#include "xanatoo/html_page.hpp"
#include "xanatoo/util.hpp"

#if defined(XANATOO_WIKIPEDIA) && XANATOO_WIKIPEDIA
#include "xanatoo/https.hpp"
#endif

#include <array>
#include <cctype>
#include <iostream>

namespace xanatoo {

namespace {

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percent_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    int hi = 0, lo = 0;
    if (s[i] == '%' && i + 2 < s.size() && (hi = hex_val(s[i + 1])) >= 0 &&
        (lo = hex_val(s[i + 2])) >= 0) {
      out.push_back(static_cast<char>(hi * 16 + lo));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

// Title path segment: unreserved characters plus a few MediaWiki keeps
// readable; '/' and '?' must be escaped for the REST API.
std::string percent_encode_title(std::string_view s) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '~' ||
        c == '(' || c == ')' || c == ',' || c == ':' || c == '!' ||
        c == '\'' || c == '*') {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 15]);
    }
  }
  return out;
}

std::string normalize_title(std::string t) {
  for (char& c : t)
    if (c == ' ') c = '_';
  while (!t.empty() && t.front() == '_') t.erase(0, 1);
  while (!t.empty() && t.back() == '_') t.pop_back();
  return t;
}

// "de", "en", "simple", "zh-yue", "be-tarask".
bool is_lang_code(std::string_view s) {
  if (s.size() < 2 || s.size() > 12) return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || c == '-')) return false;
  return s.front() != '-' && s.back() != '-';
}

// Namespace prefixes (English + a few big wikis) whose pages are not
// articles; "…talk" / "Diskussion" variants are caught generically.
bool is_non_article_title(std::string_view title) {
  size_t colon = title.find(':');
  if (colon == std::string_view::npos || colon == 0) return false;
  std::string ns = ascii_lower(std::string(title.substr(0, colon)));
  if (ns.find('_') != std::string::npos && !ends_with(ns, "_talk"))
    return false;  // "Star_Wars:_Episode_IV" style article titles
  if (ends_with(ns, "talk") || ns.find("diskussion") != std::string::npos)
    return true;
  static constexpr std::array<std::string_view, 32> kNamespaces = {
      "file",      "image",     "media",     "category",  "help",
      "special",   "template",  "wikipedia", "wp",        "portal",
      "user",      "draft",     "module",    "mediawiki", "timedtext",
      "book",      "datei",     "bild",      "kategorie", "hilfe",
      "spezial",   "vorlage",   "benutzer",  "fichier",   "catégorie",
      "modèle",    "spécial",   "utilisateur", "archivo", "categoría",
      "especial",  "plantilla"};
  for (std::string_view n : kNamespaces)
    if (ns == n) return true;
  return false;
}

}  // namespace

std::optional<WikipediaRef> parse_wikipedia_ref(std::string_view s) {
  if (!starts_with_ci(s, kWikipediaPrefix)) return std::nullopt;
  s.remove_prefix(kWikipediaPrefix.size());
  WikipediaRef r;
  if (size_t hash = s.find('#'); hash != std::string_view::npos) {
    r.fragment = std::string(s.substr(hash + 1));
    s = s.substr(0, hash);
  }
  if (size_t colon = s.find(':');
      colon != std::string_view::npos && is_lang_code(s.substr(0, colon))) {
    r.lang = std::string(s.substr(0, colon));
    s.remove_prefix(colon + 1);
  }
  r.title = normalize_title(percent_decode(s));
  if (r.title.empty()) return std::nullopt;
  return r;
}

bool is_wikipedia_ref(std::string_view s) {
  return parse_wikipedia_ref(s).has_value();
}

std::string wikipedia_ref_string(const WikipediaRef& r) {
  std::string out(kWikipediaPrefix);
  if (r.lang != "en") out += r.lang + ":";
  out += r.title;
  if (!r.fragment.empty()) out += "#" + r.fragment;
  return out;
}

std::string wikipedia_rest_url(const WikipediaRef& r) {
  return "https://" + r.lang + ".wikipedia.org/api/rest_v1/page/html/" +
         percent_encode_title(r.title);
}

std::string wikipedia_page_url(const WikipediaRef& r) {
  return "https://" + r.lang + ".wikipedia.org/wiki/" +
         percent_encode_title(r.title);
}

std::optional<WikipediaRef> wikipedia_ref_from_url(std::string_view url) {
  if (starts_with_ci(url, "https://"))
    url.remove_prefix(8);
  else if (starts_with_ci(url, "http://"))
    url.remove_prefix(7);
  else if (starts_with(url, "//"))
    url.remove_prefix(2);
  else
    return std::nullopt;
  size_t slash = url.find('/');
  if (slash == std::string_view::npos) return std::nullopt;
  std::string host = ascii_lower(std::string(url.substr(0, slash)));
  std::string_view path = url.substr(slash);
  constexpr std::string_view kSuffix = ".wikipedia.org";
  if (!ends_with(host, kSuffix)) return std::nullopt;
  host.resize(host.size() - kSuffix.size());
  if (ends_with(host, ".m")) host.resize(host.size() - 2);
  if (!is_lang_code(host)) return std::nullopt;
  if (!starts_with(path, "/wiki/")) return std::nullopt;
  path.remove_prefix(6);
  WikipediaRef r;
  r.lang = host;
  if (size_t hash = path.find('#'); hash != std::string_view::npos) {
    r.fragment = std::string(path.substr(hash + 1));
    path = path.substr(0, hash);
  }
  if (path.find('?') != std::string_view::npos) return std::nullopt;
  r.title = normalize_title(percent_decode(path));
  if (r.title.empty() || is_non_article_title(r.title)) return std::nullopt;
  return r;
}

void wikipedia_rewrite_links(Document& doc, const WikipediaRef& self) {
  for (Node& n : doc.nodes) {
    for (InlineSpan& s : n.spans) {
      if (s.kind != InlineKind::Link || s.href.empty() || s.href[0] == '#')
        continue;
      auto r = wikipedia_ref_from_url(s.href);
      if (!r) continue;
      if (r->lang == self.lang && r->title == self.title)
        s.href = r->fragment.empty() ? std::string("#") : "#" + r->fragment;
      else
        s.href = wikipedia_ref_string(*r);
    }
  }
}

#if defined(XANATOO_WIKIPEDIA) && XANATOO_WIKIPEDIA

std::optional<Document> wikipedia_fetch(std::string_view ref_in,
                                        std::string* err,
                                        std::string* final_ref) {
  // Wikimedia asks for a descriptive User-Agent with a contact URL.
  static const char* kUserAgent =
      "xanatoo/0.1 (https://github.com/Grumbel/xanatoo; document reader)";
  auto ref = parse_wikipedia_ref(ref_in);
  if (!ref) {
    if (err) *err = "not a wikipedia: reference";
    return std::nullopt;
  }
  const std::string url = wikipedia_rest_url(*ref);
  std::cerr << "Wikipedia: GET " << url << "\n";
  auto resp = https_get_follow(url, kUserAgent, err);
  if (!resp) return std::nullopt;
  if (resp->status == 404) {
    if (err) *err = "no article \"" + ref->title + "\" on " + ref->lang +
                    ".wikipedia.org";
    return std::nullopt;
  }
  if (resp->status != 200) {
    if (err) *err = "HTTP " + std::to_string(resp->status);
    return std::nullopt;
  }
  std::string body = to_utf8(std::move(resp->body), "utf-8");
  // Redirects ("WWW" → "World_Wide_Web") end on the target's HTML, whose
  // <link rel="dc:isVersionOf" href="//en.wikipedia.org/wiki/…"> names
  // the canonical title.
  if (size_t rel = body.find("rel=\"dc:isVersionOf\"");
      rel != std::string::npos) {
    size_t lt = body.rfind('<', rel);
    size_t gt = body.find('>', rel);
    if (lt != std::string::npos && gt != std::string::npos) {
      std::string_view tag = std::string_view(body).substr(lt, gt - lt);
      if (auto canon = wikipedia_ref_from_url(html_attr(tag, "href"))) {
        canon->fragment = ref->fragment;
        *ref = std::move(*canon);
      }
    }
  }
  if (final_ref) *final_ref = wikipedia_ref_string(*ref);

  Document doc = parse_html_page(body, wikipedia_page_url(*ref));
  wikipedia_rewrite_links(doc, *ref);
  return doc;
}

#endif  // XANATOO_WIKIPEDIA

}  // namespace xanatoo
