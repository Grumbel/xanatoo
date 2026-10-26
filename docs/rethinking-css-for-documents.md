# Rethinking CSS for Documents

*A clean-slate design brief for document markup, typography, layout, and reading. Not an application UI system.*

## 1. Design goal

Design a document format and presentation system from scratch, without backward-compatibility constraints, using the lessons learned from HTML and CSS.

The primary subject is **documents**: articles, books, essays, manuals, reference works, research papers, reports, documentation, and hypertext. The system should make these documents readable and navigable by default, while still allowing authors and publishers to control their appearance when there is a genuine reason to do so.

It should not be a general-purpose application UI toolkit. App widgets, dashboards, arbitrary interface state, and application interaction patterns are out of scope.

The central principle is:

> A document should be well presented and useful before its author writes a stylesheet. Styling should refine a good default, not rescue an ugly or incomplete one.

## 2. Lessons from HTML and CSS

### 2.1 Good defaults are part of the format

HTML's historical default rendering is often treated as a bare fallback rather than a finished reading experience. A clean-slate system should reject that assumption.

A document with a title, headings, paragraphs, lists, links, quotations, figures, tables, and notes should look intentional and readable with no author styling at all.

The built-in presentation should provide:

- A considered typeface stack and sensible font sizes.
- A comfortable measure for body text.
- Good line height, paragraph spacing, and heading hierarchy.
- Clear visual treatment for links, quotations, code, lists, tables, figures, and captions.
- Responsive reflow without horizontal scrolling for ordinary prose.
- Language-aware line breaking and hyphenation where supported.
- Accessible contrast, visible focus, and readable defaults at different zoom levels.
- A coherent screen reading mode and a sensible print/PDF mode.

The default should be a real design system, not an unstyled diagnostic view. It should be attractive but deliberately restrained, so that different content types can share it and readers can override it.

### 2.2 Content and presentation should be separate, but not disconnected

Markup should express the document's structure and meaning. The renderer should use that meaning to choose a good presentation.

Authors should not need to add classes solely to make a chapter title look different from a section title, or to make a figure caption recognizable as a caption.

### 2.3 The document is not just a tree of boxes

Documents contain flowing text, references, notes, figures, tables, captions, citations, indexes, and other relationships. These should not be reduced to arbitrary rectangles whose behavior emerges from unrelated layout rules.

The system needs a shared document model that understands both hierarchy and cross-references.

### 2.4 Reader preferences are first-class

The author does not own the reader's display, font preferences, zoom, contrast needs, preferred line length, or reading mode.

Reader overrides should be supported by design and should not require fighting highly specific author rules.

### 2.5 Layout behavior should be explainable

It should be possible to discover why an element has a particular size, why a figure moved, why a page broke, or which style won. Computed styles and layout decisions should have inspectable explanations.

## 3. Markup: structure, semantics, and relationships

### 3.1 Use meaningful document elements

The markup vocabulary should directly represent common document concepts, such as:

- Document title, subtitle, author, publication metadata.
- Chapters, sections, and subsections.
- Paragraphs and inline emphasis.
- Ordered and unordered lists.
- Quotations, verse, and code.
- Figures, images, captions, and legends.
- Tables with captions, headers, row groups, and cell relationships.
- Footnotes, endnotes, citations, references, and bibliographies.
- Equations, definitions, examples, warnings, and other domain-specific blocks.
- Cross-references, indexes, glossaries, and appendices.

These are document concepts, not presentation classes. The markup should preserve their meaning even if the renderer changes their visual appearance.

### 3.2 Keep hierarchy semantic

A chapter heading and a section heading are not merely two arbitrary font sizes. They occupy different roles in the document hierarchy. The renderer should know their relationship and be able to style them accordingly.

Likewise, a caption should be associated with its figure, a table header with the cells it labels, and a footnote with its reference point.

### 3.3 Allow extensible semantic roles

No fixed vocabulary will cover every field. The system should allow domain-specific elements or roles to participate in standard presentation rules.

A stylesheet should be able to target a semantic role such as `figure-caption`, `bibliography-entry`, or `warning` without depending on the exact nesting or incidental markup used by one author.

