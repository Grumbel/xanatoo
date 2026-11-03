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
  (`App::search.open`, Ctrl+Alt+K / View → Search Bar, Esc closes): an engine
  button with a dropdown (badge letters, no per-engine art yet), a field,
  Go. It only issues `UiCommand::Search`; `run_search` builds the address and
  navigates. The engine in use is `ViewSettings::search_engine` (view.conf).
- **Keywords:** Ctrl+L text `<keyword> <words>` becomes the same address
  (`search_from_location`), unless a file of that name exists.
- **Engines:** the table in `search.cpp` plus `user_data_dir()/search.sexp`
  (`(engine "id" (name …) (keyword …) (badge …) (url "…%s…") (kind feed)
  (space "+"))`; a built-in id is replaced, a new one appended).
- **No forms in documents.** A document can *link* to a search
  (`[x](search:arxiv:electron+spin)`); it cannot contain an input. That keeps
  the document format free of application state.

## Engines checked on 2026-11-03

| Engine | Returns | Notes |
|--------|---------|-------|
| Wikipedia, Wikipedia (de) | HTML | exact title → the article; otherwise the result list. Heavy page chrome: F9 |
| arXiv | Atom | blanks become `+AND+all:` |
| Standard Ebooks, Gutenberg | HTML | list pages |
| Reddit | Atom | rate-limited (429) on bursts |
| Mastodon hashtag, Flickr tags | RSS | one tag / comma list, so `space` is `""` / `","` |
| Marginalia, Wiby, DuckDuckGo (html) | HTML | |

Tried and left out: Internet Archive `advancedsearch … output=rss` ignores
the query (returns recent additions); Stack Overflow answers with a
"Human verification" page; Twitter / Instagram / TikTok have nothing public.

## Next: JSON engines → native result documents

Needs a result kind next to `feed` (a mapper per service, built on
`json_min.hpp` plus a small array reader): Wikipedia `action=opensearch` /
`list=search` (results as `wikipedia:` links, no page chrome), Internet
Archive `advancedsearch.php?output=json`, Hacker News Algolia
(`hn:item?id=` links), Bluesky `searchPosts`, Mastodon `/api/v2/search`,
Lemmy `/api/v3/search`, Reddit `/search.json`, Gutendex.
Also open: per-engine icons, a toolbar toggle button, suggestions while
typing, "search this site" for the focused page's host.
