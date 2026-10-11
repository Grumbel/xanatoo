# xanatoo semantic document format (S-expression)

This is the **canonical** digital form for xanatoo: a tree of *meaning*, not a
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
| `(thread …)` | Discussion container (mailing list / forum thread) |
| `(message …)` | One post or comment; may reply to another message |
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

## Threads and messages

Mailing-list and forum discussions are first-class structure, not free text.

```lisp
(thread :id "t-rfc" :subject "RFC: widget API"
  (message :id "m1" :from "Alice <alice@ex.org>" :date "2026-03-01T10:00:00Z"
    (p "Proposal…"))
  (message :id "m2" :from "Bob <bob@ex.org>" :date "2026-03-01T11:00:00Z"
           :in-reply-to "m1"
    (p "Counter-proposal…")))
```

Markdown fences:

```
:::thread id="t-rfc" subject="RFC: widget API"
:::message id="m1" from="Alice <alice@ex.org>" date="2026-03-01T10:00:00Z"
Proposal…

:::
:::message id="m2" from="Bob <bob@ex.org>" date="2026-03-01T11:00:00Z" in-reply-to="m1"
Counter-proposal…

:::
:::
```

| Field | Node storage | Meaning |
|-------|--------------|---------|
| `id` / `:id` | `label` | Stable message-id (anchor `#m1`) |
| `from` / `:from` | `src` | Author (display string or email) |
| `date` / `:date` | `summary` | Timestamp (ISO-8601 preferred) |
| `in-reply-to` | `alt` | Parent message-id (threading) |
| `about` | `alt` = `about:#…` | Comment on a document passage |
| `subject` (thread) | `summary` | Thread title |

`:::comment` is an alias for `:::message`. Depth in the layout is derived by walking the `in-reply-to` chain within the document.

## Article, section, and header

Prefer HTML-like sectioning over ranked `h1`–`h6`:

| Node | Role |
|------|------|
| `header` | Title for the enclosing section/article (no rank) |
| `section` | Thematic group of blocks; may nest |
| `article` | Self-contained composition |

Markdown:

```
:::article
:::header
Title
:::

Body paragraph…

:::section
:::header
Subsection
:::

More body…
:::
:::
```

S-expression: `(article (header "Title") (p "…") (section (header "…") …))`.

Outline depth is the nesting count of `section`/`article` ancestors (legacy
ATX `#` headers may still carry a soft depth hint). Commands: **Header**
(`Ctrl+1`), **Wrap in Section** (`Ctrl+Shift+1`), **Wrap in Article**
(`Ctrl+Shift+2`), **Unwrap Section/Article**.
