# Nostr in xanatoo

Xanatoo can open **Nostr** notes as read-only discussion threads
(Thread / Message nodes). Network access uses public relays over WSS
(OpenSSL; build with `-DXANATOO_NOSTR=ON`, the default when OpenSSL is available).

## How to open a note

1. Press **Ctrl+L** (URL bar).
2. Paste one of:
   - `note1…` (NIP-19 event id)
   - `nevent1…` (event + optional relay hints)
   - `nostr:note1…` / `nostr:nevent1…`
   - a 64-character hex event id

The client asks a few public relays for the root event and kind-1
replies tagged with that id, then lays them out as nested messages.

## Example link

Try a well-known bootstrap note if you have network access (id may move
over time — replace with any `note1` from a client):

- Open the URL bar and paste a `note1…` from your Nostr client.

## Scope (v1)

- **Read-only** view of kind-1 text notes and replies
- No posting, no NIP-07 extension, no local key storage
- Signatures are not verified yet (display only)
- Relays: Damus, nos.lol, nostr.band, plus any `nevent` hints

## Why Thread / Message?

The document model already has discussion containers. Nostr maps cleanly:

| Nostr | xanatoo node |
|-------|----------------|
| Root note | `Thread` (subject from content) |
| kind 1 note | `Message` (author short hex, time, body) |
| `e` reply tag | `Message.alt` → parent id (indent in layout) |
