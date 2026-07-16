# XLR8 Backend Code Generation Review

**Date**: 2026-07-15
**Scope**: `backend/` -- all four platform backends (arm64, x86-64, arm32, riscv64) and common infrastructure
**Focus**: Clarity, correctness, and efficiency of the dynamic code generation model

---

## Executive Summary

The XLR8 backend translates bytecode opcodes into native machine code via a clean
table-driven architecture: `.tbl` files define opcodes once, and per-platform
`emit_*` virtual methods implement each opcode for every target.  The design is
sound, but all four backends share a set of cross-cutting correctness bugs
(`switch`), and each platform has its own encoding or semantic
errors.  The arm64 backend is the most mature; arm32 is an early prototype with
show-stopping issues in fundamental operations.

### Severity Legend

- **P0 -- Crash/data corruption**: Will produce wrong results or crash at runtime
- **P1 -- Broken opcode**: Specific opcodes do not work; programs using them fail
- **P2 -- Latent/conditional**: Bug exists but is not triggered by current code paths
- **P3 -- Efficiency**: Correct but significantly suboptimal
- **P4 -- Cleanup**: Dead code, stale comments, style issues

---

## 1. Architecture Overview

### 1.1 Translation Pipeline

```
bytecode --> emit.tbl switch --> emit_*() virtual calls --> native bytes in section
         |                                                         |
         |  Stage 2: callee discovery loop (tok_jumps)             |
         |  Stage 3: resolve_labels() + finish_translation()       |
         v                                                         v
    gen.cpp:translate()                               map_executable() + invoke
```

The `OP()` macro in `emit.tbl` decodes each opcode's operands inline and calls the
corresponding `emit_XXX()` pure virtual method.  `opover.tbl` declares these
methods in the base class; each arch header re-includes it for override
declarations.  This is clean and prevents signature drift.

### 1.2 Register Models by Platform

| Role              | arm64         | x86-64            | x86-32       | arm32        | riscv64       |
|-------------------|---------------|-------------------|--------------|--------------|---------------|
| Shadow locals     | x0-x15        | r8,r9,r10,rbx     | (none)       | r0-r7        | x10-x25       |
| Frame pointer     | x19           | r12               | edi          | r9           | x8 (s0)       |
| Flags/predicates  | x22           | r15               | esi          | r10          | x9 (s1)       |
| Scratch value     | x23           | r11               | ecx          | r0 (alias!)  | x28 (t3)      |
| Scratch address   | x24           | --                | --           | r8           | x29           |
| Scratch offset    | x25           | --                | --           | r2 (alias!)  | x30           |
| Intra-call IP     | x16           | --                | --           | r12          | x5 (t0)       |
| Saved link        | x28           | --                | --           | r11          | --            |
| Frame slot size   | 8 bytes       | 8 bytes           | 8 bytes      | 4 bytes      | 8 bytes       |
| Max shadow locals | 16            | 4                 | 0            | 8            | 16            |

### 1.3 Platforms and Test Machines

| Platform | JIT backend | Machine(s)              |
|----------|-------------|-------------------------|
| arm64    | arm64       | macOS (dev), torpille   |
| x86-64   | x86_64      | turbo                   |
| x86-32   | i386        | turbo-x86               |
| arm32    | arm         | turbo-arm               |
| riscv64  | riscv64     | risky (`fedora-riscv`)  |

### 1.4 Parallel Baseline (2026-07-16)

**Commit**: `f2c95aa` — *Fix arm64 codegen for bytecode locals >= 16.*

**Protocol**: Per `PLATFORM-ENABLEMENT.md` — on each machine,
`git fetch mac && git reset --hard mac/xlr8`, `git submodule update --init
--recursive`, `make`, then `make tests` (interpreter) and `make tests
OPTLEVEL=-O` (JIT).  On platforms where both pass, `make perf` and `make perf
OPTLEVEL=-O`.

#### Functional test results

| Machine     | Platform | Reference          | JIT (`-O`)         | Status |
|-------------|----------|--------------------|--------------------|--------|
| macOS       | arm64    | 225 / 0 failed     | 225 / 0 failed     | Green  |
| turbo       | x86-64   | 225 / 0 failed     | 225 / 0 failed     | Green  |
| torpille    | arm64    | 225 / 0 failed     | 225 / 0 failed     | Green  |
| risky       | riscv64  | 224 / 0 failed\*   | 2 / 222 failed\*   | Red    |
| turbo-x86   | x86-32   | 210 / 14 failed\*  | incomplete\*       | Red    |
| turbo-arm   | arm32    | 210 / 14 failed\*  | incomplete\*       | Red    |

