// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/util.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace xanatoo {

std::string ascii_lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

bool is_text_node_kind(NodeKind k) {
  return k == NodeKind::Paragraph || k == NodeKind::Heading ||
         k == NodeKind::Header || k == NodeKind::ListItem ||
         k == NodeKind::BlockQuote ||
         k == NodeKind::Note || k == NodeKind::CodeBlock ||
         k == NodeKind::Term || k == NodeKind::Definition ||
         k == NodeKind::Caption || k == NodeKind::BiblioEntry ||
         k == NodeKind::TableCell;
}

void collect_text_nodes(const Document& doc, NodeId id,
                        std::vector<NodeId>& out) {
  if (id == INVALID_NODE) return;
  const Node& n = doc.get(id);
  // n.id != id means get() returned the invalid-node sentinel (stale id).
  if (n.id != id) return;
  if (is_text_node_kind(n.kind)) out.push_back(id);
  for (NodeId c : n.children) collect_text_nodes(doc, c, out);
}

std::string plain_text(const Document& doc, NodeId id) {
  if (id == INVALID_NODE) return {};
  const Node& n = doc.get(id);
  std::string out;
  for (const auto& sp : n.spans) out += sp.text;
  return out;
}

NodeId find_footnote(const Document& doc, std::string_view key) {
  const std::string klow = ascii_lower(std::string(key));
  for (const Node& n : doc.nodes) {
    if (n.id != INVALID_NODE && n.kind == NodeKind::Footnote &&
        ascii_lower(n.label) == klow)
      return n.id;
  }
  return INVALID_NODE;
}

NodeId find_anchor_node(const Document& doc, std::string_view href) {
  if (href.empty() || href[0] != '#') return INVALID_NODE;
  std::string tlow = ascii_lower(std::string(href.substr(1)));
  if (tlow.empty() || doc.root == INVALID_NODE) return INVALID_NODE;

  // Footnotes live in their own key space: #fn:key → first text inside.
  if (tlow.size() > 3 && tlow.compare(0, 3, "fn:") == 0) {
    NodeId fn = find_footnote(doc, std::string_view(tlow).substr(3));
    if (fn == INVALID_NODE) return INVALID_NODE;
    std::vector<NodeId> body;
    collect_text_nodes(doc, fn, body);
    return body.empty() ? INVALID_NODE : body.front();
  }

  // Prefer stable :label (semantic id) over heading text match.
  // Bibliography keys are labels; cite:key and #key both resolve here.
  if (tlow.size() > 5 && tlow.compare(0, 5, "cite:") == 0)
    tlow = tlow.substr(5);
  for (const Node& n : doc.nodes) {
    if (n.id == INVALID_NODE) continue;
    if (!n.label.empty() && ascii_lower(n.label) == tlow) return n.id;
  }

  std::vector<NodeId> nodes;
  collect_text_nodes(doc, doc.root, nodes);

  for (NodeId id : nodes) {
    if (ascii_lower(plain_text(doc, id)) == tlow) return id;
  }
  for (NodeId id : nodes) {
    if (doc.get(id).kind != NodeKind::Heading &&
        doc.get(id).kind != NodeKind::Header)
      continue;
    if (ascii_lower(plain_text(doc, id)).find(tlow) != std::string::npos)
      return id;
  }
  return INVALID_NODE;
}

bool path_ends_with_ci(std::string_view path, std::string_view suffix) {
  if (path.size() < suffix.size()) return false;
  auto a = ascii_lower(std::string(path.substr(path.size() - suffix.size())));
  auto b = ascii_lower(std::string(suffix));
  return a == b;
}

bool is_raster_image_path(std::string_view path) {
  return path_ends_with_ci(path, ".png") || path_ends_with_ci(path, ".jpg") ||
         path_ends_with_ci(path, ".jpeg") || path_ends_with_ci(path, ".gif") ||
         path_ends_with_ci(path, ".bmp");
}

std::string config_dir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  fs::path base;
  if (xdg && xdg[0]) {
    base = fs::path(xdg) / "xanatoo";
  } else {
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) return {};
    base = fs::path(home) / ".config" / "xanatoo";
  }
  std::error_code ec;
  fs::create_directories(base, ec);
  if (ec) {
    std::cerr << "xanatoo: cannot create config dir " << base << ": "
              << ec.message() << "\n";
    return {};
  }
  return base.string();
}

std::string cache_dir() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0])
    return (fs::path(xdg) / "xanatoo").string();
  if (const char* home = std::getenv("HOME"); home && home[0])
    return (fs::path(home) / ".cache" / "xanatoo").string();
  return {};
}

