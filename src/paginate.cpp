// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "xanatoo/paginate.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xanatoo {

namespace {

// Something a page break must not run through: a line of text or a block
// image, as a vertical extent of the continuous layout.
struct Atom {
  float top = 0.f;
  float bottom = 0.f;
  bool heading = false;
};

constexpr float kEps = 0.5f;

class Paginator {
public:
  Paginator(const LayoutResult& in, const PageSpec& spec, PageMap& map)
      : in_(in), spec_(spec), map_(map) {
    usable_ = std::max(40.f, spec.page_h - spec.pad_top - spec.pad_bottom);
    cols_ = std::max(1, spec.columns);
  }

  LayoutResult run() {
    scale_images();
    collect_atoms();
    cut_pages();
    return stack();
  }

private:
  const LayoutResult& in_;
  const PageSpec& spec_;
  PageMap& map_;
  float usable_ = 0.f;
  int cols_ = 1;
  float max_atom_h_ = 0.f;
  std::vector<Atom> atoms_;
  // Image heights after fitting into a page (index = in_.images).
  std::vector<float> img_h_;
  std::vector<float> img_w_;

  float asc(FontRole r) const {
    const size_t i = static_cast<size_t>(r);
    if (spec_.extents.empty()) return 12.f;
    return (i < spec_.extents.size() ? spec_.extents[i] : spec_.extents[0])
        .ascent;
  }
  float desc(FontRole r) const {
    const size_t i = static_cast<size_t>(r);
    if (spec_.extents.empty()) return 4.f;
    return (i < spec_.extents.size() ? spec_.extents[i] : spec_.extents[0])
        .descent;
  }

  void scale_images() {
    const float max_h = std::max(20.f, usable_ - 8.f * spec_.px_scale);
    img_h_.reserve(in_.images.size());
    img_w_.reserve(in_.images.size());
    for (const PositionedImage& im : in_.images) {
      float w = im.width, h = im.height;
      if (!im.in_text && h > max_h) {
        w *= max_h / h;
        h = max_h;
      }
      img_w_.push_back(w);
      img_h_.push_back(h);
    }
  }

  void collect_atoms() {
    float last_y = std::numeric_limits<float>::quiet_NaN();
    FontRole last_font = FontRole::Body;
    for (const PositionedGlyph& g : in_.glyphs) {
      if (g.y == last_y && g.font == last_font) continue;
      last_y = g.y;
      last_font = g.font;
      atoms_.push_back({g.y - asc(g.font), g.y + desc(g.font),
                        g.font == FontRole::Heading});
    }
    for (size_t i = 0; i < in_.images.size(); ++i) {
      const PositionedImage& im = in_.images[i];
      atoms_.push_back({im.y, im.y + img_h_[i], false});
    }
    std::sort(atoms_.begin(), atoms_.end(), [](const Atom& a, const Atom& b) {
      return a.top != b.top ? a.top < b.top : a.bottom < b.bottom;
    });
    // Same extent twice (one per face of a mixed line) is one atom.
    std::vector<Atom> u;
    u.reserve(atoms_.size());
    for (const Atom& a : atoms_) {
      if (!u.empty() && std::abs(u.back().top - a.top) < kEps &&
          std::abs(u.back().bottom - a.bottom) < kEps) {
        u.back().heading = u.back().heading || a.heading;
        continue;
      }
      u.push_back(a);
    }
    atoms_ = std::move(u);
    for (const Atom& a : atoms_)
      max_atom_h_ = std::max(max_atom_h_, a.bottom - a.top);
  }

  // First atom starting at or below y.
  size_t atom_at(float y) const {
    return static_cast<size_t>(
        std::lower_bound(atoms_.begin(), atoms_.end(), y - kEps,
                         [](const Atom& a, float v) { return a.top < v; }) -
        atoms_.begin());
  }

  // The latest y <= e that runs through no atom; a hard cut at e when the
  // search would shrink the page to nothing.
  float safe_cut(float e, float start) const {
    float c = e;
    for (int guard = 0; guard < 64; ++guard) {
      bool moved = false;
      size_t j = atom_at(c);
      while (j > 0) {
        --j;
        const Atom& a = atoms_[j];
        if (a.top < c - max_atom_h_ - 1.f) break;
        if (a.top < c - kEps && a.bottom > c + kEps) {
          c = a.top;
          moved = true;
        }
      }
      if (!moved) break;
    }
    if (c < start + 0.5f * usable_) return e;  // taller than the page: slice
    // A heading is not left at the foot of the page.
    size_t j = atom_at(c);
    while (j > 0 && atoms_[j - 1].heading && atoms_[j - 1].bottom <= c + kEps &&
           atoms_[j - 1].top > start + 0.3f * usable_) {
      c = atoms_[j - 1].top;
      --j;
    }
    return c;
  }

  void cut_pages() {
    const float lead = 4.f * spec_.px_scale;
    const float y_end = spec_.last_y > 0.f
                            ? spec_.last_y
                            : std::max(spec_.first_y + 1.f,
                                       in_.content_height - spec_.first_y);
    float start = spec_.first_y;
    {
      size_t j = atom_at(start);
      if (j < atoms_.size()) start = std::max(start, atoms_[j].top - lead);
    }
    for (int guard = 0; guard < 1000000; ++guard) {
      const float e = start + usable_;
      float end;
      if (e >= y_end - kEps) {
        end = std::max(y_end, start + 1.f);
      } else {
        end = safe_cut(e, start);
      }
      map_.pages.push_back({start, end, 0});
      if (end >= y_end - kEps) break;
      const size_t j = atom_at(end);
      if (j >= atoms_.size()) break;  // nothing but space is left
      start = std::max(end, atoms_[j].top - lead);
    }
  }

