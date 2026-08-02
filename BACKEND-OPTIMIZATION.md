# XLR8 Backend Code Generation Optimization Review

**Date**: 2026-07-17 (regalloc addendum 2026-07-27)
**Scope**: `backend/` — all four platform backends, with focus on generated code quality
**Companion**: `BACKEND-REVIEW.md` (correctness review, updated 2026-07-17)

### Addendum — Direct-map register allocation (2026-07-27)

Arm64 keeps `local n` in `xn` for recursive bytecode `call` (no `spill_all`),
with durable homes in X even for floats: `register_sync` remaps live `Vn` →
`Xn`. C/`sys` paths use `syscall()` to spill live call-clobbered `x0`–`x18`.
`enter` saves caller GPRs into the callee frame; `get`/`set` read that copy
for low indices. `x18` is reserved. Details:
`docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md`.
Remaining `-O` work:
`docs/superpowers/plans/2026-07-27-arm64-O-failures-after-direct-map.md`.
Older sections below that assume shadow-register evacuation are historical.

---

## Executive Summary

This document identifies concrete optimization opportunities for the XLR8 JIT
backends, organized by architectural impact.  The largest wins come from three
structural changes: inlining `enter`/`leave` in generated code, inlining known
`sys` opcodes from `native.tbl`, and eliminating redundant spill/reload cycles.
Per-platform instruction-level improvements (shadow register direct operations,
paired loads/stores, branchless predicates) compound to save 40-60% of
instructions in hot paths.

### Correction to BACKEND-REVIEW.md §7.2

The triple inclusion of `emit.tbl` in `gen.cpp` is **intentional and correct**.
The `.tbl` pattern ensures one-definition-rule compliance: one file defines the
switch body, and all three contexts (`translate`, `translate_sub`,
`ensure_branch_target_translated`) automatically stay in sync when the `.tbl`
is modified.  This is the DRY principle applied to preprocessor includes.

---

## 1. Tier 0 — Architectural Optimizations

These are structural changes to the code generation model, not
platform-specific instruction tweaks.

### 1.0 Normal prologue and epilogue on selected targets

Normal entry (prologue) and exit (epilogue) sequences for recursive functions follow the architecture's calling convention (ABI). These ensure the return address, callee-saved registers, stack pointer (SP), and often a frame pointer (FP) are preserved across calls. Recursive functions are non-leaf (they call themselves), so they must save the return address and any used callee-saved registers on the stack.

The intent and purpose of `enter` and `leave` is to represent this normal prologue and epilogue. `enter` reserves a number of stack slots (locals), which can be grown further with `sys alloca` if necessary. `leave` restores the previous stack frame in the list. Code generation should match the native conventions.

#### ARM 32-bit (AArch32 / ARM)

Calling convention: Parameters in `r0`–`r3` (then stack). Callee-saved: `r4`–`r11`, `r13` (`SP`, stack pointer), `r14` (`LR`, link register for return address). Frame pointer often `r11` (`FP`, frame pointer). Stack grows down, typically 8-byte aligned at boundaries. The 8-byte alignment matches XLR8 conventions.

Typical prologue (entry):
```asm
push {r4-r7, r11, lr}   ; Save callee-saved regs + FP + return addr
add  r11, sp, #offset   ; Set up frame pointer (often after push)
sub  sp, sp, #locals    ; Allocate space for locals/outbound params
Typical epilogue (exit):textadd  sp, sp, #locals    ; Deallocate locals
pop  {r4-r7, r11, pc}   ; Restore regs + return via PC (or pop LR then bx lr)
```

Recursion note: `LR` (return address) is saved on stack. Multiple recursive calls build stack frames with saved `LR`/`FP` chains.

#### ARM 64-bit (AArch64)

Calling convention (AAPCS64): Parameters in `x0`–`x7` (integers) / `d0`–`d7` or `v0`–`v7` (FP). Callee-saved: `x19`–`x28`, `FP` (`x29`), `LR` (`x30`). Stack is 16-byte aligned (stricter than xlr8). Often uses paired stores (`STP`).

Typical prologue:
```asm
stp  x29, x30, [sp, #-16]!   ; Save FP + LR (pre-decrement)
mov  x29, sp                 ; Set frame pointer
stp  x19, x20, [sp, #offset] ; Save other callee-saved (as needed)
sub  sp, sp, #locals         ; Allocate locals
Typical epilogue:textadd  sp, sp, #locals
ldp  x19, x20, [sp, #offset] ; Restore callee-saved
ldp  x29, x30, [sp], #16     ; Restore FP + LR (post-increment)
ret                          ; Return via LR (x30)
```

Recursion note: Each level saves `LR` (`x30`) and `FP` (`x29`) for chaining. PAC (Pointer Authentication) may sign `LR` on some systems.

#### x86 32-bit

Calling convention (e.g., cdecl, common on Linux/Windows): Parameters pushed on stack (right-to-left). Callee-saved: `ebx`, `esi`, `edi`, `ebp` (`FP`). Return in `eax`. Stack grows down.

Typical prologue:
```asm
push ebp
mov  ebp, esp      ; Set frame pointer
sub  esp, #locals  ; Allocate locals
push ebx/esi/edi   ; Save callee-saved as needed
Typical epilogue:textpop  ebx/esi/edi
mov  esp, ebp      ; Or leave (mov esp,ebp; pop ebp)
pop  ebp
ret
```
Recursion note: Each recursive call pushes a new return address (by call) and frame. `EBP` chain supports unwinding/debugging.

#### x86-64 (AMD64 / x86-64)

Calling convention (System V on Linux/macOS; similar Windows): Args in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (then stack). Callee-saved: `rbx`, `rbp`, `r12`–`r15`. Return in `rax`. Red zone (128 bytes below `RSP` usable in leaf functions). 16-byte `SP` alignment before call (stricter than XLR8).

