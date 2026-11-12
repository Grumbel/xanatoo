// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/archive.hpp"
#include "xanatoo/http_status.hpp"

#include "xanatoo/html_min.hpp"
#include "xanatoo/json_dom.hpp"
#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"
#include "xanatoo/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
#include "xanatoo/https.hpp"
#endif

namespace xanatoo {

namespace {

constexpr std::string_view kPrefix = "ia:";

bool valid_id(std::string_view id) {
  if (id.empty() || id.size() > 200) return false;
  for (char c : id) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!(std::isalnum(u) || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}

std::string percent_encode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
      out += c;
    else {
      out += '%';
      out += hex[u >> 4];
      out += hex[u & 15];
    }
  }
  return out;
}

// Running text on one line, blanks collapsed.
std::string one_line(std::string_view s) {
  std::string out;
  bool blank = false;
  for (char c : s) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      blank = true;
      continue;
    }
    if (blank && !out.empty()) out += ' ';
    blank = false;
    out += c;
  }
  return out;
}

// Markdown running text: specials escaped, line-start markers too.
std::string esc(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const bool start = out.empty() || out.back() == '\n';
    if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' ||
        c == '<' || c == '>' || ((c == '#' || c == '-' || c == '+') && start))
      out += '\\';
    out += c;
  }
  return out;
}

// Link text is literal in our parser: only brackets end it early.
std::string link(std::string_view text, std::string_view url) {
  std::string t = one_line(text);
  for (char& c : t)
    if (c == '[') c = '(';
    else if (c == ']') c = ')';
  std::string u;
  for (char c : url) {
    if (c == '(') u += "%28";
    else if (c == ')') u += "%29";
    else if (c == ' ') u += "%20";
    else if (c == '<') u += "%3C";
    else if (c == '>') u += "%3E";
    else u += c;
  }
  return "[" + t + "](" + u + ")";
}

// A metadata value is a string or an array of strings (or numbers).
std::vector<std::string> values(const Json* v) {
  std::vector<std::string> out;
  if (!v) return out;
  auto one = [&](const Json& j) {
    if (j.type == Json::Type::String) out.push_back(j.s);
    else if (j.type == Json::Type::Number) out.push_back(std::to_string(static_cast<long long>(j.n)));
  };
  if (v->type == Json::Type::Array)
    for (const Json& j : v->items) one(j);
  else
    one(*v);
  return out;
}

std::string joined(const std::vector<std::string>& v, const char* sep, size_t max = 12) {
  std::string out;
  for (size_t i = 0; i < v.size() && i < max; ++i) {
    if (i) out += sep;
    out += esc(one_line(v[i]));
  }
  if (v.size() > max) out += sep + std::string("…");
  return out;
}

std::string human_size(long long n) {
  char b[32];
  if (n >= (1LL << 30)) std::snprintf(b, sizeof b, "%.1f GB", n / double(1LL << 30));
  else if (n >= (1LL << 20)) std::snprintf(b, sizeof b, "%.1f MB", n / double(1LL << 20));
  else if (n >= (1LL << 10)) std::snprintf(b, sizeof b, "%.1f KB", n / double(1LL << 10));
  else std::snprintf(b, sizeof b, "%lld bytes", n);
  return b;
}

// Description HTML → paragraphs (blank line separated), escaped.
std::string description_md(std::string_view html) {
  const std::string plain = html_to_plain(html);
  std::string out, para;
  auto flush = [&] {
    const std::string t = one_line(para);
    if (!t.empty()) out += esc(t) + "\n\n";
    para.clear();
  };
  size_t i = 0;
  while (i <= plain.size()) {
    const size_t e = plain.find('\n', i);
    const std::string_view line =
        std::string_view(plain).substr(i, e == std::string::npos ? e : e - i);
    if (one_line(line).empty()) flush();
    else para += std::string(line) + " ";
    if (e == std::string::npos) break;
    i = e + 1;
  }
  flush();
  return out;
}

bool playable_format(std::string_view f) {
  return f == "MPEG4" || f == "h.264" || f == "h.264 IA" || f == "512Kb MPEG4" ||
         f == "Ogg Video" || f == "Matroska" || f == "VBR MP3" || f == "MP3" ||
         f == "128Kbps MP3" || f == "64Kbps MP3" || f == "Ogg Vorbis" || f == "Flac";
}

// Files worth offering, originals first; bookkeeping files are left out.
bool hidden_file(const std::string& name, const std::string& format) {
  if (format == "Metadata" || format == "Archive BitTorrent" || format == "Item Tile")
    return true;
  if (ends_with(name, "_meta.xml") || ends_with(name, "_files.xml") ||
      ends_with(name, "_meta.sqlite") || ends_with(name, "_meta.txt") ||
      ends_with(name, "_reviews.xml") || ends_with(name, "_archive.torrent"))
    return true;
  return false;
}