  // Page holding source y (the one whose start is the latest <= y).
  size_t page_of(float y) const {
    const auto& p = map_.pages;
    size_t lo = 0, hi = p.size();
    while (hi - lo > 1) {
      const size_t mid = (lo + hi) / 2;
      if (p[mid].start <= y + kEps)
        lo = mid;
      else
        hi = mid;
    }
    return lo;
  }

  float dy(size_t page) const {
    const size_t spread = page / static_cast<size_t>(cols_);
    return static_cast<float>(spread) * spec_.page_h + spec_.pad_top -
           map_.pages[page].start;
  }
  float dx(size_t page) const {
    return static_cast<float>(page % static_cast<size_t>(cols_)) * spec_.pitch;
  }

  // The part of `b` on each page it touches, mapped.
  template <typename Fn>
  void pieces(const LayoutBox& b, Fn&& emit) const {
    const auto& p = map_.pages;
    const float y0 = b.y, y1 = b.y + b.height;
    size_t pg = page_of(y0);
    if (b.height <= 0.f) {
      LayoutBox o = b;
      o.x += dx(pg);
      o.y += dy(pg);
      emit(o);
      return;
    }
    for (; pg < p.size() && p[pg].start < y1; ++pg) {
      const float a = std::max(y0, p[pg].start);
      const float z = std::min(y1, p[pg].end);
      if (z <= a) continue;
      LayoutBox o = b;
      o.x += dx(pg);
      o.y = a + dy(pg);
      o.height = z - a;
      emit(o);
    }
  }

  void split(const std::vector<LayoutBox>& from,
             std::vector<LayoutBox>& to) const {
    for (const LayoutBox& b : from)
      pieces(b, [&](const LayoutBox& o) { to.push_back(o); });
  }
  void split(const std::vector<RoleBox>& from, std::vector<RoleBox>& to) const {
    for (const RoleBox& r : from)
      pieces(r.box, [&](const LayoutBox& o) { to.push_back({o, r.role}); });
  }

  LayoutResult stack() {
    LayoutResult out;
    out.generation = in_.generation;
    out.materialised_top = in_.materialised_top;
    out.materialised_bottom = in_.materialised_bottom;
    const size_t np = map_.pages.size();
    std::vector<uint32_t> first(np, UINT32_MAX);

    out.glyphs.reserve(in_.glyphs.size());
    for (size_t i = 0; i < in_.glyphs.size(); ++i) {
      PositionedGlyph g = in_.glyphs[i];
      const size_t pg = page_of(g.y - asc(g.font) * 0.5f);
      g.x += dx(pg);
      g.y += dy(pg);
      if (first[pg] == UINT32_MAX) first[pg] = static_cast<uint32_t>(i);
      out.glyphs.push_back(g);
    }
    for (size_t i = 0; i < in_.images.size(); ++i) {
      PositionedImage im = in_.images[i];
      const size_t pg = page_of(im.in_text ? im.y + img_h_[i] * 0.5f : im.y);
      im.x += dx(pg) + (im.width - img_w_[i]) * 0.5f;
      im.y += dy(pg);
      im.width = img_w_[i];
      im.height = img_h_[i];
      out.images.push_back(std::move(im));
    }
    split(in_.boxes, out.boxes);
    split(in_.quote_bars, out.quote_bars);
    split(in_.note_bars, out.note_bars);
    split(in_.message_bars, out.message_bars);
    for (const MessageCard& m : in_.message_cards)
      pieces(m.box, [&](const LayoutBox& o) {
        out.message_cards.push_back({o, m.alt});
      });
    split(in_.code_blocks, out.code_blocks);
    split(in_.table_lines, out.table_lines);
    split(in_.table_header_fills, out.table_header_fills);
    split(in_.hr_lines, out.hr_lines);
    split(in_.role_fills, out.role_fills);
    split(in_.role_rules, out.role_rules);
    split(in_.role_frames, out.role_frames);
    for (const ColumnGap& g : in_.column_gaps) {
      LayoutBox b{INVALID_NODE, g.x, g.y, 0.f, g.height};
      pieces(b, [&](const LayoutBox& o) {
        ColumnGap n = g;
        n.x = o.x;
        n.y = o.y;
        n.height = o.height;
        out.column_gaps.push_back(n);
      });
    }
    if (in_.chrome_notice.height > 0.f) {
      bool done = false;
      pieces(in_.chrome_notice, [&](const LayoutBox& o) {
        if (!done) out.chrome_notice = o;
        done = true;
      });
    }
    // No separate sheet: every column is its own text area.
    out.page_x = 0.f;
    out.page_w = 0.f;
    out.content_height =
        static_cast<float>(map_.spread_count()) * spec_.page_h;

    for (size_t i = 0; i < np; ++i) {
      uint32_t f = first[i];
      if (f == UINT32_MAX) f = i > 0 ? map_.pages[i - 1].first_glyph : 0;
      map_.pages[i].first_glyph = f;
    }
    map_.columns = cols_;
    map_.page_h = spec_.page_h;
    map_.glyph_count = in_.glyphs.size();
    return out;
  }
};

}  // namespace

LayoutResult paginate(const LayoutResult& in, const PageSpec& spec,
                      PageMap& map) {
  map = PageMap{};
  map.columns = std::max(1, spec.columns);
  map.page_h = spec.page_h;
  Paginator p(in, spec, map);
  return p.run();
}

}  // namespace xanatoo
