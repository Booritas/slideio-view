#!/usr/bin/env bash
# Fails if any Mach-O file under a directory needs a newer macOS than the floor.
#
#   check-macos-min-version.sh <dir> [floor]
#
# The floor defaults to conan/macos-deployment-target. CI has no runner old
# enough to launch the bundle on the oldest macOS it claims to support, so this
# reads what each binary declares instead: the minos of LC_BUILD_VERSION, or the
# version of the older LC_VERSION_MIN_MACOSX. dyld refuses to load anything whose
# minimum is above the running system, so one stray Qt plugin or dependency
# built for the runner's SDK breaks the bundle as surely as the executable would.
set -euo pipefail

fail() { echo "FAIL: $*" >&2; exit 1; }

dir="${1:?usage: $0 <dir> [floor]}"
[ -d "$dir" ] || fail "no such directory: $dir"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
floor="${2:-$(tr -d '[:space:]' < "$repo_root/conan/macos-deployment-target")}"

# 12.0 -> 120000, 10.13.4 -> 101304. Plain numbers compare in bash.
to_num() { awk -F. '{ printf "%d", $1 * 10000 + $2 * 100 + $3 }' <<<"$1"; }
floor_num=$(to_num "$floor")

checked=0
over=()
while IFS= read -r -d '' f; do
    # A case, not `file | grep -q`: grep -q exits on the first match, file takes
    # SIGPIPE, and under pipefail a Mach-O file would read as "not Mach-O".
    case "$(file -b "$f")" in
        *Mach-O*) ;;
        *) continue ;;
    esac

    # awk reads to the end rather than exiting on the first match, for the same
    # reason: an early exit would SIGPIPE otool.
    min=$(otool -l "$f" | awk '
        /cmd LC_BUILD_VERSION/      { want = "minos";   next }
        /cmd LC_VERSION_MIN_MACOSX/ { want = "version"; next }
        want != "" && $1 == want && found == "" { found = $2 }
        END { print found }')
    [ -n "$min" ] || fail "no minimum macOS version recorded in $f"

    checked=$((checked + 1))
    if [ "$(to_num "$min")" -gt "$floor_num" ]; then
        over+=("$min  ${f#"$dir"/}")
    fi
done < <(find "$dir" -type f -print0)

[ "$checked" -gt 0 ] || fail "no Mach-O files under $dir"

if [ "${#over[@]}" -gt 0 ]; then
    printf '  %s\n' "${over[@]}" >&2
    fail "${#over[@]} of $checked Mach-O files need a newer macOS than $floor"
fi
echo "OK: all $checked Mach-O files run on macOS $floor or newer"
