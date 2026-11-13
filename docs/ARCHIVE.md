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

- Reviews, the full-text viewer and uploads are not supported.
- Items with restricted access (`is_dark`, lending-only books) only show what
  the metadata API reveals; the files may not download.

## Wayback Machine (`wayback:`)

Old versions of ordinary web pages (`src/wayback.cpp`, `wayback.hpp`; needs
`XANATOO_WEB`):

- `wayback:URL` is the nearest snapshot, `wayback:20150301/URL` the one nearest
  to a date (4–14 digits). The fetch is the ordinary web page reader on
  `https://web.archive.org/web/<ts>id_/URL` (`id_` = as archived, without the
  Wayback toolbar). The Wayback redirect supplies the real capture time; the
  pane address becomes that snapshot URL, which View → Live Page maps back.
- View → **Old Version (archive.org)** opens the focused http(s) page's
  snapshot; on a snapshot the same entry reads **Live Page**.
- A page that fails with 404 / 410 / 5xx gets a "Look for an old version at
  archive.org" link in its error banner.
- Links inside a snapshot are the archived page's own (absolute links go to the
  live web; relative ones stay inside the archive).
- No availability-API lookup yet: the redirect already picks the nearest
  capture; a page without captures shows the archive's 404.
- Not CORS-open: `web.archive.org` snapshots send no `Access-Control-Allow-Origin`
  (only the `archive.org/wayback/available` JSON does), so this feature will
  need a proxy in the WebAssembly build.
