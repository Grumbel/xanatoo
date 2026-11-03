// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// MathNode tree → glyphs and rules (see math.hpp). TeXbook Appendix G with
// the constants and stretchy glyphs of an OpenType MATH table. y grows up;
// every length is in em of the base font size, scaled by the size of the
// style it is set in.

#include "xanatoo/math.hpp"

#include <algorithm>
#include <cmath>

namespace xanatoo {

namespace {

using C = MathClass;

// TeX styles: 0 D, 1 D', 2 T, 3 T', 4 S, 5 S', 6 SS, 7 SS' (odd = cramped).
constexpr int kDisplay = 0;
constexpr int kText = 2;
constexpr int kScript = 4;
constexpr int kScriptScript = 6;

int sup_style(int s) { return 2 * (s / 4) + 4 + (s % 2); }
int sub_style(int s) { return 2 * (s / 4) + 5; }
int num_style(int s) { return s + 2 - 2 * (s / 6); }
int den_style(int s) { return 2 * (s / 2) + 3 - 2 * (s / 6); }
int cramped(int s) { return s | 1; }

struct Box {
  float w = 0.f;
  float asc = 0.f;
  float desc = 0.f;
  float italic = 0.f;
  float accent = -1.f;  // x of the top accent attachment
  bool is_char = false;
  std::vector<MathItem> items;
};

void put(Box& d, const Box& s, float x, float y) {
  for (MathItem it : s.items) {
    it.x += x;
    it.y += y;
    d.items.push_back(it);
  }
  d.w = std::max(d.w, x + s.w);
  d.asc = std::max(d.asc, y + s.asc);
  d.desc = std::max(d.desc, s.desc - y);
}

void add_rule(Box& d, float x, float y, float w, float h) {
  MathItem r;
  r.rule = true;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  d.items.push_back(r);
  d.w = std::max(d.w, x + w);
  d.asc = std::max(d.asc, y + h);
  d.desc = std::max(d.desc, -y);
}

bool is_big_op(uint32_t cp) {
  return cp == 0x2211 || cp == 0x220F || cp == 0x2210 ||
         (cp >= 0x222B && cp <= 0x2233) || (cp >= 0x22C0 && cp <= 0x22C3) ||
         (cp >= 0x2A00 && cp <= 0x2A06) || cp == 0x2A0C;
}

// Spacing between atom classes (Ord Op Bin Rel Open Close Punct Inner), see
// TeXbook ch. 18: 0 none, 1 thin, 2 medium, 3 thick; a / b / c are thin /
// medium / thick spaces that only exist outside script styles.
float spacing(C l, C r, int style) {
  static const char t[8][9] = {
      /*          Ord  Op   Bin  Rel  Open Cls  Pun  Inn */
      /* Ord  */ "01bc000a",
      /* Op   */ "110c000a",
      /* Bin  */ "bb00b00b",
      /* Rel  */ "cc00c000",
      /* Open */ "00000000",
      /* Close*/ "01bc000a",
      /* Punct*/ "aa0aaaaa",
      /* Inner*/ "a1bca0aa",
  };
  const char c = t[static_cast<int>(l)][static_cast<int>(r)];
  float mu = 0.f;
  switch (c) {
    case '1': mu = 3.f; break;
    case '2': mu = 4.f; break;
    case '3': mu = 5.f; break;
    case 'a': mu = style < kScript ? 3.f : 0.f; break;
    case 'b': mu = style < kScript ? 4.f : 0.f; break;
    case 'c': mu = style < kScript ? 5.f : 0.f; break;
    default: break;
  }
  return mu / 18.f;
}

class Layout {
public:
  Layout(const MathFace& face) : face_(face), K(face.constants()) {}

  Box node(const MathNode& n, int style) {
    switch (n.kind) {
      case MathKind::Atom: return atom(n, style);
      case MathKind::Text: return text(n, style);
      case MathKind::List: return list(n.kids, style);
      case MathKind::Frac: return frac(n, style);
      case MathKind::Radical: return radical(n, style);
      case MathKind::Script: return script(n, style);
      case MathKind::Fenced: return fenced(n, style);
      case MathKind::Accent: return accent(n, style);
      case MathKind::Rule: return rule(n, style);
      case MathKind::OverUnder: return over_under(n, style);
      case MathKind::Matrix: return matrix(n, style);
      case MathKind::Space: {
        Box b;
        b.w = n.amount * sz(style);
        return b;
      }
      case MathKind::Style: {
        if (n.kids.empty()) return Box{};
        const int s = n.style >= 0 ? n.style : style;
        return node(n.kids[0], s);
      }
      case MathKind::Phantom: {
        Box b = n.kids.empty() ? Box{} : node(n.kids[0], style);
        b.items.clear();
        if (!n.has_a) b.w = 0.f;
        if (!n.has_b) b.asc = b.desc = 0.f;
        return b;
      }
    }
    return Box{};
  }

private:
  const MathFace& face_;
  const MathConstants& K;

