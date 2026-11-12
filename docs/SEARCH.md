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
- **Location bar button:** a magnifier beside the address when an engine
  searches the focused page's service (`engine_for_address`: the engine of a
  `search:` address, else the longest `sites` entry, an address prefix like
  `hn:` or a host with its subdomains). It issues `UiCommand::SearchSite`:
  `search_site` shows the bar with that engine (and makes it the engine in
  use); while the bar shows that engine it is latched and a click hides it.
- **Keywords:** Ctrl+L text `<keyword> <words>` becomes the same address
  (`search_from_location`), unless a file of that name exists.
- **Engines:** the table in `search.cpp` plus `user_data_dir()/search.sexp`
  (`(engine "id" (name …) (keyword …) (badge …) (url "…%s…") (kind feed)
  (space "+") (site "host" "ref:"))`; a built-in id is replaced, a new one appended). `kind` is
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
while typing, "search this site" for hosts no engine covers (a site's own
OpenSearch description), paging
("more results" links: Wikipedia `sroffset`, Algolia `page`, Lemmy `page`).

## Published-media catalogs

The built-in search-engine menu now includes public JSON catalogs:

| Search address | Keyword | Results | Record navigation |
|---|---|---|---|
| `search:openlibrary:dune` | `ol` | Books and works | `media:openlibrary:OL…W` |
| `search:tvmaze:fringe` | `tv` | Television shows | `media:tvmaze:<numeric-id>` |
| `search:musicbrainz:beatles` | `mb` | Music release groups | `media:musicbrainz:<uuid>` |
| `search:wikidata:xanadu` | `wdt` | Cross-media entities | `media:wikidata:Q<number>` |
| `search:openalex:hypertext` | `oa` | Academic works | `media:openalex:W<number>` |
| `search:crossref:hypertext` | `cr` | DOI publications | `https://doi.org/…` |

`media:<source>:<identifier>` is a native, bookmarkable document address.
The fetcher only expands known sources and strictly validated identifiers into
HTTPS API URLs, then renders a compact record with a link to its original page.
It never permits arbitrary remote hosts in `media:` addresses.

The existing Internet Archive and Gutenberg engines remain available. These
new services need no API keys. Provider terms and rate limits still apply;
MusicBrainz in particular asks clients to limit request rates. Credentials,
noncommercial licenses, bulk download/import and API-only services (IGDB,
TMDB, MobyGames, Discogs, etc.) are *not* automatically enabled by this change.

All providers go through the existing configured search engine, history,
bookmark and HTTP infrastructure. Custom search engines still live in
`search.sexp`. HTTPS-disabled minimal builds report that remote media records
are unavailable, instead of trying a local file or unsafe network fallback.
