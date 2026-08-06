# x86 Direct-Map Allocator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract arm64’s policy-agnostic allocator bookkeeping into `backend`, then implement callee-saved-first direct mapping on x86-64 (turbo) and i386 (crazypad) so both match the arm64 contract and pass the full functional suite at ref and `-O`.

**Architecture:** Shared spill bookmarks / frame nesting state live in `backend`. Each target supplies `iorder`, `register_for` / `register_sync`, and C/`sys` spill emit. x86 grows in place (amd64 first, then i386 shrink). Phase B (globals base, `pca`, invoke save/restore, frame-size visibility) follows only after both x86 hosts are green on Phase A.

**Tech Stack:** C++ JIT backends (`backend/`, `arm64.*`, `x86.*`); validation via `PLATFORM-ENABLEMENT.md` tmux protocol (`scripts/platform-validate-all.sh`, `scripts/tmux-all`).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-06-x86-direct-map-allocator-design.md`
- Arm64 reference model: `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md`
- Commit on Mac only; never `git push`; remotes only `git fetch mac` + `git reset --hard mac/xlr8`
- Tmux only: session `xlr8`, target `xlr8:0.0`; no `cd` in pane commands; crazypad pane already has `BUILDENV=i686`
- Each commit must not regress already-green hosts; amend + re-validate if a green host goes red
- Success = real `[SUMMARY] Failed tests: 0` from teed logs (not exit codes alone)
- Host order: extract (macOS) → turbo → crazypad → Phase B

## File map

| File | Role |
|------|------|
| `backend/backend.h` / `backend.cpp` | Shared `regspill`, `spilled_args`, `pre_enter_frame_size`, `local_frame_size`, `label()` spill record |
| `backend/arm64.h` / `arm64.cpp` | Thin wrappers onto shared state; keep emit/`iorder` here |
| `backend/x86.h` / `x86.cpp` | `HW_*`, `iorder`, `register_for`/`register_sync`, sys spill helpers, prologue/flags |
| `backend/x86-emit.cpp` | enter/leave/call, `CALL`/`ARG`/`RET` macros, SYS bodies |
| `backend/regmap.h` | Comment update for x86-64 / i386 maps |
| `docs/superpowers/specs/*`, `BACKEND-REVIEW.md` | Docs after green |

**Helper — tmux send to one host (from Mac):**

```bash
# Usage: pane_cmd HOST 'shell command...'
pane_cmd() {
  local host="$1"; shift
  local cmd="$*"
  local qcmd; qcmd=$(printf %q "$cmd")
  ssh "$host" "tmux send-keys -t 'xlr8:0.0' C-c; sleep 0.2; tmux send-keys -t 'xlr8:0.0' C-u; tmux send-keys -t 'xlr8:0.0' -l $qcmd; tmux send-keys -t 'xlr8:0.0' Enter"
}
```

**Helper — full parallel validation:**

```bash
scripts/platform-validate-all.sh
# Completion line per host: XLR8_VALIDATE_DONE exit=0
# Then: grep '^\[SUMMARY\].*Failed tests' /tmp/xlr8-validate-*/... as printed by the script
```

---

### Task 1: Extract spill bookmarks + frame nesting into `backend`

**Files:**
- Modify: `backend/backend.h` (protected members + `label` override path)
- Modify: `backend/backend.cpp` (`label`, constructors/`begin_translation` resets)
- Modify: `backend/arm64.h` (remove duplicate members; keep arch-specific helpers)
- Modify: `backend/arm64.cpp` (use base members; keep `register_sync` / `hw_sys_*`)

**Interfaces:**
- Consumes: existing `backend::label(byte_p)`, `arm64::regspill` pattern
- Produces: `backend::regspill`, `backend::spills`, `backend::spilled_args`, `backend::local_frame_size`, `backend::pre_enter_frame_size`, `backend::num_functions`; `label()` records `spilled_args` when non-zero path is active

- [ ] **Step 1: Add shared types/state to `backend.h` (protected)**

Add next to existing `reg_local` / `local_reg`:

```cpp
    uint          local_frame_size = 0;
    uint          spilled_args = 0;
    uint          num_functions = 0;
    uint          pre_enter_frame_size = 0;

    struct regspill { byte_p bytecode; uint spilled; };
    using regspills = std::vector<regspill>;
    regspills     spills;

    friend struct regspill_compare;
    template<typename T> static auto lower_bound(const regspills &, T);
    template<typename T> static auto equal_range(const regspills &, T);
