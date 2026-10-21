#!/usr/bin/env bash
# Offscreen golden check for the new UI: run the app, screenshot it, verify the
# pixels and the Qt log. Linux-only (ctest gates it).
set -u
binary="$1"
checker="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

QT_QPA_PLATFORM=offscreen \
QT_QUICK_BACKEND=software \
MC_SCREENSHOT="$work/ui.png" \
MC_SCREENSHOT_DELAY="${MC_SCREENSHOT_DELAY:-6000}" \
MC_LOG="$work/ui.log" \
    "$binary" --ui=v2
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL: bmcd exited with $status"
    cat "$work/ui.log" 2>/dev/null
    exit 1
fi

"$checker" "$work/ui.png" "$work/ui.log"
