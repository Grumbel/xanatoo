# Semantic markup and reader capabilities

> Design brainstorm, 2026-11-04. **Proposal, not implemented syntax or a
> committed roadmap.** See also [BRAINSTORM.md](BRAINSTORM.md),
> [PASSAGES.md](PASSAGES.md), [STYLESHEETS.md](STYLESHEETS.md), and
> [the project proposal](../PROPOSAL.md).

## The idea

Turn strings that happen to resemble dates, usernames, ISBNs, scores, or
citations into **typed objects with useful reader-provided behavior**.

A date should know *when* it is. An ISBN should know which edition it
identifies. A username should know which account on which service it names.
A reply should know which passage it answers. The label displayed on the
page need not be the canonical value, the identity, or the destination.

HTML is not devoid of these ideas: it has `<time>`, `<data>`, `<meter>`,
`<progress>`, `<cite>`, `<abbr>`, and structured-data mechanisms such as
Microdata; Schema.org and microformats define richer vocabularies. The gap
worth exploring is **native reader behavior** driven by semantics rather
than page-specific scripts and CSS. What can a reader do automatically when
it understands the value, identity, or relationship?

## Design principles

1. **Meaning, presentation, and interaction are independent.** A span can
   simultaneously be linked, be metadata, represent a date, and be quoted.
   A `.xss` style sheet controls its appearance; it does not determine its
   meaning.
2. **Small primitives, extensible vocabularies.** Avoid a new renderer node
   kind for every website, identifier scheme, or metadata field.
3. **Stable reference, flexible label.** `yesterday` and `2026-11-04` can
   describe the same instant. `@alice` without a service does *not*
   identify a unique account.
4. **Preserve precision, uncertainty, provenance, and time of observation.**
   A historical score is not live data; `summer 1969` is not a timestamp.
5. **Keep document data passive.** The reader may offer resolvers, previews,
   conversions, and menus, but network fetching and state-changing actions
   remain explicit, permission-aware capabilities.
6. **Typed relationships are first-class hypertext.** A reply, citation,
   translation, sibling, and revision are not all the same kind of edge.
   Passage addresses and backlinks should work across them.

## Core semantic primitives

| Primitive | Specializations and examples | Useful reader behavior |
|---|---|---|
| `temporal` | Instant, calendar date, interval, duration, recurrence | UTC/local/relative display, calendar navigation, timelines |
| `entity` | Person, service account, organization, book/work, edition, software project | Profile preview, aliases, contextual navigation, mentions |
| `identifier` | ISBN, DOI, arXiv, RFC, Wikidata ID, Git SHA | Validate, normalize, resolve, find related records |
| `value` | Length, temperature, storage size, currency, percentage | Convert units, compare, copy normalized representation |
| `metric` | Points, upvotes, ratings, downloads, view count, progress | Explain scale, subject, source, observation time, trend |
| `reference` | Link, citation, reply, sibling, translation, revision, excerpt | Follow, preview, reverse link, show relationship context |
| `place` | Address, coordinate, region | Map, distance, location context |
| `state` | Open/closed, deprecated, task progress, availability | State explanation, state history, authorized actions |

These names are conceptual categories, **not** a finalized markup grammar.
They should compose: a `metric` may have a `temporal` observation value and
a `reference` to the `entity` it measures.

## Temporal markup: instants are only the beginning

Distinct forms of time carry different information:

| Kind | Example | Important distinction |
|---|---|---|
| Instant | `2026-11-04T14:30:00Z` | Same point in time worldwide |
| Wall-clock appointment | `14:30 Europe/Berlin` on a given date | Zone rules and daylight-saving transitions matter |
| Date | `2026-11-04` | A calendar day, not a precise moment |
| Coarse date | `2026-11`, `1969` | Month/year precision, not missing zeros |
| Approximate period | `summer 1969`, `circa 1850` | Uncertain bounds and interpretation |
| Interval | January 1–15 | Start/end with inclusive/exclusive semantics |
| Duration | 90 minutes | Length of time, no absolute origin |
| Recurrence | Every second Tuesday | Rule, zone, and exceptions |

