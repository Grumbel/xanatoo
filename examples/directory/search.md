# Search

[← Directory](../directory.md)

**Ctrl+K** (View → Search Bar) opens the search bar under the toolbar,
**Esc** closes it. The button at its left picks the engine; the choice is
remembered. **Ctrl+L** accepts `<keyword> <words>` as well.

| Keyword | Engine | Results |
|---------|--------|---------|
| `w` · `wd` | Wikipedia · Wikipedia (Deutsch) | article list with the matched words in bold; a click opens the native `wikipedia:` article |
| `hn` | Hacker News | stories with points, link to the story and to the `hn:` comment thread |
| `ia` | Internet Archive | items and collections, most downloaded first |
| `pg` | Project Gutenberg | books by download count; a click downloads and opens the book |
| `l` | Lemmy (lemmy.world) | posts that open as native `lemmy:` threads |
| `bs` | Bluesky | recent posts, newest matching first |
| `ax` | arXiv | a feed in relevance order; every word must match |
| `se` | Standard Ebooks | the catalogue list |
| `r` | Reddit | a feed of posts |
| `mt` | Mastodon hashtag | a feed; one word, no `#` |
| `fl` | Flickr tags | a feed; words become tags |
| `mg` | Marginalia | small-web results |
| `wb` | Wiby | old-web results |
| `dd` | DuckDuckGo | web results, no JavaScript needed |

A search is an address, `search:arxiv:electron+spin`, so it can be a link
in any document: [arXiv: electron spin](search:arxiv:electron+spin) ·
[Wikipedia: Xanadu](search:wikipedia:xanadu) ·
[Standard Ebooks: Wells](search:standard-ebooks:wells).

## Your own engines

Create `search.sexp` in the user data directory (next to your `styles/`):

```
(engine "my-wiki" (name "My Wiki") (keyword "mw") (badge "MW")
        (url "https://wiki.example.org/search?q=%s")
        (kind feed)      ; page (default) | feed | wikipedia | hn | archive
                         ; | lemmy | bluesky | gutendex
        (space "+"))     ; what a blank becomes in the URL
```

`%s` is the encoded query. An engine with a built-in id replaces it. Any
site that answers a URL with a page or a feed works; sites that only answer
through JavaScript do not. The JSON kinds read the API of that service, so
`(kind lemmy)` with another instance's `/api/v3/search?q=%s&type_=Posts` URL
searches that instance, `(kind wikipedia)` another language edition.
