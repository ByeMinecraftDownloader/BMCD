#!/usr/bin/env bash
# Keeps OS-specific code inside src/platform/. If this fails, move the call
# behind platform::FileOps / Paths / PlatformInfo / Console instead of adding an
# exception.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail=0

check() {
    local description="$1"
    local pattern="$2"
    local output
    output="$(grep -rnE "$pattern" src qml2 tests \
        --include='*.cpp' --include='*.h' --include='*.qml' \
        --exclude-dir=platform || true)"
    if [[ -n "$output" ]]; then
        echo "✗ $description"
        echo "$output"
        fail=1
    fi
}

check "POSIX headers outside src/platform" '(^|[^a-zA-Z_])#include\s*<(unistd.h|sys/stat.h|sys/types.h|fcntl.h|pwd.h)>'
check "Win32 API outside src/platform"   '(qt_windows\.h|windows\.h|<QWinEventNotifier>)'
check "direct QProcess use"              '(^|[^a-zA-Z_])QProcess'
check "hard-coded ~/.minecraft"          '"/\.minecraft"'
check "stale user agent"                 'BMCD/0\.3'

if [[ $fail -ne 0 ]]; then
    echo
    echo "platform guard failed: OS-specific code must live in src/platform/."
    exit 1
fi
echo "platform guard: OK"
