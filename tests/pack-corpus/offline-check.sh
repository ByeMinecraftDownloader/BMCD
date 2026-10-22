#!/usr/bin/env bash
# Offline deployment rehearsal: no network namespace, a fresh target directory.
# This is NOT the clean-VM acceptance in docs/plan.md; it proves the deploy path
# itself does not touch the network once the pack staging cache is populated.
set -euo pipefail

pack=${1:?usage: offline-check.sh <staging/import.json> <target-dir> [binary]}
target=${2:?usage: offline-check.sh <staging/import.json> <target-dir> [binary]}
binary=${3:-build/bmcd}

test -f "$pack" || { echo "missing pack: $pack" >&2; exit 2; }
command -v bwrap >/dev/null || { echo "bwrap is required" >&2; exit 2; }
rm -rf "$target"
mkdir -p "$target"

echo "deploying offline -> $target"
bwrap --unshare-net --dev-bind / / "$binary" --pack "$pack" --to "$target" --no-backup
bwrap --unshare-net --dev-bind / / "$binary" --pack "$pack" --verify "$target"
