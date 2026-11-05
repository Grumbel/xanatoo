# Layout features from books and scholarly PDFs

> Research inventory, 2026-11-05. **Not an implemented PDF importer or new
> markup specification.** Candidate sources are linked rather than copied;
> inspect individual scans and their rights before downloading or using them
> with an AI service. Machine-readable list: [`research/pdf-layout-corpus.json`](../research/pdf-layout-corpus.json).
>
> Related: [FORMAT.md](../FORMAT.md), [semantic markup](SEMANTIC_MARKUP.md),
> [passage identity](PASSAGES.md), [paged reading](PAGED.md),
> [document styles](STYLESHEETS.md).

## Goal

Take an arbitrary book or paper PDF and generate a **readable, editable,
reflowable Xanatoo document**, with its figures, math, references, document
structure, and reading order intact. The original PDF remains an immutable
source and an optional facsimile view, not the template for screen coordinates.

The useful question is *what does this arrangement mean?* A caption belongs
to a figure; the formula numbered (3) is referred to elsewhere; two columns
may be either continuous typesetting **or** two aligned translations. A
successful conversion distinguishes these cases rather than copying glyph
positions into Markdown.

Three distinct things must survive conversion:

1. **Semantic structure:** paragraphs, sections, figures, citations,
   evidence, examples, dialogue, theorem/proof relationships, etc. This is
   the canonical Xanatoo tree, expressible in `.sexp` and eventually in its
   Markdown extension syntax.
2. **Reader-layout hints:** suggested figure placement, panel grouping,
   parallel versus continuous flow, long-line poetry, optional break-before,
   table headers. These are not hard-coded font sizes or absolute positions.
3. **Source provenance:** printed page label, PDF page index, region, source
   URI/hash, extraction method and uncertainty. These let a reader compare
   against the scan and let a human correct mistakes. They are *not* stable
   passage addresses, which must be derived from document identity/labels.

## First corpus: deliberately difficult documents

Open the linked catalog item first, then use its downloadable PDF or page
images. A catalog link is intentional: Archive.org may offer a different
scanned edition, a searchable OCR file, or no unrestricted PDF download.
The last two article entries supply structured XML unavailable for most
historical scans. Later magazine additions are exploratory references, not
necessarily cleared for automated ingestion.

