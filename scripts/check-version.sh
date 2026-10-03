#!/usr/bin/env bash
# Reads the project version and, when given a tag, requires it to agree.
# Usage: check-version.sh [<tag>]
set -euo pipefail

cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(slideio-viewer VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
if [ -z "$version" ]; then
    echo "Could not read the version from CMakeLists.txt" >&2
    exit 1
fi

tag="${1:-}"
if [ -n "$tag" ]; then
    case "$tag" in
        v[0-9]*) ;;
        *) echo "Tag '${tag}' is not a version tag (expected v<major>.<minor>.<patch>)." >&2; exit 1 ;;
    esac
    # A prerelease tag is legitimate: v0.2.0-rc1 releases the 0.2.0 tree. Compare
    # the version core and let a suffix through, rather than rejecting the
    # release candidate or -- worse -- accepting v0.2.0-rc1 for a 0.3.0 tree.
    tag_version="${tag#v}"
    tag_core="${tag_version%%-*}"
    if [ "$tag_core" != "$version" ]; then
        echo "Tag ${tag} does not match project version ${version} in CMakeLists.txt." >&2
        echo "Update the project() version or retag." >&2
        exit 1
    fi
fi

echo "$version"
