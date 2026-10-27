// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/style.hpp"

#include "xanatoo/sexpr.hpp"
#include "xanatoo/view_settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace xanatoo {

namespace {

constexpr const char* kStyleExt = ".xss";

struct RoleName {
  const char* name;
  BlockRole role;
};
constexpr RoleName kBlockRoles[] = {
    {"paragraph", BlockRole::Paragraph},
    {"header", BlockRole::Header},
    {"section", BlockRole::Section},
    {"list", BlockRole::List},
    {"list-item", BlockRole::ListItem},
    {"block-quote", BlockRole::BlockQuote},
    {"note", BlockRole::Note},
    {"code-block", BlockRole::CodeBlock},
    {"term", BlockRole::Term},
    {"definition", BlockRole::Definition},
    {"figure", BlockRole::Figure},
    {"caption", BlockRole::Caption},
    {"bibliography-entry", BlockRole::BiblioEntry},
    {"footnote", BlockRole::Footnote},
    {"thread", BlockRole::Thread},
    {"message", BlockRole::Message},
    {"message-alt", BlockRole::MessageAlt},
    {"message-author", BlockRole::MessageAuthor},
    {"message-op", BlockRole::MessageOp},
    {"message-meta", BlockRole::MessageMeta},
    {"table", BlockRole::Table},
    {"rule", BlockRole::Rule},
    {"transclusion", BlockRole::Transclusion},
};

struct InlineName {
  const char* name;
  InlineKind kind;
};
constexpr InlineName kInlineRoles[] = {
    {"text", InlineKind::Text},     {"emphasis", InlineKind::Emphasis},
    {"strong", InlineKind::Strong}, {"strike", InlineKind::Strike},
    {"link", InlineKind::Link},     {"code", InlineKind::Code},
    {"cite", InlineKind::Cite},     {"footnote-ref", InlineKind::FootnoteRef},
};

constexpr std::pair<const char*, FontRole> kFontRoles[] = {
    {"body", FontRole::Body},
    {"bold", FontRole::BodyBold},
    {"italic", FontRole::BodyItalic},
    {"bold-italic", FontRole::BodyBoldItalic},
    {"heading", FontRole::Heading},
    {"code", FontRole::Code},
};

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// #rgb, #rrggbb, #rrggbbaa
bool parse_hex_color(const std::string& s, Rgba& out) {
  if (s.size() < 2 || s[0] != '#') return false;
  std::vector<int> d;
  for (size_t i = 1; i < s.size(); ++i) {
    int v = hex_digit(s[i]);
    if (v < 0) return false;
    d.push_back(v);
  }
  auto byte = [&](size_t i) { return static_cast<uint8_t>(d[i] * 16 + d[i + 1]); };
  if (d.size() == 3) {
    out = {static_cast<uint8_t>(d[0] * 17), static_cast<uint8_t>(d[1] * 17),
           static_cast<uint8_t>(d[2] * 17), 255};
    return true;
  }
  if (d.size() == 6 || d.size() == 8) {
    out = {byte(0), byte(2), byte(4), d.size() == 8 ? byte(6) : uint8_t{255}};
    return true;
  }
  return false;
}

// Applies sheet forms with one shared table of (define name value) tokens.
class SheetReader {
 public:
  SheetReader(DocStyle& style, std::string sheet)
      : style_(style), sheet_(std::move(sheet)) {}

  // Pass 1 over all forms of the inherit chain, so a child sheet's define
  // also changes the parent rules that use the token.
  void collect_defines(const std::vector<SExpr>& forms) {
    for (const SExpr& f : forms) {
      if (is_form(f, "define") && f.kids.size() >= 3 &&
          f.kids[1].kind == SExpr::Kind::Atom)
        defines_[f.kids[1].text] = f.kids[2];
    }
  }

