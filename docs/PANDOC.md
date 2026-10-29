# Pandoc quirks and workarounds

xanatoo imports EPUB, DOCX, ODT, … through pandoc (`import_via_pandoc`,
`src/epub_import.cpp`) into cached GFM Markdown. This file collects the
places where pandoc's output needed help, what we do about it, and whether
it looks worth reporting upstream.

Current call (cwd = the import cache dir):

```
pandoc <file> -f <reader> -t gfm-raw_html+yaml_metadata_block -s \
  -L strip-raw-html.lua -o content.md --extract-media=media
```

followed by `sanitize_epub_markdown` on the result. When the call or the
filter changes, bump `kFormat` so cached imports are redone.

Each entry: symptom, cause, workaround, upstream status. Checked against
pandoc 3.7.0.2 unless noted.

## Raw HTML tag lines in EPUB imports

- **Symptom:** Standard Ebooks show `<hgroup class="has-h2">` /
  `</hgroup>` around chapter titles and `<footer role="presentation">`
  inside letter blockquotes as literal text lines; Gutenberg EPUB3 shows
  an empty `<svg …></svg>` cover wrapper; also `<article>`, `<time>`.
- **Cause:** two things together.
  1. The EPUB reader keeps elements it does not map as
     `RawBlock "html"` (`<hgroup>`, `<footer>`, …). The HTML reader on the
     same XHTML (`pandoc -f html`) maps them to plain blocks and leaves no
     raw HTML, so the two readers disagree.
  2. The `gfm` / `commonmark` writers print `RawBlock "html"` even with
     `-raw_html`; the extension only stops them from rendering Div / Span
     as HTML. (The `markdown` writer fences them as ```` ```{=html} ````
     instead.)
  `-f epub-raw_html` is accepted but changes nothing: the raw blocks are
  still produced.
- **Workaround:** Lua filter `strip-raw-html.lua` (written into the cache
  dir from `kStripRawHtmlLua`) drops every html `RawBlock` / `RawInline`.
  The content of those elements is already separate blocks, so only the
  tag lines go. The svg wrappers are empty (the cover is a separate
  image), so nothing is lost there either.
- **Upstream:** worth reporting, as two issues:
  - EPUB reader: `-raw_html` has no effect, and EPUB vs. HTML reader
    handle `<hgroup>` / `<footer>` differently. Looks like a bug.
  - gfm writer: `-raw_html` does not drop raw HTML blocks. Possibly by
    design, but undocumented. Check the manual's `raw_html` wording first.
  Not filed yet.

## Calibre span / anchor residue

- **Symptom:** empty `<span class="calibreN">` wrappers, `<a id=…></a>`
  page markers, NBSP entities in Calibre-made EPUBs.
- **Workaround:** `sanitize_epub_markdown` (string pass after pandoc,
  also run on cache hits). Probably redundant now that the Lua filter
  drops raw HTML; keep until checked against Calibre books.
- **Upstream:** not a pandoc bug (the markup is in the source).

## Open

- **Escaped HTML in front matter:** `-s` writes the EPUB's
  `dc:description` into the YAML front matter as escaped HTML
  (`\<p\>\<i\>Suspense\</i\> is one of …`). The metadata really holds
  HTML; pandoc treats it as text. Fix would be a filter or post-pass
  that turns the description into plain text.
