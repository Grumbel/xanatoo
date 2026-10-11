// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/util.hpp"

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

NodeId find_anchor_node(const Document& doc, std::string_view href) {
  if (href.empty() || href[0] != '#') return INVALID_NODE;
  std::string tlow = ascii_lower(std::string(href.substr(1)));
  if (tlow.empty() || doc.root == INVALID_NODE) return INVALID_NODE;

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

}  // namespace xanatoo
