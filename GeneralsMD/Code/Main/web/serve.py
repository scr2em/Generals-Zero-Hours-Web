#!/usr/bin/env python3
"""Tiny development server for the WebAssembly build.

SharedArrayBuffer (needed for the game's worker thread) only exists on cross
origin isolated pages, so every response carries COOP/COEP headers. Usage:

    python3 serve.py [--port 8080] [--dir path/to/build/output] [--bind 127.0.0.1]

The game is only served; the game data stays in the browser (Origin Private
File System) and never touches this server.
"""

import argparse
import functools
import http.server
import os
import socketserver
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
	extensions_map = {
		**http.server.SimpleHTTPRequestHandler.extensions_map,
		".wasm": "application/wasm",
		".js": "text/javascript",
		".mjs": "text/javascript",
		".html": "text/html; charset=utf-8",
		".json": "application/json",
		".svg": "image/svg+xml",
	}

	def end_headers(self):
		self.send_header("Cross-Origin-Opener-Policy", "same-origin")
		self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
		self.send_header("Cross-Origin-Resource-Policy", "same-origin")
		self.send_header("Cache-Control", "no-cache")
		super().end_headers()

	def log_message(self, fmt, *args):
		if os.environ.get("SERVE_QUIET"):
			return
		super().log_message(fmt, *args)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
	daemon_threads = True
	allow_reuse_address = True


def main():
	default_dir = os.path.dirname(os.path.abspath(__file__))
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--port", type=int, default=8080)
	parser.add_argument("--bind", default="127.0.0.1")
	parser.add_argument("--dir", default=default_dir, help="directory to serve (default: next to this script)")
	args = parser.parse_args()

	handler = functools.partial(Handler, directory=args.dir)
	with Server((args.bind, args.port), handler) as httpd:
		print("Serving %s on http://%s:%d/ (cross-origin isolated)" % (args.dir, args.bind, httpd.server_address[1]))
		sys.stdout.flush()
		try:
			httpd.serve_forever()
		except KeyboardInterrupt:
			pass


if __name__ == "__main__":
	main()