\* Red-platform counts from earlier same-day baseline at `0282055` (suite was
224 tests then; +1 is `high-local.xlr8`).  Not re-run at `f2c95aa`.

Expected baseline: **225** functional tests per run (Jul 2026).

#### Performance suite (`make perf`)

Runs 231 tests (225 functional + 6 `performance/` benchmarks).

| Machine  | Reference          | JIT (`-O`)         | Notes |
|----------|--------------------|--------------------|-------|
| macOS    | 230 / 0 failed\*   | 230 / 0 failed\*   | Measured at `0282055` (224 functional); all six benchmarks pass |
| turbo    | 224 / 6 failed\*   | 224 / 6 failed\*   | All six `performance/` tests fail: `timeout: invalid time interval './time'` (harness issue, not codegen) |
| torpille | 227 / 3 failed\*   | incomplete\*       | `millions`, `millions-addi`, `millions-incr` fail in reference perf run |
| risky    | not run            | not run            | Functional JIT red |
| turbo-x86 | not run           | not run            | Functional tests red |
| turbo-arm | not run           | not run            | Functional tests red |

#### Embedded timing (from `make tests` summary table)

Selected benchmark times in seconds (lower is better).  From green-platform
validation at `f2c95aa` where available; risky/`-O` timings from earlier run.

| Machine   | Mode | fibonacci | millions | millions-incr | tokenization |
|-----------|------|-----------|----------|---------------|--------------|
| macOS     | ref  | 6.41      | 5.98     | 5.96          | 0.86         |
| macOS     | `-O` | 3.02      | 0.77     | 0.85          | 3.34         |
| turbo     | ref  | 7.87      | 7.00     | 6.78          | 1.94         |
| turbo     | `-O` | 3.61      | 0.97     | 0.81          | 1.91         |
| torpille  | ref  | 52.92     | —        | —             | —            |
| torpille  | `-O` | 9.24      | 12.46    | 12.07         | 16.17        |
| risky     | ref  | 5.05      | —        | —             | —            |
| risky     | `-O` | 0.09      | 27.65    | 27.17         | 0.09         |

Note: risky `-O` timings are not meaningful — only 2 of 224 functional tests
pass in JIT mode (see below).

#### Known failures (non-green platforms)

**turbo-x86 and turbo-arm** share the same 14 interpreter failures:

- Sign-extended loads: `ld2s`, `ld4s`, `ld8s`
- Sign/zero extension: `sext`, `sext32`, `zext`, `zext32`
- Recursion / re-entry: `recursive-factorial`, `recursive-fibonacci`,
  `repeated-execution`
- Stack: `sys-alloca`
- Scanner / LLVM: `expressions`, `printf-style-formatting-in-expression`,
  `llvm/e2e-factorial`

JIT (`-O`) runs abort early on the same `ld*s` failures (sign-extended load
returns `-1` instead of expected value).  Example from turbo-x86:
`ld2s` expected `-32768`, got `-1`.

**risky (riscv64)**: interpreter fully green (224/224).  JIT nearly completely
broken (2/222).  Only passing JIT tests:
`bootstrap/infix-prefix-postfix-block.xlr8` and
`bytecode/constant-redefinition.xlr8`.  Typical failure mode: correct
interpreter output concatenated with `Test FAILED` on the next test, indicating
broad codegen breakage rather than isolated opcode bugs.

### 1.5 Future Work: Shadow Local Residency Tracking

**Priority**: later optimization (P3-class).  Correctness is already handled by
eagerly syncing shadows and frame memory; this is about cutting redundant moves.

Today each shadow local is treated as if its value might need refreshing from
several places on every use.  In reality the same logical slot can already be
live in more than one place, and we do not record which place was written last.

**ARM64 register files** (important): `x0`/`w0` share one **GPR**; `v0`/`d0`/`s0`
share one **SIMD/FP** register.  Those two banks do **not** alias each other.
`fmov s0, w0` copies bits GPR → FP; it is **not** a no-op.  What *is* free is
reinterpreting within a bank (`w0`↔low 32 of `x0`, `s0`↔low 32 of `d0`/`v0`).

**ARM64 example** — bytecode local 0 may live in:

| View | Bank | Location | Notes |
|------|------|----------|-------|
| `x0` | GPR | 64-bit shadow | integer ops, loads, zero-extend |
| `w0` | GPR | low 32 bits of `x0` | same register as `x0` (no move) |
| `s0` | SIMD | FP scalar | needs `fmov` to/from `w0` |
| `d0` | SIMD | 64-bit FP view | same SIMD reg as `s0` (no move) |
| `[x19, #0]` | mem | frame slot | `str`/`ldr` after stores / spill |