  void apply(const SExpr& f) {
    if (f.kind != SExpr::Kind::List || f.kids.empty() ||
        f.kids[0].kind != SExpr::Kind::Atom) {
      warn("expected (form …)");
      return;
    }
    const std::string& head = f.kids[0].text;
    if (head == "define" || head == "inherit") return;
    if (head == "name") {
      if (f.kids.size() > 1) style_.title = sexpr_text(value(f.kids[1]));
    } else if (head == "description") {
      if (f.kids.size() > 1) style_.description = sexpr_text(value(f.kids[1]));
    } else if (head == "page") {
      for_props(f, [&](const std::string& prop, const SExpr& v) {
        if (prop == "background") color(v, style_.paper);
        else if (prop == "color") color(v, style_.ink);
        else if (prop == "selection") color(v, style_.selection);
        else warn("unknown page property " + prop);
      });
    } else if (head == "fonts") {
      fonts(f);
    } else if (head == "role") {
      role(f);
    } else {
      warn("unknown form (" + head + " …)");
    }
  }

 private:
  DocStyle& style_;
  std::string sheet_;
  std::map<std::string, SExpr> defines_;

  void warn(const std::string& msg) {
    style_.warnings.push_back(sheet_.empty() ? msg : sheet_ + ": " + msg);
  }

  // Follow token references (bounded, so a define cycle cannot hang).
  const SExpr& value(const SExpr& e) const {
    const SExpr* cur = &e;
    for (int guard = 0; guard < 16 && cur->kind == SExpr::Kind::Atom; ++guard) {
      auto it = defines_.find(cur->text);
      if (it == defines_.end()) break;
      cur = &it->second;
    }
    return *cur;
  }

  // (head (prop value…) …) → fn(prop, first value)
  template <typename Fn>
  void for_props(const SExpr& f, size_t first, Fn fn) {
    for (size_t i = first; i < f.kids.size(); ++i) {
      const SExpr& p = f.kids[i];
      if (p.kind != SExpr::Kind::List || p.kids.size() < 2 ||
          p.kids[0].kind != SExpr::Kind::Atom) {
        warn("expected (property value)");
        continue;
      }
      fn(p.kids[0].text, p.kids[1]);
    }
  }
  template <typename Fn>
  void for_props(const SExpr& f, Fn fn) {
    for_props(f, 1, fn);
  }

  bool color(const SExpr& raw, Rgba& out) {
    const SExpr& v = value(raw);
    if (v.kind != SExpr::Kind::List) {
      const std::string s = v.text;
      if (parse_hex_color(s, out)) return true;
      if (s == "none" || s == "transparent") {
        out = {0, 0, 0, 0};
        return true;
      }
      if (s == "black") {
        out = {0, 0, 0, 255};
        return true;
      }
      if (s == "white") {
        out = {255, 255, 255, 255};
        return true;
      }
    } else if ((is_form(v, "rgb") && v.kids.size() == 4) ||
               (is_form(v, "rgba") && v.kids.size() == 5)) {
      auto ch = [&](size_t i) {
        return static_cast<uint8_t>(
            std::clamp(sexpr_int(value(v.kids[i]), 0), 0, 255));
      };
      out = {ch(1), ch(2), ch(3), v.kids.size() == 5 ? ch(4) : uint8_t{255}};
      return true;
    }
    warn("bad colour " + (v.kind == SExpr::Kind::List ? "(…)" : v.text));
    return false;
  }

