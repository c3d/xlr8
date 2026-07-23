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
errors.  The arm64 backend is the most mature; arm32 and i386 now pass the full
functional suite on **pi3** and **holy** (ref and `-O`).

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
| Shadow locals     | x0–x15        | r8,r9,r10,rbx     | (none)       | r0–r3        | x10–x25       |
| Frame pointer     | x19           | r12               | edi          | r11          | x8 (s0)       |
| Frame anchor      | x27           | --                | --           | --           | --            |
| Flags/predicates  | x22           | r15               | esi          | r10          | x9 (s1)       |
| Scratch value     | x23           | r11; rax,rcx,rdx  | ecx; eax,edx  | r4           | x28 (t3)      |
| Scratch address   | x24           | --                | --           | r8           | x29 (t4)      |
| Scratch offset    | x25           | --                | --           | r5           | x30 (t5)      |
| Intra-call IP     | x16           | --                | --           | r12          | x5 (t0)       |
| Saved link        | x28           | --                | --           | --           | --            |
| Frame slot size   | 8 bytes       | 8 bytes           | 8 bytes      | 8 bytes†     | 8 bytes       |
| Max shadow locals | 16            | 4                 | 0            | 4            | 16            |

† arm32 frame slots are `ularge_t` (8 bytes) even though the ABI is ILP32;
  pointers and most integers still occupy a full slot.

**ABI / shadow overlap (important for sys and C helpers):**

- **arm64**: locals 0–1 are also AAPCS argument registers (x0–x1).  Locals
  0–15 are caller-saved; every `rtcall` must spill/reload (or otherwise
  preserve) live shadows.
- **riscv64**: locals 0–7 are a0–a7 (`x10`–`x17`) by design.  JIT code is not
  a normal RV callee: helpers clobber those registers, so paths like
  `emit_sys`, `push_local`/`pop_local`, and `reload_flags_reg` must restore
  shadows from the frame after the call (see §1.4 riscv64 fixes).
- **arm32**: only r0–r3 are shadowed (arg regs); r4–r5/r8 are dedicated
  scratches so temps never silently overwrite live locals (§6.11).
- **x86-64**: four shadows in callee-saved / extra regs (r8–r10, rbx).
  Dedicated scratch is **r11** (`HW_SCR`); opcode helpers also use **rax,
  rcx, rdx** as caller-saved value temps (div, immediates, returns).
  **i386** keeps all locals in the frame only; dedicated scratch is **ecx**,
  with **eax** / **edx** as the usual value-temp pair (e.g. `idiv` uses
  `edx:eax`).

**arm64 frame anchor**: x27 (`HW_ANCHOR`) holds a stable copy of `rt.frame`
across syscalls; `restore_frame_ptr()` is `mov x19, x27` (no C call).
**riscv64** has no anchor — after helpers that may move the frame it reloads
`s0` via `xlr8_get_frame` (same idea as Linux arm64’s post-sys path).

### 1.3 Platforms and Test Machines

| Platform | JIT backend | Machine(s)              |
|----------|-------------|-------------------------|
| arm64    | arm64       | macOS (dev), torpille   |
| x86-64   | amd64       | turbo                   |
| x86-32   | x86         | **holy** (was turbo-x86)|
| arm32    | arm         | **pi3** (was turbo-arm) |
| riscv64  | riscv64     | risky (`fedora-riscv`)  |

**ILP32 hosts (current):**

- **pi3** — physical Raspberry Pi (armv7l / Cortex-A7).  Preferred arm32
  check machine; much faster than the old QEMU **turbo-arm** box.
- **holy** — x86 32-bit checks.  Builds must use **`BUILDENV=i686`**.  The
  `xlr8` tmux pane on holy already has `export BUILDENV=i686`, so the usual
  protocol (`tmux send-keys` into `xlr8:0.0`, no extra env) picks it up
  automatically.  Any **direct SSH** `make` / one-off shell outside that pane
  must set `BUILDENV=i686` explicitly or the wrong (likely amd64) config is
  used.

Baseline functional rows below still mix older snapshot status with current
hosts; **holy** / **pi3** are the live ILP32 machines.  Do **not** reuse
QEMU **turbo-x86** / **turbo-arm** timing as a stand-in for those hosts.


