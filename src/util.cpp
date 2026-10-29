// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/util.hpp"
#include "xanatoo/paths.hpp"

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

bool write_file_atomic(const std::string& path, std::string_view content) {
  const fs::path target(path);
  if (!ensure_dir(target.parent_path().string())) return false;
  const fs::path tmp = fs::path(path + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out) {
      std::cerr << "xanatoo: cannot write " << tmp.string() << "\n";
      std::error_code ec;
      fs::remove(tmp, ec);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(tmp, target, ec);
  if (ec) {
    std::cerr << "xanatoo: cannot replace " << path << ": " << ec.message()
              << "\n";
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

std::string bookmarks_path() {
  std::string dir = state_dir();
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
  if (no_save()) return true;
  std::string path = bookmarks_path();
  if (path.empty()) return false;
  std::string text = "# xanatoo bookmarks — one URL or path per line\n";
  for (const auto& e : entries) {
    if (!e.empty()) text += e + '\n';
  }
  return write_file_atomic(path, text);
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

std::string sha256_hex(std::string_view data) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  // Message + 0x80 + zero padding + 64-bit big-endian bit length.
  std::string m(data);
  const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  m.push_back(static_cast<char>(0x80));
  while (m.size() % 64 != 56) m.push_back('\0');
  for (int i = 7; i >= 0; --i) m.push_back(static_cast<char>(bits >> (i * 8)));
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      const auto* b = reinterpret_cast<const unsigned char*>(m.data() + off + i * 4);
      w[i] = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
             (uint32_t(b[2]) << 8) | uint32_t(b[3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) +
                          ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) +
                          ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (uint32_t v : h)
    for (int i = 28; i >= 0; i -= 4) out.push_back(hex[(v >> i) & 0xf]);
  return out;
}

}  // namespace xanatoo