```

Move `regspill_compare` and the `lower_bound` / `equal_range` templates for `regspills` from `arm64.cpp` into `backend.cpp` (same comparator logic keyed on `bytecode` / `spilled`).

- [ ] **Step 2: Record spill mask in `backend::label`**

In `backend.cpp` `label()`, after resolving branches/calls and before/while inserting into `labels`, record the current spill mask (mirror arm64):

```cpp
    auto spill = lower_bound(spills, bytecode);
    if (spill == spills.end() || spill->bytecode != bytecode)
        spills.emplace(spill, regspill{ bytecode, spilled_args });
```

Reset `spilled_args`, `pre_enter_frame_size`, and clear `spills` appropriately in `begin_translation()` (arm64 today clears `spilled_args` / `pre_enter_frame_size` there; `num_functions++` stays in finish path per target or move carefully to avoid double-count).

- [ ] **Step 3: Delete duplicates from `arm64.h` / constructor**

Remove `local_frame_size`, `spilled_args`, `num_functions`, `pre_enter_frame_size`, `regspill`, `spills`, and friend/templates from `arm64`. Constructor should not re-init those if base defaults suffice; keep `arm64::label` override only if it still needs arch-specific work — prefer calling `backend::label` then returning, or delete override if base is sufficient.

Current arm64 override:

```cpp
uint arm64::label(byte_p bytecode)
{
    uint brcount = backend::label(bytecode);
    auto spill  = lower_bound(spills, bytecode);
    if (spill == spills.end() || spill->bytecode != bytecode)
        spills.emplace(spill, regspill{ bytecode, spilled_args });
    return brcount;
}
```

After Step 2, delete `arm64::label` override entirely if behavior is identical.

- [ ] **Step 4: Build and validate macOS (no x86 behavior change)**

```bash
make -j"$(sysctl -n hw.ncpu)"
make tests 2>&1 | tee /tmp/xlr8-mac-ref.log
make tests OPTLEVEL=-O 2>&1 | tee /tmp/xlr8-mac-opt.log
grep '^\[SUMMARY\].*Failed tests' /tmp/xlr8-mac-ref.log /tmp/xlr8-mac-opt.log
```

Expected: both show `Failed tests: 0` (current baseline count, e.g. 226).

Optional: `scripts/platform-validate-all.sh` and confirm torpille still green.

- [ ] **Step 5: Commit**

```bash
git add backend/backend.h backend/backend.cpp backend/arm64.h backend/arm64.cpp
git commit -m "$(cat <<'EOF'
backend: Lift spill bookmarks and frame nesting out of arm64