For exact instants, a hover panel can show UTC, reader-local time, original
timezone, and age/future distance. A context menu might provide ISO 8601,
Unix time, conversion, calendar/event context, and a timeline centered on
that instant. For an interval, show its length and a timeline. Do **not**
silently turn approximate dates into spurious exact instants.

**Reader preference:** display the same semantic value as `yesterday`,
`4 Nov 2026`, or `Wednesday, November 4, 2026` without changing the stored
meaning. A timezone-less local appointment must not be treated as UTC.

**Already present:** `InlineSpan::datetime` and imported HTML
`<time datetime="…">` preserve an ISO 8601 instant; hover shows UTC,
machine-local time, and relative time. The broader temporal model above is
still an idea.

## Identity and identifiers

`@alice` is a **display handle**, not a globally unique identity. The
account `hn:user?id=alice`, `reddit:u/alice`, a GitHub account, an email
address, and a real person are distinct objects. Profiles may change their
display names. Multiple accounts should only be associated with a person
when there is evidence or an explicit assertion: never merge on equal
handles alone.

An identifier resolver registry looks especially attractive because many
schemes can share one generic representation:

| Scheme | Possible actions |
|---|---|
| ISBN-10 / ISBN-13 | Check digit, book/edition metadata, other editions, library catalogs |
| DOI | Publication information, source preview, related citations |
| arXiv ID | Abstract, PDF, available revisions |
| RFC number | Read standard, successors, related documents |
| Git SHA | Repository context, changes, parents, associated review |
| Wikidata Q-ID | Entity description and relationships |
| Package/version coordinate | Release notes, dependencies, compatibility |

Maintain the difference between a literary **work**, a particular
**edition/translation**, and an individual **physical copy**. An ISBN
typically identifies an edition or format, not the abstract work.
Likewise, a software version and a particular commit are different kinds
of references.

## Quantities, measurements, metrics, and actions

Numbers that look alike can have quite different semantics:

- **Quantity:** `12 km`, `3.5 GiB`, `24 °C` — value plus unit/dimension.
- **Measurement:** `21.4 °C ± 0.2 °C` — observed quantity with uncertainty.
- **Currency value:** `€125 in 1995` — amount, currency, and reference date;
  historical conversion is different from a current exchange rate.
- **Metric:** `173 Hacker News points`, `8,000 downloads` — numeric value
  about an identified subject, from a source, observed at a time.
- **Rating:** `4.2 / 5 stars`, `92% approval` — scale, sample size, and
  aggregation method matter.
- **Progress:** `3 of 5 tasks completed` — numerator, denominator, state.

A metric should be able to record `subject`, `kind`, `value`, `scale`,
`source`, `observed_at`, and whether it represents a **snapshot** or
**live** value. An archived HN post should not quietly replace its 2026
score with a 2031 score. A displayed upvote count is data; performing an
upvote is a separate authenticated action.

## Typed links: hypertext beyond ordinary navigation

This may be more transformative than introducing additional inline tags.
The same passage-address and backlink machinery can serve relations with
very different meanings:

| Relation | Meaning and possible reader behavior |
|---|---|
| `reply-to` | Jump to parent, preview it, show siblings and conversation |
| `next`, `previous` | Navigation through an ordered sequence |
| `sibling` | Move laterally through sections at the same level |
| `parent`, `child` | Breadcrumbs, outline and hierarchy |
| `related` | Related-content panel, without implying hierarchy |
| `references` | Inspect cited passage and find reverse citations |
| `supports`, `contradicts` | Show evidence and counterevidence side by side |
| `translation-of` | Parallel-language reading and alignment |
| `revision-of` | Compare versions, display diff/history |
| `derived-from` | Follow provenance or transformation chain |
| `excerpt-of` | Open the exact source passage in its original context |

Particularly important: `#id:` may **point** to a passage in a whole
document, while `//id:` can **cut** to that passage. Keep those distinct
from the relation describing *why* the two things are connected. See
[PASSAGES.md](PASSAGES.md). This is the natural home for lateral navigation
and for relations between documents, not just links descending into them.

## Other promising domains

- **Quotation/excerpt:** distinguish quoting from citing; remember the
  source work, edition, passage, version, and attribution. Allow opening
  the quoted portion in context or transcluding it.
- **Book structure:** work, edition, translation, chapter, figure, paragraph,
  page reference, footnote. Keep passage identity independent of pagination
  and screen layout.
