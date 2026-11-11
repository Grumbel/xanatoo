// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/render.hpp"
#include "xanatoo/math_font.hpp"
#include <cmath>

namespace xanatoo {

// Vertical positions floor (not truncate) so that shifting scroll_y by a
// whole pixel shifts every element by exactly that pixel, also above the
// clip top — the pane raster cache relies on this to blit scrolled content.
static int px(float v) { return static_cast<int>(std::floor(v)); }

static void set_color(SDL_Renderer* sdl, const Rgba& c) {
  SDL_SetRenderDrawColor(sdl, c.r, c.g, c.b, c.a);
}

static const DocStyle& fallback_style() {
  static const DocStyle style = builtin_style();
  return style;
}

Renderer::Renderer(SDL_Renderer* sdl, FontSet& fonts, ImageCache* images)
    : sdl_(sdl), fonts_(fonts), images_(images) {}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b) {
  SDL_SetRenderDrawColor(sdl_, r, g, b, 255);
  SDL_RenderClear(sdl_);
}

// `img` repeated over `rect`, the pattern anchored at (ax, ay).
static void tile_image(SDL_Renderer* sdl, const Image& img, SDL_Rect rect,
                       int ax, int ay) {
  if (!img.ok || !img.texture || img.width <= 0 || img.height <= 0) return;
  SDL_Rect old{};
  const bool was = xanatoo::sdl::clip_enabled(sdl);
  if (was) xanatoo::sdl::get_clip_rect(sdl, &old);
  SDL_Rect clip = rect;
  if (was && !xanatoo::sdl::intersect_rect(&old, &rect, &clip)) return;
  xanatoo::sdl::set_clip_rect(sdl, &clip);
  SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
  auto first = [](int from, int anchor, int size) {
    return anchor + static_cast<int>(std::floor(
                        static_cast<float>(from - anchor) / size)) * size;
  };
  for (int y = first(clip.y, ay, img.height); y < clip.y + clip.h;
       y += img.height)
    for (int x = first(clip.x, ax, img.width); x < clip.x + clip.w;
         x += img.width) {
      SDL_Rect to{x, y, img.width, img.height};
      xanatoo::sdl::copy(sdl, img.texture, nullptr, &to);
    }
  if (was) xanatoo::sdl::set_clip_rect(sdl, &old);
  else xanatoo::sdl::set_clip_rect(sdl, nullptr);
}

void Renderer::draw_backdrop(const LayoutResult& layout, const SDL_Rect& area,
                             float scroll_y, float origin_x) {
  const DocStyle& st = style_ ? *style_ : fallback_style();
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  const Image* margin_img = nullptr;
  const Image* paper_img = nullptr;
  if (images_) {
    if (!st.margin_image.empty())
      margin_img = &images_->get(style_asset_path(st.margin_image));
    if (!st.paper_image.empty())
      paper_img = &images_->get(style_asset_path(st.paper_image));
  }
  const int ox = static_cast<int>(std::floor(origin_x));
  const int oy = px(-scroll_y);
  auto paper = [&](SDL_Rect r, int ax) {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_, st.paper.r, st.paper.g, st.paper.b, 255);
    xanatoo::sdl::fill_rect(sdl_, &r);
    if (paper_img) tile_image(sdl_, *paper_img, r, ax, oy);
  };
  if (!st.has_margin() || layout.page_w <= 0.f) {
    paper(area, ox);
    return;
  }
  const Rgba m = st.margin.visible() ? st.margin : st.paper;
  auto margin = [&](SDL_Rect r) {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_, m.r, m.g, m.b, 255);
    xanatoo::sdl::fill_rect(sdl_, &r);
    if (margin_img) tile_image(sdl_, *margin_img, r, ox, oy);
  };
  margin(area);
  SDL_Rect sheet{px(layout.page_x + origin_x), area.y,
                 static_cast<int>(layout.page_w), area.h};
  SDL_Rect vis{};
  if (!xanatoo::sdl::intersect_rect(&sheet, &area, &vis)) return;
  paper(vis, sheet.x);
  for (const LayoutBox& g : layout.page_gaps) {
    SDL_Rect r{sheet.x, px(g.y - scroll_y), sheet.w,
               std::max(1, static_cast<int>(g.height))};
    SDL_Rect hit{};
    if (xanatoo::sdl::intersect_rect(&r, &vis, &hit)) margin(hit);
  }
}

