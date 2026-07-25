# Arm32 Backend Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `backend/arm*` to the same architecture as refactored x86/arm64 (native frame, `local()`, `branch`/`label`/`patch`, thin EMIT bodies).

**Architecture:** Mirror `arm64.h` / `arm64.cpp` / `arm64-emit.cpp` (and x86). Keep AArch32 ARM-mode encoders; replace shadows/`rtcall`/`record_branch` with allocator + native enter/leave. Spec: `docs/superpowers/specs/2026-07-24-arm32-backend-refactor-design.md`.

**Tech Stack:** C++17, AArch32 (ARM mode encodings), existing `backend` / `regmap` / recorder / `xlr8_jit_write_*`.

## Global Constraints

- Frame: r11=`HW_FRAME`, lr=r14, r10=`HW_FLAGS`, r12=`HW_IP` (scratch); leave r9 alone.
- Allocatable INT: r4–r8 only (`RSPEC_CALL|RSPEC_INT`); r0–r3 not for live locals.
- `local_disp(n)=~(n)*sizeof(ularge_t)`; `arg_disp(n)=n*sizeof(ularge_t)+2*pointer_size()`; `pointer_size()=4`.
- No `rtcall` / `record_branch` / `jump_chain` / shadow `rreg()` / `rt.frame` priming.
- `frame_active`: load/store touch `[fp,#disp]` only after `op_enter`; otherwise zero-on-load / keep-in-reg on store (arm64 pattern).
- Prologue saves FP+LR before any helper `bl`/`blx` (arm64 LR lesson).
- Done when: Mac `make` includes `arm.cpp`; smoke `addi`/`addition`/`bfalse` pass on `ssh pi3` at `OPTLEVEL=` and `-O` vs `.ref`.
- Commits only when the user requests them.
- Do not port riscv64 in this plan; do not “fix” shared backend unless required (TOK LEB128 already fixed).

## File map

| File | Role after refactor |
|------|---------------------|
| `backend/arm.h` | Register enum, encoders, helper decls, disp helpers |
| `backend/arm.cpp` | iface + helpers; `#include "arm-emit.cpp"` at end |
| `backend/arm-emit.cpp` | Thin `EMIT` / `SYS` only (no standalone EMIT macro define) |
| `backend/Makefile` | Re-add `arm.cpp` to sources |
| `backend/backend.cpp` | `make(ARCH_ARM)` → `arm::make(rt)`; `#include "arm.h"` |

Reference implementations: `backend/arm64.{h,cpp}`, `arm64-emit.cpp`, `x86-emit.cpp`. Baseline: `work/xlr8/backend/arm.*` for old encoders worth keeping.

---

### Task 1: Rewrite `arm.h` — register model + helper API

**Files:**
- Modify: `backend/arm.h`
- Reference: `backend/arm64.h`, `backend/x86.h`, spec §Registers

**Interfaces:**
- Produces: `enum` with `HW_R0`…`HW_R15` (or `HW_R0`…`HW_LR`/`HW_SP`), `HW_FRAME=HW_R11`, `HW_FLAGS=HW_R10`, `HW_IP=HW_R12`, `HW_LR=HW_R14`, `HW_SP=HW_R13`, `HW_COUNT`; optional VFP later; `hw()`, `hw_local()`, `local_disp()`, `arg_disp()`, `rn()` encoder index helper; helper decls matching arm64 (`load_local`, `binop_local(…, op_t)`, `bra_pred`, `rel_branch`, `rel_function`, `prologue`/`epilogue`, `call_ptr`, `frame_active`).
- Consumes: `backend.h`, `backend-iface.h`.

- [ ] **Step 1:** Replace header register-map comments with native-frame conventions from the spec (delete shadow r0–r3 narrative).
- [ ] **Step 2:** Replace `enum` / `rreg()` / `slot_off` with allocator-facing enum + `HW_COUNT`. Physical numbers must match ARM Rn encoding (r0=0 … r15=15).
- [ ] **Step 3:** Keep working encoder inlines (`mov_rr`, `add_rrr`, `ldr`/`str` imm/reg, `b_imm`, `bl_imm`, conditional B, `push`/`pop` bitmasks, ALU). Drop helpers that assume `ARM_SHADOW_COUNT` or `HW_LOC*`.
- [ ] **Step 4:** Add:

```cpp
int local_disp(int n) const { return ~n * int(sizeof(ularge_t)); }
int arg_disp(int n) const {
    return n * int(sizeof(ularge_t)) + 2 * int(pointer_size());
}
```

(Implement `pointer_size()` override in `.cpp` as `4`, or inline `4` in `arg_disp` if preferred — must match iface.)

