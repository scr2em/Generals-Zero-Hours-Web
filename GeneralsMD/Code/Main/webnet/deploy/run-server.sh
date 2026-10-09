#!/usr/bin/env bash
# Runs the Zero Hour web network server (rooms, signaling, relay and, with ZHNET_STATIC, the game itself) without
# Docker: only Node.js 18 or newer is needed. Settings come from an environment file and the command line.
#
#   ./run-server.sh                          # settings from ./zhnet.env or /etc/zhnet/zhnet.env, if there is one
#   ./run-server.sh --port 8787 --static /srv/zh/site
#   ZHNET_ENV=/path/to/file ./run-server.sh  # another environment file
#   ./run-server.sh --help                   # every option (each also exists as a ZHNET_* variable)
#
# On first start (or after an update) it installs the one dependency (ws) with `npm ci`.
# See ../README.md ("Hosting on a public server") for TLS, TURN and systemd.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
server_dir="$here/../server"

if ! command -v node >/dev/null 2>&1; then
	echo "run-server.sh: Node.js is not installed (version 18 or newer is needed): https://nodejs.org/" >&2
	exit 127
fi
node_major="$(node -p 'process.versions.node.split(".")[0]')"
if [ "$node_major" -lt 18 ]; then
	echo "run-server.sh: Node.js $node_major is too old, 18 or newer is needed" >&2
	exit 127
fi

# Environment file: ZHNET_ENV, else ./zhnet.env, else /etc/zhnet/zhnet.env. Lines are KEY=value (no export needed).
env_file="${ZHNET_ENV:-}"
if [ -z "$env_file" ]; then
	for candidate in "$PWD/zhnet.env" "$here/zhnet.env" /etc/zhnet/zhnet.env; do
		if [ -f "$candidate" ]; then env_file="$candidate"; break; fi
	done
fi
if [ -n "$env_file" ]; then
	if [ ! -f "$env_file" ]; then echo "run-server.sh: $env_file does not exist" >&2; exit 2; fi
	set -a
	# shellcheck disable=SC1090
	. "$env_file"
	set +a
fi

# The dependency. `npm ci` only when needed (no node_modules, or package-lock.json changed).
stamp="$server_dir/node_modules/.zhnet-lock"
if [ ! -d "$server_dir/node_modules/ws" ] || ! cmp -s "$server_dir/package-lock.json" "$stamp" 2>/dev/null; then
	echo "run-server.sh: installing the dependencies (npm ci)" >&2
	(cd "$server_dir" && npm ci --omit=dev --no-audit --no-fund) >&2
	cp "$server_dir/package-lock.json" "$stamp"
fi

exec node "$server_dir/server.mjs" "$@"
