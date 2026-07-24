# Platform enablement protocol

This document is the **mandatory** workflow for enabling or fixing XLR8 JIT
backends across machines. Follow it for every commit that touches shared code
or any backend. Do not claim success without evidence from the steps below.

## Machines

| Nickname     | Role              | Architecture | JIT backend   |
|--------------|-------------------|--------------|---------------|
| **macOS**    | Dev / source      | aarch64      | arm64         |
| **turbo**    | Linux x86-64      | amd64        | amd64         |
| **torpille** | Linux arm64       | aarch64      | arm64         |
| **risky**    | Linux RISC-V      | riscv64      | riscv64       |
| **crazypad** | Linux amd64 host, i686 via `BUILDENV=i686` | i686 | i386 |
| **turbo-arm** | Linux arm32      | armv7l       | arm           |

The machine name **risky** is a nick-name for **fedora-riscv**, so use  `ssh fedora-riscv`.


Mac is the **only** commit source. **Commit locally; never push.** Test
platforms never commit locally. They only **`git fetch mac`** (then
`git reset --hard mac/xlr8` to pick up the new local commits). No other
remotes, no `git push`, no SCP/sed on test machines.

## Golden rules

1. **One platform per commit when possible.** Do not “fix torpille” by changing
   code that also affects a working platform unless you have verified all
   affected platforms afterward.

2. **Never claim success from exit codes alone.** Check output against `.ref`
   files. A test that exits 0 but prints garbage is a failure.

3. **Never claim success from a broken grep.** The test summary must come from
   real `[SUMMARY]` lines printed by the test run, not Makefile shell
   fragments. Prefer:
   ```bash
   make tests 2>&1 | tee /tmp/xlr8-tests.log
   grep '^\[SUMMARY\].*Successful tests' /tmp/xlr8-tests.log
   grep '^\[SUMMARY\].*Failed tests' /tmp/xlr8-tests.log
   ```

4. **Do not proceed to the next commit** until every *other* platform that was
   already green stays green (or regressions are understood and accepted).

5. **Submodule drift invalidates results.** After every `git reset --hard`, run
   `git submodule update --init --recursive` before building.

## Mandatory remote sync and build

On every remote machine, from `~/Work/xlr8`:

```bash
git fetch mac && git reset --hard mac/xlr8
git submodule update --init --recursive
make
```

Use this before any individual smoke test during investigation.

## Mandatory final validation (claim success only after this)

On every remote machine:

```bash
git fetch mac && git reset --hard mac/xlr8
git submodule update --init --recursive
make tests 2>&1 | tee /tmp/xlr8-ref.log
make tests OPTLEVEL=-O 2>&1 | tee /tmp/xlr8-opt.log
```

Success criteria for each run:

- `[SUMMARY] Failed tests: 0` (or the documented expected-failure count)
- Full test count matches the current baseline (224 reference + 224 `-O` as of
  Jul 2026, unless the suite has grown)
- Spot-check known fragile tests with output verification, not just exit code

On macOS (local), same commands from the repo root:

```bash
make tests 2>&1 | tee /tmp/xlr8-mac-ref.log
make tests OPTLEVEL=-O 2>&1 | tee /tmp/xlr8-mac-opt.log
```

## Intermediate investigation (single tests)

Allowed after the sync-and-build step above:

```bash
./xlr8 tests/bytecode/float-add.xlr8          # interpreter
./xlr8 -O tests/bytecode/float-add.xlr8       # JIT — verify output content
```

For slow hosts (risky), use `timeout` generously and distinguish timeout from
SIGSEGV.

## Parallel baseline before any fix

Before changing code for one platform, run the **full mandatory validation**
on all six machines in parallel and record:

- commit hash (`git log -1 --oneline`)
- reference pass/fail counts
- `-O` pass/fail counts
- list of failing tests per machine

Only then start platform-specific work.

## Commit and deploy loop

Mac holds the branch; test machines pull from Mac over the LAN. **Do not push**
to `origin`, GitHub, or any other remote. Publishing is out of band (human
decision later). Until then, test platforms see new work only via
`git fetch mac`.

1. Implement fix on macOS (or platform-isolated `#ifdef` if required).
2. Run local mandatory validation on macOS.
3. **`git commit`** on macOS (on branch `xlr8`). Do **not** `git push`.
4. **Validate all test platforms in parallel** (never one host after another).
   From Mac, fan out into each host's existing tmux session `xlr8`:
   ```bash
   scripts/platform-validate-all.sh
   ```
   Per host the controller uses **two SSH sessions only** (none for local
   macOS): one persistent connection that arms `tmux pipe-pane` and
   `tail -F`s `/tmp/xlr8-validate-<nickname>.log` into a single local poll
   file, and one one-shot SSH that only `send-keys`s bare
   `scripts/platform-validate.sh` (default mode `both`; no `cd` — the pane
   is already in the tree).  Completion is the pane line
   `XLR8_VALIDATE_DONE exit=N`.  Do not add BatchMode/timeout SSH options;
   every extra SSH costs load on slow hosts (especially risky).
5. **If any already-green platform regresses:** fix on Mac and
   **`git commit --amend`** (same commit). Repeat **parallel** fetch + full
   validation on all test platforms until nothing regresses.
6. **If the target platform improved and every other platform is unchanged
   (no regression):** stop amending; that commit is done. Start the **next**
   commit for further work on that or another platform.
7. Never stack a new commit on top while a prior commit still regresses a
   green platform.

## Platform-isolated changes

When Mac and Linux aarch64 need different LR paths, different invoke stubs, or
different ABI handling:

- Prefer `#if defined(__APPLE__)` / `#elif defined(__linux__)` in the
  smallest possible functions.
- Document in the commit message which platforms were validated.
- A fix for torpille must not be reported as done until turbo (amd64) still
  passes mandatory validation.

## Anti-patterns (do not repeat)

- Reporting “224/224 on torpille” because Mac passed or because JIT blobs match.
- Using `grep Failed tests` without capturing real `[SUMMARY]` lines.
- `cd Work/xlr8` instead of `cd ~/Work/xlr8` on remotes.
- **`git push`** from Mac or anywhere — test platforms only **`git fetch mac`**.
- SCP/sed patches on remotes instead of fetch/reset from mac.
- Changing `arm64.cpp` while debugging arm32 or riscv “just in case”.
- Proceeding to riscv/i386 work while torpille or turbo is unknowingly red.
- Validating hosts **sequentially** (turbo, then torpille, then …). Always
  fan out in parallel; wall-clock should be ~max(host times), not the sum.

## Current priority (Jul 2026)

1. Record parallel baseline on all six machines at `mac/xlr8`.
2. Restore **torpille** (arm64 Linux JIT, especially float ops) without
   breaking **turbo** (amd64).
3. Keep **macOS** at 224/224 reference and `-O`.
4. Track **risky**, **crazypad**, **turbo-arm** / **pi3** status; do not
   regress green platforms while fixing red ones.
