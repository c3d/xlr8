#!/bin/bash
# Run mandatory platform validation per PLATFORM-ENABLEMENT.md
# Usage: scripts/platform-validate.sh [ref|opt|both]
# Default: both.  Assumes cwd is already the xlr8 tree (tmux pane — no cd).
# Suite logs are named with $(hostname).  Always ends with:
#   XLR8_VALIDATE_DONE exit=N
# so the controller can detect completion from the pipe-pane capture alone.
set -euo pipefail

MODE="${1:-both}"
HOST="$(hostname -s 2>/dev/null || hostname)"
RUN_TAG="xlr8-validate-${HOST}"

# $? in an EXIT trap is the status the shell is exiting with.
trap 'echo "XLR8_VALIDATE_DONE exit=$?"' EXIT

# Remotes sync from Mac via the "mac" git remote.  On the Mac itself that
# remote does not exist — stay on the current commit and just build/test.
if git remote get-url mac >/dev/null 2>&1; then
    git fetch mac
    git reset --hard mac/xlr8
    git submodule update --init --recursive
fi

make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" 2>&1

run_suite() {
    local label="$1"
    local optflag="$2"
    local log="/tmp/${RUN_TAG}-${label}.log"
    echo "=== ${label} host=${HOST} ==="
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
        return 1
    fi
    return 0
}

case "$MODE" in
    ref)  run_suite ref "" || exit $? ;;
    opt)  run_suite opt "-O" || exit $? ;;
    both)
        run_suite ref "" || exit $?
        run_suite opt "-O" || exit $?
        ;;
    *) echo "usage: $0 [ref|opt|both]" >&2; exit 2 ;;
esac

echo "commit=$(git log -1 --oneline)"
echo "arch=$(uname -m)"
echo "host=${HOST}"
exit 0
