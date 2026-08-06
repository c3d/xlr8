# Register allocation: direct mapping (arm64)

**Date:** 2026-07-27 (updated 2026-08-03)  
**Status:** Implemented on arm64; stubs on x86 / arm32 / riscv64  

## Goal

Keep bytecode locals in fixed hardware registers (direct map). Prefer
**callee-saved** homes for the first locals so typical frames survive C/`sys`
calls without spill/fill, while still supporting recursive XLR8 `call` via
`enter`/`leave`.

## Success signals

- `tests/performance/fibonacci.xlr8` passes with `OPTLEVEL=-O`.
- `tests/bootstrap/unicode-characters.xlr8` passes at `-O` after call-clobbered
  X preservation (loop counters survive `sys write_*`).
- `make perf OPTLEVEL=-O` is green on arm64 hosts.

## Shared allocator API (`backend`)

| Piece | Role |
|-------|------|
| `reg_local[hw]` / `local_reg[loc]` | Bidirectional map; `NO_LOCAL` / `NO_REGISTER` when empty |
| `local(loc, spec, ld)` | Reuse mapped reg if `register_matches`; else `spill` and `register_for` |
| `saved` / `spill` / `spill_all` | Update maps; `store` on spill |
| `forget_registers()` | Drop maps without stores (after leave / frame discard) |
| `register_sync()` | After each `emit_*`: spill overflow temps; remap FP → X |
| `callee_entry` | Set when starting a translated callee body |

Targets **must** implement `register_for`, `register_sync`, `move`/`load`/`store`,
`register_definition`. Only **arm64** does today; others still stub.

## Arm64 policy: callee-saved-first direct map

### Integer map (`iorder` / `iorder_reg`)

| Locals | Hardware registers | Notes |
|--------|--------------------|--------|
| **0…8** | **x19…x27** | Callee-saved; survive C/`sys` without spill |
| **9…23** | **x0…x14** | Caller-saved / AAPCS64 args+temps |

```
local 0  → x19    local 9  → x0
local 1  → x20    local 10 → x1
…                 …
local 8  → x27    local 23 → x14
```

### Constants (`arm64.h`)

| Name | Value | Meaning |
|------|-------|---------|
| `HW_ARGS` | 8 | AAPCS64 integer arg/return regs (`x0`–`x7`) |
| `HW_CLOBBERED` | 19 | `x0`–`x18` caller-saved under AAPCS64 (ABI fact) |
| `HW_SPILL` | 15 | C-spill set: durable homes `x0`–`x14` only |
| `HW_SHADOWS` | 24 | Direct GPR homes (see `iorder`) |
| `HW_FSHADOWS` | 12 | Direct FP homes (`v0`–`v11`) |
| `HW_TEMPS` | 3 | Overflow temps (`x16`–`x18`; `x15` is `HW_SCRATCH`) |
| `HW_RESERVED` | `x18` | Platform/kernel scratch on macOS — not a durable home |
| `HW_SCRATCH` | `x15` | General scratch |
| `HW_FLAGS` | `x28` | Sticky flags |
| `HW_FRAME` | `x29` | Frame pointer; `local_disp(n) = ~(n)*8` |

### FP bank (transient)

```
local n, RSPEC_FLT  →  v0f+n    for n < 12
local n, RSPEC_DBL  →  v0d+n    for n < 12
```

After every opcode, `register_sync` bit-copies live `Vn` back to **`iorder[n]`**
(not `x0+n`) and clears the V map entries. X is the durable home.

### Sys / C helpers — preserve call-clobbered X

`CALL` goes through `syscall()`:

1. Live overflow temps (`x16`–`x18`) are spilled to the **bytecode frame** and
   their maps cleared — temps are ephemeral across `blr`.
2. `hw_sys_arg` / `hw_sys_imm` place ABI args in `x0`–`x7` (moving from
   `iorder[loc]` or a frame-filled temp) and spill via `hw_sys_arg_spill`.
3. C spill covers physical **`x0`–`x14`** (`HW_SPILL`) — durable homes for
   locals **9…23**. Scratch `x15` and temps `x16`–`x18` are **not** saved in
   the C spill area. Locals **0…8** in `x19`–`x27` need no spill.
4. After the helper, `hw_sys_arg_reload()` restores spilled GPRs; `hw_sys_ret`
   moves `x0` into the destination local’s home when that is not `x0`.

Spill slot index = **physical register number**, not bytecode local index.
`HW_SPILL` is 15 (`x0`–`x14`); spill/reload loops pair through `x13` and store
`x14` alone so they never touch temps or `x19` (local 0).

### enter / leave (callee frame holds caller local homes)

`enter N` allocates **`N+1` slots** (last usable local index is `N`).

```
; enter N  (frame = N+1)
stp  x29, x30, [sp, #-16]!
mov  x29, sp
; push caller iorder[0]… in pairs (stp_pre), shadows = min(frame,24) rounded up even
; if frame > shadows: sub sp for the remaining slots

; leave — reload saved iorder homes from [x29, #local_disp], then tear FP/LR
ldp  iorder[i+1], iorder[i], [x29, #local_disp(i+1)]
mov  sp, x29
ldp  x29, x30, [sp], #16
```

`get`/`set` for indices `< HW_SHADOWS` and `< local_frame_size` use the
**current** frame (`[x29, #local_disp(…)]`) — the saved copies of the caller’s
**local homes** — instead of walking through `[x29]` to the parent.

### Recursive `call`

No AAPCS64 marshal and no `spill_all`. Arguments and the return value stay in
local homes (`iorder`). Optional FP/LR save around `bl` only when there is no
bytecode `enter` yet (`!pre_enter_frame_size`).

`ret` still mirrors local 0 into `x0` for host/AAPCS64 observers; XLR8 callers
do not depend on that move.

## Why this mapping

| Concern | Approach |
|---------|----------|
| Cheap C/`sys` | Locals 0…8 in callee-saved regs → no spill |
| Recursive XLR8 | In-place homes + enter/leave save `iorder` |
| `get`/`set` | Frame slots indexed by local, holding `iorder[i]` |
| Do **not** assume `local 0 == x0` | Spill/reload and enter/leave use physical regs / `iorder` |

## Consistency audit

| Area | Status |
|------|--------|
| `iorder` locals 0…8 → x19…x27, 9…23 → x0…x14 | OK |
| FP remap V→`iorder[n]` in `register_sync` | OK |
| C/`sys` spill of `x0`–`x18` only | OK |
| `enter`/`leave` save/restore `iorder` | OK |
| Bytecode `call` in-place (no x0 marshal) | OK |
| x86 / arm / riscv64 `register_for` / `register_sync` | Still stubs |

## Out of scope here

Porting this map to x86/arm/riscv64; instruction-level peepholes from
`BACKEND-OPTIMIZATION.md`. Remaining historical `-O` notes:
`docs/superpowers/plans/2026-07-27-arm64-O-failures-after-direct-map.md`.
