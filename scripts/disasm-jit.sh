#!/bin/sh
# Disassemble a raw JIT dump produced with XLR8_DUMP_JIT=/path/to/file.bin
set -e
BIN="${1:?usage: disasm-jit.sh <jit.bin> [arch]}"
ARCH="${2:-aarch64}"
LLVM="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
MC="${LLVM_MC:-$LLVM/bin/llvm-mc}"
OBJDUMP="${LLVM_OBJDUMP:-$LLVM/bin/llvm-objdump}"
TMP_ASM="$(mktemp -t xlr8-jit.XXXXXX.s)"
TMP_OBJ="$(mktemp -t xlr8-jit.XXXXXX.o)"
trap 'rm -f "$TMP_ASM" "$TMP_OBJ"' EXIT INT TERM

printf '.text\n.globl _jit\n_jit:\n' >"$TMP_ASM"
od -An -tx1 -v "$BIN" | awk '{for (i = 1; i <= NF; i++) printf " .byte 0x%s\n", $i}' >>"$TMP_ASM"
"$MC" -filetype=obj -arch="$ARCH" "$TMP_ASM" -o "$TMP_OBJ"
exec "$OBJDUMP" -d --no-show-raw-insn "$TMP_OBJ"
