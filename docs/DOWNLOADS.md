# Web downloads (experimental)

Following an HTTP or HTTPS link now uses the response's MIME type and
`Content-Disposition`, rather than excluding `.pdf`, `.zip`, and similar
extensions before the request. HTML, Markdown, Gemtext, plain text, JSON and
feeds are still rendered as documents. Other types, or any response declared
`Content-Disposition: attachment`, are downloaded without replacing the
current page. Known image links retain the existing image-viewer route.

The toolbar's down-arrow button or **Panels → Downloads** opens a panel
showing active transfers and recently finished files:

- Received bytes and total bytes/percentage when the server supplies
  `Content-Length`; an indeterminate segment when the total is unknown.
- `×` cancels an active transfer, removes its partial file and keeps a
  cancelled entry in the list.
- `Dir` asks the platform to open the containing folder; clicking a completed
  row elsewhere copies its absolute path to the clipboard. Downloaded files
  are never executed or opened automatically.
- Failed transfers report their error. Up to 100 successful downloads are
  remembered in `state_dir()/downloads.conf` and shown next session if their
  files still exist. In-progress/failed transfers do not resume after restart.

Downloads stream to a temporary `.part` file in the same directory as the
destination, so interrupted transfers do not appear as complete files. At
completion, the final name is assigned without overwriting an existing file
(`archive (1).zip`, etc). An untrusted server-provided filename is sanitized
to a local basename. Files are saved in the XDG `Downloads` directory, falling
back to `~/Downloads` on Linux. Portable and platform-specific data-directory
builds save under their user-data directory's `downloads/` subdirectory.

Unlike regular web downloads, ebook imports and remote thumbnails still use
xanatoo's internal cache. All of these HTTPS paths require certificate-chain
and hostname verification. Plain HTTP downloads remain explicitly unencrypted;
see [TLS notes](TLS.md).