So in today's `float-add` JIT, `fmov s0, w0` / `fadd` / `fmov w0, s0` is the
required GPR↔FP dance.  Residency tracking would skip a *second* crossing when
the value is already in the needed bank (e.g. keep the result in `s0` for a
following float op instead of bouncing back through `w0`), and skip `ldr x0,
[x19]` when `x0` is still valid.

Similar multi-home situations exist on other backends (x86-64 GPR vs XMM;
high locals that only live in `[frame + n*8]`).

**Work to do**

1. Per shadow slot, track residency: `InGPR` (and whether full `Xn` or only
   `Wn` is definitive), `InSIMD` (`Sn`/`Dn`), `InMem`, plus coherence flags.
2. On read, emit a cross-bank `fmov` or a mem reload only when the needed bank
   is not current; within a bank, prefer the matching width view for free.
3. On write, update tags and invalidate the other bank / mem as needed
   (integer write to `x0` clears `InSIMD`; float write to `s0` may still need
   `fmov`+`uxtw` before `InGPR` is authoritative for the full 64-bit slot).
4. Integrate with spill/reload around `sys` and calls: after a full spill,
   residency becomes `InMem` until shadows are reloaded.
5. Start with arm64 float/integer traffic (highest cross-bank noise), then
   generalize to x86-64 / riscv64 / arm32.

Do **not** block correctness work on this.  Ship as a dedicated optimization
pass once the backends are green.

---

## 2. Cross-Platform Bugs (All Backends)

### 2.1 [P0] ~~`ccopy` and `select` ignore predicate~~ — **FIXED** (2026-07-15)

**Files**: `arm64.cpp`, `x86.cpp`, `arm.cpp`, `riscv64.cpp`

Previously every backend's `cond_mov_local()` cast `(void)p` and did an unconditional
move, and `select_local()` always picked operand `s`.

**Fix**: Predicate-aware codegen on all four backends — test the predicate bit in
`HW_FLAGS`, branch over the move when clear (`ccopy`), or branch to pick `src` vs
`xtra` (`select`).  Validated by `tests/bytecode/ccopy.xlr8` and
`tests/bytecode/select.xlr8`, each exercising both set and clear predicate paths
(would pass with unconditional moves only on the true-path half).

### 2.2 [P1] `emit_switch()` calls unregistered runtime function

**Files**: `arm64-emit.cpp:461-468`, `riscv64-emit.cpp:457-464`, `x86-emit.cpp:453-461`, `arm-emit.cpp:443-450`

All backends call `rtcall("xlr8_switch", s)`, but `xlr8_switch` is not in the
`xlr8_runtime_fn()` dispatch table.  The lookup returns `nullptr`, and calling
address 0 crashes.

**Action**: Either register `xlr8_switch` in the runtime bridge table, or implement
switch/dispatch inline (binary search or jump table over the case entries).

---

## 3. ARM64-Specific Findings

### 3.1 [P0] ~~`neg_rr()` encoding puts Rm in the wrong field~~ — **FIXED** (2026-07-16)

**File**: `arm64.h:514-520`

Previously `neg_rr` ORed `rd` into bits 9:5 (Rn, already XZR), leaving Rm=0, so
it encoded `NEG Xd, X0` for every `d`.

**Fix**: Use `(rd << 16) | rd` so Rm is the source register (`SUB Xd, XZR, Xd`).
`tests/bytecode/neg.xlr8` now uses local 3 (not x0) so the old encoding cannot
pass silently.

### 3.2 [P0] ~~`emit_lds()` does not load from memory~~ — **FIXED** (already in tree)

**File**: `arm64-emit.cpp:1185-1192`

Previously the JIT copied local `s` to `d` and sign-extended the register value,
skipping the memory dereference.

**Fix**: `emit_lds()` and `emit_ldos()` delegate to `load_signext()` /
`load_signext_off()`, which load through the address in `s` then sign-extend.
Validated by `tests/bytecode/ld1s.xlr8`, `ld2s.xlr8`, `ld4s.xlr8`, and
`ld8s.xlr8` (interpreter and `-O`).

### 3.3 [P1] ~~Operations assume d < 16 without bounds checking~~ — **FIXED** (2026-07-16)

**Files**: `arm64.cpp` — `cmp_local`, `cmp_imm`, `neg_local`, `not_local`, `store_double`

These paths used `xreg(d)` directly; for `d >= 16` that aliases reserved hardware
registers (x16=HW_IP, x19=HW_FRAME, x22=HW_FLAGS, …).

