# Subscriptions

Subscriptions watch changing collections, unlike **Bookmarks**, which mark a
specific page, passage or reading position. They are read-only: no account,
follow API, notification server or posting rights are required.

## Using the panel

- **Panels → Subscriptions** opens a native, dockable and resizable panel.
- **Subscribe to current page** watches the currently open feed, community or
  user profile; alternatively paste a source address and choose **Add
  subscription**. The Bookmarks menu offers the current-source action too.
- **Open** navigates to the watched source and clears its unread count.
- In **Lemmy Explorer** and **Nostr Explorer**, right-click a discovered
  community/person or Nostr author/group and choose **Subscribe**.
- **Refresh** schedules an immediate check of that source; **Refresh all**
  queues all sources. Refresh requests do not replace the foreground page.
- **Mark read** clears the count without navigating. **Remove…** asks for
  confirmation. Choose a check interval of 5 minutes to 24 hours per source.

Supported sources, when the corresponding build feature is available:

| Source | Example |
|---|---|
| Lemmy community | `lemmy:lemmy.world/c/technology` |
| Lemmy user | `lemmy:lemmy.world/u/username` |
| Nostr author | `nostr:npub1…` or `nostr:nprofile1…` |
| Nostr group/recent | A `nostr:group@…` link or `nostr:recent` |
| RSS/Atom | `https://example.org/feed.xml` |
| Reddit/Hacker News lists | `reddit:r/linux/`, `hn:news` |

Ordinary Lemmy web URLs for communities/users are converted to native Lemmy
addresses on addition. Single posts or individual Nostr notes are not changing
collections and cannot be subscribed to. Unsupported sources show an error.

## Change detection and scheduling

The first successful check establishes a **baseline** (existing items are not
marked new). Subsequent checks compare the last two snapshots of stable
message IDs / post permalinks, not their mutable score, summary or displayed
order. New IDs increment the **new** counter, which stays until the user
opens or marks the subscription read. For feed items without permalinks the
visible content is the fallback identity. This is a rolling comparison of up
to 256 items, not a complete history or a guarantee that items were seen by
all federated servers.

Automatic checks are due from the time of the previous check, with at most
two workers checking subscriptions concurrently and an automatic start
throttle of one source per five seconds. Manual requests take priority and
are queued if both slots are occupied. Network errors do not clear unread
counts or the last successful snapshot; the panel shows the error and the
source can be retried. When the application is closed no checks occur; due
checks resume after launch. A change to the computer's wall clock does not
stall checks indefinitely.

Subscriptions and their unread counts, last check times and rolling item IDs
are persisted atomically in `state_dir()/subscriptions.txt`. `--no-save` keeps
them in memory only; no credentials are stored. There is a cap of 1024 watched
sources. No system desktop notifications are currently sent: new content is
indicated in the panel.
