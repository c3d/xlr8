# XLR8 bootstrap: XL compiler plan

This directory bootstraps a minimal XL implementation on top of the XLR8
virtual machine. The language definition is in the XL handbook
([`HANDBOOK.adoc`](../../xlr/docs/HANDBOOK.adoc) in the `xlr` tree).

The goal is the **simplest implementation that can grow into a real compiler**,
not a feature-complete XL clone on day one. The code must stay simple enough to
compile with a separate C++→XLR8 toolchain under development: favour plain
structs, explicit memory management, and algorithms over C++ metaprogramming.

## Design constraints

| Constraint | Rationale |
|------------|-----------|
| **Minimal C++** | No templates (except maybe a future `vector<T>` once the compiler supports it), no exceptions, no RTTI, no lambdas, no `std::iostream`, no smart pointers, no virtual inheritance. Plain `struct`, `enum`, `switch`, function pointers, and `for`/`while` loops. |
| **I/O via `native.tbl` only** | All input/output goes through `sys read_*`, `sys write_*`, `sys read_scanner`, `sys branch_on_eof`, and `sys print_error`. No `FILE*`, no C++ streams. |
| **XLR8 memory model** | One arena: heap grows up, stack grows down. Use `sys kmalloc` / `realloc` / `free` for long-lived data (parse trees, symbol tables). Use `sys alloca` / `valloca` for per-frame scratch. Use `sto` / `rcl` globals for VM-visible state. Prefer bump-pointer arenas over per-node `free` during parsing. |
| **Homoiconic parse tree first** | Per the handbook, XL programs are trees of leaf nodes (`natural`, `real`, `text`, `symbol`) and inner nodes (`infix`, `prefix`, `postfix`, `block`). Build and interpret this normal form before worrying about machine representations or optimizations. |
| **Three execution phases** | Parsing → declaration → evaluation (handbook §Evaluation). Bootstrap milestone 1 stops after parsing; milestone 2 adds a tiny declaration+eval loop. |
| **Syntax is data** | Operator spellings and precedences come from `sys infix` / `prefix` / `postfix` / `block` and `sys parsing`, not hard-coded keywords. |

Reference implementations exist in the `xlr` tree (`src/scanner.cpp`, `src/parser.cpp`,
`include/tree.h`) but they use GC, C++ strings, and iostreams. Treat them as
**behavioural references**, not as code to port literally.

## Reference parser (`~/Work/xlr`)

The XL reference parser is deliberately small (~700 lines in
[`xlr/src/parser.cpp`](../../xlr/src/parser.cpp); design notes in
[`xlr/include/parser.h`](../../xlr/include/parser.h)). Understanding its shape
is the blueprint for the bootstrap parser.

### Core algorithm

`Parser::Parse(closing, opening, opening_pos)` is a single recursive function
with a **precedence stack**, not a table-driven yacc grammar:

```
                    ┌─────────────────────────────────────┐
  tokens ──────────►│  NextToken()  (skip comments)       │
                    └──────────────┬──────────────────────┘
                                   ▼
                    ┌─────────────────────────────────────┐
                    │  switch(token): build `right` leaf  │
                    │  or detect infix / open block       │
                    └──────────────┬──────────────────────┘
                                   ▼
                    ┌─────────────────────────────────────┐
                    │  Pending stack: (op, left, prio)    │
                    │  reduce while new_prio ≤ stack top  │
                    │  odd priorities → right-associative │
                    └──────────────┬──────────────────────┘
                                   ▼
                              Tree *result
```

**`Pending` entries** (`parser.cpp` lines 71–82) hold `(opcode, argument,
priority, position)`. The stack defers infix reductions until a lower-precedence
operator forces a pop — classic operator-precedence parsing.

**Name disambiguation** is the subtle part (handbook + `parser.h` comment block).
When a name appears where both prefix and infix are possible, the parser uses:

1. Precedence tables from `Syntax` (`InfixPriority`, `PrefixPriority`,
   `PostfixPriority`).
2. **Spacing**: infix is favoured when `hadSpaceBefore || !hadSpaceAfter` and
   the name has infix precedence.
