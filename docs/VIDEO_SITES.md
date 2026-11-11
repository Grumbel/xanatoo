# Video sites (PeerTube, Odysee, Dailymotion)

Read-only, local-only, like `youtube:` (`docs/YOUTUBE.md`). Gated by
`XANATOO_VIDEO` (default ON, needs a TLS backend; off in the minimal port).
Public unauthenticated JSON APIs only: no accounts, no posting.

Code: `include/xanatoo/video_sites.hpp`, `src/video_sites.cpp`. Parsers
(`pt_parse_*`, `od_parse_*`, `dm_parse_*`) and document builders
(`vs_listing_document`, `vs_video_document`) are pure and tested offline in
`tests/video_sites_tests.cpp`; only `video_site_fetch` needs the network.

## Refs

| Ref | Shows |
|-----|-------|
| `peertube:HOST[?page=N]` | recent videos of an instance |
| `peertube:HOST/c/NAME`, `/a/NAME` | channel / account header + videos |
| `peertube:HOST/w/ID[?comments=N&thread=ID]` | video, comments; `thread=` shows one thread's replies |
| `peertube:HOST/search?search=Q` | instance search; `sepiasearch.org` searches the fediverse |
| `odysee:@chan:8[?page=N]` | channel header + uploads |
| `odysee:@chan:8/slug:e[?comments=N&thread=ID]` | video, comments (top sort), replies |
| `odysee:search?s=Q` | search (lighthouse → claim_search) |
| `dailymotion:xID` | video, comments |
| `dailymotion:user/ID`, `dailymotion:search?search=Q` | channel videos / search |

Search-bar engines: `peertube` (`pt`), `odysee` (`od`), `dailymotion` (`dm`).
Video pages carry "Watch on X" (`external:URL`) and "Watch with mpv"
(`external:mpv:URL`) links. A PeerTube video's ref uses the host it really
lives on (search results can span instances).

Web URLs map through `video_site_ref_from_web_url` (native toggle):
`odysee.com/@…`, `dailymotion.com/video/xID`, `dai.ly`, PeerTube `/w/ID`,
`/videos/watch/ID`, `/video-channels/NAME`, `/accounts/NAME`. Other PeerTube
paths are not claimed (an arbitrary `/w/…` is MediaWiki).

## Limits

- Dailymotion comments are untested against a video that has comments.
- Odysee comments: top sort only. Odysee `lbry://@a#8/b#e` is `@a:8/b:e`.
- Not done: Piped, Rumble, Vimeo, Bilibili.