Typical prologue (with frame pointer):
```asm
push rbp
mov  rbp, rsp
sub  rsp, #locals     ; Allocate (multiple of 16 usually)
push rbx/r12/etc.     ; Callee-saved as needed
```

Typical epilogue:
```asm
pop  rbx/r12/etc.
mov  rsp, rbp
pop  rbp
ret
```

Recursion note: Return address pushed by `call`. Frame pointer (`RBP`) optional but common for debugging/unwinding. Leaf functions may omit frame setup.

#### RISC-V (RV32/RV64)

Calling convention: Args in `a0`–`a7`. Return in `a0`/`a1`. Callee-saved: `s0`–`s11` (s0 often `FP`), `sp`. Caller-saved: `t0`–`t6`, `a0`–`a7`, `ra` (`x1`). Stack 16-byte aligned. Grows down.

Typical prologue:
```asm
addi sp, sp, -frame_size   ; Allocate (multiple of 16)
sd   ra, offset(sp)        ; Save return address (if non-leaf)
sd   s0, offset2(sp)       ; Save used callee-saved (s0–s11)
addi s0, sp, frame_size    ; Optional: set frame pointer
```

Typical epilogue:
```asn
ld   ra, offset(sp)        ; Restore
ld   s0, offset2(sp)
addi sp, sp, frame_size
jr   ra                    ; Or ret pseudoinstruction
```

Recursion note: `jal ra, func` (or `call`) saves return address in `ra`. Must save `ra` on stack for recursion. Callee restores everything.


### 1.1 Inline `enter` in Generated Code

**Current cost**: 5-6 C calls per `enter` (set_flags + enter + get_frame +
reload_shadows + get_flags), plus full spill/reload of all shadow registers.

**What `enter(locals)` actually does** (from `runtime.h:328-352`):

```
stack -= locals                    // decrement stack pointer
finfo = (frame-stack)<<16 | (byte(flags)<<8) | locals
frame = stack                      // frame = new stack
push(finfo)                        // frame[-1] = packed info word
arg_locals = old_frame             // for get/set arg access
```

This is 5 pointer/integer operations on runtime fields (`stack`, `frame`,
`flags`).  In JIT code, the frame pointer and flags register are already in
hardware registers.  The only runtime fields the JIT does not hold in registers
are `rt.stack` and `rt.arg_locals`.

**Proposed inline sequence** (arm64 example, ~12 instructions):

```asm
; d = locals count (compile-time constant from bytecode)
mov  x23, x19                     ; save old frame → will become arg_locals
ldr  x24, [x27, #STACK_OFF]       ; x24 = rt.stack  (x27 = HW_ANCHOR = &rt)
sub  x24, x24, #(d * 8)           ; stack -= locals  (scaled by slot size)
; pack finfo: (old_frame - new_stack)<<16 | (flags_byte<<8) | locals
sub  x25, x19, x24                ; frame - stack (in bytes)
lsr  x25, x25, #3                 ; convert to slot count
lsl  x25, x25, #16                ; << 16
and  x22t, x22, #0xFF             ; extract flags byte
orr  x25, x25, x22t, lsl #8      ; | (flags << 8)
orr  x25, x25, #d                 ; | locals
mov  x19, x24                     ; frame = stack
sub  x24, x24, #8                 ; push finfo
str  x25, [x24]                   ;   frame[-1] = finfo
str  x24, [x27, #STACK_OFF]       ; rt.stack = stack - 1
str  x23, [x27, #ARGLOCALS_OFF]   ; rt.arg_locals = old frame
; reload_shadows unnecessary — frame[0..locals-1] are uninitialized
```

**Savings vs current**: eliminates 2 C calls (`xlr8_set_flags` + `xlr8_enter`),
the full `spill_frame()` of all shadow registers, `restore_frame_ptr`, and
`reload_shadows`.  Net savings: **~30-50 instructions** per `enter`.

**Prerequisite**: The JIT must know the runtime struct offsets of `stack` and
`arg_locals` at compile time.  These could be exposed via `xlr8_runtime_fn()`
or as constants in a shared header.

**Note on error checking**: The interpreter's `enter` checks for stack overflow
(`stack - locals - 1 <= heap`).  The JIT can either (a) emit a compare+branch
to an error trampoline, or (b) omit the check when the stack is known to be
large enough (e.g., for small `locals` counts after a recent check).

### 1.2 Inline `leave` in Generated Code

**What `leave()` does** (from `runtime.h:355-371`):

```
finfo  = frame[-1]                 // read packed info
locals = byte(finfo)               // extract locals count
flags  = byte(finfo>>8) | ((flags>>8)<<8)  // restore low flags, keep high
stack  = frame + locals            // restore stack
frame += finfo >> 16               // restore frame pointer
arg_locals = arguments()           // frame + (frame[-1] >> 16)
```

**Proposed inline sequence** (arm64, ~10 instructions):

```asm
ldr  x25, [x19, #-8]              ; finfo = frame[-1]
and  x23, x25, #0xFF              ; locals = byte(finfo)
ubfx x24, x25, #8, #8             ; saved_flags_byte = byte(finfo >> 8)
bfi  x22, x24, #0, #8             ; flags[7:0] = saved_flags_byte
add  x24, x19, x23, lsl #3        ; stack = frame + locals * 8
str  x24, [x27, #STACK_OFF]       ; rt.stack = stack
lsr  x25, x25, #16                ; frame_offset = finfo >> 16
add  x19, x19, x25, lsl #3        ; frame += offset * 8
str  x19, [x27, #ARGLOCALS_OFF]   ; rt.arg_locals = arguments() = frame
; Note: arg_locals = arguments() = frame + (new_frame[-1] >> 16)
; but the simpler version is arg_locals = new_frame (caller frame)
```

**Savings**: eliminates 3 C calls (`xlr8_leave` + `xlr8_get_frame` +
`xlr8_get_flags`), all shadow spill/reload.  Net savings: **~25-40 instructions**.

