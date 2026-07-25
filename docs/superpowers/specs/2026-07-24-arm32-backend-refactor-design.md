# Arm32 backend refactor (x86 / arm64 parity)

**Date:** 2026-07-24  
**Status:** Approved design (pending implementation plan)  
**Reference:** Refactored `backend/x86.*`, `backend/arm64.*`; pre-refactor baseline under `work/xlr8/backend/arm.*`

## Goal

Port the 32-bit ARM (AArch32 / AAPCS, ARM mode) JIT backend to the same architecture as the refactored x86 and arm64 backends: native frame (enter/leave), `backend::local()` allocation, `branch`/`function`/`label`/`patch` fixups, thin `EMIT(op)` emitters, no `rtcall` / `record_branch` / fixed shadow map.

## Success criteria

- Compiles on macOS as part of `libbackend.a` (host is aarch64; arm32 objects are still built as portable C++ emitting ARM encodings).
- `backend::make(rt, ARCH_ARM)` returns a live `arm` backend (stub removed).
- Smoke subset on **`ssh pi3`** (SSH Host alias → `ptipi.local`, the former pi3 box): `tests/bytecode` `addi`, `addition`, `bfalse` (and optionally one `enter`/`get` test) pass interpreter and `-O` against `.ref`.
- Sync on pi3 per PLATFORM-ENABLEMENT: `git fetch mac && git reset --hard mac/xlr8`, submodule update, `make`, then smoke.
- Full suite on pi3 and **torpille**/other platforms are **out of scope** for this pass.

## Non-goals

- riscv64 port
- BACKEND-OPTIMIZATION.md performance work
- Keeping a hybrid `rt.frame` + new allocator model
- Thumb / Thumb-2 emission (stay ARM mode, TinyCC-compatible, as today)

## Architecture

| Layer | Files | Responsibility |
|-------|--------|----------------|
| Shared | `backend.h`, `backend.cpp` | `local()`, `branch()`/`function()`/`label()`, translate loop; TOK=LEB128 |
| Arm32 API | `arm.h` | Register enum, AArch32 encoders, helper decls, `local_disp` / `arg_disp` |
| Arm32 impl | `arm.cpp` | iface + helpers; `#include "arm-emit.cpp"` at end (x86/arm64 style) |
| Emitters | `arm-emit.cpp` | Thin `EMIT(op)` bodies only |

Arm32 is a peer of x86/arm64 under the new backend — not a subclass.

### Patterns to mirror from x86 / arm64

- Opcode bodies use `emit_args` fields; branch targets are `e.bpc + e.broff`.
- Binops/cmps take `op_t`, not `cstring`.
- Runtime entry via `syscall(fn)` / `call(fn)` with function pointers, not `rtcall("xlr8_…")`.
- Relative sites: emit placeholder → `rel_branch` / `rel_function` → `backend::branch`/`function` → `patch`.
- `begin_translation` / `finish_translation` wrap `prologue` / `epilogue`.
- Prefer callee-saved allocatable GPRs so values survive `sys` / helper calls.
- No frame load/store until `op_enter` has allocated slots (`frame_active`), same lesson as arm64.
- Prologue must save LR (and FP); helper calls via `bl`/`blx` must not lose the JIT return address.

### Delete from current arm32

`rtcall`, `record_branch`, `jump_chain`, shadow `rreg()` / `HW_LOC*`, `rt.frame` priming in prologue, string-based binops, old spill-through-shadow helpers, void relocate keyed on old offsets.

## Registers and frame

| Role | Register | Notes |
|------|----------|--------|
| Frame | r11 (`HW_FRAME`) | Native FP (AAPCS) |
| Link | r14 (lr) | Saved with FP on enter / in JIT prologue |
| Flags | r10 (`HW_FLAGS`) | Callee-saved; predicate bits |
| Scratch | r12 (`HW_IP`) | Temps / call targets; not allocatable |
| Avoid | r9 | Platform/PIC SB on some ABIs — leave alone |
| Allocatable | r4–r8 | `RSPEC_CALL \| RSPEC_INT` (callee-saved) |
| Call args | r0–r3 | Not allocatable for live locals |
| SP | r13 | Native stack; 8-byte aligned at calls |

FP (VFP/NEON) only if smoke needs it; otherwise stub like other backends’ incomplete SYS/FP paths.

### enter / leave

```
; enter n
push {fp, lr}          ; or stmfd sp!, {fp, lr}
mov  fp, sp
sub  sp, sp, #(n * 8)  ; ularge_t slots

; leave
mov  sp, fp
pop  {fp, lr}          ; restore only — does not return

; leave immediately followed by ret (peephole):
mov  sp, fp
pop  {fp, pc}          ; consume ret; branches to that ret translate separately
```

### Displacements

- `local_disp(n) = ~(n) * sizeof(ularge_t)` → local 0 at `[fp, #-8]` (same as x86/arm64).
- After `push {fp, lr}` only: `arg_disp(n) = n * sizeof(ularge_t) + 2 * pointer_size()` → arg0 at `[fp, #8]` (mirror arm64’s `+ 2 * 8`, not i386’s extra flags slot).
- `pointer_size() = 4`.

### JIT prologue / epilogue

Save/restore FP+LR and callee-saved regs used by the backend (r4–r8, r10 at minimum). Do not load `HW_FRAME` from `rt.frame`. Set `frame_active` only in `EMIT(enter)`; clear in `EMIT(leave)`.

## Implementation order

1. Iface: `patch`, `move`/`load`/`store`, `register_definition`, `call`/`syscall`, `relocate`, prologue/epilogue.
2. Movers via `local()`.
3. Binops/cmps with `op_t`; keep existing ARM encoders where valid.
4. Branches: `rel_branch` / `rel_function`; update all `EMIT(b*)`.
5. Stack/control: inline `enter`/`leave`/`get`/`set`/`push`/`pop`/`ret`/`jmp`/`call`.
6. `sys` + `dispatch_sys`; wire write_integer/token/newline to existing `xlr8_jit_write_*` helpers.
7. FP last if smoke does not require it.

Restore `arm.cpp` in `backend/Makefile` and `#include "arm.h"` in `backend::make`.

## Patch / branch notes

- `patch(pos, target, rel)` must rewrite ARM `B` / `BL` / conditional `B` immediates (24-bit signed, word-aligned).
- Out-of-range: `arm_error` (do not silently truncate).
- Known-target path must go through `patch` so range checks apply.

## Testing

**Local (macOS):**

```bash
make -C backend   # or full make — arm.cpp must compile
```

**Remote (`ssh pi3` → ptipi.local):**

```bash
git fetch mac && git reset --hard mac/xlr8
git submodule update --init --recursive
make
cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=
cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=-O
```

Verify output against `.ref`, not exit code alone. Commits only when the user requests them; remotes never commit.

## Risks

1. Wrong `local_disp` / `arg_disp` vs `push {fp,lr}` breaks get/set/enter.
2. Call-clobbered r0–r3/r12 must not hold live locals across sys/call.
3. Branch range (±32MB) may need trampolines or errors for long jumps.
4. Forgetting to save LR in prologue causes post-sys segfaults (seen on arm64).
5. Do not mix optimization refactors into this port.

## Decisions (locked)

- Frame model: **A** — full native enter/leave parity with x86/arm64.
- Done bar: compile on Mac + smoke subset on **`ssh pi3`** (Host alias for ptipi.local).
- Approach: **1** — mirror x86/arm64 structure 1:1.
- Remote access: **B** — keep `pi3` SSH alias; scripts/docs may still say pi3.
