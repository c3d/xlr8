# x86 direct-map allocator (align with arm64)

**Date:** 2026-08-06  
**Status:** Approved design  
**Depends on:** `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md`  
**Validation:** `PLATFORM-ENABLEMENT.md` (tmux protocol)

## Goal

Align JIT infrastructure for **x86-64 (turbo)** and **i386 (crazypad)** with the
arm64 direct-map model on macOS: shared allocator bookkeeping in `backend`,
per-arch register policy and emit. Do **phase A** (allocator) first, then
**phase B** (feature parity). Host order: turbo, then crazypad. Keep already-green
platforms green on every commit.

## Decisions

| Topic | Choice |
|-------|--------|
| Scope order | A (allocator), then B (parity) |
| Host order | turbo (amd64), then crazypad (i386 / `BUILDENV=i686`) |
| Map policy | Callee-saved-first; reassign reserved regs if it helps |
| Implementation approach | Extract shared core from arm64 into `backend`, then plug x86 policy |
| Green bar | Incremental: each commit must not regress already-green hosts |

## Phase A — Architecture

### Shared core (`backend`)

Lift policy-agnostic machinery out of arm64 so x86 only supplies ABI tables and
emit helpers:

- Existing bidirectional maps: `local_reg` / `reg_local`, `local` / `saved` /
  `spill` / `spill_all` / `forget_registers`
- **Branch spill bookmarking:** `regspill { bytecode, spilled_mask }`,
  `label()` recording, lookup helpers (today private to `arm64`)
- **Frame nesting bookkeeping:** `pre_enter_frame_size` / `local_frame_size`
  coordination (state in base; emit remains per-arch)
- Target hooks (unchanged contract): `register_for`, `register_sync`,
  `move` / `load` / `store`, `register_definition`

### Per-arch (arm64 and x86)

- `iorder` / FP order / `HW_*` (shadows, spill set, temps, frame, flags)
- Encoding for load/store/move, C/`sys` spill area, enter/leave push of homes
- Optional register reassignment for better durable-home count

### Migration sequence

1. Extract arm64 spill-tracking / shared state into `backend` with
   **behavior-identical** arm64 wrappers — validate macOS (and preferably
   torpille) **before** any x86 functional change.
2. Implement x86-64 policy + emit on turbo; full suite green per commit.
3. Adapt i386 on crazypad.
4. Phase B only after A is green on both x86 hosts.

## Phase A — x86-64 register map (turbo)

Callee-saved-first direct map (same idea as arm64).

| Locals | Hardware | Notes |
|--------|----------|--------|
| 0…4 | `rbx`, `r12`, `r13`, `r14`, `r15` | Callee-saved; survive C/`sys` without spill. **Move flags off `r15`** (today’s home) so local 4 can use it — e.g. sticky flags in a frame slot or a caller-saved always spilled around helpers. Exact flags home chosen in implementation if a cleaner option appears. |
| 5…N | caller-saved subset | e.g. `rax`, `rdx`, `rsi`, `rdi`, `r8`–`r11` (minus scratch); spilled around C/`sys` by physical reg index |

**Reserved / not durable homes:**

- `rbp` — frame (`HW_FRAME`); `local_disp(n) = ~(n)*8` unchanged
- One scratch (today `rcx`) — temps / encoding constraints (`shl` by `%cl`, etc.)
- `rsp` — stack

**FP:** transient XMM homes for float/double locals (mirror arm64 V→X). After
each opcode, `register_sync` bit-copies live XMM back to the local’s durable
GPR home and clears the XMM map.

**C/`sys`:** spill only the caller-saved durable set (`HW_SPILL`); callee-saved
homes stay live. Temps frame-synced before the call. Spill slot = physical
register index (arm64 rule).

**enter / leave:** save/restore `iorder[i]` into callee frame slots (not
“physical xi”); in-range `get`/`set` use those slots. Bytecode `call` stays
in-place (no SysV marshal of XLR8 args).

## Phase A — i386 (crazypad)

Same `x86` family sources; `BUILDENV=i686` / `ptr64()==false`. Shrink of the
amd64 map, not a second allocator API. Tmux pane already exports
`BUILDENV=i686` — no extra env on send-keys.

| Role | i386 choice |
|------|-------------|
| Frame | `ebp` (`HW_FRAME`) |
| Callee-saved homes | as many as practical after frame/scratch — typically `ebx`, `esi`, `edi` (flags may live in frame high half as today, or one dedicated reg if still needed) |
| Caller-saved homes | `eax`, `edx`, … minus scratch (`ecx`) |
| FP | XMM if available under the build; otherwise keep float bits in GPR/frame and skip transient XMM bank |
| Spill / enter / leave | same shared backend bookkeeping; emit uses 32-bit ops and existing i386 encodings |

No i386 functional commits until turbo phase A is full-suite green. Each i386
commit validated on crazypad; macOS + turbo must stay green.

## Phase B — Feature parity

Only after turbo + crazypad phase A are full-suite green. One concern per
commit; turbo then crazypad.

**In scope:**

- **Globals access** — align `sto`/`rcl` with arm64 globals-base style
- **`pca` / bytecode address translation** — match arm64 JIT-aware handling
- **`rt.invoke` save/restore** — preserve allocator/runtime state across invoke
- **enter/leave frame-size visibility** — default/pre-enter sizing consistent
  with arm64
- **Docs** — update `regmap.h`, this family of specs, and `BACKEND-REVIEW.md`
  register tables for x86-64 / i386

**Out of scope for this effort:**

- arm32 / riscv64 allocator ports
- Perf-suite harness fixes (e.g. turbo `timeout`) unless they block functional green
- Peephole / encoding micro-opts from optimization docs

## Validation protocol

Follow `PLATFORM-ENABLEMENT.md` strictly:

- Commit on Mac (`xlr8`); **never push**
- Remotes: pane already in tree; sync via `git fetch mac && git reset --hard
  mac/xlr8` + submodule update through send-keys / validate script — no SCP
- Fan-out: `scripts/platform-validate-all.sh` (or `scripts/tmux-all` for
  sync/build smokes)
- Success = real `[SUMMARY] Failed tests: 0` from teed logs

### Per-commit gates

| Stage | Must stay green | Primary target |
|-------|-----------------|----------------|
| Extract shared core | macOS (± torpille) | no x86 behavior change |
| x86-64 map / spill / enter | macOS + turbo | turbo |
| i386 adapt | macOS + turbo + crazypad | crazypad |
| Phase B items | all three above | turbo then crazypad |

If an already-green host goes red: fix on Mac, amend that commit, re-validate
in parallel until clean before the next commit.

## Done when

- **Phase A:** turbo and crazypad at full functional **226 / 0** (or current
  baseline count) for reference and `-O`; shared core extracted; arm64 still
  green on macOS
- **Phase B:** listed parity items landed with the same green bar; docs updated

## Consistency with existing arm64 spec

This design does not change arm64’s shipping `iorder` / spill policy. It
generalizes the bookkeeping so x86 can implement the same *contract* with an
amd64/i386-appropriate register map. Historical `-O` notes for arm64 remain in
`docs/superpowers/plans/2026-07-27-arm64-O-failures-after-direct-map.md`.
