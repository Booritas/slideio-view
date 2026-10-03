#!/usr/bin/env bash
# Asserts that build/packages holds only artifacts, and nothing from a previous
# run. cpack -B leaves a _CPack_Packages staging tree -- a second full copy of
# the install tree -- and a stale artifact from an earlier run is
# indistinguishable from a fresh one once it reaches the release page.
set -euo pipefail

dir="${1:-build/packages}"

if [ ! -d "$dir" ]; then
    echo "FAIL: $dir does not exist" >&2
    exit 1
fi

if [ -d "$dir/_CPack_Packages" ]; then
    echo "FAIL: $dir/_CPack_Packages was left behind" >&2
    exit 1
fi

if [ -e "$dir/STALE-MARKER" ]; then
    echo "FAIL: $dir still contains STALE-MARKER from a previous run" >&2
    exit 1
fi

count=$(find "$dir" -maxdepth 1 -type f \
    \( -name '*.exe' -o -name '*.zip' -o -name '*.dmg' -o -name '*.deb' -o -name '*.tar.gz' \) | wc -l)
if [ "$count" -eq 0 ]; then
    echo "FAIL: no packages in $dir" >&2
    ls -la "$dir" >&2
    exit 1
fi
echo "OK: $dir holds $count package(s) and nothing stale"
