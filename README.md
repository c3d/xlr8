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
[leb128]: https://en.wikipedia.org/wiki/LEB128

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
./xlr8 tests/bytecode/addition.xlr8
```

The default memory arena is 32 Ki. The `-m` option selects a size in kilobytes.
Programs can also be read from standard input (`xlr8 -`).

A single input file may contain **several bytecode sections** separated by `;`.
The driver scans and runs each section in order while input remains available.
Later sections can redefine symbols or continue execution with updated register
state. See `tests/bytecode/repeated-execution.xlr8` and
`tests/bytecode/multi-section-data.xlr8`.

To run the test suite, use `make tests`. To update a test reference,
use `make ref-pattern`, where `pattern` is a part of the name of the test to
update.


## Bytecode

XLR8 programs at the lowest level are `.xlr8` files: human-readable
bytecode assembly that the scanner turns into executable bytecode and the
runtime interprets.

Below a commented example of bytecode that writes a null-terminated string:

```xlr8
[STDOUT 1]

    pca 1 :mytext                       // R1 = PC-relative address of [mytext]
.chars                                  // Symbolic label [chars]
    ld1 0 1                             // R0 = byte at address R1
    beq 0 :done                         // Branch to [done] if R0==0
    sys write_char STDOUT 0             // Write R0 to stdout
    incr 1                              // Increment R1 (next character)
    bra :chars                          // Loop back to [chars]
.mytext                                 // Label [mytext] used in pca above
    #"Hello world!" #10                 // Insert raw bytes in stream
    #"This is a C-style null-terminated string"
    #16#0A #0                           // Based-number notation and trailing 0
.done
```

# Virtual machine model

XLR8 is a virtual machine optimized for fast interpreted execution and reduced
memory usage. It is also designed for extensibility.

## Instruction format

The XLR8 virtual machine uses byte-sized opcodes. Most instructions name a
destination register and a source register (`op dst src`); others append
immediate operands, predicate numbers, branch offsets, or inline data (floats,
dispatch tables). Built-in opcodes are defined in `runtime/opcodes.tbl`, with
values below 128. The current number of reserved opcodes is shown by `xlr8 -tmain`.

XLR8 source code directly translates into bytecode through one-pass scanning.
For example, when you write `add 1 2`, this generates the byte corresponding to
`add`, followed by one byte with value `1` and one byte with value `2`.
The resulting byte sequence is interpreted as adding register 2 to register 1.

However, these are only bytes, and the scanner makes no attempt at enforcing any
kind of syntax or instruction format check. The `end` opcode has byte value `2`,
so it is equivalent to write `end` or `2`, and `add end end` will add register 1
to itself. Byte value `0` is the `null` opcode (always an error); byte value `1`
is `undef`, which expects a token operand for forward `*name` resolution.

## Registers

The virtual machine can address up to 256 "registers" at any given time.
Registers live in a [register window](#register-window) backed up by the stack,
where register 0 in the current function is distinct from register 0 in a caller
or in a callee.

Registers are normally referenced using a byte index. However, be mindful that
the scanner by default encodes numbers using [LEB-128 encoding][leb128]. This
means that any register index larger than 128 must be explicitly encoded as a
byte using the `#` prefix. For example, adding register 200 to register 0 must
be written as `add 0 #200`, since `add 0 200` would add one extra LEB128 byte.

For density and efficiency, opcodes use two registers, where the target register
is both a source and destination. By convention, the destination is the first
argument: `op dst src`. This order is purely conventional, matching the order
found in assignments in C or C++. For example, `add 3 2` matches `R3+=R2`.

*Rationale* While two-address instructions were typical for CPUs designed in the
1980s, many modern real CPUs, including most RISC designs, use three-address
encoding instead, i.e. `op t=s1,s2`. The trade-off for a virtual machine is
slightly different, and having only two register addresses accelerates execution
and improves bytecode density. The drawback is that a compiler may have to
occasionally emit additional `copy` instructions.

## Register window

