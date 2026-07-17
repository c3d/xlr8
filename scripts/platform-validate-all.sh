#!/bin/bash
# Fan out mandatory validation to all platforms in parallel.
# Usage: scripts/platform-validate-all.sh [ref|opt|both]
# Run from Mac (commit source). Remotes only git fetch mac; never push.
#
# Each host must already have a tmux session named "xlr8" (override with
# TMUX_SESSION).  Commands are send-keys'd into that session with a real
# pane TTY (line-buffered).  Output is captured with tmux pipe-pane into
# /tmp for the Mac to poll and summarize — no | tee on the validate
# command (a pipe would re-introduce full block buffering).
set -euo pipefail

MODE="${1:-both}"
TMUX_SESSION="${TMUX_SESSION:-xlr8}"
# Prefer window 0 pane 0 so send-keys and pipe-pane hit the same target
# even if another pane (e.g. btop) is active.
TMUX_TARGET="${TMUX_TARGET:-${TMUX_SESSION}:0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${TMPDIR:-/tmp}/xlr8-validate-${STAMP}"
mkdir -p "$OUTDIR"

# nickname -> ssh host (empty = local)
HOSTS=(
  "macOS:"
  "turbo:turbo"
  "torpille:torpille"
  "pi3:pi3"
  "risky:fedora-riscv"
  "turbo-x86:turbo-x86"
  "turbo-arm:turbo-arm"
)

echo "outdir=$OUTDIR"
echo "mode=$MODE"
echo "tmux_target=$TMUX_TARGET"
echo "commit_expected=$(cd "$ROOT" && git log -1 --oneline)"
echo "note: watch progress in tmux target '$TMUX_TARGET' on each host"

remote_sh() {
  local where="$1"
  shift
  if [ -z "$where" ]; then
    bash -c "$*"
  else
    ssh -o BatchMode=yes -o ConnectTimeout=15 "$where" "$@"
  fi
}

tmux_kick() {
  local where="$1"
  local tag="$2"
  local rlog="/tmp/xlr8-validate-${tag}.log"
  local rexit="/tmp/xlr8-validate-${tag}.exit"
  local rdone="/tmp/xlr8-validate-${tag}.done"

  remote_sh "$where" "set -euo pipefail
if ! tmux has-session -t '$TMUX_SESSION' 2>/dev/null; then
  echo \"error: tmux session '$TMUX_SESSION' not found on \$(hostname)\" >&2
  exit 90
fi
rm -f '$rdone' '$rexit' '$rlog'
# Stop any prior pipe, then capture pane output to the log file.
tmux pipe-pane -t '$TMUX_TARGET'
tmux pipe-pane -o -t '$TMUX_TARGET' \"cat >> '$rlog'\"
tmux send-keys -t '$TMUX_TARGET' C-c
sleep 0.3
tmux send-keys -t '$TMUX_TARGET' C-u
# No | tee: keep stdout as the pane TTY so make/xlr8 stay line-buffered.
tmux send-keys -t '$TMUX_TARGET' \"cd ~/Work/xlr8 && rm -f '$rdone' '$rexit' && { bash scripts/platform-validate.sh $MODE; echo \\\$? > '$rexit'; touch '$rdone'; }\" Enter
echo \"kicked target=$TMUX_TARGET tag=$tag pipe-pane=$rlog (live in tmux)\"
"
}

tmux_wait() {
  local where="$1"
  local tag="$2"
  local local_log="$3"
  local rlog="/tmp/xlr8-validate-${tag}.log"
  local rexit="/tmp/xlr8-validate-${tag}.exit"
  local rdone="/tmp/xlr8-validate-${tag}.done"

  while true; do
    local st sz
    st=$(remote_sh "$where" "if test -f '$rdone'; then echo DONE; else echo WAIT; fi" \
         | tr -d '[:space:]')
    sz=$(remote_sh "$where" "if test -f '$rlog'; then wc -c < '$rlog'; else echo 0; fi" \
         | tr -d '[:space:]')
    # Refresh Mac-side copy from the pipe-pane capture
    if [ -z "$where" ]; then
      if [ -f "$rlog" ]; then cp "$rlog" "$local_log"; fi
    else
      ssh -o BatchMode=yes -o ConnectTimeout=15 "$where" \
        "test -f '$rlog' && cat '$rlog' || true" >"$local_log" 2>/dev/null || true
    fi
    echo "[poll $(date +%H:%M:%S)] $([ -z "$where" ] && echo local || echo "$where") bytes=${sz:-0} status=${st:-WAIT}" \
      >>"$OUTDIR/poll.log"

    if [ "${st:-}" = "DONE" ]; then
      break
    fi
    sleep 15
  done

  # Let pipe-pane flush last lines, then disconnect capture
  sleep 0.5
  remote_sh "$where" "tmux pipe-pane -t '$TMUX_TARGET' 2>/dev/null || true" || true

  if [ -z "$where" ]; then
    [ -f "$rlog" ] && cp "$rlog" "$local_log"
  else
    remote_sh "$where" "cat '$rlog'" >"$local_log" || true
  fi
  local ec
  ec=$(remote_sh "$where" "cat '$rexit' 2>/dev/null || echo 1" | tr -d '[:space:]')
  return "${ec:-1}"
}

pids=()
names=()
logs=()

for entry in "${HOSTS[@]}"; do
  name="${entry%%:*}"
  host="${entry#*:}"
  log="$OUTDIR/${name}.log"
  tag="${STAMP}-${name}"
  names+=("$name")
  logs+=("$log")

  (
    set +e
    {
      echo "=== kick $name (tmux $TMUX_TARGET, pipe-pane capture) ==="
      tmux_kick "$host" "$tag"
    } >"$log" 2>&1
    if [ $? -ne 0 ]; then
      echo "kick failed for $name" >>"$log"
      exit 90
    fi
    tmux_wait "$host" "$tag" "$log"
    exit $?
  ) &
  pids+=("$!")
  echo "started $name pid=${pids[$((${#pids[@]} - 1))]} tmux=$TMUX_TARGET tag=$tag log=$log"
done

status_line() {
  local parts=() i sz
  for i in "${!pids[@]}"; do
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
  echo "waiting for $name (pid $pid) — progress is in tmux '$TMUX_TARGET' on that host..."
  while kill -0 "$pid" 2>/dev/null; do
    status_line
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
    | grep -E 'SUMMARY|commit=|host=|arch=|Test FAILED|error:|fatal:|kicked ' \
    | tail -20
  echo
done

echo "outdir=$OUTDIR"
status_line
exit "$fail"
