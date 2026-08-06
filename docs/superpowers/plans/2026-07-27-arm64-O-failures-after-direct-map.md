# Arm64 / -O: remaining failures after direct-map regalloc

**Date:** 2026-07-27 (updated after clobber/FP/enter follow-ups)  
**Depends on:** `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md`  
**Host check:** arm64 macOS; compare JIT output to `.ref` (not stale `*-O.ref`)

## Baseline

- **Passing:** `performance/fibonacci` `-O`; `bootstrap/unicode-characters` `-O` (after `db5bcab`).
- **Ground truth:** interpreter / `.ref`. Delete or regenerate `*-O.ref` only when JIT matches `.ref`.

## Done since first draft

| Item | Resolution |
|------|------------|
| Call-clobbered `x0`–`x18` across `sys` | `syscall()` spills live mapped regs; `CALL` uses `syscall` (`db5bcab`) |
| FP V0–V7 clobber | `register_sync` remaps live V → matching `Xn` and clears V maps (`85f6bf1`) |
| `hw_sys_ret` bitmask | `&= ~(1<<loc)` |
| Double spill | assert in `hw_sys_arg` / `hw_sys_imm` (`f5dbead`) |
| `x18` as direct home | `HW_RESERVED`; overflow path (`f619e72`) |
| Caller regs on enter | Save/restore in callee frame; `get`/`set` use local copy (`7efc68a`) |
| `enter N` size | Frame has `N+1` locals (`daf854b`) |

## Failure clusters still open

### P0

1. **Top-level `enter` vs entry stub** — partially softened (`enter` twice only if `num_functions`); re-check `high-local` / `sys-alloca`.
2. **Stale `*-O.ref`** — delete wrong refs; re-run so failures are visible.

### P1 — Call / frame / flags

3. **Sticky predicates (language)** — done in docs/interpreter/tests; JIT already
   keeps preds in `HW_FLAGS` (`x28`) without enter/leave save/restore.
4. **`forget_registers` on leave** — maps can disagree with restored HW regs.
5. **Globals / `sto`/`rcl`** — globals base wiring.
6. **Indirect control flow** (`jmp` / `ijmp` / `icall`) — bytecode vs native address.

### P1 — Sys / memory / I/O

7. Memory natives (`memset`, `memmove`, `kmem*`, …) and I/O (`read_*`, …) — re-validate after clobber spill; fix remaining marshalling.
8. **`free` / `realloc` / `alloca`** — re-check crashes.

### P2 — Multi-call / loops

9. **`recursive-fibonacci` loop**, factorial, nested defs, intermediate loop prints — live locals / flags across repeated bytecode `call` + `sys`.
10. **`select` / `ccopy` / `neg`** — pred or scratch clashes.

### P3 — Portability / docs

11. Implement `register_for` / `register_sync` on x86, arm32, riscv64 (policy may differ from arm64 direct map).
12. Keep `BACKEND-REVIEW.md` register table in sync with this model.

## Suggested sequence

```text
1. Re-run -O vs .ref; delete bad *-O.ref; refresh failure list
2. Flags save/clear across enter/leave/call + forget_registers on leave
3. globals / jmp·ijmp·icall
4. Sweep sys/mem/I/O leftovers
5. Multi-call fib/factorial loops
6. Port allocator to other backends
```

## Done when

- `cd tests && make OPTLEVEL=-O` — no unexpected fails vs `.ref` on arm64 host for `bytecode/` (performance timeouts OK).
- Spec and `arm64.h` comments match shipping policy.
- No `*-O.ref` that disagrees with a passing `.ref`.