std::string bookmarks_path() {
  std::string dir = config_dir();
  if (dir.empty()) return {};
  return (fs::path(dir) / "bookmarks").string();
}

std::vector<std::string> load_bookmarks() {
  std::vector<std::string> out;
  std::string path = bookmarks_path();
  if (path.empty()) return out;
  std::ifstream in(path);
  if (!in) return out;
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.pop_back();
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] == '#') continue;
    out.push_back(line.substr(i));
  }
  return out;
}

bool save_bookmarks(const std::vector<std::string>& entries) {
  std::string path = bookmarks_path();
  if (path.empty()) return false;
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    std::cerr << "xanatoo: cannot write bookmarks to " << path << "\n";
    return false;
  }
  out << "# xanatoo bookmarks — one URL or path per line\n";
  for (const auto& e : entries) {
    if (!e.empty()) out << e << '\n';
  }
  return static_cast<bool>(out);
}

void utf8_pop_back(std::string& s) {
  if (s.empty()) return;
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xc0) == 0x80)
    s.pop_back();
  if (!s.empty()) s.pop_back();
}

namespace {

// Latin-1 names, U+00A0 … U+00FF in order.
constexpr std::string_view kLatin1Entities[96] = {
    "nbsp",   "iexcl",  "cent",   "pound",  "curren", "yen",    "brvbar",
    "sect",   "uml",    "copy",   "ordf",   "laquo",  "not",    "shy",
    "reg",    "macr",   "deg",    "plusmn", "sup2",   "sup3",   "acute",
    "micro",  "para",   "middot", "cedil",  "sup1",   "ordm",   "raquo",
    "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc",
    "Atilde", "Auml",   "Aring",  "AElig",  "Ccedil", "Egrave", "Eacute",
    "Ecirc",  "Euml",   "Igrave", "Iacute", "Icirc",  "Iuml",   "ETH",
    "Ntilde", "Ograve", "Oacute", "Ocirc",  "Otilde", "Ouml",   "times",
    "Oslash", "Ugrave", "Uacute", "Ucirc",  "Uuml",   "Yacute", "THORN",
    "szlig",  "agrave", "aacute", "acirc",  "atilde", "auml",   "aring",
    "aelig",  "ccedil", "egrave", "eacute", "ecirc",  "euml",   "igrave",
    "iacute", "icirc",  "iuml",   "eth",    "ntilde", "ograve", "oacute",
    "ocirc",  "otilde", "ouml",   "divide", "oslash", "ugrave", "uacute",
    "ucirc",  "uuml",   "yacute", "thorn",  "yuml"};

// Lower-case Greek, U+03B1 … U+03C9 in order.
constexpr std::string_view kGreekEntities[25] = {
    "alpha", "beta",    "gamma", "delta",   "epsilon", "zeta",  "eta",
    "theta", "iota",    "kappa", "lambda",  "mu",      "nu",    "xi",
    "omicron", "pi",    "rho",   "sigmaf",  "sigma",   "tau",   "upsilon",
    "phi",   "chi",     "psi",   "omega"};

struct NamedEntity {
  std::string_view name;
  uint32_t cp;
};

// Punctuation and symbols common in feeds and blog HTML.
constexpr NamedEntity kOtherEntities[] = {
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},
    {"quot", '"'},      {"apos", '\''},     {"ndash", 0x2013},
    {"mdash", 0x2014},  {"lsquo", 0x2018},  {"rsquo", 0x2019},
    {"sbquo", 0x201A},  {"ldquo", 0x201C},  {"rdquo", 0x201D},
    {"bdquo", 0x201E},  {"dagger", 0x2020}, {"Dagger", 0x2021},
    {"bull", 0x2022},   {"hellip", 0x2026}, {"permil", 0x2030},
    {"prime", 0x2032},  {"Prime", 0x2033},  {"lsaquo", 0x2039},
    {"rsaquo", 0x203A}, {"euro", 0x20AC},   {"trade", 0x2122},
    {"larr", 0x2190},   {"uarr", 0x2191},   {"rarr", 0x2192},
    {"darr", 0x2193},   {"harr", 0x2194},   {"lArr", 0x21D0},
    {"rArr", 0x21D2},   {"hArr", 0x21D4},   {"minus", 0x2212},
    {"infin", 0x221E},  {"asymp", 0x2248},  {"ne", 0x2260},
    {"le", 0x2264},     {"ge", 0x2265},     {"ensp", 0x2002},
    {"emsp", 0x2003},   {"thinsp", 0x2009}, {"zwnj", 0x200C},
    {"zwj", 0x200D},    {"lrm", 0x200E},    {"rlm", 0x200F},
    {"OElig", 0x152},   {"oelig", 0x153},   {"Scaron", 0x160},
    {"scaron", 0x161},  {"Yuml", 0x178},    {"fnof", 0x192},
    {"circ", 0x2C6},    {"tilde", 0x2DC},   {"hearts", 0x2665},
    {"check", 0x2713},  {"NoBreak", 0x2060}, {"ZeroWidthSpace", 0x200B},
};