  bool length(const SExpr& raw, Length& out) {
    const SExpr& v = value(raw);
    std::string s = sexpr_text(v);
    Length l;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "em") == 0) {
      l.em = true;
      s.resize(s.size() - 2);
    } else if (s.size() > 2 && s.compare(s.size() - 2, 2, "px") == 0) {
      s.resize(s.size() - 2);
    }
    try {
      size_t used = 0;
      l.value = std::stof(s, &used);
      if (used != s.size()) throw std::invalid_argument(s);
    } catch (...) {
      warn("bad length " + sexpr_text(v));
      return false;
    }
    out = l;
    return true;
  }

  void fonts(const SExpr& f) {
    FontFamilies& ff = style_.fonts;
    for (size_t i = 1; i < f.kids.size(); ++i) {
      const SExpr& p = f.kids[i];
      if (p.kind != SExpr::Kind::List || p.kids.size() < 2 ||
          p.kids[0].kind != SExpr::Kind::Atom) {
        warn("expected (face \"family\" …)");
        continue;
      }
      const std::string& prop = p.kids[0].text;
      auto families = [&]() {
        std::vector<std::string> out;
        for (size_t k = 1; k < p.kids.size(); ++k)
          out.push_back(sexpr_text(value(p.kids[k])));
        return out;
      };
      if (prop == "body") ff.body = families();
      else if (prop == "heading") ff.heading = families();
      else if (prop == "code") ff.code = families();
      else if (prop == "heading-weight") {
        const std::string w = sexpr_text(value(p.kids[1]));
        if (w == "bold") ff.heading_bold = true;
        else if (w == "regular" || w == "normal") ff.heading_bold = false;
        else warn("heading-weight is bold or regular");
      } else if (prop == "heading-scale") {
        style_.heading_scale =
            std::clamp(sexpr_float(value(p.kids[1]), 0.f), 0.f, 4.f);
      } else {
        warn("unknown fonts entry " + prop);
      }
    }
  }

  void role(const SExpr& f) {
    if (f.kids.size() < 2 || f.kids[1].kind != SExpr::Kind::Atom) {
      warn("expected (role name (property value) …)");
      return;
    }
    const std::string& name = f.kids[1].text;
    for (const auto& r : kBlockRoles) {
      if (name != r.name) continue;
      block_props(f, style_.block[static_cast<size_t>(r.role)]);
      return;
    }
    for (const auto& r : kInlineRoles) {
      if (name != r.name) continue;
      inline_props(f, style_.inl[static_cast<size_t>(r.kind)]);
      return;
    }
    warn("unknown role " + name);
  }

  void block_props(const SExpr& f, BlockStyle& b) {
    for_props(f, 2, [&](const std::string& prop, const SExpr& v) {
      if (prop == "space-before") length(v, b.space_before);
      else if (prop == "space-after") length(v, b.space_after);
      else if (prop == "indent") length(v, b.indent);
      else if (prop == "color") b.has_color = color(v, b.color) || b.has_color;
      else if (prop == "background") color(v, b.background);
      else if (prop == "border") color(v, b.border);
      else if (prop == "font") {
        const std::string s = sexpr_text(value(v));
        auto it = std::find_if(std::begin(kFontRoles), std::end(kFontRoles),
                               [&](const auto& fr) { return s == fr.first; });
        if (it != std::end(kFontRoles)) b.font = it->second;
        else warn("unknown font " + s);
      } else {
        warn("unknown block property " + prop);
      }
    });
  }

  void inline_props(const SExpr& f, InlineStyle& s) {
    for_props(f, 2, [&](const std::string& prop, const SExpr& v) {
      if (prop == "color") s.has_color = color(v, s.color) || s.has_color;
      else if (prop == "background") color(v, s.background);
      else if (prop == "underline") s.underline = sexpr_bool(value(v), s.underline);
      else warn("unknown inline property " + prop);
    });
  }
};

// Top-level forms of a sheet; a single (stylesheet …) wrapper is optional.
std::vector<SExpr> sheet_forms(std::string_view source) {
  std::vector<SExpr> top = SExprParser(source).parse_all();
  if (top.size() == 1 && is_form(top[0], "stylesheet"))
    return {top[0].kids.begin() + 1, top[0].kids.end()};
  return top;
}

void apply_forms(DocStyle& style, const std::vector<SExpr>& forms,
                 const std::string& sheet_name) {
  SheetReader reader(style, sheet_name);
  reader.collect_defines(forms);
  for (const SExpr& f : forms) reader.apply(f);
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

std::string find_style_path(const std::string& name) {
  std::string found;
  for (const std::string& dir : style_dirs()) {
    fs::path p = fs::path(dir) / (name + kStyleExt);
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) found = p.string();  // later dirs win
  }
  return found;
}

