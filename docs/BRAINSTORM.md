# xanatoo — design brainstorm

> Working notes from ongoing design (2026-10). Not a roadmap commitment.
> See also `PROPOSAL.md`, `FORMAT.md`, `AGENTS.md`, `TODO.md`.

This document captures product decisions already made, open structural ideas,
and longer-horizon hypertext / multi-document thinking (Xanadu-shaped, without
pretending to be a full Xanadu implementation).

---

## 1. Locked product decisions (sectioning & view state)

| Topic | Decision |
|-------|----------|
| Org stars / ATX / setext | **Always** normalize into nested `Section` + `Header` title child. Body blocks are children of the section. |
| `Article` | **Never** synthesised automatically. Magazine-scale unit: author must opt in (`:::article` / wrap command). |
| Outline jump to folded target | **Jump only** for now; do not auto-expand. Expand-on-jump can come later. |
| Fold / chrome state | Ultimately **side SQLite**, **not** written into `.md` / `.org`. |
| Document files | Trend toward **immutability**. May **ingest** files into the DB for versioning and annotation without rewriting the original. |
| Structure highlight | Header labels show depth: `header1` … `headerN` (soft level or section nesting). |

**Implemented (as of tip ~224):** section-tree normalize, view-local fold
(`folded_sections` in memory → layout skips body; outline twisties; Ctrl+Shift+F),
large-doc layout virtualization (viewport + height hints).

**Implemented (232):** promote/demote, section move, same-doc backlinks, transclude other-pane + broken flash.

**Still open on this line:** promote/demote / move subtree; SQLite side store;
dual-pane “outline | body” view (view only, same tree).

---

## 2. Document structure (mental model)

Canonical sectioning (already in `FORMAT.md`):

| Node | Role |
|------|------|
| **Header** | Title of the enclosing section/article — **no rank** as identity |
| **Section** | Thematic group; may nest |
| **Article** | Self-contained composition (intentional) |

Interop depth (org stars, ATX `#`) is a **soft field** (`level`) for export and
structure highlight; **hierarchy lives in the tree**.

Outline is a **projection** of Section/Article (plus legacy bare headers), not a
second document. Fold axes:

| Axis | Storage | Effect |
|------|---------|--------|
| Document body fold | View-local set → future SQLite | Layout shows header only |
| Outline row expand | Same set for v1 (tracks body fold) | Nested outline rows hidden |

Later option: independent outline UI expand vs body fold.

---

## 3. Identity, numbering, and change

### What not to use as permanent addresses

- **Visible paragraph numbers** — presentation of order in one version; regenerate for print; never primary link targets after edits.
- **Session `NodeId` (arena index)** — runtime handle only; invalid across reload/edit sessions as a durable citation key.

### Preferred address stack

| Mechanism | Stability | Role |
|-----------|-----------|------|
| **Stable label** (`Node.label`, org `CUSTOM_ID`, fence `id=`) | High if maintained | Primary human address (`#label`) |
| **Structural path** | Medium | Fallback / debug |
| **Content fingerprint** (normalized text hash) | Detects drift | “Source changed” warning |
| **Version id** (content hash, ingest revision) | Pins a snapshot | Citation, review |
| **Machine UUID** (optional) | High | Assigned at first cite/annotate; lives mainly in side store |

**Policy when sources change:**

- Live resolve: label @ chosen version (often “head”).
- Pinned resolve: label @ version V (or hash) for citations that must not drift.
- Label moved → follow label; label gone → broken link + optional fuzzy recovery.

Do **not** force a UUID onto every paragraph in plain `.md`. Generate machine ids
when a passage is cited or annotated.

---

## 4. Image galleries (semantic)

Prefer **document meaning** over chrome-only “gallery mode”.

| Approach | Notes |
|----------|--------|
| **Section (or dedicated Gallery) of Figures** | Ordered children: image + caption + per-item label. Outline lists the set. |
| **Layout as view** | Grid / strip / lightbox is presentation of the same container. |
| **Columns of figures** | Works today for 2–3; weak semantics if overused as “the” gallery. |

Worth storing: order (tree order), shared credit on container, per-item alt,
optional link to a passage or URL. Transclusion can pull one plate or the whole set.

---

## 5. Transclusion and multi-document interaction

Existing primitives: `:::include #anchor` / `path.md#anchor`, optional excerpt;
external doc cache; tabs / split panes; jump-to-href.

### Kinds of reference (keep distinct)

1. **Citation / jump** — navigate to a passage.
2. **Transclusion** — live embed of a passage here.
3. **Range / excerpt** — partial embed (phrase or future start/end labels).
4. **Snapshot citation** — fixed text as of version V (needs version store).

### Multi-doc UX increments

- Open transclude / link target in the **other pane** (parallel documents).
- **Inspector**: resolved path, anchor, version, broken?, edit-here vs edit-source.
- Breadcrumbs for include chains; depth limit + cycle detection.
- Write policy: same-doc write-through later; cross-doc read-only until identity is stable.

### When the other document changes

Resolve by **label** (and optional version). On mismatch: stale/broken UI; keep last
good snapshot in host view if available; offer update / open source.

Side SQLite should eventually hold **versions, labels, fold/view, annotations, and
resolved transclude edges**; files remain canonical bytes when ingested.

---

## 6. Xanadu-style hypertext links

xanatoo is **not** a full Xanadu implementation (`PROPOSAL.md`). It is a realtime
composition engine where relationships should survive rearrangement.

### Xanadu themes that matter here

