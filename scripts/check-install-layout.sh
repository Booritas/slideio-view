#!/usr/bin/env bash
# Asserts that an install tree has the shape the requested layout promises.
# Usage: check-install-layout.sh <prefix> <per-config|flat>
set -euo pipefail

prefix="$1"
layout="$2"

case "$layout" in
    per-config) bin="$prefix/release/bin" ;;
    flat)       bin="$prefix/bin" ;;
    *) echo "unknown layout: $layout" >&2; exit 2 ;;
esac

exe="$bin/slideio-viewer"
[ -f "$exe" ] || exe="$exe.exe"
if [ ! -f "$exe" ] && [ ! -d "$bin/slideio-viewer.app" ] && [ ! -d "$prefix/slideio-viewer.app" ]; then
    echo "FAIL: no slideio-viewer under $bin (layout=$layout)" >&2
    find "$prefix" -maxdepth 3 >&2
    exit 1
fi
echo "OK: $layout layout present under $prefix"
