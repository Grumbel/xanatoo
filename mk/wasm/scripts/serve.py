#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static server for the WebAssembly build.

The build runs worker threads (thumtoo, PDF / DjVu reading), which browsers
only allow on a cross-origin isolated page: every response carries
Cross-Origin-Opener-Policy / Cross-Origin-Embedder-Policy.
Usage: serve.py DIRECTORY [PORT]
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
    handler = functools.partial(Handler, directory=directory)
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()


main()