### 1.4 Parallel Baseline (2026-07-17, refreshed)

**Commit**: tip of `xlr8` after pi3 arm32 JIT work (`ae4317b` and parents; see
§1.4 known-fixes notes below).  The earlier `ef61add` Linux LP64 overload clash
was fixed in `9c1e9f9` and is **not** a current build blocker.

**Protocol**: Per `PLATFORM-ENABLEMENT.md` — on each machine,
`git fetch mac && git reset --hard mac/xlr8`, `git submodule update --init
--recursive`, `make`, then `make tests` (interpreter) and `make tests
OPTLEVEL=-O` (JIT).  On platforms where both pass, `make perf` and `make perf
OPTLEVEL=-O`.

#### Functional test results

| Machine     | Platform | Reference          | JIT (`-O`)         | Status |
|-------------|----------|--------------------|--------------------|--------|
| macOS       | arm64    | 226 / 0 failed     | 226 / 0 failed     | Green  |
| turbo       | x86-64   | 226 / 0 failed     | 226 / 0 failed     | Green  |
| torpille    | arm64    | 226 / 0 failed     | 226 / 0 failed     | Green  |
| risky       | riscv64  | 226 / 0 failed     | **226 / 0 failed** | Green  |
| holy        | x86-32   | 226 / 0 failed     | **226 / 0 failed** | Green  |
| pi3         | arm32    | **226 / 0 failed** | **226 / 0 failed** | Green  |

risky’s **226 / 0** ref and `-O` were verified on-host after the riscv64 JIT
fixes (`fc01a85` and parents: soft-stack returns, `jalr` encoding, zero-ext
loads, `pop_local` shadow reload, far B+J branches).

Expected baseline: **226** functional tests per run (Jul 2026).

LP64 hosts turbo / torpille / risky build and pass the full suite after
`9c1e9f9` and the riscv64 JIT fixes.  Do not treat the old `ef61add`-era
“build failed” rows as current.
holy’s **226 / 0** at `-O` was verified on-host after the i386 JIT fixes.
pi3’s **226 / 0** ref and `-O` were verified on-host after the arm32 JIT fixes
(tmux `xlr8:0.0`, mirror `/tmp/xlr8-pi3-local.log`).

#### Performance suite (`make perf`)

Runs 232 tests (226 functional + 6 `performance/` benchmarks).

**Hosts vs profiles:** pass/fail and especially wall-clock times are tied to
the machine class.  **holy** and **pi3** are physical boxes; the old
**turbo-x86** / **turbo-arm** entries were QEMU VMs.  Real-metal and VM
profiles are not interchangeable — re-measure on the current host, do not
carry QEMU numbers over to holy/pi3 (or the reverse).

Pass/fail rows below are from earlier green runs on the names then in use
(not re-measured at `ef61add`).  ILP32 rows use the **current** host names.

| Machine  | Reference          | JIT (`-O`)         | Notes |
|----------|--------------------|--------------------|-------|
| macOS    | 230 / 0 failed\*   | 230 / 0 failed\*   | Earlier run; all six benchmarks pass |
| turbo    | 224 / 6 failed\*   | 224 / 6 failed\*   | All six `performance/` tests fail: `timeout: invalid time interval './time'` (harness issue, not codegen) |
| torpille | 227 / 3 failed\*   | incomplete\*       | `millions`, `millions-addi`, `millions-incr` fail in reference perf run |
| risky    | not run            | not run            | Functional JIT green; perf not refreshed in this pass |
| holy     | not run            | not run            | Functional JIT green; perf not yet measured on this host |
| pi3      | not run            | not run            | Functional JIT green; perf not yet measured on this host |

#### Embedded timing (from `make tests` summary table)

Selected benchmark times in seconds (lower is better).  From earlier
green-platform validation; not refreshed at `ef61add`.

| Machine   | Mode | fibonacci | millions | millions-incr | tokenization |
|-----------|------|-----------|----------|---------------|--------------|
| macOS     | ref  | 6.41      | 5.98     | 5.96          | 0.86         |
| macOS     | `-O` | 3.02      | 0.77     | 0.85          | 3.34         |
| turbo     | ref  | 7.87      | 7.00     | 6.78          | 1.94         |
| turbo     | `-O` | 3.61      | 0.97     | 0.81          | 1.91         |
| torpille  | ref  | 52.92     | —        | —             | —            |
| torpille  | `-O` | 9.24      | 12.46    | 12.07         | 16.17        |
| risky     | ref  | 5.05      | —        | —             | —            |

