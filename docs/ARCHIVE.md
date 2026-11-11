# Internet Archive

archive.org renders its pages with JavaScript, so they cannot be shown as web
pages. `ia:` refs read the public JSON APIs instead: read-only, local-only, no
account. Needs a network build (`XANATOO_HTTPS`).

Code: `include/xanatoo/archive.hpp`, `src/archive.cpp`. Ref parsing and the
JSON → Markdown mapping (`ia_markdown`) are pure and tested offline in
`tests/archive_tests.cpp`; only `ia_fetch` touches the network.

| Ref | Shows |
|-----|-------|
| `ia:ID` | item: thumbnail, details (creator, date, language, collections, subjects, rights), description, files |
| `ia:ID?page=N` | page N (50 items, most downloaded first) of a collection's members |

- Data: `https://archive.org/metadata/ID` (item) and `advancedsearch.php?q=collection:ID`
  (members of a collection; only fetched when `mediatype` is `collection`).
- Files link to `archive.org/download/ID/NAME`; bookkeeping files (`*_meta.xml`,
  `*_files.xml`, torrents, item tiles) are left out, originals come first, at
  most 100 are listed. PDFs / EPUBs / images route through the usual
  download / viewer handling.
- Movies and audio get a "Watch / listen with mpv" link (`external:mpv:URL`).
- Web URLs `archive.org/details|download|stream|embed/ID` map to `ia:ID` (native
  toggle); search, account and Wayback pages stay web pages.
- The `ia` search engine's results link to `ia:ID`.

## Limits

- Reviews, the full-text viewer, Wayback Machine (`web.archive.org`) and
  uploads are not supported.
- Items with restricted access (`is_dark`, lending-only books) only show what
  the metadata API reveals; the files may not download.
