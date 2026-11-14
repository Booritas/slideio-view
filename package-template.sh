#!/usr/bin/env bash
set -e

# Builds the distributable packages for this platform. CI calls exactly this, so
# a release can be reproduced on a developer machine -- which is the property
# that keeps the packaging rules reviewable rather than discovered on a tag.

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) BUILD_DIR="build/build" ;;
    *)                    BUILD_DIR="build/build/$BUILD_TYPE" ;;
esac

# The install tree has to be flat, or every package carries a release/ directory
# the user has to descend through -- and the .deb would place its binary at
# /opt/slideio-viewer/release/bin/slideio-viewer.
INSTALL_LAYOUT=flat "$REPO_ROOT/build-template.sh"

# Start from nothing. cpack would happily leave the previous run's artifacts in
# place beside this one's, and the upload step cannot tell them apart.
rm -rf "$REPO_ROOT/build/packages"
mkdir -p "$REPO_ROOT/build/packages"

# On macOS, hdiutil intermittently fails with "create failed - Resource busy"
# while something on the runner still holds the staging folder; it is not
# reproducible and a second attempt succeeds. Retry there, and only there, so a
# real packaging error elsewhere still fails on the first run.
attempts=1
[ "$(uname -s)" = "Darwin" ] && attempts=3
for attempt in $(seq 1 "$attempts"); do
    if cpack --config "$REPO_ROOT/$BUILD_DIR/CPackConfig.cmake" \
             -B "$REPO_ROOT/build/packages" \
             -C "$BUILD_TYPE"; then
        break
    fi
    if [ "$attempt" -eq "$attempts" ]; then
        exit 1
    fi
    echo "cpack failed (attempt $attempt of $attempts); retrying in 10 seconds" >&2
    sleep 10
done

# cpack -B leaves its staging directory here: a full second copy of the installed
# tree. Left in place it triples every artifact upload and collides across
# platforms once they are merged into one directory.
rm -rf "$REPO_ROOT/build/packages/_CPack_Packages"

ls -la "$REPO_ROOT/build/packages"