**Fix**: Route comparisons through `load_local` + scratch registers (matching
riscv64); use load/operate/store via `HW_SCR_VAL` for `neg`/`not` and
`store_double` when `d >= LOC_SHADOW_COUNT`.  Validated by
`tests/bytecode/high-local.xlr8` (`enter 19`, locals 16–18, interpreter and `-O`).

### 3.4 [P3] ~~Predicate branches: 3-5 instructions instead of 1~~ — **FIXED** (2026-07-16)

**File**: `arm64.cpp` — `bra_pred()`

Previously loaded a mask via MOVZ/MOVK, ANDed with `HW_FLAGS`, then CBZ/CBNZ.

**Fix**: Emit a single `TBNZ` (`btrue`) or `TBZ` (`bfalse`) on bit `p` of
`HW_FLAGS` (x22).  `patch_branch_site()` updated to fix up TBZ/TBNZ immediates.
Validated by `tests/bytecode/btrue.xlr8` and `bfalse.xlr8` (interpreter and `-O`).

### 3.5 [P3] ~~No MOVN for negative immediates~~ — **FIXED** (2026-07-16)

**File**: `arm64.cpp` — `load_imm64()`

Previously always used MOVZ + up to 3 MOVK.  Value `-1` cost 4 instructions.

**Fix**: Prefer a MOVN-based sequence when `~v` has fewer non-zero halfwords than
`v` (e.g. `-1` → single `MOVN Xd, #0`).  Validated by
`tests/bytecode/movn-minus-one.xlr8` (output `m1=-1`); JIT dump disassembles to
a single `mov x0, #-0x1` (MOVN), not four MOVZ/MOVK words.

### 3.6 [P3] ~~Linux float binops go through C helper (~8x slower)~~ — **FIXED** (2026-07-16)

**Files**: `arm64.cpp` — `binop_local` float ops; `store_float_local`

**Root cause (torpille)**: Not the FP ops themselves.  Early inline float results
were written with 32-bit `str_w_off`, which left garbage in the upper half of
each 8-byte frame slot.  The next `emit_sys` spills/reloads full 64-bit slots
and then crashed or misbehaved.  Bisect notes from Jul 2026: failure was the
first `sys` after a float binop.

**Workaround that was in place**: Linux routed `fadd`/`fsub`/`fmul`/`fdiv` through
`xlr8_float_binop` (C), which wrote `frame[d] = uint64_t(bits)`.

**Real fix (already in `store_float_local`)**: `uxtw` + 64-bit `str` so the frame
slot matches integer locals.  With that in place, the Linux `#ifdef` C helper is
unnecessary.

**Change**: Use the same inline FMOV+FOP sequence on all hosts.  Validated on
macOS and torpille (`float-add` and full suite, interpreter and `-O`).

### 3.7 [P4] ~~Dead helper functions with encoding bugs~~ — **FIXED** (2026-07-16)

**File**: `arm64.h` / `arm64.cpp`

Helpers were unused or mis-encoded.  Kept and corrected; wired into real paths:

| Helper | Bug | Now used for |
|--------|-----|--------------|
| `ubfm_width` | Wrong opcode base (`0x9300fc00`) | `zext_local` (was mask+AND) |
| `sbfm_width` | Encoding was already correct | `sext_reg` / `sext_local` / narrow sign-extend loads |
| `ubfm_64` / `sbfm_64` | `ubfm_64` used wrong base | Identity UXTX/SXTX forms (kept; available) |
| `lsl_imm64` | `imms` always 63 | `binop_imm` `shl_imm` when `0 ≤ v < 64` |
| `lsr_imm64` / `asr_imm64` | (lsr OK; asr added) | `shr_imm` / `sar_imm` immediates |
| `str_w_off` / `ldr_w_off` | Offset `slot<<8` not `<<11` | 32-bit `store_mem` / `load_mem` (+ off / signext) |
| `frame_addr_rr` | Encoding OK | Large-slot `frame_addr` (`add xN, x19, xM, lsl #3`) |
| `stp_fp_lr` | Encoding OK | AAPCS-style prologue helper (JIT uses `stp_push`) |

Validated by existing `zext`/`sext`/`zext32`/`sext32` and shift/load tests.

### 3.8 [P4] ~~Stale HW_ANCHOR comments reference x20 instead of x27~~ — **FIXED** (2026-07-16)

**File**: `arm64.cpp` — `reload_frame_reg()`

Comment said "refresh x20"; `HW_ANCHOR` is x27.  Updated to match
`restore_frame_ptr()` and `arm64.h` (`HW_ANCHOR = 27`).

---

## 4. x86-64-Specific Findings

### 4.1 [P2] `emit_push_reg()` missing REX prefix for r8-r15

**File**: `x86.cpp:190-196`