An element may have a basic structural type and an additional role. For example, a paragraph inside a warning is still a paragraph, but also participates in a warning context.

### 3.4 Structure should not depend on visual styling

Reading order, heading hierarchy, reference relationships, and associations between content and captions must remain meaningful when visual styling is disabled or changed. CSS-like layout rules must not be the only place where these relationships exist.

## 4. A strong built-in reading experience

The system should ship with a complete, coherent default theme.

### 4.1 Sensible typography

Defaults should include a readable body type size, comfortable line height, restrained paragraph spacing, a clear heading scale, good list indentation, and appropriate treatments for quotations, code, links, and captions.

The exact values may vary by renderer, language, and output medium, but the design goals should be specified. A browser should not need a site-specific stylesheet to make a long article comfortable to read.

### 4.2 Responsive by intrinsic layout

Ordinary text should naturally reflow as the available space changes. The renderer should prefer content-aware constraints over a collection of device-specific breakpoints.

Examples:

- Body text has a preferred readable measure and can shrink on narrow displays.
- Figures scale to fit their containing text measure while respecting their intrinsic proportions.
- Tables use readable strategies for narrow spaces rather than forcing the entire document to overflow.
- Multiple columns appear only where the content and available space justify them.
- Side notes can become inline notes or overlays when there is insufficient room.

Explicit fixed layouts should remain possible for special cases, but should not be required for normal documents.

### 4.3 Consistency across renderers

Continuous screen reading, paged screen reading, ebooks, print, and PDF should share the same semantic document and as much of the same layout model as possible. Their presentations may differ, but content relationships should not.

## 5. Document navigation should work automatically

A document is more than a stream of visible text. It has navigational structure and auxiliary information that readers should be able to access without each site implementing its own scripts and controls.

### 5.1 Automatic table of contents

The renderer should be able to construct a table of contents from the document's heading hierarchy.

Expected behavior:

- It derives entries from semantic headings and their nesting.
- It links to the corresponding sections.
- It updates when the document structure changes.
- It can be opened as a built-in navigation panel or overlay.
- The author can specify which heading levels to include and whether a visible inline TOC should also appear.
- It remains available even if the author does not manually write a TOC.

A document may supply a custom title or curated navigation structure when necessary, but ordinary documents should not need a hand-maintained list of headings.

### 5.2 Footnotes and endnotes as built-in behavior

Footnotes should be a native document concept, not a pattern assembled from superscript links, hidden elements, and JavaScript.

The renderer should automatically provide:

- A reference marker at the point of use.
- A clear association between reference and note.
- A way to inspect the note without losing the reader's place.
- A return link or equivalent navigation back to the reference.
- Keyboard and assistive-technology support.
- A suitable print/paged presentation, such as page-bottom footnotes when requested.
- A sensible fallback when overlays are unavailable or inappropriate.

On a screen, activating a footnote can open it in a small overlay, popover, or side panel. The exact behavior should be native to the reading environment, not a per-site script. On paper, the same note can be placed at the bottom of the page or in an endnotes section according to document policy.

The overlay is a presentation of the note, not a separate copy of its content. There must be one source of truth.

### 5.3 Other generated navigation

The same principle should apply where useful to:

- Backlinks from notes to references.
- Bibliography and citation navigation.
- Glossary definitions.
- Index entries and their targets.
- Cross-reference previews.
- Chapter and section navigation.
- Figure and table lists.

Not every document needs every feature. The renderer should generate what can be derived reliably, and allow the author to curate or override it when editorial judgment is needed.

### 5.4 Overlays are a reading primitive

A built-in overlay is not the same thing as an application modal. It is a way to reveal associated document material while preserving reading context.

Examples include footnotes, glossary definitions, citation details, image enlargement, and TOC navigation. The renderer should provide consistent behavior, focus handling, dismissal, keyboard access, and fallback behavior.

Overlays must not be the only way to access the content. Every note, definition, and reference must remain reachable through ordinary document navigation and in non-overlay output modes.

## 6. Text flow and composition

The layout model should start with flowing text, not with a universal box model.

### 6.1 First-class flow concepts

The system should explicitly model:

