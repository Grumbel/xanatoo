# Revisions, drafts and document identity

Goal: never ask "discard unsaved changes?". Every edit lands in a local,
append-only revision store; files on disk are one view of it. Long term the
same store carries identity, versioning and transclusion (Xanadu spirit)
without a central naming authority.

## Separate identities

| Mechanism | Identifies | Notes |
|-----------|------------|-------|
| Doc key: random 256-bit `doc:<hex>` (or `file:<path>` for now) | a logical document | stable across title / content changes |
| Content id `sha256:<hex>` | exact bytes | self-verifying; algorithm-tagged for agility |
| Revision id (content id of the record) | one immutable revision | record = doc key, parent, content, kind, time |
| Public key (later) | an author | Ed25519 signature over the revision record |
| Title / path / URL | a human name | mutable alias, never the identity |

A hash proves content matches an id; a signature proves who authorised a
revision. Neither says anything about trust or truth.

## Phase 1 (done): local drafts

`include/xanatoo/revstore.hpp`, `src/revstore.cpp`,
`~/.local/state/xanatoo/revisions.sqlite` (`state_dir()`):

- `objects(id, data)` — content-addressed bytes, insert-only.
- `revisions(id, doc_key, parent, content, kind, time)` — immutable
  records; `kind` is `file` (what was on disk when editing began),
  `draft` (autosave) or `save` (written to the file). The id is the
  content id of the canonical text (`xanatoo-revision 1\ndoc …\nparent …
  \ncontent …\nkind …\ntime …\n`).
- `heads(doc_key, latest, draft, base, title, time)` — the mutable
  pointer: latest revision, current draft, saved content it started from.
- `object_heads(key, revision, kind)` — independent heads for versioned typed
  objects; shares the immutable `objects` and `revisions` tables, never the
  file/draft head pointer. Existing databases acquire this table in place.

App side (`src/app_drafts.cpp`):

- Edits schedule a draft write (`kDraftDelayMs`); leaving a buffer
  (navigate, close tab, New, Open, Quit, window close) writes it at once.
  Undoing back to the saved text drops the draft.
- Loading a file (`attach_draft`) puts its draft in place of the file text
  (tab shows `*`); if the file changed on disk since, the notice says so.
- Ctrl+S records a `save` revision and clears the draft. File → Revert to
  Saved drops the draft (its revisions stay). File → Drafts / `drafts:`
  lists every draft; untitled ones open as `draft:doc:<hex>`.
- Without SQLite the old "repeat to discard" confirmation remains.

## Annotation layers (storage foundation)

`include/xanatoo/annotations.hpp`, `src/annotations.cpp`:

- A layer (`layer:<random-id>`) is an independently versioned typed object.
  Its snapshots contain the layer's owner/name and an ordered collection of
  annotations, encoded as canonical, length-delimited binary, not a modified
  copy of the article. The layer's `revision` is the immutable revision id.
- Each annotation has a stable id, kind, body, optional style name, and one
  or more targets. Each target records a document key, the document revision
  at annotation time, and a serialized passage locator. These can refer to
  different documents. This storage layer does not resolve/reanchor locators.
- Multiple users' layers can independently advance and later be composed on
  the same document. A layer save checks the revision the caller loaded; a
  stale writer is rejected instead of silently overwriting someone else's
  changes. Layers can be enumerated, and old versions remain retrievable by
  revision id.
- No annotation UI, collaboration protocol, signatures, or document changes
  are implemented yet. Owners are descriptive, not authenticated. Visibility
  is view-local and should not be baked into the shared annotation contents.
- `--no-save` and builds without SQLite cannot persist layers. The API
  reports failure rather than pretending the data was saved.

Next: compose the enabled layers over document panes by resolving their
locators (highlight, note anchors, scrollbar marks); keep unresolved anchors
visible for reattachment. Then add per-annotation edit commands and an
annotation-layer selector. Avoid interpreting text diffs as annotations.

## Later phases

1. **Revision browser**: history of a doc key (parent chain), diff any two
   revisions (the diff view exists), restore one as the new draft.
2. **Identity in the document**: store the random doc id in the file
   (front matter / `.sexp` meta) so renames and copies keep identity;
   `file:` keys become aliases.
3. **Overlays for foreign content**: edits / annotations on a remote page
   keyed by URL + the fetched content id, shown over the live page.
4. **Persistent text units**: stable ids for blocks (later spans) with
   mappings between revisions, so links and transclusions survive edits.
5. **Transclusion by revision**: `{source_doc, source_revision, range}`;
   *snapshot* by default, *follow latest* through the revision mappings
   with explicit handling for deleted / split / merged material.
6. **Signatures**: Ed25519 key per author, signed revision records and
   signed heads (doc id → current revision).
7. **Exchange**: content-addressed bundles over files / HTTP first, peer
   discovery and replication later. Competing heads are kept as branches;
   no global "latest", no blockchain.
8. **Canonical encoding** (deterministic CBOR or similar) once records
   leave the machine; housekeeping / GC policy for old objects.

Identity is not availability: a valid hash is useless once every copy is
gone, so replication and archiving are separate concerns.
