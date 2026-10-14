# Design note: splitting `Node` into per-kind payloads

Status: proposal, no code. Follow-up to `docs/PLAN-ecs-lite.md` (Phase D
left this out on purpose).

## Problem

`xanatoo::Node` (`include/xanatoo/types.hpp`) is one struct for all 29
`NodeKind`s. Kind-specific data is squeezed into a handful of shared string
fields whose meaning depends on `kind`, documented only in a comment block:

| Field | Meaning by kind |
|-------|-----------------|
| `level` | legacy Heading depth; List ordered flag; TableCell header flag |
| `src` | Image path; CodeBlock language; Note kind; Message author; Header TODO keyword |
| `alt` | Image alt text; Message in-reply-to id or `about:#anchor` |
| `summary` | Stretch collapsed label; Transclude `:excerpt`; Thread subject; Message date |
| `label` | link anchor (`#label`); Thread id; Message-id; BiblioEntry key |
| `tags` | org tags (Header) |
| `expanded`, `display_scale` | Stretch / Image only, but carried by every node |

Rough current usage (member accesses in `src/` + `include/`): `label` 106,
`level` 88, `src` 75, `summary` 47, `display_scale` 41, `alt` 37,
`expanded` 20, `tags` 13. Every node also carries two `std::string`s and a
`vector` it never uses (e.g. a Paragraph pays for `src`, `alt`, `summary`,
`label`, `tags`).

Consequences: misuse is silent (`node.src` on a Message is an author),
new kinds must invent more overloads, and serialization/layout code has to
remember each kind's convention.

## Goals / non-goals

Goals: type-safe, self-describing kind data; smaller `Node`; same stable
`NodeId`s and arena storage; no change to on-disk formats or rendering.

Non-goals: changing `Document` as an arena, the span model, or adding a
generic ECS. Not a prerequisite for anything else in the roadmap.

## Options

**A. `std::variant` payload inside `Node`.**
`Node { id, kind, parent, children, spans, Payload data; }` with
`Payload = std::variant<std::monostate, ImageData, StretchData, CodeData,
NoteData, TranscludeData, ThreadData, MessageData, BiblioData, HeaderData,
TableCellData, ListData, ...>`. Accessors `node.image()` /
`node.as<ImageData>()` that assert the kind.
+ One container, minimal change to `Document`, `create`, traversal.
+ `kind` stays authoritative; payload is derived from it.
− `kind` and variant index can drift; need a single `set_kind` path.
− Every call site of the 8 shared fields changes.

**B. Side tables keyed by `NodeId`** (true ECS-lite).
`Document` gains `std::unordered_map<NodeId, ImageData> images;` etc.
+ Zero cost for nodes without the component; matches AGENTS.md's
  data-oriented principle.
− Lookup cost in layout hot paths; history snapshots must copy every table;
  deleting/moving nodes must keep all tables consistent.

**C. Keep `Node`, add typed accessor views** (no storage change).
`ImageView{Node&}` with `.path()` → `src`, `.alt()` → `alt`, etc.
+ Almost free, can be done incrementally, no format risk.
+ Gives the type safety and documentation; the layout stays as is.
− Does not shrink `Node`.

**Recommendation: C first, then A if memory/readability still matters.**
C removes the real hazard (undocumented overloads) at low risk and is a
mechanical, per-kind migration. A can follow kind by kind behind the same
accessors, because call sites already go through them. B only pays off
if documents grow large enough for per-node size to matter — not the case
now, and it complicates `History` (whole-document snapshots).

## Migration sketch (option C → A)

1. Add `node_views.hpp` with one view per kind group: Image/Figure,
   Stretch, Code, Note, Transclude, Thread/Message, Biblio, Header, Table,
   List. Each exposes named getters/setters over the existing fields.
2. Migrate call sites per file in this order (smallest blast radius first):
   `reddit.cpp`/`lemmy.cpp`/`nostr.cpp` (Thread/Message only) → `org.cpp` →
   `parse.cpp` → `layout.cpp`/`render.cpp` → `editor.cpp` → `app_*.cpp`.
   Each step compiles and runs `xanatoo_tests`.
3. Add round-trip tests (parse → serialize → parse) for every kind that has
   an overloaded field *before* migrating it, so behaviour is pinned.
4. Only then, optionally swap the storage behind the views for a variant
   (option A) one kind at a time; delete the old fields when the last
   reader is gone.

## Risks

- `History` copies whole `Document`s: a variant payload with heap members
  stays value-copyable; side tables (B) would need explicit snapshotting.
- Parsers set fields before the final kind is known (e.g. Header vs
  Heading, Section synthesis). A variant forces deciding earlier or
  re-assigning payload on `set_kind`.
- `level` is used both as legacy depth and as a flag; the migration must
  split it into `depth` / `ordered` / `header_cell` explicitly and keep the
  "soft interop" export behaviour (AGENTS.md "Section tree").
- Sidestore keys (`section_stable_key`) hash node content; changing which
  fields hold what must not change keys, or saved fold state is lost.

## Open questions

- Should `label` (the anchor) move onto *every* node as a first-class
  `anchor` field, since links can target any block, with kind-specific ids
  (thread id, message-id) separate?
- Do we want `Image.display_scale` persisted differently from layout hints?
- Is the legacy `Heading` kind still needed once all loaders emit
  Section + Header?
