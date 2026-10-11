# Nostr in xanatoo

Xanatoo can open **Nostr** notes as read-only discussion threads
(Thread / Message nodes). Network access uses public relays over WSS
(OpenSSL; build with `-DXANATOO_NOSTR=ON`, the default when OpenSSL is available).

## Try this first

Specific old `note1` links often **age off** public relays. For a reliable demo:

- [Recent public notes (firehose)](nostr:recent) — fetches the latest kind-1 notes

Or press **Ctrl+L** and type `nostr:recent`.

## Specific notes

Paste any `note1…` / `nevent1…` from a Nostr client into **Ctrl+L**.
Clickable examples only work while relays still store that event:

- [Example note (may expire)](note1h66lenndtaw5zjcntqxafc0254s7dgl8e7e66sty6jswczg63swq6njy8g)

If you see *note not found on N relay(s)*, the id is rare or gone — use **nostr:recent** instead.

## How to open a note

1. Press **Ctrl+L** (URL bar).
2. Paste one of:
   - `nostr:recent` — live sample of public notes
   - `note1…` (NIP-19 event id)
   - `nevent1…` (event + optional relay hints)
   - `nostr:note1…` / 64-character hex event id

The client queries several public relays (nos.lol, damus, primal, …),
merges results, and lays out Thread / Message nodes.

## Scope (v1)

- **Read-only** view of kind-1 text notes and replies
- Authors resolved via kind-0 profiles when relays provide them
- Multiple relays merged (hints from `nevent` preferred)
- No posting, no NIP-07 extension, no local key storage
- Signatures are not verified yet (display only)
- **Reload** (Ctrl+R) re-fetches the same ref

## Why Thread / Message?

| Nostr | xanatoo node |
|-------|----------------|
| Root note | `Thread` (subject from content) |
| kind 1 note | `Message` (author name / npub, time, body) |
| `e` reply tag | `Message.alt` → parent id (indent in layout) |
