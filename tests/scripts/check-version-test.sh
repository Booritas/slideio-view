#!/usr/bin/env bash
# The version check is the thing standing between a mistyped tag and three
# platforms spending an hour producing packages whose names are wrong.
set -uo pipefail
cd "$(dirname "$0")/../.."

pass=0; fail=0
expect_ok()   { if ./scripts/check-version.sh "$1" >/dev/null 2>&1; then pass=$((pass+1)); else echo "FAIL: expected '$1' accepted"; fail=$((fail+1)); fi; }
expect_fail() { if ./scripts/check-version.sh "$1" >/dev/null 2>&1; then echo "FAIL: expected '$1' rejected"; fail=$((fail+1)); else pass=$((pass+1)); fi; }

version=$(./scripts/check-version.sh)
if [ -n "$version" ]; then pass=$((pass+1)); else echo "FAIL: no version read"; fail=$((fail+1)); fi

expect_ok   "v${version}"
expect_ok   "v${version}-rc1"          # a release candidate for this tree
expect_fail "v9.9.9"                   # a tag for a different version
expect_fail "v9.9.9-rc1"               # a prerelease of a different version
expect_fail "not-a-tag"

echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
