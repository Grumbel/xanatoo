# Semantic sectioning (article / section / header)

Markdown ATX titles become a generic **header** (no h1–h6 rank). Group
related paragraphs with `:::section` or `:::article` so they stay one unit
when moving, outlining, or exporting.

:::article id="overview"
:::header
Why not numbered headings?
:::

HTML ranks (`h1`…`h6`) encode presentation depth in the element name. In a
live outline the depth is better derived from **nesting**: a header inside
two sections is “deeper” than one at the top of an article.

That keeps the document tree aligned with meaning: an article is a
self-contained piece; a section is a thematic block of several paragraphs
(and nested sections) that belong together.

:::section id="model"
:::header
The model
:::

- **Header** — title text for the enclosing section or article (no level field
  required).
- **Section** — groups body blocks; may nest.
- **Article** — a self-contained composition (essay, RFC, help chapter).

Wrap the block under the caret (and following body siblings until the next
header/section boundary) with **Format → Structure → Wrap in Section**
(`Ctrl+Shift+1`) or **Wrap in Article** (`Ctrl+Shift+2`). **Header** is
`Ctrl+1`.

:::section id="example-nested"
:::header
Nested example
:::

Outer section body starts here and stays tied to the header above.

Still the same section — two paragraphs connected by the container, not by
coincidence of sequential headings.

:::section id="inner"
:::header
Inner section
:::

Nested depth increases outline indent without renaming the title element.
:::
:::
:::

## Interop

Plain `# ATX` still parses (as a header with a soft depth hint). Prefer
fences in new writing so articles and sections round-trip losslessly.