  float sz(int style) const {
    return style >= kScriptScript ? K.script_script_scale
           : style >= kScript     ? K.script_scale
                                  : 1.f;
  }
  float axis(int style) const { return K.axis_height * sz(style); }

  // -- glyphs ---------------------------------------------------------------

  Box glyph_box(uint32_t gid, int style) const {
    const MathGlyph m = face_.metrics(gid);
    const float s = sz(style);
    Box b;
    b.w = m.advance * s;
    b.asc = std::max(0.f, m.ascent * s);
    b.desc = std::max(0.f, m.descent * s);
    b.italic = m.italic * s;
    b.accent = m.accent >= 0.f ? m.accent * s : -1.f;
    b.is_char = true;
    MathItem it;
    it.gid = gid;
    it.size = s;
    b.items.push_back(it);
    return b;
  }

  // The glyph of a character in an alphabet; 0 when the font lacks it.
  uint32_t glyph_of(uint32_t cp, MathVariant v) const {
    const uint32_t mapped = math_alphabet(cp, v);
    uint32_t gid = face_.glyph(mapped);
    if (!gid && mapped != cp) gid = face_.glyph(cp);
    return gid;
  }

  // Moves the box vertically so its ink is centred on the math axis.
  void centre_on_axis(Box& b, int style) const {
    const float shift = axis(style) - (b.asc - b.desc) * 0.5f;
    for (MathItem& it : b.items) it.y += shift;
    b.asc += shift;
    b.desc -= shift;
  }

  // The glyph of `cp`, stretched along one axis to at least `required`.
  Box stretch(uint32_t cp, float required, bool vertical, int style,
              bool centre) const {
    const uint32_t gid = face_.glyph(cp);
    if (!gid) {
      Box b;
      b.w = 0.4f * sz(style);
      return b;
    }
    const float s = sz(style);
    const std::vector<MathVariantGlyph> vars = face_.variants(gid, vertical);
    uint32_t pick = gid;
    bool found = false;
    for (const MathVariantGlyph& v : vars) {
      pick = v.gid;
      if (v.size * s >= required) {
        found = true;
        break;
      }
    }
    Box b;
    if (!found) {
      std::vector<MathPart> parts;
      if (face_.assembly(gid, vertical, parts) && !parts.empty())
        b = assemble(parts, required / s, vertical, s);
      else
        b = glyph_box(pick, style);
    } else {
      b = glyph_box(pick, style);
    }
    if (vertical && centre) centre_on_axis(b, style);
    return b;
  }