Share regspill tracking and enter/leave frame bookkeeping so x86 can
reuse the same allocator contract without duplicating label state.
EOF
)"
```

---

### Task 2: x86-64 `HW_*` map + `register_for` / `register_sync`

**Files:**
- Modify: `backend/x86.h` (constants, `hw_int_order`, flags home, comments)
- Modify: `backend/x86.cpp` (`register_for`, `register_sync`, `register_definition` RSPEC bits)
- Modify: `backend/regmap.h` (x86-64 / i386 comment block)

**Interfaces:**
- Consumes: `backend::local`, `spill`, `load`, `store`, `move`
- Produces: amd64 `HW_SHADOWS`, `HW_SPILL`, `HW_TEMPS`, `HW_FSHADOWS`, `hw_int_order(loc)`, working `register_for` / `register_sync`

**Amd64 integer map (from spec):**

| Locals | Regs |
|--------|------|
| 0…4 | `rbx`, `r12`, `r13`, `r14`, `r15` (callee-saved) |
| 5… | caller-saved durable homes: `rax`, `rdx`, `rsi`, `rdi`, `r8`–`r11` minus scratch |

- Frame: `rbp`. Scratch: `rcx`. Flags: **not** `r15` — use a dedicated approach from the spec (prefer sticky flags in a callee-saved that is *not* in `iorder`, or spill flags to `[rbp+…]` around helpers). Concrete pick for this task: keep flags in **memory at a fixed frame slot** for the first green cut *or* use `r11` as non-durable flags temp that is always saved/restored around `sys` — document the choice in `x86.h` comments. Prefer keeping a real HW flags register if prologue/`invoke` can be updated in the same commit without breaking macOS (macOS does not run x86 JIT).

- [ ] **Step 1: Add constants + `hw_int_order` in `x86.h`**

```cpp
        HW_ARGS      = 6,   // SysV: rdi,rsi,rdx,rcx,r8,r9 — rcx is scratch; track carefully
        HW_SPILL     = /* count of caller-saved durable homes */,
        HW_SHADOWS   = /* len(iorder) */,
        HW_FSHADOWS  = 8,   // xmm0–xmm7 transient (tune if needed)
        HW_TEMPS     = 2,
        // HW_TEMP0 / HW_FTEMP0 / HW_DTEMP0 / HW_SCRATCH / HW_FLAGS / HW_FRAME
```

Implement `static x86reg_t hw_int_order(local_t loc)` returning the table above (assert/`record` on OOR like arm64).

Update `register_definition` so durable integer homes have `RSPEC_INT`, callee-saved homes also `RSPEC_CALL`, XMM entries `RSPEC_FLT` / `RSPEC_DBL`.

- [ ] **Step 2: Implement `register_for` (mirror arm64 structure)**

Replace the stub in `x86.cpp` with logic parallel to `arm64::register_for`:
- If `loc < HW_SHADOWS` (or `HW_FSHADOWS` for FP): return `hw_int_order` / XMM order; on FP load, move bits from durable GPR home into XMM.
- Else allocate from `HW_TEMP0…` with spill-on-busy.

- [ ] **Step 3: Implement `register_sync`**

Mirror arm64: spill temps; clear stale temp forward maps; bit-copy live XMM → `hw_int_order(loc)`; if `label_branches(pc)` then reload C-spill set (call into helpers added in Task 3 — for this task, call a no-op or existing `spill` path until Task 3 lands; prefer landing Task 2+3 in one commit if split would leave `-O` unusable).

**Note:** If Task 2 alone cannot keep a coherent JIT, combine Tasks 2–4 into one commit on turbo after local compile checks — still validate with tmux before declaring done.

- [ ] **Step 4: Local compile smoke (Mac cross or syntax)**

```bash
make -j"$(sysctl -n hw.ncpu)"
# macOS arm64 build must still pass:
make tests OPTLEVEL=-O 2>&1 | tee /tmp/xlr8-mac-opt.log
grep '^\[SUMMARY\].*Failed tests' /tmp/xlr8-mac-opt.log
```

Expected: macOS still `Failed tests: 0`.

- [ ] **Step 5: Commit** (or continue into Task 3 before commit if intentionally combined)

```bash
git add backend/x86.h backend/x86.cpp backend/regmap.h
git commit -m "$(cat <<'EOF'
x86: Add direct-map register_for/register_sync for amd64