uint32_t named_entity(std::string_view name) {
  for (const auto& e : kOtherEntities)
    if (e.name == name) return e.cp;
  for (size_t k = 0; k < 96; ++k)
    if (kLatin1Entities[k] == name) return static_cast<uint32_t>(0xA0 + k);
  for (size_t k = 0; k < 25; ++k)
    if (kGreekEntities[k] == name) return static_cast<uint32_t>(0x3B1 + k);
  return 0;
}

}  // namespace

namespace {

void append_utf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) {
    o.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Windows-1252 0x80–0x9F (unassigned bytes keep their C1 code point).
constexpr uint32_t kCp1252[32] = {
    0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
    0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

bool valid_utf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80          ? 0
               : (c >> 5) == 0x6 ? 1
               : (c >> 4) == 0xE ? 2
               : (c >> 3) == 0x1E ? 3
                                  : 4;
    if (n == 4 || i + n >= s.size() + (n == 0)) return n == 0;
    for (size_t k = 1; k <= n; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += n + 1;
  }
  return true;
}

// Charset value at `at` ("utf-8", "ISO-8859-1"; quotes skipped), lower-case.
std::string charset_token(std::string_view s, size_t at) {
  while (at < s.size() && (s[at] == '"' || s[at] == '\'' || s[at] == ' '))
    ++at;
  size_t e = at;
  while (e < s.size() &&
         (std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '-' ||
          s[e] == '_' || s[e] == '.' || s[e] == ':'))
    ++e;
  return ascii_lower(std::string(s.substr(at, e - at)));
}

}  // namespace

std::string detect_charset(std::string_view content_type,
                           std::string_view body) {
  std::string ct = ascii_lower(std::string(content_type));
  if (size_t p = ct.find("charset="); p != std::string::npos)
    return charset_token(ct, p + 8);
  // <meta charset=…>, <meta http-equiv … content="…; charset=…">,
  // <?xml … encoding="…"?> all sit near the top.
  std::string head = ascii_lower(std::string(body.substr(0, 4096)));
  if (size_t p = head.find("charset="); p != std::string::npos)
    return charset_token(head, p + 8);
  if (starts_with(head, "<?xml"))
    if (size_t p = head.find("encoding="); p != std::string::npos &&
                                           p < head.find("?>"))
      return charset_token(head, p + 9);
  return {};
}

std::string to_utf8(std::string body, std::string_view charset) {
  // Valid UTF-8 wins over the label: servers often send a stale
  // "charset=ISO-8859-1" default for UTF-8 pages.
  if (valid_utf8(body)) return body;
  const bool western =
      charset.empty() || charset == "utf-8" || charset == "utf8" ||
      charset == "iso-8859-1" || charset == "iso8859-1" ||
      charset == "latin1" || charset == "iso-8859-15" ||
      charset == "windows-1252" || charset == "cp1252" ||
      charset == "us-ascii" || charset == "ascii";
  if (!western) return body;  // other code pages are not converted
  std::string out;
  out.reserve(body.size() + body.size() / 8);
  for (char ch : body) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x80) out.push_back(ch);
    else append_utf8(out, c < 0xA0 ? kCp1252[c - 0x80] : c);
  }
  return out;
}

std::string html_decode_entities(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    size_t semi;
    if (s[i] != '&' ||
        (semi = s.find(';', i + 1)) == std::string_view::npos ||
        semi - i > 16) {
      o.push_back(s[i]);
      continue;
    }
    std::string_view name = s.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (name.size() > 1 && name[0] == '#') {
      std::string num(name.substr(1));
      bool hex = !num.empty() && (num[0] == 'x' || num[0] == 'X');
      cp = static_cast<uint32_t>(
          std::strtoul(num.c_str() + (hex ? 1 : 0), nullptr, hex ? 16 : 10));
      if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
        cp = 0xFFFD;
      // &#150; and friends mean Windows-1252 (HTML5 does the same).
      if (cp >= 0x80 && cp < 0xA0) cp = kCp1252[cp - 0x80];
    }
    if (cp == 0) cp = named_entity(name);
    if (cp == 0) {
      o.push_back(s[i]);
      continue;
    }
    append_utf8(o, cp);
    i = semi;
  }
  return o;
}

}  // namespace xanatoo
