#!/bin/bash
# Fan out mandatory validation to all platforms in parallel.
# Usage: scripts/platform-validate-all.sh [ref|opt|both]
# Run from Mac (commit source). Remotes only git fetch mac; never push.
set -euo pipefail

MODE="${1:-both}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${TMPDIR:-/tmp}/xlr8-validate-${STAMP}"
mkdir -p "$OUTDIR"

# nickname -> ssh host (empty = local)
HOSTS=(
  "macOS:"
  "turbo:turbo"
  "torpille:torpille"
  "risky:fedora-riscv"
  "turbo-x86:turbo-x86"
  "turbo-arm:turbo-arm"
)

REMOTE_SCRIPT='set -euo pipefail
cd ~/Work/xlr8
git fetch mac
git reset --hard mac/xlr8
git submodule update --init --recursive
bash scripts/platform-validate.sh '"$MODE"'
'

echo "outdir=$OUTDIR"
echo "mode=$MODE"
echo "commit_expected=$(cd "$ROOT" && git log -1 --oneline)"

pids=()
names=()

for entry in "${HOSTS[@]}"; do
  name="${entry%%:*}"
  host="${entry#*:}"
  log="$OUTDIR/${name}.log"
  names+=("$name")
  if [ -z "$host" ]; then
    (
      cd "$ROOT"
      # Local mac: already on the commit; skip fetch/reset
      MODE="$MODE" bash -c '
        set -euo pipefail
        MODE="$0"
        run_suite() {
          local label="$1" optflag="$2"
          local log="/tmp/xlr8-mac-${label}-'"$STAMP"'.log"
          echo "=== ${label} ==="
          if [ -n "$optflag" ]; then
            make tests OPTLEVEL=-O 2>&1 | tee "$log"
          else
            make tests 2>&1 | tee "$log"
          fi
          grep "^\[SUMMARY\].*Successful tests" "$log" | tail -1 || true
          grep "^\[SUMMARY\].*Failed tests" "$log" | tail -1 || true
        }
        case "$MODE" in
          ref)  run_suite ref "" ;;
          opt)  run_suite opt "-O" ;;
          both) run_suite ref ""; run_suite opt "-O" ;;
          *) echo "usage: both|ref|opt" >&2; exit 2 ;;
        esac
        echo "commit=$(git log -1 --oneline)"
        echo "arch=$(uname -m)"
        echo "host=$(hostname)"
      ' "$MODE"
    ) >"$log" 2>&1 &
  else
    ssh -o BatchMode=yes -o ConnectTimeout=15 "$host" "$REMOTE_SCRIPT" \
      >"$log" 2>&1 &
  fi
  pid=$!
  pids+=("$pid")
  echo "started $name pid=$pid log=$log"
done

fail=0
for i in "${!pids[@]}"; do
  name="${names[$i]}"
  pid="${pids[$i]}"
  if wait "$pid"; then
    echo "=== $name OK ==="
  else
    ec=$?
    echo "=== $name FAILED (exit $ec) ==="
    fail=1
  fi
  sed 's/\x1b\[[0-9;]*m//g' "$OUTDIR/${name}.log" \
    | grep -E 'SUMMARY|commit=|host=|arch=|Test FAILED|error:|fatal:' \
    | tail -20
  echo
done

echo "outdir=$OUTDIR"
exit "$fail"
