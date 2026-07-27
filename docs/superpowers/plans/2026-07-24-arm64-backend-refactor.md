# Arm64 Backend Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `backend/arm64*` to the same architecture as refactored x86 (native frame, `local()`, `branch`/`label`/`patch`, thin EMIT bodies).

**Architecture:** Mirror `x86.h` / `x86.cpp` / `x86-emit.cpp`. Keep AArch64 encoders; replace shadows/`rtcall`/`record_branch` with allocator + native enter/leave. Spec: `docs/superpowers/specs/2026-07-24-arm64-backend-refactor-design.md`.

**Tech Stack:** C++17, AArch64, existing `backend` / `regmap` / recorder.

**Status (2026-07-27):** Structural port is largely in place. Register allocation
shipped as **direct mapping** (not preferred-order callee-saved shadows).
Fibonacci `-O` passes. Remaining `-O` work is tracked in
`docs/superpowers/plans/2026-07-27-arm64-O-failures-after-direct-map.md`
and `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md`.

## Global Constraints

- Frame: x29=`HW_FRAME`, x30=LR, **x28=`HW_FLAGS`, x27=`HW_SCRATCH`**; `local_disp(n)=~(n)*8`; `arg_disp(n)=n*8+16`.
- Direct map: local `n` → `xn` (`n < 24`); no `spill_all` on bytecode `call`.
- No `rtcall` / `record_branch` / `jump_chain` / `xreg()` shadows.
- Smoke + fibonacci `-O` done; full `-O` suite is the 2026-07-27 plan.
- Do not port riscv/arm32 in this plan.
- Commits only when the user requests them.

---

### Task 1: Rewrite `arm64.h` — register model + helper API

**Files:**
- Modify: `backend/arm64.h`
- Reference: `backend/x86.h` (structure), spec §Registers

**Interfaces:**
- Produces: `enum` with `HW_X0`…`HW_X30`, `HW_V0F`… / `HW_V0D`…, `HW_FRAME=HW_X29`, `HW_FLAGS=HW_X22`, `HW_SCRATCH=HW_X16`, `HW_COUNT`; `hw()`, `hw_local()`, `local_disp()`, `arg_disp()`; helper decls matching x86 (`load_local`, `binop_local(…, op_t)`, `bra_pred(byte, byte_p, bool)`, `rel_branch`, `rel_function`, `prologue`/`epilogue`, …).
- Consumes: `backend.h`, `backend-iface.h`.

- [ ] **Step 1:** Replace header register-map comments with native-frame conventions from the spec.
- [ ] **Step 2:** Replace `enum hwreg_t` / `xreg()` with allocator-facing enum + `HW_COUNT` (physical numbers still equal AArch64 Xn/Vn indices where needed for encoding).
- [ ] **Step 3:** Keep working encoder inlines (`mov_rr`, `ldr_off`, `str_off`, `b_imm`, `tbz_imm`, ALU, FP); adjust any that hardcode `HW_FRAME==19` to use `HW_FRAME`.
- [ ] **Step 4:** Update helper declarations to `local_t` / `op_t` / `byte_p` signatures; remove `rtcall` / `jump` / `resolve_jump` / `patch_branch_site` / `spill_frame` decls.
- [ ] **Step 5:** Compile-check header inclusion: `clang++ -std=gnu++17 -fsyntax-only -I../recorder -I../runtime -I. -c arm64.cpp` may fail on .cpp; at least ensure .h is self-consistent.

---

### Task 2: Implement iface + prologue in `arm64.cpp`

**Files:**
- Modify: `backend/arm64.cpp` (top half)
- Reference: `backend/x86.cpp` iface section

**Interfaces:**
- Produces: `machine_defs`, `registers`, `pointer_size` (=8), `begin_translation`/`finish_translation`, `patch`, `call`/`syscall`, `relocate`, `move`/`load`/`store`, `register_definition`, `prologue`/`epilogue`, `call_ptr`.
- Consumes: Task 1 enum + encoders.

