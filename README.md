# XLR8 Extensible Runtime and Runtime

XLR8 is a successor to [XL][xl] intended as the foundation for a new kind of
[hardware-agnostic][taos], [actor-based][art], [agentic][agentic] [operating
system][os].

The big picture of the components we want to be able to write with XLR8 is shown
below:

![Overall OS design](OS Design.png)

[xl]: https://xlr.sf.net
[os]: https://grenouillebouillie.wordpress.com/2020/03/27/old-ideas-about-an-os-from-scratch/
[taos]: http://uruk.org/emu/Taos.html
[art]: https://fosdem.org/2024/events/attachments/fosdem-2024-3485-hactorscript-in-art-bug-free-software-on-unhackable-hardware/slides/22293/Draft2_3QvqGJE.pdf

The first step is to write the compiler and bootstrap it.


## Key ideas

The key ideas of XLR8 are:

1. A sandboxed runtime
2. Extensible bytecode
3. Actor-based operation, local or remote
4. Remote operation similar to ELFE
5. Async filesystem and networking (transparent networking)
6. Async for 3D graphics similar to Tao3D
7. Capability-based / visibility-based security


## Building and running

Build the runtime and command-line tool with `make`, then run a bytecode
program:

```sh
make
./cmd/xlr8 tests/bytecode/addition.xlr8
```

The default memory arena is 32 KiB (`-m` selects a size in kilobytes).
Programs can also be read from standard input (`xlr8 -`).


## Bytecode

XLR8 programs at the lowest level are `.xlr8` files: human-readable
bytecode assembly that the scanner turns into executable bytecode and the
runtime interprets.

The model is **register-based with extensible opcodes**. Each activation
has numbered local registers (`0`, `1`, …). Most arithmetic, logic, and I/O
operations take a destination register and/or source register as operands.
A separate value stack supports calls, returns, and explicit `push`/`pop`.

Built-in opcodes are defined in `runtime/opcodes.tbl` (currently 125 of
them). Their names are registered in order when the runtime starts, so the
first byte of an instruction is both an opcode number and a token index.
New opcodes can be defined in source with `=` and invoked with `call *name`.


### Assembly directives

| Symbol | Meaning |
|--------|---------|
| `//` … | Line comment (C++ style) |
| `/*` … `*/` | Block comment |
| `*name` | Insert a reference to a named opcode body (required for `call`) |
| `!name` | Forward-declare a user opcode defined later (needed for mutual recursion) |
| `=name` | Begin defining a new opcode; body runs until `;` |
| `;` | End an opcode definition (also ends the top-level program) |
| `.label` | Define a branch target at the current code position |
| `:label` | Insert a signed branch offset to `.label` (LEB128, relative to the offset itself) |

Built-in opcode names (`add`, `call`, `ret`, …) are written directly.
User-defined opcodes are referenced with `*` when their address is needed,
for example `call *fact`.

When one user opcode calls another that is not defined yet, pre-register the
name with `!name` before the first `call *name`. This is required for
**mutual recursion** (and other forward references across opcodes). A
single opcode that calls itself only needs `=name`, because the name is
registered at the start of its own definition.


### Instruction encoding

The scanner emits a byte stream with this layout:

1. **Opcode** — one byte, the token index of the opcode name (matches the
   `op_t` enum order in `opcodes.tbl` for built-ins).
2. **Operands** — encoding depends on the operand kind:
   - **Source and destination registers** — one byte each (indices 0–255).
     This keeps the common `op dst src` form compact and fast to decode.
   - **Variable-size operands** — [LEB128][leb128]-encoded when their size is
     not known ahead of time: branch deltas, immediate constants (`addi`,
     `nat`, …), token references (`call`, `jmp`, `txt`, …), and similar.
3. **Floating-point immediates** — an IEEE 754 `double` copied verbatim
   (8 bytes) when a real literal appears in source.

The 256-register limit applies to the byte-encoded `dst` and `src` operands.
That is enough in practice for local frames allocated with `enter`.

Branch offsets are **relative to the beginning of the offset** (the first
byte of the LEB128 field), not to the byte after it. That choice is necessary
because the offset size is not known until the target label is resolved: a
delta that initially fits in one byte may later require two or three. Keeping
the branch base at the start of the offset field means resizing an offset does
not change how other branches compute their targets. Labels can be
forward-referenced; the scanner patches offsets once the target is known, and
re-adjusts other branches when an offset grows and shifts following code.

[leb128]: https://en.wikipedia.org/wiki/LEB128


### Runtime layout

All data lives in one memory arena allocated at startup:

```
  low address                                              high address
  |<-------- heap (grow up) -------->|<--- stack (grow down) --->|
  memory                             heap                  stack/frame
```

| Component | Role |
|-----------|------|
| `memory` … `heap` | Heap for token strings and bytecode blobs |
| `stack` … `histack` | Value stack (return addresses, `push`/`pop` data) |
| `frame` | Base pointer for the current function's local registers |
| `tokens[]` | Pool of text and bytecode; indices are `tok_t` values |
| `defined` | Map from user opcode names to their bytecode entry tokens |
| `entry` | Program entry point after scanning |

`enter N` allocates `N` local registers below the current frame. Most
two-operand instructions use the form `op dst src`, meaning `local[dst] ← local[dst] op local[src]`. Immediate forms append a
LEB128 constant after the register, for example `addi 0 3` adds 3 to
register 0.


