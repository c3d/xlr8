# Riscv64 Backend Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `backend/riscv64*` to the same architecture as refactored x86/arm64/arm32 (native frame, `local()`, `branch`/`label`/`patch`, thin EMIT bodies).

**Architecture:** Mirror `arm64.h` / `arm64.cpp` / `arm64-emit.cpp`. Keep RV64 encoders; replace shadows/`rtcall`/`record_branch` with allocator + native enter/leave. Spec: `docs/superpowers/specs/2026-07-24-riscv64-backend-refactor-design.md`.

**Tech Stack:** C++17, RV64 SysV ABI encodings, existing `backend` / `regmap` / recorder / `xlr8_jit_write_*`.

## Global Constraints

- Frame: x8=`HW_FRAME` (s0/fp), x1=`HW_RA` (ra), x9=`HW_FLAGS` (s1), x5=`HW_IP` (t0 scratch).
- Allocatable INT: x18–x27 (s2–s11) only (`RSPEC_CALL|RSPEC_INT`); a0–a7 not for live locals.
- `local_disp(n)=~(n)*sizeof(ularge_t)`; `arg_disp(n)=n*sizeof(ularge_t)+16`; `pointer_size()=8`.
- No `rtcall` / `record_branch` / `jump_chain` / shadow `HW_LOC*` / `rt.frame` priming.
- `frame_active`: load/store touch frame only after `op_enter`.
- Prologue saves fp+ra before any helper `jalr`.
- `leave` does not auto-return; optional leave+ret peephole (consume `op_ret`, emit return); no `spill_all` on that path.
- Done when: Mac `make` includes `riscv64.cpp`; smoke `addi`/`addition`/`bfalse` pass on `ssh fedora-riscv` at `OPTLEVEL=` and `-O` vs `.ref`.
- Commits only when the user requests them.

## File map

| File | Role after refactor |
|------|---------------------|
| `backend/riscv64.h` | Register enum, encoders, helper decls, disp helpers |
| `backend/riscv64.cpp` | iface + helpers; `#include "riscv64-emit.cpp"` at end |
| `backend/riscv64-emit.cpp` | Thin `EMIT` / `SYS` only |
| `backend/Makefile` | Re-add `riscv64.cpp` |
| `backend/backend.cpp` | `make(ARCH_RISCV64)` → `riscv64::make(rt)` |

Reference: `backend/arm64.{h,cpp}`, `arm64-emit.cpp`. Baseline: `work/xlr8/backend/riscv64.*`.

---

### Task 1: Rewrite `riscv64.h` — register model + helper API

**Files:**
- Modify: `backend/riscv64.h`
- Reference: `backend/arm64.h`, spec §Registers

**Interfaces:**
- Produces: `enum` with `HW_X0`…`HW_X31` (physical = ABI numbers), `HW_FRAME=HW_X8`, `HW_RA=HW_X1`, `HW_FLAGS=HW_X9`, `HW_IP=HW_X5`, `HW_SP=HW_X2`, `HW_COUNT`; `hw()`, `hw_local()`, `local_disp()`, `arg_disp()`; helper decls matching arm64; `bool frame_active`.
- Consumes: `backend.h`, `backend-iface.h`.

- [ ] **Step 1:** Replace header comments with native-frame conventions; delete shadow x10–x25 narrative.
- [ ] **Step 2:** Replace `HW_LOC*` / shadow helpers with allocator-facing enum + `HW_COUNT`.
- [ ] **Step 3:** Keep working RV64 encoder inlines (addi, add, ld/sd, jal/jalr, beq/bne/blt/…, lui/auipc, …); drop shadow-only helpers.
- [ ] **Step 4:** Add:

```cpp
int local_disp(int n) const { return ~n * int(sizeof(ularge_t)); }
int arg_disp(int n) const {
    return n * int(sizeof(ularge_t)) + 16;
}
```

- [ ] **Step 5:** Update helper decls to `local_t` / `op_t` / `byte_p`; remove `rtcall` / `jump` / `resolve_jump` / shadow spill decls.

---

### Task 2: Implement iface + prologue in `riscv64.cpp`

**Files:**
- Modify: `backend/riscv64.cpp` (rewrite top half)
- Reference: `backend/arm64.cpp` iface

**Interfaces:**
- Produces: `machine_defs`, `registers`, `pointer_size` (=8), `begin_translation`/`finish_translation`, `patch`, `call`/`syscall`, `relocate`, `move`/`load`/`store`, `register_definition`, `prologue`/`epilogue`, `call_ptr`.
- Consumes: Task 1.