```cpp
void x86::emit_push_reg(uint r)
{
    g(byte(0x50 + reg_value(r)));  // no REX.B for extended registers
}
```

Currently only called on i386 (never with r8-r15), but calling with HW_R12 would
emit `push rsp` instead of `push r12`.

### 4.2 [P3] `load_hw_imm()` always emits 10-byte movabs

**File**: `x86.cpp:393-405`

Every immediate load is a 10-byte `movabs r, imm64`.  Opportunities missed:
- `imm == 0`: `xor eax, eax` (2 bytes)
- `imm` fits in 32 bits unsigned: `mov eax, imm32` (5 bytes)
- Small constants: significant code bloat for the common case

### 4.3 [P3] No short (rel8) branch encoding

**File**: `x86.cpp` (all branch emission)

All conditional branches use 6-byte `0F 8x rel32`.  No attempt to use 2-byte
`7x rel8` for nearby targets.  Unconditional `gjmp()` always uses 5-byte
`E9 rel32`, never 2-byte `EB rel8`.

### 4.4 [P3] Shift-by-immediate uses movabs + cl shift

**File**: `x86.cpp:879-904`

Emits `movabs rcx, imm64` (10 bytes) + `shl rax, cl` (3 bytes) = 13 bytes.
Should use `shl rax, imm8` = 4 bytes with REX.

### 4.5 [P3] Predicate set/clear/test are 30-44 bytes each

**File**: `x86.cpp:952-966`, `x86.h:454-478`

Each predicate operation loads a 64-bit mask via movabs.  Using `BT`/`BTS`/`BTR`
instructions would reduce each to 5-7 bytes.

### 4.6 [P3] No memory-destination ALU operations

All arithmetic does load-local/operate/store-local (3 operations, ~18 bytes).
x86 supports `inc qword [r12+disp]` and similar memory-destination forms that
would halve code size for incr/decr/addi/subi.

### 4.7 [P3] `emit_leave()` generates redundant frame reload

**File**: `x86-emit.cpp:254-263` combined with `x86.cpp:1636`

`emit_runtime_call("xlr8_leave")` internally calls `reload_frame_reg()` (via the
strcmp match), and then `emit_leave()` calls it again.

### 4.8 [P1] No Windows x64 calling convention support

**File**: `x86.cpp:199-254`

All argument passing hardcodes System V ABI (rdi/rsi).  Windows uses rcx/rdx.
No callee-saved register save/restore in prologue (r12/r15 are clobbered).

---

## 5. RISC-V 64-Specific Findings

### 5.1 [P0] `not_rr()` generates ANDI instead of XORI

**File**: `riscv64.h:318-324`

```cpp
static inline uint not_rr(uint rd)
{
    return 0xfff07013u | (rd << 15) | (rd << 7);  // funct3=7 = ANDI
}
```

ANDI rd, rd, -1 is a no-op.  Should be `0xfff04013` (funct3=4 = XORI).

**Impact**: `emit_not(d)` silently does nothing.

### 5.2 [P0] Double-precision uses integer conversion instead of bitwise move

**File**: `riscv64.cpp:549-563`

```cpp
emit32le(fcvt_d_x(0, rd));   // integer-to-double CONVERSION
// ... arithmetic ...
emit32le(fcvt_x_d(rd, 0));   // double-to-integer CONVERSION
```

Frame slots contain double bit-patterns stored as integers.  Moving them to FP
registers should use `fmv.d.x` (bitwise move), not `fcvt.d.l` (numeric
conversion).  The float path correctly uses `fmv.w.x`.

**Action**: Add `fmv_d_x()` / `fmv_x_d()` helpers and use them instead of
`fcvt_d_x()` / `fcvt_x_d()`.

### 5.3 [P0] `fmv_x_w` encoding has wrong funct3

**File**: `riscv64.h:635-641`

Base `0xe0001053` has funct3=1; `FMV.X.W` requires funct3=0.  Generates an
illegal instruction on real hardware.

**Fix**: Change base to `0xe0000053`.

### 5.4 [P2] `store_double()` no bounds check for d >= 16

**File**: `riscv64.cpp:470-481`

Uses `xreg(d)` without checking `d < LOC_SHADOW_COUNT`.  For d=21, `xreg(21) = 31`
= sp.  Writing an immediate into sp is catastrophic.

### 5.5 [P2] `fcvt_x_d` has baked-in rd=4

**File**: `riscv64.h:715-721`

Base `0xc2200253` has rd=4 in bits 11:7.  Currently safe because the sole call
site uses rd=29 (29|4=29), but any other rd without bit 2 set will be wrong.

**Fix**: Change base to `0xc2200053`.