- Paragraph flow and inline content.
- Block-level content and spacing.
- Lists and nested lists.
- Figures and captions.
- Tables and table headers.
- Side notes, footnotes, and marginalia.
- Multi-column text regions.
- Page fragmentation and running page furniture.

These concepts should compose under shared rules. For example, a figure's placement should take account of surrounding text, available space, caption attachment, and page boundaries.

### 6.2 Constraint-based placement

Authors should express intent and limits rather than writing a recipe for every screen size.

A figure might prefer to sit beside a paragraph when enough width exists, but become full-width when it does not. A text column might have a preferred measure and a minimum readable width. A sidebar might move below its associated section on narrow screens.

The engine should choose a feasible composition from these constraints. Authors should be able to inspect why a fallback occurred.

### 6.3 Avoid making absolute positioning the normal case

Absolute positioning is useful for exceptional layouts, overlays, diagrams, and precise page composition. It is a poor foundation for flowing prose because it disconnects objects from the natural movement of text.

Normal document layout should remain robust when fonts change, text is enlarged, content is translated, or the viewport changes.

## 7. Pagination is part of the core model

Pagination should not be an afterthought bolted onto a screen-first renderer.

The system should support page size and margins, page numbers, running headers and footers, recto/verso chapter starts, footnotes, page breaks, multi-column sections, and repeated table headers.

It should also understand relationships that affect page composition:

- Keep a heading with the following content.
- Avoid widows and orphans.
- Keep a figure and its caption together.
- Prefer placing a figure near its reference.
- Split long tables at sensible row boundaries.
- Repeat table headers across pages.
- Place footnotes appropriately without producing pathological page breaks.

Pagination is a global composition problem. Moving a figure or expanding a footnote can affect later pages. The layout engine should be able to reconsider affected regions and resolve conflicts predictably.

Screen and print modes should use the same document structure, while being allowed to make different composition choices.

## 8. A more coherent styling language

The styling language should be declarative, modular, and designed around document concepts.

### 8.1 Style semantic roles first

Most document themes should be expressible through named roles such as `body-text`, `chapter-heading`, `figure-caption`, `footnote`, and `bibliography-entry`.

Structural selectors should remain available for advanced cases, but authors should not need elaborate selectors merely to distinguish normal document roles.

### 8.2 Make precedence predictable

Keep the useful idea of layered styling, but define precedence explicitly. A possible order is:

1. Reader accessibility requirements and explicit overrides.
2. Reader preferences.
3. Document-specific styles.
4. Publisher theme.
5. Built-in defaults.

The exact policy needs careful design, especially for user agents and accessibility. The important point is that a publisher should not be able to defeat reader control through selector specificity or `!important` escalation.

### 8.3 Prefer named tokens and reusable definitions

Themes should define reusable type scales, spacing scales, page templates, and role styles. A theme should be able to override a few named definitions without copying a large stylesheet.

Style modules should state what they provide and depend on. Reuse should depend on semantic roles, not accidental markup structure.

### 8.4 Explain computed results

The renderer should expose the origin of computed styles and layout decisions. It should be possible to answer:

- Which rule chose this typeface?
- Why is this line measure constrained?
- Why did this figure move?
- Why did the table overflow or switch presentation?
- Which reader preference overrode the publisher's value?

### 8.5 Use units with clear meanings

Provide coherent ways to express text-relative sizes, text measure, container-relative dimensions, physical print dimensions, and intrinsic content size.

Percentages should have an unambiguous reference quantity. Values should be typed so that dimensional mistakes are caught rather than silently accepted.

A first-class text measure is particularly useful: the readable width of a text column is often more meaningful than the width of the viewport.

## 9. Typography is a layout subsystem

Typography determines the shape and pagination of a document. It should not be reduced to a font-family property and a line-height value.

The system should account for:

- Language-sensitive line breaking and hyphenation.
- Unicode scripts and writing directions.
- Font fallback and missing glyphs.
- Variable fonts and optical sizing.
- Punctuation hanging and justification.
- Paragraph indentation and spacing.
- Widow, orphan, and heading-break policies.
- Mathematical notation and mixed inline content.
- Ruby annotations and other script-specific features.

