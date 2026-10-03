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
# Both libraries FindSlideIO.cmake lists in its REQUIRED_VARS, under either
# naming convention. A tree carrying the top-level library but not the core
# component passes a looser check and then fails at find_package(SlideIO),
# attributed to the wrong thing.
for _lib in slideio slideio-core; do
    ls "$root"/release/lib/lib${_lib}.* >/dev/null 2>&1 || \
      ls "$root"/release/lib/${_lib}.lib >/dev/null 2>&1 || ok=0
done

if [ "$ok" -eq 0 ]; then
    echo "SlideIO install at $root is incomplete; discarding it so it is rebuilt"
    rm -rf "$root"
    exit 0
fi
echo "OK: SlideIO install at $root is complete"
