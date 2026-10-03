#!/usr/bin/env bash
# Checks a built package the way a stranger would meet it: installed or unpacked,
# with no build tree, no conan and no toolchain file on the path.
set -euo pipefail

pkg_dir="${1:-build/packages}"

# Absolute, because apt-get does not take a relative path to a .deb: an argument
# with a slash that does not begin with / or ./ is read as package/release, so
# `apt-get install build/packages/x.deb` fails with "Unable to locate package
# build/packages" and says nothing about the file it was handed.
pkg_dir=$(cd "$pkg_dir" 2>/dev/null && pwd) || { echo "FAIL: no such directory: ${1:-build/packages}" >&2; exit 1; }

fail() { echo "FAIL: $*" >&2; exit 1; }

# Under `set -e` a failing command exits with whatever it printed, which for a
# tool run with -quiet is nothing at all: the CI step then shows exit 1 and an
# empty log, and the only way to find the failing line is to guess. Name it.
trap 'echo "FAIL: $0:$LINENO: ${BASH_COMMAND}" >&2' ERR

# `find … | head -1` is wrong under `set -o pipefail`: head exits after the first
# line, find takes SIGPIPE, and the assignment fails -- so the script dies before
# printing anything and the CI step shows an empty failure. It is a race, so it
# passes on one platform and not another; it passed on Git Bash and killed the
# macOS run. `-print -quit` has no pipe to break.
#
# Every `find` below uses it. Say so here because the next person to add one
# will reach for the pipe.

# A GUI process that stays up has resolved every library it needs to reach the
# event loop. One that exits immediately has not, and that is what a missing
# library or platform plugin actually looks like.
#
# Deliberately not `timeout`: that is GNU coreutils and macOS does not ship it,
# so the macOS branch would die on a missing command. Polling a background pid
# needs nothing that is not in bash.
stays_up() {
    local exe="$1"; shift
    local limit=15

    "$exe" -platform offscreen "$@" >/tmp/smoke-run.log 2>&1 &
    local pid=$!

    local waited=0
    while [ "$waited" -lt "$limit" ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            local rc=0
            wait "$pid" || rc=$?
            echo "--- output ---" >&2
            cat /tmp/smoke-run.log >&2 || true
            fail "$(basename "$exe") exited with $rc after ${waited}s instead of staying up"
        fi
        sleep 1
        waited=$((waited + 1))
    done

    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "OK: $(basename "$exe") stayed up for ${limit}s"
}

case "$(uname -s)" in
Linux)
    deb=$(find "$pkg_dir" -maxdepth 1 -name '*.deb' -print -quit)
    [ -n "$deb" ] || fail "no .deb in $pkg_dir"
    # Runs as root inside the clean container CI uses, and under sudo on a
    # developer machine.
    SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    $SUDO apt-get update
    $SUDO apt-get install -y "$deb"

    prefix=/opt/slideio-viewer
    [ -x "$prefix/bin/slideio-viewer" ] || fail "$prefix/bin/slideio-viewer not installed"
    [ -f "$prefix/plugins/platforms/libqxcb.so" ] || fail "xcb platform plugin not packaged"
    [ -f "$prefix/plugins/platforms/libqoffscreen.so" ] || fail "offscreen platform plugin not packaged"

    # Every shared object in the package, not just the executable and the xcb
    # plugin. A GLX-integration plugin that cannot load is what breaks a real
    # desktop launch, and the offscreen probe below is structurally blind to it.
    while IFS= read -r lib; do
        if ldd "$lib" 2>/dev/null | grep -q 'not found'; then
            ldd "$lib" | grep 'not found' >&2
            fail "unresolved libraries in $lib"
        fi
    done < <(find "$prefix" -name '*.so*' -type f; echo "$prefix/bin/slideio-viewer")
    echo "OK: no unresolved libraries anywhere in the package"

    # The package bundles Qt so it runs against the Qt it was tested with. On a
    # machine that also has a system Qt, "no unresolved libraries" is satisfied
    # just as well by resolving against /usr/lib -- which is the failure this
    # test exists to catch, and cannot see by absence alone. Assert where the Qt
    # libraries actually come from.
    for lib in "$prefix/bin/slideio-viewer" \
               "$prefix/plugins/platforms/libqxcb.so" \
               "$prefix/plugins/xcbglintegrations/libqxcb-glx-integration.so"; do
        [ -f "$lib" ] || continue
        outside=$(ldd "$lib" | grep -E 'libQt6' | grep -v "$prefix/" || true)
        if [ -n "$outside" ]; then
            echo "$outside" >&2
            fail "$lib resolves Qt from outside the bundle"
        fi
    done
    echo "OK: every Qt library resolves from inside the bundle"

    stays_up /usr/bin/slideio-viewer
    ;;
Darwin)
    dmg=$(find "$pkg_dir" -maxdepth 1 -name '*.dmg' -print -quit)
    [ -n "$dmg" ] || fail "no .dmg in $pkg_dir"
    mount_point=$(mktemp -d)
    # Not -quiet: it suppresses the reason for a failed attach as well as the
    # chatter, which turns a broken disk image into an empty CI step.
    # The trap detaches on any failure between here and the detach below, so a
    # failed run does not leave the image mounted.
    trap 'hdiutil detach "$mount_point" -force >/dev/null 2>&1 || true' EXIT
    hdiutil attach "$dmg" -mountpoint "$mount_point" -nobrowse
    rm -rf /tmp/smoke-app && mkdir -p /tmp/smoke-app
    cp -R "$mount_point"/*.app /tmp/smoke-app/
    hdiutil detach "$mount_point"
    trap - EXIT

    app=$(find /tmp/smoke-app -maxdepth 1 -name '*.app' -print -quit)
    [ -n "$app" ] || fail "no .app in the disk image"
    [ -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib" ] || fail "cocoa platform plugin not bundled"
    [ -f "$app/Contents/PlugIns/platforms/libqoffscreen.dylib" ] || fail "offscreen platform plugin not bundled"

    # A bundle whose signature does not verify launches fine here but is
    # reported as "damaged" on any machine that downloaded it, because the
    # quarantine flag makes Gatekeeper check the seal. This run never sees the
    # quarantine flag, so check the seal directly.
    codesign --verify --deep --strict --verbose=2 "$app" || fail "bundle signature does not verify"
    echo "OK: bundle signature verifies"

    # An absolute build-machine path here means the bundle works for whoever
    # built it and nobody else -- exactly the breakage the old Windows glob was.
    bad=$(find "$app" -type f \( -name '*.dylib' -o -perm +111 \) -exec otool -L {} + 2>/dev/null \
          | grep -E '/Users/|\.conan2|/conan2/' || true)
    if [ -n "$bad" ]; then
        echo "$bad" >&2
        fail "bundle references build-machine paths"
    fi
    echo "OK: no build-machine paths in the bundle"

    stays_up "$app/Contents/MacOS/slideio-viewer"
    ;;
*)
    fail "unsupported platform $(uname -s)"
    ;;
esac

echo "OK: package smoke test passed"
