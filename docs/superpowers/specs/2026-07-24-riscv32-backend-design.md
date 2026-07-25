# Riscv32 backend (thin subclass of riscv64)

**Date:** 2026-07-24  
**Status:** Implemented (compile-only done bar)  
**Reference:** Refactored `backend/riscv64.*`; family precedent `amd64 : x86` (inverted: ILP32 on LP64 base)

## Goal

Add an **RV32** JIT target as a thin `struct riscv32 : riscv64` that reuses riscv64 emit/helpers and overrides only XLEN-sensitive behavior (pointer width, load/store word size, frame save size, imm/address materialization).

## Success criteria

- Compiles on macOS: `riscv32.cpp` in `libbackend.a`, `backend::make(rt, ARCH_RISCV32)` returns a live backend.
- `machine_defs()` reports `__riscv` and `__riscv_xlen 32`.
- No remote smoke this pass (compile-only done bar).

## Non-goals

- Separate `riscv32-emit.cpp` peer fork
- Full suite / fedora-riscv smoke
- Changing ularge_t slot size (locals remain `sizeof(ularge_t)` bytes)
- Host auto-select of ARCH_RISCV32 unless building on real RV32 (`__riscv && __riscv_xlen==32`)

## Architecture

| Piece | Role |
|-------|------|
| `riscv64` (base) | Full backend; expose a few `virtual` hooks for word size / pointer loads |
| `riscv32` (derived) | Overrides those hooks; same register map and EMIT bodies |
| Shared emit | Existing `#include "riscv64-emit.cpp"` in base — no second emit file |

### Wire-up

- Add `ARCH_RISCV32` to `backend::arch_t` and `arch_name`.
- `backend::make`: `case ARCH_RISCV32: return riscv32::make(rt);`
- Makefile: compile `riscv32.cpp` (and keep `riscv64.cpp`).
- Optional: `ARCH_NATIVE` on RV32 Linux → `ARCH_RISCV32`.

### Register / control model (unchanged from riscv64)

- Frame x8, ra x1, flags x9, scratch x5; allocatable x18–x27.
- `frame_active`, leave does not auto-return; leave+ret peephole without `spill_all`.
- `dispatch_sys` / `xlr8_jit_write_*` via a0/a1.

### ILP32 differences (overrides)

| Concern | RV64 | RV32 |
|---------|------|------|
| `pointer_size()` | 8 | 4 |
| Saved fp+ra on enter | 16 bytes (`sd`/`ld`) | 8 bytes (`sw`/`lw`) |
| `arg_disp` base | `+16` | `+8` (`2 * pointer_size()`) |
| Local slots | `sizeof(ularge_t)` (8) | same; high half 0 when storing 32-bit GPR into 8-byte slot (arm32 pattern) |
| Pointer / helper imm | `load_imm64` | 32-bit materialization (`load_uimm32` / `lui`+`addi`) |
| `call_ptr` target | 64-bit into t0 | 32-bit into t0 |

### Base hooks to virtualize (minimal)

Make the following overridable on `riscv64` (names illustrative):

- `pointer_size()` (already virtual on `backend`)
- `arg_disp()` or compute via `2 * pointer_size()`
- Word store/load used by enter/leave/prologue/epilogue (`sd`/`ld` vs `sw`/`lw`)
- `load_ptr_imm` / `call_ptr` absolute address width
- `emit_load_off` / `emit_store_off` for pointer-sized accesses if they assume 64-bit

Prefer small virtual helpers over copying prologue/enter bodies wholesale.

## Implementation order

1. Add `ARCH_RISCV32` + stubs in `make`/`arch_name`.
2. Virtualize minimal hooks in `riscv64` (no behavior change for RV64).
3. Add `riscv32.h` / `riscv32.cpp` with overrides + `machine_defs`.
4. Makefile + compile check on Mac.
5. Optional unit/smoke later on an RV32 host — out of scope.

## Testing

```bash
make   # must compile riscv32.cpp with 0 errors
# Optional: temporary test harness calling backend::make(rt, ARCH_RISCV32)
```

Commits only when the user requests them.

## Risks

1. Enter/leave/`arg_disp` mismatch if fp+ra save size is wrong (8 vs 16).
2. Forgetting to zero high half of 8-byte local slots on ILP32.
3. Over-virtualizing the base — keep the hook set small.
4. `jalr`/`auipc` immediates still fine; absolute `call_ptr` must not emit RV64-only sequences on RV32.

## Decisions (locked)

- Structure: **C** — thin `riscv32 : riscv64`.
- Done bar: **A** — compile-only on Mac.