  // Builds a glyph from parts (first part at the bottom / left).
  Box assemble(const std::vector<MathPart>& parts, float required,
               bool vertical, float s) const {
    const float minov = K.min_connector_overlap;
    bool has_ext = false;
    for (const MathPart& p : parts) has_ext = has_ext || p.extender;

    std::vector<size_t> seq;
    float lo = 0.f, hi = 0.f;
    std::vector<float> jmax;
    auto build = [&](int n) {
      seq.clear();
      for (size_t i = 0; i < parts.size(); ++i) {
        const int times = parts[i].extender ? n : 1;
        for (int t = 0; t < times; ++t) seq.push_back(i);
      }
      float sum = 0.f;
      jmax.assign(seq.empty() ? 0 : seq.size() - 1, 0.f);
      for (size_t k = 0; k < seq.size(); ++k) {
        sum += parts[seq[k]].advance;
        if (k + 1 < seq.size())
          jmax[k] = std::max(minov, std::min(parts[seq[k]].end_overlap,
                                             parts[seq[k + 1]].start_overlap));
      }
      float jsum = 0.f;
      for (float j : jmax) jsum += j;
      lo = sum - jsum;
      hi = sum - minov * static_cast<float>(jmax.size());
    };
    int n = 1;
    build(n);
    while (has_ext && hi < required && n < 64) build(++n);

    float f = hi > lo ? (required - lo) / (hi - lo) : 0.f;
    f = std::clamp(f, 0.f, 1.f);
    Box b;
    float cursor = 0.f;
    for (size_t k = 0; k < seq.size(); ++k) {
      const MathPart& p = parts[seq[k]];
      MathItem it;
      it.gid = p.gid;
      it.size = s;
      if (vertical) {
        // Placed by its ink: the part's lowest ink sits at the cursor.
        const MathGlyph m = face_.metrics(p.gid);
        it.x = 0.f;
        it.y = (cursor + m.descent) * s;
        b.asc = std::max(b.asc, it.y + m.ascent * s);
        b.w = std::max(b.w, m.advance * s);
      } else {
        it.x = cursor * s;
        it.y = 0.f;
        const MathGlyph m = face_.metrics(p.gid);
        b.asc = std::max(b.asc, m.ascent * s);
        b.desc = std::max(b.desc, m.descent * s);
        b.w = std::max(b.w, (cursor + p.advance) * s);
      }
      b.items.push_back(it);
      cursor += p.advance - (k < jmax.size() ? jmax[k] - f * (jmax[k] - minov) : 0.f);
    }
    return b;  // vertical: the ink bottom of the first part is at y = 0
  }

  // -- atoms ----------------------------------------------------------------

  Box atom(const MathNode& n, int style) {
    if (!n.text.empty()) return word(n.text, MathVariant::Roman, style);
    if (n.size > 0) {
      // \big … \Bigg: a fixed size, centred on the axis.
      static const float kSizes[] = {1.2f, 1.8f, 2.4f, 3.0f};
      return stretch(n.cp, kSizes[n.size - 1], true, kText, true);
    }
    const uint32_t gid = glyph_of(n.cp, n.variant);
    if (!gid) {
      Box b;
      b.w = 0.5f * sz(style);
      return b;
    }
    if (n.cls == C::Op && is_big_op(n.cp)) {
      // Large operators: a taller variant in display style, centred.
      const float want = style < kScript && style < 2
                             ? K.display_op_min_height
                             : 0.f;
      Box b = stretch(n.cp, want * sz(style), true, style, true);
      b.is_char = false;  // scripts are placed by its size, not like a letter
      return b;
    }
    if (n.stretchy) return stretch(n.cp, 0.f, false, style, false);
    return glyph_box(gid, style);
  }

