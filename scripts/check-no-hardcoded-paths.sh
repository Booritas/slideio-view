#!/usr/bin/env bash
# Fails while the build hardcodes an absolute path into somebody's machine.
#
# The Qt deployment used to glob "d:/conan2/p/b/qt*/p/bin". On the machine that
# path describes, every outcome test passes; everywhere else the glob matches
# nothing and -- because the installs were OPTIONAL -- the installer builds
# cleanly and contains no Qt. A test of the result cannot see that, because the
# result is correct for whoever wrote it. This checks the cause instead.
set -euo pipefail

cd "$(dirname "$0")/.."

# Drive-letter absolute paths and absolute home directories, in the files that
# decide what gets built and shipped. The drive letter has to start a token --
# otherwise "Run:\n" inside a message string reads as a path.
pattern='(^|["'"'"'( =])[A-Za-z]:[\\/]|/Users/|/home/'
files=$(git ls-files 'CMakeLists.txt' 'cmake/*.cmake' 'src/**/CMakeLists.txt' 'tests/CMakeLists.txt')

hits=""
for f in $files; do
    # Comments are allowed to name a path as an example; code is not.
    while IFS= read -r line; do
        hits="${hits}${f}:${line}"$'\n'
    done < <(grep -nE "$pattern" "$f" | grep -vE '^\s*[0-9]+:\s*#' | grep -vE '^[0-9]+:\s*#' || true)
done

if [ -n "$hits" ]; then
    echo "FAIL: absolute machine-specific paths in the build files:" >&2
    echo "$hits" >&2
    exit 1
fi
echo "OK: no absolute machine-specific paths in the build files"
