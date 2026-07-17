#!/bin/bash
# Fan out mandatory validation to all platforms in parallel.
# Usage: scripts/platform-validate-all.sh [ref|opt|both]
# Mode is optional; omit it and remotes run the script default (both).
#
# Per host, exactly two SSH sessions (none for local macOS):
#   1) persistent: arm tmux pipe-pane + tail -F into one local poll file
#   2) one-shot:  send-keys "scripts/platform-validate.sh" only
# No BatchMode / ConnectTimeout / ServerAlive* — keep the SSH line bare.
# Completion: grep the local poll file for XLR8_VALIDATE_DONE.
# Pane capture path is /tmp/xlr8-validate-<nickname>.log (stable, known
# on the Mac).  Suite logs inside platform-validate.sh still use hostname.
set -euo pipefail

MODE="${1:-}"
TMUX_SESSION="${TMUX_SESSION:-xlr8}"
TMUX_TARGET="${TMUX_TARGET:-${TMUX_SESSION}:0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${TMPDIR:-/tmp}/xlr8-validate-${STAMP}"
mkdir -p "$OUTDIR"

# nickname -> ssh host (empty = local).  Slowest first.
HOSTS=(
  "risky:fedora-riscv"
  "pi3:pi3"
  "torpille:torpille"
  "holy:holy"
  "turbo:turbo"
  "macOS:"
)

if [ -n "$MODE" ]; then
  PANE_CMD="scripts/platform-validate.sh ${MODE}"
else
  PANE_CMD="scripts/platform-validate.sh"
fi

echo "outdir=$OUTDIR"
echo "pane_cmd=$PANE_CMD"
echo "tmux_target=$TMUX_TARGET"
echo "commit_expected=$(cd "$ROOT" && git log -1 --oneline)"

pids=()
names=()
logs=()
mirrors=()

cleanup() {
  local pid
  if [ "${#mirrors[@]}" -gt 0 ]; then
    for pid in "${mirrors[@]}"; do
      kill "$pid" 2>/dev/null || true
    done
  fi
}
trap cleanup EXIT

# Persistent session: arm pipe-pane, then follow the log forever.
start_mirror() {
  local where="$1"
  local name="$2"
  local local_log="$3"
  local rlog="/tmp/xlr8-validate-${name}.log"

  : >"$local_log"
  if [ -z "$where" ]; then
    if ! tmux has-session -t "$TMUX_SESSION" 2>/dev/null; then
      echo "error: tmux session '$TMUX_SESSION' not found locally" >&2
      return 90
    fi
    tmux pipe-pane -t "$TMUX_TARGET" 2>/dev/null || true
    : >"$rlog"
    tmux pipe-pane -o -t "$TMUX_TARGET" "cat >> '$rlog'"
    ( tail -n0 -F "$rlog" ) >>"$local_log" 2>/dev/null &
  else
    # Bare ssh — no options.  Arms pipe-pane then tails (one connection).
    ( ssh "$where" "set -e
if ! tmux has-session -t '$TMUX_SESSION' 2>/dev/null; then
  echo \"error: tmux session '$TMUX_SESSION' not found on \$(hostname)\" >&2
  exit 90
fi
L='$rlog'
tmux pipe-pane -t '$TMUX_TARGET' 2>/dev/null || true
: > \"\$L\"
tmux pipe-pane -o -t '$TMUX_TARGET' \"cat >> \$L\"
exec tail -n0 -F \"\$L\"
" >>"$local_log" 2>/dev/null ) &
  fi
  mirrors+=("$!")
  echo "mirror name=$name pane_log=$rlog poll=$local_log pid=$!"
}

# One-shot: send-keys only (pane already in ~/Work/xlr8 — never cd).
tmux_send() {
  local where="$1"

  if [ -z "$where" ]; then
    tmux send-keys -t "$TMUX_TARGET" C-c
    sleep 0.2
    tmux send-keys -t "$TMUX_TARGET" C-u
    tmux send-keys -t "$TMUX_TARGET" -l "$PANE_CMD"
    tmux send-keys -t "$TMUX_TARGET" Enter
  else
    ssh "$where" "tmux send-keys -t '$TMUX_TARGET' C-c; sleep 0.2; tmux send-keys -t '$TMUX_TARGET' C-u; tmux send-keys -t '$TMUX_TARGET' -l '$PANE_CMD'; tmux send-keys -t '$TMUX_TARGET' Enter"
  fi
  echo "kicked host=${where:-local} target=$TMUX_TARGET cmd=$PANE_CMD"
}

wait_done() {
  local name="$1"
  local local_log="$2"
  local line ec

  while true; do
    if line=$(grep -a 'XLR8_VALIDATE_DONE exit=' "$local_log" 2>/dev/null | tail -1); then
      ec=$(echo "$line" | sed -n 's/.*XLR8_VALIDATE_DONE exit=\([0-9][0-9]*\).*/\1/p')
      echo "[done $(date +%H:%M:%S)] $name $line"
      return "${ec:-1}"
    fi
    echo "[poll $(date +%H:%M:%S)] $name bytes=$(wc -c <"$local_log" | tr -d ' ')" \
      >>"$OUTDIR/poll.log"
    sleep 15
  done
}

for entry in "${HOSTS[@]}"; do
  name="${entry%%:*}"
  host="${entry#*:}"
  log="$OUTDIR/${name}.log"
  names+=("$name")
  logs+=("$log")

  echo "=== kick $name ==="
  if ! start_mirror "$host" "$name" "$log"; then
    echo "mirror failed for $name"
    ( exit 90 ) &
  elif ! tmux_send "$host"; then
    echo "send-keys failed for $name"
    ( exit 90 ) &
  else
    (
      set +e
      wait_done "$name" "$log"
      exit $?
    ) &
  fi
  pids+=("$!")
  echo "started $name pid=${pids[$((${#pids[@]} - 1))]} log=$log"
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
  echo "waiting for $name (pid $pid) — read progress in $log ..."
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
    | grep -E 'SUMMARY|commit=|host=|arch=|Test FAILED|error:|fatal:|XLR8_VALIDATE_DONE' \
    | tail -25
  echo
done

echo "outdir=$OUTDIR"
status_line
exit "$fail"
