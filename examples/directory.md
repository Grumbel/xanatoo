# xanatoo Directory

A small old-school link directory in the spirit of early Yahoo!: pick a
category, follow a link. Everything here opens **in the app** — feeds as
message cards, sites as reader-mode pages, `hn:` / `reddit:` / `lemmy:` /
`wikipedia:` in their native views. Every feed link was fetched and returned
entries when this list was written; feeds come and go, so a dead one is a
bug in the list, not in the app.

Add your own with **Ctrl+Shift+B** (bookmark) or edit these pages: they are
plain Markdown files under `examples/directory/`.

## Categories

- [Social and the Fediverse](directory/social.md) — Mastodon, Bluesky, Lemmy, Reddit, Tumblr, Pixelfed, Flickr, Pinterest, Telegram. And what Twitter, Instagram and TikTok do not offer.
- [Science fiction and fantasy](directory/scifi.md) — magazines, podcasts, news, reference sites, books
- [Tech and computing](directory/tech.md) — tech news, Linux and free software, programming, security, hardware, German tech press
- [Science and space](directory/science.md) — journals, space agencies, astronomy, preprints
- [Culture, design and long reads](directory/culture.md) — essays, art, design, history, music
- [Games and comics](directory/games.md) — game news, devlogs, webcomics
- [News](directory/news.md) — world news in English, German, French, Spanish, Italian and Japanese; text-only editions
- [Video and podcasts](directory/media.md) — YouTube channels as feeds, audio fiction
- [Search](directory/search.md) — engines and keywords
- [Libraries, archives and the old web](directory/reference.md) — Internet Archive, Gutenberg, Wikimedia, RFCs, indexes of small sites

## Built-in services

Native views (no HTML scraping) — see also [Books](books.md) and
[Feeds](feeds.md):

- [Hacker News](hn:) · [new](hn:newest) · [Ask HN](hn:ask) · [Show HN](hn:show)
- [Reddit: r/popular](reddit:r/popular/) · [r/scifi](reddit:r/scifi/) · [r/printSF](reddit:r/printSF/) · [r/space](reddit:r/space/) · [r/programming](reddit:r/programming/)
- [Lemmy: technology](lemmy:lemmy.world/c/technology) · [space](lemmy:lemmy.world/c/space) · [Linux](lemmy:lemmy.ml/c/linux) · [Rust](lemmy:programming.dev/c/rust)
- [Wikipedia](wikipedia:Main_Page) · [Wikipedia (Deutsch)](wikipedia:de:Wikipedia:Hauptseite) · [Science fiction](wikipedia:Science_fiction)
- [Nostr: recent notes](nostr:recent)
- [Gemini: geminiprotocol.net](gemini://geminiprotocol.net/)
- [Standard Ebooks](https://standardebooks.org/ebooks) · [Project Gutenberg today](https://www.gutenberg.org/cache/epub/feeds/today.rss)

## Open any feed yourself

**Ctrl+L** takes a feed URL, or `feed:https://site/` to find the feed of
any page. Patterns that work on many sites:

| Where | Feed URL |
|-------|----------|
| Mastodon user | `https://instance/@name.rss` |
| Mastodon hashtag | `https://instance/tags/name.rss` |
| Bluesky user | `https://bsky.app/profile/handle/rss` |
| Tumblr blog | `https://name.tumblr.com/rss` |
| YouTube channel | `https://www.youtube.com/feeds/videos.xml?channel_id=UC…` |
| Medium user / tag | `https://medium.com/feed/@name` · `https://medium.com/feed/tag/name` |
| Substack | `https://name.substack.com/feed` |
| GitHub user / releases / commits | `https://github.com/user.atom` · `…/repo/releases.atom` · `…/repo/commits.atom` |
| Reddit | `https://www.reddit.com/r/name/.rss` |
| Lemmy community | `https://instance/feeds/c/name.xml` |
| Stack Exchange site | `https://site.stackexchange.com/feeds` |
| Flickr tag | `https://www.flickr.com/services/feeds/photos_public.gne?tags=name&format=rss2` |
| Pixelfed user | `https://instance/users/name.atom` |
| DeviantArt search | `https://backend.deviantart.com/rss.xml?q=term` |
| Pinterest user | `https://www.pinterest.com/name/feed.rss` |
| Wikipedia / Commons feature feeds | `…/w/api.php?action=featuredfeed&feed=potd&feedformat=atom` |
| arXiv category | `https://rss.arxiv.org/rss/cs.AI` |
| Internet Archive collection | `https://archive.org/services/collection-rss.php?collection=name` |

## Search

**Ctrl+K** (View → Search Bar) shows the search bar: pick the engine with
the button at its left, type, **Enter**. In the location bar (**Ctrl+L**) a
keyword does the same: `w xanadu`, `ax electron spin`, `se wells`. Results
are ordinary pages with the address `search:<engine>:<words>`, so Back,
bookmarks and links work. [More about search](directory/search.md).
