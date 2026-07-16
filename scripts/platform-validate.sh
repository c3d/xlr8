#!/bin/bash
# Run mandatory platform validation per PLATFORM-ENABLEMENT.md
# Usage: platform-validate.sh [ref|opt|both]
set -euo pipefail

MODE="${1:-both}"
cd ~/Work/xlr8

# Remotes sync from Mac via the "mac" git remote.  On the Mac itself that
# remote does not exist — stay on the current commit and just build/test.
if git remote get-url mac >/dev/null 2>&1; then
    git fetch mac
    git reset --hard mac/xlr8
    git submodule update --init --recursive
fi

make -j$(nproc) 2>&1

run_suite() {
    local label="$1"
    local optflag="$2"
    local log="/tmp/xlr8-${label}-$(date +%Y%m%d-%H%M%S).log"
    echo "=== ${label} ==="
    if [ -n "$optflag" ]; then
        make tests OPTLEVEL=-O 2>&1 | tee "$log"
    else
        make tests 2>&1 | tee "$log"
    fi
    echo "log=$log"
    grep '^\[SUMMARY\].*Successful tests' "$log" | tail -1 || true
    grep '^\[SUMMARY\].*Failed tests' "$log" | tail -1 || true
    if grep -q 'Test FAILED' "$log"; then
        echo "failures:"
        grep 'Test FAILED' "$log" | head -20
    fi
}

case "$MODE" in
    ref)  run_suite ref "" ;;
    opt)  run_suite opt "-O" ;;
    both)
        run_suite ref ""
        run_suite opt "-O"
        ;;
    *) echo "usage: $0 [ref|opt|both]" >&2; exit 2 ;;
esac

echo "commit=$(git log -1 --oneline)"
echo "arch=$(uname -m)"
echo "host=$(hostname)"