| Idea | Local reading |
|------|----------------|
| Addressable content | Passages with stable labels (and later version pins) |
| Transclusion | Show original in place (`Transclude` node) |
| Links as relations | Not only “blue underline paint” on one end |
| Typed relations | `InlineSpan.rel` (glossary, source, contra, …) |
| Bidirectionality | Backlinks: who points here |
| Versioning | Live vs pinned ends |
| Parallel documents | Side-by-side panes for from/to or two versions |

### Already in the model

- `Link` / `Cite` spans; `href`; `rel`
- `jump_to_href`, hover previews
- `Transclude` + excerpt
- Message `about:#anchor` (comment about a passage)
- Session `InlineSpan.target` (`NodeId`) — not durable

### Proposed layering

**PassageRef** (conceptual):

```text
doc     : DocId          // library path or ingest UUID
ver     : VersionId?     // null = head
label   : string?        // preferred
uuid    : string?        // optional machine id
range   : RangeSpec?     // whole node | labels | excerpt
```

**Link** as a first-class relation (may be stored in SQLite; markers remain in text):

```text
type    : RelType
from    : PassageRef
to      : PassageRef
```

**Presentations of the same relation:** jump, typed badge, transclude embed,
margin/backlinks, parallel pane.

**Small relation vocabulary (start):** empty (see also), `defines`/`glossary`,
`source`/`quotes`, `contra`, `continues`/`part-of`, `annotates`, `includes`.

Map existing features into that vocabulary (Message-about → `annotates`,
Transclude → `includes`) so one **link index** can query them.

### Suggested implementation slices (not scheduled)

| Slice | Outcome |
|-------|---------|
| A | `#label` prefers `Node.label`; section labels first-class in outline |
| B | Same-doc link index + backlinks panel; then library-wide |
| C | Show `rel` in UI; index Message-about and Transclude |
| D | Open internal/cross-doc target in other pane |
| E | Side store: DocId, version hash, pinned ends, annotations |
| F | Pinned vs live transclude; stale detection |

Avoid: global universal addressing before local library + labels work; mandatory
visible numbers as identity; UUIDs forced into every paragraph of the source file.

---

## 7. Dual-pane outline | body (later)

Same document tree, different chrome:

- Left: outline tree (depth filter optional).
- Right: continuous document or body of the selected section.

**View mode / soft-window pair**, not a split of the file into two documents.
Scriptable later as a small view descriptor (ratio, fold policy). Design constraint:
do **not** maintain a parallel outline buffer that can drift from the tree.

---

## 8. Side SQLite (direction of travel)

Intended responsibilities (brainstorm, not schema):

- Ingested document versions (hash, path, timestamps)
- View state: fold set, outline geometry, pane layout
- Annotations and typed link edges (including backlink index)
- Optional node UUID ↔ label maps per version
- Bookmarks / history already partly file-based; may migrate

**Principle:** editing *view* and *annotation* should not require rewriting the
author’s original `.md`/`.org` once ingest is in place.

### Saving without "Save" (Canon Cat direction, 2026-10-27)

User direction, still brainstorm — nothing specified yet:

- Saving is **automatic** and goes into the internal database, not the
  filesystem: every edit lands there, **versioned**, with **persistent
  undo** across sessions (the Canon Cat never asked to save; the text
  simply was).
- **Export** to `.md` / `.org` / `.sexp` files becomes an explicit action,
  not the normal way work is kept.
- **Foreign content** (web pages, feeds, Reddit / Lemmy / HN / Nostr
  threads, imported EPUB / HTML) is read-only and opened in browse mode.
  To work with it, the user either **clones** it into the database (an own,
  editable version that remembers its origin) or switches on an
  **overlay** (annotations / edits kept beside the untouched original —
  ties in with the Xanadu-style links in §6).
- Implemented so far (stop-gap): foreign = remote URL, `.html` / `.epub`
  path, or anything under `cache_dir()`; such buffers are always browsed
  (`browsing(app)`), F2 refuses, and Ctrl+S offers "Save as" a `.md`
  copy — the file-based stand-in for "clone into the database".
- Open questions: granularity of versions (keystroke runs? idle commits?),
  how a clone tracks upstream changes (re-fetch → diff against the
  version it was cloned from), and how files on disk relate to database
  documents (import once, or watch and ingest).

---

## 9. Related open work (engineering)

From `TODO.md` / recent tips — not repeated in full:

- Large-document virtualization v1 done; tighter edit invalidation still open
- Section promote/demote / subtree move
- HarfBuzz, operation-log undo, editable same-doc transclusion, word-level diff
- Tables / docking / citations polish

---

## 10. Pointers

| File | Role |
|------|------|
| `PROPOSAL.md` | Vision: live workspace, stretchtext, relationships |
| `FORMAT.md` | Concrete markup, sectioning, sexpr, typed links notes |
| `AGENTS.md` | Agent conventions, feature gates, continuity |
| `TODO.md` | Tip, status, bundle history, open checklist |
| `docs/ENVIRONMENT.md` | Build / `nix develop` |

When an idea here is implemented, record the outcome in `TODO.md` and trim or
mark the corresponding brainstorm section so this file stays a **forward** notebook,
not a second changelog.


## EPUB import (implemented path)

EPUB is treated as **immutable**. Opening `.epub` runs:

```bash
pandoc book.epub -f epub -t gfm -o content.md --extract-media=media
```

under `~/.cache/xanatoo/epub-import/<stem>-<hash>/`. The editor loads the
derived Markdown; media paths resolve relative to that directory. Re-import
is skipped when `content.md` is newer than the EPUB. Requires `pandoc` on
`PATH` (provided in `nix develop` / wrapped `nix run`).
