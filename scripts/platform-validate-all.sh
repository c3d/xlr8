#!/bin/bash
# Fan out mandatory validation to all platforms in parallel.
# Usage: scripts/platform-validate-all.sh [ref|opt|both]
# Run from Mac (commit source). Remotes only git fetch mac; never push.
#
# SSH without a TTY block-buffers remote stdout, so a host log can sit at a
# few hundred bytes (post git-reset) for minutes while make tests is busy.
# Do not treat a small/stale-looking log as "idle" or "done".  We force line
# buffering with stdbuf on the remote, and print a heartbeat while waiting.
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
if command -v stdbuf >/dev/null 2>&1; then
  stdbuf -oL -eL bash scripts/platform-validate.sh '"$MODE"'
else
  bash scripts/platform-validate.sh '"$MODE"'
fi
'

echo "outdir=$OUTDIR"
echo "mode=$MODE"
echo "commit_expected=$(cd "$ROOT" && git log -1 --oneline)"
echo "note: small log after git reset != idle; ssh may buffer until stdbuf/flush"

pids=()
names=()
logs=()

for entry in "${HOSTS[@]}"; do
  name="${entry%%:*}"
  host="${entry#*:}"
  log="$OUTDIR/${name}.log"
  names+=("$name")
  logs+=("$log")
  if [ -z "$host" ]; then
    (
      cd "$ROOT"
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
  pids+=("$!")
  echo "started $name pid=${pids[$((${#pids[@]} - 1))]} log=$log"
done

status_line() {
  local parts=() i
  for i in "${!pids[@]}"; do
    local sz
    sz=$(wc -c < "${logs[$i]}" 2>/dev/null | tr -d ' ')
    if kill -0 "${pids[$i]}" 2>/dev/null; then
      parts+=("${names[$i]}:run/${sz}B")
    else
      parts+=("${names[$i]}:done/${sz}B")
    fi
  done
  echo "  status: ${parts[*]}"
}

fail=0
for i in "${!pids[@]}"; do
  name="${names[$i]}"
  pid="${pids[$i]}"
  log="${logs[$i]}"
  echo "waiting for $name (pid $pid)..."
  while kill -0 "$pid" 2>/dev/null; do
    status_line
    # sleep in foreground so we can see heartbeats; 20s keeps noise low
    sleep 20
  done
  set +e
  wait "$pid"
  ec=$?
  set -e
  if [ "$ec" -eq 0 ]; then
    echo "=== $name OK ==="
  else
    echo "=== $name FAILED (exit $ec) ==="
    fail=1
  fi
  sed 's/\x1b\[[0-9;]*m//g' "$log" \
    | grep -E 'SUMMARY|commit=|host=|arch=|Test FAILED|error:|fatal:' \
    | tail -20
  echo
done

echo "outdir=$OUTDIR"
status_line
exit "$fail"
