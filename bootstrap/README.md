# XLR8 bootstrap

This bootstrap stage reads input files and builds a simple XL interpreter.
It consists of three key components:

## 1. Scanner

A scanner transforms input characters into tokens, in a way similar to what is
found in [the C++ runtime](../runtime/scanner.cpp).

The bootstrap implementation lives in [`scanner.xlr8`](scanner.xlr8): a heavily
commented XLR8 bytecode port of `runtime/scanner.cpp`, including `next()`,
`scan()`, `validate()`, character classification, string/program buffers, LEB128
append, and the `do_*` directive handlers (`=`, `?`, `!`, `.`, `:`, `*`, `#`).

Global state is stored in the runtime `globals` array (`sto` / `rcl`); heap
buffers use `sys kmalloc`, `sys realloc`, and `sys free`.

## 2. Operator precedence

XL source uses infix, prefix, postfix, and block operators with explicit
precedence. The C++ runtime exposes these through `sys` natives that populate
the scanner precedence tables:

| Native | Purpose |
|--------|---------|
| `sys infix prec name` | Register an infix operator at precedence `prec` |
| `sys prefix prec name` | Register a prefix operator |
| `sys postfix prec name` | Register a postfix operator |
| `sys block prec open close` | Register a matched pair (`{` `}`, `(` `)`, …) |
| `sys parsing` | Enter high-level parsing mode |
| `sys builtin` | Return to bytecode scanning mode |

The test program [`tests/bootstrap/infix-prefix-postfix-block.xlr8`](../tests/bootstrap/infix-prefix-postfix-block.xlr8) shows how to define thin macro wrappers around these natives and register a minimal precedence table (for example `infix 21 is` and newline/`;` at precedence 11).

## 3. Parser

A parser for XL expressions and declarations is planned as the next bootstrap
component. It will consume tokens from the scanner, use the precedence tables
above, and emit XLR8 bytecode.

## Building and testing

Run the bootstrap scanner tests from the repository root:

```sh
make tests
```

Bootstrap-specific tests live under `tests/bootstrap/`.
