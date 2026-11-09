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
- `document_heads(key, working, published, title, time)` — database-native
  logical documents (`doc:<64 hex>`). `working` advances with autosaves;
  `published` advances **only** on explicit local Publish. These are separate
  immutable parent chains (`kind=working` and `kind=published`) and neither
  overwrites the other. The document's bytes live in `objects`, encoded with
  the internal S-expression representation, not a filesystem path.

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

## Database-native working and published documents

File → New creates a random document identity and immediately writes its
first working revision to the local SQLite store. Subsequent edits are
autosaved on the draft debounce, and Ctrl+S commits the current working
revision immediately. File → **Publish locally** records the current working
content in a separate publication history; this does **not** upload anything
or configure a remote destination. Optimistic revision-head checks reject
conflicting writes rather than replacing another editor's version.

File → **Local documents** or `documents:` lists these persisted documents.
Each document opens at `db:doc:<64 hex>`, retaining its stable identity on
reload or between runs. The tab shows the stored short title. Native
document File → Save As and Export commands write a file as an *export*;
the working document remains attached to its database key. A stored
published revision can be loaded separately by its immutable revision ID
through `load_stored_document_revision`, but there is not yet a GUI browser
to inspect or restore old working/published revisions.

**Migration is incomplete:** previously opened files still use the legacy
draft and save workflow, and Ctrl+S on one of those paths still writes the
file. A later increment should import the source version into a native
document without overwriting it, and distinguish the original from the local
editable version. Remote publishing, replication, signatures and portable
annotation exchange are likewise future work.

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

Annotation layers are overlays, not inferred from textual diffs. Their own
history and identity remain separate from the source document's revisions.

## Annotation highlights and notes (reader integration)

The **Panels → Annotations** window lists independently stored layers, with a
visibility checkbox and a radio choice of the local layer to edit. **New
personal layer** creates a fresh history; **Highlight selection** (also in
the document context menu) attaches the selected text to the active local
layer. It works on read-only foreign documents, without modifying them or
creating document drafts. If there is no local layer, the first highlight
creates one. Annotations save immediately; a failed save is reported.

The app loads layers on startup. Visible targets matching the current page
are re-resolved through the same durable passage selectors as bookmarks;
successful ranges are drawn over the document and marked on the scrollbar.
Unresolved targets remain in the layer and are counted but not yet offered in
an automated reattachment UI. Per-user layer visibility is saved in `sidestore.sqlite`,
not in the layer revision that might later be shared with others.

**Attach a note** writes a text-box body into the active local layer, pointed
at the selected passage. Notes are represented by a small tab at their text
anchor, as well as an overlay wash and a scrollbar mark; they do not add
document nodes or alter the saved page. The Annotations panel lists items on
the current page, including unresolved items, and selecting one jumps to its
resolved passage. Other owners' items can be viewed but not edited.

Local notes have a multiline text editor, with an explicit Save and an
automatic commit after 0.8 seconds without typing or when the editor loses
focus. Remove annotation deletes the entry from the **current layer head**;
older layer revisions retain it. On write failure the in-memory layer and
its revision cursor remain unchanged; a new note draft is only cleared once
its create operation succeeds. Each successful edit creates an immutable
revision (identical content does not duplicate revisions). This is a layer
history, entirely separate from document undo/drafts.

**Local layer history** shows up to 24 ancestor revisions of the active local
layer, newest first, with timestamps and annotation counts. Restore takes the
selected historical snapshot and commits it as a *new child of the current
head*: versions after the selected snapshot remain in history. A stale head
cannot be overwritten. The history browser can also recover removed notes.
Clicking or right-clicking a note's passage tab opens the Annotations panel
and selects the attached note. Tabs from multiple layers on the same line are
offset to avoid overlapping hit targets.

Limitations: the history browser is bounded to the latest 24 revisions and
currently restores an entire layer snapshot, not one annotation in isolation.
Automatic autosaving happens on idle/blur rather than after every keystroke.
Annotations use the existing bookmark wash colour; per-layer
colours, interactive margin text boxes, permissions, and anchor reattachment
are still planned. Published layer exchange remains future work. Database-native
working/published heads now exist, separately from annotation histories.

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