| ID / source | What to inspect | Why it is difficult |
|---|---|---|
| **byrne-euclid** — [Oliver Byrne, *Euclid* (1847)](https://archive.org/details/firstsixbooksofe00eucl) | Definitions and propositions, coloured geometry inserted in the argument | **Meaning carried by coloured diagrams and symbols**, not OCR-able prose alone; proof structure and figure references |
| **maxwell-electricity-v1** — [Maxwell, *A Treatise on Electricity and Magnetism*, vol. 1 (1873)](https://archive.org/details/electricandmagne01maxwrich) | Numbered paragraphs, displayed multi-line formulas, diagrams, references | Mathematical layout, equations versus ordinary numbers, cross-references |
| **gray-anatomy** — [Gray, *Anatomy of the Human Body* (1918)](https://archive.org/details/anatomyofhumanbo00grayrich) | Anatomy diagrams, labels, terminology and index | Illustration callouts, caption linkage, many reference targets, complex hierarchy; very large scan, use excerpts |
| **century-dictionary** — [*The Century Dictionary* (1889–91), part 1](https://archive.org/details/centurydictipt100whituoft) | Headwords, pronunciation, etymology, numbered senses, usage examples | Dictionary *entries* rather than paragraphs; crossrefs and multi-column continuous flow |
| **chicago-manual** — [*A Manual of Style* (1906)](https://archive.org/details/manualofstylebei00univrich) | Editorial examples, typographic samples, lists, tables, index | Examples whose typography is itself the subject, nested lists and back matter |
| **scientific-american-1906** — [*Scientific American*, 14 July 1906](https://archive.org/details/scientific-american-1906-07-14) | Multiple articles, columns, illustrations, advertisements | **Multiple separate reading orders on one page**; page composition is not one article and not parallel-text columns |
| **popular-magazine-1920** — [*The Popular Magazine*, 7 June 1920](https://archive.org/details/the-popular-magazine-v-056-n-04-1920-06-07) | Pulp fiction, covers, display headlines, serials, advertising | Feature openings, illustration/text alternation, editorial versus commercial content; historical public-domain candidate |
| **dada-1917** — [*Dada* no. 1 (1917), University of Iowa](https://sdrc.lib.uiowa.edu/dada/dada/1/index.htm) | Avant-garde magazine pages, typography and image relationships | Design can be part of the work; page-image source, not necessarily a downloadable PDF |
| **391-1920** — [*391*, no. 13 (1920), Wikimedia Commons](https://commons.wikimedia.org/wiki/File:391_magazine_Volume_13.pdf) | Experimental typographic composition and juxtaposition | Historically avant-garde page design with an identified PDF and public-domain declaration |
| **mondo-2000-01** — [*Mondo 2000* no. 1 (1989)](https://archive.org/details/Mondo.2000.Issue.01.1989) | Early magazine structure, interviews, reviews and artwork | Earlier baseline for comparison with later issues; **copyrighted, reference only unless cleared** |
| **mondo-2000-04** — [*Mondo 2000* no. 4 (1991)](https://archive.org/details/Mondo.2000.Issue.04.1991) | Digital collage, experimental feature design, pull quotes, article opening spreads | A severe reading-order and text/graphic-boundary stress test; **copyrighted, reference only unless cleared** |
| **emigre-11** — [*Emigre* no. 11 (1989), Letterform Archive](https://letterformarchive.org/news/emigre-issue-11/) | Typography as subject, digital display types, unusual placement | Can the extraction retain information where type design itself is content? **Reference only; viewing access is not an AI licence** |
| **whitman-leaves** — [Whitman, *Leaves of Grass* (1891–92)](https://archive.org/details/leavesofgrassinc00whitrich) | Long verse lines, stanzas, indentation, poems and sequence | Line boundaries are meaningful; poem versus prose and deliberate whitespace |
| **shakespeare-folio** — [Shakespeare, *First Folio* (1623), Library of Congress](https://www.loc.gov/item/2021666879/) | Act/scene divisions, speech labels, stage directions | Speaker/speech/aside structure; historical orthography and challenging OCR |
| **aucassin-nicolette** — [*Aucassin & Nicolette* (1887)](https://archive.org/details/aucassinnicolett00bouruoft) | Verse/prose transitions, notes, glossary, bibliography | Mixed literary forms and extensive scholarly back matter; confirm the particular edition's layout |
| **berghaus-atlas** — [Berghaus, *The Physical Atlas* (1845), Wellcome](https://wellcomecollection.org/works/qukcpt3s) | Maps, legend and associated notes | Labeled map regions, figure legends, plates that should be zoomable, not forced into text |
| **jats-bmj** — [JATS sample BMJ article (PDF + XML)](https://jats.nlm.nih.gov/publishing/tag-library/1.2/chapter/samples.html) | Title block, affiliations, structured sections and references | Paired source structure to score PDF extraction against; **licence/redistribution review required** |
| **jats-pnas** — [JATS sample PNAS article (PDF + XML)](https://jats.nlm.nih.gov/publishing/tag-library/1.2/chapter/samples.html) | Scientific figures, equations, tables, references | A second paired gold-standard example; **licence/redistribution review required** |

For a machine-readable starting point, the companion JSON contains stable
catalog URLs, high-level feature tags, and rights caveats. It does **not**
contain PDFs, OCR, scraped full text or claimed ground-truth annotations.

### Rights and provenance

- Many nineteenth-century scans are **US public-domain candidates**, but
  jurisdiction, edition, illustrations, annotations and scan-provider terms
  still need checking. The first prototype should be a local conversion of
  specific, verified public-domain copies; no automatic bulk upload to a
  third-party AI service.
- Modern research articles require per-document licence checks. Public
  access on PMC or arXiv **does not mean** permission for every kind of
  redistribution or AI use. PMC's Open Access Subset records individual
  licences; JATS sample files are for format study pending review.
- **Mondo 2000 and Emigre are modern copyrighted magazines.** Their scans
  and online reading archives are valuable *visual design references*, not
  an assumed licence to bulk download, train a model, publish derived OCR,
  or upload entire issues to an AI service. Keep them reference-only until
  a suitable use and permission are established. Start automated benchmark
  fixtures with vetted public-domain magazines (Scientific American, Dada,
  391, The Popular Magazine).
- Explicit counterexample: **OpenStax Astronomy 2e** is freely readable,
  but its current site terms explicitly restrict ingestion by generative AI
  without permission. Do **not** add it to an automated AI-ingestion set.
- Save the *exact edition*, item URL, PDF byte hash, capture date and licence
  evidence alongside every future manually annotated fixture. Scan page
  index (zero-based), original printed page label and logical page sequence
  are three different coordinates.

## Magazines as a separate problem: *Mondo 2000*

Magazine composition deserves its own benchmark rather than treating it as
another book layout. For example, [*Mondo 2000* issue 4 (1991)](https://archive.org/details/Mondo.2000.Issue.04.1991)
is a particularly interesting case: the magazine's digital collages,
unconventional typography and experimental layouts are documented in a
[retrospective on its design](https://www.wired.com/2015/07/tech-time-warp-week-wired-eccentric-mondo-2000/).
The Archive.org item identifies 164 scanned pages and offers PDF,
PDF-with-text, OCR and hOCR exports; these are potential *inputs* to a
reading-order investigation, not evidence that the resulting text is good.
Issue [1 (1989)](https://archive.org/details/Mondo.2000.Issue.01.1989)
provides a useful earlier comparison. The [Anarchivism issue list](https://anarchivism.org/w/Mondo_2000)
links scans across the run.

| Magazine composition | Recovery question | Desired semantic result |
|---|---|---|
| Feature opening spread | Is large lettering a title, caption, decorative art, or all three? | One article with a proper title; preserve stylized title art separately when significant |
| Several irregular text blocks | Which fragments form a continuous story, and in what order? | Explicit ordered article flow, not global left-to-right/top-to-bottom reading |
| Interleaved sidebar or fact box | Does it interrupt the main argument or offer optional context? | Distinct sidebar passage with an `about`/`related` link to its anchor |
| Pull quote | Is this new text, or a styled repeat of a sentence elsewhere? | A reference to/excerpt of the original passage; avoid duplicating narrative content |
| Photographic collage and overlays | Are words part of a diagram, illustration, or readable article prose? | Figure/plate with text annotations and source regions where warranted |
| Ads, product blurbs, and reviews | Do these belong to the adjacent feature? | Independent labelled items with explicit commercial/editorial role |
| Split article across spreads | Which story continues on a later page? | Continuous semantic article with source page segments retained as provenance |
| Rotated/angled text | Does orientation signal emphasis, a separate content path, or a purely graphical motif? | Typed text if meaningful; retain a facsimile crop if flattening erases meaning |
| Contents, folios and running headers | Which strings navigate, and which are page furniture? | Article/issue links; print pagination only as source location |

This exposes a limitation of the phrase **"same document in Markdown"**:
an aggressively art-directed spread is sometimes itself an image/composition.
No semantic text tree can reproduce *every* intentional interaction of ink,
font, and collage. There should be two complementary outputs:

1. **Semantic reader edition:** articles, headings, sidebars, interviews,
   figures and linked excerpts; excellent search, accessibility and reflow.
2. **Facsimile/graphic edition:** an original or rights-cleared page/spread
   image with text/region links into the semantic edition. This preserves
   artwork and graphic relationships that should not be faked with CSS or
   discarded by OCR.

A later hybrid reader could show the source spread with selectable semantic
overlays, or switch a passage from designed-facsimile to reflow. The markup
should identify *what belongs together*; the facsimile/source map can retain
where the designer placed it. In particular, avoid treating every PDF text
box as a new authored paragraph or assuming the magazine has one universal
reading sequence.

**Minimum magazine test cases** after selecting exact pages: one ordinary
multi-column article, an unusual article opener, an interleaved sidebar, a
collage with text, a pull quote, a separate advertisement, and a feature
continued on another spread. First establish these against public-domain
magazines; compare *Mondo* and *Emigre* visually under their access terms.

## Feature inventory and the mapping to Xanatoo

Legend: **Now** = implemented representation according to `FORMAT.md` and
`examples/tour.md`; **Partial** = a useful fallback exists but loses meaning;
**New** = proposal only. A *rendering* limitation is distinct from a *markup*
limitation; no claim here that existing nodes reproduce print typesetting.

| Feature family | What a PDF conversion must recover | Xanatoo status / direction | Example |
|---|---|---|---|
| Titles, parts, chapters, sections, appendices | Hierarchy, title vs running head, stable targets | **Now:** document metadata, `section`/`article`/`header`; explicit part and appendix roles **New** | Gray, Maxwell |
| Front matter | Title, authors, affiliations, abstract, keywords, edition/DOI, epigraph, dedication | **Partial:** YAML metadata, headers, paragraphs; typed academic front matter **New** | JATS articles |
| Body prose, lists, quotations | Paragraph boundaries, hierarchy, quotation/source | **Now:** `p`, `ul`/`ol`, `quote`, `note`, `deflist` | Chicago manual |
| Book and paper citations | Linked inline citations, distinct bibliography entries, resolvable IDs | **Now:** `cite`, `biblio`, `ref`; DOI/ISBN metadata, named citation styles **New** | JATS, Maxwell |
| Footnotes, endnotes, table notes | Mark to note relationship, backlinks, scope, numbering | **Now:** footnotes; typed endnotes/table footnotes **New** | Chicago, PNAS |
| Figures and plates | Caption, label, subfigure group, figure references, alt text, source | **Now:** `figure`/`img`/`caption`; multi-panel groups and embedded diagram labels **New** | Byrne, Gray |
| Tables | Headers, row/column grouping, spanned cells, footers, units | **Partial:** semantic table with headers; cell spans, nested headers, tables split across pages **New** | PNAS, Chicago |
| Mathematics | Inline/display separation, symbol order, alignment, equation label | **Partial:** TeX math atom; aligned equation groups, equation numbering and reference targets **New** | Maxwell, PNAS |
| Formal science objects | Theorem, proof, lemma, definition, experiment, algorithm, numbered example | **Partial:** generic section/note; typed block role, numbering, mutual links **New** | Byrne, PNAS |
| Verse, drama, letters | Verse line and stanza boundaries, speaker, stage direction, attribution | **Partial:** paragraphs and code can preserve lines but not meaning; `verse`/`stanza`/`speech` **New** | Whitman, Shakespeare |
| Dictionary and glossary | Headword, language, pronunciation, senses, etymology, crossrefs | **Partial:** `deflist`/linked labels; rich lexical entry/inline phonetics **New** | Century Dictionary |
| Parallel text and annotation | Distinguish aligned translations, commentary, side notes from page columns | **Partial:** `columns` expresses parallel blocks; synchronized alignment and marginal note anchor **New** | Annotated editions |
| Magazine/newspaper | Article boundaries, continuation links, pull-quote identity, optional sidebars, ads and artwork distinct from text flow | **Now:** explicit `article`, `section`, `note`; independent reading flows, sidebar-to-anchor links, and visual/semantic cross-maps **New** | Scientific American, Mondo 2000 |
| Back matter and navigation | TOC, index with page/passage refs, glossary, appendix, errata | **Partial:** sections and links; semantic index terms and generated index/TOC **New** | Chicago, Gray |
| Nonlinear media and maps | Plate/region references, legends, legends linked to geometry | **Partial:** figure/image/collection; image-region anchors and diagram text/colour **New** | Berghaus, Byrne |
| Print-only mechanics | Running head, folio/page label, ornaments, crop marks, hyphenation | **Source provenance / view metadata**, *not* semantic body text | All scanned books |

Two misleading equivalences to avoid:

1. **Two newspaper columns are one text flow.** `:::columns` in Xanatoo
   currently means multiple *parallel* blocks that can stack in a narrow
   view. It is not a request to mimic PDF typesetting. Ingest a newspaper
   article as ordered blocks; let the reader reflow it.
2. **A page-number reference is not a link to the content.** Preserve the
   source folio for comparison, but map `Fig. 3`, `§12`, `ibid.` or an index
   entry to a stable labelled passage wherever it is unambiguous. Keep an
   unresolved source reference explicitly unresolved rather than guessing.

### Markdown extension sketches — **not parsed yet**

Ordinary Markdown cannot represent all of this, but a small fenced-block
vocabulary could. Prefer typed roles on existing structural primitives and
preserve unknown roles without data loss. These are **proposals, not usage
instructions**:

````markdown
:::statement kind=theorem label=thm-pythagoras number="I.47"
The square on the hypotenuse equals the sum of the other two squares.
:::

:::statement kind=proof of=thm-pythagoras
...proof text and labelled diagrams...
:::

:::equation label=eq-gauss number="3" align=equals
$$\nabla\cdot\mathbf{E}=\rho/\epsilon_0$$
:::

:::verse label=poem-1
:::stanza
First line as a *verse line*
Second line, even if it wraps in the reader
:::
:::

:::table label=table-1
:::caption
Results by treatment group.
:::
...cells with future rowspan/colspan and header associations...
:::

:::annotation target=paragraph-12 kind=marginal-note
Editorial comment, linked to the passage, not an x-coordinate.
:::
````

The first experiment should instead export the **supported subset** using
existing `:::figure`, `:::columns`, `:::note`, bibliography and footnote
syntax, with a machine-readable *loss report* for anything requiring a
new node. Parsing the proposals directly without first fixing their data
model would turn the Markdown extensions into another presentation language.

## A PDF-to-Xanatoo pipeline

**Prefer deterministic extraction and structure-aware converters with an
optional local AI/VLM repair stage.** A single unconstrained "read this PDF
and output Markdown" prompt tends to lose labels, shuffle columns, invent
mathematical symbols and omit figures. AI may help, but the source pixels and
machine-detected text must remain independently inspectable.

1. **Intake** — inspect metadata, PDF permissions, file hash, pages, embedded
   text layer, image resolution, bookmarks and tagged PDF structure. Keep
   existing digital structure when available. OCR pages only when needed.
2. **Region detection** — page image + words/glyph coordinates -> title,
   text, headings, captions, formulas, tables, images, header/footer and
   sidebars. Distinguish an illustration's *inside text* from article text.
3. **Reading-order graph** — group blocks into sections/articles and
   determine parent, predecessor, caption-of, note-of, refers-to and
   aligned-with relations. Do not infer order from `x/y` sorting alone.
4. **Specialist extraction** — native text or OCR for prose, math OCR for
   formula sources, table grid with row/header associations, image crops for
   figures, and references/DOI parsing. Keep original crops where confidence
   is low or symbols are essential to comprehension.
5. **Constrained semantic pass** — use a model, if needed, to classify
   roles, connect captions/callouts, repair OCR in context, and identify
   uncertain reading order. Validate against source words and regions;
   every generated fact must trace to a source region.
6. **Neutral intermediate tree** — retain IDs, text, roles, relations,
   page/box and confidence, *then* lower to Xanatoo's canonical AST. Emit
   both `.sexp` and a readable `.md` projection; do not assume the two
   have identical expressive power yet.
7. **Validate and review** — parse generated markup back, render at several
   window sizes, compare against source per region, review low-confidence
   symbols, tables and links, and report both structural omissions and
   unsupported semantic constructs. Offer source-page side-by-side view.

An example *intermediate annotation* (not Xanatoo file syntax):

```json
{
  "id": "r-204",
  "role": "figure_caption",
  "text": "Fig. 2. Experimental apparatus",
  "source": {"uri": "source.pdf", "sha256": "<record actual hash>"},
  "pdf_page_index": 12,
  "printed_page_label": "8",
  "bbox_normalized": [0.12, 0.71, 0.84, 0.79],
  "relations": [{"kind": "caption-of", "target": "r-203"}],
  "method": "ocr+layout-model",
  "confidence": 0.89
}
```

The bbox is only for traceability and a facsimile comparison. Do **not**
serialize arbitrary PDF x/y offsets into semantic content or attach the
number `0.89` to an uncited generated claim.

### Baseline technologies to evaluate

| Tool/source | Best contribution | Limitation to test |
|---|---|---|
| [Docling](https://github.com/docling-project/docling) | Layout, reading order, tables, formulas, Markdown and structured JSON; offline operation and VLM integration | Semantic links/footnotes/book-specific constructs may not round-trip |
| [GROBID](https://github.com/grobidOrg/grobid) | Scholarly PDF → TEI with title/authors/abstract, sections, figures/tables, citations and page coordinates | Focused on papers; old scans, poetry and dictionaries require a different path |
| [Nougat](https://github.com/facebookresearch/nougat) | OCR-oriented scientific PDF image → markup, especially math | Image-to-text errors; may discard region/reading-order provenance |
| [DocLayNet](https://github.com/DS4SD/DocLayNet) | Generic document-layout training/evaluation labels and boxes | Region labels alone do not produce semantic relationships |
| [PubLayNet](https://github.com/ibm-aur-nlp/PubLayNet) | Paper page layouts paired with structured PMC articles | Narrower document types and coarse region classes |
| [JATS](https://jats.nlm.nih.gov/), [BITS](https://jats.nlm.nih.gov/extensions/bits/) | Rich, existing XML vocabularies for scholarly articles/books, and concrete reference implementations | Many tags are publication-specific; map a useful subset instead of cloning their entire grammars |

Where the same article is available as born-digital JATS XML *and* as a
PDF, importing the XML is a **ground-truth shortcut** and a meaningful
target for the PDF model. JATS already distinguishes figures/groups,
formulas, tables/footnotes, formal statements, boxed text, quotes, speech,
verse, front and back matter. The NLM JATS 1.2 sample page provides two
such PDF/XML pairs (BMJ and PNAS). See also the 2024 [JATS 1.4 standard](https://www.niso.org/standards-committees/jats).

## Benchmark and implementation milestones

1. **Curate 2–4 pages per type**, chosen after opening scans rather than
   guessing page numbers. For each page store the PDF hash, document/printed
   page number, important regions, expected reading order and intended
   Xanatoo AST. Start with Byrne, Century, Scientific American and the two
   JATS samples, then extend.
2. **Build an offline harness**, `PDF -> candidate AST -> Markdown/S-expression
   -> Xanatoo parser -> structural diff`. Record a fidelity report for each
   source and upload no content externally by default.
3. **Score semantics, not pixel similarity**: exact text with uncertainty;
   coverage of meaningful blocks; ordering/edge accuracy; math symbol
   agreement; table header associations; figure-caption links; citation
   target resolution; footnote/backlink consistency; number of manual fixes.
   Also measure *unsupported-role* count. For magazines additionally score
   article-continuation edges, separation of ads/sidebars from editorial
   prose, detection of duplicated pull quotes, and retention of artwork
   that cannot be semantically reflowed.
4. **Implement high-return roles** after inspecting errors: front matter,
   theorem/definition/proof, numbered math, table footnotes/spans, verse and
   marginal notes. Extend the canonical AST first, then Markdown fences,
   parser/serializer tests, layout, and styles.
5. **Preserve a lossless sidecar** for source provenance, confidence and
   unrecognized regions; later use it for a facsimile/semantic split-view
   editor and targeted human correction.

The near-term success criterion is not "any PDF looks identical". It is:
**one representative page from each genre can become editable semantic
markup, reflow into a narrow window, preserve usable links, and let the
reader inspect any uncertain region in the original**.

## Research sources

- NLM [JATS publishing tag set and complete PDF/XML samples](https://jats.nlm.nih.gov/publishing/tag-library/1.2/chapter/samples.html),
  [JATS 1.4 display formulas](https://jats.nlm.nih.gov/archiving/tag-library/1.4/element/disp-formula.html),
  [JATS4R display object practices](https://jats4r.niso.org/display-objects-figures-tables-boxed-text-etc/).
- [PMC submission specifications](https://pmc.ncbi.nlm.nih.gov/pub/filespec/)
  and [PMC Open Access Subset](https://pmc.ncbi.nlm.nih.gov/tools/openftlist/)
  for matching article XML/PDF with item-specific licences.
- [Docling](https://github.com/docling-project/docling),
  [GROBID](https://github.com/grobidOrg/grobid),
  [Nougat](https://github.com/facebookresearch/nougat),
  [DocLayNet](https://github.com/DS4SD/DocLayNet),
  [PubLayNet](https://github.com/ibm-aur-nlp/PubLayNet).
- [Wikimedia Commons' Byrne scan](https://commons.wikimedia.org/wiki/File:The_first_six_books_of_the_Elements_of_Euclid,_in_which_coloured_diagrams_and_symbols_are_used_instead_of_letters_.._(IA_firstsixbooksofe00eucl).pdf),
  [Century Dictionary scan and licence note](https://commons.wikimedia.org/wiki/File:The_Century_dictionary_-_an_encyclopedic_lexicon_of_the_English_language-_prepared_under_the_superintendence_of_William_Dwight_Whitney_(IA_centurydictipt100whituoft).pdf),
  [Whitman Archive](https://whitmanarchive.org/item/ppp.00707),
  [Library of Congress First Folio](https://www.loc.gov/item/2021666879/).
- Magazine references: [*Mondo 2000* scan index](https://anarchivism.org/w/Mondo_2000),
  [issue 4](https://archive.org/details/Mondo.2000.Issue.04.1991),
  [Emigre / Letterform Archive](https://letterformarchive.org/news/new-in-the-online-archive-complete-emigre-magazine/),
  [University of Iowa Digital Dada Library](https://sdrc.lib.uiowa.edu/dada/digitaldadalib/index.html),
  [UPenn public-domain *Dada* catalogue](https://onlinebooks.library.upenn.edu/webbin/serial?id=dada1917),
  [Wikimedia Commons *391* PDF](https://commons.wikimedia.org/wiki/File:391_magazine_Volume_13.pdf).