- **Programming symbols:** an identifier can resolve to a symbol in a
  particular repository/revision, with definition, uses, docs, and history.
  This goes beyond mere `<code>` styling.
- **Media passages:** address video/audio time ranges and image regions,
  using the same general passage model as text.
- **Claims/evidence:** explicitly link assertions to supporting and
  contradicting passages; show sources without inventing a truth score.
- **Terms and translations:** concepts, synonyms, language, pronunciation,
  definitions, translations, and occurrences. Useful for glossaries and
  parallel texts.
- **Lifecycle and task status:** open/closed/deprecated, assignments, due
  dates, milestones; separate declared state from operations that alter it.

## What the markup could look like

The examples below are **illustrative S-expressions only**, not accepted
Xanatoo syntax. The text at the end is a human-facing label; the structured
fields are the semantic value.

```scheme
(entity (kind user)
        (ref "hn:user?id=dang")
        "dang")

(temporal (instant "2026-11-04T14:30:00Z")
          "yesterday")

(identifier (scheme isbn13)
            (value "9780140328721")
            "ISBN 978-0-14-032872-1")

(metric (kind score)
        (value 173)
        (subject "hn:item?id=12345")
        (observed "2026-11-04T14:30:00Z")
        "173 points")

(reference (rel reply-to)
           (target "#id:12340")
           "reply to alice")
```

There need not be distinct *structural* node kinds for every example. A
better implementation may use a small set of typed annotations attached to
existing inline spans, blocks, or document passages. A span can carry a
link target, a semantic value, a role such as metadata, source attribution,
and additional relationships simultaneously.

## Reader-supplied capabilities

The payoff is a **semantic context menu** generated by the reader, not a
site-authored JavaScript widget. Right-click an ISBN and see:

1. Show book information.
2. Find other editions.
3. Search libraries.
4. Copy normalized ISBN.
5. Copy a semantic reference.

The same generic mechanism could offer timestamp conversion, user-profile
navigation, citation inspection, unit conversion, or source-code lookup.

Important boundaries:

- Semantic annotations should remain useful offline; network enrichment
  is optional, cached, and visibly sourced.
- Read-only inspection is not the same as mutating a remote account.
- Avoid scripts embedded in documents as the mechanism for core behavior.
- Unknown semantic vocabularies should remain readable as ordinary content.
- Keep the canonical value separate from user-facing labels and styles.
- Preserve evidence, uncertainty, version, and observation time when known.

## Incremental plan

| Order | Work | Why now |
|---|---|---|
| 1 | Generalize temporal annotations to dates, durations, intervals | Current timestamp support is a small working foundation |
| 2 | Make link relations and passage targets explicit | Central to the hypertext vision; reuse backlinks and selectors |
| 3 | Build an extensible identifier/resolver registry | One mechanism enables ISBN, DOI, RFC, Git SHA, etc. |
| 4 | Introduce entity/account identities | Useful across HN, Reddit, Wikipedia, authors, books |
| 5 | Add quantities, metrics, and observation/provenance metadata | Scores, ratings, sizes, prices, historical values |
| 6 | Centralize semantic hover/context capabilities | Shared behavior across markup types and service clients |

The architecture should settle **annotation composition, stable identity,
typed relationships, and capability dispatch** before adding dozens of
specialized tags.

## Related standards and prior art

- [WHATWG HTML: text-level semantics](https://html.spec.whatwg.org/multipage/text-level-semantics.html)
  (`time`, `data`, and other existing elements).
- [WHATWG HTML: forms](https://html.spec.whatwg.org/multipage/form-elements.html)
  (`meter`, `progress`).
- [WHATWG HTML: Microdata](https://html.spec.whatwg.org/multipage/microdata.html).
- [Schema.org: Book](https://schema.org/Book) and
  [QuantitativeValue](https://schema.org/QuantitativeValue).
- [Microformats2](https://microformats.org/wiki/microformats2) (`h-card`,
  `h-event`, `h-entry`).

These offer vocabulary and interoperability ideas, but the objective here
is deliberately more interactive: meaningful document objects should
activate useful generic **reader** behavior, including passage-level
navigation, without requiring each source to implement it.