3. **Context**: first token of a statement vs middle of expression; block
   delimiters recurse with `Parse(close, open, pos)`.

**Special cases worth porting verbatim:**

| Case | Behaviour |
|------|-----------|
| `Write -A, B` | Known quirk: parses as `(Write -A), B` not `Write (-A), B` |
| `A and not B` | Push `A and` on stack, parse `not B` as higher-precedence prefix |
| Postfix `X:natural!` | Flush stack while `postfix_priority > prev.priority & ~1` |
| Newline / `;` | Treated as infix at `statement_priority` (100 in default syntax) |
| `(` `)` blocks | Recursive `Parse`, wrap in `Block` node |
| `extern` / child syntax | `HasSpecialSyntax` → nested `Parser` with alternate `Syntax` |

### Syntax tables (`xl.syntax`)

Precedences are **data**, not code. The reference file
[`xlr/src/xl.syntax`](../../xlr/src/xl.syntax) declares:

```
INFIX   11  NEWLINE ;
        21  -> is has
        …
PREFIX  350 not
        …
POSTFIX 400 ! ? %
BLOCK   5   INDENT UNINDENT
        25  '{' '}'
        500 '(' ')' '[' ']'
COMMENT "//" NEWLINE
BINARY  "bits"
SYNTAX  "C" extern ;
```

`Syntax::ReadSyntaxFile` (`xlr/src/syntax.cpp`) maps each section into hash
tables. Magic names:

| Token in file | Meaning |
|---------------|---------|
| `NEWLINE` | `\n` infix (statement separator) |
| `INDENT` / `UNINDENT` | Off-side rule block delimiters |
| `STATEMENT` | Sets `statement_priority` (100) |
| `FUNCTION` | Sets `function_priority` (200) |
| `DEFAULT` | Sets `default_priority` (200) — “not an operator” |
| `BINARY` | Prefix names (`bits`) that start binary literal parsing |

Block lines register **two** precedences: the opener (`{` at 25) and the closer
(`}` at 25) as infix entries, plus `block_delimiters[open] = close`.

## XLR8 scanner: why it is more flexible

The XLR8 scanner ([`runtime/scanner.cpp`](../runtime/scanner.cpp),
[`bootstrap/scanner.xlr8`](scanner.xlr8)) serves **two roles**:

| Mode | Native | Purpose |
|------|--------|---------|
| **Bytecode** (`builtin`) | `sys builtin` | Assemble `.xlr8` — `;` ends a section, `=` defines functions, `{`/`[` define expanders |
| **XL parsing** (`parsing`) | `sys parsing` | Read XL source — `;` is an ordinary symbol, precedence hooks are active |

In parsing mode the scanner still returns a compact token set (`NAME`, `SYMBOL`,
`INTEGER`, `REAL`, `TEXT`, `COMMENT`, `DONE`), but it records operator
precedences in `infixen`, `prefixen`, `postfixen`, and `blocks` maps
(`scanner.h` lines 138–145). The bootstrap parser will **read those tables** to
make the same disambiguation decisions as `xlr/src/parser.cpp`.

`sys read_scanner dst stream` invokes `scanner::scan` and leaves the token kind
in `dst` and the lexeme in the scanner's `scanned` buffer — the parser's
`NextToken()` equivalent.

### Three extension mechanisms (scan time)

XLR8 extensibility is built into the scanner itself ([README §Bytecode definitions](../README.md)):

| Mechanism | Syntax | When | Effect |
|-----------|--------|------|--------|
| **Macro** | `[name [args] body]` | Scan | Textual substitution into bytecode |
| **Bytecode hook** | `{name [args] body}` | Scan | **Execute** body in mini-VM; stream-0 output spliced into program |
| **Named function** | `=name … ;` | Run | Store bytecode under token; `call *name` at runtime |

For XL bootstrap, **`{…}` bytecode blocks are the key**: they let syntax and
even lexing be extended **in XLR8 itself**, without recompiling the C++ runtime.

Example from [`tests/bootstrap/infix-prefix-postfix-block.xlr8`](../tests/bootstrap/infix-prefix-postfix-block.xlr8) — the syntax loader is already expressed as bytecode hooks:

```xlr8
{infix   [prec name]    sys infix}
{prefix  [prec name]    sys prefix}
{postfix [prec name]    sys postfix}
{block   [prec op cl]   sys block}
{builtin []             sys builtin}
{parsing []             sys parsing}

[NEWLINE "
"]

infix 11 NEWLINE
infix 11 ";"
infix 21 is

parsing
end
```

Each `{infix …}` / `{prefix …}` definition is a **scan-time function** that
receives `prec` and `name` (or `op` / `cl` for blocks) and calls the
corresponding `sys` native. This is exactly how `xl.syntax` should be wired:
a bootstrap program reads the syntax file (or an embedded copy) and invokes
`infix 21 is`, `block 25 { }`, etc.

`[NEWLINE "\n"]` is a macro that expands to a quoted newline token — needed
because a bare newline in the syntax table is not otherwise representable in
`.xlr8` source.

### Extending XL beyond the handbook node types

The handbook defines leaf kinds `natural`, `real`, `text`, `symbol` and inner
kinds `infix`, `prefix`, `postfix`, `block`. New kinds — **dates**, **binary
blobs**, domain literals — can be added without changing the C++ parser loop:

1. **Register a prefix hook** in the syntax table (`PREFIX  … date`).
2. **Define a scan-time bytecode** `{scan_date [dst stream] …}` that reads
   characters, validates the literal, and either:
   - appends a custom token tag the parser recognises, or
   - directly allocates a `tree_make_date(...)` node via parser callbacks.
3. **Teach the parser** one new `case` in its token switch (or a function-pointer
   table keyed by token kind).

The `BINARY "bits"` line in `xl.syntax` is the precedent: the reference scanner
treats `bits` as a known binary prefix. On XLR8, implement that as
`{scan_bits [dst] …}` rather than hard-coded C++.

Because `{foo …}` blocks run full XLR8 code with `malloc`, `read_char`, and
`write_*` to stream 0, a syntax extension can be as complex as needed (regex,
Unicode normalisation, external table lookup) while the core parser stays a
fixed, simple precedence machine.

**Suggested plugin table (bootstrap):**

```text
G_HOOK_CNT, G_HOOK_NAME[], G_HOOK_FN[]   // token name → scan hook (=label)
G_PARSE_PLUGIN[], G_EVAL_PLUGIN[]        // kind → tree builder / evaluator
```

Hooks are registered by bytecode executed at startup, same as precedences.

## Porting `Parser::Parse` onto the XLR8 scanner

### Division of labour

| Layer | Responsibility | Source to mirror |
|-------|----------------|------------------|
| **Scanner** | Characters → tokens; store precedences; run `{scan_*}` hooks | `scanner.xlr8`, `runtime/scanner.cpp` |
| **Syntax loader** | Read `xl.syntax` → call `infix`/`prefix`/`postfix`/`block` hooks | `xlr/src/syntax.cpp`, `infix-prefix-postfix-block.xlr8` |
| **Parser** | Tokens → `Tree` via `Pending` stack | `xlr/src/parser.cpp` |
| **Tree** | Normal-form nodes in kmalloc arena | `xlr/include/tree.h` (simplified) |
| **Eval** | Walk tree, `is` declarations | `xlr/src/runtime.cpp` (later) |

The parser does **not** re-lex: it calls `sys read_scanner` (or a thin wrapper
`parser_next()` that skips `COMMENT` tokens like `Parser::NextToken`).

### Mapping `Syntax` queries to XLR8

The C++ parser calls `syntax.InfixPriority(name)` etc. On XLR8, mirror the
tables when loading syntax:

```
// When {infix [prec name] sys infix} runs, also:
table_infix_store(name, prec)     // kmalloc'd open-address or sorted vector
```

Initially duplicate the tables in parser globals (same info the C++ scanner
stores in `std::map`). A future optimisation is `sys` read-back natives
(`infix_priority name → prec`); not required for bootstrap.

Constants to replicate from default `xl.syntax`:

| Constant | Value | Use |
|----------|-------|-----|
| `default_priority` | 200 | Name is “not an operator” |
| `statement_priority` | 100 | Statement vs expression boundary |
| `function_priority` | 200 | Bare literal / name binding power |