void Renderer::draw(const LayoutResult& layout, float scroll_y, float origin_x) {
  const DocStyle& st = style_ ? *style_ : fallback_style();
  // Cull against the active SDL clip rect (pane / soft-window client area).
  SDL_Rect clip{};
  const bool clipped = xanatoo::sdl::clip_enabled(sdl_);
  if (clipped) xanatoo::sdl::get_clip_rect(sdl_, &clip);
  auto visible = [&](int x, int y, int w, int h) -> bool {
    if (!clipped) return true;
    if (y + h < clip.y || y > clip.y + clip.h) return false;
    if (x + w < clip.x || x > clip.x + clip.w) return false;
    return true;
  };

  // Message cards (drawn under images, text, code blocks and table fills)
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    for (const RoleBox& rb : layout.role_fills) {
      set_color(sdl_, layout.style_for(rb.box.node, st)[rb.role].background);
      SDL_Rect r{static_cast<int>(rb.box.x + origin_x), px(rb.box.y - scroll_y),
                 static_cast<int>(rb.box.width),
                 static_cast<int>(rb.box.height)};
      if (visible(r.x, r.y, r.w, r.h)) xanatoo::sdl::fill_rect(sdl_, &r);
    }
    for (const RoleBox& rb : layout.role_frames) {
      set_color(sdl_, layout.style_for(rb.box.node, st)[rb.role].border);
      SDL_Rect r{static_cast<int>(rb.box.x + origin_x), px(rb.box.y - scroll_y),
                 static_cast<int>(rb.box.width),
                 static_cast<int>(rb.box.height)};
      if (visible(r.x, r.y, r.w, r.h)) xanatoo::sdl::draw_rect(sdl_, &r);
    }
    for (const auto& mc : layout.message_cards) {
      const BlockRole role = mc.alt ? BlockRole::MessageAlt : BlockRole::Message;
      const Rgba& fill = layout.style_for(mc.box.node, st)[role].background;
      if (!fill.visible()) continue;
      set_color(sdl_, fill);
      SDL_Rect r{
          static_cast<int>(mc.box.x + origin_x),
          px(mc.box.y - scroll_y),
          static_cast<int>(mc.box.width),
          static_cast<int>(mc.box.height)};
      if (!visible(r.x, r.y, r.w, r.h)) continue;
      xanatoo::sdl::fill_rect(sdl_, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  if (images_) {
    for (const auto& pi : layout.images) {
      if (pi.page_w > 0) {
        // Fixed-layout page: only its sheet and frame live in the raster;
        // the tiles are painted over it every frame (app_pagetiles.cpp).
        SDL_Rect r{static_cast<int>(pi.x + origin_x), px(pi.y - scroll_y),
                   static_cast<int>(pi.width), static_cast<int>(pi.height)};
        if (!visible(r.x, r.y, r.w, r.h)) continue;
        const BlockStyle& ps = layout.style_for(pi.node, st)[BlockRole::Page];
        SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
        set_color(sdl_, ps.background);
        xanatoo::sdl::fill_rect(sdl_, &r);
        SDL_Rect frame{r.x - 1, r.y - 1, r.w + 2, r.h + 2};
        set_color(sdl_, ps.border);
        xanatoo::sdl::draw_rect(sdl_, &frame);
        SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
        continue;
      }
      const Image& img = pi.thumb > 0 ? images_->thumb(pi.src, pi.base, pi.thumb)
                                      : images_->get(pi.src, pi.base);
      SDL_Rect dst{
          static_cast<int>(pi.x + origin_x),
          px(pi.y - scroll_y),
          static_cast<int>(pi.width),
          static_cast<int>(pi.height)};
      if (!visible(dst.x, dst.y, dst.w, dst.h)) continue;
      if (img.ok && img.texture) {
        xanatoo::sdl::copy(sdl_, img.texture, nullptr, &dst);
      } else {
        SDL_SetRenderDrawColor(sdl_, 200, 200, 210, 255);
        xanatoo::sdl::draw_rect(sdl_, &dst);
        SDL_SetRenderDrawColor(sdl_, 230, 230, 235, 255);
        xanatoo::sdl::fill_rect(sdl_, &dst);
      }
      // Collection cards / remote images (click opens the viewer) and
      // images in the text are not resized by hand.
      if (pi.thumb > 0 || pi.in_text) continue;
      // Resize handle (bottom-right corner)
      const int hs = 10;
      SDL_Rect handle{dst.x + dst.w - hs, dst.y + dst.h - hs, hs, hs};
      SDL_SetRenderDrawColor(sdl_, 25, 90, 170, 255);
      xanatoo::sdl::fill_rect(sdl_, &handle);
    }
  }

  // Table header cell fills (drawn under text)
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    for (const auto& fill : layout.table_header_fills) {
      set_color(sdl_,
                layout.style_for(fill.node, st)[BlockRole::Table].background);
      SDL_Rect r{
          static_cast<int>(fill.x + origin_x),
          px(fill.y - scroll_y),
          static_cast<int>(fill.width),
          static_cast<int>(fill.height)};
      xanatoo::sdl::fill_rect(sdl_, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  // Fenced code-block soft background
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    for (const auto& cb : layout.code_blocks) {
      const BlockStyle& code =
          layout.style_for(cb.node, st)[BlockRole::CodeBlock];
      set_color(sdl_, code.background);
      SDL_Rect r{
          static_cast<int>(cb.x + origin_x),
          px(cb.y - scroll_y),
          static_cast<int>(cb.width),
          static_cast<int>(cb.height)};
      if (code.background.visible()) xanatoo::sdl::fill_rect(sdl_, &r);
      set_color(sdl_, code.border);
      if (code.border.visible()) xanatoo::sdl::draw_rect(sdl_, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
  // Block-quote (and transclusion) left bars
  for (const auto& qb : layout.quote_bars) {
    set_color(sdl_, layout.style_for(qb.node, st)[BlockRole::BlockQuote].border);
    int bw = static_cast<int>(qb.width);
    if (bw < 2) bw = 2;
    SDL_Rect bar{
        static_cast<int>(qb.x + origin_x),
        px(qb.y - scroll_y),
        bw,
        static_cast<int>(qb.height)};
    xanatoo::sdl::fill_rect(sdl_, &bar);
  }

  // Note left bars (amber, distinct from quotes)
  for (const auto& nb : layout.note_bars) {
    set_color(sdl_, layout.style_for(nb.node, st)[BlockRole::Note].border);
    int bw = static_cast<int>(nb.width);
    if (bw < 2) bw = 2;
    SDL_Rect bar{
        static_cast<int>(nb.x + origin_x),
        px(nb.y - scroll_y),
        bw,
        static_cast<int>(nb.height)};
    xanatoo::sdl::fill_rect(sdl_, &bar);
  }

  // Message / comment bars (teal — discussion thread)
  for (const auto& mb : layout.message_bars) {
    set_color(sdl_, layout.style_for(mb.node, st)[BlockRole::Message].border);
    int bw = static_cast<int>(mb.width);
    if (bw < 2) bw = 2;
    SDL_Rect bar{
        static_cast<int>(mb.x + origin_x),
        px(mb.y - scroll_y),
        bw,
        static_cast<int>(mb.height)};
    xanatoo::sdl::fill_rect(sdl_, &bar);
  }

  // Table grid lines
  for (const auto& tl : layout.table_lines) {
    set_color(sdl_, layout.style_for(tl.node, st)[BlockRole::Table].border);
    int w = static_cast<int>(tl.width);
    int h = static_cast<int>(tl.height);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_Rect line{
        static_cast<int>(tl.x + origin_x),
        px(tl.y - scroll_y),
        w, h};
    xanatoo::sdl::fill_rect(sdl_, &line);
  }

  // Rules under headers
  for (const RoleBox& rb : layout.role_rules) {
    set_color(sdl_, layout.style_for(rb.box.node, st)[rb.role].border);
    SDL_Rect line{static_cast<int>(rb.box.x + origin_x), px(rb.box.y - scroll_y),
                  static_cast<int>(rb.box.width),
                  std::max(1, static_cast<int>(rb.box.height))};
    xanatoo::sdl::fill_rect(sdl_, &line);
  }

  // Horizontal rules
  for (const auto& hr : layout.hr_lines) {
    set_color(sdl_, layout.style_for(hr.node, st)[BlockRole::Rule].border);
    int th = static_cast<int>(hr.height);
    if (th < 1) th = 1;
    SDL_Rect line{
        static_cast<int>(hr.x + origin_x),
        px(hr.y - scroll_y),
        static_cast<int>(hr.width),
        th};
    xanatoo::sdl::fill_rect(sdl_, &line);
  }
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);

  // Soft background runs under inline code
  {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    float run_x0 = 0.f, run_y = 0.f, run_x1 = 0.f;
    bool in_run = false;
    Rgba run_color{};
    auto flush_run = [&]() {
      if (!in_run) return;
      int x = static_cast<int>(run_x0 + origin_x) - 2;
      int y = px(run_y - scroll_y) - 2;
      int w = static_cast<int>(run_x1 - run_x0) + 4;
      int h = fonts_.pick(FontRole::Code).line_height() + 2;
      set_color(sdl_, run_color);
      SDL_Rect r{x, y, w, h};
      xanatoo::sdl::fill_rect(sdl_, &r);
      in_run = false;
    };
    for (const auto& pg : layout.glyphs) {
      if (pg.kind != InlineKind::Code) {
        flush_run();
        continue;
      }
      const Rgba bg = layout.style_for(pg.node, st)[InlineKind::Code].background;
      if (!bg.visible()) {
        flush_run();
        continue;
      }
      Font& font = fonts_.pick(pg.font);
      const Glyph& g = font.glyph(pg.codepoint);
      float adv = static_cast<float>((g.advance >> 6) > 0 ? (g.advance >> 6)
                                                          : (g.width + 1));
      float top = pg.y - static_cast<float>(font.ascender());
      if (!in_run) {
        run_x0 = pg.x;
        run_x1 = pg.x + adv;
        run_y = top;
        run_color = bg;
        in_run = true;
      } else if (bg == run_color && std::abs(top - run_y) < 0.5f &&
                 pg.x <= run_x1 + 2.f) {
        run_x1 = pg.x + adv;
      } else {
        flush_run();
        run_x0 = pg.x;
        run_x1 = pg.x + adv;
        run_y = top;
        run_color = bg;
        in_run = true;
      }
    }
    flush_run();
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
  }

  const auto& glyphs = layout.glyphs;
  for (size_t i = 0; i < glyphs.size(); ++i) {
    const PositionedGlyph& pg = glyphs[i];
    if (pg.kind == InlineKind::Image) continue;  // drawn with the images
    // The caret slot of a formula (its fallback text is drawn as code).
    if (pg.kind == InlineKind::Math && pg.codepoint == kInlineImageCp) continue;
    Font& font = fonts_.pick(pg.font);
    const Glyph& g = font.glyph(pg.codepoint);

    if (g.texture) {
      SDL_Rect dst;
      dst.x = static_cast<int>(pg.x + origin_x);
      dst.y = px(pg.y - g.bearing_y - scroll_y);
      dst.w = g.width;
      dst.h = g.height;
      if (visible(dst.x, dst.y, dst.w, dst.h)) {
        SDL_SetTextureColorMod(g.texture, pg.color.r, pg.color.g, pg.color.b);
        SDL_SetTextureAlphaMod(g.texture, pg.color.a);
        xanatoo::sdl::copy(sdl_, g.texture, nullptr, &dst);
      }
    }

    const bool underline = layout.style_for(pg.node, st)[pg.kind].underline;
    const bool strike = pg.kind == InlineKind::Strike;
    if (underline || strike) {
      // Decorations bridge to the next glyph when it continues the same
      // decoration on the same line, so spaces (no bitmap, justified gaps)
      // stay covered; a trailing space at a line or run end is left bare.
      const PositionedGlyph* next =
          i + 1 < glyphs.size() ? &glyphs[i + 1] : nullptr;
      const bool joins = next && next->node == pg.node &&
                         std::abs(next->y - pg.y) < 0.5f && next->x > pg.x &&
                         next->kind == pg.kind;
      if (pg.codepoint == ' ' && !joins) continue;
      const int ux = static_cast<int>(pg.x + origin_x);
      const int uw =
          joins ? static_cast<int>(next->x + origin_x) - ux
                : static_cast<int>((g.advance >> 6) > 0 ? (g.advance >> 6)
                                                        : (g.width + 1));
      if (!visible(ux, px(pg.y - g.bearing_y - scroll_y), uw,
                   g.height + 4))
        continue;
      SDL_SetRenderDrawColor(sdl_, pg.color.r, pg.color.g, pg.color.b, 255);
      if (underline) {
        const int uy = px(pg.y + 2.f - scroll_y);
        xanatoo::sdl::draw_line(sdl_, ux, uy, ux + uw, uy);
      }
      if (strike) {
        const int mid =
            px(pg.y - static_cast<float>(g.bearing_y) * 0.35f - scroll_y);
        xanatoo::sdl::draw_line(sdl_, ux, mid, ux + uw, mid);
      }
    }
  }

  // TeX formulas (InlineKind::Math), over the text.
  if (!layout.maths.empty()) {
    if (MathFont* mf = fonts_.math_font()) {
      for (const PositionedMath& pm : layout.maths) {
        const MathBox& b = *pm.box;
        const int bx = static_cast<int>(pm.x + origin_x);
        const int by = px(pm.y - scroll_y);
        if (!visible(bx, by - static_cast<int>(b.ascent * pm.px) - 2,
                     static_cast<int>(b.width * pm.px) + 2,
                     static_cast<int>((b.ascent + b.depth) * pm.px) + 4))
          continue;
        draw_math(sdl_, *mf, b, static_cast<float>(bx), static_cast<float>(by),
                  pm.px, pm.color);
      }
    }
  }
}

GlyphExtents Renderer::glyph_extents() const {
  GlyphExtents ext;
  for (FontRole role : {FontRole::Body, FontRole::BodyBold, FontRole::BodyItalic,
                        FontRole::BodyBoldItalic, FontRole::Heading,
                        FontRole::Code, FontRole::Small, FontRole::SmallBold,
                        FontRole::SmallItalic, FontRole::SmallBoldItalic,
                        FontRole::SmallCode}) {
    const Font& f = fonts_.pick(role);
    ext.push_back({static_cast<float>(f.ascender()),
                   static_cast<float>(-f.descender())});
  }
  return ext;
}

void Renderer::draw_selection(const std::vector<SelRect>& rects, float origin_x) {
  const Rgba& c = (style_ ? *style_ : fallback_style()).selection;
  draw_selection(rects, origin_x, c.r, c.g, c.b, c.a);
}

void Renderer::draw_selection(const std::vector<SelRect>& rects, float origin_x,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(sdl_, r, g, b, a);
  for (const auto& rect : rects) {
    SDL_Rect sr{
        static_cast<int>(rect.x + origin_x), px(rect.y),
        static_cast<int>(rect.w), static_cast<int>(rect.h)};
    xanatoo::sdl::fill_rect(sdl_, &sr);
  }
  SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_NONE);
}

void Renderer::draw_caret(const CaretScreenPos& pos, float origin_x) {
  if (!pos.valid) return;
  SDL_SetRenderDrawColor(sdl_, 25, 90, 170, 255);
  SDL_Rect r;
  r.x = static_cast<int>(pos.x + origin_x);
  r.y = static_cast<int>(pos.y - pos.height * 0.85f);
  r.w = 2;
  r.h = static_cast<int>(pos.height * 0.95f);
  xanatoo::sdl::fill_rect(sdl_, &r);
}

void Renderer::draw_text(Font& font, float x, float y_baseline,
                         const std::string& text,
                         uint8_t r, uint8_t g, uint8_t b) {
  size_t i = 0;
  float pen = x;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp = 0;
    size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
      cp = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f);
      len = 2;
    } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
      cp = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
      len = 3;
    } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
      cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
           ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
      len = 4;
    } else {
      ++i;
      continue;
    }
    i += len;
    const Glyph& gl = font.glyph(cp);
    if (gl.texture) {
      SDL_SetTextureColorMod(gl.texture, r, g, b);
      SDL_SetTextureAlphaMod(gl.texture, 255);  // glyphs share textures
      SDL_Rect dst{
          static_cast<int>(pen + gl.bearing_x),
          static_cast<int>(y_baseline - gl.bearing_y),
          gl.width, gl.height};
      xanatoo::sdl::copy(sdl_, gl.texture, nullptr, &dst);
    }
    pen += static_cast<float>(gl.advance >> 6);
  }
}

void Renderer::present() {
  SDL_RenderPresent(sdl_);
}

}  // namespace xanatoo