#### Known failures (non-green platforms)

**Interpreter (ILP32)** — fixed by `ef61add` / `76157a3` / `df4c77d` plus
ILP32 `snprintf` casting for scanner printf-style formatting:

| Host | Reference | Remaining failures |
|------|-----------|--------------------|
| holy      | 226 / 0 | — |
| pi3       | 226 / 0 | — |

**JIT (`-O`) on holy (i386)**: **226 / 0 failed**.  i386 path is green.

Key fixes along the way: callee-saved prologue; preserve helper /
`after_sys` / `dispatch` return values across C reloads; keep live flags
in `esi` (do not `get_flags` after helpers); relocate `mov eax,imm32`
return addresses; store both halves of double immediates; drop spurious
`0x66` on `movsx eax,m16`.

**JIT (`-O`) on pi3 (arm32)**: **226 / 0 failed**.  arm32 path is green.

Key fixes along the way: VFP doubles in `binop_local`; `load_pca` /
`pending_branch_target`; soft-stack push/pop/ret/globals; branch over
callee bodies; frame shadow write-through; `xlr8_get_arg`/`xlr8_set_arg`;
signed `addi`/`subi` (negative immediates); `shift_rrr` bit 4.

**JIT (`-O`) on risky (riscv64)**: green (**226 / 0** on-host after `fc01a85`
and parents).  See §1.4 table.

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

### 4.1 [P2] ~~`emit_push_reg()` missing REX prefix for r8-r15~~ — **FIXED** (pre-existing)

**File**: `x86.cpp` — `emit_push_reg()` / `emit_pop_reg()`

Previously `g(0x50 + (r&7))` with no REX.B, so `push r12` would encode as
`push rsp`.  Fixed when the x86-64 prologue landed (`d43b0da`): both push and
pop emit `0x41` when `pointer_size()==8 && rex_b(r)`.

```cpp
if (pointer_size() == 8 && rex_b(r))
    emit8(0x41);
g(byte(0x50 + reg_value(r)));
```

Current call sites use `rax`/`rbx`/`rbp` (no REX.B needed), but `HW_FRAME`
(`r12`) and other r8–r15 args are safe if pushed later.

### 4.2 [P3] ~~`load_hw_imm()` always emits 10-byte movabs~~ — **FIXED** (2026-07-16)

**File**: `x86.cpp` — `load_hw_imm()`

Prefer short encodings on x86-64 (i386 still truncates to 32 bits):

| Value | Encoding | Size |
|-------|----------|------|
| `0` | `xor r32, r32` | 2–3 B |
| `0 … 2³²−1` | `mov r32, imm32` (zero-extends) | 5–6 B |
| signed `imm32` (e.g. `-1`) | `mov r64, imm32` (sign-extends) | 7 B |
| otherwise | `movabs r64, imm64` | 10 B |

Validated on macOS / turbo / torpille (226/0) with commit `a164ffe`.

### 4.3 [P3] ~~No short (rel8) branch encoding~~ — **FIXED** (2026-07-16)

**File**: `x86.cpp` / `x86.h` / `section.h`

- Local skips (`set_pred_cmp`, `cond_mov_local`, `select_local`) emit
  `7x rel8` / `EB rel8` instead of `0F 8x rel32` / `E9 rel32`.
- `patch_branch_site` recognizes both forms (opcode before the disp site).
- `jmp_tok` to an already-bound label uses `emit_jmp_to` (rel8 when in ±128).
- Deferred bytecode branches (`bra_*`, unbound `jump` chains) still use rel32;
  distance is unknown at emit time and expanding a short site would shift
  later fixups.

Validated on macOS / turbo / torpille (226/0) with commit `8795932`.

### 4.4 [P3] ~~Shift-by-immediate uses movabs + cl shift~~ — **FIXED** (2026-07-16)

**File**: `x86.cpp` / `x86.h` — `binop_imm`, `emit_shift_imm8`