  Box word(const std::string& s, MathVariant v, int style) {
    Box b;
    float x = 0.f;
    for (size_t i = 0; i < s.size();) {
      uint32_t cp = static_cast<unsigned char>(s[i]);
      size_t len = 1;
      if (cp >= 0xF0) { len = 4; cp &= 0x07; }
      else if (cp >= 0xE0) { len = 3; cp &= 0x0F; }
      else if (cp >= 0xC0) { len = 2; cp &= 0x1F; }
      for (size_t k = 1; k < len && i + k < s.size(); ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
      i += len;
      if (cp == ' ') {
        x += 0.25f * sz(style);
        continue;
      }
      const uint32_t gid = glyph_of(cp, v);
      if (!gid) {
        x += 0.5f * sz(style);
        continue;
      }
      Box g = glyph_box(gid, style);
      put(b, g, x, 0.f);
      x += g.w;
    }
    b.w = x;
    return b;
  }

  Box text(const MathNode& n, int style) {
    Box b = word(n.text, n.variant == MathVariant::Auto ? MathVariant::Roman
                                                        : n.variant, style);
    return b;
  }

  // -- lists ------------------------------------------------------------------

  static bool transparent(const MathNode& n) {
    return n.kind == MathKind::Space ||
           (n.kind == MathKind::List && n.kids.empty() && !n.has_a);
  }

  Box list(const std::vector<MathNode>& kids, int style) {
    const size_t cnt = kids.size();
    std::vector<Box> boxes;
    boxes.reserve(cnt);
    for (const MathNode& k : kids) boxes.push_back(node(k, style));

    // Atom classes, binary operators demoted where TeX does.
    std::vector<C> cls(cnt, C::Ord);
    std::vector<bool> real(cnt, false);
    int prev = -1;
    for (size_t i = 0; i < cnt; ++i) {
      if (transparent(kids[i])) continue;
      real[i] = true;
      C c = kids[i].cls;
      if (c == C::Bin) {
        if (prev < 0) {
          c = C::Ord;
        } else {
          const C p = cls[static_cast<size_t>(prev)];
          if (p == C::Bin || p == C::Op || p == C::Rel || p == C::Open ||
              p == C::Punct)
            c = C::Ord;
        }
      } else if ((c == C::Rel || c == C::Close || c == C::Punct) && prev >= 0 &&
                 cls[static_cast<size_t>(prev)] == C::Bin) {
        cls[static_cast<size_t>(prev)] = C::Ord;
      }
      cls[i] = c;
      prev = static_cast<int>(i);
    }
    if (prev >= 0 && cls[static_cast<size_t>(prev)] == C::Bin)
      cls[static_cast<size_t>(prev)] = C::Ord;

    Box out;
    float x = 0.f;
    int last = -1;
    for (size_t i = 0; i < cnt; ++i) {
      if (real[i] && last >= 0)
        x += spacing(cls[static_cast<size_t>(last)], cls[i], style) * sz(style);
      put(out, boxes[i], x, 0.f);
      x += boxes[i].w;
      out.w = x;
      if (real[i]) last = static_cast<int>(i);
    }
    out.w = x;
    if (cnt == 1) {
      out.italic = boxes[0].italic;
      out.accent = boxes[0].accent;
      out.is_char = false;
    } else if (cnt > 0) {
      out.italic = boxes[cnt - 1].italic;
    }
    return out;
  }

  // -- fractions ----------------------------------------------------------------

  Box frac(const MathNode& n, int style) {
    if (n.style >= 0) style = n.style | (style & 1);
    const float s = sz(style);
    const bool disp = style < kText;
    Box num = node(n.kids[0], num_style(style));
    Box den = node(n.kids[1], den_style(style));
    const float a = axis(style);
    const float t = n.bar ? K.fraction_rule_thickness * s : 0.f;
    float up, down;
    if (n.bar) {
      up = (disp ? K.fraction_numerator_display_shift_up
                 : K.fraction_numerator_shift_up) * s;
      down = (disp ? K.fraction_denominator_display_shift_down
                   : K.fraction_denominator_shift_down) * s;
      const float ngap = (disp ? K.fraction_numerator_display_gap_min
                               : K.fraction_numerator_gap_min) * s;
      const float dgap = (disp ? K.fraction_denominator_display_gap_min
                               : K.fraction_denominator_gap_min) * s;
      up = std::max(up, a + t * 0.5f + ngap + num.desc);
      down = std::max(down, den.asc + dgap + t * 0.5f - a);
    } else {
      up = (disp ? K.stack_top_display_shift_up : K.stack_top_shift_up) * s;
      down = (disp ? K.stack_bottom_display_shift_down
                   : K.stack_bottom_shift_down) * s;
      const float gap_min = (disp ? K.stack_display_gap_min : K.stack_gap_min) * s;
      const float gap = (up - num.desc) - (den.asc - (-down));
      if (gap < gap_min) {
        const float d = (gap_min - gap) * 0.5f;
        up += d;
        down += d;
      }
    }
    const float pad = 0.12f * s;  // nulldelimiterspace
    const float w = std::max(num.w, den.w);
    Box b;
    put(b, num, pad + (w - num.w) * 0.5f, up);
    put(b, den, pad + (w - den.w) * 0.5f, -down);
    if (n.bar) add_rule(b, pad, a - t * 0.5f, w, t);
    b.w = w + 2.f * pad;
    return b;
  }

  // -- radicals -------------------------------------------------------------------

  Box radical(const MathNode& n, int style) {
    const float s = sz(style);
    Box body = node(n.kids[0], cramped(style));
    const float t = K.radical_rule_thickness * s;
    float gap = (style < kText ? K.radical_display_vertical_gap
                               : K.radical_vertical_gap) * s;
    const float needed = body.asc + body.desc + gap + t;
    Box sign = stretch(0x221A, needed, true, style, false);
    // `sign` has its ink from desc below the baseline up to asc.
    const float total = sign.asc + sign.desc;
    if (total > needed) gap += (total - needed) * 0.5f;
    // Ink top of the sign = top of the rule.
    const float top = body.asc + gap + t;
    const float sign_y = top - sign.asc;

    Box out;
    float x = 0.f;
    Box idx;
    float idx_shift = 0.f;
    if (n.has_b && n.kids.size() > 1) {
      idx = node(n.kids[1], kScriptScript);
      idx_shift = sign_y - sign.desc + K.radical_degree_bottom_raise * total;
      x += K.radical_kern_before_degree * s;
      const float after = K.radical_kern_after_degree * s;
      // The index may not stick out to the left of the box.
      const float need = idx.w + x + after;
      if (need < 0.f) x -= need;
      put(out, idx, x, idx_shift);
      x += idx.w + after;
    }
    put(out, sign, x, sign_y);
    x += sign.w;
    put(out, body, x, 0.f);
    add_rule(out, x, top - t, body.w, t);
    out.w = x + body.w;
    out.asc = std::max(out.asc, top + K.radical_extra_ascender * s);
    return out;
  }

  // -- scripts --------------------------------------------------------------------

  Box script(const MathNode& n, int style) {
    const MathNode& base_n = n.kids[0];
    Box base = node(base_n, style);
    const bool big_op = base_n.kind == MathKind::Atom && base_n.cls == C::Op;
    const bool brace = base_n.kind == MathKind::Accent && base_n.stretchy &&
                       (base_n.cp == 0x23DE || base_n.cp == 0x23DF);
    const bool limits =
        brace ||
        (big_op && (base_n.limits == 1 ||
                    (base_n.limits == 0 && base_n.dlimits && style < kText)));

    const int sps = sup_style(style), sbs = sub_style(style);
    Box sup, sub;
    if (n.has_b) sup = node(n.kids[2], sps);
    if (n.has_a) sub = node(n.kids[1], sbs);
    const float s = sz(style);

    if (limits) {
      float w = base.w;
      if (n.has_b) w = std::max(w, sup.w);
      if (n.has_a) w = std::max(w, sub.w);
      Box out;
      put(out, base, (w - base.w) * 0.5f, 0.f);
      if (n.has_b) {
        const float up = std::max(K.upper_limit_baseline_rise_min * s,
                                  base.asc + K.upper_limit_gap_min * s + sup.desc);
        put(out, sup, (w - sup.w) * 0.5f + base.italic * 0.5f, up);
      }
      if (n.has_a) {
        const float dn = std::max(K.lower_limit_baseline_drop_min * s,
                                  base.desc + K.lower_limit_gap_min * s + sub.asc);
        put(out, sub, (w - sub.w) * 0.5f - base.italic * 0.5f, -dn);
      }
      out.w = w;
      return out;
    }

    const bool is_char = base.is_char;
    float u = 0.f, v = 0.f;
    if (!is_char) {
      u = base.asc - K.superscript_baseline_drop_max * sz(sps);
      v = base.desc + K.subscript_baseline_drop_min * sz(sbs);
    }
    float up = 0.f, down = 0.f;
    if (n.has_b) {
      const float p = (style & 1 ? K.superscript_shift_up_cramped
                                 : K.superscript_shift_up) * s;
      up = std::max({u, p, sup.desc + K.superscript_bottom_min * s});
    }
    if (n.has_a) {
      down = std::max({v, K.subscript_shift_down * s,
                       sub.asc - K.subscript_top_max * s});
    }
    if (n.has_a && n.has_b) {
      const float gap = (up - sup.desc) - (sub.asc - down);
      if (gap < K.sub_superscript_gap_min * s) {
        down += K.sub_superscript_gap_min * s - gap;
        const float psi =
            K.superscript_bottom_max_with_subscript * s - (up - sup.desc);
        if (psi > 0.f) {
          up += psi;
          down -= psi;
        }
      }
    }
    Box out;
    put(out, base, 0.f, 0.f);
    float end = base.w;
    const float space = K.space_after_script * s;
    if (n.has_b) {
      // Integrals lean right: their italic correction pushes the exponent.
      const float sx = base.w + (big_op ? 0.f : base.italic);
      put(out, sup, sx, up);
      end = std::max(end, sx + sup.w + space);
    }
    if (n.has_a) {
      const float sx = base.w - (big_op ? base.italic : 0.f);
      put(out, sub, sx, -down);
      end = std::max(end, sx + sub.w + space);
    }
    out.w = end;
    return out;
  }

  // -- delimiters ---------------------------------------------------------------

  Box fenced(const MathNode& n, int style) {
    Box body = n.kids.empty() ? Box{} : node(n.kids[0], style);
    const float s = sz(style);
    const float a = axis(style);
    const float delta = std::max(body.asc - a, body.desc + a);
    const float required =
        std::max(delta * 2.f * 0.901f, delta * 2.f - 0.5f * s);
    Box out;
    float x = 0.f;
    if (n.cp) {
      Box l = stretch(n.cp, required, true, style, true);
      put(out, l, 0.f, 0.f);
      x += l.w;
    }
    put(out, body, x, 0.f);
    x += body.w;
    if (n.cp2) {
      Box r = stretch(n.cp2, required, true, style, true);
      put(out, r, x, 0.f);
      x += r.w;
    }
    out.w = x;
    return out;
  }

  // -- accents and bars -----------------------------------------------------------

  // Spacing form of a combining accent, for fonts without the combining one.
  static uint32_t spacing_accent(uint32_t cp) {
    switch (cp) {
      case 0x300: return 0x60;
      case 0x301: return 0xB4;
      case 0x302: return 0x2C6;
      case 0x303: return 0x2DC;
      case 0x304: return 0xAF;
      case 0x306: return 0x2D8;
      case 0x307: return 0x2D9;
      case 0x308: return 0xA8;
      case 0x30A: return 0x2DA;
      case 0x30C: return 0x2C7;
      case 0x20D7: return 0x2192;
      default: return cp;
    }
  }

  Box accent(const MathNode& n, int style) {
    const float s = sz(style);
    Box base = node(n.kids[0], n.above ? cramped(style) : style);
    const bool combining = (n.cp >= 0x300 && n.cp <= 0x36F) ||
                           (n.cp >= 0x20D0 && n.cp <= 0x20FF);
    uint32_t cp = n.cp;
    if (!face_.glyph(cp)) cp = spacing_accent(cp);
    Box acc;
    if (n.stretchy) {
      acc = stretch(cp, base.w, false, style, false);
    } else if (const uint32_t gid = face_.glyph(cp)) {
      acc = glyph_box(gid, style);
    }
    Box out;
    put(out, base, 0.f, 0.f);
    // Over the attachment point of the base (its middle when it has none),
    // by the accent's own attachment point (its ink's middle).
    const float base_x = base.accent >= 0.f ? base.accent : base.w * 0.5f;
    float x = (base.w - acc.w) * 0.5f;
    if (!n.stretchy && acc.items.size() == 1) {
      const MathGlyph m = face_.metrics(acc.items[0].gid);
      const float k = acc.items[0].size;
      const float mid = m.accent >= 0.f ? m.accent : m.lsb + m.ink_w * 0.5f;
      x = base_x - mid * k;
    }
    float y;
    if (n.above) {
      // Combining accents sit at the height they were designed for; braces
      // and arrows just above the base.
      y = combining ? std::max(base.asc - K.accent_base_height * s, 0.f)
                    : base.asc + 0.1f * s;
    } else {
      y = -(base.desc + acc.asc + 0.1f * s);
    }
    put(out, acc, x, y);
    out.w = base.w;
    return out;
  }

  Box rule(const MathNode& n, int style) {
    const float s = sz(style);
    Box b = node(n.kids[0], n.above ? cramped(style) : style);
    Box out;
    put(out, b, 0.f, 0.f);
    if (n.above) {
      const float gap = K.overbar_vertical_gap * s;
      const float t = K.overbar_rule_thickness * s;
      add_rule(out, 0.f, b.asc + gap, b.w, t);
      out.asc = b.asc + gap + t + K.overbar_extra_ascender * s;
    } else {
      const float gap = K.underbar_vertical_gap * s;
      const float t = K.underbar_rule_thickness * s;
      add_rule(out, 0.f, -(b.desc + gap + t), b.w, t);
      out.desc = b.desc + gap + t + K.underbar_extra_descender * s;
    }
    out.w = b.w;
    return out;
  }

  Box over_under(const MathNode& n, int style) {
    const float s = sz(style);
    Box out;
    if (n.stretchy) {
      // \xrightarrow[below]{above}: the arrow grows with its labels.
      Box above = node(n.kids[1], sup_style(style));
      Box below;
      if (n.has_a && n.kids.size() > 2) below = node(n.kids[2], sub_style(style));
      const float w = std::max(above.w, below.w) + 0.9f * s;
      Box arrow = stretch(n.kids[0].cp, w, false, style, false);
      centre_on_axis(arrow, style);
      const float aw = std::max(w, arrow.w);
      put(out, arrow, (aw - arrow.w) * 0.5f, 0.f);
      put(out, above, (aw - above.w) * 0.5f, arrow.asc + 0.1f * s + above.desc);
      if (n.has_a) put(out, below, (aw - below.w) * 0.5f,
                       -(arrow.desc + 0.1f * s + below.asc));
      out.w = aw;
      return out;
    }
    Box base = node(n.kids[0], style);
    Box ann = node(n.kids[1], n.above ? sup_style(style) : sub_style(style));
    const float w = std::max(base.w, ann.w);
    put(out, base, (w - base.w) * 0.5f, 0.f);
    if (n.above) {
      const float up = std::max(K.upper_limit_baseline_rise_min * s,
                                base.asc + K.upper_limit_gap_min * s + ann.desc);
      put(out, ann, (w - ann.w) * 0.5f, up);
    } else {
      const float dn = std::max(K.lower_limit_baseline_drop_min * s,
                                base.desc + K.lower_limit_gap_min * s + ann.asc);
      put(out, ann, (w - ann.w) * 0.5f, -dn);
    }
    out.w = w;
    return out;
  }

  // -- matrices ---------------------------------------------------------------------

  Box matrix(const MathNode& n, int style) {
    const size_t rows = static_cast<size_t>(n.rows);
    const size_t cols = static_cast<size_t>(n.cols);
    const int cs = n.has_a ? kDisplay : n.has_b ? kScript
                   : style >= kScript ? style : kText;
    const float s = sz(cs);
    std::vector<Box> cells;
    cells.reserve(n.kids.size());
    for (const MathNode& k : n.kids) cells.push_back(node(k, cs));

    std::vector<float> colw(cols, 0.f), rasc(rows, 0.f), rdesc(rows, 0.f);
    for (size_t r = 0; r < rows; ++r) {
      rasc[r] = 0.7f * 1.2f * s;
      rdesc[r] = 0.3f * 1.2f * s;
      for (size_t c = 0; c < cols; ++c) {
        const Box& b = cells[r * cols + c];
        colw[c] = std::max(colw[c], b.w);
        rasc[r] = std::max(rasc[r], b.asc);
        rdesc[r] = std::max(rdesc[r], b.desc);
      }
    }
    std::vector<float> colx(cols, 0.f);
    float x = 0.f;
    for (size_t c = 0; c < cols; ++c) {
      colx[c] = x;
      x += colw[c];
      if (c + 1 < cols) {
        const char l = c < n.align.size() ? n.align[c] : 'c';
        const char r = c + 1 < n.align.size() ? n.align[c + 1] : 'c';
        float sep = 1.0f;
        if (n.has_a && l == 'r' && r == 'l') sep = 0.f;
        else if (n.has_a && l == 'l' && r == 'r') sep = 2.0f;
        if (n.has_b) sep = 0.5f;
        x += sep * s;
      }
    }
    float height = 0.f;
    for (size_t r = 0; r < rows; ++r) height += rasc[r] + rdesc[r];
    const float top = axis(cs) + height * 0.5f;

    Box out;
    float y = top;
    for (size_t r = 0; r < rows; ++r) {
      const float base_y = y - rasc[r];
      for (size_t c = 0; c < cols; ++c) {
        const Box& b = cells[r * cols + c];
        const char al = c < n.align.size() ? n.align[c] : 'c';
        const float dx = al == 'l' ? 0.f
                         : al == 'r' ? colw[c] - b.w
                                     : (colw[c] - b.w) * 0.5f;
        put(out, b, colx[c] + dx, base_y);
      }
      y -= rasc[r] + rdesc[r];
    }
    out.w = x;
    out.asc = top;
    out.desc = height - top;
    return out;
  }
};

}  // namespace

MathBox layout_math(const MathNode& root, const MathFace& face, bool display) {
  Layout lay(face);
  Box b = lay.node(root, display ? kDisplay : kText);
  MathBox out;
  out.width = b.w;
  out.ascent = b.asc;
  out.depth = b.desc;
  out.items = std::move(b.items);
  return out;
}

}  // namespace xanatoo