Each activation has its own numbered local registers (`0`, `1`, …).
Most arithmetic, logic, and system operations take a destination register and/or
source register as operands. A separate [value stack](#stack) supports calls,
returns, and explicit `push`/`pop`.

The size of the current register window is set by the `enter` opcode, which
creates a new activation record below the current stack pointer.
This is intended to create a stack frame on entry to a function.

When the runtime starts a program, `runtime::run` executes `enter 8` before the
entry point. Top-level bytecode therefore has registers `0`…`7` available without
an explicit `enter`. Use `enter N` only when you need a larger frame or a nested
activation (for example inside a recursive function).

The reciprocal operation is `leave`, which returns to the previous stack
frame. Typically, a recursive function will begin with `enter N` where `N` is
the number of registers it uses, and finish with `leave ret`, where `leave`
returns to the previous stack frame and `ret` returns to the caller.

`enter` saves the low **8 predicate flags** (bits 0–7 of the `flags` register)
in the frame record; `leave` restores them. Predicates 8–63 are not saved across
calls—use `pushf` / `popf` on the value stack when you need to preserve the
full flag word. See `enter-leave-flags.xlr8` in the test suite.

## Calling conventions

By convention, a callee may clobber registers `0`…`7` in the caller's register
window (`CALL_CLOBBERED_REGS` in `runtime.h`). Values that must survive a call
should live in registers `8` and above, or be saved on the value stack with
`push` / `pop`.

Arguments are accessed using the `get` and `set` opcodes, which access the
caller's register window. `get 3 0` gets argument 0 and puts it in register 3,
whereas `set 2 6` puts register 6 into argument 2.

The `get` and `set` opcodes access the caller's activation record only after
`enter`.  Like for registers, 256 distinct arguments can be accessed that way.

Returning a value is typically done by writing into argument 0.

## Stack

The stack can also be accessed using `push` and `pop`. This moves the stack
pointer but not the register frame. On the other hand, `enter` will always
allocate a new register window below the current stack pointer.

## User-defined opcodes and named definitions

XLR8 distinguishes two extension mechanisms:

**Named definitions (`=foo`).** A `=name` … `;` block stores a bytecode body in the
token pool. `call *name` emits a `call` plus a token reference (LEB128) pointing
at that body. This is ordinary **function definition**, not a single-byte opcode.
See [Example 3](#example-3-function-definition-recursive-factorial).

**Extensible opcodes (128–255).** Byte values from 128 upward name a separate
opcode space reserved for **compact encoding**: one byte in the instruction stream
instead of a multi-byte `call` sequence or a long sequence of built-ins. The
runtime dispatches these to handlers registered at startup (the sample driver uses
`-d N` to bind opcode `N` to a native function). Emit them in source with `#N`,
for example `#199` for opcode byte 199.

Unused values below 128 can also be bound the same way, but any built-in opcode
in `opcodes.tbl` takes precedence over a user handler at the same byte value.
See [Example 6](#example-6-extensible-opcode-debug-print).

# Source format

XLR8 programs can be written using a standard source code format.

## Tokens

The text format is a sequence of [LEB128-encoded][leb128] tokens:

* Names, e.g. `sys`, encoded as a natural number for the corresponding token.
* Text between single or double quotes, e.g. `"Hello"` or `'World'`, encoded as
  a natural number for the corresponding token.
* Numbers, e.g. `42` or `-25`. Numbers beginning with `+` or `-` are encoded as
  signed values, otherwise as natural (unsigned) values.
* Floating-pointer numbers such as `1.23` or `3.25F`. If the `F` or `f` suffix
  is used, the value is stored as 32-bits IEEE-754, 64-bit otherwise.

Names and text are represented by token value that uniquely identify them.
The token value for opcodes is the byte encoding for that opcode. For example
`null` has token value `0`, `undef` has token value `1`, and `end` has token
value `2`.

## Directives

* `=foo` defines a bytecode sequence—a **function body** stored as a token.
  The sequence ends with the next `;`. Bodies can be nested. Invoke with
  `call *foo`, not by writing `foo` as an opcode byte.
* `?foo` reserves a name as an external reference. This is useful for mutually
  recursive calls.
* `!foo` sets the value associated to symbol foo, and is used in conjuction with
  the `*foo` notation that inserts the associated value.
* `.foo` defines `foo` as a label for the current position.
* `:foo` inserts a signed branch offset from the current PC to label `foo`.
* `*foo` inserts the value of symbol `foo`. Note that this is typically needed
  for the `call` instruction, but `nat 0 *foo` will set register `0`
* `[foo …]` defines a **macro** that expands at scan time (see [Macros](#macros)).
  Invoke with `foo` followed by one argument per formal parameter.
* `{foo …}` defines a **bytecode function** evaluated at scan time (see
  [Bytecode definitions](#bytecode-definitions)). Invoke with `foo` followed by
  one argument per formal parameter; the body runs in a mini-runtime and its
  output bytes are spliced into the enclosing program.

## Comments

Comments follow the C/C++ conventions.

* Single-line comments begin with `//` and extend to the end of the line.
* Multi-line comments begin with `/*` and end with `*/`.

## User-defined constants

Setting a constant with `!FOO` enters its value in the symbol table.
Using `*FOO` then inserts the _value_ of the constant.

For example, after`!FOO 24`, any occurence of `*FOO` is the same as `24`.
A constant can be inserted before it is known. This is notably useful for calls,
but can also be used to name registers, etc.

```xlr8
!INDEX          0
!LOOPS          42
nat *INDEX *LOOPS
```

## Macros

The scanner supports **macros**: named templates that expand into bytecode at
scan time. A macro definition is wrapped in square brackets and starts with the
macro name:

```xlr8
[NAME body tokens until closing bracket]
```

Formal parameters, if any, appear immediately after the name in their own
bracket list:

```xlr8
[NAME [A B C] body using A B C]
```

Invoke a macro by writing its name followed by one argument for each parameter.
During expansion each parameter name behaves as a temporary macro bound to the
corresponding argument. The macro table is saved and restored around each
expansion, so parameter bindings and any macros defined inside the body do not
leak into the surrounding program.

Example (from `tests/scanner/macros.xlr8`):

```xlr8
[tout[t]
 [TMP    0]
 [STDOUT 1]

 txt TMP t
 sys write_text TMP STDOUT
 sys write_newline  STDOUT
]

tout "Hello"
tout "World"
```

This defines `tout` with parameter `t`. The body also defines local macros
`TMP` and `STDOUT` (registers `0` and `1`) that exist only while `tout`
expands. The two invocations at the bottom emit the same bytecode as if the body
had been written out twice with `t` replaced by `"Hello"` and `"World"`.

A macro argument is normally a single token (a name, numeral, or quoted text).
To pass several tokens as one substitution, wrap the argument in brackets:

```xlr8
[cmpge[r a b] cmple r b a]
```

When called as `cmpge 0 1 2`, this expands to `cmple 0 2 1` — an `>=` test
implemented by swapping the last two operands of `cmple`.

Macro definitions may nest other macro definitions in their body. Those inner
definitions are stored as part of the outer macro's text and take effect only
when the outer macro is expanded.

## Bytecode definitions

Curly-brace definitions `{name …}` look like macros but behave differently:
the body is **executed** during scanning, not textually substituted. Arguments
are passed as typed values (natural, integer, float, double, or text). The
body may use normal opcodes; output written to stream `0` during execution is
appended to the enclosing bytecode buffer.

```xlr8
{fact [n]                 // factorial as a scan-time bytecode generator
 enter 2
 nat 0 1
 get 1 n
 beq 1 :done
 .loop
   mul 0 1
   dbra 1 :loop
 .done
 sys nat 0 0              // emit the result as a natural constant
 leave
 end}

nat 0 fact 6               // expands to nat 0 720 at scan time
```

Use `sys nat`, `sys int`, `sys flt`, or `sys dbl` inside the body to append
typed constants to the output stream. See `tests/scanner/bytecode-insertion.xlr8`.

| Mechanism         | When      | Effect                                      |
|-------------------|-----------|---------------------------------------------|
| `[name …]` macro  | Scan time | Textual substitution into bytecode          |
| `{name …}` bytecode | Scan time | Execute body; splice stream-0 output into bytecode |
| `!name` / `*name` | Scan time | Symbolic constants and forward references   |
| `=name` … `;`     | Scan time | Named function stored as a token; invoked at runtime with `call *name` |

## Compile-time expressions

Parenthesized expressions are evaluated at scan time and their result is
inserted into the bytecode (via `append`, or as a single byte after `#`).

```xlr8
nat 0 (1+2*3)                   // register 0 ← 7
txt 1 ("ABC"+"DEF")             // text token for ABCDEF
nat 2 (add+1)                   // token value of add, plus 1
addi 0 #(1+2)                   // emit byte 3 as immediate operand
```

Supported operators are arithmetic `+` `-` `*` `/` `%`, bitwise logical `&` `|`
`^` and unary `-`. They follow usual precedence: multiplicative before additive
before `&` before `^` before `|`.

String literals combine with `+`.

A string followed by `%` and a value applies **printf-style formatting** at
scan time (one `%` conversion per `%` operator, left to right). Use `%%` for a
literal percent sign. Supported conversions include `%d`, `%u`, `%x`, `%s`,
`%f`, `%g`, and related variants. See
`tests/scanner/printf-style-formatting-in-expression.xlr8`.

```xlr8
txt 0 ("value=%08X" % 33)       // text token for "value=00000021"
```

Names expand macros when applicable; otherwise the token index of the name is
used as a numeric value.

Relational operators and shifts are not implemented yet.

Macro bodies and arguments can use expressions, for example
`[inc[r] nat 0 (r+1)]`.

See `tests/scanner/expressions.xlr8`.


## Why the `*` in `call *foo`?

A common use case for `*` is when calling a function:

```xlr8
=foo
    decr 0
    bne 0 :exit
    call *foo
    .exit
```

The reason for having to write `*foo` and not `foo` is that `foo` is the token
for `foo`. Internally, the token value for `foo` is `[3][f][o][o]`. Unless the
intent is to execute whatever bytecode that represents, what we really want is
the bytecode value _defined_ for `foo`.

Note that the mechanism is general. For example, you can load the address of the
code to execute using something like `nat 0 *foo`. This is illustrated by the
`reading-my-own-code.xlr8` test case, which uses this technique to get a
sequence of bytes directly from the bytecode:

```xlr8
// Load a token constant and display it
nat 0 *ARRAY
sys tok 1 0

=ARRAY 1 2 3 42 #"XYZ" 0;
```

## Branches

Branches are PC-relative. Offsets are relative to the **beginning** of the
offset (the first byte of the LEB128 field), not to the byte after it.
That choice is necessary because the offset size is not fixed. A branch offset
that reaches -64/+63 bytes fits in a byte; larger displacements require more
bytes.

Register branches (`beq`, `blt`, …) test a **signed** register value.
`dbra` decrements an unsigned register and branches while non-zero. Predicate
branches use `btrue pred :label` or `bfalse pred :label` (see [64 flags](#64-flags-and-conditional-execution)).
Unconditional branches use `bra :label`. Token jumps use `jmp *name`.


## Syntax summary

| Syntax        | Meaning                                                      |
|---------------|--------------------------------------------------------------|
| `SomeName`    | Insert the token value, including for opcodes like `add`     |
| `"Some Text"` | Insert the token value for the text                          |
| `123`         | Insert the natural value in unsigned form                    |
| `+123`,`-5`   | Insert an integer value in signed form                       |
| `1.23F`       | Insert a 32-bit IEEE-754 floating point value                |
| `1.23`, `1.D` | Insert a 64-bit IEEE-754 floating point value                |
| `//` …        | Line comment (C++ style)                                     |
| `/*` … `*/`   | Block comment                                                |
| `*name`       | Insert a token reference (required for `call`, `jmp`)        |
| `?name`       | Reserve an external reference for forward `*name` resolution |
| `!name`       | Define a symbolic constant (`*name` inserts its value)       |
| `=name`       | Begin defining a new function; the body runs until `;`       |
| `;`           | End an opcode definition (also ends the top-level program)   |
| `.label`      | Define a branch target at the current code position          |
| `:label`      | Insert a signed branch offset to `.label` [1]                |
| `#num`        | Insert a byte value directly, e.g. `#192` for register 192   |
| `#text`       | Insert a byte sequence directly, e.g. `#"Hello"              |
| `[name …]`    | Define a scan-time macro; invoke as `name` with arguments    |
| `{name …}`    | Define a scan-time bytecode function; invoke like a macro     |
| `(expr)`      | Evaluate a compile-time expression; insert its value         |
| `#(expr)`     | Evaluate an expression and insert a single byte (0–255)      |

[1] signed branches are relative to the offset itself.

Built-in opcode names (`add`, `call`, `ret`, …) are written directly.
User-defined constants are referenced with `*` when their address is needed,
for example `call *fact`.

When one user opcode calls another that is not defined yet, pre-register the
name with `?name` before the first `call *name`. This is required for
mutual recursion and other forward references across functions.

A single opcode that calls itself only needs `=name`, because the name is
registered at the start of its own definition.

# Examples

### Integer constants

`int` loads a signed integer constant.
`nat` loads a natural (unsigned) constant.
Both take a destination register and a numeral.
The scanner appends the value as [LEB128][leb128] after the register byte.

```xlr8
int 0 -42
nat 1 1_000_000
```

Numerals accept XL-style bases and underscores (`16#FF`, `1_000_000`).
A leading `+` or `-` selects the signed path used with `int`.
Plain numerals are treated as naturals and pair with `nat`.

**Ambiguous numerals.** Token values for small integers coincide with built-in
opcode names (`0` is `null`, `1` is `undef`, `2` is `int`, `10` is `select`,
`25` is `sys`, …). When a numeral is written as a freestanding token in the
bytecode stream, the scanner emits the opcode byte, not the numeric value. Use a
leading `+` or `-` to force a signed LEB128 constant (`int 0 +100`), or `#` for
a raw byte (`#2`). This matters for `dispatch` case values, stream indices, and
any operand that is not immediately after `int` or `nat`.


### Floating-point constants

`flt` and `dbl` inserts 32-bit and 64-bit floating-point constants.
There is no LEB128 encoding in that case, the value is copied literally.

The literal may appear on the same line or on the following line:

```xlr8
flt 0 2.5f
dbl 1 3.0
```

The scanner chooses the embedded size from the literal suffix:

| Literal suffix | Embedded size   | Typical opcode |
|----------------|-----------------|----------------|
| `f`            | 32-bit `float`  | `flt`          |
| `d`            | 64-bit `double` | `dbl`          |
| *(none)*       | 64-bit `double` | `dbl`          |

Use the matching arithmetic and `sys` opcodes: `fadd` / `sys ffloor` for floats,
`dadd` / `sys dfloor` for doubles.

Type conversions and math library routines are **`sys` natives** (`sys i2f`,
`sys u2d`, `sys f2d`, `sys f2i`, `sys fsqrt`, …). Built-in `fadd`…`fmod` and
`dadd`…`dmod` opcodes cover in-register arithmetic only.

Real literals accept decimal and based syntax (`16#1.A#f`, `1.5e2f`, …).
See [Example 2](#example-2-counting-loop) for based-number rules, including
exponents in the mantissa base and the `#f` suffix when `f` would otherwise
be a valid digit in the base (`16#1.FF#F`).

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
| `globals` | Global variable slots (`sto` / `rcl`; sized with `sys globals`) |
| `entry` | Program entry point after scanning |

`enter N` allocates `N` local registers below the current frame. The runtime
opens the initial frame with `enter 8` before running the program entry point.
Most two-operand instructions use the form `op dst src`, meaning `local[dst] ← local[dst] op local[src]`. Immediate forms append a
LEB128 constant after the register, for example `addi 0 3` adds 3 to
register 0.


### Built-in opcodes (overview)

There are **120 built-in opcodes** (byte values `0`…`119`). The count is shown
at startup with `xlr8 -tmain`. Definitions and semantics are in
`runtime/opcodes.tbl`. Most instructions use `op dst src`; immediate forms
append a LEB128 constant; several groups use additional operands as described
below.

| Group | Opcodes | Purpose |
|-------|---------|---------|
| Meta | `null`, `undef`, `end` | `null` traps zero-filled regions; `undef` resolves forward `*name` references; `end` stops execution |
| Constants | `int`, `nat`, `flt`, `dbl`, `txt` | Load a signed integer, natural, float, double, or text token into a register |
| Data movement | `copy`, `swap`, `ccopy`, `select` | Register copy; `ccopy`/`select` are predicate-guarded (see below) |
| Control flow | `jmp`, `call`, `ijmp`, `icall`, `ret`, `ppc`, `pc`, `pca`, `enter`, `leave`, `get`, `set`, `push`, `pop`, `switch`, `dispatch` | Calls, returns, PC access, frames, argument access, multi-way dispatch |
| Globals | `sto`, `rcl` | Store to / recall from the global array (resize with `sys globals`) |
| System | `sys` | Invoke a native operation (see [Native operations](#native-operations-sys)) |
| Branches | `dbra`, `bra`, `btrue`, `bfalse`, `beq`, `bne`, `blt`, `bgt`, `ble`, `bge` | PC-relative branches; `btrue`/`bfalse` test a predicate; others test a **signed** register |
| Predicates | `true`, `false`, `cmpeq`, `cmpne`, `cmplt`, `cmple`, `cmpule`, `cmpult`, `cmpeqi`, `cmpnei`, `cmplti`, `cmplei`, `cmpequ`, `cmpneu`, `cmpltu`, `cmpleu`, `pushf`, `popf` | 64 condition flags (see below); for `>`, `>=`, swap register operands; `*i`/`*u` suffixes are signed/unsigned **immediate** compares |
| Integer math | `add`, `sub`, `mul`, `div`, `mod`, `neg`, `incr`, `decr`, `umul`, `udiv`, `umod` | Two-address register arithmetic |
| Immediate math | `addi`, `subi`, `muli`, `divi`, `modi`, `addu`, `subu`, `mulu`, `divu`, `modu` | Same, with an inline LEB128 constant |
| Bitwise | `and`, `or`, `xor`, `shl`, `shr`, `ushr`, `not`, `andu`, `oru`, `xoru`, `shlu`, `shru`, `ushru` | Bit operations; `shru`/`ushru` use an inline shift count |
| Addressing | `frame` | `frame dst src` stores the address of register `src` in `dst` |
| Memory | `ld8`…`ld1`, `st8`…`st1`, `ld8o`…`ld1o`, `st8o`…`st1o` | Load/store through an address in a register; `*o` forms add a signed LEB128 byte offset (`ld1o dst ptr +0`) |
| Float / double | `fadd`, `fsub`, `fmul`, `fdiv`, `fmod`, `dadd`, `dsub`, `dmul`, `ddiv`, `dmod` | In-register IEEE-754 arithmetic |

**`null` vs `undef`.** Uninitialized heap and token storage is zero-filled. If
the program counter ever fetches byte `0`, the VM executes `null` and stops with
an error (`Null opcode, probably a bug`). That catches stray execution into
padding or truncated bytecode. Forward references (`?foo`, `call *foo` before
`=foo`) are encoded as an `undef` instruction (byte `1`) followed by the callee
token; at run time `undef` patches the call site and continues into the real
body. So `0` means “invalid / unset”, while `1` means “defined later, resolve
now”.

Many arithmetic and logical opcodes come in several variants: plain names for
signed integer (`add`, `div`, …), `u` for unsigned (`udiv`, `umul`, …), `f` for
float (`fadd`, …), and `d` for double (`dadd`, …). Immediate forms follow the
same convention (`divi` vs `divu`, etc.).

Branch opcodes (`beq` … `bge`, `dbra`) treat the tested register as a **signed**
integer. There are no unsigned branch variants; use predicate compares (`cmpult`,
…) with `btrue` / `bfalse` instead.

**Jump forms.** `bra :label` and conditional branches use `:label` PC-relative
offsets. `jmp *name` and `call *name` jump to the bytecode bound to token
`*name`. `ijmp` / `icall` take an address from a register (often loaded with
`pca` or `sys tok`).

**Dispatch.** `dispatch reg N v1 :lab1 v2 :lab2 …` compares the signed value in
`reg` against each threshold `v1`, `v2`, … (use `+v` or `#v` when `v` would
name an opcode). The branch to the **last** label whose threshold is `<=` the
value is taken; if none match, execution continues after the table.

`switch reg N v1 :lab1 v2 :lab2 …` uses the same table layout but branches to
the **last** label whose threshold **equals** the value in `reg`.

**Memory addressing.** `ld1 dst ptr` loads one byte from `*ptr` into `dst`
(zero-extended). `ld1o dst ptr +off` adds a signed byte offset before
dereferencing. Stores mirror this (`st1`, `st1o`, …). For signed narrow values,
use `sys ld1s` or `sys ld1os`; for arbitrary bit widths in a register, use
`sys sext dst +width` or `sys zext dst +width`.


### 64 flags and conditional execution

XLR8 maintains **64 condition flags** in a `flags` register (one bit each, numbered
0…63). Compare opcodes set or clear a selected bit; only explicit **guarded**
opcodes (`ccopy`, `select`, `btrue`, `bfalse`, and the float compares via `sys`) consult
that bit. Ordinary instructions such as `add` or `copy` always execute—unlike
architectures where every instruction is predicated.

| Opcode | Operands | Effect |
|--------|----------|--------|
| `true` / `false` | `pred` | Set or clear predicate bit `pred` |
| `cmpeq`, `cmpne`, `cmplt`, `cmple` | `pred dst src` | Signed compare `dst` vs `src`; update `pred` |
| `cmpule`, `cmpult` | `pred dst src` | Unsigned compare `dst` vs `src`; update `pred` |
| `cmpeqi`, `cmpnei`, `cmplti`, `cmplei` | `pred dst imm` | Signed compare `dst` vs inline LEB128 immediate (`+n` / `-n`) |
| `cmpequ`, `cmpneu`, `cmpltu`, `cmpleu` | `pred dst imm` | Unsigned compare `dst` vs inline natural immediate |
| `ccopy` | `pred dst src` | `dst ← src` if `pred` is set |
| `select` | `pred dst src xtra` | `dst ← src` if `pred` set, else `dst ← xtra` |
| `btrue` / `bfalse` | `pred :label` | Branch if `pred` is set / clear |
| `pushf` / `popf` | *(none)* | Save / restore the entire `flags` word on the value stack |

For relations not listed (`>`, `>=`), swap register operands (`cmplt b a` for
`a > b`, etc.). Immediate forms compare a register to an inline constant:
`cmpeqi pred dst +10`, `cmpltu pred dst 5`.

Example: **registers 0 and 1 are equal** — pick a result in register 2 (`+1` if
equal, `+0` otherwise). Several idioms work; predicates add `cmpeq`/`btrue`/`select`
/`ccopy`, while classic signed branches can use `sub` + `beq` without touching
`flags`:

| Approach | Instructions | Notes |
|----------|--------------|-------|
| **`cmpeq` + `btrue`** | `cmpeq pred R0 R1` then `btrue pred :equal` | Sets predicate bit `pred`, then branches. Compare and branch are separate; other guarded ops can reuse the same predicate. |
| **`sub` + `beq`** | `copy tmp R0` · `sub tmp R1` · `beq tmp :equal` | No predicates. `tmp` is zero iff `R0 == R1` (signed). Destroys `tmp`. |
| **`cmpeq` + `select`** | `cmpeq pred R0 R1` · `select pred dst if_true if_false` | Writes one of two source registers into `dst` based on equality; no branch. |
| **`cmpeq` + `ccopy`** | `cmpeq pred R0 R1` · `ccopy pred dst value` | Copies `value` into `dst` only when equal; `dst` unchanged otherwise (initialize `dst` first). |

Using **`cmpeq` + `btrue`** (output `1` when equal):

```xlr8
int 0 +10
int 1 +10
cmpeq 0 0 1
btrue 0 :equal
int 2 +0
bra :done
.equal
int 2 +1
.done
[STDOUT 1]
sys write_integer STDOUT 2
sys write_newline STDOUT
```

Using **`sub` + `beq`** (same result, no predicates):

```xlr8
int 0 +10
int 1 +10
copy 2 0
sub 2 1
beq 2 :equal
int 3 +0
bra :done
.equal
int 3 +1
.done
[STDOUT 1]
sys write_integer STDOUT 3
sys write_newline STDOUT
```

Using **`cmpeq` + `select`** (no branch; register 2 holds the outcome):

```xlr8
int 0 +10
int 1 +10
nat 3 +1
nat 4 +0
cmpeq 0 0 1
select 0 2 3 4
[STDOUT 1]
sys write_integer STDOUT 2
sys write_newline STDOUT
```

Using **`cmpeq` + `ccopy`** (`ccopy` overwrites register 2 only when equal):

```xlr8
int 0 +10
int 1 +10
int 2 +0
nat 3 +1
cmpeq 0 0 1
ccopy 0 2 3
[STDOUT 1]
sys write_integer STDOUT 2
sys write_newline STDOUT
```

Floating-point compares (`fcmpeq`, `dcmplt`, …) are **`sys` natives** with the
same `pred dst src` operand pattern.


### Native operations (`sys`)

Host services are invoked with `sys name operands…`. Names and implementations
live in `runtime/native.tbl`. Operand order matches each native's definition
(often `dst src`, sometimes `stream value` for `write_*`, sometimes
`:branch stream`, etc.).

Stream indices are small integers: `0` is the program input, `1` standard output, `2` standard error. Examples use a `[STDOUT 1]` macro for readability.
All `write_*` natives take the **stream first**, then the value to write.

| Group | Names |
|-------|-------|
| Heap / globals | `globals`, `malloc`, `kmalloc`, `realloc`, `free`, `alloca`, `valloca` |
| Process environment | `cmdline`, `env` |
| Parsing / precedence | `infix`, `prefix`, `postfix`, `block`, `builtin`, `parsing` |
| Input | `read_char`, `read_integer`, `read_natural`, `read_float`, `read_double`, `read_scanner`, `branch_on_eof`, `branch_on_bad` |
| Output | `write_char`, `write_integer`, `write_natural`, `write_float`, `write_double`, `write_text`, `write_newline` |
| Errors | `print_error`, `clear_error`, `trap_error` |
| Conversions | `i2f`, `i2d`, `u2f`, `u2d`, `f2i`, `d2i`, `f2u`, `d2u`, `f2d`, `d2f` |
| Float stack | `fpush`, `fpop`, `dpush`, `dpop` |
| Math library | `ffloor`, `fceil`, `fsqrt`, `fcbrt`, `fsin`, `fcos`, `ftan`, `fasin`, `facos`, `fatan`, `fexp`, `flog`, `fexp2`, `flog2`, and the `d…` counterparts |
| Float compares | `fcmpeq`, `fcmpne`, `fcmplt`, `fcmpgt`, `fcmple`, `fcmpge`, and the `d…` counterparts |
| Stack | `stack` — address of the value stack pointer |
| Memory (volatile) | `ld1v`…`ld8v`, `st1v`…`st8v` — volatile load/store through a register address |
| Memory (sign-extending) | `ld1s`…`ld8s`, `ld1vs`…`ld8vs`, `ld1os`…`ld8os` — sign-extending loads (plain, volatile, and with signed offset) |
| Extension | `sext`, `zext` — sign- or zero-extend a register to `kshift` bits (`sys sext dst +8`, `sys zext dst +32`) |
| Bytecode / LEB128 | `tok`, `leb2i`, `leb2u`, `i2leb`, `u2leb`, `i2lebsz`, `u2lebsz`, `nat`, `int`, `flt`, `dbl` |

`malloc` and `realloc` read the size from a **register** (`sys malloc 0 1` after
`nat 1 16`). `kmalloc` reads the size inline, like `globals` (`sys kmalloc 0 +16`).
Bare `+16` after `malloc` is a register index, not a byte count.

`sys alloca dst size` allocates `size` bytes on the stack frame (static within
the current activation). `sys valloca dst src` allocates a dynamic size taken
from register `src`. See `tests/bytecode/sys-alloca.xlr8`.

The `infix`, `prefix`, `postfix`, and `block` natives register operator
precedence in the scanner for high-level parsing mode (`sys parsing` /
`sys builtin`). See `tests/bootstrap/infix-prefix-postfix-block.xlr8`.

The `nat`, `int`, `flt`, and `dbl` natives append a typed constant to the
scanner output stream; they are intended for use inside `{…}` bytecode
definitions.

Built-in `ld1`…`ld8` / `st1`…`st8` zero-extend loaded values into the register.
Use `sys ld1s` (or `ld1os` with an offset) when you need a signed narrow load.
Floating-point values in memory are accessed with pointer arithmetic and
`memcpy`-style patterns, or by keeping them in registers after `flt` / `dbl`.

Typical output idiom:

```xlr8
[STDOUT 1]

txt 1 "answer="
sys write_text STDOUT 1
sys write_integer STDOUT 0
sys write_newline STDOUT
```

When a program is loaded from a file, stream `0` is that file (exhausted after
scanning), which is why `branch_on_eof` fires at runtime and `read_*` see EOF
unless input is supplied differently (for example `xlr8 -` with the program and
data on standard input).


### Example 1: addition and output

```xlr8
[STDOUT 1]

nat 0 1
nat 1 1
add 0 1
txt 1 "1+1="
sys write_text STDOUT 1
sys write_integer STDOUT 0
sys write_newline STDOUT
```

Output: `1+1=2`


### Example 2: counting loop

Use `dbra` for a compact counted loop: it decrements an **unsigned** register
and branches while the result is non-zero (after the decrement).

```xlr8
[STDOUT 1]

nat 0 2                         // counter
nat 1 999_998                   // iterations until counter reaches limit

.loop
addu 0 1                        // counter++
dbra 1 :loop                    // --remaining; branch if non-zero

txt 2 "I="
sys write_text STDOUT 2
sys write_integer STDOUT 0
sys write_newline STDOUT
```

The limit `1_000_000` is reached after `999_998` increments from the starting
value `2`. For a limit test instead of a down-counter, use `blt` / `bge` on a
signed difference (see [Branches](#branches)).

Numbers accept XL-style bases and underscores (`16#FF`, `1_000_000`).

**Based reals** (`16#…`) follow the same rules as numerals for integers: the mantissa is
read in the given base, and a second `#` starts an exponent in that same
base. For example `16#1.0#E3` is (1.0)₁₆ × 16³ = 4096, and
`16#1.FFFF_FFFF#E3` ≈ 2 × 16³ = 8192. Decimal exponent notation uses `e`
or `E` instead (`1.5e2`).

When the base is hexadecimal, `f` and `F` are valid mantissa digits (15).
To request a 32-bit float in that case, use the `#f` suffix form, e.g.
`16#1.A#f` (not `16#1.Af`, where the final `f` is the digit).


### Example 3: function definition (recursive factorial)

A `=name` block is a named function: bytecode stored under a token, invoked
with `call *name`:

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

nat 0 6
call *fact
[STDOUT 1]
txt 1 "Factorial 6 is "
sys write_text STDOUT 1
sys write_integer STDOUT 0
sys write_newline STDOUT
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

nat 0 5
call *foo
[STDOUT 1]
txt 1 "foo(5)="
sys write_text STDOUT 1
sys write_integer STDOUT 0
sys write_newline STDOUT
```

Output: `foo(5)=7`


### Example 5: floating-point constants

```xlr8
[STDOUT 1]

flt 0 2.5f
flt 1 3.0f
fadd 0 1
txt 1 "2.5f+3.0f="
sys write_text STDOUT 1
sys write_float STDOUT 0
sys write_newline STDOUT
```

Output: `2.5f+3.0f=5.5`


### Example 6: extensible opcode (debug print)

Register a handler for opcode 199 when starting the runtime (`xlr8 -d 199`).
The program can then emit that single byte—denser than spelling out the same
logic with built-in opcodes:

```xlr8
[STDOUT 1]

nat 0 32
.loop
  sys write_integer STDOUT 0
  #199 #": Hello, I just executed a user-defined opcode" #16#0A #0
  dbra 0 :loop
```

Run as `xlr8 -d 199 program.xlr8`. The `#199` byte is opcode 199; the following
`#"…"` bytes are operands consumed by that handler. Values **128–255** are the
primary extensible range; the same mechanism works for unused bytes below 128
if no built-in opcode occupies them.


## Scanning

The scanner (`runtime/scanner.cpp`) reads a `.xlr8` source stream token by
token and appends to a bytecode buffer:

1. **Names** — built-in or user opcode; emits the name's token index (one byte).
2. **Numerals** — integer literals in source (bases like `16#A_F`, `_`
   separators). With a leading `+` or `-`, the value is signed and used with
   **`int`**; otherwise it is a natural and used with **`nat`**. Register
   indices in other instructions are byte-encoded; constant operands use LEB128.
3. **Reals** — floating-point literals. The preceding opcode must be `flt` or
   `dbl`; the scanner embeds a 4- or 8-byte IEEE 754 value (see
   [Floating-point constants](#floating-point-constants)). Suffix `f` selects
   `float`, suffix `d` or no suffix selects `double`.
4. **Text** — `'…'` or `"…"` strings (double the quote to escape); stored
   in the token pool, referenced by a LEB128 token index.
5. **Directives** — `*`, `?`, `!`, `=`, `.`, `:`, `[` … `]` macros, and
   `{` … `}` bytecode definitions as described above.
6. **Macros** — when a name matches a defined macro, the scanner binds arguments
   to parameters and re-scans the macro body into the bytecode buffer.
7. **Bytecode functions** — when a name matches a `{…}` definition, the
   scanner evaluates the body in a nested runtime and splices its output.

When scanning finishes, the program blob is stored as a token and
`runtime.entry` is set to point at it. The command-line driver may then scan
and run additional `;`-terminated sections from the same input. Undefined
forward branch labels are reported as errors. A `?name` declaration or a `call *name` before `=name`
inserts an `undef` placeholder (opcode byte `1`) in the bytecode stream; the
runtime resolves it on first execution. An `!name` declaration reserves an entry
in `runtime.defined` so `*name` can be used before `=name`. When the definition
is scanned, its bytecode is stored in that reserved slot. Nested `=opcode`
definitions use a child scanner over the same input stream; the defined body
is registered in `runtime.defined` before scanning resumes.

The main loop in `runtime.run()` fetches each opcode byte, dispatches through
the table in `opcodes.tbl`, and advances the program counter over that
instruction's operands. Errors set `runtime.error()` and stop execution.
