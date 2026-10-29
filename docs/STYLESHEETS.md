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

  (page (background "#221d18") (color "#e6d9c3") (selection (rgba 90 130 200 110)))

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
`rule`, `transclusion`.

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
| `font` | `body`, `bold`, `italic`, `bold-italic`, `heading`, `code` |
| `color` | text colour (else the page colour) |
| `background` | code-block fill, table header fill, message card fill, band behind a `header` / `thread` title |
| `border` | side bar, rule, table grid, code-block frame, footnote overlay rule, rule under a `header` / `thread` title |

**Inline roles:** `text`, `emphasis`, `strong`, `strike`, `link`, `code`,
`cite`, `footnote-ref`, with the properties `color` (beats the block colour), `background`
(only `code` uses it) and `underline` (`#t` / `#f`).

Unknown roles, properties or bad values become warnings on stderr, and the
status line reports how many there were. The rest of the sheet still applies.

## Site looks

`hackernews.xss`, `reddit.xss` (old.reddit) and `wikipedia.xss` imitate those
sites in broad strokes: Verdana at `body-scale` 0.85 under an orange `thread`
band, blue authors with zebra nesting under a pale blue band, serif titles
over grey `header` rules. They are ordinary sheets — the selection is still
global, not per site.

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
