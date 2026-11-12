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

### Expanded catalog engines (2026-11-12)

These are native search-result documents, not web-page scraping; clicking a
result opens a validated `media:<source>:<id>` address. Search fetches the
source's query API; clicking a record fetches its detail API. No remote data
is bulk-imported.

| Media | Search engine (`search:<id>:…`) | Native record | Authentication |
|---|---|---|---|
| Video games | `steam` | `media:steam:<appid>` | None; Steam **undocumented** store JSON endpoint, limited to its small search result set |
| Video games | `igdb` | `media:igdb:<id>` | Twitch Client ID + OAuth app access token |
| Video games | `rawg` | `media:rawg:<id>` | RAWG API key; noncommercial free tier and attribution |
| Interactive fiction | `ifdb` | `media:ifdb:<tuid>` | None; IFDB requests low-volume use |
| Books | `googlebooks` | `media:googlebooks:<volume-id>` | None for the public search endpoint, quota dependent |
| Books/works | `bookbrainz-work`, `bookbrainz-edition` | `media:bookbrainz-work:<uuid>` / `media:bookbrainz-edition:<uuid>` | None, alpha API |
| Movies, TV | `tmdb-movie`, `tmdb-tv` | `media:tmdb-movie:<id>` / `media:tmdb-tv:<id>` | TMDB API key or read token |
| Anime, manga | `anilist-anime`, `anilist-manga` | `media:anilist-anime:<id>` / `media:anilist-manga:<id>` | None; POST GraphQL, rate limited |
| Periodicals | `zdb` | `media:zdb:<ZDB-ID>` | None; serial **titles**, not every issue |
| Comics | `gcd` | `media:gcd:<series-id>` | Anonymous test quota; may require account in future |
| Historic books, papers, film, music | `loc-books`, `loc-newspapers`, `loc-films`, `loc-music` | `media:loc:<item-id>` | None; digitized Library of Congress holdings, not the whole bibliographic catalog |
| Music releases | `discogs` | `media:discogs:<release-id>` | Discogs personal token |

Existing `openlibrary`, `tvmaze`, `musicbrainz`, `wikidata`, `openalex`,
`crossref`, `archive`, and `gutenberg` remain unchanged. This expands search
*and* linked record browsing within the existing toolbar and address bar.

#### Optional credentials

Use **Edit → Credentials…** (also linked from Preferences) to enter,
replace or remove API keys for IGDB, RAWG, TMDB and Discogs. The same dialog
manages Reddit OAuth client credentials. Saved catalog credentials are stored
as plain text in `config_dir()/catalog-credentials.conf`, created with `0600`
permissions on Unix and atomically replaced. The Reddit pair stays in
`config_dir()/reddit.conf` for compatibility. Saving is disabled with
`--no-save`. The dialog masks keys by default, supports showing individual
values and opens the providers' official credential pages. Empty a field and
click **Save catalog keys** to remove an individual value.

Environment variables remain supported and override saved values. Only
provide variables required by services you actually use. Examples:

```sh
export XANATOO_TMDB_READ_TOKEN='…'       # alternatively: XANATOO_TMDB_API_KEY
export XANATOO_RAWG_API_KEY='…'
export XANATOO_IGDB_CLIENT_ID='…'
export XANATOO_IGDB_ACCESS_TOKEN='…'     # short-lived Twitch app OAuth token
export XANATOO_DISCOGS_TOKEN='…'
nix run .#xanatoo
```

Credentials are **not** included in `search:` or `media:` URLs, history,
bookmarks or serialized sessions. For a credentialed provider, the fetch path
only sends authentication to an exact built-in API host, over HTTPS, and
**does not follow redirects**. API keys requiring a query argument are
inserted only for the outgoing request. Twitch OAuth tokens expire and must be
replaced when invalid; this implementation does not perform automatic OAuth
login. Do not store these values in Nix store strings or a world-readable
configuration file. The local files are **not encrypted**, so protect
backups and avoid copying them into repositories.

The global search engine dropdown is intentionally also the provider selector;
no independent media search panel or giant local replica is introduced.

Examples: `search:steam:portal`, `search:ifdb:anchorhead`,
`search:loc-newspapers:space`, `search:zdb:c64`, `search:gcd:batman`,
`search:tmdb-movie:alien` (requires token), `search:anilist-manga:akira`.

#### Boundaries and follow-on work

The client currently offers user-initiated lookup only, not background
crawling, pagination or data synchronization. Provider rate limits still
apply. The `gcd` integration searches **series**, not individual issues. ZDB
covers **periodical titles**, not issue-level page scans. Steam's store API is
undocumented and may change. The GCD API allows only a small anonymous testing
quota; do not use it for a bulk mirror. Most record views intentionally show a
brief summary and link to the authoritative source rather than reconstructing
all nested provider data.

IGDB, TMDB, RAWG and Discogs have access or licensing restrictions; no keys are
bundled. MobyGames is transitioning from API v1 to v2, while Comic Vine and
Metron require credentials. GOGDB, IMDb, Libretro, Open Library, MusicBrainz,
and ZDB *bulk data* need a separate import/index subsystem rather than an
interactive query mapper. These sources are **not** silently scraped and no
local mirror is claimed here.

Source specs: https://api-docs.igdb.com/ ; https://rawg.io/apidocs ;
https://developer.themoviedb.org/docs/authentication-application ;
https://docs.anilist.co/guide/graphql/ ;
https://zeitschriftendatenbank.de/services/schnittstellen/json-api ;
https://www.comics.org/api/ ; https://ifdb.org/api/ ;
https://www.loc.gov/apis/json-and-yaml/ ; https://api.bookbrainz.org/1/docs/ .
