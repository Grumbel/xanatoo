# Experimental minimal XML format

Xanatoo can read, edit and save `.xml` documents as an alternative serialization
of its semantic tree. **This is an experiment, not a decision to adopt XML as
the canonical format.** `.sexp` and Markdown remain available.

Example:

```xml
<doc title="Little experiment" lang="en">
  <section label="intro">
    <heading>Introduction</heading>
    <p>Rock &amp; roll: <em>text</em>, <a href="#intro">links</a>,
      <math src="x^2"/> and <img src="diagram.png" alt="a diagram"/>.</p>
    <ol>
      <li level="1">One</li>
      <li level="2">Two</li>
    </ol>
  </section>
</doc>
```

The parser does **not** implement general XML. It accepts exactly:

- One `<doc>` root and nested tags with matching end tags or `/>`.
- ASCII tag/attribute names (`A-Z`, `a-z`, digits, hyphen, underscore; a
  letter must come first), case-sensitive. No namespace prefixes or `xmlns`.
- Quoted attributes (`"value"` or `'value'`), including whitespace around `=`.
- Ordinary UTF-8 text and the five predefined entities `&amp;`, `&lt;`,
  `&gt;`, `&quot;`, `&apos;`, plus decimal/hex numeric character references
  (`&#10;`, `&#xA;`). Invalid XML characters and UTF-8 are rejected.
- Whitespace between elements in block containers is insignificant. Whitespace
  inside text-bearing tags (`p`, `heading`, `li`, etc.) is content and preserved,
  except for XML's usual raw CR/LF normalization (use `&#13;` to preserve CR).
  Raw tab/CR/LF in attributes normalize to spaces (use numeric references to
  preserve them).

It deliberately **rejects** XML declarations (`<?xml…?>`), other processing
instructions, DTDs, `<!DOCTYPE>`, external/internal entities, namespace tags,
comments, CDATA, unquoted/duplicate attributes, unknown tags/attributes,
unbalanced elements, and nested inline markup. There is no network access,
entity substitution, or script execution. Malformed files produce a read-only
error view in the app instead of an editable replacement for the source.

## Semantic tag vocabulary

| Role | Block tags |
| --- | --- |
| Document and grouping | `doc`, `article`, `section`, `heading`, `h`, `chrome`, `aside` |
| Paragraphs and lists | `p`, `ul`, `ol`, `li`, `quote`, `hr`, `codeblock`, `note` |
| Media and layout | `image`, `figure`, `caption`, `stretch`, `columns`, `column`, `include`, `collection` |
| Definitions and references | `deflist`, `term`, `def`, `biblio`, `ref`, `footnote` |
| Tables | `table`, `tr`, `th`, `td` |
| Discussion | `thread`, `message` |

Inline content within text-bearing blocks: `em`, `strong`, `strike`, `code`,
`a`, `cite`, `fnref`, `img`, `math`, and `text` (when a text span has metadata).
An `<image>` is a block node; `<img>` is an inline object. An `<h level="2">`
is a legacy ranked heading; `<heading>` is the unranked semantic title.
Older Xanatoo documents using `<header>` as a title are still accepted on
import and exported as `<heading>`. The `<header>` name is reserved for future
page-layout semantics, like `<footer>`; it is not a new layout node yet.

Every block may carry `nid` (the source document's numeric node identity),
`label`, `style`, `tags`, `level`, `src`, `alt`, `summary`, `expanded` (`true`
or `false`), `float`, `scale`, `colspan`, and `rowspan`, as appropriate to its
node role. This is intentionally a direct serialization of the existing
`Node` fields, rather than an independent XML document model. Defaults can be
omitted; the exporter omits most default attributes. `<doc>` additionally
has `title`, `lang`, `author`, and may contain one `<front-matter>` text element
to preserve Markdown YAML metadata without losing line breaks.

Inline tags may carry `role="metadata"`, `href`, `rel`, `datetime`, `target`,
`src`, `alt`, `width`, `height`, and `display="true"` as appropriate. `nid`
on blocks and `target` on inline spans preserve internal references even when
node IDs are reassigned during import. These IDs are not stable cross-file
anchors; `label` / `href` remain the human-facing references.

Flat inline spans deliberately cannot nest (for example, `<em>one
<strong>two</strong></em>` is invalid). This mirrors the present document
model; silently flattening nested markup would lose semantics. XML import also
doesn't attempt to read arbitrary SVG, XHTML or generic XML vocabularies.

## Editing and round trips

Open a `.xml` file, or use **File → Export as XML (experimental)…**. Saving an
open `.xml` document writes this format. The direct `parse_xml` and
`serialize_xml` functions are also exposed in `parse.hpp`; both preserve
the semantic tree and metadata (not cosmetic whitespace). Invalid input
throws `std::invalid_argument` with a byte offset. Output is UTF-8 without an
XML declaration, to match the restricted grammar.

No claim of full compatibility with generic XML tools is made. The format
could still be replaced by S-expressions or kept alongside them.
