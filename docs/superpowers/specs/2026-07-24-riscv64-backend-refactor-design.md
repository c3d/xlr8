# Riscv64 backend refactor (x86 / arm64 / arm32 parity)

**Date:** 2026-07-24  
**Status:** Approved design (pending implementation plan)  
**Reference:** Refactored `backend/x86.*`, `arm64.*`, `arm.*`; pre-refactor baseline under `work/xlr8/backend/riscv64.*`

## Goal

Port the RISC-V 64 (RV64GC / SysV ABI) JIT backend to the same architecture as the refactored x86, arm64, and arm32 backends: native frame (enter/leave), `backend::local()` allocation, `branch`/`function`/`label`/`patch` fixups, thin `EMIT(op)` emitters, no `rtcall` / `record_branch` / fixed shadow map.

## Success criteria

- Compiles on macOS as part of `libbackend.a` (host may be aarch64; riscv64 objects are portable C++ emitting RV64 encodings).
- `backend::make(rt, ARCH_RISCV64)` returns a live `riscv64` backend (stub removed).
- Smoke subset on **`ssh fedora-riscv`** (PLATFORM-ENABLEMENT nickname **risky**): `tests/bytecode` `addi`, `addition`, `bfalse` (and optionally one `enter`/`get` test) pass interpreter and `-O` against `.ref`.
- Sync on fedora-riscv per PLATFORM-ENABLEMENT: `git fetch mac && git reset --hard mac/xlr8`, submodule update, `make`, then smoke.
- Full suite on risky and other platforms are **out of scope** for this pass.

## Non-goals

- BACKEND-OPTIMIZATION.md performance work
- Keeping a hybrid `rt.frame` + new allocator model
- Completing every `SYS(*)` stub beyond write_integer / write_token / write_newline for smoke

## Architecture

| Layer | Files | Responsibility |
|-------|--------|----------------|
| Shared | `backend.h`, `backend.cpp` | `local()`, `branch()`/`function()`/`label()`, translate loop; TOK=LEB128 |
| Riscv64 API | `riscv64.h` | Register enum, RV64 encoders, helper decls, `local_disp` / `arg_disp` |
| Riscv64 impl | `riscv64.cpp` | iface + helpers; `#include "riscv64-emit.cpp"` at end |
| Emitters | `riscv64-emit.cpp` | Thin `EMIT(op)` / `SYS(*)` bodies only |

Riscv64 is a peer of x86/arm64/arm under the new backend — not a subclass.

### Patterns to mirror

- Opcode bodies use `emit_args`; branch targets are `e.bpc + e.broff`.
- Binops/cmps take `op_t`, not `cstring`.
- Runtime entry via `syscall(fn)` / `call(fn)` / `call_ptr`, not `rtcall("xlr8_…")`.
- Relative sites: placeholder → `rel_branch` / `rel_function` → `backend::branch`/`function` → `patch`.
- `begin_translation` / `finish_translation` wrap `prologue` / `epilogue`.
- Prefer callee-saved allocatable GPRs so values survive `sys` / helper calls.
- No frame load/store until `op_enter` (`frame_active`); otherwise zero-on-load / keep-in-reg on store.
- Prologue saves ra (and fp); helper `jalr` must not lose the JIT return address.
- `EMIT(end)` does not emit epilogue (finish_translation does).
- `leave` does **not** automatically return. Optional peephole: if next opcode is `op_ret`, emit return after leave and consume that `ret` (`e.pc++`). No `spill_all` on that path — the frame is being destroyed. Branches that target the `ret` bytecode are translated separately when that label is picked up.

### Delete from current riscv64

`rtcall`, `record_branch` / `jump` / `resolve_jump`, shadow `xreg` / `HW_LOC*`, `rt.frame` priming in prologue, string-based binops, old spill-through-shadow helpers, void relocate keyed on old offsets.

## Registers and frame

