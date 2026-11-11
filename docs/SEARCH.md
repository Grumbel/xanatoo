# Search

Search is an address, not a widget in the document and not a panel.

## Design

- **Address:** `search:<engine>:<query>`. The query is percent-encoded with
  `+` for blanks (`search.hpp`), so `#`, `|`, `//` and blanks never reach
  `locator.hpp`. `search_ref(engine, typed text)` builds it.
- **Fetch:** `fetch_page` expands the engine's URL template
  (`search_target`) and fetches that through the ordinary dispatch — feed,
  web page, Standard Ebooks list, native ref. The page keeps the `search:`
  address (`final_url`), so Back / Forward, bookmarks, tabs, split browsing
  and session restore need no search code. Results are read-only foreign
  documents like any remote page.
- **Search bar:** `ui.cpp` row `##toolbar_search`, hidden by default
  (`App::search.open`, Ctrl+K / View → Search Bar, Esc closes): an engine
  button with a dropdown (badge letters, no per-engine art yet), a field,
  Go. It only issues `UiCommand::Search`; `run_search` builds the address and
  navigates. The engine in use is `ViewSettings::search_engine` (view.conf).
- **Keywords:** Ctrl+L text `<keyword> <words>` becomes the same address
  (`search_from_location`), unless a file of that name exists.
- **Engines:** the table in `search.cpp` plus `user_data_dir()/search.sexp`
  (`(engine "id" (name …) (keyword …) (badge …) (url "…%s…") (kind feed)
  (space "+"))`; a built-in id is replaced, a new one appended). `kind` is
  `page` (default), `feed` or a JSON mapper (below).
- **No forms in documents.** A document can *link* to a search
  (`[x](search:arxiv:electron+spin)`); it cannot contain an input. That keeps
  the document format free of application state.

## Engines (checked 2026-11-03)

| Kind | Engines | Answer → document |
|------|---------|-------------------|
| `wikipedia` | Wikipedia en / de | `list=search` JSON → list with `wikipedia:` links (parentheses percent-encoded), matched words bold |
| `hn` | Hacker News | Algolia JSON → story link + `hn:item?id=` thread link |
| `archive` | Internet Archive | `advancedsearch … output=json`, most downloaded first → `ia:` links (`docs/ARCHIVE.md`) |
| `gutendex` | Project Gutenberg | Gutendex JSON → `gutenberg.org/ebooks/N` (opens as epub) |
| `lemmy` | Lemmy (lemmy.world) | `/api/v3/search` → `lemmy:host/post/N` |
| `bluesky` | Bluesky | `searchPosts` → one card per post, link to bsky.app |
| `page` | PeerTube (`pt`, sepiasearch), Odysee (`od`), Dailymotion (`dm`) | `peertube:` / `odysee:` / `dailymotion:` search refs (`docs/VIDEO_SITES.md`) |
| `feed` | arXiv, Reddit, Mastodon hashtag, Flickr tags | the feed path |
| `page` | YouTube (`youtube:results?…`, native InnerTube search), Standard Ebooks, Marginalia, Wiby, DuckDuckGo (html) | the web / ebook path |

## JSON mappers (`search_results.cpp`)

`search_results_markdown(plan, json)` is pure (no network, unit-tested on
canned answers): it writes Markdown and `parse_document` makes the
document, so a mapper is ~25 lines and results get every link feature for
free. `search_fetch` (HTTPS builds) fetches and maps. Rules learned:

- The parser reads **link text literally** (no escapes, no emphasis): only
  `[` / `]` are changed (to parentheses). Running text is escaped
  (`search_md_escape`: `\ ` * _ [ ]`, and `# - +` at a line start); `\<` is
  *not* unescaped, so `<` / `>` stay raw.
- Link destinations: `(` `)` and blanks are percent-encoded.
- The API's own fields can be arrays (Internet Archive `title`,
  `creator`, `description`): `first_string`.
- Wikipedia exact-title redirects are gone with the JSON engine: the first
  hit is the article. A mapper for a service without keys only; Mastodon's
  status search needs a login, so it stays a hashtag feed.

Tried and left out: Internet Archive `output=rss` ignores the query;
Stack Overflow answers with a "Human verification" page; Twitter /
Instagram / TikTok have nothing public.

## Open

Mastodon `/api/v2/search` for accounts / hashtags, Reddit `search.json`
(the feed is enough), per-engine icons, a toolbar toggle button, suggestions
while typing, "search this site" for the focused page's host, paging
("more results" links: Wikipedia `sroffset`, Algolia `page`, Lemmy `page`).
