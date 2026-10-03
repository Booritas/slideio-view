#!/usr/bin/env bash
# Checks a built package the way a stranger would meet it: installed or unpacked,
# with no build tree, no conan and no toolchain file on the path.
set -euo pipefail

pkg_dir="${1:-build/packages}"

fail() { echo "FAIL: $*" >&2; exit 1; }

# A GUI process that stays up has resolved every library it needs to reach the
# event loop. timeout reports 124 when it has to kill the process, which is the
# success case here -- so never assert on a zero exit.
stays_up() {
    local exe="$1"; shift
    set +e
    timeout 15 "$exe" -platform offscreen "$@" >/tmp/smoke-run.log 2>&1
    local rc=$?
    set -e
    if [ "$rc" -ne 124 ]; then
        echo "--- output ---" >&2; cat /tmp/smoke-run.log >&2
        fail "$exe exited with $rc instead of staying up"
    fi
    echo "OK: $(basename "$exe") stayed up for 15s"
}

case "$(uname -s)" in
Linux)
    deb=$(find "$pkg_dir" -maxdepth 1 -name '*.deb' | head -1)
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
    dmg=$(find "$pkg_dir" -maxdepth 1 -name '*.dmg' | head -1)
    [ -n "$dmg" ] || fail "no .dmg in $pkg_dir"
    mount_point=$(mktemp -d)
    hdiutil attach "$dmg" -mountpoint "$mount_point" -nobrowse -quiet
    rm -rf /tmp/smoke-app && mkdir -p /tmp/smoke-app
    cp -R "$mount_point"/*.app /tmp/smoke-app/
    hdiutil detach "$mount_point" -quiet

    app=$(find /tmp/smoke-app -maxdepth 1 -name '*.app' | head -1)
    [ -n "$app" ] || fail "no .app in the disk image"
    [ -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib" ] || fail "cocoa platform plugin not bundled"
    [ -f "$app/Contents/PlugIns/platforms/libqoffscreen.dylib" ] || fail "offscreen platform plugin not bundled"

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