### 5.6 [P2] `slti` encoding uses wrong funct3

**File**: `riscv64.h:254-260`

Base `0x00001013` has funct3=1 (SLLI), should be funct3=2 (SLTI).  Currently
unused but will silently generate shifts instead of comparisons if ever called.

### 5.7 [P3] Shift-by-immediate uses register-register path

**File**: `riscv64.cpp:621-631`

`SLLI`/`SRAI`/`SRLI` helpers are defined in the header but `binop_imm` loads the
shift amount into a register and uses `SLL`/`SRA`/`SRL`.  Wastes 1-2 instructions
per shift.

### 5.8 [P2] Conditional branch range not validated

**File**: `riscv64.cpp:1058-1080`

B-type branches have a +/-4KB range.  `patch_branch_site()` does not check whether
the offset fits, silently truncating large offsets.

---

## 6. ARM32-Specific Findings

**Overall assessment: Early prototype.  Multiple show-stopping bugs prevent
execution of non-trivial programs.**

### 6.1 [P0] `ldr_off` / `str_off` U-bit is inverted

**File**: `arm.h:212-225`

```cpp
static inline uint ldr_off(uint rd, uint rn, uint off) {
    return 0xE5100000u | ...;  // U=0: offset SUBTRACTED
}
```

Base `0xE5100000` has bit 23 (U) = 0, meaning the offset is subtracted.  Every
`ldr_off(r, HW_FRAME, n*4)` loads from `[r9, #-n*4]` instead of `[r9, #+n*4]`.

**Fix**: Use `0xE5900000` (ldr) and `0xE5800000` (str).

### 6.2 [P0] Comparisons always produce false

**File**: `arm.cpp:455-489`

```cpp
set_pred(p, !strcmp(op, "cmpeq"));  // op is "eq", not "cmpeq" --> always false
```

The comparison instruction sets CPSR flags via `cmp_rr()`, but `set_pred()` ignores
the CPSR entirely and unconditionally clears the predicate bit.

**Action**: Implement proper condition-code reading, e.g., using conditional
`ORR` with the ARM condition field.

### 6.3 [P0] `gsym_addr()` missing -8 pipeline offset

**File**: `arm.cpp:238-258`

ARM branch offsets are relative to PC = instruction_address + 8.  The formula
`(a - n) / 4` is wrong; correct is `(a - n - 8) / 4`.  Compare with
`patch_branch_site` (line 685) which gets this right.

### 6.4 [P0] Double-precision truncated to 32 bits

**File**: `arm.cpp:334-343`

`store_double()` calls `store_imm(d, value_t(bits))` which truncates the 64-bit
representation to 32 bits.  All double-precision values lose their upper half.

### 6.5 [P1] Division, modulo, shift-immediate, bitwise-immediate all trap

**Files**: `arm-emit.cpp:657-710`, `arm-emit.cpp:912-970`

`binop_local` and `binop_imm` do not handle operation strings like `"imul"`,
`"idiv"`, `"shl_imm"`, `"and"`, `"or"`, `"xor"`.  All fall through to `trap(op)`.

### 6.6 [P1] `sext_local` and `zext_local` are no-ops

**File**: `arm.cpp:571-589`

Both ignore the width parameter.  Sign/zero extension after narrow loads is broken.

### 6.7 [P1] Memory access size ignored -- all loads/stores are 4-byte

**File**: `arm.cpp:593-641`

`store_mem` and `load_mem` ignore the `sz` parameter.  `st1`, `st2`, `ld1`, `ld2`,
`ld8`, `st8` all behave as 4-byte operations.

### 6.8 [P1] `frame_addr` uses wrong shift for slot calculation

**File**: `arm.cpp:674`

Encodes `LSL #1` (r9 + s*2) instead of `LSL #2` (r9 + s*4).  Every indexed
frame access reads/writes the wrong slot.

### 6.9 [P1] Missing callee-saved register save/restore

**File**: `arm.cpp:261-278`

`begin_translation` only saves LR.  r4-r11 are clobbered without being saved,
violating the AAPCS.

### 6.10 [P1] VFP helper encodings are incorrect

**File**: `arm.h:286-336`

`vadd_ss`, `vsub_ss`, `vmul_ss`, `vdiv_ss` have `sm << 20` (should be bits 3:0).
`vmov_s_r` and `vmov_r_s` have wrong N-bit placement.  These are dead code
(hardcoded instruction words are used instead), but they are traps for maintenance.

### 6.11 [P2] Scratch register aliases shadow locals

**File**: `arm.h:55`, `arm.cpp` (various)