Callee-saved-first iorder and FP→GPR sync mirror the arm64 allocator
contract so hw_local stops falling back to rax.
EOF
)"
```

---

### Task 3: x86 C/`sys` spill + `syscall()` path

**Files:**
- Modify: `backend/x86.h` (`hw_sys_arg`, `hw_sys_imm`, `hw_sys_ret`, spill/reload/respill)
- Modify: `backend/x86.cpp` (`syscall` — stop using blind `spill_all` as the only strategy)

**Interfaces:**
- Consumes: `spilled_args`, durable `HW_SPILL` set, `emit_store`/`emit_load` via frame or SP area
- Produces: arm64-equivalent `hw_sys_arg*` API used by emit macros

- [ ] **Step 1: Implement spill area helpers on amd64**

Follow arm64’s pattern (physical reg index = spill slot):
- `hw_sys_arg_spill()` — allocate stack area; store each caller-saved durable home
- `hw_sys_arg_reload()` / `hw_sys_arg_reload(reg, load)` / `hw_sys_arg_respill()`
- `hw_sys_arg(arg, loc)` / `hw_sys_imm` / `hw_sys_ret` — place SysV args in `rdi,rsi,rdx,rcx,r8,r9` **without permanently stealing durable homes**; if arg reg aliases a home, spill first (same asserts as arm64 “spill once”)

Scratch `rcx` conflicts with SysV 4th arg — either exclude `rcx` from durable homes (recommended) or move scratch to another reg for the duration of helper marshalling.

- [ ] **Step 2: Change `x86::syscall`**

```cpp
void x86::syscall(void (*target)())
{
    // Ensure temps are frame-synced; durable callee-saved stay live.
    // Caller-saved durable set handled by hw_sys_arg_spill inside CALL macro
    // or spill here if CALL not used yet:
    if (!spilled_args)
        hw_sys_arg_spill();
    call_ptr(target);
}
```

Align with how arm64 `CALL` does `hw_sys_arg_respill()` before `syscall()`.

- [ ] **Step 3: Sync to turbo and smoke one JIT test**

```bash
git commit  # if not yet committed with Task 2
pane_cmd turbo 'git fetch mac && git reset --hard mac/xlr8 && git submodule update --init --recursive && make -j$(nproc) && ./xlr8 -O tests/performance/fibonacci.xlr8'
```

Expected: builds; fibonacci may still fail until SYS/`enter` done — record failure mode. Prefer progressing Task 4 before claiming green.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
x86: Spill caller-saved direct-map homes across C helpers

Replace blind spill_all syscalls with physical-reg spill slots matching
the arm64 HW_SPILL protocol.
EOF
)"
```

---

### Task 4: enter / leave / bytecode `call` + CALL macros + SYS port

**Files:**
- Modify: `backend/x86-emit.cpp` (`EMIT(enter)`, `EMIT(leave)`, `EMIT(call)`, `EMIT(ret)`, SYS section)
- Modify: `backend/x86.cpp` / `x86.h` (prologue saves all callee-saved homes used by `iorder`)

**Interfaces:**
- Consumes: `hw_int_order`, `hw_sys_*`, `pre_enter_frame_size`, `local_frame_size`
- Produces: enter saves `iorder[i]` into frame slots; leave restores; `CALL`/`ARG`/`RET`/`IMM`/`NORET` macros; SYS bodies filled from arm64 counterparts

- [ ] **Step 1: Update prologue/epilogue / `invoke` for new callee-saved set**

Amd64 prologue must push every callee-saved reg used as a durable home (`rbx,r12–r15`) plus any still-needed saves. Update `PROLOGUE_SAVE_BYTES()`, `arg_disp()`, and `invoke` asm (`rbp` + flags register) to match the flags decision from Task 2.

- [ ] **Step 2: Rewrite `EMIT(enter)` / `EMIT(leave)` like arm64**

- `enter N`: frame size `N+1`; set `pre_enter_frame_size`; push FP; save `iorder[0]…` into local slots (pairs where possible); allocate remaining stack for high locals.
- `leave`: reload `iorder` homes from slots; restore FP; restore `local_frame_size` from `pre_enter_frame_size`; `forget_registers()` only if maps would be stale — prefer matching arm64 (reload homes, clear pre_enter).
- `get`/`set` for `src < HW_SHADOWS && src < local_frame_size`: use `local_disp` on current frame (saved caller homes), not parent walk — mirror `arm64-emit.cpp`.