`shl_imm` / `sar_imm` / `shr_imm` now emit `C1 /ext ib` (or `D1 /ext` when
the count is 1) instead of loading rcx and shifting by `cl`.  Same helper used
for the generic `sext_reg` shift pair.

### 4.5 [P3] ~~Predicate set/clear/test are 30-44 bytes each~~ — **FIXED** (2026-07-16)

**File**: `x86.h` — `emit_pred_*_bit` / `emit_btx_imm8`

Previously each predicate op loaded a 64-bit mask via `movabs` + AND/OR/TEST
(~30–44 bytes).  Now emits a single `BT`/`BTS`/`BTR` with imm8 bit index
(`0F BA /ext ib`, 5–6 bytes).  Consumers of `emit_pred_test_bit` branch on **CF**
(`JC`/`JNC`) instead of ZF.

### 4.6 [P3] ~~No memory-destination ALU operations~~ — **FIXED** (2026-07-16)

**File**: `x86.h` / `x86.cpp` — `emit_grp5_frame`, `emit_alu_imm_frame`

Non-shadow `incr`/`decr` now emit `inc`/`dec [frame+disp]` (`FF /0`/`/1`).
Non-shadow `addi`/`subi` emit `add`/`sub [frame+disp], imm` (`83 ib` or `81 id`).
Shadow locals keep the register INC/DEC + frame store path.

### 4.7 [P3] ~~`emit_leave()` generates redundant frame reload~~ — **FIXED** (2026-07-16)

**File**: `x86-emit.cpp` / `x86.cpp` — `emit_leave`, `emit_runtime_call`

`emit_leave` used the zero-arg `emit_runtime_call`, which always ran
`reload_rt_regs()` (no-op on x86-64; on i386 it re-fetched frame/flags and left
`eax` holding flags), then called `reload_frame_reg()` again.  Zero-arg
`emit_runtime_call` now special-cases `xlr8_leave`/`xlr8_enter` like the byte-arg
overload (`mov` frame from `rax`), and `emit_leave` only reloads flags afterward.

### 4.8 [P1] ~~No Windows x64 calling convention support~~ — **FIXED** (2026-07-16)

**File**: `x86.h` / `x86.cpp` — `hw_arg0`/`hw_arg1`, `call_ptr`, prologue/epilogue

- Helper args use `hw_arg0()`/`hw_arg1()`: SysV `rdi`/`rsi`, Win64 `rcx`/`rdx`
  (`win64_abi()` from `_WIN64`).
- `call_ptr` allocates the Win64 32-byte parameter home space around the call.
- x86-64 prologue/epilogue now save/restore **r12 and r15** (frame/flags) in
  addition to rbx/rbp — required on both SysV and Win64 (callee-saved).

---

## 5. RISC-V 64-Specific Findings

### 5.1 [P0] ~~`not_rr()` generates ANDI instead of XORI~~ — **FIXED** (2026-07-16)

**File**: `riscv64.h` — `not_rr()`

Previously used funct3=7 (`ANDI rd, rd, -1`, a no-op).  Now funct3=4
(`XORI rd, rd, -1`).  `emit_not` / `tests/bytecode/not.xlr8` exercise this.

### 5.2 [P0] ~~Double-precision uses integer conversion instead of bitwise move~~ — **FIXED** (2026-07-16)

**File**: `riscv64.h` / `riscv64.cpp` — `fmv_d_x` / `fmv_x_d`, `binop_local`

Double binops loaded/stored frame bit-patterns with `fcvt.d.l` / `fcvt.l.d`
(numeric conversion).  Now use `fmv.d.x` / `fmv.x.d` (bitwise), matching the
float path’s `fmv.w.x` / `fmv.x.w`.

### 5.3 [P0] ~~`fmv_x_w` encoding has wrong funct3~~ — **FIXED** (2026-07-16)

**File**: `riscv64.h` — `fmv_x_w()`

Base was `0xe0001053` (funct3=1).  `FMV.X.W` requires funct3=0; corrected to
`0xe0000053`.

### 5.4 [P2] ~~`store_double()` no bounds check for d >= 16~~ — **FIXED** (2026-07-16)

**File**: `riscv64.cpp` — `store_double()`

