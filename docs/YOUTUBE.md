# YouTube (InnerTube)

Read-only, local-only. Gated by `XANATOO_YOUTUBE` (default ON, needs a TLS
backend). Uses the JSON API that youtube.com's own pages call
(`https://www.youtube.com/youtubei/v1/{browse,next,navigation/resolve_url}`),
with a `WEB` client context and `Cookie: SOCS=CAI` (consent). No key, no
account, no posting. It is unofficial and can change without notice.

## Refs

| Ref | Shows |
|-----|-------|
| `youtube:@handle[?page=N]` | uploads grid (also `channel/UC…`, `c/name`, `user/name`) |
| `youtube:playlist?list=ID[&page=N]` | playlist items |
| `youtube:watch?v=ID[&comments=N][&sort=new]` | thumbnail, description, comments |
| `youtube:results?search_query=Q[&page=N]` | video search (`search` endpoint; also the `youtube` / `yt` engine of the search bar) |

`@handle`, `c/` and `user/` go through `navigation/resolve_url` first.
Plain `https://www.youtube.com/…` / `youtu.be/…` URLs open as web pages; the
"Native view" toggle maps them (`youtube_ref_from_web_url`).

## Paging

Continuation tokens are opaque and come from the previous reply. Page N
re-walks N-1 continuations (one request each); no token cache yet.
Grid chunks are about 30 videos, comment chunks about 20.
The first `continuationItemRenderer` of a browse reply is the grid's; the
header's engagement panels carry unrelated ones.

## Limits

- Comment replies are not fetched (counts only).
- Shorts / Live tabs and recommendations are not covered; search lists videos only (no channel / playlist hits, no filters).
- Replies are Message nodes; likes and reply counts are in the caption.

## Code

`yt_parse_*` (pure, JSON in) → `yt_listing_document` / `yt_video_document`
(Document out); `youtube_fetch` is the only networked entry point, called
from `app_fetch.cpp`. Tests: `tests/youtube_tests.cpp`.