std::string members_md(std::string_view members_json, const IaRef& ref) {
  auto reply = json_parse(members_json);
  if (!reply) return {};
  const Json* resp = json_get(*reply, "response");
  const Json* docs = resp ? json_get(*resp, "docs") : nullptr;
  if (!docs || docs->items.empty()) return "This collection has no items.\n";
  const long long total = json_number(json_get(*resp, "numFound"));
  std::string md = "## Items\n\n";
  if (total > 0)
    md += std::to_string(total) + (total == 1 ? " item" : " items") +
          ", the most downloaded first.\n\n";
  for (const Json& d : docs->items) {
    const std::string id = json_string(json_get(d, "identifier"));
    if (!valid_id(id)) continue;
    const auto ts = values(json_get(d, "title"));
    md += "- " + link(ts.empty() ? id : ts[0], "ia:" + id);
    std::string meta = json_string(json_get(d, "mediatype"));
    const auto by = values(json_get(d, "creator"));
    if (!by.empty()) meta += (meta.empty() ? "" : " · ") + one_line(by[0]);
    if (!meta.empty()) md += " — " + esc(meta);
    md += "\n";
  }
  md += "\n";
  const long long shown = static_cast<long long>(ref.page) * kIaPageRows;
  if (ref.page > 1)
    md += link("Previous page", ia_ref_string({ref.id, ref.page - 1})) + "  ";
  if (shown < total)
    md += link("Next page", ia_ref_string({ref.id, ref.page + 1}));
  md += "\n";
  return md;
}

}  // namespace

std::optional<IaRef> parse_ia_ref(std::string_view ref) {
  if (!starts_with_ci(ref, kPrefix)) return std::nullopt;
  std::string_view rest = ref.substr(kPrefix.size());
  IaRef r;
  if (const size_t q = rest.find('?'); q != std::string_view::npos) {
    std::string_view query = rest.substr(q + 1);
    rest = rest.substr(0, q);
    while (!query.empty()) {
      const size_t amp = query.find('&');
      const std::string_view kv = query.substr(0, amp);
      if (starts_with(kv, "page=")) r.page = std::max(1, std::atoi(std::string(kv.substr(5)).c_str()));
      query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
    }
  }
  if (const size_t h = rest.find('#'); h != std::string_view::npos) rest = rest.substr(0, h);
  for (std::string_view p : {"details/", "download/", "stream/", "embed/"})
    if (starts_with(rest, p)) rest.remove_prefix(p.size());
  if (const size_t s = rest.find('/'); s != std::string_view::npos) rest = rest.substr(0, s);
  if (!valid_id(rest)) return std::nullopt;
  r.id = std::string(rest);
  return r;
}

bool is_ia_ref(std::string_view ref) { return parse_ia_ref(ref).has_value(); }

std::string ia_ref_string(const IaRef& r) {
  std::string s = "ia:" + r.id;
  if (r.page > 1) s += "?page=" + std::to_string(r.page);
  return s;
}

std::string ia_web_url(const IaRef& r) { return "https://archive.org/details/" + r.id; }

std::string ia_ref_from_web_url(std::string_view url) {
  for (std::string_view p : {"https://archive.org/", "http://archive.org/",
                             "https://www.archive.org/", "http://www.archive.org/"}) {
    if (!starts_with_ci(url, p)) continue;
    std::string_view rest = url.substr(p.size());
    for (std::string_view d : {"details/", "download/", "stream/", "embed/"})
      if (starts_with(rest, d)) {
        auto r = parse_ia_ref(std::string("ia:") + std::string(rest));
        return r ? ia_ref_string({r->id, 1}) : std::string();
      }
  }
  return {};
}

std::string ia_metadata_url(const IaRef& r) {
  return "https://archive.org/metadata/" + percent_encode(r.id);
}

std::string ia_members_url(const IaRef& r) {
  return "https://archive.org/advancedsearch.php?q=collection%3A" + percent_encode(r.id) +
         "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=mediatype&fl%5B%5D=creator"
         "&sort%5B%5D=downloads+desc&rows=" + std::to_string(kIaPageRows) +
         "&page=" + std::to_string(r.page) + "&output=json";
}

std::string ia_mediatype(std::string_view metadata_json) {
  auto j = json_parse(metadata_json);
  if (!j) return {};
  return json_string(json_path(*j, "metadata/mediatype"));
}