// Forms of `name` with its (inherit …) parents spliced in front.
bool gather_forms(const std::string& name, std::vector<SExpr>& out, int depth,
                  std::string* error) {
  if (depth > 8) {
    if (error) *error = "inherit chain too deep at " + name;
    return false;
  }
  const std::string path = find_style_path(name);
  if (path.empty()) {
    if (name == "default") return true;  // built in
    if (error) *error = "style sheet not found: " + name;
    return false;
  }
  std::string src;
  if (!read_file(path, src)) {
    if (error) *error = "cannot read " + path;
    return false;
  }
  std::vector<SExpr> forms = sheet_forms(src);
  for (const SExpr& f : forms) {
    if (is_form(f, "inherit") && f.kids.size() > 1) {
      const std::string parent = sexpr_text(f.kids[1]);
      if (parent != name && !gather_forms(parent, out, depth + 1, error))
        return false;
    }
  }
  out.insert(out.end(), forms.begin(), forms.end());
  return true;
}

}  // namespace

DocStyle builtin_style() {
  DocStyle s;
  auto blk = [&](BlockRole r) -> BlockStyle& {
    return s.block[static_cast<size_t>(r)];
  };
  auto px = [](float v) { return Length{v, false}; };

  blk(BlockRole::Paragraph).space_after = px(14);
  {
    BlockStyle& b = blk(BlockRole::Header);
    b.space_before = px(10);
    b.space_after = px(12);
    b.font = FontRole::Heading;
  }
  {
    BlockStyle& b = blk(BlockRole::Section);
    b.space_before = px(6);
    b.space_after = px(8);
    b.indent = px(8);
  }
  blk(BlockRole::List).space_after = px(8);
  blk(BlockRole::ListItem).indent = px(28);
  blk(BlockRole::ListItem).space_after = px(8);
  {
    BlockStyle& b = blk(BlockRole::BlockQuote);
    b.indent = px(14);
    b.space_after = px(12);
    b.border = {140, 155, 180, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::Note);
    b.indent = px(14);
    b.space_after = px(12);
    b.border = {200, 140, 40, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::CodeBlock);
    b.indent = px(8);
    b.space_after = px(10);
    b.font = FontRole::Code;
    b.background = {245, 240, 230, 210};
    b.border = {210, 200, 180, 255};
  }
  blk(BlockRole::Term).space_after = px(2);
  blk(BlockRole::Definition).indent = px(20);
  blk(BlockRole::Definition).space_after = px(10);
  blk(BlockRole::Figure).space_after = px(10);
  blk(BlockRole::Caption).space_after = px(6);
  blk(BlockRole::Caption).font = FontRole::BodyItalic;
  blk(BlockRole::BiblioEntry).indent = px(16);
  blk(BlockRole::BiblioEntry).space_after = px(8);
  {
    BlockStyle& b = blk(BlockRole::Footnote);
    b.indent = px(24);
    b.space_after = px(8);
    b.border = {170, 168, 160, 255};
  }
  blk(BlockRole::Thread).space_after = px(10);
  blk(BlockRole::Thread).font = FontRole::Heading;
  {
    BlockStyle& b = blk(BlockRole::Message);
    b.space_after = px(8);
    b.border = {40, 130, 140, 255};
    b.background = {255, 255, 255, 150};
  }
  blk(BlockRole::MessageAlt).background = {120, 110, 90, 22};
  {
    BlockStyle& b = blk(BlockRole::MessageAuthor);
    b.has_color = true;
    b.color = {30, 90, 180, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::MessageOp);
    b.has_color = true;
    b.color = {190, 80, 20, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::MessageMeta);
    b.has_color = true;
    b.color = {120, 118, 112, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::Table);
    b.space_after = px(10);
    b.border = {160, 165, 175, 255};
    b.background = {230, 235, 245, 220};
  }
  {
    BlockStyle& b = blk(BlockRole::Rule);
    b.space_before = px(10);
    b.space_after = px(10);
    b.border = {170, 168, 160, 255};
  }
  {
    BlockStyle& b = blk(BlockRole::Transclusion);
    b.indent = px(12);
    b.space_after = px(10);
    b.border = {140, 155, 180, 255};
  }

  auto inl = [&](InlineKind k) -> InlineStyle& {
    return s.inl[static_cast<size_t>(k)];
  };
  inl(InlineKind::Link) = {true, {30, 90, 180, 255}, {0, 0, 0, 0}, true};
  inl(InlineKind::Cite) = {true, {90, 50, 140, 255}, {0, 0, 0, 0}, true};
  inl(InlineKind::FootnoteRef) = {true, {30, 90, 180, 255}, {0, 0, 0, 0}, false};
  inl(InlineKind::Code) = {true, {130, 55, 30, 255}, {245, 235, 220, 200}, false};
  inl(InlineKind::Strike) = {true, {90, 90, 95, 255}, {0, 0, 0, 0}, false};
  return s;
}

