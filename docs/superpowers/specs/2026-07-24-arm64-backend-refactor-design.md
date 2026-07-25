# Arm64 backend refactor (x86 parity)

**Date:** 2026-07-24  
**Status:** Approved design (pending implementation plan)  
**Reference:** Refactored `backend/x86.{h,cpp}`, `x86-emit.cpp`, `amd64.*`; pre-refactor baseline under `work/xlr8/backend/`

## Goal

Port the arm64 JIT backend to the same architecture as the refactored x86 backend: native frame (enter/leave), `backend::local()` allocation, `branch`/`function`/`label`/`patch` fixups, thin `EMIT(op)` emitters, no `rtcall` / `record_branch` / fixed shadow map.

## Success criteria

- Compiles on macOS (arm64 objects + shared backend pieces used by arm64).
- Small smoke subset under `tests/bytecode` passes interpreter and `-O` against `.ref` (e.g. `addi`, `addition`, `bfalse`, plus one exercise of `enter`/`get` if available).
- Full suite and **torpille** verification are **out of scope** for this pass.

## Non-goals

- riscv64 / arm32 ports
- BACKEND-OPTIMIZATION.md performance work
- Keeping a hybrid `rt.frame` + new allocator model

## Architecture

| Layer | Files | Responsibility |
|-------|--------|----------------|
| Shared | `backend.h`, `backend.cpp` | `local()`, `branch()`/`function()`/`label()`, translate loop |
| Arm64 API | `arm64.h` | Register enum, AArch64 encoders, helper decls, `local_disp` / `arg_disp` |
| Arm64 impl | `arm64.cpp` | iface + helpers; `#include "arm64-emit.cpp"` at end (x86 style) |
| Emitters | `arm64-emit.cpp` | Thin `EMIT(op)` bodies only |

Arm64 is a peer of x86 under the new backend — not a subclass of x86.

### Patterns to mirror from x86

- Opcode bodies use `emit_args` fields; branch targets are `e.bpc + e.broff`.
- Binops/cmps take `op_t`, not `cstring`.
- Runtime entry via `syscall(fn)` / `call(fn)` with function pointers (or inline machine ops), not `rtcall("xlr8_…")`.
- Relative sites: emit placeholder → `rel_branch` / `rel_function` → `backend::branch`/`function` → `patch` (immediate if labeled, else deferred via `label()`).
- `begin_translation` / `finish_translation` wrap `prologue` / `epilogue`.

### Delete from current arm64

`rtcall`, `record_branch`, `jump_chain`, `jump`/`resolve_jump`, `code_offset`, `xreg(n)` fixed shadows, `HW_ANCHOR` / `rt.frame` priming paths, string-based `binop_local`, `spill_frame` / `touch_local` / `reload_shadows` (replaced by `spill`/`saved`/`spill_all`), old void `relocate` using `code_offset`.

## Registers and frame

| Role | Register | Notes |
|------|----------|--------|
| Frame | x29 (`HW_FRAME`) | Native FP, like RBP |
| Link | x30 | Saved with FP on enter; epilogue `ret` |
| Flags | x22 (`HW_FLAGS`) | Callee-saved; 64-bit predicates |
| Scratch | x16 (`HW_SCRATCH`) | Temps / call targets; not allocatable |
| Allocatable GPRs | Subset of x0–x15, x19–x21, x23–x28 | Via `local()` + `RSPEC_*` |
| FP | v-regs | `RSPEC_FLT` / `RSPEC_DBL` |

### enter / leave

```
; enter n
stp  x29, x30, [sp, #-16]!
mov  x29, sp
sub  sp, sp, #(n * 8)

; leave
mov  sp, x29
ldp  x29, x30, [sp], #16
```

### Displacements

- `local_disp(n) = ~(n) * 8` → local 0 at `[x29, #-8]` (same formula as x86).
- `arg_disp(n) = n * 8 + 16` → skip saved FP+LR; arg0 at `[x29, #16]`.

### JIT prologue / epilogue

Save/restore callee-saved registers pinned by the backend (at least x22; plus any callee-saved allocatable used as shadows). Do not prime `HW_FRAME` from `rt.frame` at JIT entry — enter owns the frame.

## Implementation order

1. Iface: `patch`, `move`/`load`/`store`, `register_definition`, `call`/`syscall`, `relocate`, prologue/epilogue.
2. Movers: `load_local` / `store_local` / `store_imm` / `mov_local` via `local()`.
3. Binops/cmps: `op_t` switches; keep existing AArch64 encoders where valid.
4. Branches: `rel_branch` / `rel_function`; update all `EMIT(b*)`.
5. Stack/control: inline `enter`/`leave`/`get`/`set`/`push`/`pop`/`ret`/`jmp`/`call`.
6. `sys` + `dispatch_sys`; remove remaining `rtcall`.
7. FP ops last if smoke does not require them.

Compile after each chunk. Prefer keeping other backends from blocking arm64 iteration (build arm64 objects directly if needed).

## Patch / branch notes

- `patch(pos, target, rel)` must rewrite B, B.cond, CBZ/CBNZ, TBZ/TBNZ immediates correctly.
- Out-of-range immediates: record `arm64_error` (do not silently truncate).
- Known-target path must go through `patch` (or equivalent) so encoding/range checks apply — same discipline as x86 `rel32_branch`.

## Testing

Smoke on macOS:

```bash
# build arm64 path (full backend make may still fail on unreworked riscv/arm)
make -C backend   # or targeted clang++ of arm64.cpp + backend.cpp

cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=
cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=-O
```

Verify output against `.ref`, not exit code alone.

## Risks

1. Wrong `local_disp` / `arg_disp` relative to `stp fp,lr` breaks get/set/enter.
2. Call-clobbered regs (x0–x18) must not be treated as `RSPEC_CALL` unless spilled across sys/call.
3. TBZ/B.cond range limits need trampolines or error paths for long branches.
4. Do not mix optimization refactors into this port.

## Decisions (locked)

- Frame model: **A** — full native enter/leave parity with x86.
- Done bar: **C** — compiles + smoke subset; full suite later.
- Approach: **1** — mirror x86 structure 1:1 (not strangler, not shared emit generator).
