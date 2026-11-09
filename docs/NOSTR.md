<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# Nostr in Xanatoo

## Goal

Move from retrieve-and-forget note viewing to a local, searchable catalogue
of encountered events, profiles, public keys, relay observations, and
eventual user-controlled identities. **It is a partial local observation, not
a directory of every Nostr user or post.** Relay queries return a subset of
the network, may omit old events, and can include spam and invalid signatures.

## Local storage

`state_dir()/nostr.sqlite` holds distinct events (event ID), encountered
public keys (even without profile metadata), event-to-relay observations, and
local identity aliases. Fetching the same event twice must not increment its
post count. Save all event kinds that a query actually downloads, including
kind-0 profiles fetched to render a note. Track first/last observation and
the newest kind-0 profile per key. Index pubkeys from event authors, `p`
tags and NIP-19 mentions. Cache lookups/search operate without a relay.

Privacy/security: local cache may contain personal data and could grow
indefinitely; future releases need size/export/clear controls. Only treat
profile metadata as *claimed* names until event IDs and Schnorr signatures
are validated. NIP-05 identifiers additionally require remote verification.
The current relay reader does not verify Schnorr signatures. Never treat
profile metadata as verified identity merely because it is stored locally.
`--no-save` must disable all persistent cache and identity writes.

## Two native dockable panels

### Nostr explorer

- Search cached authors and notes; show total unique events, note count,
  public keys and observed profiles. Display per-author *locally observed*
  note count and last-seen time; no claim of a network-wide total.
- Click an author to navigate to `nostr:npub1…`; click a note to its thread.
- Show relay coverage, kinds, hashtags, follow lists, bookmarks, communities,
  and other locally observed structured events when supported.
- Add active fetch/search controls separately; local search should remain
  functional offline. NIP-50 relay search is optional and capability-gated.

### Nostr accounts

- Multiple labelled local identities, each with a public key, explicit
  signer association and separate follows/bookmarks/publishing preferences.
- Watch-only identities are useful immediately. They **cannot sign** and
  must never be presented as posting-capable accounts.
- For creation/import of usable signing identities, prefer NIP-46 remote
  signers or password-encrypted NIP-49 `ncryptsec` storage with deliberate
  backup/recovery UI. Do **not** silently store plaintext `nsec` values in
  config or SQLite. Key generation requires audited secp256k1/BIP-340
  operations and a secure signer/key-store design.
- Relay preferences (NIP-65), profile editing (kind 0), follow list (kind 3),
  private messaging and publication come **after** signing works.

## Groups, channels, forums and categorization

Nostr has several mechanisms, but **no universal subreddit registry or
hierarchy**:

| Mechanism | Event kinds | Role / limitations |
|---|---|---|
| NIP-29 relay-based groups | `h` group tags, `39000` group metadata, `9` group chat, `9000`-series moderation | Most appropriate current group model. Relay controls access and membership; optional parent/child groups are relay-enforced, not a global hierarchy. |
| NIP-72 moderated communities | `34550` community definition, `4550` approvals, `1111` comments | Reddit-like, but currently marked *unrecommended* in favor of NIP-29. Support reading legacy communities, not make this the new foundation. |
| NIP-28 public chat channels | `40` channel, `41` metadata, `42` messages | Older public group chat; also marked *unrecommended* in favor of NIP-29. |
| NIP-51 user lists | `10009` joined NIP-29 groups, `10005` public chats, `10015` interests, follow sets | Personal categorization, not an authoritative network taxonomy. |
| Hashtags / topic tags | `t` tags on ordinary events | Loose cross-relay topics; no moderation/ownership. |
| NIP-23 articles / NIP-22 comments | `30023` articles, `1111` comments | Articles and threaded discussions, not communities by themselves. |

NIP-29 group identity is **group ID plus enforcing relay**; a group ID alone
is not globally unique. Discovery should show both and retain provenance.
Private groups/messages and NIP-42 authenticated relays require appropriate
credentials, encryption and consent before being fetched or stored.

References: [NIP-29](https://github.com/nostr-protocol/nips/blob/master/29.md),
[NIP-72](https://github.com/nostr-protocol/nips/blob/master/72.md),
[NIP-28](https://github.com/nostr-protocol/nips/blob/master/28.md),
[NIP-51](https://github.com/nostr-protocol/nips/blob/master/51.md),
[NIP-50](https://github.com/nostr-protocol/nips/blob/master/50.md),
[NIP-46](https://github.com/nostr-protocol/nips/blob/master/46.md),
[NIP-49](https://github.com/nostr-protocol/nips/blob/master/49.md).
