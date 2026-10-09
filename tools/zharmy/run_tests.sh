#!/bin/sh
# Runs every unit test of the army package converter. Needs only Python 3.
set -e
cd "$(dirname "$0")/.."
exec python3 -W ignore::ResourceWarning -m unittest discover -s zharmy/tests -t . "$@"
