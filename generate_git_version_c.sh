#!/bin/bash

set -euo pipefail

output="${1:-src/git_version.c}"
mkdir -p "$(dirname "$output")"

version="$(git describe --tags --dirty --always 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo unknown)"
escaped_version="$(printf '%s' "$version" | sed 's/\\/\\\\/g; s/"/\\"/g')"

cat >"$output" <<EOF
const char* git_version(void) { static const char* GIT_Version = "${escaped_version}"; return GIT_Version; }
EOF