std::string ia_markdown(std::string_view metadata_json, std::string_view members_json,
                        const IaRef& ref) {
  auto reply = json_parse(metadata_json);
  const Json* meta = reply ? json_get(*reply, "metadata") : nullptr;
  if (!meta || meta->type != Json::Type::Object) {
    return "# Not found: " + esc(ref.id) + "\n\nThe Internet Archive has no public item `" +
           ref.id + "`, or it is not available.\n\n" +
           link("Open on archive.org", "external:" + ia_web_url(ref)) + "\n";
  }
  auto field = [&](const char* k) { return values(json_get(*meta, k)); };
  const std::string type = json_string(json_get(*meta, "mediatype"));
  const auto titles = field("title");
  std::string md = "# " + esc(titles.empty() ? ref.id : one_line(titles[0])) + "\n\n";

  if (ref.page <= 1 || type != "collection") {
    md += "![](https://archive.org/services/img/" + ref.id + ")\n\n";
    std::string facts;
    auto add = [&](const char* label, const std::string& v) {
      if (!v.empty()) facts += "- " + std::string(label) + ": " + v + "\n";
    };
    add("Creator", joined(field("creator"), "; "));
    add("Date", joined(field("date"), "; ", 1));
    add("Publisher", joined(field("publisher"), "; ", 3));
    add("Language", joined(field("language"), ", ", 5));
    add("Type", esc(type));
    std::string colls;
    for (const std::string& c : field("collection"))
      if (valid_id(c)) colls += (colls.empty() ? "" : ", ") + link(c, "ia:" + c);
    add("Collections", colls);
    const auto subj = field("subject");
    add("Subjects", joined(subj, "; "));
    {
      const auto rights = field("rights");
      if (!rights.empty()) add("Rights", esc(one_line(html_to_plain(rights[0]))));
    }
    const auto lic = field("licenseurl");
    if (!lic.empty() && (starts_with(lic[0], "http://") || starts_with(lic[0], "https://")))
      add("License", link(lic[0], "external:" + lic[0]));
    const auto src = field("source");
    if (!src.empty() && (starts_with(src[0], "http://") || starts_with(src[0], "https://")))
      add("Source", link(src[0], src[0]));
    if (!facts.empty()) md += facts + "\n";

    const auto desc = field("description");
    if (!desc.empty()) md += description_md(desc[0]);

    // Files.
    const Json* files = json_get(*reply, "files");
    struct F { std::string name, format, source; long long size; };
    std::vector<F> list;
    if (files && type != "collection")  // a collection's files are its own icons
      for (const Json& f : files->items) {
        F x{json_string(json_get(f, "name")), json_string(json_get(f, "format")),
            json_string(json_get(f, "source")), json_number(json_get(f, "size"))};
        if (x.name.empty() || hidden_file(x.name, x.format)) continue;
        list.push_back(std::move(x));
      }
    std::stable_sort(list.begin(), list.end(), [](const F& a, const F& b) {
      return (a.source == "original") > (b.source == "original");
    });
    const std::string base = "https://archive.org/download/" + ref.id + "/";
    for (const F& f : list)
      if (playable_format(f.format) && (type == "movies" || type == "audio" || type == "etree")) {
        md += link("Watch / listen with mpv", "external:mpv:" + base + percent_encode(f.name)) + "\n\n";
        break;
      }
    if (!list.empty()) {
      md += "## Files\n\n";
      size_t n = 0;
      for (const F& f : list) {
        if (++n > 100) {
          md += "- … " + std::to_string(list.size() - 100) + " more on archive.org\n";
          break;
        }
        md += "- " + link(f.name, base + percent_encode(f.name));
        std::string info = f.format;
        if (f.size > 0) info += (info.empty() ? "" : ", ") + human_size(f.size);
        if (!info.empty()) md += " — " + esc(info);
        md += "\n";
      }
      md += "\n";
    }
  }
  if (type == "collection" && !members_json.empty()) md += members_md(members_json, ref);
  md += link("Open on archive.org", "external:" + ia_web_url(ref)) + "\n";
  return md;
}

#if defined(XANATOO_HTTPS) && XANATOO_HTTPS
namespace {
std::optional<std::string> get(const std::string& url, std::string* err) {
  static const std::string ua = user_agent("archive");
  auto r = https_get_follow(url, ua, err);
  if (!r) return std::nullopt;
  if (r->status != 200) {
    if (err) *err = http_status_error(r->status, "archive.org");
    return std::nullopt;
  }
  return std::move(r->body);
}
}  // namespace

std::optional<Document> ia_fetch(std::string_view ref, std::string* err) {
  const auto r = parse_ia_ref(ref);
  if (!r) {
    if (err) *err = "Not an Internet Archive ref: " + std::string(ref);
    return std::nullopt;
  }
  auto meta = get(ia_metadata_url(*r), err);
  if (!meta) return std::nullopt;
  std::string members;
  if (ia_mediatype(*meta) == "collection") {
    std::string ignored;
    if (auto m = get(ia_members_url(*r), &ignored)) members = std::move(*m);
  }
  return parse_document(ia_markdown(*meta, members, *r), "archive.md");
}
#endif

}  // namespace xanatoo
