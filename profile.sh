#!/bin/sh
rm /mp3/*.db
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/build/bin/mp3build"
"$BIN" -f
gprof "$BIN" | less