### 1.3 Inline Known `sys` Opcodes from `native.tbl`

The `sys` opcode dispatches through `xlr8_sys` → `rt.native(t)` → map lookup →
function pointer call.  For the ~120 operations defined in `native.tbl`, the
token-to-function mapping is fixed at compile time.  The JIT can inline their
implementations directly.

**Classification of `native.tbl` entries by inlineability**:

#### Category A: Single-instruction or trivial (inline directly)

| Native | Semantics | JIT inline |
|--------|-----------|------------|
| `i2f` | `DFLT = SINT` | `scvtf s0, xd` (arm64) |
| `f2i` | `DINT = value_t(SFLT)` | `fcvtzs xd, s0` (arm64) |
| `i2d` | `DDBL = SINT` | `scvtf d0, xd` |
| `d2i` | `DINT = value_t(SDBL)` | `fcvtzs xd, d0` |
| `u2f` | `DFLT = SNAT` | `ucvtf s0, xd` |
| `f2u` | `DNAT = uvalue_t(SFLT)` | `fcvtzu xd, s0` |
| `f2d` | `DDBL = SFLT` | `fcvt d0, s0` |
| `d2f` | `DFLT = SDBL` | `fcvt s0, d0` |

These are 1-3 native instructions, no C call needed.

#### Category B: Direct C library call (inline the call, skip dispatch)

| Native | C function | Args |
|--------|------------|------|
| `malloc` | `malloc(SNAT)` | 1 arg, 1 result |
| `free` | `free(DPTR)` | 1 arg |
| `memcpy` | `memcpy(DPTR, SPTR, LSIZE)` | 3 args |
| `strlen` | `strlen(SCSTR)` | 1 arg, 1 result |
| `strcmp` | `strcmp(SCSTR, XCSTR)` | 2 args, 1 result |
| `strcpy` | `strcpy(DCPTR, SCSTR)` | 2 args, 1 result |
| `strdup` | `strdup(SCSTR)` | 1 arg, 1 result |
| `memset` | `memset(DPTR, SINT, LSIZE)` | 3 args |
| `memcmp` | `memcmp(SPTR, XPTR, LSIZE)` | 3 args, 1 result |
| `realloc` | `realloc(DPTR, SNAT)` | 2 args, 1 result |
| `sin`/`cos`/`sqrt`/etc. | `std::sin(SFLT)` | 1 arg, 1 result |

These still require a C call, but the JIT can decode the register arguments
from the bytecode at JIT time, load them into the C ABI argument registers
directly, and call the libc function — bypassing the `xlr8_sys` → map lookup →
`native_fn` chain entirely.

**Savings per Category B sys call**: eliminates `xlr8_set_flags` call,
`xlr8_sys` call (which does map lookup), `xlr8_after_sys` call, and most of the
flag save/restore.  Net: **~40-60 instructions saved** per occurrence.

#### Category C: Volatile loads/stores (inline as memory access)

| Native | Semantics | JIT inline |
|--------|-----------|------------|
| `st8v` | `*(volatile u64*)DNAT = SNAT` | `str xs, [xd]` with barrier |
| `ld8v` | `DNAT = *(volatile u64*)SNAT` | `ldr xd, [xs]` with barrier |
| `st4v`/`ld4v` | 32-bit volatile | sized load/store |
| `st2v`/`ld2v` | 16-bit volatile | sized load/store |
| `st1v`/`ld1v` | 8-bit volatile | sized load/store |
| `ld*vs` | sign-extending volatile loads | load + sign-extend |

These are 1-2 native instructions.  A `DMB` barrier may be needed depending on
the memory ordering model.

#### Category D: Runtime-dependent (require rt access)

| Native | Why not trivially inlineable |
|--------|------------------------------|
| `globals` | `rt.globals.resize(klen)` — C++ vector operation |
| `alloca`/`valloca` | `rt.alloca()` — runtime stack management |
| `cmdline`/`env` | `rt.argv()`/`rt.env()` — runtime accessor |
| `infix`/`prefix`/etc. | Parser mode changes |
| `read_*`/`write_*` | Stream I/O through `rt.istream()`/`rt.ostream()` |
| `read_scanner` | Scanner integration |
| `stack` | `DNAT = (uintptr_t) rt.stack` — could inline if rt offset known |

These require access to runtime internals that are not in JIT registers.
They should still call C helpers, but can use direct `call` to the target
function (skipping the `xlr8_sys` dispatch).

#### Category E: Branch-carrying ops (require special handling)

| Native | Why special |
|--------|-------------|
| `branch_on_eof` | `BR_OFF; ISTREAM; if (eof) BRANCH` — modifies PC |
| `branch_on_bad` | `BR_OFF; ISTREAM; if (bad) BRANCH` — modifies PC |
| `trap_error` | `BR_OFF; if (rt.error()) BRANCH` — modifies PC |

These decode a branch offset from the bytecode args and conditionally redirect
the PC.  The JIT can handle them like local branches: decode the branch offset
at JIT time, call the predicate function (e.g., `stream.eof()`), and emit a
conditional branch to the target.

#### Category F: Float push/pop and fmod (mixed)

| Native | Notes |
|--------|-------|
| `fpush`/`dpush` | `rt.push(SFLT/SDBL)` — stack push |
| `fpop`/`dpop` | `DFLT/DDBL = rt.pop<>()` — stack pop |
| `fmod`/`dmod` | `std::fmod()` — C library call |

`fpush`/`fpop` could inline (they are stack operations, same as `push`/`pop`
but with float casting).  `fmod` is a direct C call.

### 1.4 Disabled `sys` Opcodes — JIT Abort

When the user disables a sys opcode for security reasons, the JIT should emit
an immediate abort (e.g., `ud2` on x86, `brk #1` on arm64, `ebreak` on
RISC-V).  This is simpler than the current path and provides a hard guarantee
that disabled code cannot execute.

