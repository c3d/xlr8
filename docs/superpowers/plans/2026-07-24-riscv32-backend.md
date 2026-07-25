# Riscv32 Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `ARCH_RISCV32` as a thin `riscv32 : riscv64` subclass with ILP32 overrides; Mac compile-only done bar.

**Architecture:** Virtualize minimal XLEN hooks on `riscv64`; `riscv32` overrides pointer size, word load/store, frame save size, imm/call_ptr. Spec: `docs/superpowers/specs/2026-07-24-riscv32-backend-design.md`.

**Tech Stack:** C++17, existing riscv64 backend.

## Global Constraints

- Structure: `struct riscv32 : riscv64` only — no separate emit file.
- Locals stay `sizeof(ularge_t)` slots; pointer_size=4; enter fp+ra save = 8 bytes (`sw`/`lw`).
- `arg_disp` base = `2 * pointer_size()`.
- Done: Mac `make` compiles; `make(ARCH_RISCV32)` works. No remote smoke.
- Commits only when the user requests them.
- Keep RV64 behavior unchanged (hooks default to current 64-bit paths).

---

### Task 1: Virtualize XLEN hooks on riscv64

**Files:** `backend/riscv64.h`, `backend/riscv64.cpp`, possibly `riscv64-emit.cpp` if enter/leave hardcode `sd`/`ld`.

- [x] **Step 1:** Ensure `arg_disp` uses `2 * pointer_size()` (not hardcoded 16).
- [x] **Step 2:** Add virtual helpers e.g. `store_ra_fp_frame()`, `load_ra_fp_frame()`, or width-aware `emit_store_ptr`/`emit_load_ptr` used by enter/leave/prologue/epilogue/`call_ptr`.
- [x] **Step 3:** Default implementations keep `sd`/`ld` and `load_imm64` for pointers.
- [x] **Step 4:** Compile — RV64 path unchanged.

---

### Task 2: Add riscv32 subclass + wire-up

**Files:** Create `backend/riscv32.h`, `backend/riscv32.cpp`; modify `backend.h`, `backend.cpp`, `Makefile`.

- [x] **Step 1:** `ARCH_RISCV32` in enum + `arch_name` → `"riscv32"`.
- [x] **Step 2:** `riscv32` ctor passes `ARCH_RISCV32`; override `pointer_size()→4`, `machine_defs()`, word/ptr hooks (`sw`/`lw`, 32-bit imm for `call_ptr`).
- [x] **Step 3:** `make(ARCH_RISCV32) → riscv32::make(rt)`; `#include "riscv32.h"`.
- [x] **Step 4:** Optional: `#elif __riscv && __riscv_xlen==32` → `ARCH_RISCV32` for `ARCH_NATIVE`.
- [x] **Step 5:** Makefile add `riscv32.cpp`; `make` → 0 errors.

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Virtual hooks, RV64 unchanged | 1 |
| riscv32 subclass + ARCH + Makefile | 2 |
| Compile-only done bar | 2 |
