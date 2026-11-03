// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// TeX math source → MathNode tree (see math.hpp). Never fails: what is not
// understood stays in the tree as upright text and the first problem is
// reported in MathParse::error.

#include "xanatoo/math.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace xanatoo {

namespace {

using C = MathClass;

struct Sym {
  const char* name;
  uint32_t cp;
  C cls;
};

// clang-format off
const Sym kSyms[] = {
  // Greek (TeX's \phi is the closed ϕ, \varphi the open φ; \epsilon the lunate ϵ).
  {"alpha",0x3B1,C::Ord},{"beta",0x3B2,C::Ord},{"gamma",0x3B3,C::Ord},
  {"delta",0x3B4,C::Ord},{"epsilon",0x3F5,C::Ord},{"varepsilon",0x3B5,C::Ord},
  {"zeta",0x3B6,C::Ord},{"eta",0x3B7,C::Ord},{"theta",0x3B8,C::Ord},
  {"vartheta",0x3D1,C::Ord},{"iota",0x3B9,C::Ord},{"kappa",0x3BA,C::Ord},
  {"varkappa",0x3F0,C::Ord},{"lambda",0x3BB,C::Ord},{"mu",0x3BC,C::Ord},
  {"nu",0x3BD,C::Ord},{"xi",0x3BE,C::Ord},{"omicron",0x3BF,C::Ord},
  {"pi",0x3C0,C::Ord},{"varpi",0x3D6,C::Ord},{"rho",0x3C1,C::Ord},
  {"varrho",0x3F1,C::Ord},{"sigma",0x3C3,C::Ord},{"varsigma",0x3C2,C::Ord},
  {"tau",0x3C4,C::Ord},{"upsilon",0x3C5,C::Ord},{"phi",0x3D5,C::Ord},
  {"varphi",0x3C6,C::Ord},{"chi",0x3C7,C::Ord},{"psi",0x3C8,C::Ord},
  {"omega",0x3C9,C::Ord},{"digamma",0x3DD,C::Ord},
  {"Gamma",0x393,C::Ord},{"Delta",0x394,C::Ord},{"Theta",0x398,C::Ord},
  {"Lambda",0x39B,C::Ord},{"Xi",0x39E,C::Ord},{"Pi",0x3A0,C::Ord},
  {"Sigma",0x3A3,C::Ord},{"Upsilon",0x3A5,C::Ord},{"Phi",0x3A6,C::Ord},
  {"Psi",0x3A8,C::Ord},{"Omega",0x3A9,C::Ord},
  // Binary operators.
  {"pm",0xB1,C::Bin},{"mp",0x2213,C::Bin},{"times",0xD7,C::Bin},
  {"div",0xF7,C::Bin},{"cdot",0x22C5,C::Bin},{"ast",0x2217,C::Bin},
  {"star",0x22C6,C::Bin},{"circ",0x2218,C::Bin},{"bullet",0x2219,C::Bin},
  {"oplus",0x2295,C::Bin},{"ominus",0x2296,C::Bin},{"otimes",0x2297,C::Bin},
  {"oslash",0x2298,C::Bin},{"odot",0x2299,C::Bin},{"cup",0x222A,C::Bin},
  {"cap",0x2229,C::Bin},{"uplus",0x228E,C::Bin},{"sqcup",0x2294,C::Bin},
  {"sqcap",0x2293,C::Bin},{"vee",0x2228,C::Bin},{"lor",0x2228,C::Bin},
  {"wedge",0x2227,C::Bin},{"land",0x2227,C::Bin},{"setminus",0x2216,C::Bin},
  {"smallsetminus",0x2216,C::Bin},{"wr",0x2240,C::Bin},{"diamond",0x22C4,C::Bin},
  {"triangleleft",0x25C1,C::Bin},{"triangleright",0x25B7,C::Bin},
  {"dagger",0x2020,C::Bin},{"ddagger",0x2021,C::Bin},{"amalg",0x2A3F,C::Bin},
  {"boxplus",0x229E,C::Bin},{"boxminus",0x229F,C::Bin},{"boxtimes",0x22A0,C::Bin},
  {"boxdot",0x22A1,C::Bin},{"ltimes",0x22C9,C::Bin},{"rtimes",0x22CA,C::Bin},
  {"bigcirc",0x25EF,C::Bin},{"barwedge",0x22BC,C::Bin},{"veebar",0x22BB,C::Bin},
  {"curlywedge",0x22CF,C::Bin},{"curlyvee",0x22CE,C::Bin},
  // Relations.
  {"leq",0x2264,C::Rel},{"le",0x2264,C::Rel},{"geq",0x2265,C::Rel},
  {"ge",0x2265,C::Rel},{"neq",0x2260,C::Rel},{"ne",0x2260,C::Rel},
  {"ll",0x226A,C::Rel},{"gg",0x226B,C::Rel},{"equiv",0x2261,C::Rel},
  {"sim",0x223C,C::Rel},{"simeq",0x2243,C::Rel},{"approx",0x2248,C::Rel},
  {"cong",0x2245,C::Rel},{"propto",0x221D,C::Rel},{"perp",0x22A5,C::Rel},
  {"parallel",0x2225,C::Rel},{"nparallel",0x2226,C::Rel},{"mid",0x2223,C::Rel},
  {"nmid",0x2224,C::Rel},{"asymp",0x224D,C::Rel},{"doteq",0x2250,C::Rel},
  {"prec",0x227A,C::Rel},{"succ",0x227B,C::Rel},{"preceq",0x2AAF,C::Rel},
  {"succeq",0x2AB0,C::Rel},{"in",0x2208,C::Rel},{"notin",0x2209,C::Rel},
  {"ni",0x220B,C::Rel},{"owns",0x220B,C::Rel},{"subset",0x2282,C::Rel},
  {"supset",0x2283,C::Rel},{"subseteq",0x2286,C::Rel},{"supseteq",0x2287,C::Rel},
  {"nsubseteq",0x2288,C::Rel},{"nsupseteq",0x2289,C::Rel},
  {"subsetneq",0x228A,C::Rel},{"supsetneq",0x228B,C::Rel},
  {"sqsubset",0x228F,C::Rel},{"sqsupset",0x2290,C::Rel},
  {"sqsubseteq",0x2291,C::Rel},{"sqsupseteq",0x2292,C::Rel},
  {"vdash",0x22A2,C::Rel},{"dashv",0x22A3,C::Rel},{"models",0x22A8,C::Rel},
  {"vDash",0x22A8,C::Rel},{"bowtie",0x22C8,C::Rel},{"smile",0x2323,C::Rel},
  {"frown",0x2322,C::Rel},{"leqslant",0x2A7D,C::Rel},{"geqslant",0x2A7E,C::Rel},
  {"lesssim",0x2272,C::Rel},{"gtrsim",0x2273,C::Rel},{"lessgtr",0x2276,C::Rel},
  {"triangleq",0x225C,C::Rel},{"coloneqq",0x2254,C::Rel},{"approxeq",0x224A,C::Rel},
  {"nless",0x226E,C::Rel},{"ngtr",0x226F,C::Rel},{"nleq",0x2270,C::Rel},
  {"ngeq",0x2271,C::Rel},{"nsim",0x2241,C::Rel},{"ncong",0x2247,C::Rel},
  {"napprox",0x2249,C::Rel},{"nequiv",0x2262,C::Rel},{"ntriangleleft",0x22EA,C::Rel},
  {"vartriangleleft",0x22B2,C::Rel},{"vartriangleright",0x22B3,C::Rel},
  {"trianglelefteq",0x22B4,C::Rel},{"trianglerighteq",0x22B5,C::Rel},
  // Arrows.
  {"leftarrow",0x2190,C::Rel},{"gets",0x2190,C::Rel},{"rightarrow",0x2192,C::Rel},
  {"to",0x2192,C::Rel},{"leftrightarrow",0x2194,C::Rel},{"uparrow",0x2191,C::Rel},
  {"downarrow",0x2193,C::Rel},{"updownarrow",0x2195,C::Rel},
  {"Leftarrow",0x21D0,C::Rel},{"Rightarrow",0x21D2,C::Rel},
  {"Leftrightarrow",0x21D4,C::Rel},{"Uparrow",0x21D1,C::Rel},
  {"Downarrow",0x21D3,C::Rel},{"Updownarrow",0x21D5,C::Rel},
  {"nearrow",0x2197,C::Rel},{"searrow",0x2198,C::Rel},{"swarrow",0x2199,C::Rel},
  {"nwarrow",0x2196,C::Rel},{"mapsto",0x21A6,C::Rel},{"longmapsto",0x27FC,C::Rel},
  {"hookrightarrow",0x21AA,C::Rel},{"hookleftarrow",0x21A9,C::Rel},
  {"longleftarrow",0x27F5,C::Rel},{"longrightarrow",0x27F6,C::Rel},
  {"longleftrightarrow",0x27F7,C::Rel},{"Longleftarrow",0x27F8,C::Rel},
  {"Longrightarrow",0x27F9,C::Rel},{"Longleftrightarrow",0x27FA,C::Rel},
  {"implies",0x27F9,C::Rel},{"impliedby",0x27F8,C::Rel},{"iff",0x27FA,C::Rel},
  {"leadsto",0x21DD,C::Rel},{"rightsquigarrow",0x21DD,C::Rel},
  {"rightharpoonup",0x21C0,C::Rel},{"rightharpoondown",0x21C1,C::Rel},
  {"leftharpoonup",0x21BC,C::Rel},{"leftharpoondown",0x21BD,C::Rel},
  {"rightleftharpoons",0x21CC,C::Rel},{"leftrightharpoons",0x21CB,C::Rel},
  {"twoheadrightarrow",0x21A0,C::Rel},{"twoheadleftarrow",0x219E,C::Rel},
  {"rightarrowtail",0x21A3,C::Rel},{"leftarrowtail",0x21A2,C::Rel},
  {"nrightarrow",0x219B,C::Rel},{"nleftarrow",0x219A,C::Rel},
  {"circlearrowleft",0x21BA,C::Rel},{"circlearrowright",0x21BB,C::Rel},
  // Ordinary symbols.
  {"infty",0x221E,C::Ord},{"partial",0x2202,C::Ord},{"nabla",0x2207,C::Ord},
  {"forall",0x2200,C::Ord},{"exists",0x2203,C::Ord},{"nexists",0x2204,C::Ord},
  {"emptyset",0x2205,C::Ord},{"varnothing",0x2205,C::Ord},{"neg",0xAC,C::Ord},
  {"lnot",0xAC,C::Ord},{"ell",0x2113,C::Ord},{"hbar",0x210F,C::Ord},
  {"hslash",0x210F,C::Ord},{"Re",0x211C,C::Ord},{"Im",0x2111,C::Ord},
  {"aleph",0x2135,C::Ord},{"beth",0x2136,C::Ord},{"gimel",0x2137,C::Ord},
  {"wp",0x2118,C::Ord},{"imath",0x131,C::Ord},{"jmath",0x237,C::Ord},
  {"prime",0x2032,C::Ord},{"top",0x22A4,C::Ord},{"bot",0x22A5,C::Ord},
  {"angle",0x2220,C::Ord},{"measuredangle",0x2221,C::Ord},{"triangle",0x25B3,C::Ord},
  {"square",0x25A1,C::Ord},{"Box",0x25A1,C::Ord},{"blacksquare",0x25A0,C::Ord},
  {"surd",0x221A,C::Ord},{"flat",0x266D,C::Ord},{"natural",0x266E,C::Ord},
  {"sharp",0x266F,C::Ord},{"clubsuit",0x2663,C::Ord},{"diamondsuit",0x2662,C::Ord},
  {"heartsuit",0x2661,C::Ord},{"spadesuit",0x2660,C::Ord},{"checkmark",0x2713,C::Ord},
  {"S",0xA7,C::Ord},{"P",0xB6,C::Ord},{"copyright",0xA9,C::Ord},
  {"complement",0x2201,C::Ord},{"because",0x2235,C::Rel},{"therefore",0x2234,C::Rel},
  {"mho",0x2127,C::Ord},{"Finv",0x2132,C::Ord},{"eth",0xF0,C::Ord},
  {"backslash",0x5C,C::Ord},{"degree",0xB0,C::Ord},{"Bbbk",0x1D55C,C::Ord},
  {"dots",0x2026,C::Inner},{"ldots",0x2026,C::Inner},{"cdots",0x22EF,C::Inner},
  {"dotsc",0x2026,C::Inner},{"dotsb",0x22EF,C::Inner},{"mathellipsis",0x2026,C::Inner},
  {"vdots",0x22EE,C::Ord},{"ddots",0x22F1,C::Inner},{"iddots",0x22F0,C::Inner},
  // Delimiters.
  {"langle",0x27E8,C::Open},{"rangle",0x27E9,C::Close},{"lfloor",0x230A,C::Open},
  {"rfloor",0x230B,C::Close},{"lceil",0x2308,C::Open},{"rceil",0x2309,C::Close},
  {"lbrace",'{',C::Open},{"rbrace",'}',C::Close},{"lbrack",'[',C::Open},
  {"rbrack",']',C::Close},{"lvert",'|',C::Open},{"rvert",'|',C::Close},
  {"lVert",0x2016,C::Open},{"rVert",0x2016,C::Close},{"vert",'|',C::Ord},
  {"Vert",0x2016,C::Ord},{"lgroup",0x27EE,C::Open},{"rgroup",0x27EF,C::Close},
  {"llbracket",0x27E6,C::Open},{"rrbracket",0x27E7,C::Close},
  {"ulcorner",0x231C,C::Open},{"urcorner",0x231D,C::Close},
  {"llcorner",0x231E,C::Open},{"lrcorner",0x231F,C::Close},
  // Large operators (limits above / below in display style).
  {"sum",0x2211,C::Op},{"prod",0x220F,C::Op},{"coprod",0x2210,C::Op},
  {"bigcup",0x22C3,C::Op},{"bigcap",0x22C2,C::Op},{"bigvee",0x22C1,C::Op},
  {"bigwedge",0x22C0,C::Op},{"bigoplus",0x2A01,C::Op},{"bigotimes",0x2A02,C::Op},
  {"bigodot",0x2A00,C::Op},{"biguplus",0x2A04,C::Op},{"bigsqcup",0x2A06,C::Op},
  // …and the integrals (limits at the side).
  {"int",0x222B,C::Op},{"iint",0x222C,C::Op},{"iiint",0x222D,C::Op},
  {"iiiint",0x2A0C,C::Op},{"oint",0x222E,C::Op},{"oiint",0x222F,C::Op},
  {"oiiint",0x2230,C::Op},{"intop",0x222B,C::Op},{"smallint",0x222B,C::Op},
};

// Operators written as upright words; the flag says "limits in display".
struct NamedOp {
  const char* name;
  const char* text;
  bool limits;
};
const NamedOp kNamedOps[] = {
  {"sin","sin",false},{"cos","cos",false},{"tan","tan",false},
  {"cot","cot",false},{"sec","sec",false},{"csc","csc",false},
  {"arcsin","arcsin",false},{"arccos","arccos",false},{"arctan","arctan",false},
  {"sinh","sinh",false},{"cosh","cosh",false},{"tanh","tanh",false},
  {"coth","coth",false},{"log","log",false},{"ln","ln",false},{"lg","lg",false},
  {"exp","exp",false},{"arg","arg",false},{"deg","deg",false},{"dim","dim",false},
  {"hom","hom",false},{"ker","ker",false},{"Pr","Pr",true},{"det","det",true},
  {"gcd","gcd",true},{"lim","lim",true},{"limsup","lim sup",true},
  {"liminf","lim inf",true},{"max","max",true},{"min","min",true},
  {"sup","sup",true},{"inf","inf",true},{"injlim","inj lim",true},
  {"projlim","proj lim",true},{"varlimsup","lim",true},{"varliminf","lim",true},
};

// Text accents and wide accents: command → accent character.
struct AccentDef {
  const char* name;
  uint32_t cp;
  bool stretchy;
  bool above;
};
const AccentDef kAccents[] = {
  {"hat",0x302,false,true},{"check",0x30C,false,true},{"breve",0x306,false,true},
  {"acute",0x301,false,true},{"grave",0x300,false,true},{"tilde",0x303,false,true},
  {"bar",0x304,false,true},{"vec",0x20D7,false,true},{"dot",0x307,false,true},
  {"ddot",0x308,false,true},{"dddot",0x20DB,false,true},{"mathring",0x30A,false,true},
  {"widehat",0x302,true,true},{"widetilde",0x303,true,true},
  {"overbrace",0x23DE,true,true},{"underbrace",0x23DF,true,false},
  {"overrightarrow",0x20D7,true,true},{"overleftarrow",0x20D6,true,true},
  {"overleftrightarrow",0x20E1,true,true},{"underrightarrow",0x2192,true,false},
  {"underleftarrow",0x2190,true,false},
};
// clang-format on

const std::unordered_map<std::string, const Sym*>& sym_table() {
  static const auto* t = [] {
    auto* m = new std::unordered_map<std::string, const Sym*>;
    for (const Sym& s : kSyms) m->emplace(s.name, &s);
    return m;
  }();
  return *t;
}

const NamedOp* find_named_op(const std::string& name) {
  for (const NamedOp& o : kNamedOps)
    if (name == o.name) return &o;
  return nullptr;
}

const AccentDef* find_accent(const std::string& name) {
  for (const AccentDef& a : kAccents)
    if (name == a.name) return &a;
  return nullptr;
}

// Integrals take their scripts at the side, the rest above / below.
bool is_integral(uint32_t cp) {
  return (cp >= 0x222B && cp <= 0x2233) || cp == 0x2A0C;
}

bool is_ascii_letter(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

enum End : unsigned {
  kEndBrace = 1,  // }
  kEndRight = 2,  // \right
  kEndEnv = 4,    // \end
  kEndCell = 8,   // & and \\ (inside an environment)
  kEndBracket = 16,  // ] (optional argument)
};

enum class T : uint8_t { End, Open, Close, Sup, Sub, Amp, Cmd, Char };

struct Tok {
  T t = T::End;
  std::string name;  // Cmd
  uint32_t cp = 0;   // Char
};

uint32_t next_cp(std::string_view s, size_t& i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = 1;
  uint32_t cp = c;
  if (c >= 0xF0 && i + 3 < s.size() + 0) {
    n = 4;
    cp = c & 0x07;
  } else if (c >= 0xE0) {
    n = 3;
    cp = c & 0x0F;
  } else if (c >= 0xC0) {
    n = 2;
    cp = c & 0x1F;
  }
  for (size_t k = 1; k < n && i + k < s.size(); ++k)
    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
  i += n;
  return cp;
}

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

MathNode make_atom(uint32_t cp, C cls = C::Ord) {
  MathNode n;
  n.kind = MathKind::Atom;
  n.cls = cls;
  n.cp = cp;
  return n;
}

MathNode make_text(std::string text) {
  MathNode n;
  n.kind = MathKind::Text;
  n.text = std::move(text);
  return n;
}

MathNode make_space(float em) {
  MathNode n;
  n.kind = MathKind::Space;
  n.amount = em;
  return n;
}

MathNode make_list(std::vector<MathNode> kids = {}) {
  MathNode n;
  n.kind = MathKind::List;
  n.kids = std::move(kids);
  return n;
}

// The atom class of an ASCII character.
C ascii_class(uint32_t c) {
  switch (c) {
    case '+': case '-': case '*': return C::Bin;
    case '=': case '<': case '>': case ':': return C::Rel;
    case ',': case ';': return C::Punct;
    case '(': case '[': return C::Open;
    case ')': case ']': case '!': case '?': return C::Close;
    default: return C::Ord;
  }
}

// \big, \Bigl, … → size level and side.
bool big_command(const std::string& name, int& size, C& cls) {
  static const char* kBase[] = {"big", "Big", "bigg", "Bigg"};
  for (int i = 0; i < 4; ++i) {
    const std::string b = kBase[i];
    if (name == b || name == b + "l" || name == b + "r" || name == b + "m") {
      size = i + 1;
      const char last = name.back();
      cls = name == b ? C::Ord
            : last == 'l' ? C::Open
            : last == 'r' ? C::Close
                          : C::Rel;
      return true;
    }
  }
  return false;
}

class Parser {
public:
  explicit Parser(std::string_view s) : s_(s) {}

  MathParse run() {
    MathParse out;
    MathNode list = parse_seq(0);
    // A stray `}` or `\right` ends the sequence early: keep going.
    while (peek().t != T::End) {
      fail("unexpected " + describe(peek()));
      next();
      MathNode more = parse_seq(0);
      for (MathNode& k : more.kids) list.kids.push_back(std::move(k));
    }
    out.root = std::move(list);
    out.error = error_;
    return out;
  }

private:
  std::string_view s_;
  size_t i_ = 0;
  std::string error_;

  void fail(const std::string& msg) {
    if (error_.empty()) error_ = msg;
  }

  static std::string describe(const Tok& t) {
    switch (t.t) {
      case T::Close: return "}";
      case T::Amp: return "&";
      case T::Cmd: return "\\" + t.name;
      default: return "input";
    }
  }

  void skip_space() {
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
      } else if (c == '%') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
      } else {
        break;
      }
    }
  }

  Tok peek() {
    const size_t save = i_;
    Tok t = next();
    i_ = save;
    return t;
  }

  Tok next() {
    skip_space();
    Tok t;
    if (i_ >= s_.size()) return t;
    const char c = s_[i_];
    switch (c) {
      case '{': ++i_; t.t = T::Open; return t;
      case '}': ++i_; t.t = T::Close; return t;
      case '^': ++i_; t.t = T::Sup; return t;
      case '_': ++i_; t.t = T::Sub; return t;
      case '&': ++i_; t.t = T::Amp; return t;
      case '~': ++i_; t.t = T::Cmd; t.name = " "; return t;
      default: break;
    }
    if (c == '\\') {
      ++i_;
      t.t = T::Cmd;
      if (i_ >= s_.size()) return t;
      if (is_ascii_letter(static_cast<unsigned char>(s_[i_]))) {
        const size_t b = i_;
        while (i_ < s_.size() &&
               is_ascii_letter(static_cast<unsigned char>(s_[i_])))
          ++i_;
        t.name = std::string(s_.substr(b, i_ - b));
      } else {
        t.name = std::string(1, s_[i_++]);
      }
      return t;
    }
    t.t = T::Char;
    t.cp = next_cp(s_, i_);
    return t;
  }

  bool at_end_of(unsigned ends) {
    const Tok t = peek();
    switch (t.t) {
      case T::End: return true;
      case T::Close: return true;  // always ends a sequence
      case T::Amp: return (ends & kEndCell) != 0;
      case T::Char: return t.cp == ']' && (ends & kEndBracket) != 0;
      case T::Cmd:
        if (t.name == "right") return true;
        if (t.name == "end") return true;
        if (t.name == "\\" || t.name == "cr") return (ends & kEndCell) != 0;
        return false;
      default: return false;
    }
  }

  // Raw text up to the matching `}` (the `{` is already consumed).
  std::string raw_group() {
    std::string out;
    int depth = 1;
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == '{') {
        ++depth;
      } else if (c == '}') {
        if (--depth == 0) {
          ++i_;
          return out;
        }
      } else if (c == '\\' && i_ + 1 < s_.size()) {
        const char d = s_[i_ + 1];
        if (d == '&' || d == '%' || d == '$' || d == '#' || d == '_' ||
            d == '{' || d == '}') {
          out.push_back(d);
          i_ += 2;
          continue;
        }
        if (d == ' ' || d == ',') {
          out.push_back(' ');
          i_ += 2;
          continue;
        }
      }
      out.push_back(c);
      ++i_;
    }
    fail("missing }");
    return out;
  }

  // `{…}` or one token.
  MathNode parse_arg() {
    skip_space();
    const Tok t = peek();
    if (t.t == T::Open) {
      next();
      MathNode body = parse_seq(kEndBrace);
      if (peek().t == T::Close)
        next();
      else
        fail("missing }");
      return body;
    }
    if (t.t == T::End || t.t == T::Close) {
      fail("missing argument");
      return make_list();
    }
    if (t.t == T::Cmd && (t.name == "right" || t.name == "end")) {
      fail("missing argument");
      return make_list();
    }
    next();
    return parse_token(t);
  }

  // An argument that is plain text: `{abc}`.
  std::string parse_text_arg() {
    skip_space();
    if (peek().t == T::Open) {
      next();
      return raw_group();
    }
    const Tok t = next();
    std::string out;
    if (t.t == T::Char) append_utf8(out, t.cp);
    return out;
  }

  // An optional `[…]` argument; false when there is none.
  bool parse_optional(MathNode& out) {
    skip_space();
    const Tok t = peek();
    if (t.t != T::Char || t.cp != '[') return false;
    next();
    out = parse_seq(kEndBracket);
    const Tok c = peek();
    if (c.t == T::Char && c.cp == ']')
      next();
    else
      fail("missing ]");
    return true;
  }

  // The character a delimiter token stands for (0 for `.`).
  uint32_t parse_delim() {
    skip_space();
    const Tok t = next();
    if (t.t == T::Char) return t.cp == '.' ? 0 : t.cp;
    if (t.t == T::Open) return '{';
    if (t.t == T::Close) return '}';
    if (t.t == T::Cmd) {
      if (t.name == "{") return '{';
      if (t.name == "}") return '}';
      if (t.name == "|") return 0x2016;
      if (t.name == "/") return '/';
      auto it = sym_table().find(t.name);
      if (it != sym_table().end()) return it->second->cp;
    }
    fail("bad delimiter");
    return 0;
  }

  void restyle(MathNode& n, MathVariant v) {
    if (n.kind == MathKind::Atom && n.text.empty()) n.variant = v;
    for (MathNode& k : n.kids) restyle(k, v);
  }

  // Wraps `body` as a node of class `cls` (\mathrel{…} and friends).
  MathNode with_class(MathNode body, C cls) {
    if (body.kind != MathKind::List) body = make_list({std::move(body)});
    body.cls = cls;
    return body;
  }

  MathNode operator_name(const std::string& text, bool limits) {
    MathNode n;
    n.kind = MathKind::Atom;
    n.cls = C::Op;
    n.text = text;
    n.dlimits = limits;
    return n;
  }

  static std::string flat_text(const MathNode& n) {
    std::string out;
    if (n.kind == MathKind::Atom) {
      if (!n.text.empty())
        out += n.text;
      else
        append_utf8(out, n.cp);
    } else if (n.kind == MathKind::Text) {
      out += n.text;
    } else if (n.kind == MathKind::Space) {
      out += ' ';
    }
    for (const MathNode& k : n.kids) out += flat_text(k);
    return out;
  }

  MathNode parse_environment() {
    const std::string name = parse_text_arg();
    std::string align;
    MathNode body;
    uint32_t left = 0, right = 0;
    bool delimited = false;
    std::string cols = "c";
    bool display = false;
    bool script = false;

    if (name == "matrix") {
    } else if (name == "pmatrix") {
      left = '('; right = ')'; delimited = true;
    } else if (name == "bmatrix") {
      left = '['; right = ']'; delimited = true;
    } else if (name == "Bmatrix") {
      left = '{'; right = '}'; delimited = true;
    } else if (name == "vmatrix") {
      left = '|'; right = '|'; delimited = true;
    } else if (name == "Vmatrix") {
      left = 0x2016; right = 0x2016; delimited = true;
    } else if (name == "smallmatrix") {
      script = true;
    } else if (name == "cases") {
      left = '{'; delimited = true; cols = "ll";
    } else if (name == "array" || name == "subarray") {
      const std::string spec = parse_text_arg();
      cols.clear();
      for (char c : spec)
        if (c == 'l' || c == 'c' || c == 'r') cols.push_back(c);
      if (cols.empty()) cols = "c";
    } else if (name == "aligned" || name == "align" || name == "align*" ||
               name == "split" || name == "alignat" || name == "alignat*" ||
               name == "eqnarray" || name == "eqnarray*" || name == "flalign" ||
               name == "flalign*") {
      cols = "rl"; display = true;
      if (name.rfind("alignat", 0) == 0) parse_text_arg();  // column count
    } else if (name == "gathered" || name == "gather" || name == "gather*" ||
               name == "multline" || name == "multline*") {
      display = true;
    } else if (name == "equation" || name == "equation*" || name == "math" ||
               name == "displaymath") {
      MathNode inner = parse_seq(kEndEnv);
      finish_env(name);
      return inner;
    } else {
      fail("unknown environment " + name);
    }

    std::vector<std::vector<MathNode>> rows;
    rows.emplace_back();
    size_t maxc = 0;
    for (;;) {
      MathNode cell = parse_seq(kEndCell | kEndEnv);
      rows.back().push_back(std::move(cell));
      const Tok t = peek();
      if (t.t == T::Amp) {
        next();
      } else if (t.t == T::Cmd && (t.name == "\\" || t.name == "cr")) {
        next();
        MathNode skip;
        parse_optional(skip);  // \\[2pt]
        maxc = std::max(maxc, rows.back().size());
        rows.emplace_back();
      } else {
        break;
      }
    }
    maxc = std::max(maxc, rows.back().size());
    // A trailing `\\` leaves an empty last row.
    if (rows.size() > 1 && rows.back().size() == 1 &&
        rows.back()[0].kids.empty())
      rows.pop_back();
    finish_env(name);

    // Aligned environments pair columns (right, left) as many as needed.
    if (cols == "rl") {
      std::string c;
      for (size_t k = 0; k < maxc; ++k) c.push_back(k % 2 == 0 ? 'r' : 'l');
      cols = c;
    } else if (cols.size() < maxc) {
      const char fill = cols.empty() ? 'c' : cols.back();
      while (cols.size() < maxc) cols.push_back(fill);
    }
    MathNode m;
    m.kind = MathKind::Matrix;
    m.rows = static_cast<int>(rows.size());
    m.cols = static_cast<int>(std::max<size_t>(maxc, 1));
    m.align = cols;
    m.has_a = display;   // cells in display style
    m.has_b = script;    // cells in script style
    for (auto& row : rows) {
      row.resize(static_cast<size_t>(m.cols));
      for (MathNode& c : row) {
        if (display) {
          // TeX sets aligned cells as `{}#`: the empty atom makes a leading
          // relation (`&= b`) take its space.
          MathNode empty = make_list();
          empty.has_a = true;
          c.kids.insert(c.kids.begin(), std::move(empty));
        }
        m.kids.push_back(std::move(c));
      }
    }
    if (!delimited) return m;
    MathNode f;
    f.kind = MathKind::Fenced;
    f.cls = C::Inner;
    f.cp = left;
    f.cp2 = right;
    f.kids.push_back(std::move(m));
    return f;
  }

  void finish_env(const std::string& name) {
    const Tok t = peek();
    if (t.t == T::Cmd && t.name == "end") {
      next();
      const std::string got = parse_text_arg();
      if (got != name) fail("\\end{" + got + "} does not match \\begin{" + name + "}");
    } else {
      fail("missing \\end{" + name + "}");
    }
  }

  // Style switches take the rest of the group.
  MathNode style_rest(int style, unsigned ends) {
    MathNode n;
    n.kind = MathKind::Style;
    n.style = static_cast<int8_t>(style);
    n.kids.push_back(parse_seq(ends));
    return n;
  }

  // Consumes the token `t` (already read) and returns what it stands for.
  MathNode parse_token(const Tok& t, unsigned ends = 0) {
    switch (t.t) {
      case T::Open: {
        MathNode body = parse_seq(kEndBrace);
        if (peek().t == T::Close)
          next();
        else
          fail("missing }");
        return body;
      }
      case T::Char: return parse_char(t.cp);
      case T::Cmd: return parse_command(t.name, ends);
      case T::Sup:
      case T::Sub:
        fail("misplaced ^ or _");
        return make_list();
      case T::Amp:
        fail("misplaced &");
        return make_list();
      default: return make_list();
    }
  }

  MathNode parse_char(uint32_t c) {
    if (c == '-') return make_atom(0x2212, C::Bin);
    if (c == '*') return make_atom(0x2217, C::Bin);
    if (c == '\'') return make_atom(0x2032, C::Ord);
    if (c == '`') return make_atom(0x2018, C::Ord);
    if (c < 0x80) return make_atom(c, ascii_class(c));
    // Unicode input: a few classes matter for spacing.
    C cls = C::Ord;
    if ((c >= 0x2190 && c <= 0x21FF) || (c >= 0x2200 && c <= 0x22FF)) {
      switch (c) {
        case 0x2200: case 0x2203: case 0x2205: case 0x2207: case 0x2202:
        case 0x221E: case 0x2220: case 0x221A:
          break;
        case 0x2211: case 0x220F: case 0x222B:
          cls = C::Op;
          break;
        case 0x00B1: case 0x2213: case 0x2227: case 0x2228: case 0x2229:
        case 0x222A: case 0x2216: case 0x2218: case 0x2219: case 0x22C5:
          cls = C::Bin;
          break;
        default: cls = C::Rel;
      }
    } else if (c == 0xB1 || c == 0xD7 || c == 0xF7) {
      cls = C::Bin;
    } else if (c == 0x2264 || c == 0x2265 || c == 0x2260 || c == 0x2248) {
      cls = C::Rel;
    }
    MathNode a = make_atom(c, cls);
    a.dlimits = cls == C::Op && !is_integral(c);
    return a;
  }

  MathNode frac(MathNode a, MathNode b, bool bar, int style) {
    MathNode f;
    f.kind = MathKind::Frac;
    f.cls = C::Inner;
    f.bar = bar;
    f.style = static_cast<int8_t>(style);
    f.kids.push_back(std::move(a));
    f.kids.push_back(std::move(b));
    return f;
  }

  MathNode fenced(MathNode body, uint32_t l, uint32_t r) {
    MathNode f;
    f.kind = MathKind::Fenced;
    f.cls = C::Inner;
    f.cp = l;
    f.cp2 = r;
    f.kids.push_back(std::move(body));
    return f;
  }

  MathNode accent(MathNode base, const AccentDef& a) {
    MathNode n;
    n.kind = MathKind::Accent;
    n.cp = a.cp;
    n.stretchy = a.stretchy;
    n.above = a.above;
    n.kids.push_back(std::move(base));
    return n;
  }

  MathNode parse_command(const std::string& name, unsigned ends) {
    // Single-character commands.
    if (name.size() == 1 && !is_ascii_letter(static_cast<unsigned char>(name[0]))) {
      switch (name[0]) {
        case ',': return make_space(3.f / 18.f);
        case ':': case '>': return make_space(4.f / 18.f);
        case ';': return make_space(5.f / 18.f);
        case '!': return make_space(-3.f / 18.f);
        case ' ': return make_space(1.f / 3.f);
        case '{': return make_atom('{', C::Open);
        case '}': return make_atom('}', C::Close);
        case '|': return make_atom(0x2016, C::Ord);
        case '&': case '%': case '$': case '#': case '_':
          return make_atom(static_cast<unsigned char>(name[0]), C::Ord);
        case '/': return make_space(0.f);
        case '-': return make_space(0.f);
        case '\\':
          fail("misplaced \\\\");
          return make_list();
        default: break;
      }
      fail("unknown command \\" + name);
      return make_text("\\" + name);
    }

    auto sym = sym_table().find(name);
    if (sym != sym_table().end()) {
      MathNode a = make_atom(sym->second->cp, sym->second->cls);
      a.dlimits = a.cls == C::Op && !is_integral(a.cp);
      return a;
    }
    if (const NamedOp* op = find_named_op(name))
      return operator_name(op->text, op->limits);
    if (const AccentDef* ac = find_accent(name)) return accent(parse_arg(), *ac);

    int bsize = 0;
    C bcls = C::Ord;
    if (big_command(name, bsize, bcls)) {
      const uint32_t d = parse_delim();
      MathNode a = make_atom(d, bcls);
      a.size = static_cast<int8_t>(bsize);
      return a;
    }

    // Fonts.
    struct FontCmd {
      const char* name;
      MathVariant v;
    };
    static const FontCmd kFonts[] = {
        {"mathrm", MathVariant::Roman},     {"rm", MathVariant::Roman},
        {"mathbf", MathVariant::Bold},      {"bf", MathVariant::Bold},
        {"mathit", MathVariant::Italic},    {"mathnormal", MathVariant::Italic},
        {"boldsymbol", MathVariant::BoldItalic}, {"bm", MathVariant::BoldItalic},
        {"mathbb", MathVariant::DoubleStruck}, {"Bbb", MathVariant::DoubleStruck},
        {"mathcal", MathVariant::Script},   {"mathscr", MathVariant::Script},
        {"cal", MathVariant::Script},       {"mathfrak", MathVariant::Fraktur},
        {"frak", MathVariant::Fraktur},     {"mathsf", MathVariant::SansSerif},
        {"mathtt", MathVariant::Monospace}, {"mathbfit", MathVariant::BoldItalic},
        {"pmb", MathVariant::Bold},
    };
    for (const FontCmd& f : kFonts) {
      if (name == f.name) {
        MathNode arg = parse_arg();
        restyle(arg, f.v);
        return arg;
      }
    }

    if (name == "text" || name == "textrm" || name == "mbox" ||
        name == "textnormal" || name == "textsf" || name == "texttt" ||
        name == "hbox")
      return make_text(parse_text_arg());
    if (name == "textbf" || name == "textit") {
      MathNode t = make_text(parse_text_arg());
      t.variant = name == "textbf" ? MathVariant::Bold : MathVariant::Italic;
      return t;
    }
    if (name == "operatorname" || name == "mathop") {
      bool star = false;
      if (name == "operatorname") {
        skip_space();
        const Tok st = peek();
        if (st.t == T::Char && st.cp == '*') {
          next();
          star = true;
        }
      }
      MathNode arg = parse_arg();
      if (name == "mathop") return with_class(std::move(arg), C::Op);
      return operator_name(flat_text(arg), star);
    }
    if (name == "mathbin") return with_class(parse_arg(), C::Bin);
    if (name == "mathrel") return with_class(parse_arg(), C::Rel);
    if (name == "mathord") return with_class(parse_arg(), C::Ord);
    if (name == "mathopen") return with_class(parse_arg(), C::Open);
    if (name == "mathclose") return with_class(parse_arg(), C::Close);
    if (name == "mathpunct") return with_class(parse_arg(), C::Punct);
    if (name == "mathinner") return with_class(parse_arg(), C::Inner);

    if (name == "frac" || name == "dfrac" || name == "tfrac" ||
        name == "cfrac") {
      MathNode a = parse_arg();
      MathNode b = parse_arg();
      const int st = name == "dfrac" || name == "cfrac" ? 0
                     : name == "tfrac"                  ? 2
                                                        : -1;
      return frac(std::move(a), std::move(b), true, st);
    }
    if (name == "binom" || name == "dbinom" || name == "tbinom") {
      MathNode a = parse_arg();
      MathNode b = parse_arg();
      const int st = name == "dbinom" ? 0 : name == "tbinom" ? 2 : -1;
      MathNode f = frac(std::move(a), std::move(b), false, st);
      return fenced(make_list({std::move(f)}), '(', ')');
    }
    if (name == "genfrac") {
      const uint32_t l = parse_delim_group();
      const uint32_t r = parse_delim_group();
      const std::string thick = parse_text_arg();
      const std::string sty = parse_text_arg();
      MathNode a = parse_arg();
      MathNode b = parse_arg();
      int st = -1;
      if (!sty.empty() && sty[0] >= '0' && sty[0] <= '3') st = (sty[0] - '0') * 2;
      MathNode f = frac(std::move(a), std::move(b), thick != "0pt" && thick != "0", st);
      if (!l && !r) return f;
      return fenced(make_list({std::move(f)}), l, r);
    }
    if (name == "sqrt") {
      MathNode idx;
      const bool has = parse_optional(idx);
      MathNode body = parse_arg();
      MathNode r;
      r.kind = MathKind::Radical;
      r.kids.push_back(std::move(body));
      if (has) {
        r.has_b = true;
        r.kids.push_back(std::move(idx));
      }
      return r;
    }
    if (name == "left") {
      const uint32_t l = parse_delim();
      MathNode body = parse_seq(kEndRight);
      uint32_t r = 0;
      const Tok t = peek();
      if (t.t == T::Cmd && t.name == "right") {
        next();
        r = parse_delim();
      } else {
        fail("missing \\right");
      }
      return fenced(std::move(body), l, r);
    }
    if (name == "right") {
      fail("\\right without \\left");
      return make_list();
    }
    if (name == "begin") return parse_environment();
    if (name == "end") {
      fail("\\end without \\begin");
      return make_list();
    }
    if (name == "substack") {
      // \substack{a \\ b}: a one-column matrix in script style.
      skip_space();
      if (peek().t == T::Open) next();
      std::vector<MathNode> rows;
      for (;;) {
        rows.push_back(parse_seq(kEndCell));
        const Tok t = peek();
        if (t.t == T::Cmd && t.name == "\\") {
          next();
          continue;
        }
        break;
      }
      if (peek().t == T::Close) next();
      MathNode m;
      m.kind = MathKind::Matrix;
      m.rows = static_cast<int>(rows.size());
      m.cols = 1;
      m.align = "c";
      m.has_b = true;
      m.kids = std::move(rows);
      return m;
    }
    if (name == "overline" || name == "underline") {
      MathNode n;
      n.kind = MathKind::Rule;
      n.above = name == "overline";
      n.kids.push_back(parse_arg());
      return n;
    }
    if (name == "overset" || name == "underset" || name == "stackrel" ||
        name == "overarc") {
      MathNode a = parse_arg();
      MathNode b = parse_arg();
      MathNode n;
      n.kind = MathKind::OverUnder;
      n.above = name != "underset";
      n.cls = name == "stackrel" ? C::Rel : C::Ord;
      n.kids.push_back(std::move(b));
      n.kids.push_back(std::move(a));
      return n;
    }
    if (name == "xrightarrow" || name == "xleftarrow" ||
        name == "xleftrightarrow" || name == "xRightarrow" ||
        name == "xLeftarrow" || name == "xmapsto" || name == "xhookrightarrow") {
      MathNode below;
      const bool has_below = parse_optional(below);
      MathNode above = parse_arg();
      MathNode arrow;
      arrow.kind = MathKind::Atom;
      arrow.cls = C::Rel;
      arrow.stretchy = true;
      arrow.cp = name == "xrightarrow"       ? 0x2192
                 : name == "xleftarrow"      ? 0x2190
                 : name == "xleftrightarrow" ? 0x2194
                 : name == "xRightarrow"     ? 0x21D2
                 : name == "xLeftarrow"      ? 0x21D0
                 : name == "xmapsto"         ? 0x21A6
                                             : 0x21AA;
      MathNode n;
      n.kind = MathKind::OverUnder;
      n.cls = C::Rel;
      n.stretchy = true;
      n.has_a = has_below;
      n.kids.push_back(std::move(arrow));
      n.kids.push_back(std::move(above));
      if (has_below) n.kids.push_back(std::move(below));
      return n;
    }
    if (name == "phantom" || name == "hphantom" || name == "vphantom") {
      MathNode n;
      n.kind = MathKind::Phantom;
      n.kids.push_back(parse_arg());
      n.has_a = name != "vphantom";  // keeps the width
      n.has_b = name != "hphantom";  // keeps the height
      return n;
    }
    if (name == "pmod" || name == "pod") {
      MathNode body = parse_arg();
      std::vector<MathNode> k;
      k.push_back(make_space(0.667f));
      k.push_back(make_atom('(', C::Open));
      if (name == "pmod") {
        k.push_back(make_text("mod"));
        k.push_back(make_space(1.f / 3.f));
      }
      k.push_back(std::move(body));
      k.push_back(make_atom(')', C::Close));
      return make_list(std::move(k));
    }
    if (name == "bmod" || name == "mod") {
      std::vector<MathNode> k;
      if (name == "mod") k.push_back(make_space(0.667f));
      MathNode m = make_text("mod");
      m.cls = name == "bmod" ? C::Bin : C::Ord;
      k.push_back(std::move(m));
      if (name == "mod") k.push_back(make_space(1.f / 3.f));
      MathNode l = make_list(std::move(k));
      l.cls = name == "bmod" ? C::Bin : C::Ord;
      return l;
    }
    if (name == "quad") return make_space(1.f);
    if (name == "qquad") return make_space(2.f);
    if (name == "enspace") return make_space(0.5f);
    if (name == "thinspace") return make_space(3.f / 18.f);
    if (name == "medspace") return make_space(4.f / 18.f);
    if (name == "thickspace") return make_space(5.f / 18.f);
    if (name == "negthinspace") return make_space(-3.f / 18.f);
    if (name == "negmedspace") return make_space(-4.f / 18.f);
    if (name == "negthickspace") return make_space(-5.f / 18.f);
    if (name == "hspace" || name == "hskip" || name == "kern" ||
        name == "mkern" || name == "mskip") {
      return make_space(parse_length(name == "hspace"));
    }
    if (name == "displaystyle") return style_rest(0, ends);
    if (name == "textstyle") return style_rest(2, ends);
    if (name == "scriptstyle") return style_rest(4, ends);
    if (name == "scriptscriptstyle") return style_rest(6, ends);
    if (name == "limits" || name == "nolimits") return make_list();
    if (name == "color") {
      parse_text_arg();
      return make_list();
    }
    if (name == "textcolor" || name == "colorbox") {
      parse_text_arg();
      return parse_arg();
    }
    if (name == "boxed" || name == "fbox" || name == "mathstrut" ||
        name == "strut" || name == "relax" || name == "nonumber" ||
        name == "notag" || name == "displaylines" || name == "protect" ||
        name == "allowbreak" || name == "nobreak" || name == "hline" ||
        name == "hdashline" || name == "vcenter")
      return name == "boxed" || name == "vcenter" ? parse_arg() : make_list();
    if (name == "tag" || name == "label" || name == "ensuremath") {
      if (name == "ensuremath") return parse_arg();
      parse_text_arg();
      return make_list();
    }
    if (name == "not") {
      // \not= and friends: the negated relation when Unicode has one.
      MathNode a = parse_arg();
      if (a.kind == MathKind::Atom && a.text.empty()) {
        static const struct { uint32_t from, to; } kNeg[] = {
            {'=', 0x2260},    {0x2208, 0x2209}, {0x220B, 0x220C},
            {0x2282, 0x2284}, {0x2283, 0x2285}, {0x2286, 0x2288},
            {0x2287, 0x2289}, {0x2261, 0x2262}, {0x2248, 0x2249},
            {0x2243, 0x2244}, {0x2245, 0x2247}, {0x223C, 0x2241},
            {0x2264, 0x2270}, {0x2265, 0x2271}, {'<', 0x226E},
            {'>', 0x226F},    {0x2223, 0x2224}, {0x2225, 0x2226},
            {0x2192, 0x219B}, {0x2190, 0x219A}, {0x21D2, 0x21CF},
            {0x21D0, 0x21CD}, {0x21D4, 0x21CE}, {0x2203, 0x2204},
            {0x22A2, 0x22AC}, {0x22A8, 0x22AD},
        };
        for (const auto& p : kNeg)
          if (p.from == a.cp) {
            a.cp = p.to;
            a.cls = C::Rel;
            return a;
          }
      }
      return a;
    }
    if (name == "over" || name == "choose" || name == "atop" ||
        name == "above") {
      // Handled by parse_seq; reaching this means no enclosing sequence.
      fail("misplaced \\" + name);
      return make_list();
    }

    fail("unknown command \\" + name);
    return make_text("\\" + name);
  }

  // `{(}`-style delimiter argument of \genfrac (empty = none).
  uint32_t parse_delim_group() {
    const std::string s = parse_text_arg();
    if (s.empty()) return 0;
    MathParse p = parse_math(s);
    if (!p.root.kids.empty() && p.root.kids[0].kind == MathKind::Atom)
      return p.root.kids[0].cp;
    return 0;
  }

  // A TeX length (`1em`, `3mu`, `2pt`, optionally braced) in em.
  float parse_length(bool braced) {
    skip_space();
    std::string raw;
    if (braced || peek().t == T::Open) {
      if (peek().t == T::Open) next();
      raw = raw_group();
    } else {
      while (i_ < s_.size() &&
             (isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' ||
              s_[i_] == '-' || s_[i_] == '+'))
        raw.push_back(s_[i_++]);
    }
    char* end = nullptr;
    const float v = std::strtof(raw.c_str(), &end);
    const std::string unit = end ? std::string(end) : "";
    if (unit == "em") return v;
    if (unit == "mu") return v / 18.f;
    if (unit == "ex") return v * 0.43f;
    if (unit == "pt") return v / 10.f;
    if (unit == "px") return v / 16.f;
    if (unit == "cm") return v * 28.45f / 10.f;
    if (unit == "mm") return v * 2.845f / 10.f;
    if (unit == "in") return v * 72.27f / 10.f;
    if (unit.empty()) return v;
    return v;
  }

  // A sequence of items up to a terminator (not consumed).
  MathNode parse_seq(unsigned ends) {
    std::vector<MathNode> nodes;
    // Infix fractions (`a \over b`): numerator so far.
    bool infix = false;
    bool infix_bar = true;
    uint32_t infix_l = 0, infix_r = 0;
    std::vector<MathNode> numerator;

    while (!at_end_of(ends)) {
      Tok t = next();
      if (t.t == T::Cmd &&
          (t.name == "over" || t.name == "choose" || t.name == "atop" ||
           t.name == "above")) {
        if (infix) fail("ambiguous \\" + t.name);
        if (t.name == "above") parse_length(false);
        infix = true;
        infix_bar = t.name == "over" || t.name == "above";
        if (t.name == "choose") infix_l = '(', infix_r = ')';
        numerator = std::move(nodes);
        nodes.clear();
        continue;
      }
      if (t.t == T::Sup || t.t == T::Sub) {
        // Scripts without a nucleus.
        nodes.push_back(make_list());
        attach_scripts(nodes, t);
        continue;
      }
      MathNode n = parse_token(t, ends);
      const bool is_style = n.kind == MathKind::Style && n.style >= 0 &&
                            t.t == T::Cmd && t.name.find("style") != std::string::npos;
      nodes.push_back(std::move(n));
      if (is_style) break;  // swallowed the rest of the group
      attach_scripts(nodes, Tok{});
    }

    MathNode list = make_list(std::move(nodes));
    if (infix) {
      MathNode f = frac(make_list(std::move(numerator)), std::move(list),
                        infix_bar, -1);
      if (infix_l || infix_r)
        return make_list({fenced(make_list({std::move(f)}), infix_l, infix_r)});
      return make_list({std::move(f)});
    }
    return list;
  }

  // Attaches ^ _ ' and \limits to the last node. `first` is a token already
  // read that starts the scripts (when the sequence began with one).
  void attach_scripts(std::vector<MathNode>& nodes, Tok first) {
    MathNode* script = nullptr;
    bool have_first = first.t == T::Sup || first.t == T::Sub;
    for (;;) {
      Tok t;
      if (have_first) {
        t = first;
        have_first = false;
      } else {
        t = peek();
        if (t.t == T::Cmd && (t.name == "limits" || t.name == "nolimits")) {
          next();
          MathNode& base = script ? script->kids[0] : nodes.back();
          base.limits = t.name == "limits" ? 1 : 2;
          continue;
        }
        const bool prime = t.t == T::Char && t.cp == '\'';
        if (t.t != T::Sup && t.t != T::Sub && !prime) return;
        if (!prime) next();
      }
      if (!script) {
        MathNode s;
        s.kind = MathKind::Script;
        s.kids.resize(3);
        s.kids[0] = std::move(nodes.back());
        s.cls = s.kids[0].cls;
        nodes.back() = std::move(s);
        script = &nodes.back();
      }
      if (t.t == T::Char) {  // primes: x'' → x^{′′}
        std::string primes;
        while (peek().t == T::Char && peek().cp == '\'') {
          next();
          primes += "\xE2\x80\xB2";
        }
        MathNode p = make_list();
        p.kids.push_back(make_atom(0x2032 + (primes.size() == 6 ? 1 : primes.size() == 9 ? 2 : 0), C::Ord));
        if (script->has_b) {
          script->kids[2].kids.push_back(std::move(p.kids[0]));
        } else {
          script->has_b = true;
          script->kids[2] = std::move(p);
        }
        continue;
      }
      MathNode arg = parse_arg();
      if (t.t == T::Sup) {
        if (script->has_b) {
          fail("double superscript");
          continue;
        }
        script->has_b = true;
        script->kids[2] = std::move(arg);
      } else {
        if (script->has_a) {
          fail("double subscript");
          continue;
        }
        script->has_a = true;
        script->kids[1] = std::move(arg);
      }
    }
  }
};

}  // namespace