- [ ] **Step 1:** Implement `register_definition` — mark x0–x18 call-clobbered (no `RSPEC_CALL`); x19–x28 callee-saved with `RSPEC_CALL|RSPEC_INT` as allocatable; x22 flags not allocatable (spec 0); x29/x30/sp/x16 not allocatable; FP regs `RSPEC_FLT`/`RSPEC_DBL`.
- [ ] **Step 2:** Implement `load`/`store`/`move` using LDR/STR/MOV relative to `HW_FRAME` + `local_disp`.
- [ ] **Step 3:** Port `patch` from old `patch_branch_site` logic to `patch(hwcode_t, hwcode_t, bool)` for B / B.cond / CBZ / TBZ; range errors → `arm64_error`.
- [ ] **Step 4:** `call`/`syscall` → `spill_all` + `call_ptr` (LDR literal / BR via x16).
- [ ] **Step 5:** `prologue` push callee-saved used (x19,x20,x21,x22,x23… as needed); `epilogue` restore + `ret`.
- [ ] **Step 6:** `relocate` return `uint`, clear simple reloc list like x86 (or no-op if unused).
- [ ] **Step 7:** Compile `arm64.cpp` against stubs for helpers if emit still broken — or leave helpers next.

---

### Task 3: Core helpers (movers, binops, preds, branches)

**Files:**
- Modify: `backend/arm64.cpp`
- Reference: `backend/x86.cpp` helpers

**Interfaces:**
- Produces: `load_local`, `store_local`, `mov_local`, `swap_locals`, `store_imm`, `store_float`/`store_double`, `binop_local`/`binop_imm` (`op_t`), `cmp_*`, `set_pred`, `cond_mov_local`, `select_local`, unary ops, mem ops, `bra_pred`/`bra_test`, `rel_branch`/`rel_function`, `dispatch`.
- Consumes: Task 2 iface.

- [ ] **Step 1:** Movers via `hw(local(loc))` — no shadow write-through.
- [ ] **Step 2:** `binop_local` / `binop_imm` `switch (op)` using existing ALU encoders.
- [ ] **Step 3:** Predicates on `HW_FLAGS` (bit set/clear/test); prefer TBZ/TBNZ for `bra_pred`.
- [ ] **Step 4:** `rel_branch` / `rel_function` identical control flow to x86 (`branch`/`function` then `patch` if off≠0).
- [ ] **Step 5:** Delete dead old helpers (`rtcall*`, `jump_chain`, …).

---

### Task 4: Rewrite `arm64-emit.cpp` to x86-emit semantics

**Files:**
- Modify: `backend/arm64-emit.cpp`
- Modify: `backend/arm64.cpp` — move `#define EMIT` to end of `.cpp` and `#include "arm64-emit.cpp"` like x86; remove EMIT define from emit file if present.
- Reference: `backend/x86-emit.cpp`

**Interfaces:**
- Consumes: Task 3 helpers.
- Produces: All `emit_*` / `emit_sys_*` compiling.

- [ ] **Step 1:** For each `EMIT` body, align with x86-emit control flow (same helper calls; ARM encoding inside helpers).
- [ ] **Step 2:** `enter`/`leave`/`get`/`set`/`push`/`pop` inline per spec (no rtcall).
- [ ] **Step 3:** Branches use `e.bpc + e.broff` + `rel_branch` / `bra_*`.
- [ ] **Step 4:** `jmp`/`call` use `rel_function`; `sys` uses `dispatch_sys`.
- [ ] **Step 5:** `end` → `epilogue(); e.pc = e.end`; `null`/`undef` → `syscall(xlr8_*)`.

---

### Task 5: Build + smoke

**Files:** none (verify)

- [x] **Step 1:** `clang++ -std=gnu++17 -c -I../recorder -I../runtime -I. arm64.cpp backend.cpp` (and deps) — clean compile.
- [x] **Step 2:** Build enough of the product to run `tests/xlr8` (or `make` skipping broken backends if required).
- [x] **Step 3:** Smoke: `cd tests && make ref-addi ref-addition ref-bfalse` and same with `OPTLEVEL=-O`; compare to `.ref`.
- [x] **Step 4:** Fibonacci `-O` via direct-map regalloc (`0a582d4`). Full suite: see `2026-07-27-arm64-O-failures-after-direct-map.md`.

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Native frame x29/x30, disp formulas | 1, 4 |
| Delete rtcall/record_branch/shadows | 1, 3, 4 |
| iface patch/load/store/… | 2 |
| Helpers local_t/op_t/rel_* | 3 |
| Thin EMIT parity with x86-emit | 4 |
| Smoke done bar | 5 |
| Direct-map regalloc / fibonacci `-O` | 2026-07-27 specs |
| Remaining `-O` failures | `plans/2026-07-27-arm64-O-failures-after-direct-map.md` |
