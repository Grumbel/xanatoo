# Style sheets

A first step from `docs/rethinking-css-for-documents.md`: document
presentation lives in S-expression style sheets (`styles/*.xss`), kept apart
from the markup. A sheet styles **semantic roles** (paragraph, header, note,
link, …), never markup structure, and there are no selectors or specificity.

Pick one from the toolbar's style button (next to *Text width*) or
**View → Style sheet**. The choice is a reader preference stored as
`style=<name>` in `~/.config/xanatoo/view.conf`. Editing a sheet takes effect
via **Reload style sheets** in the same menu.

## Precedence

1. Reader view prefs: line spacing, margins, text width, justify, zoom,
   `--font`.
2. The selected style sheet, and the sheets it `inherit`s.
3. The built-in default (`builtin_style()`; `styles/default.xss` spells it
   out).

Every sheet starts from the built-in default, so it only needs the values it
changes. `XANATOO_FONT` (set by the Nix wrapper) only applies when the sheet
names no body family.

## Search path

Lowest priority first; a later directory replaces a sheet of the same name:

- `<data dir>/styles` (installed: `<exe>/../share/xanatoo/styles`; see
  `xanatoo --show-paths`)
- `$XDG_DATA_HOME/xanatoo/styles`, default `~/.local/share/xanatoo/styles`
  (your own sheets)

## Format

```scheme
(stylesheet
  (name "Night Amber")                 ; menu title (else the file stem)
  (description "…")
  (inherit "night")                    ; start from another sheet

  (define accent "#ffb454")            ; tokens: usable wherever a value goes
  (define gap 0.9em)

  (page (background "#221d18") (color "#e6d9c3")
        (selection (rgba 90 130 200 110))  ; washes over the text:
        (mark (rgba 230 160 30 70))        ;   the passage of the address (#…)
        (bookmark (rgba 90 180 110 60))    ;   bookmarks inside the page
        (margin-background "#4a5568")      ; desk around the page (see below)
        (margin-image "felt.png")          ; pattern tiled over the desk
        (image "paper.png"))               ; pattern tiled over the paper

  (fonts (body "Charter" "DejaVu Serif" "serif")   ; first installed one wins
         (heading "DejaVu Sans" "sans-serif")      ; default: the body family
         (heading-weight regular)                  ; bold | regular
         (heading-scale 1.5)                       ; heading px = body px × 1.5
         (body-scale 0.9)                          ; body px = zoom px × 0.9
         (code "Fira Mono" "monospace"))

  (role paragraph (space-after gap))
  (role header (color accent) (space-before 1em))
  (role link (color accent) (underline #t)))
```

The `(stylesheet …)` wrapper is optional. `;` starts a comment.

**Tokens are late-bound.** All `define`s across the inherit chain are
collected first and the child's win, so a child that redefines `accent`
recolours every parent rule that uses `accent` (see `night-amber.xss`).

**Values**

- Colours: `"#rgb"`, `"#rrggbb"`, `"#rrggbbaa"`, `(rgb r g b)`,
  `(rgba r g b a)`, `black`, `white`, `none` / `transparent`.
- Lengths: `14`, `14px`, or `0.8em`. `em` is the body font size, so it
  follows zoom.

**Block roles:** `paragraph`, `header`, `section`, `list`, `list-item`,
`block-quote`, `note`, `code-block`, `term`, `definition`, `figure`,
`caption`, `bibliography-entry`, `footnote`, `thread`, `message`,
`message-alt`, `message-author`, `message-op`, `message-meta`, `table`,
`rule`, `transclusion`, `aside`, `article`.

Message headers (forum / Reddit / Lemmy / Nostr comments) colour the author
with `message-author` (`message-op` when a reply comes from the thread
starter) and the date / score / reply-to with `message-meta`. Each message
is a card filled with the `message` background; replies at odd depth use
`message-alt` instead, so nesting alternates like old.reddit.

Their properties:

| property | meaning |
|----------|---------|
| `space-before`, `space-after` | vertical gap around the block |
| `indent` | text inset: after the side bar (quote, note, transclusion), list marker slot, code padding, definition / bibliography / footnote hang, nested section inset |
| `font` | `body`, `bold`, `italic`, `bold-italic`, `heading`, `code`; `small`, `small-bold`, `small-italic`, `small-bold-italic`, `small-code` are the same faces at 0.85 × the body size |
| `color` | text colour (else the page colour) |
| `background` | code-block fill, table header fill, message card fill, band behind a `header` / `thread` title |
| `border` | side bar, rule, table grid, code-block frame, footnote overlay rule, rule under a `header` / `thread` title, aside frame |
| `width` | preferred width of a side box (`aside`) |
| `side` | `left`, `right` or `none`: which side of the page a side box goes to |
| `placement` | `margin`, `float` or `flow`: where a side box goes (below) |

