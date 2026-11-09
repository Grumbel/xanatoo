<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# Publishing Xanatoo documents

Status: design sketch. **File → Publish locally** currently advances an
immutable published revision in `revisions.sqlite`; it does **not** upload
anything. Network publication must remain a separate, observable operation.

## Separate the jobs

| Problem | Candidate | Notes |
|---|---|---|
| Canonical editable document | Xanatoo S-expression / XML | Keep stable document identity distinct from a revision hash. |
| Human-readable representation | Static HTML, Markdown | Readers should not need Xanatoo. |
| Writable ordinary web hosting | HTTPS/WebDAV | Upload immutable revision files, then update a `latest` pointer conditionally using ETags. |
| Immutable distribution / archiving | IPFS | CID identifies bytes; an IPNS name may point to the latest CID. Availability still needs pinning. |
| Authors, following, announcements, discussion | Nostr | NIP-23 articles, NIP-22 comments, relay discovery, profile/follow information; **not** a guaranteed revision archive. |
| Source-controlled publication | Git/Pages | Alternative transport for the same static output. |

Do **not** create a proprietary network protocol or require a Xanatoo
server. Define a small open publication manifest and independent adapters.
The transport should not determine document identity or internal format.

## Proposed publication model

- `document_id`: stable logical document identity, independent of location.
- `revision_id`: content hash of one immutable document snapshot. Parent
  revision IDs describe ancestry; published and working heads remain separate.
- `manifest`: schema version, document and revision IDs, title, creation and
  publication times, media type, parent(s), assets with their checksums and
  relative URLs, optional author signatures. Avoid self-referential hashes.
- `representations`: canonical editable content, static HTML, optional
  Markdown and assets. Exports never overwrite the user's source documents.
- `latest`: *mutable* pointer to an otherwise immutable revision. Publish
  revision contents before updating the pointer.
- `receipts`: one local record per destination and revision, with remote URL
  or CID/event ID, status, last attempt, and ETag where applicable. A locally
  published revision can have zero or many external publication receipts.
- `annotations`: independently versioned layers, separately publishable and
  subscribable, with targets that identify a document and optionally an exact
  immutable revision. Never silently merge foreign annotations into source.

An illustrative WebDAV/static export:

```
article-123/
  latest.json
  revisions/
    <revision-id>/
      manifest.json
      document.sexp
      document.html
      assets/<digest>...
```

For IPFS, publish the immutable revision tree to obtain a CID; IPNS is an
optional mutable pointer. Nostr publishes readable Markdown as NIP-23
`kind:30023` under a stable `d` tag and links the richer Xanatoo manifest.
Never depend on a relay retaining older versions of replaceable events.
NIP-22 `kind:1111` comments can be presented as a discussion/annotation
layer, with attribution and provenance.

## Implementation order

1. **Directory exporter**: reproducible, standalone publication tree; offline
   tests for manifest parsing, relative assets and revision round trips.
2. **WebDAV adapter**: configurable HTTPS endpoint, credentials stored
   securely outside the document, conditional PUT on `latest.json`, retries,
   conflict errors, and recorded receipts.
3. **Nostr adapter**: optional NIP-23 article / change announcement, profile
   identity, NIP-22 comments; use external signing providers rather than
   embedding secret keys in published documents. First improve Nostr reading,
   caching and identity discovery (`docs/NOSTR.md`).
4. **IPFS adapter**: CID publication and explicit pin status, optional IPNS.
5. **Git adapter** as another way to deploy the same directory structure.

Each adapter should accept a prepared immutable publication and return a
receipt. Failures must not roll back the local published revision. Multiple
destinations are valid for the same revision; retries must be idempotent.

## References

- [HTTP Extensions for Web Distributed Authoring and Versioning (RFC 4918)](https://www.rfc-editor.org/rfc/rfc4918)
- [IPFS content addressing](https://docs.ipfs.tech/concepts/content-addressing/)
- [IPNS](https://docs.ipfs.tech/concepts/ipns/), [pinning](https://docs.ipfs.tech/how-to/pin-files/)
- [NIP-23 long-form content](https://github.com/nostr-protocol/nips/blob/master/23.md)
- [NIP-22 comments](https://github.com/nostr-protocol/nips/blob/master/22.md)
- [NIP-01 basic protocol](https://github.com/nostr-protocol/nips/blob/master/01.md)
