// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Tiny S-expression reader shared by the experimental .sexp document format
// and style sheets. Atoms, "strings" (with \n \t escapes), lists, ; comments.

#include <string>
#include <string_view>
#include <vector>

namespace xanatoo {

struct SExpr {
  enum class Kind { List, Atom, String } kind = Kind::Atom;
  std::string text;  // atom or string value
  std::vector<SExpr> kids;
};

class SExprParser {
 public:
  explicit SExprParser(std::string_view src) : s_(src) {}

  SExpr parse_top() {
    skip();
    return parse_one();
  }

  // Every top-level form in the source.
  std::vector<SExpr> parse_all() {
    std::vector<SExpr> out;
    for (;;) {
      skip();
      if (i_ >= s_.size()) break;
      if (s_[i_] == ')') {  // stray close paren
        ++i_;
        continue;
      }
      out.push_back(parse_one());
    }
    return out;
  }

 private:
  std::string_view s_;
  size_t i_ = 0;

  void skip() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
        continue;
      }
      if (c == ';') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
        continue;
      }
      break;
    }
  }

  SExpr parse_one() {
    skip();
    if (i_ >= s_.size()) return {};
    if (s_[i_] == '(') {
      ++i_;
      SExpr list;
      list.kind = SExpr::Kind::List;
      for (;;) {
        skip();
        if (i_ >= s_.size()) break;
        if (s_[i_] == ')') {
          ++i_;
          break;
        }
        list.kids.push_back(parse_one());
      }
      return list;
    }
    if (s_[i_] == '"') {
      ++i_;
      SExpr str;
      str.kind = SExpr::Kind::String;
      while (i_ < s_.size() && s_[i_] != '"') {
        if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
          char e = s_[i_ + 1];
          if (e == 'n')
            str.text.push_back('\n');
          else if (e == 't')
            str.text.push_back('\t');
          else
            str.text.push_back(e);
          i_ += 2;
        } else {
          str.text.push_back(s_[i_]);
          ++i_;
        }
      }
      if (i_ < s_.size() && s_[i_] == '"') ++i_;
      return str;
    }
    SExpr atom;
    atom.kind = SExpr::Kind::Atom;
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
          c == ')' || c == ';')
        break;
      atom.text.push_back(c);
      ++i_;
    }
    return atom;
  }
};

inline bool is_atom(const SExpr& e, const char* name) {
  return e.kind == SExpr::Kind::Atom && e.text == name;
}

// (head …) — list whose first element is the atom `name`.
inline bool is_form(const SExpr& e, const char* name) {
  return e.kind == SExpr::Kind::List && !e.kids.empty() &&
         is_atom(e.kids[0], name);
}

inline std::string sexpr_text(const SExpr& e) {
  if (e.kind == SExpr::Kind::String || e.kind == SExpr::Kind::Atom)
    return e.text;
  return {};
}

inline bool sexpr_bool(const SExpr& e, bool def = false) {
  if (e.kind != SExpr::Kind::Atom) return def;
  if (e.text == "#t" || e.text == "true") return true;
  if (e.text == "#f" || e.text == "false") return false;
  return def;
}

inline float sexpr_float(const SExpr& e, float def = 0.f) {
  if (e.kind != SExpr::Kind::Atom && e.kind != SExpr::Kind::String) return def;
  try {
    return std::stof(e.text);
  } catch (...) {
    return def;
  }
}

inline int sexpr_int(const SExpr& e, int def = 0) {
  if (e.kind != SExpr::Kind::Atom && e.kind != SExpr::Kind::String) return def;
  try {
    return std::stoi(e.text);
  } catch (...) {
    return def;
  }
}

}  // namespace xanatoo