Side boxes are `aside` (text boxes of the document: `:::aside`, HTML
`<aside>` inside the article). Navigation and side bars of web pages are not
part of the text: they go into the Page panel (View → Page Navigation).
With `placement margin` (the default) a box goes into the page
margin beside the place where it stands; the text column keeps its lines,
the box does not push the text down, and boxes on one side stack. When the
margin is narrower than `width`, the text column gives up the difference as
long as 28 em of text remain; a margin under 8 em takes no boxes, and they
go into the flow at full width. `placement float` is the HTML way: the box
sits inside the column and the text runs past it (only while the box is at
most 45 % of the column and 10 em of text stay beside it). `placement flow`
always puts it in the text. Text inside a side box takes its `font` (small
faces) and `color` unless its own role sets one. `indent` is the padding
inside the box; with a `background` the box is filled and framed with
`border`, without one `border` is a bar on the side facing the text. A
document may pick the side (`:::aside side=left`).

**Page and margins.** `background` is the paper, the sheet the text column
sits on. Without `margin-background` / `margin-image` the paper fills the
whole view. With one of them the paper shrinks to the text column (plus a
little padding) and the margin colour / pattern shows left and right of it,
behind side boxes, and in the gaps between articles. The `article` role
makes those gaps: its `space-before` / `space-after` are bands across the
sheet where the margin shows through, so every `:::article` (or HTML
`<article>` that became one) looks like a separate sheet; a gap at the very
top or bottom of the page is simply the margin. Pattern images are looked up
in the style directories (or give an absolute path), tile from the top-left of
the view and scroll with the text. `desk.xss` is an example.

**Inline roles:** `text`, `emphasis`, `strong`, `strike`, `link`, `code`,
`cite`, `footnote-ref`, `image` (inline images; no colour of their own),
`math` (TeX formulas; the colour is the glyphs' and rules'), with the
properties `color` (beats the block colour), `font` (one of the block face
names above), `background` (only `code` uses it) and `underline` (`#t` /
`#f`).

**Inline semantic role:** `metadata` is independent of the kind of span.
An article's source hostname can be metadata *inside its title line*;
the link to its discussion can be metadata and still be a clickable `link`.
`(role metadata (font small) (color "#828282"))` controls this presentation
without needing site-specific renderer conditions. When present, the
metadata role's `font` and `color` override the inline-kind and block
styles; otherwise those existing styles apply. The built-in style gives
metadata small, muted text.

**Time is semantic, not a visual role.** An inline span can carry a separate
ISO 8601 `datetime` value, regardless of its visible text, link target or
style. HTML `<time datetime="2026-11-04T10:30:00Z">Wednesday</time>` is
preserved by the importer. Hovering the span shows three live values: the
instant in UTC, the instant in the reader's system-local timezone, and its
relative age (or how far in the future it lies). A `<time>` inside `<a>`
remains clickable; neither its text nor its destination is inferred from the
timestamp. A timezone (`Z` or an explicit offset) is required for unambiguous
hover conversion; unqualified dates still render as ordinary text.

Native services can annotate their own date spans the same way. Hacker News
uses this on stories and comments; its comment header also gives author,
timestamp and reply reference separate link targets. These distinctions do
not depend on the active `.xss` sheet, and custom sheets continue to control
their appearance via the existing `link` and `message-meta` roles.

Unknown roles, properties or bad values become warnings on stderr, and the
status line reports how many there were. The rest of the sheet still applies.

## Site looks

`hackernews.xss`, `reddit.xss` (old.reddit) and `wikipedia.xss` imitate those
sites in broad strokes: HN uses Verdana at `body-scale` 0.85 under an orange
`thread` band, article headings with small inline source hostnames and
caption-sized points / discussion links; Reddit uses blue authors with zebra
nesting under a pale blue band; Wikipedia uses serif titles over grey `header`
rules. They are ordinary sheets — the selection is still global, not per site.

## Implementation

- `include/xanatoo/style.hpp`, `src/style.cpp` — `DocStyle` (resolved
  values), `load_style`, `list_styles`, `builtin_style`.
- `include/xanatoo/sexpr.hpp` — reader shared with the `.sexp` document
  format.
- Layout resolves spacing, fonts and glyph colours (`PositionedGlyph::color`).
  The renderer reads decoration colours from the same `DocStyle`.
- `apply_style` (`app_view.cpp`) reloads fonts when the families change,
  drops height hints and relayouts panes, help and floating windows.

## Not yet

- Per-document or per-buffer sheets (the selection is global).
- Note kinds as roles (`note:warning`), or roles for metadata.
- Explaining *why* a value won (sheet / origin per property).
- Hot reload on file change.
- Per-glyph font fallback: a body face without ☐ / ☑ shows tofu for task
  boxes.
- UI chrome (status line, floating window titles) shares the body font.
