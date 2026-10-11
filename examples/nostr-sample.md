# Nostr in xanatoo

Xanatoo can open **Nostr** notes as read-only discussion threads
(Thread / Message nodes). Network access uses public relays over WSS
(OpenSSL; build with `-DXANATOO_NOSTR=ON`, the default when OpenSSL is available).

## Starting points (click a link)

These are real public notes. Click to fetch the thread from relays
(needs network). If a note is sparse on one relay, try **Reload**.

- [Jack Dorsey — 🫂](note1gfvpkhsz0nuc3pkstuh903farnnt0mv9uyqlz4rg596h9hcct25s0dmkud)
- [Jack Dorsey — parent of the above](note1j4d0fvelk2staurj3mtqmn8c8duksw3v52a02y2ufdn5242u7vfqt4vh5l)
- [nak CLI sample note](note1ttnnrw78wy0hs5fa59yj03yvcu2r4y0xetg9vh7uf4em39n604vsyp37f2)
- [nak “hello from the nostr army knife”](note12dzr2ph86z09twfz5gmfhq8eycq84z5236jlp803mdvluxvnxd0q867kyw)
- [franca — “q tags” joke](note14nrd2wck8v99kwccg93hphvng0exxvkq4dt0gfqr6f5e0sph2ups9de0cf)

Or paste any `note1…` / `nevent1…` into the location bar (**Ctrl+L**).

## How to open a note

1. Press **Ctrl+L** (URL bar).
2. Paste one of:
   - `note1…` (NIP-19 event id)
   - `nevent1…` (event + optional relay hints)
   - `nostr:note1…` / `nostr:nevent1…`
   - a 64-character hex event id

The client asks a few public relays for the root event and kind-1
replies tagged with that id, then lays them out as nested messages.

## Scope (v1)

- **Read-only** view of kind-1 text notes and replies
- Authors resolved via kind-0 profiles when relays provide them
- Multiple relays merged (hints from `nevent` preferred)
- No posting, no NIP-07 extension, no local key storage
- Signatures are not verified yet (display only)
- Relays: Damus, nos.lol, nostr.band, nostr.wine, plus `nevent` hints
- **Reload** (Ctrl+R) re-fetches the same note

## Why Thread / Message?

The document model already has discussion containers. Nostr maps cleanly:

| Nostr | xanatoo node |
|-------|----------------|
| Root note | `Thread` (subject from content) |
| kind 1 note | `Message` (author name / npub, time, body) |
| `e` reply tag | `Message.alt` → parent id (indent in layout) |