Previously always used `xreg(d)` (so `d=21` → `x31`/sp).  Now matches
`store_float`: shadow locals via `xreg(d)`, else `HW_SCR_VAL` + `store_local`.
`tests/bytecode/high-local.xlr8` stores/prints `dbl` in local 18.

### 5.5 [P2] ~~`fcvt_x_d` has baked-in rd=4~~ — **FIXED** (2026-07-16)

**File**: `riscv64.h` — `fcvt_x_d()`

Base was `0xc2200253` (rd=4 in bits 11:7).  Corrected to `0xc2200053` so
`rd << 7` sets the destination cleanly.  (Unused after §5.2’s `fmv` path;
kept correct for real numeric conversions.)

### 5.6 [P2] ~~`slti` encoding uses wrong funct3~~ — **FIXED** (2026-07-16)

**File**: `riscv64.h` — `slti()`

Base was `0x00001013` (funct3=1 = SLLI).  Corrected to `0x00002013` (funct3=2 =
SLTI).  Currently unused; prevents silent wrong codegen if wired later.

### 5.7 [P3] ~~Shift-by-immediate uses register-register path~~ — **FIXED** (2026-07-16)

**File**: `riscv64.cpp` — `binop_imm`

For `0 ≤ v < 64`, `shl_imm` / `sar_imm` / `shr_imm` now emit `SLLI` / `SRAI` /
`SRLI`.  Out-of-range amounts still load a scratch and use the register path.

### 5.8 [P2] ~~Conditional branch range not validated~~ — **FIXED** (2026-07-16)

**File**: `riscv64.cpp` — `patch_branch_site()`

B-type (±4KiB) and J-type (±1MiB) patches now reject out-of-range or odd
offsets via `riscv64_error` instead of silently truncating the immediate.

---

## 6. ARM32-Specific Findings

**Overall assessment: Early prototype.  Multiple show-stopping bugs prevent
execution of non-trivial programs.**

### 6.1 [P0] ~~`ldr_off` / `str_off` U-bit is inverted~~ — **FIXED** (2026-07-16)

**File**: `arm.h` — `ldr_off()` / `str_off()`

Bases were `0xE5100000` / `0xE5000000` (U=0: subtract offset).  Corrected to
`0xE5900000` / `0xE5800000` (U=1: add offset) so frame/local accesses use
`[rn, #+off]`.

### 6.2 [P0] ~~Comparisons always produce false~~ — **FIXED** (2026-07-16)

**File**: `arm.cpp` / `arm.h` — `set_pred_cmp()`, `cond_from_op()`

`cmp_local` / `cmp_imm` now map `"eq"`/`"ne"`/`"lt"`/… to ARM condition codes,
clear the predicate bit with flag-preserving `AND`, then `ORR{cond}` from CPSR.

### 6.3 [P0] ~~`resolve_jump()` missing -8 pipeline offset~~ — **FIXED** (2026-07-16)

**File**: `arm.cpp` — `resolve_jump()`

Offset is now `(a - n - 8) / 4`, matching `patch_branch_site`.  Chain-next is
taken from the 24-bit imm field only (not the full insn word).

### 6.4 [P0] ~~Double-precision truncated to 32 bits~~ — **FIXED** (2026-07-16)

**File**: `arm.cpp` — `store_double()`

Writes both 32-bit halves into the `ularge_t` frame slot at `d * 8`, and updates
the shadow GPR with the low half when `d < 8`.

### 6.5 [P1] ~~Division, modulo, shift-immediate, bitwise-immediate all trap~~ — **FIXED** (2026-07-16)

**Files**: `arm.cpp` / `arm.h` — `binop_local()`, `binop_imm()`

Handles `imul`/`idiv`/`div`/`idiv_mod`/`div_mod`, shift-imm (`SLL`/`ASR`/`LSR`),
and bitwise/mul/div immediates.  Encodings checked with
`scripts/check-arm32-binop-encodings.sh` (`llvm-objdump --triple=arm`).

### 6.6 [P1] ~~`sext_local` and `zext_local` are no-ops~~ ✅ FIXED

**File**: `arm.cpp` (`sext_reg` / `sext_local` / `zext_local`)

Was: both ignored the width parameter.

