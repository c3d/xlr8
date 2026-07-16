#!/bin/bash
# Fan out mandatory validation to all platforms in parallel.
# Usage: scripts/platform-validate-all.sh [ref|opt|both]
# Run from Mac (commit source). Remotes only git fetch mac; never push.
#
# Each host must already have a tmux session named "xlr8" (override with
# TMUX_SESSION).  Build/test commands are send-keys'd into that session.
# Output and exit status land under /tmp and are polled from Mac.
set -euo pipefail

MODE="${1:-both}"
TMUX_SESSION="${TMUX_SESSION:-xlr8}"
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

echo "outdir=$OUTDIR"
echo "mode=$MODE"
echo "tmux_session=$TMUX_SESSION"
echo "commit_expected=$(cd "$ROOT" && git log -1 --oneline)"

remote_sh() {
  # remote_sh "" 'cmd'  -> local bash -c
  # remote_sh host 'cmd' -> ssh host
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
tmux send-keys -t '$TMUX_SESSION' C-c
sleep 0.3
tmux send-keys -t '$TMUX_SESSION' C-u
tmux send-keys -t '$TMUX_SESSION' \"cd ~/Work/xlr8 && rm -f '$rdone' '$rexit' && { bash scripts/platform-validate.sh $MODE; echo \\\$? > '$rexit'; } >'$rlog' 2>&1; touch '$rdone'\" Enter
echo \"kicked session=$TMUX_SESSION tag=$tag log=$rlog\"
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
    {
      echo "[poll $(date +%H:%M:%S)] remote_log_bytes=${sz:-0} status=${st:-WAIT}"
      remote_sh "$where" "if test -f '$rlog'; then tail -c 8192 '$rlog'; fi" || true
    } >"$local_log"

    if [ "${st:-}" = "DONE" ]; then
      break
    fi
    sleep 15
  done

  remote_sh "$where" "cat '$rlog'" >"$local_log" || true
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
      echo "=== kick $name (tmux $TMUX_SESSION) ==="
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
  echo "started $name pid=${pids[$((${#pids[@]} - 1))]} tmux=$TMUX_SESSION tag=$tag log=$log"
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
  echo "waiting for $name (pid $pid)..."
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
