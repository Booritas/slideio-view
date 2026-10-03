#!/usr/bin/env bash
# build-template.sh skips the SlideIO submodule build when one header exists. A
# cache saved from a cancelled run can satisfy that check while carrying no
# libraries, and the viewer then links against a half-built install -- a failure
# that looks like a source problem and is not. Discard such a tree so the build
# rebuilds it.
set -euo pipefail

root="${1:-extern/slideio/build/install}"

if [ ! -d "$root/release" ]; then
    echo "no SlideIO install at $root; nothing to verify"
    exit 0
fi

ok=1
[ -f "$root/release/include/slideio/slideio/slideio.hpp" ] || ok=0
# One of the two library naming conventions must be present.
ls "$root"/release/lib/libslideio.* >/dev/null 2>&1 || \
  ls "$root"/release/lib/slideio.lib >/dev/null 2>&1 || ok=0

if [ "$ok" -eq 0 ]; then
    echo "SlideIO install at $root is incomplete; discarding it so it is rebuilt"
    rm -rf "$root"
    exit 0
fi
echo "OK: SlideIO install at $root is complete"