**Fix** (`3b74449`): `sext_reg` does LSL+ASR for `w < 32` (no-op at 32-bit
width); `zext_local` ANDs with `(1<<w)-1` for `w < 32`.  Verified on turbo-arm
`-O`: `sext`/`sext32`/`zext`/`zext32` match ref (`-128`, `-2147483648`, `255`,
`4294967295`).  Note: signed memory loads (`load_signext`) still ignore width
until §6.7 sized loads land; they can then call `sext_reg`.

### 6.7 [P1] ~~Memory access size ignored -- all loads/stores are 4-byte~~ ✅ FIXED

**File**: `arm.cpp` (`emit_load_sized` / `emit_store_sized` / `load_mem*` /
`store_mem*` / `load_signext*`)

Was: `store_mem` / `load_mem` ignored `sz` (always word).  `load_signext`
ignored width.

**Fix**: STRB/LDRB, STRH/LDRH, STR/LDR, and paired word ops for size 8;
`load_signext` loads `ceil(w/8)` bytes then `sext_local`.  Verified on
turbo-arm `-O`: `st1`/`st2`/`st4`/`st8`/`ld1s`/`ld2s`/`ld1o` exit 0.
(Early memops briefly read from frame to dodge §6.11 scratch/shadow
aliasing; §6.11 now uses dedicated r4/r5 scratches, so memops use
`load_local` again.)

### 6.8 [P1] ~~`frame_addr` uses wrong shift for slot calculation~~ ✅ FIXED

**File**: `arm.cpp` (`frame_addr`)

Was: treated `s` as a runtime local value and did `frame + val<<3` (and
earlier review noted a wrong LSL for 4-byte slots).  `frame d s` must take the
address of **slot** `s` (`frame + s*8`).

**Fix**: immediate `add` of `slot_off(s)` to `HW_FRAME` (or `mov` when `s==0`),
matching arm64/x86.

### 6.9 [P1] ~~Missing callee-saved register save/restore~~ ✅ FIXED

**File**: `arm.cpp` (`begin_translation` / `epilogue`), `arm.h` (`push_callee_lr` /
`pop_callee_pc`)

Was: prologue only saved LR.

**Fix** (in `a8df381`): `push {r4-r11,lr}` then `sub sp,#4` (9 words → keep
8-byte SP align for AAPCS C calls); epilogue `add sp,#4` then
`pop {r4-r11,pc}`.  Encodings `0xE92D4FF0` / `0xE8BD8FF0` verified.  r9 is
saved/restored but left unused as `HW_SB` (PIC/sb on some ABIs).

### 6.10 [P1] ~~VFP helper encodings are incorrect~~ ✅ FIXED

**File**: `arm.h` (`vmov_*`, `vfp_ss` / `vadd_ss` / …), `arm.cpp` (float binops)

Was: `vadd_ss`/`vsub_ss`/`vmul_ss`/`vdiv_ss` put `sm` at bits 20+ and mistook
the D bit; `vmov_s_r`/`vmov_r_s` put the odd-S bit at 22 instead of N (bit 7).
Call sites used hardcoded words for the ALU ops, so helpers were traps.

**Fix**: Encode S regs as `V* = Sx[4:1]`, `D/N/M = Sx[0]` at bits 22/7/5;
VMOV uses N at bit 7.  Checked against `llvm-mc` (`vmov s1,r3` = `EE003A90`,
`vadd.f32 s0,s0,s1` = `EE300A20`).  Float binops now call the helpers.

### 6.11 [P2] ~~Scratch register aliases shadow locals~~ ✅ FIXED

**File**: `arm.h` (register map / `ARM_SHADOW_COUNT`), `regmap.h`, `arm.cpp`

Was: `HW_SCR_VAL`/`HW_SCR_OFF` were r0/r2, aliasing shadow locals 0 and 2.
Temps (e.g. `store_imm`) could leave a stale shadow while the frame still
held the true value — classic `st1` SEGV at `si_addr=0x2a`.

**Fix**: Shadow only **r0–r3** (`ARM_SHADOW_COUNT = 4`).  Dedicated scratches
`HW_SCR_VAL = r4`, `HW_SCR_OFF = r5` (still callee-saved in the prologue pack).
Locals ≥ 4 always live in the frame.  Same shadow depth as x86-64.

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

