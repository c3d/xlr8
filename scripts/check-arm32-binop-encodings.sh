#!/bin/bash
# Validate arm32 §6.5 encodings against llvm-mc / llvm-objdump.
# Usage: scripts/check-arm32-binop-encodings.sh
set -euo pipefail

LLVM="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
MC="${LLVM_MC:-$LLVM/bin/llvm-mc}"
OBJDUMP="${LLVM_OBJDUMP:-$LLVM/bin/llvm-objdump}"
TRIPLE="armv7-unknown-linux-gnueabihf"
ATTR="+hwdiv-arm"
MCPU="cortex-a15"
TMPDIR="${TMPDIR:-/tmp}"
ASM="$TMPDIR/xlr8-arm32-6.5.s"
OBJ="$TMPDIR/xlr8-arm32-6.5.o"
BIN="$TMPDIR/xlr8-arm32-6.5.bin"
DUMP="$TMPDIR/xlr8-arm32-6.5.dump"

# Formulas must match backend/arm.h helpers (sdiv/udiv/mls/shift_imm/mul/and/orr/eor).
sdiv() { printf '%u' $((0xE710F010 | ($1 << 16) | ($3 << 8) | $2)); }
udiv() { printf '%u' $((0xE730F010 | ($1 << 16) | ($3 << 8) | $2)); }
mls()  { printf '%u' $((0xE0600090 | ($1 << 16) | ($4 << 12) | ($3 << 8) | $2)); }
mul()  { printf '%u' $((0xE0000090 | ($1 << 16) | ($3 << 8) | $2)); }
shift_imm() { # rd rm imm stype
  printf '%u' $((0xE1A00000 | ($1 << 12) | (($3 & 31) << 7) | ($4 << 5) | $2));
}
and_rr() { printf '%u' $((0xE0000000 | ($1 << 12) | ($2 << 16) | $3)); }
orr_rr() { printf '%u' $((0xE1800000 | ($1 << 12) | ($2 << 16) | $3)); }
eor_rr() { printf '%u' $((0xE0200000 | ($1 << 12) | ($2 << 16) | $3)); }

check_mc() {
  local asm="$1" expect="$2" label="$3"
  local got b0 b1 b2 b3 word
  got=$("$MC" -show-encoding -triple="$TRIPLE" -mattr="$ATTR" <<<"	$asm" \
        | sed -n 's/.*encoding: \[\(.*\)\].*/\1/p' | head -1)
  IFS=', ' read -r b0 b1 b2 b3 <<<"$got"
  word=$(( (${b3} << 24) | (${b2} << 16) | (${b1} << 8) | ${b0} ))
  if [[ "$word" -ne "$expect" ]]; then
    printf 'FAIL %s: llvm-mc=%08x helper=%08x (asm: %s)\n' \
      "$label" "$word" "$expect" "$asm" >&2
    exit 1
  fi
  printf 'OK   %s = %08x\n' "$label" "$expect"
}

echo "=== llvm-mc golden vs arm.h helpers ==="
check_mc "sdiv r0, r1, r2" "$(sdiv 0 1 2)" "sdiv r0,r1,r2"
check_mc "udiv r0, r1, r2" "$(udiv 0 1 2)" "udiv r0,r1,r2"
check_mc "mls r0, r1, r2, r3" "$(mls 0 1 2 3)" "mls r0,r1,r2,r3"
check_mc "mul r0, r1, r2" "$(mul 0 1 2)" "mul r0,r1,r2"
check_mc "mov r0, r1, lsl #3" "$(shift_imm 0 1 3 0)" "lsl #3"
check_mc "mov r0, r1, asr #2" "$(shift_imm 0 1 2 2)" "asr #2"
check_mc "mov r0, r1, lsr #4" "$(shift_imm 0 1 4 1)" "lsr #4"
check_mc "and r0, r0, r8" "$(and_rr 0 0 8)" "and r0,r0,r8"
check_mc "orr r0, r0, r8" "$(orr_rr 0 0 8)" "orr r0,r0,r8"
check_mc "eor r0, r0, r8" "$(eor_rr 0 0 8)" "eor r0,r0,r8"

# Synthetic stream matching binop patterns (rd=r0, rs=r1, imm via r8, q via r2)
WORDS=(
  "$(mul 0 0 1)"
  "$(sdiv 0 0 1)"
  "$(udiv 0 0 1)"
  "$(sdiv 8 0 1)"
  "$(mls 0 8 1 0)"
  "$(shift_imm 0 0 3 0)"
  "$(shift_imm 0 0 2 2)"
  "$(shift_imm 0 0 4 1)"
  "$(and_rr 0 0 8)"
  "$(orr_rr 0 0 8)"
  "$(eor_rr 0 0 8)"
  "$(sdiv 0 0 8)"
  "$(sdiv 2 0 8)"
  "$(mls 0 2 8 0)"
)

{
  printf '.text\n.globl _jit\n_jit:\n'
  for w in "${WORDS[@]}"; do
    printf '  .inst 0x%08x\n' "$w"
  done
} >"$ASM"

"$MC" -filetype=obj -triple="$TRIPLE" -mattr="$ATTR" "$ASM" -o "$OBJ"

python3 - "$BIN" "${WORDS[@]}" <<'PY'
import struct, sys, pathlib
path = pathlib.Path(sys.argv[1])
words = [int(x) for x in sys.argv[2:]]
path.write_bytes(b"".join(struct.pack("<I", w) for w in words))
PY

echo
echo "=== llvm-objdump --triple=arm --arch-name=arm -d ==="
"$OBJDUMP" --triple=arm --arch-name=arm --mcpu="$MCPU" -d "$OBJ" | tee "$DUMP"

need=(mul sdiv udiv mls lsl asr lsr and orr eor)
for m in "${need[@]}"; do
  if ! grep -Eq "$m" "$DUMP"; then
    echo "FAIL: disassembly missing '$m'" >&2
    exit 1
  fi
done

echo
echo "All arm32 §6.5 encodings OK"
echo "  dump=$DUMP"
echo "  bin=$BIN"
echo "  recheck: $OBJDUMP --triple=arm --arch-name=arm --mcpu=$MCPU -d $OBJ"