### Built-in opcodes (overview)

The full list and semantics live in `runtime/opcodes.tbl`. Summary by group:

| Group | Opcodes | Purpose |
|-------|---------|---------|
| Control | `end`, `copy`, `swap`, `jmp`, `call`, `ijmp`, `icall`, `ret`, `ppc`, `pc`, `pca`, `enter`, `leave`, `save`, `restore`, `push`, `pop`, `dispatch` | Frames, calls, register moves, indirect jumps, multi-way dispatch |
| Branches | `dbra`, `bra`, `beq`, `bne`, `blt`, `bgt`, `ble`, `bge` | Branches on a **signed** register test (`dbra` decrements and branches) |
| Integer math | `add`, `sub`, `mul`, `div`, `mod`, `neg`, `incr`, `decr`, `umul`, `udiv`, `umod` | Register arithmetic |
| Immediate math | `addi`, `subi`, `muli`, `divi`, `modi`, `addu`, `subu`, `mulu`, `divu`, `modu` | Same, with an inline constant |
| Bitwise | `and`, `or`, `xor`, `shl`, `shr`, `ushr`, `not`, `andu`, `oru`, `xoru`, `shlu`, `shru`, `ushru` | Bit operations |
| Constants | `int`, `nat`, `txt` | Load a signed, unsigned, or text constant into a register |
| Errors | `err`, `clear`, `trap` | Error reporting and traps |
| I/O | `stdin`, `stdout`, `stderr`, `eof`, `inpc`…`inpt`, `outc`…`outt`, `outl` | Stream binding and typed input/output |
| Float / double | `i2f`, `i2d`, `f2i`, `d2i`, `fadd`…`fmod`, `dadd`…`dmod`, `ffloor`…`flog2`, `dfloor`…`dlog2` | Conversions and math library |

Many arithmetic and logical opcodes come in several variants, using a prefix
when the operand type or operation matters: plain names for signed integer
(`add`, `div`, …), `u` for unsigned (`udiv`, `umul`, `umod`, …), `f` for
float (`fadd`, `fdiv`, …), and `d` for double (`dadd`, `ddiv`, …). The
same convention applies to immediate forms (`divi` vs `divu`, etc.) and
to bitwise shifts.

Branch opcodes always treat the tested register as a **signed** integer.
There are no unsigned branch variants.


### Example 1: addition and output

```xlr8
enter 2
nat 0 1
nat 1 1
add 0 1
txt 1 "1+1="
outt 1
outi 0
outl
```

Output: `1+1=2`


### Example 2: counting loop

```xlr8
enter 4
nat 0 2                         // counter
nat 1 1                         // increment
nat 2 1_000_000                 // limit

.loop
add 0 1
copy 3 0
sub 3 2                         // counter - limit
blt 3 :loop                     // continue while counter < limit

txt 3 "I="
outt 3
outi 0
outl
```

Numbers accept XL-style bases and underscores (`16#FF`, `1_000_000`).


### Example 3: extensible opcode (recursive factorial)

User-defined opcodes are bodies of bytecode stored under a name. Call them
through `call *name` so the scanner emits a token pointing at the body:

```xlr8
=fact
ble 0 :return1                  // if N <= 0, return 1
push 0                          // save N across recursive call
subu 0 1                        // N - 1
call *fact
pop 1                           // saved N into register 1
umul 0 1                        // fact(N-1) * N
ret
.return1
nat 0 1
ret
;

enter 2
nat 0 6
call *fact
txt 1 "Factorial 6 is "
outt 1
outi 0
outl
```

Output: `Factorial 6 is 720`


### Example 4: mutual recursion with `!`

When two opcodes call each other, declare both names before either
definition:

```xlr8
!foo
!bar

=foo
ble 0 :done
subu 0 1
call *bar
incr 0
ret
.done
nat 0 0
ret
;

=bar
ble 0 :done
subu 0 1
call *foo
incr 0
incr 0
ret
.done
nat 0 0
ret
;

enter 2
nat 0 5
call *foo
txt 1 "foo(5)="
outt 1
outi 0
outl
```

Output: `foo(5)=7`


## Scanning

The scanner (`runtime/scanner.cpp`) reads a `.xlr8` source stream token by
token and appends to a bytecode buffer:

1. **Names** — built-in or user opcode; emits the name's token index (one byte).
2. **Integers** — numerals in source (supports bases like `16#A_F`, `_`
   separators, optional `+` or `-` for signed values). In the bytecode,
   register indices are byte-encoded; other integer operands use LEB128.
3. **Reals** — copied as an 8-byte `double`.
4. **Text** — `'…'` or `"…"` strings (double the quote to escape); stored
   in the token pool, referenced by a LEB128 token index.
5. **Directives** — `*`, `!`, `=`, `.`, and `:` as described above.

When scanning finishes, the program blob is stored as a token and
`runtime.entry` is set to point at it. Undefined forward branch labels are
reported as errors. An `!name` declaration reserves an entry in
`runtime.defined` so `*name` can be used before `=name`. When the definition
is scanned, its bytecode is stored in that reserved slot. Nested `=opcode`
definitions use a child scanner over the same input stream; the defined body
is registered in `runtime.defined` before scanning resumes.

The main loop in `runtime.run()` fetches each opcode byte, dispatches through
the table in `opcodes.tbl`, and advances the program counter over that
instruction's operands. Errors set `runtime.error()` and stop execution.