`HW_SCR_VAL` aliases r0 (frame[0]) and `HW_SCR_OFF` aliases r2 (frame[2]).
Operations that use these as scratch can corrupt locals 0 and 2 without spilling.

---

## 7. Common Infrastructure Findings

### 7.1 [P1] Sign-extension load tests failing

**Evidence**: Untracked `tests/bytecode/ld{1,2,4,8}s.err` files in working tree

The `lds` / `ldos` opcodes are broken across backends.  arm64's `emit_lds()`
skips the memory dereference entirely (Section 3.2).  This likely explains the
test failures.

### 7.2 [P2] `emit.tbl` included three times in gen.cpp

**File**: `gen.cpp:59, 89, 282`

`translate()`, `translate_sub()`, and `ensure_branch_target_translated()` each
expand the full opcode switch.  If one is modified without updating the others,
bugs will be subtle.

**Action**: Factor the switch body into a single shared method.

### 7.3 [P2] `section::realloc()` silently ignores allocation failure

**File**: `section.cpp:43-44`

If `std::realloc()` returns nullptr, the old (freed) pointer is used for
subsequent writes -- undefined behavior.

### 7.4 [P2] `resolve_labels()` silently leaves unresolved fixups

**File**: `gen.cpp:361-364, 393-396`

Unresolvable branch targets produce a log warning and continue.  The generated
code retains placeholder zeros, causing wrong jumps or crashes.

### 7.5 [P3] No code caching

**File**: `native-run.cpp:345`

Every `run_native()` call retranslates from scratch and unmaps after execution.
Hot bytecode functions pay the full JIT cost on every invocation.

### 7.6 [P4] `opdecl.tbl` is dead code

Never `#include`d anywhere.  Remove or document its purpose.

### 7.7 [P4] `branch_patches` member is never used

**File**: `backend.h:137`

Declared as `std::vector<uint>` but never referenced.

### 7.8 [P4] `patch_branch()` function is dead code

**File**: `gen.cpp:328-335`

Defined but never called.

---

## 8. Efficiency Summary by Category

### 8.1 sys Opcode State Transition

| Platform | Approx. Instructions | Main Cost |
|----------|---------------------|-----------|
| arm64    | ~93                 | 3x full shadow reload (16 regs each) |
| x86-64   | ~25                 | 2x movabs for args + 2x call_ptr |
| riscv64  | ~80                 | 2x full spill/reload + movabs addresses |
| arm32    | ~30                 | Full spill + 2 calls + reload |

**Recommendation**: The fast path (sys does not redirect PC) should avoid the
second `reload_shadows()`.  Consider a single combined `xlr8_sys_and_check()`
call that returns both the sys result and whether the PC changed.

### 8.2 Local Branches (bra, btrue, etc.)

| Platform | Technique         | Issue |
|----------|-------------------|-------|
| arm64    | MOVZ+AND+CBNZ     | Should use TBZ/TBNZ (1 insn vs 3-5) |
| x86-64   | movabs+AND+TEST+Jcc | Should use BT+Jcc (2 insns vs 6) |
| riscv64  | AND+BNEZ           | Reasonable for RISC-V |
| arm32    | Broken             | Comparisons always false |

### 8.3 Distant Branches (jmp, call)

All platforms use deferred fixup chains (gjmp/gsym) for forward references, which
is correct.  Call sequences are 10-15 instructions due to push-return-address +
sync-args + jump.  This is inherent to the software call stack model (no hardware
return-address prediction).

### 8.4 Native Call Resolution (sys targets)

All platforms resolve function addresses at JIT compile time via
`xlr8_runtime_fn()`, which does a linear scan of a ~25-entry table.  The address
is then loaded as a 64-bit immediate (4 instructions on arm64/riscv64, 10 bytes on
x86-64).

**Recommendation**: Sort the table and use binary search, or use a hash map.
Consider AUIPC-relative calls on arm64/riscv64 when the target is within range.

---

## 9. Recommended Action Plan

### Phase 1 -- Critical Correctness (unblocks testing)

| # | Bug | Platforms | Est. Effort |
|---|-----|-----------|-------------|
| 1 | ~~Fix `cond_mov_local` / `select_local`~~ | All 4 | done |
| 2 | Fix `neg_rr()` encoding (rd << 16) | arm64 | 5min |
| 3 | Fix `emit_lds()` to call `load_signext()` | arm64 | 5min |
| 4 | Fix `not_rr()` funct3 (ANDI -> XORI) | riscv64 | 5min |
| 5 | Fix `fmv_x_w` funct3 | riscv64 | 5min |
| 6 | Fix double-precision `fcvt` -> `fmv` | riscv64 | 30min |
| 7 | Register `xlr8_switch` or implement inline | All 4 | 1h |
| 8 | Fix `ldr_off`/`str_off` U-bit | arm32 | 5min |
| 9 | Fix comparison string mismatch | arm32 | 30min |
| 10 | Fix `gsym_addr` pipeline offset (-8) | arm32 | 5min |