All platforms use deferred fixup chains (jump/resolve_jump) for forward references, which
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
| 4 | ~~Fix `not_rr()` funct3 (ANDI -> XORI)~~ — §5.1 | riscv64 | — |
| 5 | ~~Fix `fmv_x_w` funct3~~ — §5.3 | riscv64 | — |
| 6 | ~~Fix double-precision `fcvt` -> `fmv`~~ — §5.2 | riscv64 | — |
| 7 | Register `xlr8_switch` or implement inline | All 4 | 1h |
| 8 | ~~Fix `ldr_off`/`str_off` U-bit~~ — §6.1 | arm32 | — |
| 9 | ~~Fix comparison string mismatch~~ — §6.2 | arm32 | — |
| 10 | ~~Fix `resolve_jump` pipeline offset (-8)~~ — §6.3 | arm32 | — |

### Phase 2 -- Correctness Hardening

| # | Bug | Platforms | Est. Effort |
|---|-----|-----------|-------------|
| 11 | ~~Add `d < LOC_SHADOW_COUNT` guards~~ — arm64 §3.3; riscv64 `store_double` §5.4 | arm64, riscv64 | — |
| 12 | ~~Fix `store_double` 64-bit truncation~~ — §6.4 | arm32 | — |
| 13 | Implement `sext_local`/`zext_local` | arm32 | 1h |
| 14 | Implement memory access sizes | arm32 | 2h |
| 15 | Fix `frame_addr` shift amount | arm32 | 5min |
| 16 | Add callee-save prologue/epilogue | arm32 | 1h |
| 17 | ~~Implement division/modulo/shift-imm~~ — §6.5 | arm32 | — |
| 18 | ~~Fix `fcvt_x_d` baked-in rd~~ — §5.5 | riscv64 | — |
| 19 | ~~Validate branch range on patch~~ — §5.8 | riscv64 | — |
| 20 | Fix `section::realloc()` null check | common | 5min |
| 21 | Factor emit.tbl into single method | common | 1h |

### Phase 3 -- Efficiency Wins (highest ROI first)

| # | Optimization | Platforms | Impact |
|---|-------------|-----------|--------|
| 22 | ~~Use TBZ/TBNZ for predicate branches~~ — §3.4 | arm64 | — |
| 23 | ~~Use MOVN for negative immediates~~ — §3.5 | arm64 | — |
| 24 | ~~Use BT/BTS/BTR for predicates~~ — §4.5 | x86-64 | — |
| 25 | ~~Use rel8 short branches~~ — §4.3 | x86-64 | — |
| 26 | ~~Use imm8 shifts instead of movabs+cl~~ — §4.4 | x86-64 | — |
| 27 | ~~Optimize `load_hw_imm` for small values~~ — §4.2 | x86-64 | — |
| 28 | ~~Use SLLI/SRAI/SRLI immediates~~ — §5.7 | riscv64 | — |
| 29 | Merge sys reload passes | arm64, riscv64 | -16 insns/sys fast path |
| 30 | ~~Fix Linux float binop path~~ — §3.6 | arm64 | — |
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
| 38 | ~~Fix `slti` encoding in riscv64.h~~ — §5.6 | — |

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
| Float (double)       | Working | Working | Working | Broken  |
| Memory loads/stores  | Working | Working | Working | Wrong sz|
| Sign/zero extension  | Working | Working | Working | No-op   |
| Bitwise NOT          | Working | Working | Working | Working |
| Division/modulo      | Traps   | Working | Working | Working*|
| switch/dispatch      | Crashes | Crashes | Crashes | Crashes |
| Callee-save ABI      | Correct | Correct | Correct | Missing |
| Windows support      | N/A     | ABI ready* | N/A   | N/A     |

\* x86-64 Win64 arg/home-space/callee-save paths are compiled in under `_WIN64`;
  full Windows host bring-up is still untested.

**Bottom line**: arm64 is the primary development platform and closest to
production-ready, but needs `neg_rr`, `lds`, and `switch` fixes.
x86-64 is functionally correct for most opcodes but has significant code-size
inefficiencies.  riscv64 has correct structure but critical encoding bugs.  arm32
needs substantial work before it can run non-trivial programs.