Semantic roles and a coherent type scale should make consistent typography easy. Reader text-size changes should adjust the composition sensibly rather than requiring the author to tune every heading and margin separately.

## 10. Accessibility and reader control

Accessibility is a property of the document model and renderer, not a corrective stylesheet.

The system should preserve semantic reading order and reference relationships across visual layouts. It should support reader-selected fonts, text sizes, line spacing, line length, contrast, forced colors, reduced motion, zoom, and reflow.

A publisher can express a preferred presentation, but should not be able to make content inaccessible by relying on fixed dimensions or by making meaning depend only on visual placement.

Features such as footnotes, TOC overlays, popovers, and image enlargement must work with keyboards and assistive technologies, and must have a non-overlay fallback.

## 11. Multiple output modes from one source

The same document should be renderable as:

- Continuous screen reading.
- Paged screen reading.
- Reflowable ebook.
- Print and PDF.
- Alternative accessible reading interfaces.

These are different presentations of the same semantic content, not separate documents that merely share text.

The renderer should preserve relationships and navigation across output modes. It should report unsupported features rather than silently dropping important content.

## 12. What should be built in, and what should authors provide?

A useful boundary is:

### The format and renderer should provide

- A polished, readable default theme.
- Semantic rendering of common document elements.
- Responsive text flow and intrinsic sizing.
- Heading hierarchy and generated TOC navigation.
- Native footnote and endnote relationships.
- Cross-reference navigation and return links.
- Standard overlays for notes, definitions, and similar auxiliary content.
- Basic page layout, fragmentation, and print output.
- Accessibility, keyboard behavior, and reader preferences.
- Sensible image, table, list, quotation, and code presentation.
- A coherent typography system.

### The author should provide

- The actual content and its semantic structure.
- Meaningful headings, captions, labels, and reference relationships.
- Alternative text and other content-specific accessibility information.
- Editorial choices that cannot be inferred reliably, such as a curated TOC, unusual note policy, or specialized page sequence.
- Optional visual identity and deliberate departures from defaults.

### The publisher or theme should provide

- Branding and visual identity.
- A typeface and color palette where appropriate.
- A house style for headings, figures, tables, and citations.
- Page templates and print conventions.
- Optional layouts for particular document genres.

The principle is **derive what can be derived, default what can be defaulted, and ask the author only for information that requires editorial judgment**.

A renderer should not require an author to write a script to get a TOC, make footnotes usable, link citations, or make a basic document readable.

## 13. Deliberately out of scope

This design is not intended to be a universal styling system for application interfaces.

It should not be organized around app widgets, dashboards, arbitrary interactive state, or animation-heavy UI behavior. General-purpose scripting should not be part of the core styling language.

Advanced layout remains necessary for unusual documents, but the ordinary case should be handled by semantic structure, flowing text, constraints, and built-in document behavior.

## 14. Summary of design principles

1. **Good by default:** unstyled documents should already be readable and visually coherent.
2. **Semantics first:** markup describes meaning and relationships, not merely boxes and classes.
3. **Documents are more than box trees:** text flow, figures, tables, notes, and references belong to a shared model.
4. **Navigation is built in:** TOCs, footnote access, backlinks, and cross-reference navigation should work without site-specific scripting.
5. **Overlays are native reading behavior:** associated material can be inspected without losing context, but remains accessible without overlays.
6. **Constraints over breakpoint recipes:** authors express intent and limits; the engine finds a feasible layout.
7. **Pagination from the start:** print and paged reading are first-class modes.
8. **Predictable styling:** semantic roles, explicit layers, reusable tokens, and explainable computed results.
9. **Reader control:** preferences and accessibility are fundamental, not optional patches.
10. **One source, multiple presentations:** screen, ebook, print, and accessible reading modes share one semantic document.
11. **Derive before asking:** the renderer should generate navigation and behavior wherever the document structure provides enough information.
12. **Document-first scope:** do not dilute the design by trying to solve application UI layout in the same system.

The goal is not simply a cleaner CSS syntax. It is a document format and rendering model in which good typography, navigation, reading behavior, and output flexibility are inherent capabilities rather than a pile of optional conventions.