- [ ] **Step 3: Bytecode `call` / `ret`**

- `call`: **no** `spill_all`; keep args in local homes; optional FP save only if `!pre_enter_frame_size` (arm64 LR pattern — on x86 this may mean ensuring return address / no extra marshal).
- `ret`: mirror local 0 into ABI return reg if needed for host observers; do not require SysV for XLR8 callers.

- [ ] **Step 4: Add CALL macros and fill SYS bodies**

At the SYS section of `x86-emit.cpp`, add:

```cpp
#define SYS(op) void x86::emit_sys_##op(emit_args &e)
#define ARG(a, loc)   hw_sys_arg(a, loc);
#define IMM(a, val)   hw_sys_imm(a, val);
#define RET(loc)      hw_sys_ret(loc);
#define SRET(loc,sz)  hw_sys_ret(loc, sz);
#define NORET         /* no output */

#define CALL(fn, inputs, outputs)                   \
    do                                              \
    {                                               \
        hw_sys_arg_respill();                       \
        inputs;                                     \
        syscall(reinterpret_cast<void (*)()>(fn));  \
        outputs;                                    \
    } while (0)
```

Port each non-empty arm64 `SYS(...)` body from `arm64-emit.cpp` to x86, adapting register names via `ARG`/`IMM`/`RET`. Replace empty stubs. Keep existing handwritten `write_integer` / `write_token` paths only if they already match; otherwise convert to `CALL`.

For i386 (`!ptr64()`), use stack-arg marshalling inside `hw_sys_arg` / `hw_sys_imm` (push) rather than SysV regs — can be stubbed to “amd64 only” until Task 6 if `#if`/`ptr64()` guards keep i386 compiling.

- [ ] **Step 5: Validate turbo fully**

```bash
# after commit + fetch on host
pane_cmd turbo 'scripts/platform-validate.sh'
# or from Mac:
scripts/platform-validate-all.sh
```

Expected on turbo: `[SUMMARY] Failed tests: 0` for ref and `-O`. macOS must stay green. If red, fix + amend before Task 5/6.

- [ ] **Step 6: Commit**

```bash
git commit -m "$(cat <<'EOF'
x86: Direct-map enter/leave and port SYS helpers on amd64

Save iorder homes across enter, spill caller-saved regs across C calls,
and fill native.tbl emitters via CALL/ARG/RET like arm64.
EOF
)"
```

---

### Task 5: Turbo green bar + docs touch for Phase A amd64

**Files:**
- Modify: `backend/regmap.h` (finalize amd64 comment)
- Modify: `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md` (mark x86-64 in progress/done)
- Modify: `BACKEND-REVIEW.md` register table row for x86-64 if stale

- [ ] **Step 1: Run full parallel validation**

```bash
scripts/platform-validate-all.sh
```

Confirm turbo + macOS green. Note other hosts (risky/pi3/…) must not regress if they were green; if x86-only commits cannot affect them, still check exit lines.

- [ ] **Step 2: Update docs to state amd64 Phase A done; i386 next**

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
docs: Record amd64 direct-map Phase A green on turbo

EOF
)"
```

---

### Task 6: i386 shrink on crazypad

**Files:**
- Modify: `backend/x86.h` / `x86.cpp` / `x86-emit.cpp` (`ptr64()` branches for `iorder`, spill, SYS args, prologue)

**Interfaces:**
- Consumes: same shared backend state
- Produces: i386 `iorder` using `ebx`/`esi`/`edi` (+ limited caller-saved); stack-based C args; no `r8`–`r15`

- [ ] **Step 1: Split `hw_int_order` (and spill set) on `ptr64()`**

```cpp
static x86reg_t hw_int_order(local_t loc, bool is64)
{
    if (is64) { /* Task 2 table */ }
    // i386: callee-saved first ebx, esi, edi (adjust if flags still need esi)
    // then eax, edx, … excluding ecx scratch
}
```

Ensure `registers()` / `HW_COUNT` usage never indexes `r8+` encodings when `!ptr64()`.

- [ ] **Step 2: i386 `hw_sys_*` pushes args; clean stack after call**

Reuse `emit_cleanup_call_args` patterns already present for `write_integer`.

- [ ] **Step 3: Validate crazypad**

```bash
pane_cmd crazypad 'scripts/platform-validate.sh'
# BUILDENV=i686 already in pane — do not export in the command
```

Expected: ref and `-O` both `Failed tests: 0`. Re-check turbo + macOS via `scripts/platform-validate-all.sh`.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
x86: Shrink direct-map allocator for i386 on crazypad

EOF
)"
```

