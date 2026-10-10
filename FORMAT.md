# glyphtoo semantic document format (S-expression)

This is the **canonical** digital form for glyphtoo: a tree of *meaning*, not a
page description. Layout (columns, fonts, margins) is a view over this tree and
must not be required to understand or exchange a document.

Markdown remains a convenience import/export. Prefer `.sexp` when structure and
stable identity matter.

## Design principles

1. **Semantics over presentation** — nodes name roles (*heading*, *note*,
   *code*, *stretch*), not pixels or CSS.
2. **Stable identity** — every node may carry `:id` (session-stable integer) and
   optionally `:label` (human-stable symbol for links).
3. **Transclusion first** — references (`include`, links) point at labels or
   paths; the reader resolves them.
4. **Round-trippable** — parse → edit → serialize should preserve structure and
   labels; cosmetic whitespace may change.
5. **Layout-agnostic** — no absolute coordinates, no font sizes, no column
   widths *as content*. Column *structure* (`columns` / `column`) is optional
   semantic grouping (“these passages are alternatives / parallel”), not a
   print specification.

## Document root

```lisp
(doc
  :title "Living hypertext"
  :lang "en"
  :author "…"
  …body…)
```

| Keyword   | Meaning                          |
|-----------|----------------------------------|
| `:title`  | Document title (metadata)        |
| `:lang`   | Primary language (BCP 47)        |
| `:author` | Authorship string                |

## Blocks

| Form | Role |
|------|------|
| `(h N …)` | Heading level 1–6 |
| `(p …)` | Paragraph |
| `(quote …)` | Quotation (attributed speech / excerpt) |
| `(note …)` | Authorial aside / callout (semantic, not a sidebar region) |
| `(code :lang "cpp" "…")` | Preformatted code; `:lang` is a language tag |
| `(hr)` | Thematic break |
| `(ul …)` / `(ol …)` + `(li …)` | Lists |
| `(stretch :summary "…" …)` | Progressive disclosure |
| `(columns (column …) …)` | Parallel / multi-track structure |
| `(include "#label")` | Transclusion of a labelled passage |
| `(img "path" :alt "…")` | Media with accessible alternative |
| `(deflist (term …) (def …) …)` | Definition list (glossary-style) |
| `(figure (img …) (caption …))` | Media + caption as one unit |
| `(biblio (ref :key "…" …) …)` | Bibliography; keys match citations |
| `(include "#lab" :excerpt "…")` | Transclude a *range* (phrase match) inside a labelled block |
| `(table (tr (th…) (td…)) …)` | Semantic grid; cells are nodes, not CSS |

Inline: plain strings, `(em …)`, `(strong …)`, `(code …)`,
`(a "href" …)`, `(a "href" :rel "glossary" …)`,
`(cite :key "nelson65" "Nelson 1965")`.

### Typed links and citations

- `:rel` on `(a …)` names the *relationship* (e.g. `glossary`, `source`,
  `contra`) without prescribing presentation.
- `(cite :key "…")` points at a `(ref :key "…")` in `(biblio …)`.
  Clicking a citation jumps to that entry (`#key` resolution).

## Identity and linking

```lisp
(h 2 :label "stretchtext" "Stretchtext")
(p "See " (a "#stretchtext" "stretchtext") ".")
```

- `#label` resolves to the node whose `:label` matches (preferred).
- Fallback: heading text slug / title match (legacy markup behaviour).
- `:id` is emitted on serialize for tooling; do not hand-edit as the sole key.

## Deliberately out of band (layout / chrome)

Not part of the semantic document:

- Window size, scroll position, caret, selection
- Zoom level, theme, font choice
- Pane split ratio, diff marks
- Exact column pixel widths (optional *weights* may appear later as soft hints)

## Planned (not all implemented)

- **Richer table presentation** — column weights, row/col spans (still semantic)
- **i18n variants** — alternate `lang` branches for the same `:label`
- **Byte/character-accurate ranges** — when phrase excerpt is not enough

## Example

See `examples/semantic.sexp`.
