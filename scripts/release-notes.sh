#!/usr/bin/env bash
# Prints the CHANGELOG.md section for one version, without its heading, and
# fails when the section is missing or empty -- a release without notes is
# refused the same way a release whose tag disagrees with the project version is.
# Usage: release-notes.sh <version>      (e.g. 0.2.0, no leading v)
set -euo pipefail

cd "$(dirname "$0")/.."

version="${1:?usage: release-notes.sh <version>}"

# From the line after "## [<version>]" up to the next "## [" heading -- or, for
# the oldest version, the compare-link definitions at the foot of the file --
# with leading and trailing blank lines dropped. index() rather than a regex, so
# the dots in the version are not wildcards.
notes=$(awk -v heading="## [${version}]" '
    index($0, heading) == 1      { inside = 1; next }
    inside && /^## \[/           { exit }
    inside && /^\[[^]]+\]: /     { exit }
    inside                   { lines[++n] = $0 }
    END {
        first = 1; while (first <= n && lines[first] ~ /^[[:space:]]*$/) first++
        last = n;  while (last >= first && lines[last] ~ /^[[:space:]]*$/) last--
        for (i = first; i <= last; i++) print lines[i]
    }' CHANGELOG.md)

if [ -z "$notes" ]; then
    echo "CHANGELOG.md has no notes for ${version}." >&2
    echo "Add a '## [${version}] - <date>' section, or rename '## [Unreleased]' to it." >&2
    exit 1
fi

printf '%s\n' "$notes"