- [ ] **Step 1:** `register_definition` — only x18–x27 allocatable (`RSPEC_CALL|RSPEC_INT`); x1/x2/x5/x8/x9 and a0–a7/t* not allocatable for live locals.
- [ ] **Step 2:** `load`/`store`/`move` with `frame_active` gating (zero-on-load / `saved` only when inactive; ld/sd via fp+`local_disp` when active).
- [ ] **Step 3:** `patch` for J-type and B-type; misaligned/out-of-range → `riscv64_error`, no truncate.
- [ ] **Step 4:** `call`/`syscall` → `spill_all` + `call_ptr` (load abs into t0/`HW_IP`, `jalr`).
- [ ] **Step 5:** Prologue: save fp+ra + callee-saved used (s1–s11 as needed); set fp. Epilogue: restore + `jalr zero, 0(ra)` (or `ret`).
- [ ] **Step 6:** `begin_translation`: `frame_active=false`; backend begin; prologue. `finish_translation`: epilogue; backend finish.
- [ ] **Step 7:** Helper stubs OK for Task 3; compile-check when Makefile includes the file (or standalone `-c` with includes).

---

### Task 3: Core helpers (movers, binops, preds, branches)

**Files:**
- Modify: `backend/riscv64.cpp`
- Reference: `backend/arm64.cpp` helpers

**Interfaces:**
- Produces: movers, `store_imm`, `binop_local`/`binop_imm` (`op_t`), cmp/pred/select, unary, mem, `bra_pred`/`bra_test`, `rel_branch`/`rel_function`, `dispatch`.
- Consumes: Task 2.

- [ ] **Step 1:** Movers via `hw(local(loc))`.
- [ ] **Step 2:** `binop_*` `switch (op)` with RV64 ALU encoders.
- [ ] **Step 3:** Predicates on `HW_FLAGS`; branches via B-type + `rel_branch`.
- [ ] **Step 4:**

```cpp
void riscv64::rel_branch(byte_p target, hwcode_t site) {
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off, true);
}
```

Same for `rel_function`.
- [ ] **Step 5:** Delete dead old helpers.

---

### Task 4: Rewrite `riscv64-emit.cpp`

**Files:**
- Modify: `backend/riscv64-emit.cpp`
- Modify: `backend/riscv64.cpp` — `#define EMIT`/`SYS` at end + `#include "riscv64-emit.cpp"`
- Reference: `backend/arm64-emit.cpp`

**Interfaces:**
- Consumes: Task 3; produces all `emit_*` / `emit_sys_*`.

- [ ] **Step 1:** Align each `EMIT` with arm64-emit control flow.
- [ ] **Step 2:** `enter`/`leave`/`get`/`set`/`push`/`pop` per spec; `frame_active` on enter/leave; leave+ret peephole without `spill_all`.
- [ ] **Step 3:** Branches use `e.bpc + e.broff` + `rel_branch` / `bra_*`.
- [ ] **Step 4:** `jmp`/`call` → `rel_function`; `sys` → `dispatch_sys(e)`.
- [ ] **Step 5:** `end` → `e.pc = e.end` only; `null`/`user`/`undef` → `syscall(xlr8_*)`.
- [ ] **Step 6:** `SYS(write_integer/token/newline)` with a0=stream, a1=value/tok; `call_ptr(xlr8_jit_write_*)`.

---

### Task 5: Wire build + Mac compile

**Files:**
- Modify: `backend/Makefile`, `backend/backend.cpp`

- [ ] **Step 1:** Re-add `riscv64.cpp` to sources.
- [ ] **Step 2:** `#include "riscv64.h"`; `case ARCH_RISCV64: return riscv64::make(rt);`.
- [ ] **Step 3:** `make` on macOS — 0 errors.
- [ ] **Step 4:** `./xlr8 tests/bytecode/addi.xlr8` interpreter still OK on Mac.

---

### Task 6: Smoke on `ssh fedora-riscv`

**Files:** none (verify).

- [ ] **Step 1:** User must commit (and tip must be on `mac/xlr8`) before remote can sync — ask if not yet committed.
- [ ] **Step 2:**

```bash
ssh fedora-riscv 'cd ~/Work/xlr8 && git fetch mac && git reset --hard mac/xlr8 && git submodule update --init --recursive && make'
```

- [ ] **Step 3:** `OPTLEVEL=` smoke `ref-addi ref-addition ref-bfalse` — Failed tests: 0; outputs match `.ref`.
- [ ] **Step 4:** Same with `OPTLEVEL=-O`.
- [ ] **Step 5:** On failure: check ra save, callee-saved alloc, TOK LEB128, write_* AAPCS (SysV a0/a1).

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Native frame x8/x1, disp formulas | 1, 2, 4 |
| Delete rtcall/shadows | 1, 3, 4 |
| iface patch/load/store/prologue ra | 2 |
| Helpers local_t/op_t/rel_* | 3 |
| Thin EMIT + write_* + leave peephole | 4 |
| Makefile + make(ARCH_RISCV64) | 5 |
| Smoke on fedora-riscv | 6 |