---

### Task 7: Phase B — globals base + `pca` / invoke / frame visibility

**Files:**
- Modify: `backend/x86-emit.cpp` (`EMIT(sto)`, `EMIT(rcl)`, `EMIT(pca)`, `EMIT(ijmp)`, `EMIT(icall)`)
- Modify: `backend/x86.cpp` (`invoke`, begin/finish translation frame sizing)
- Compare: `backend/arm64-emit.cpp` / `arm64.cpp` for the behaviors to mirror

**Interfaces:**
- Consumes: green Phase A allocator
- Produces: parity items listed in the design spec §Phase B

- [ ] **Step 1: Globals**

Change `sto`/`rcl` to load a globals base (as arm64 uses `xlr8_globals`) instead of baking `&rt.globals[i]` only if that fights relocated JIT; match arm64’s approach.

- [ ] **Step 2: `pca` / indirect control flow**

Implement bytecode→native translation for `pca` / `ijmp` / `icall` using `translated_address` / branch recording like arm64 (remove the `// TODO: Translate bytecode address` stubs).

- [ ] **Step 3: `invoke` + default frame visibility**

Mirror arm64 `begin_translation` default `local_frame_size = DEFAULT_FRAME_SIZE` and invoke save/restore of allocator-relevant state.

- [ ] **Step 4: Validate turbo then crazypad (parallel fan-out)**

```bash
scripts/platform-validate-all.sh
```

Expected: macOS + turbo + crazypad all green.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
x86: Phase B parity for globals, pca, and invoke framing

EOF
)"
```

---

### Task 8: Final docs

**Files:**
- Modify: `docs/superpowers/specs/2026-08-06-x86-direct-map-allocator-design.md` (status: implemented)
- Modify: `docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md` (x86 no longer stub)
- Modify: `backend/regmap.h`, `BACKEND-REVIEW.md`

- [ ] **Step 1: Update status tables and register docs to match shipping maps**

- [ ] **Step 2: Final `scripts/platform-validate-all.sh` and paste SUMMARY lines into `BACKEND-REVIEW.md` §1.4 if that section is maintained**

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
docs: Mark x86 direct-map allocator complete for turbo and crazypad

EOF
)"
```

---

## Spec coverage (self-review)

| Spec requirement | Task |
|------------------|------|
| Extract shared core; macOS green first | Task 1 |
| Callee-saved-first amd64 map; flags reassignment allowed | Task 2 |
| C/`sys` spill by physical reg | Task 3 |
| enter/leave `iorder`, in-place call, SYS port | Task 4 |
| Turbo full suite green | Task 5 |
| i386 shrink; crazypad green | Task 6 |
| Phase B parity items | Task 7 |
| Docs / BACKEND-REVIEW | Tasks 5, 8 |
| Tmux protocol / incremental green | Global Constraints + every validate step |

## Placeholder / consistency check

- No TBD steps; SYS port references arm64 bodies explicitly.
- Shared names: `regspill`, `spilled_args`, `hw_sys_arg`, `hw_int_order`, `CALL`/`ARG`/`RET` consistent across tasks.
- Flags home left as an explicit implementation choice inside Task 2 (allowed by spec) — must be documented in `x86.h` in that same commit.