### Mapping tokens

| XL (`xlr`) | XLR8 scanner | Parser action |
|------------|--------------|---------------|
| `tokNATURAL` | `INTEGER` / natural | `tree_natural(value)` |
| `tokREAL` | `REAL` | `tree_real(value)` |
| `tokTEXT`, `tokQUOTE` | `TEXT` | `tree_text(scanned)` |
| `tokNAME`, `tokSYMBOL` | `NAME`, `SYMBOL` | disambiguate prefix/infix/postfix |
| `tokNEWLINE` | `SYMBOL` `"\n"` or dedicated token* | infix `"\n"` at prec 11 |
| `tokPAROPEN` / `tokPARCLOSE` | `SYMBOL` `(` `)` | recursive `parse(close, open)` |
| `tokINDENT` / `tokUNINDENT` | not yet emitted* | milestone F |
| `tokBINARY` | via `{scan_bits …}` hook | `tree_text` or `tree_bits` |
| `tokEOF` | `DONE` | return |
| `tokERROR` | `ERROR` | `sys print_error` |

\*Parsing-mode enhancements: the reference XL scanner emits `tokNEWLINE` and
indentation tokens itself. The bootstrap parser can either (a) add a thin
newline/indent layer on top of `read_scanner`, or (b) extend `scanner.xlr8`
to emit extra token kinds in parsing mode. Option (a) is simpler for milestone A.

### `Pending` stack in XLR8

Port the stack as a growable array in globals (same pattern as `G_LABELS` in
`scanner.xlr8`):

```text
PEND_STRIDE = 32   // opcode:8, arg:8, prio:8, pos:8  (text → kmalloc pointer)
```

Core loop structure (pseudocode matching `parser.cpp` lines 352–701):

```text
parse(closing, opening, opening_pos):
    result = null; stack = empty
    loop:
        tok = parser_next()
        switch tok:
            INTEGER  → right = tree_natural(...)
            NAME     → disambiguate; maybe set infix = scanned
            SYMBOL   → if scanned == closing: return result
                         if is_block_open(scanned): right = parse(close, open)
            ...
        // reduce stack (same priority rules as C++, including & ~1 for odd prec)
        if left && infix: push Pending; result = null
        elif right: push prefix Pending; result = right
    flush stack → return result
```

### Syntax loader as XLR8 program

Milestone D (`syntax.xlr8`) should:

1. Open `xl.syntax` (or embedded string) with `read_char` / a small line buffer.
2. Parse lines `INFIX 21 is has` / `BLOCK 25 { }` — a trivial state machine
   matching `syntax.cpp` states (`inInfix`, `inBlock`, `inBlockDef`, …).
3. For each entry, call the existing bytecode wrappers:
   `infix 21 is`, `block 25 { }`, etc.
4. Switch to `parsing` mode and hand off to `parse_program()`.

This makes the syntax description **user-extensible**: ship a custom
`my.syntax`, load it from a `{load_syntax [file] …}` bytecode block, and new
operators work without touching the parser binary.

### What not to port from `xlr`

| `xlr` feature | Bootstrap stance |
|---------------|-------------------|
| `CommentsInfo` / `AddComments` | Defer or strip comments only |
| `ChildSyntax` / `extern C` | Milestone F; use `{…}` hook when needed |
| `GCPtr` / garbage collection | Bump arena + `free` at shutdown |
| `signedConstants` option | Match XLR8 scanner numeric rules |
| Recorder / `record(parser, …)` | Optional `write_text` tracing |

## Architecture

```
  .xl source                parse tree (normal form)          result
  ----------    scanner     ------------------------    eval    ------
  characters -> tokens  ->  kmalloc'd tree nodes    ->  ...  -> values / bytecode
                ^                    ^
                |                    |
           scanner.xlr8         tree.{h,cpp} or tree.xlr8
           (done)              parser (next)
```

Longer term, evaluation can emit XLR8 bytecode instead of interpreting trees
directly. The scanner already knows how to build bytecode buffers; the
compiler back-end can reuse that machinery once the front-end is stable.

### Memory layout for bootstrap data

