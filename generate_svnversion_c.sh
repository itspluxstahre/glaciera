#!/bin/bash

set -e

output="src/svn_version.c"
mkdir -p "$(dirname "$output")"
printf 'char* svn_version(void) { static char* SVN_Version = "' > "$output"
svnversion -n . >> "$output"
printf '"; return SVN_Version; }\n' >> "$output"