MathParse parse_math(std::string_view tex) {
  Parser p(tex);
  return p.run();
}

// ---------------------------------------------------------------------------
// Alphabets
// ---------------------------------------------------------------------------

uint32_t math_alphabet(uint32_t cp, MathVariant v) {
  const bool upper = cp >= 'A' && cp <= 'Z';
  const bool lower = cp >= 'a' && cp <= 'z';
  const bool digit = cp >= '0' && cp <= '9';
  const bool g_upper = cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2;
  const bool g_lower = cp >= 0x3B1 && cp <= 0x3C9;
  if (v == MathVariant::Auto) {
    // TeX sets Latin letters, lower-case Greek and ∂ in italic.
    const bool greek_variant = cp == 0x2202 || cp == 0x3F5 || cp == 0x3D1 ||
                               cp == 0x3F0 || cp == 0x3D5 || cp == 0x3F1 ||
                               cp == 0x3D6;
    if (lower || upper || g_lower || greek_variant) v = MathVariant::Italic;
    else return cp;
  }
  if (v == MathVariant::Roman) return cp;

  auto letter = [&](uint32_t up, uint32_t lo) -> uint32_t {
    return upper ? up + (cp - 'A') : lo + (cp - 'a');
  };
  if (upper || lower) {
    switch (v) {
      case MathVariant::Bold: return letter(0x1D400, 0x1D41A);
      case MathVariant::Italic:
        return cp == 'h' ? 0x210E : letter(0x1D434, 0x1D44E);
      case MathVariant::BoldItalic: return letter(0x1D468, 0x1D482);
      case MathVariant::Script: {
        static const struct { uint32_t from, to; } kHoles[] = {
            {'B', 0x212C}, {'E', 0x2130}, {'F', 0x2131}, {'H', 0x210B},
            {'I', 0x2110}, {'L', 0x2112}, {'M', 0x2133}, {'R', 0x211B},
            {'e', 0x212F}, {'g', 0x210A}, {'o', 0x2134}};
        for (const auto& h : kHoles)
          if (h.from == cp) return h.to;
        return letter(0x1D49C, 0x1D4B6);
      }
      case MathVariant::Fraktur: {
        static const struct { uint32_t from, to; } kHoles[] = {
            {'C', 0x212D}, {'H', 0x210C}, {'I', 0x2111}, {'R', 0x211C},
            {'Z', 0x2128}};
        for (const auto& h : kHoles)
          if (h.from == cp) return h.to;
        return letter(0x1D504, 0x1D51E);
      }
      case MathVariant::DoubleStruck: {
        static const struct { uint32_t from, to; } kHoles[] = {
            {'C', 0x2102}, {'H', 0x210D}, {'N', 0x2115}, {'P', 0x2119},
            {'Q', 0x211A}, {'R', 0x211D}, {'Z', 0x2124}};
        for (const auto& h : kHoles)
          if (h.from == cp) return h.to;
        return letter(0x1D538, 0x1D552);
      }
      case MathVariant::SansSerif: return letter(0x1D5A0, 0x1D5BA);
      case MathVariant::Monospace: return letter(0x1D670, 0x1D68A);
      default: return cp;
    }
  }
  if (digit) {
    switch (v) {
      case MathVariant::Bold:
      case MathVariant::BoldItalic: return 0x1D7CE + (cp - '0');
      case MathVariant::DoubleStruck: return 0x1D7D8 + (cp - '0');
      case MathVariant::SansSerif: return 0x1D7E2 + (cp - '0');
      case MathVariant::Monospace: return 0x1D7F6 + (cp - '0');
      default: return cp;
    }
  }
  if (g_upper || g_lower) {
    uint32_t base = 0;
    switch (v) {
      case MathVariant::Bold: base = g_upper ? 0x1D6A8 : 0x1D6C2; break;
      case MathVariant::Italic: base = g_upper ? 0x1D6E2 : 0x1D6FC; break;
      case MathVariant::BoldItalic: base = g_upper ? 0x1D71C : 0x1D736; break;
      default: return cp;
    }
    return base + (g_upper ? cp - 0x391 : cp - 0x3B1);
  }
  // The variant forms of lower-case Greek and ∂.
  if (v == MathVariant::Bold || v == MathVariant::Italic ||
      v == MathVariant::BoldItalic) {
    const uint32_t base = v == MathVariant::Bold     ? 0x1D6C2
                          : v == MathVariant::Italic ? 0x1D6FC
                                                     : 0x1D736;
    switch (cp) {
      case 0x2202: return base + 25;
      case 0x3F5: return base + 26;
      case 0x3D1: return base + 27;
      case 0x3F0: return base + 28;
      case 0x3D5: return base + 29;
      case 0x3F1: return base + 30;
      case 0x3D6: return base + 31;
      default: break;
    }
  }
  return cp;
}

}  // namespace xanatoo