Follow the same patterns as [`scanner.xlr8`](scanner.xlr8):

- **Global indices** (`sto` / `rcl`) for singleton state: current stream, error
  flag, context pointer, arena bump pointer.
- **Fixed-size records** in the heap, addressed by `uintptr_t`:
  - Tree node: 32 bytes — `kind:8`, `position:8`, `a:8`, `b:8` (leaves use `a`
    for value pointer or inline small int; inner nodes use `a`/`b` for children).
  - Text blob: `length:8`, `data:…` (NUL-terminated, allocated with `kmalloc`).
  - Declaration entry: `pattern:8`, `body:8`, `next:8` (singly-linked list).
- **Growable vectors**: `(ptr, count, cap)` triple stored in globals or a
  context struct; double capacity with `realloc` (same as label/constant tables
  in the scanner).
- **Scratch buffers**: `alloca` inside `enter`/`leave` frames for token text
  and small formatting buffers.

Avoid `std::string` and `std::vector` in C++ sources; a trivial `struct buffer
{ char *data; size_t len, cap; }` with `realloc` is enough and maps cleanly to
XLR8 natives.

## Current status

### 1. Scanner — done

[`scanner.xlr8`](scanner.xlr8) is a commented XLR8 bytecode port of
[`runtime/scanner.cpp`](../runtime/scanner.cpp). It implements `next()`,
`scan()`, `validate()`, character classification, growable buffers, LEB128
append, and `do_*` directive handlers.

Global state lives in the runtime `globals` array; heap buffers use
`sys kmalloc`, `sys realloc`, and `sys free`.

### 2. Operator precedence — done (hooks wired, parser consumes next)

XL uses infix, prefix, postfix, and block operators with explicit precedence.
The C++ runtime exposes these through `sys` natives that populate the scanner's
`infixen` / `prefixen` / `postfixen` / `blocks` tables:

| Native | Purpose |
|--------|---------|
| `sys infix prec name` | Register an infix operator at precedence `prec` |
| `sys prefix prec name` | Register a prefix operator |
| `sys postfix prec name` | Register a postfix operator |
| `sys block prec open close` | Register a matched pair (`{` `}`, `(` `)`, …) |
| `sys parsing` | Enter high-level XL parsing mode (`;` no longer ends scan) |
| `sys builtin` | Return to bytecode scanning mode |