- [ ] **Step 5:** Update helper declarations to `local_t` / `op_t` / `byte_p` signatures; remove `rtcall` / `jump` / `resolve_jump` / `patch_branch_site` / shadow spill decls; add `bool frame_active`.

---

### Task 2: Implement iface + prologue in `arm.cpp`

**Files:**
- Modify: `backend/arm.cpp` (rewrite top half; may truncate old body)
- Reference: `backend/arm64.cpp` iface section

**Interfaces:**
- Produces: `machine_defs`, `registers`, `pointer_size` (=4), `begin_translation`/`finish_translation`, `patch`, `call`/`syscall`, `relocate`, `move`/`load`/`store`, `register_definition`, `prologue`/`epilogue`, `call_ptr`.
- Consumes: Task 1 enum + encoders.

- [ ] **Step 1:** `register_definition` — r0–r3, r9, r11–r15, r12: spec 0 (not allocatable); r4–r8: `RSPEC_CALL|RSPEC_INT`; r10 flags: 0.

```cpp
{ "r4", RSPEC_CALL | RSPEC_INT },
// ... r5..r8 same
{ "r10", 0 }, // HW_FLAGS
{ "r11", 0 }, // HW_FRAME
{ "r12", 0 }, // HW_IP
```

- [ ] **Step 2:** `load`/`store`/`move` — if `!frame_active`, load zeros GPR (MOV rd,#0); store calls `saved(n,h)` only. If `frame_active`, LDR/STR `[fp, #local_disp(n)]` (use 8-byte slots: two 32-bit words or LDRD/STRD if available and aligned; otherwise two LDR/STR). Match arm64’s ularge_t slot size.
- [ ] **Step 3:** `patch(hwcode_t, hwcode_t, bool rel)` for ARM `B` / `BL` / `Bcond` (imm24 << 2, signed). Out of range → `record(arm_error, …)` and do not truncate.
- [ ] **Step 4:** `call`/`syscall` → `spill_all` + `call_ptr`. `call_ptr`: MOV/LDR absolute into r12, `blx r12` (or `bl` if in range — absolute via r12 is fine).
- [ ] **Step 5:** `prologue`:

```
push {r4-r8, r10, fp, lr}   // encode as stmfd; must include lr
mov  fp, sp
```

`epilogue`: reverse `ldmfd` / `pop` including `pc` **or** restore lr then `bx lr`. Prefer restoring lr and `bx lr` so nested enter/leave stay consistent.

- [ ] **Step 6:** `begin_translation`: `frame_active=false`; `backend::begin_translation()`; `prologue()`. `finish_translation`: `epilogue()`; `backend::finish_translation()`.
- [ ] **Step 7:** `relocate` return `uint`, clear reloc list (arm64-style no-op OK if unused).
- [ ] **Step 8:** Compile `arm.cpp` once Makefile includes it (Task 5 can enable earlier for iteration).

---

### Task 3: Core helpers (movers, binops, preds, branches)

**Files:**
- Modify: `backend/arm.cpp`
- Reference: `backend/arm64.cpp`, `backend/x86.cpp` helpers

**Interfaces:**
- Produces: `load_local`, `store_local`, `mov_local`, `swap_locals`, `store_imm`, `store_float`/`store_double` (stubs OK), `binop_local`/`binop_imm` (`op_t`), `cmp_*`, `set_pred`, `cond_mov_local`, `select_local`, unary ops, mem ops, `bra_pred`/`bra_test`, `rel_branch`/`rel_function`, `dispatch`.
- Consumes: Task 2 iface.

- [ ] **Step 1:** Movers via `hw(local(loc))` — no shadow write-through.
- [ ] **Step 2:** `binop_local` / `binop_imm` `switch (op)` using existing ALU encoders (ADD/SUB/AND/ORR/EOR/MUL/…); for 64-bit `ularge_t` ops on ILP32, follow existing arm32/x86-i386 pattern already used in this tree (pair of regs or low 32 only if that’s what smoke needs — **addi smoke is 32-bit-scale ints in practice**; match arm64/x86 emit for `nat`/`addi`/`write_integer`).
- [ ] **Step 3:** Predicates on `HW_FLAGS` (ORR/BIC bit; branch with TST + Bcond, or bit test sequence).
- [ ] **Step 4:** `rel_branch` / `rel_function`:

```cpp
void arm::rel_branch(byte_p target, hwcode_t site) {
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off);
}
```

Same for `rel_function` with `function(tok, site)`.

- [ ] **Step 5:** Delete dead old helpers (`rtcall*`, `jump_chain`, shadow spill, …).

---

### Task 4: Rewrite `arm-emit.cpp` to x86/arm64-emit semantics

**Files:**
- Modify: `backend/arm-emit.cpp`
- Modify: `backend/arm.cpp` — `#define EMIT` / `#define SYS` at end and `#include "arm-emit.cpp"` like arm64; emit file must not redefine EMIT.
- Reference: `backend/arm64-emit.cpp`, `backend/x86-emit.cpp`

**Interfaces:**
- Consumes: Task 3 helpers.
- Produces: All `emit_*` / `emit_sys_*` compiling.

- [ ] **Step 1:** Align each `EMIT` body with arm64-emit control flow (same helper calls; ARM encoding inside helpers).
- [ ] **Step 2:** `enter`/`leave`/`get`/`set`/`push`/`pop` inline per spec; set/clear `frame_active` on enter/leave.
- [ ] **Step 3:** Branches use `e.bpc + e.broff` + `rel_branch` / `bra_*`.
- [ ] **Step 4:** `jmp`/`call` use `rel_function`; `sys` uses `dispatch_sys(e)`.
- [ ] **Step 5:** `end` does **not** emit epilogue (finish_translation does); set `e.pc = e.end`. `null`/`user`/`undef` → `syscall(xlr8_*)`.
- [ ] **Step 6:** Implement at least:

```cpp
SYS(write_integer) {
    // r0 = stream, r1 = value (AAPCS)
    armreg_t v = hw_local(e.src);
    // mov r1, v; set_imm r0, e.knat; call_ptr(xlr8_jit_write_integer);
}
SYS(write_token) { /* r0 stream, r1 tok */ }
SYS(write_newline) { /* r0 stream */ }
```

Other `SYS(*)` may stay empty stubs for this pass.

---

### Task 5: Wire build + Mac compile

**Files:**
- Modify: `backend/Makefile`
- Modify: `backend/backend.cpp` (`make`, includes)

- [ ] **Step 1:** Re-add `arm.cpp` to backend sources (uncomment / restore next to `arm64.cpp`).
- [ ] **Step 2:** `#include "arm.h"` and `case ARCH_ARM: return arm::make(rt);` (remove nullptr stub + error message).
- [ ] **Step 3:** `make` on macOS — clean compile of `arm.cpp` into `libbackend.a` and link `xlr8`. Expected: 0 errors.
- [ ] **Step 4:** Quick sanity: `./xlr8 tests/bytecode/addi.xlr8` (interpreter) still OK on Mac (uses arm64 JIT host — unrelated, must not regress).

---

### Task 6: Smoke on `ssh pi3`

**Files:** none (verify). Remote: Host alias `pi3` → ptipi.local. Repo `~/Work/xlr8`.

- [ ] **Step 1:** Ensure Mac commits or at least reachable tip on `mac/xlr8` remote that pi3 fetches (if uncommitted, either commit on user request or `git push` is forbidden — remotes use `git fetch mac`; uncommitted Mac work is **invisible** to pi3). If user has not asked to commit, ask before sync **or** use a temporary agreed sync method only if user explicitly allows — default is: **request commit**, then:

```bash
# on Mac (after user approves commit)
git fetch mac   # n/a locally
# on pi3:
ssh pi3 'cd ~/Work/xlr8 && git fetch mac && git reset --hard mac/xlr8 && git submodule update --init --recursive && make'
```

- [ ] **Step 2:** Interpreter smoke:

```bash
ssh pi3 'cd ~/Work/xlr8/tests && make ref-addi ref-addition ref-bfalse OPTLEVEL='
```

Expected: `[SUMMARY] Failed tests: 0` and outputs match `.ref` (`5+3=8`, etc.).

- [ ] **Step 3:** JIT smoke:

```bash
ssh pi3 'cd ~/Work/xlr8/tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=-O'
```

Expected: same — Failed tests: 0; spot-check `.output` vs `.ref`.

- [ ] **Step 4:** If segfault after prints: check prologue LR save and callee-saved alloc (arm64 failure mode). If `undef` / user opcode: check TOK LEB128 already in `backend.cpp` and `dispatch_sys` arg parse.

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Native frame r11/lr, disp formulas | 1, 2, 4 |
| Delete rtcall/record_branch/shadows | 1, 3, 4 |
| iface patch/load/store/prologue LR | 2 |
| Helpers local_t/op_t/rel_* | 3 |
| Thin EMIT + write_* SYS | 4 |
| Makefile + make(ARCH_ARM) | 5 |
| Smoke on ssh pi3 | 6 |

## Plan self-review

- Spec §Success / §Testing / §Decisions → Tasks 5–6.
- Spec §Registers / frame_active / LR → Tasks 1–2.
- No TBD placeholders.
- Commits gated on user request (PLATFORM-ENABLEMENT sync needs a published `mac/xlr8` tip).
