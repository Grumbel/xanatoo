# RFC discussion: a typed include protocol for live documents

:author: xanatoo examples
:lang: en

This document mixes ordinary exposition with a **threaded discussion** that
could as easily have been a mailing-list archive or a forum topic.  Messages
carry author, date, stable ids, and optional `in-reply-to` links; layout
indents replies by walking that chain.

Jump: [Background](#Background) · [Thread](#thread-include-protocol) · [Open questions](#Open-questions)

## Background

Transclusion (`:::include`) already embeds a passage by label or by path.
Several contributors asked whether includes should also carry *provenance*
(who pointed at what, and when) without turning every embed into a free-form
note.  The proposal below was workshopped in-thread.

:::note kind=tip
Message ids are ordinary labels: `[see Alice's post](#m-alice-1)` jumps to a
message the same way `#Heading` jumps to a heading.
:::

## Design goals

1. **Stable identity** — every post has an `id` that survives reordering and export.
2. **Threading without nested fences** — replies point at a parent id; depth is derived.
3. **Passage comments** — a message may `about="#SomeHeading"` instead of replying to a person.
4. **Round-trip** — markdown fences and the S-expression form preserve the same fields.

:::thread id="thread-include-protocol" subject="RFC: typed include protocol for live documents"

:::message id="m-alice-1" from="Alice Example <alice@example.org>" date="2026-02-28T09:14:00Z"
Colleagues,

I have been using `:::include other.md#API` across a small set of handbooks
and keep wanting two things the fence does not yet say:

1. Whether the include is a **citation** (read-only pointer) or a **live edit
   surface** (write-through, same-document only today).
2. A short **relation** — `glossary`, `normative`, `example` — so a reader
   can filter the graph later.

Straw-man fence:

```
:::include other.md#API rel=normative mode=cite
:::
```

Thoughts?
:::

:::message id="m-bob-1" from="Bob Rivera <bob@example.org>" date="2026-02-28T10:02:11Z" in-reply-to="m-alice-1"
Alice — mode=cite vs write-through maps cleanly onto what the editor already
does internally for same-doc vs cross-doc includes.  I would keep *mode* out
of the path and on the node, yes.

On `rel`: we already have typed links (`:rel` on inline links).  Reusing the
same vocabulary for includes would let one query answer “everything that
claims to be normative about X”.
:::

:::message id="m-cara-1" from="Cara Nguyen <cara@example.org>" date="2026-02-28T11:40:33Z" in-reply-to="m-alice-1"
Please do **not** overload the include fence with UI policy.  Cite vs edit is
a *view* concern; the document should only record the target and an optional
relation.

Also, mailing-list habits die hard: if this RFC is archived as a thread of
messages, the first post should remain readable without the editor.  Plain
paragraphs under `:::message` already give us that.
:::

:::message id="m-alice-2" from="Alice Example <alice@example.org>" date="2026-02-28T14:05:00Z" in-reply-to="m-cara-1"
Cara's split is right.  Revised straw-man — document side only:

```
:::include other.md#API rel=normative
:::
```

Editor chrome can still offer “open target for edit” without a `mode=` bit
in the file.
:::

:::message id="m-dev-1" from="Dev Singh <dev@example.org>" date="2026-03-01T08:22:47Z" in-reply-to="m-bob-1"
Bob, if `rel` is shared with links, please document the closed set early.
I am fine with an open string, but then exporters to HTML will need a
convention (class name? `data-rel`?).

Patch sketch for the schema:

- Message / Thread nodes as discussed in FORMAT.md
- Include node grows an optional `rel` string (store beside excerpt)
:::

:::message id="m-bob-2" from="Bob Rivera <bob@example.org>" date="2026-03-01T09:01:15Z" in-reply-to="m-dev-1"
Dev: start with an *open* string; publish a recommended vocabulary in
FORMAT.md without rejecting unknown values.  HTML export can emit
`data-rel="…"` and a weak class `rel-normative` when the token is safe.
:::

:::message id="m-cara-2" from="Cara Nguyen <cara@example.org>" date="2026-03-01T12:18:00Z" about="#Design-goals"
Commenting on the design goals rather than on a person: goal (3) “passage
comments” is what this message is doing with `about="#Design-goals"`.  That
is the generalisation — a message is a comment that may target either
another message (`in-reply-to`) or any labelled passage (`about`).
:::

:::message id="m-alice-3" from="Alice Example <alice@example.org>" date="2026-03-01T15:44:02Z" in-reply-to="m-cara-2"
Yes — that is the shape I want.  Thread is only a *bag* with a subject;
ordering is document order; threading edges are explicit ids.  No forced
tree nesting in the markup, so cut/paste of a single message stays valid.
:::

:::message id="m-erin-1" from="Erin Okafor <erin@example.org>" date="2026-03-02T07:55:30Z" in-reply-to="m-alice-1"
Late to the party.  From a moderation angle, please keep `from` as a free
display string.  Do not require email.  Archives that redact addresses
should still parse.

Also +1 to Cara on not baking edit policy into the file.
:::

:::message id="m-bob-3" from="Bob Rivera <bob@example.org>" date="2026-03-02T10:11:09Z" in-reply-to="m-erin-1"
Agreed on free-form `from`.  Date should prefer ISO-8601 so sorting tools
do not invent locales; display can pretty-print.
:::

:::message id="m-dev-2" from="Dev Singh <dev@example.org>" date="2026-03-02T16:30:00Z" in-reply-to="m-alice-3"
Implementation note for xanatoo: layout already indents by walking
`in-reply-to` → label.  Cap depth at something small (8) so a mistaken
cycle cannot push text off-screen.
:::

:::message id="m-alice-4" from="Alice Example <alice@example.org>" date="2026-03-03T09:00:00Z" in-reply-to="m-dev-2"
Then I think we have rough consensus:

| Topic | Decision |
|-------|----------|
| Include `rel` | Optional open string |
| Include `mode` | Not in the file |
| Message `from` | Free display string |
| Message `date` | ISO-8601 preferred |
| Threading | `in-reply-to` / `about` ids |
| Nesting | Flat list under `:::thread` |

I will send a follow-up patch series for FORMAT.md and the parser unless
someone objects by Friday.
:::

:::message id="m-cara-3" from="Cara Nguyen <cara@example.org>" date="2026-03-03T11:20:18Z" in-reply-to="m-alice-4"
No objection.  Please add one example that mixes a thread with ordinary
headings so people see includes and messages side by side — like this file.
:::

:::

## Open questions

- Should cross-*document* `in-reply-to` be allowed (`other.md#m1`), or only
  same-document labels for v1?
- Do we need moderation fields (`edited`, `deleted-tombstone`) as first-class
  attributes, or is a follow-up message enough?
- Export to mbox / ActivityStreams is explicitly out of scope for the first
  cut, but the ids are chosen so a later exporter can map them.

## S-expression form (same thread, abbreviated)

```sexp
(thread :id "thread-include-protocol"
        :subject "RFC: typed include protocol for live documents"
  (message :id "m-alice-1"
           :from "Alice Example <alice@example.org>"
           :date "2026-02-28T09:14:00Z"
    (p "…"))
  (message :id "m-bob-1"
           :from "Bob Rivera <bob@example.org>"
           :date "2026-02-28T10:02:11Z"
           :in-reply-to "m-alice-1"
    (p "…")))
```