### Phase 2 -- Correctness Hardening

| # | Bug | Platforms | Est. Effort |
|---|-----|-----------|-------------|
| 11 | Add `d < LOC_SHADOW_COUNT` guards | arm64, riscv64 | 1h |
| 12 | Fix `store_double` 64-bit truncation | arm32 | 1h |
| 13 | Implement `sext_local`/`zext_local` | arm32 | 1h |
| 14 | Implement memory access sizes | arm32 | 2h |
| 15 | Fix `frame_addr` shift amount | arm32 | 5min |
| 16 | Add callee-save prologue/epilogue | arm32 | 1h |
| 17 | Implement division/modulo/shift-imm | arm32 | 2h |
| 18 | Fix `fcvt_x_d` baked-in rd | riscv64 | 5min |
| 19 | Validate branch range on patch | riscv64 | 1h |
| 20 | Fix `section::realloc()` null check | common | 5min |
| 21 | Factor emit.tbl into single method | common | 1h |

### Phase 3 -- Efficiency Wins (highest ROI first)

| # | Optimization | Platforms | Impact |
|---|-------------|-----------|--------|
| 22 | Use TBZ/TBNZ for predicate branches | arm64 | -3 insns/branch |
| 23 | Use MOVN for negative immediates | arm64 | -3 insns for -1..-65536 |
| 24 | Use BT/BTS/BTR for predicates | x86-64 | -8 bytes/predicate op |
| 25 | Use rel8 short branches | x86-64 | -4 bytes/branch |
| 26 | Use imm8 shifts instead of movabs+cl | x86-64 | -9 bytes/shift |
| 27 | Optimize `load_hw_imm` for small values | x86-64 | -5 bytes common case |
| 28 | Use SLLI/SRAI/SRLI immediates | riscv64 | -1 insn/shift |
| 29 | Merge sys reload passes | arm64, riscv64 | -16 insns/sys fast path |
| 30 | Fix Linux float binop path | arm64 | -40 insns/float op |
| 31 | Shadow residency tracking (see §1.5) | all, arm64 first | skip redundant GPR↔FP↔mem moves |

### Phase 4 -- Cleanup

| # | Item | Est. Effort |
|---|------|-------------|
| 32 | Remove `opdecl.tbl` (dead) | 5min |
| 33 | Remove `branch_patches` member (dead) | 5min |
| 34 | Remove `patch_branch()` function (dead) | 5min |
| 35 | ~~Remove dead arm64.h helpers~~ — done via §3.7 (fixed + wired) | — |
| 36 | ~~Fix stale HW_ANCHOR comments (x20 -> x27)~~ — done via §3.8 | — |
| 37 | Remove/fix dead VFP helpers in arm.h | 15min |
| 38 | Fix `slti` encoding in riscv64.h | 5min |

---

## 10. Platform Maturity Matrix

| Capability           | arm64   | x86-64  | riscv64 | arm32   |
|----------------------|---------|---------|---------|---------|
| Integer arithmetic   | Working | Working | Working*| Partial |
| Comparisons/preds    | Working | Working | Working | Broken  |
| Conditional moves    | Working | Working | Working | Working |
| Branches             | Working | Working | Working | Buggy   |
| sys calls            | Working | Working | Working | Buggy   |
| enter/leave          | Working | Working | Working | Partial |
| Float (single)       | Working | Working | Working*| Working |
| Float (double)       | Working | Working | Broken  | Broken  |
| Memory loads/stores  | Working | Working | Working | Wrong sz|
| Sign/zero extension  | Working | Working | Working | No-op   |
| Bitwise NOT          | Working | Working | Broken  | Working |
| Division/modulo      | Traps   | Working | Working | Traps   |
| switch/dispatch      | Crashes | Crashes | Crashes | Crashes |
| Callee-save ABI      | Correct | Missing | Correct | Missing |
| Windows support      | N/A     | Missing | N/A     | N/A     |

\* riscv64 `fmv_x_w` has wrong encoding; integer NOT is silently a no-op

**Bottom line**: arm64 is the primary development platform and closest to
production-ready, but needs `neg_rr`, `lds`, and `switch` fixes.
x86-64 is functionally correct for most opcodes but has significant code-size
inefficiencies.  riscv64 has correct structure but critical encoding bugs.  arm32
needs substantial work before it can run non-trivial programs.
