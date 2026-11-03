# Search

[← Directory](../directory.md)

**Ctrl+Alt+K** (View → Search Bar) opens the search bar under the toolbar,
**Esc** closes it. The button at its left picks the engine; the choice is
remembered. **Ctrl+L** accepts `<keyword> <words>` as well.

| Keyword | Engine | Results |
|---------|--------|---------|
| `w` · `wd` | Wikipedia · Wikipedia (Deutsch) | the article, or the result list (press **F9** for reader mode) |
| `ax` | arXiv | a feed in relevance order; every word must match |
| `se` | Standard Ebooks | the catalogue list |
| `pg` | Project Gutenberg | book list with download counts |
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
        (kind feed)      ; the results are an RSS / Atom feed
        (space "+"))     ; what a blank becomes in the URL
```

`%s` is the encoded query. An engine with a built-in id replaces it. Any
site that answers a URL with a page or a feed works; sites that only answer
through JavaScript do not.