| Role | Register | Notes |
|------|----------|--------|
| Frame | x8 / s0 (`HW_FRAME`) | Native FP (SysV) |
| Return addr | x1 / ra (`HW_RA`) | Saved with FP on enter / in JIT prologue |
| Flags | x9 / s1 (`HW_FLAGS`) | Callee-saved; predicate bits |
| Scratch | x5 / t0 (`HW_IP`) | Temps / call targets; not allocatable |
| Allocatable | x18–x27 (s2–s11) | `RSPEC_CALL \| RSPEC_INT` only |
| Call args | x10–x17 (a0–a7) | Not allocatable for live locals |
| SP | x2 | Native stack; 16-byte aligned at calls |

FP (f/d) banks only if smoke needs them; otherwise stub like other backends.

### enter / leave

```
; enter n
addi sp, sp, -16
sd   ra, 8(sp)
sd   fp, 0(sp)
mv   fp, sp
addi sp, sp, -(n * 8)   ; ularge_t slots

; leave (restore only — does not return)
mv   sp, fp
ld   fp, 0(sp)
ld   ra, 8(sp)
addi sp, sp, 16

; leave immediately followed by ret (peephole):
;   same leave sequence, then jalr zero, 0(ra); consume op_ret
```

### Displacements

- `local_disp(n) = ~(n) * sizeof(ularge_t)` → local 0 at `[fp, #-8]`.
- After saved fp+ra: `arg_disp(n) = n * sizeof(ularge_t) + 16` → arg0 at `[fp, #16]`.
- `pointer_size() = 8`.

### JIT prologue / epilogue

Save/restore fp+ra and callee-saved regs used by the backend (at least s1=`HW_FLAGS`, plus s2–s11 as allocatable). Do not load `HW_FRAME` from `rt.frame`. Set `frame_active` only in `EMIT(enter)`; clear in `EMIT(leave)`.

## Implementation order

1. Iface: `patch`, `move`/`load`/`store`, `register_definition`, `call`/`syscall`, `relocate`, prologue/epilogue.
2. Movers via `local()`.
3. Binops/cmps with `op_t`; keep existing RV64 encoders where valid.
4. Branches: `rel_branch` / `rel_function`; update all `EMIT(b*)`.
5. Stack/control: inline `enter`/`leave`/`get`/`set`/`push`/`pop`/`ret`/`jmp`/`call` (leave+ret peephole as above).
6. `sys` + `dispatch_sys`; wire write_integer/token/newline to `xlr8_jit_write_*` (a0/a1 SysV).
7. FP last if smoke does not require it.

Restore `riscv64.cpp` in `backend/Makefile` and `#include "riscv64.h"` in `backend::make`.

## Patch / branch notes

- `patch(pos, target, rel)` must rewrite RV64 J-type (jal) and B-type branch immediates correctly.
- Out-of-range: `riscv64_error` (do not silently truncate).
- Known-target path must go through `patch` so range checks apply.

## Testing

**Local (macOS):**

```bash
make   # riscv64.cpp must compile into libbackend.a
```

**Remote (`ssh fedora-riscv`):**

```bash
git fetch mac && git reset --hard mac/xlr8
git submodule update --init --recursive
make
cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=
cd tests && make ref-addi ref-addition ref-bfalse OPTLEVEL=-O
```

Verify output against `.ref`, not exit code alone. Commits only when the user requests them; remotes never commit. Use generous `timeout` on risky if needed (PLATFORM-ENABLEMENT).

## Risks

1. Wrong `local_disp` / `arg_disp` vs `sd`/`ld` of fp+ra breaks get/set/enter.
2. Call-clobbered a0–a7/t* must not hold live locals across sys/call.
3. B-type (±4KiB) and J-type (±1MiB) range limits may need trampolines or errors for long jumps.
4. Forgetting to save ra in prologue causes post-sys corruption/segfaults.
5. Do not mix optimization refactors into this port.
6. Slow host: prefer few SSH round-trips; batch sync+build+smoke.

## Decisions (locked)

- Frame model: **A** — full native enter/leave parity with x86/arm64/arm32.
- Done bar: compile on Mac + smoke subset on **`ssh fedora-riscv`** (risky).
- Approach: **1** — mirror x86/arm64/arm32 structure 1:1.
- Leave does not auto-return; optional leave+ret peephole without `spill_all`.