The **syntax loader itself** is expressed as `{…}` bytecode blocks that forward
to these natives — see [Syntax loader as XLR8 program](#syntax-loader-as-xlr8-program)
and [`tests/bootstrap/infix-prefix-postfix-block.xlr8`](../tests/bootstrap/infix-prefix-postfix-block.xlr8).

Precedences are stored in the scanner at load time; the parser (milestone A)
mirrors them into its own lookup tables when each `infix`/`prefix`/… hook runs.

### 3. Parser — next

### 4. Parse tree — planned

### 5. Interpreter — planned

### 6. Bytecode emitter — planned

## Implementation plan

Work proceeds in thin vertical slices. Each milestone has a test under
`tests/bootstrap/` and should run with `make tests`.

### Milestone A — precedence parser → parse tree

**Goal:** read XL source and build an in-memory tree for expressions and
top-level sequences.

**Algorithm:** Port `Parser::Parse` from [`xlr/src/parser.cpp`](../../xlr/src/parser.cpp)
— operator-precedence parsing with a `Pending` stack (see
[Porting `Parser::Parse`](#porting-parserparse-onto-the-xlr8-scanner)). Not
Pratt in the abstract: match the reference line-by-line, including spacing rules
and odd-priority right-associativity (`priority & ~1`).

Implementation steps:

- Token source: `sys read_scanner` + skip `COMMENT` (wrapper `parser_next()`).
- Precedence: `table_infix_store` etc., updated by the same `{infix …}` hooks
  that call `sys infix`.
- Tree builder: `tree_make_infix(name, left, right)` etc., all nodes from a
  bump arena (no individual `free` until program end).
- Sequences: newline and `;` infix nodes at precedence 11 (already registered
  in the test file).
- Blocks: `block` nodes with opening/closing delimiter text and one child
  (indentation blocks use the off-side rule later; initially require explicit
  `{` … `}` or `(` … `)`).

**Deliverables:**

- `bootstrap/parser.xlr8` (or `parser.cpp` once the C++ compiler is ready)
- `bootstrap/tree.xlr8` — node constructors, `tree_kind()`, `tree_dump()` for tests
- `tests/bootstrap/parse-expr.xlr8` — `1+2*3`, `sin x`, `a; b`, nested blocks
- `tests/bootstrap/parse-hello.xlr8` — `print "Hello"` as `prefix(print, infix(",", …))`

**Defer:** comments as tree metadata, long-text delimiters, `bits` literals,
child syntax (`extern { … }`), `use` processing.

### Milestone B — tree dumper / round-trip

**Goal:** prove the tree matches handbook §Homoiconic representation.

- Implement `debug` pretty-printer (handbook example) using only `write_text` /
  `write_integer` / `write_newline`.
- Compare output to `.ref` files.
- Optional: `-parse` style parenthesized output like `xl -parse -style debug`.

### Milestone C — minimal declaration + evaluation

**Goal:** run small programs without a type system.

**Context model (bootstrap simplification):**

- Singly-linked list of declarations, newest first (handbook shadowing order).
- Each entry: pattern tree + body tree.
- No types, no `when` clauses, no overloading — **first pattern match wins**
  using structural tree comparison only.

**Supported forms (YAGNI):**

| Form | Treatment |
|------|-----------|
| `name is body` | Store `(pattern=name, body)` |
| `f X is body` | Pattern is `prefix(f, name X)` or `infix` with formal `X` |
| `x := expr` | Desugar to `x is expr` binding in local context |
| `print expr` | Builtin: evaluate `expr`, print natural/int/text |
| Statement sequence | Evaluate left, then right, result is right |
| Block | Evaluate child |

**Defer:** `as` / `:` type annotations, `variable`, `use`, modules, closures,
pattern matching with types, lazy evaluation, error handling beyond
`print_error`.

**Deliverables:**

- `bootstrap/context.xlr8` — `decl_add`, `decl_lookup`, `context_push` /
  `context_pop`
- `bootstrap/eval.xlr8` — `eval(tree, ctx)` returning a `value` enum
  (`VALUE_NIL`, `VALUE_INT`, `VALUE_TEXT`, `VALUE_TREE`)
- `tests/bootstrap/eval-factorial.xlr8` — recursive `fact N is …`
- `tests/bootstrap/eval-hello.xlr8` — hello world via `print`

### Milestone D — syntax file loader

**Goal:** load precedences and comment rules from `xl.syntax` instead of
hard-coding them in each test.

Implement the state machine from [`xlr/src/syntax.cpp`](../../xlr/src/syntax.cpp)
as `syntax.xlr8`: read `INFIX` / `PREFIX` / `POSTFIX` / `BLOCK` / `COMMENT` /
`BINARY` / `SYNTAX` sections, map `NEWLINE` → `"\n"`, `INDENT` / `UNINDENT` →
block delimiters, and invoke the bytecode wrappers (`infix 21 is`, `block 25 { }`,
…). See [Syntax loader as XLR8 program](#syntax-loader-as-xlr8-program).

- Bootstrap: embed a trimmed `xl.syntax` or read from path via `sys cmdline`.
- Default table must include at least: `is` (21), `:=`, `,`, `;`, newline (11),
  `if`/`then`/`else` as prefix/infix where needed for examples.
- `BINARY "bits"` registers a prefix name; pair with `{scan_bits …}` when
  binary literals are implemented.

This mirrors handbook §Syntax file without implementing `syntax` statements
inside XL source yet.

### Milestone E — bytecode emission (optional bridge)

**Goal:** compile a **restricted** subset to XLR8 bytecode instead of
interpreting.

Start with:

- Integer arithmetic and `print` → `int`, `add`, `sys write_integer`.
- Function definitions → `=name` labels, `enter`/`leave`/`ret`, `call *name`.
- Reuse scanner's program buffer and label patching (`G_LABELS`, `G_BRANCHES`).

The interpreter (milestone C) remains the reference; bytecode is an optimization
path for hot code and for dogfooding the scanner's own output format.

### Milestone F — grow toward handbook completeness

Ordered by increasing complexity (each is a separate sub-project):

1. **Comments** — attach to nodes or strip during parse (handbook: no effect on structure).
2. **Indentation blocks** — off-side rule → `block` with synthetic delimiters.
3. **`use` statements** — parse-time only; fixed module set baked in.
4. **Type annotations** — store on patterns, ignore at runtime initially.
5. **Pattern matching** — `when` clauses, overload resolution (handbook §Pattern matching).
6. **Machine representation** — boxing/unboxing (handbook §Compiling XL).
7. **Modules and standard library** — built on top, mostly in XL itself.

## C++ coding style (for C++→XLR8 sources)

When rewriting bytecode modules as C++, keep every construct translatable:

```cpp
// OK
struct tree { uint8_t kind; uint64_t a, b; };
tree *make_infix(text name, tree *l, tree *r);
int strcmp_text(text a, text b);
void write_stderr(text msg);

// Avoid for now
template<typename T> …
auto … = …
try { … } catch …
std::cout << …
std::unique_ptr
virtual void …
```

**Naming:** mirror the XLR8 bytecode modules (`tree.cpp`, `parser.cpp`, `eval.cpp`).

**Types:** use `typedef uint64_t word;` for pointers and integers in the VM;
`int64_t` / `uint64_t` for arithmetic. Float support can wait.

**Control flow:** prefer `switch (kind)` over visitor tables. Function pointers
are fine for builtins (`typedef value (*builtin_fn)(tree *, context *)`).

**Strings:** `struct text { const char *chars; size_t len; }` pointing into the
token pool or kmalloc buffer. Compare with length-aware loop, not `strcmp` on
non-NUL strings if avoidable.

## File layout (target)

```
bootstrap/
  README.md           — this plan
  scanner.xlr8        — done
  tree.xlr8           — parse tree nodes + arena
  parser.xlr8         — precedence parser
  context.xlr8        — declaration list + scopes
  eval.xlr8           — tree interpreter
  syntax.xlr8         — default precedence/comment table
  main.xlr8           — cmdline driver (sys cmdline, sys env)
tests/bootstrap/
  …                   — one .xlr8 + .ref per feature
```

Once the C++→XLR8 compiler can compile the bootstrap sources, parallel
`*.cpp` / `*.h` files can replace the `.xlr8` modules one at a time, keeping
the same interfaces and tests.

## Testing

From the repository root:

```sh
make tests
```

Bootstrap tests live under `tests/bootstrap/`. Conventions:

- `.xlr8` — source (may include inline `[STDOUT n]` stream markers).
- `.ref` — expected stdout/stderr.
- Tree dumps and error messages should be stable and ASCII-normalized.

Add one test per milestone before starting the next. Prefer small programs taken
from handbook examples (hello world, factorial, `debug` printer) over large
fixtures.

## References

- [XL handbook](../../xlr/docs/HANDBOOK.adoc) — language definition
  - §Syntax / §Homoiconic representation — parse tree shapes
  - §Evaluation — parsing, declaration, evaluation phases
  - §Compiling XL — normal vs machine representation (later)
  - §Bootstrapping XL — historical XL2 bootstrap strategy
- [XLR8 README](../README.md) — VM layout, opcodes, `sys` natives, `{…}` bytecode
- [`runtime/native.tbl`](../runtime/native.tbl) — allowed host operations
- [`runtime/scanner.cpp`](../runtime/scanner.cpp) — scanner, `{…}` / `[…]` expansion
- [`xlr/include/parser.h`](../../xlr/include/parser.h) — parser design commentary
- [`xlr/src/parser.cpp`](../../xlr/src/parser.cpp) — reference `Parser::Parse` (~700 lines)
- [`xlr/src/syntax.cpp`](../../xlr/src/syntax.cpp) — `xl.syntax` loader
- [`xlr/src/xl.syntax`](../../xlr/src/xl.syntax) — default precedence table
- [`tests/bootstrap/infix-prefix-postfix-block.xlr8`](../tests/bootstrap/infix-prefix-postfix-block.xlr8) — precedence hooks as bytecode