void apply_style_source(DocStyle& style, std::string_view source,
                        const std::string& sheet_name) {
  apply_forms(style, sheet_forms(source), sheet_name);
}

std::vector<std::string> style_dirs() {
  std::vector<fs::path> dirs;
#ifdef XANATOO_STYLE_DIR
  dirs.emplace_back(XANATOO_STYLE_DIR);
#endif
#ifdef __linux__
  {
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
      dirs.push_back(exe.parent_path() / ".." / "share" / "xanatoo" / "styles");
      dirs.push_back(exe.parent_path() / "styles");
    }
  }
#endif
  if (const char* data = std::getenv("XANATOO_DATA_DIR"); data && data[0])
    dirs.push_back(fs::path(data) / "styles");
  dirs.emplace_back("styles");
  dirs.push_back(fs::path(view_settings_path()).parent_path() / "styles");

  std::vector<std::string> out;
  for (const fs::path& d : dirs) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(d, ec);
    if (ec || !fs::is_directory(canon, ec)) continue;
    const std::string s = canon.string();
    // Keep the last (highest priority) position of a repeated directory.
    out.erase(std::remove(out.begin(), out.end(), s), out.end());
    out.push_back(s);
  }
  return out;
}

std::vector<StyleEntry> list_styles() {
  std::map<std::string, StyleEntry> by_name;
  by_name["default"] = {"default", "Default", ""};
  for (const std::string& dir : style_dirs()) {
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
      if (!de.is_regular_file(ec) || de.path().extension() != kStyleExt)
        continue;
      StyleEntry e;
      e.name = de.path().stem().string();
      e.path = de.path().string();
      e.title = e.name;
      std::string src;
      if (read_file(e.path, src)) {
        for (const SExpr& f : sheet_forms(src)) {
          if (is_form(f, "name") && f.kids.size() > 1)
            e.title = sexpr_text(f.kids[1]);
        }
      }
      by_name[e.name] = std::move(e);
    }
  }
  std::vector<StyleEntry> out;
  out.push_back(by_name["default"]);
  by_name.erase("default");
  for (auto& [_, e] : by_name) out.push_back(std::move(e));
  std::sort(out.begin() + 1, out.end(),
            [](const StyleEntry& a, const StyleEntry& b) {
              return a.title < b.title;
            });
  return out;
}

bool load_style(const std::string& name, DocStyle& out, std::string* error) {
  out = builtin_style();
  std::vector<SExpr> forms;
  if (!gather_forms(name.empty() ? "default" : name, forms, 0, error))
    return false;
  out.name = name.empty() ? "default" : name;
  apply_forms(out, forms, out.name);
  return true;
}

}  // namespace xanatoo
