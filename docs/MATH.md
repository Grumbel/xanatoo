# TeX math

Formulas are written as TeX, the way Wikipedia's `<math>` and most Markdown
tools do, and set by xanatoo itself (no TeX, no JavaScript, no new library).

## Where formulas come from

| Source | Syntax |
|--------|--------|
| Markdown | `$x^2$` inline, `$$ … $$` display (on one line, or over several lines as a block) |
| Org | `\(x^2\)`, `\[ … \]`, `$x^2$`, `$$ … $$` |
| S-expression | `(math "x^2")`, `(math "x^2" :display t)` |
| HTML / web pages | `<math>` (the `x-tex` annotation, else `alttext`), Wikipedia's `mwe-math-element` spans (the TeX comes from the annotation), KaTeX `.katex`, MathJax `<script type="math/tex">` |
| pandoc imports | pandoc writes `$…$` / `$$…$$` into the cached Markdown |

The dollar rules are pandoc's `tex_math_dollars`: the opening `$` is followed
and the closing one preceded by a non-space, and the closing one is not
followed by a digit, so "costs $5 and $6" stays text. `\$` is a dollar sign.

A formula is an `InlineKind::Math` span: `text` is U+FFFC (one caret
position, like inline images), `src` the TeX, `display` whether it is set in
display style on a line of its own (centred when alone in its paragraph).
Copying a formula gives `$src$` / `$$src$$`.

## Supported TeX

Symbols and Greek (the table `kSyms` in `math_parse.cpp`), `^ _ '`
scripts, `\frac \dfrac \tfrac \binom \genfrac`, `{a \over b}`, `\sqrt[n]{}`,
`\left … \right`, `\big … \Bigg`, big operators with limits (`\sum \prod
\int … \lim \max`, `\limits \nolimits \operatorname`), accents (`\hat \bar
\vec \dot \widehat \widetilde \overline \underline \overbrace \underbrace`),
`\overset \underset \stackrel \xrightarrow`, alphabets (`\mathbb \mathcal
\mathfrak \mathbf \mathrm \mathsf \mathtt \boldsymbol`), `\text`, spaces
(`\, \: \; \! \quad \qquad`), `\displaystyle` & co, `\phantom`, `\pmod`, and
the environments `matrix pmatrix bmatrix Bmatrix vmatrix Vmatrix smallmatrix
cases array aligned align split gather`, `\substack`.

Unknown commands stay in the formula as upright text (`\foo`) and the parser
reports the first problem in `MathParse::error`; nothing ever fails to show.
Not implemented: `\middle`, `\cancel`, `\color` (ignored), `\tag`, math
kerning of script corners (MathKernInfo), line breaking inside a formula.

## Design

- `math.hpp`: the tree (`MathNode`), `MathFace` (what layout needs of a
  font: glyph ids, metrics, MATH constants, size variants, assemblies) and the
  result `MathBox` (glyphs by id and rules, em units, y up). All pure.
- `math_parse.cpp`: TeX → tree; atom classes for spacing; alphabets map letters
  into the Unicode Mathematical Alphanumeric Symbols.
- `math_layout.cpp`: TeXbook Appendix G: inter-atom spacing table, fractions,
  radicals (`√` variant or assembly), scripts / limits, delimiters sized with
  `delimiterfactor 901` / `shortfall 0.5em`, accents (top-accent attachment),
  matrices. Constants come from the font's MATH table.
- `math_font.cpp`: `MathFont` reads the MATH table from FreeType
  (`FT_Load_Sfnt_Table`), loads glyphs by id (`raster`) and draws a box.
- Layout (`layout_paragraph`) sets a formula as an inline atom with ascent and
  depth (`PositionedMath`, drawn by `Renderer::draw`; paged reading moves them
  with the glyphs). The box is cached per source and style in the engine.

## Font

`find_math_font_path`: `data_dir()/fonts/latinmodern-math.otf` first, then the
first installed of Latin Modern Math, STIX Two Math, Libertinus Math, XITS
Math, TeX Gyre Pagella / Termes Math, Asana Math, Cambria Math, Fira Math,
DejaVu Math TeX Gyre, IBM Plex Math, Noto Sans Math. Without any, formulas
show their TeX source as code. A font without a MATH table works with typical
constants but cannot stretch delimiters or radicals.