**Implementation**: At JIT time, check whether the token is in the runtime's
native map.  If absent and the token appears in `native.tbl` (known opcode that
was explicitly removed), emit a trap instruction instead of the sys dispatch
sequence.

### 1.5 General `sys` Fallback for User-Extended Opcodes

Opcodes not in `native.tbl` (user-registered via `rt.native_add()`) must still
go through the runtime dispatch.  The current `xlr8_sys` → `rt.native(t)` path
is correct for these.  However, the dispatch can be streamlined:

- Skip `xlr8_set_flags` / `xlr8_get_flags` if the user function is known not
  to inspect flags (most don't).
- Combine `xlr8_sys` + `xlr8_after_sys` into a single call that returns both
  the success flag and any PC redirect address.

---

## 2. Cross-Platform: Redundant Spill/Reload Elimination

All four backends share a pattern of redundant `reload_shadows()` in
`emit_sys`.  This is the single highest-ROI fix across the entire backend.

### 2.1 Redundant `reload_shadows` in `emit_sys` (All Platforms)

Every backend's `emit_sys` reloads shadow registers from frame memory, then
immediately calls another C function that clobbers them, then reloads again.

**arm64** (`arm64-emit.cpp:329`): reloads 16 registers via `reload_shadows()`,
then calls `reload_flags_reg()` (which calls `rtcall("xlr8_get_flags")`,
clobbering x0-x17), then calls `rtcall("xlr8_after_sys")` (clobbering again).
The first reload is entirely wasted.

**riscv64** (`riscv64-emit.cpp:324`): same pattern — `reload_shadows()` loads
x10-x25, then `reload_flags_reg()` clobbers x10-x17 via `rtcall`.

**x86-64** (`x86-emit.cpp:318-320`): `reload_flags_reg()` internally calls
`emit_runtime_call("xlr8_get_flags")` which includes its own
`reload_shadows()`.  The explicit `reload_shadows()` at line 319 is therefore
redundant.

**arm32** (`arm-emit.cpp:327`): same pattern as riscv64.

**Fix**: Remove the first `reload_shadows()` in each platform's `emit_sys`.
Only reload after all C calls are complete.

**Savings per platform**:

| Platform | Instructions saved | Code bytes saved |
|----------|-------------------|------------------|
| arm64    | 16 (16 LDR) | 64 |
| riscv64  | 16 (16 LD) | 64 |
| x86-64   | ~32 bytes | 32 |
| arm32    | 4 (4 LDR) | 16 |

### 2.2 Redundant `restore_frame_ptr()` Calls (arm64)

`arm64::emit_sys` calls `restore_frame_ptr()` 6 times (directly + inside
`spill_frame` and `reload_shadows`).  Each emits `mov x19, x27`.  The call at
line 313 is immediately followed by `spill_frame()` which calls it again.

**Fix**: Remove the bare `restore_frame_ptr()` at line 313.  Saves 1
instruction.

### 2.3 Batched Return-Address Save/Restore (riscv64)

Each `call_abs` on riscv64 saves/restores `ra` to the stack (4 instructions:
`addi sp; sd ra; ld ra; addi sp`).  In `emit_sys`, 5 consecutive calls emit 20
instructions of redundant stack manipulation.

**Fix**: Save `ra` to a callee-saved register (e.g., `x26`/`s10`) once before
the first call, restore once after the last.  **Saves 18 instructions** in the
sys path.

---

## 3. ARM64-Specific Optimizations

### 3.1 Operate Directly on Shadow Registers

**Current**: `binop_local(d, s, "add")` with both shadowed copies both operands
to scratch registers, operates, copies back.  5 instructions.

**Proposed**: `add xd, xd, xs; str xd, [x19, #off]`.  2 instructions.

**Savings**: 3 instructions per integer binop on shadowed locals.

This applies to all integer operations (add, sub, mul, and, or, xor, shl,
shr, div, mod) and to comparison (`cmp xd, xs` directly instead of via
scratch).

Affected functions: `binop_local`, `cmp_local`, `incr_local`, `decr_local`,
`neg_local`, `not_local`, `bra_zero`, `bra_signed`, `dbra_local`.

### 3.2 BFI for Predicate Set (Replace Branch-Over Pattern)

**Current** `set_pred_cmp`: clear bit with mask+AND, then `cset`, then
branch-over + `orr`.  6 instructions + 1 branch.

**Proposed**: `cset x23, cond; bfi x22, x23, #p, #1`.  2 instructions, no
branch.

Combined with §3.1 (shadow direct `cmp`), a full compare-and-set-predicate
drops from 9 instructions to 3.

### 3.3 STP/LDP for Spill/Reload

**Current**: `spill_frame` emits N individual `str` instructions;
`reload_shadows` emits N individual `ldr` instructions.

**Proposed**: use `stp`/`ldp` pairs (2 registers per instruction).  For 16
shadow locals: 16 instructions → 8.

**Savings**: 8 instructions per `spill_frame` and 8 per `reload_shadows`.  In
`emit_sys` (which calls these multiple times), saves 16-24 instructions total.

### 3.4 Logical Immediates for AND/OR/XOR

AArch64 supports bitmask immediates for logical operations.  Single-bit masks
(`1 << p`), byte/word masks (`0xFF`, `0xFFFF`, `0xFFFFFFFF`), and many
repeating patterns can be encoded directly.

**Current**: `andu(d, 0xFF)` loads mask via `movz x23, #0xFF` then
`and xd, xd, x23`.  2-4 instructions for the mask + 1 for AND.

**Proposed**: `and xd, xd, #0xFF`.  1 instruction.

Requires implementing `encode_logical_imm()` (~40 lines) which translates a
64-bit value to the N/immr/imms fields.

### 3.5 CMN for Negative Compare-Immediates

`cmp_imm` currently truncates negative values via `cmp_r_imm(r, v & 0xFFF)`,
producing wrong results for values outside 0..4095.

**Fix**: Use `cmn xd, #(-v)` when `-v` fits in 12 bits.  Both a correctness
fix and an optimization (avoids materializing the constant).

### 3.6 Fold Offsets into LDR/STR Immediate Field

`load_mem_off` and `store_mem_off` always materialize the offset in a scratch
register and add it to the base.  AArch64 LDR/STR support unsigned 12-bit
scaled offsets directly.

**Savings**: 2 instructions per load/store with small aligned offset.

### 3.7 Optimal MOVZ Starting Halfword

`load_imm64` always starts with `movz xd, #hw0`.  For values like
`0x0001_0000_0000_0000` (only hw3 nonzero), this wastes the initial MOVZ on
zero.

**Fix**: Find the first non-zero halfword and use it for the base MOVZ.
Saves 1 instruction for sparse upper-halfword values.

### 3.8 Helper Calls: Replace MOVZ/MOVK + BLR Absolute Materialization

**Status**: deferred (recorded 2026-07-24).  Correct today; optimize later.

**Current** (`arm64::call_ptr` → `set_imm` + `blr x16`): every C helper call
materializes a full 64-bit absolute address, typically:

```asm
mov   x16, #0xe7e0
movk  x16, #0x10a, lsl #16
movk  x16, #0x1, lsl #32
blr   x16
```

That is ~3–4 instructions (12–16 bytes) plus an indirect branch per call.
Fine for correctness; heavy for hot paths (`xlr8_jit_write_*`, sys helpers).

**Preferred (when JIT buffer and helpers are within range)**:

1. **PC-relative helper pointer table** next to the code buffer (see also §7.4):
   `adrp x16, page` / `ldr x16, [x16, #off]` / `blr x16` — usually 3
   instructions, no 64-bit immediate construction.  Range ±4 GiB from the JIT
   page.
2. **Direct `bl`** with a post-`mmap` reloc when the helper is within ±128 MiB
   of the call site — 1 instruction, best case.
3. Keep MOVZ/MOVK + BLR as fallback for far / unknown targets.

**Savings**: ~1–3 instructions and a cheaper call pattern per helper site;
tens of bytes per translated function that prints or calls sys helpers.

Cross-platform twin: x86-64 §4.6 (`call [rip+disp32]`), riscv64 AUIPC+LD
(§7.4).

---

## 4. x86-64-Specific Optimizations

### 4.1 Streamline `set_pred_cmp`

**Current**: `setcc al; movzx eax,al; mov r11,rax; btr r15,p; test r11,r11;
jz +5; bts r15,p`.  24 bytes.

**Proposed**: `setcc al; btr r15,p; test al,al; jz +5; bts r15,p`.  17 bytes.

The MOVZX and MOV-to-scratch are unnecessary: BTR clobbers only CF (not ZF),
and `test al,al` checks the SETcc result directly.

**Savings**: 7 bytes per comparison (29% reduction).

### 4.2 Fused `dbra_local`

**Current**: `dbra_local` calls `decr_local` (load+dec+store), then
`bra_zero` (load AGAIN + test + jnz).  The second load is redundant.

For non-shadow locals, memory-destination DEC sets ZF directly:

```asm
dec qword [r12+disp32]     ; 8 bytes, sets ZF
jnz rel32                  ; 6 bytes
```

14 bytes total.  Current: 36 bytes.  **Saves 22 bytes** (61% reduction).

### 4.3 Memory-Destination AND/OR/XOR Immediates

`binop_imm` only uses memory-destination encoding for add/sub.  AND/OR/XOR
with small immediates should use the same `emit_alu_imm_frame` path.

**Savings**: 8-14 bytes per AND/OR/XOR immediate on non-shadow locals.

### 4.4 MOVZX for `zext_local`

**Current**: loads mask constant, ANDs.  For w=8: `mov ecx,0xFF; and rax,rcx` =
8 bytes for the mask+AND.

**Proposed**: `movzx eax,al` (3 bytes) for w=8, `movzx eax,ax` (3 bytes) for
w=16, `mov eax,eax` (2 bytes, zero-extends on x86-64) for w=32.

**Savings**: 5-6 bytes per `zext_local`.

### 4.5 Redundant Push/Pop Around Final `reload_shadows`

In `emit_sys` (line 327-331), `push rax; reload_rt_regs(); reload_shadows();
pop rax` — on x86-64, `reload_rt_regs()` is a no-op, and `reload_shadows()`
generates pure loads that don't touch rax.  The push/pop is unnecessary.

**Savings**: 2 bytes per sys opcode.

### 4.6 RIP-Relative Function Pointer Table

**Current**: every C helper call uses `movabs rax, imm64; call rax` (12 bytes).

**Proposed**: allocate a table of function pointers near the JIT code.
`call [rip+disp32]` = 6 bytes.

**Savings**: 6 bytes per call.  With ~20 runtime functions called throughout a
translation, this saves hundreds of bytes per translated function.

---

## 5. RISC-V 64-Specific Optimizations

### 5.1 Branchless Predicate Set

**Current** `set_pred_cmp`: 7 instructions + 1 conditional branch.  Uses
branch-over pattern (BEQ skip, load mask, OR, skip:).

**Proposed**: `sub; sltiu; slli #p; and ~mask; or result`.  5-6 instructions,
no branch.

### 5.2 Add ANDI/ORI Encoders

The header has `xori` and `addi` but no `andi` or `ori`.  These are standard
RV64I I-type instructions.

**Impact**: `set_pred(p, true)` for p < 11 becomes `ori FLAGS, FLAGS, (1<<p)`
= 1 instruction (vs current 2).  `bra_pred` for p < 11 becomes
`andi SCR, FLAGS, (1<<p)` = 1 instruction (vs current 2).

### 5.3 Remove Wasted `reload_shadows` in `emit_sys`

Same as §2.1.  The `reload_shadows()` at line 324 loads x10-x25, which are
immediately clobbered by `reload_flags_reg()` and `rtcall("xlr8_after_sys")`.

**Savings**: 16 instructions (16 LD).

### 5.4 AUIPC-Relative Calls for Runtime Functions

**Current** `call_abs`: `load_imm64(t0, fn)` takes 6-10 instructions for a
64-bit address.  Total `call_abs` = 11-15 instructions.

**Proposed**: If JIT code is within ±2GB of runtime functions (typical),
`auipc t0, hi20; addi t0, t0, lo12; jalr ra, t0` = 3 instructions for the
call itself.

**Savings**: 4-8 instructions per `call_abs`.  In `emit_sys` with 5 calls:
20-40 instructions saved.

### 5.5 SLLI+SRLI for `zext_local`

**Current**: materializes mask via `load_imm64`, then AND.  For w=32:
4 instructions (lui+addi+slli+srli for mask) + 1 AND = 5 instructions.

**Proposed**: `slli rd, rs, (64-w); srli rd, rd, (64-w)`.  Always 2
instructions regardless of width.

### 5.6 Direct Shadow Register Operations

Same as arm64 §3.1 — `incr_local`/`decr_local`/`binop_imm` route through
scratch registers unnecessarily for shadowed locals.

**Current** `incr_local(d)` shadowed: `mv x28,xreg(d); addi x28,x28,1;
mv xreg(d),x28; sd xreg(d), [frame+off]` = 4 instructions.

**Proposed**: `addi xreg(d),xreg(d),1; sd xreg(d),[frame+off]` = 2
instructions.

### 5.7 Compare-and-Branch Fusion

RISC-V has BEQ/BNE/BLT/BGE/BLTU/BGEU that compare two registers directly.
A `cmpeq p,d,s` + `btrue p` currently generates 12 instructions + 2 branches.

With direct compare-and-branch: `beq xd, xs, target` = 1 instruction.

This requires a peephole pass or a fused `cmp_and_branch` primitive.

### 5.8 B-type Branch Range — Correctness Bug

`patch_branch_site` silently drops B-type branches that exceed ±4KiB
(logs error, returns without patching).  The branch retains offset 0.

**Fix**: emit inverted-condition B-type + J-type pair for branch relaxation:
`bne rs1,rs2,+8; jal x0, target`.  2 instructions, ±1MiB range.

---

## 6. ARM32-Specific Optimizations

### 6.0 CORRECTNESS: `add_imm`/`sub_imm`/`cmp_r_imm` for Values > 255

**Bug**: `arm.h` functions `add_imm` (line 265), `sub_imm` (line 275), and
`cmp_r_imm` (line 282) mask the immediate with `& 0xFFF` into the 12-bit field.
But ARM decodes that field as `imm8 ROR (rotate * 2)`, not as a plain 12-bit
number.  For values 256-4095, the hardware decodes a **completely different
value** than intended.

**Affected call sites**:
- `binop_imm` with "add"/"sub" for `v >= 256 && v < 4096`
- `cmp_r_imm` in `cmp_imm` for any `v >= 256`
- `add_imm(0, 15, offset)` in `call_tok`/`icall_local`/`push_pc` (currently
  works only because patched offsets happen to stay under 256 bytes)

**Fix**: Add `encode_arm_imm()` that checks all 16 rotations of an 8-bit value.
Fall back to register-load for un-encodable values:
```cpp
static bool encode_arm_imm(uint32_t v, uint &imm12) {
    for (uint rot = 0; rot < 16; rot++) {
        uint32_t un = rot ? (v << (rot*2)) | (v >> (32-rot*2)) : v;
        if (un <= 0xFF) { imm12 = (rot << 8) | un; return true; }
    }
    return false;
}
```

### 6.1 `set_pred_cmp` — BIC+ORR{cond} (Highest Impact)

**Current** (lines 639-650): 4-5 instructions — `load_imm32(~mask)`, AND,
`load_imm32(mask)`, conditional ORR.

**Proposed**: 2 instructions, no branch:
```asm
BIC   r10, r10, #(1 << p)         ; clear bit (rotated imm, no S flag)
ORR{cond} r10, r10, #(1 << p)     ; conditionally set bit
```

Both use rotated immediates (`1<<p` always fits).  BIC does not affect NZCV,
so the condition codes from the preceding CMP are preserved.

**Savings**: 2-3 instructions per comparison — and every `cmp_local`,
`cmp_imm`, `cmp_uimm` flows through `set_pred_cmp`.

### 6.2 `set_pred` — ORR/BIC with Rotated Immediate

**Current** `set_pred(p, true)`: 2 instructions (`load_imm32 #(1<<p)` + ORR).
**Current** `set_pred(p, false)`: 2-3 instructions (`load_imm32 ~(1<<p)` + AND).

**Proposed**: 1 instruction each:
```asm
ORR   r10, r10, #(1 << p)         ; set_pred(p, true)
BIC   r10, r10, #(1 << p)         ; set_pred(p, false)
```

**Savings**: 1-2 instructions per `set_pred` call.

### 6.3 Conditional Execution for `cond_mov_local` (ccopy)

**Current** (lines 688-700): 5 instructions (load mask, ANDS, BEQ, MOV, STR).

**Proposed**: 3 instructions using TST + conditional execution:
```asm
TST    r10, #(1 << p)             ; rotated imm, sets Z
MOVNE  rd, rs                     ; conditional MOV
STRNE  rd, [r11, #off]            ; conditional STR
```

**Savings**: 2 instructions + eliminates branch penalty.

### 6.4 Conditional Execution for `select_local`

**Current** (lines 703-719): 8 instructions with 2 branches.

**Proposed**: 4 instructions, branchless:
```asm
TST    r10, #(1 << p)
MOVNE  rd, rs                     ; if predicate set: d = s
MOVEQ  rd, rx                     ; if predicate clear: d = x
STR    rd, [r11, #off_d]          ; unconditional store
```

**Savings**: 4 instructions + eliminates 2 branches.

### 6.5 TST Immediate for `bra_pred`

**Current** (lines 1028-1039): 3 instructions (`load_imm32 #(1<<p)`, ANDS, Bcc).

**Proposed**: 2 instructions:
```asm
TST    r10, #(1 << p)             ; rotated immediate
BNE/BEQ target
```

**Savings**: 1 instruction per predicated branch.

### 6.6 VLDR/VSTR for Float Operations (Bypass GPR)

**Current** float binop (non-shadow): 7 instructions — LDR to GPR, VMOV to VFP,
operate, VMOV back, STR.

**Proposed**: 4 instructions — VLDR directly to VFP, operate, VSTR back:
```asm
VLDR  s0, [r11, #off_d]           ; load float directly to VFP
VLDR  s1, [r11, #off_s]           ; load float directly to VFP
VADD.F32 s0, s0, s1               ; VFP operation
VSTR  s0, [r11, #off_d]           ; store float directly from VFP
```

Frame slot offsets (`n * 8`) are always multiples of 4 and ≤1020 for
`n ≤ 127`, fitting VLDR/VSTR's byte-offset field.

**Savings**: 3 instructions per float binop (non-shadow case).

### 6.7 `zext_local` — UXTB/UXTH or AND Immediate

**Current** (lines 815-828) for w=8, shadow d: 5 instructions (load, load mask,
AND, store back, write-through).

**Proposed**:
- w=8: `AND rd, rd, #0xFF` (rotated imm) or `UXTB rd, rd` — 1 instruction
- w=16: `UXTH rd, rd` (ARMv6) — 1 instruction (0xFFFF doesn't fit rotated imm)
- w=32: `MOV rd, rd` is a no-op on ARM32 (already 32-bit) — 0 instructions

**Savings**: 1-3 instructions per zext.

### 6.8 `neg_local`/`not_local` — Skip Scratch Round-Trip

**Current** for shadow d: 4 instructions (MOV to scratch, RSB/MVN, MOV back, STR).

**Proposed**: 2 instructions:
```asm
RSB   rd, rd, #0                  ; negate in-place
STR   rd, [r11, #off]             ; write-through
```

**Savings**: 2 instructions per neg/not on shadow locals.

### 6.9 `binop_imm` AND/OR/XOR with Rotated Immediate

When the value fits a rotated 8-bit immediate (0xFF, single-bit values, etc.):
```asm
AND   rd, rd, #0xFF               ; 1 instruction vs current 2
```

**Savings**: 1 instruction per qualifying bitwise-immediate operation.

### 6.10 Redundant `reload_shadows` in `emit_sys`

Same pattern as §2.1.  The `reload_shadows()` after `rtcall("xlr8_get_frame")`
is immediately clobbered by `rtcall("xlr8_get_flags")` and
`rtcall("xlr8_after_sys")`.

**Savings**: 4 instructions (4 × LDR for 4 shadow locals).

### 6.11 Hardware Division Portability

The backend emits SDIV/UDIV (ARMv7VE: Cortex-A15, A17).  These cause SIGILL
on Cortex-A7/A8/A9.  Options: (a) runtime check `/proc/cpuinfo` for `idiva`,
(b) always call `__aeabi_idiv`/`__aeabi_uidiv`, (c) document ARMv7VE minimum.

### 6.12 `store_imm(d, 0)` — Reuse Zero Register

**Current**: 4 instructions (MOVW rd,#0; STR low; MOVW r12,#0; STR high).
**Proposed**: 3 instructions (MOV rd,#0; STR low; STR rd, [r11,#off+4]).

**Savings**: 1 instruction per zero-store.

---

## 7. Design-Level Optimizations (Deferred)

These require more substantial infrastructure changes and are better addressed
after the per-platform instruction-level optimizations.

### 7.1 Shadow Register Dirty Tracking

Add a `uint16_t dirty_mask` bitmask to each backend.  Set a bit when a shadow
register is written; clear it when the register is spilled to frame memory.
`spill_frame()` only writes dirty registers; `store_local()` can skip the
write-through STR when the value will be overwritten again before the next
spill point.

**Impact**: For a typical inner loop doing 3-5 operations per iteration between
spill points, saves 2-4 STR instructions per iteration.

### 7.2 Code Caching

`run_native()` retranslates from scratch on every invocation and unmaps the
code after execution.  A simple hash map from bytecode entry point to JIT code
address would eliminate repeated translation of hot functions.

### 7.3 Combined Runtime Calls

Replace the `xlr8_set_flags` + `xlr8_sys` + `xlr8_get_frame` +
`xlr8_get_flags` + `xlr8_after_sys` sequence with a single
`xlr8_sys_combined(t, args, flags, next_pc)` that returns a packed result
(success + redirect address + updated flags).  This eliminates 4 C function
calls and their associated register save/restore overhead.

### 7.4 Function Pointer Table / PC-Relative Calls

Pre-populate a table of runtime function pointers near the JIT code buffer.
Each backend can then use PC-relative addressing (ADRP+LDR on arm64,
`call [rip+disp32]` on x86-64, AUIPC+LD on riscv64) instead of materializing
full absolute addresses.

Arm64 detail and concrete current sequence: §3.8 (MOVZ/MOVK + BLR → ADRP/LDR
or direct BL).

---

## 8. Prioritized Action Plan

### Phase 0 — Correctness Fixes (do first)

| # | Item | Platforms | Impact |
|---|------|-----------|--------|
| X1 | `add_imm`/`sub_imm`/`cmp_r_imm` for v > 255 (§6.0) | arm32 | **wrong code** |
| X2 | `cmp_imm` negative truncation (§3.5) | arm64 | **wrong results** |
| X3 | B-type branch range > ±4KiB (§5.8) | riscv64 | **silent miscompile** |

### Phase A — Immediate Wins (trivial, no risk)

| # | Item | Platforms | Savings |
|---|------|-----------|---------|
| A1 | Remove redundant `reload_shadows` in `emit_sys` (§2.1, §6.10) | All 4 | 4-16 insns/sys |
| A2 | Remove redundant `restore_frame_ptr` in `emit_sys` (§2.2) | arm64 | 1 insn/sys |
| A3 | Remove redundant push/pop in `emit_sys` (§4.5) | x86-64 | 2 bytes/sys |
| A4 | `store_imm(d, 0)`: reuse zero register (§6.12) | arm32 | 1 insn/zero-store |

### Phase B — Shadow Register Direct Operations (moderate, high ROI)

| # | Item | Platforms | Savings |
|---|------|-----------|---------|
| B1 | binop_local: operate on shadow regs directly (§3.1) | arm64 | 3 insns/binop |
| B2 | incr/decr_local: skip scratch round-trip (§3.1, §5.6) | arm64, riscv64 | 2 insns each |
| B3 | bra_zero/bra_signed: CBZ/CBNZ on shadow (§3.1) | arm64 | 1 insn each |
| B4 | dbra_local: fused decrement+branch (§3.1, §4.2) | arm64, x86-64 | 3-22 bytes |
| B5 | cmp_local: compare shadow regs directly (§3.1) | arm64, riscv64 | 2 insns/cmp |
| B6 | neg/not_local: skip scratch (§6.8) | arm32 | 2 insns each |

### Phase C — Predicate Optimization

| # | Item | Platforms | Savings |
|---|------|-----------|---------|
| C1 | BFI for set_pred_cmp (§3.2) | arm64 | 4 insns/cmp |
| C2 | Eliminate MOVZX+MOV in set_pred_cmp (§4.1) | x86-64 | 7 bytes/cmp |
| C3 | Branchless predicate set (§5.1) | riscv64 | 1-2 insns/cmp |
| C4 | ANDI/ORI encoders for pred test/set (§5.2) | riscv64 | 1 insn/pred op |
| C5 | BIC+ORR{cond} for set_pred_cmp (§6.1) | arm32 | 2-3 insns/cmp |
| C6 | ORR/BIC immediate for set_pred (§6.2) | arm32 | 1-2 insns each |
| C7 | TST immediate for bra_pred (§6.5) | arm32 | 1 insn/branch |
| C8 | Cond exec for cond_mov/select (§6.3, §6.4) | arm32 | 2-4 insns each |

### Phase D — Instruction Encoding Improvements

| # | Item | Platforms | Savings |
|---|------|-----------|---------|
| D1 | STP/LDP for spill/reload (§3.3) | arm64 | 8 insns/spill |
| D2 | Memory-dest AND/OR/XOR (§4.3) | x86-64 | 8-14 bytes |
| D3 | MOVZX for zext_local (§4.4) | x86-64 | 5-6 bytes |
| D4 | SLLI+SRLI for zext_local (§5.5) | riscv64 | 1-3 insns |
| D5 | Logical immediates (§3.4) | arm64 | 1-3 insns |
| D6 | AUIPC-relative calls (§5.4) | riscv64 | 4-8 insns/call |
| D7 | LDR/STR offset folding (§3.6) | arm64 | 2 insns |
| D8 | VLDR/VSTR for float ops (§6.6) | arm32 | 3 insns/float binop |
| D9 | UXTB/UXTH for zext_local (§6.7) | arm32 | 1-3 insns |
| D10 | Rotated imm for AND/OR/XOR (§6.9) | arm32 | 1 insn each |

### Phase E — Architectural Changes (high effort, highest total impact)

| # | Item | Platforms | Savings |
|---|------|-----------|---------|
| E1 | Inline enter (§1.1) | All 4 | ~30-50 insns/enter |
| E2 | Inline leave (§1.2) | All 4 | ~25-40 insns/leave |
| E3 | Inline Category A sys opcodes (§1.3) | All 4 | ~50-70 insns/sys |
| E4 | Direct C calls for Category B sys opcodes (§1.3) | All 4 | ~40-60 insns/sys |
| E5 | JIT abort for disabled sys opcodes (§1.4) | All 4 | security feature |
| E6 | Combined runtime call (§7.3) | All 4 | ~20-30 insns/sys |
| E7 | Dirty tracking (§7.1) | All 4 | 1-4 insns/op |
| E8 | Code caching (§7.2) | All | eliminates re-JIT |
| E9 | Compare-and-branch fusion (§5.7) | riscv64 | 9-11 insns |
| E10 | Helper call PC-relative / BL (§3.8, §4.6, §7.4) | arm64, x86-64, riscv64 | 1-3 insns/call |

---

## 9. Estimated Impact on Hot Paths

### Inner loop: `dbra + add + cmp + btrue`

| Platform | Current insns | After Phase A-D | Reduction |
|----------|--------------|-----------------|-----------|
| arm64    | ~24          | ~10             | 58%       |
| x86-64   | ~50 bytes    | ~28 bytes       | 44%       |
| riscv64  | ~28          | ~12             | 57%       |
| arm32    | ~20          | ~10             | 50%       |

### `emit_sys` (fast path, no PC redirect)

| Platform | Current insns | After Phase A-C | After Phase E1-E4 |
|----------|--------------|-----------------|-------------------|
| arm64    | ~76          | ~52             | ~15-25 (cat A/B)  |
| x86-64   | ~184 bytes   | ~150 bytes      | ~40-60 bytes      |
| riscv64  | ~120-160     | ~80-100         | ~20-30 (cat A/B)  |
| arm32    | ~44          | ~36             | ~12-20 (cat A/B)  |

### `enter` + `leave` (combined)

| Platform | Current insns | After Phase E1-E2 |
|----------|--------------|-------------------|
| arm64    | ~60          | ~22               |
| x86-64   | ~45 calls+reloads | ~20 inline  |
| riscv64  | ~60          | ~22               |
| arm32    | ~40          | ~18               |
