# XLR8 bootstrap

This bootstrap stage reads input files and builds a simple XL interpreter.
It consists in 3 key components:

- A scanner transforms input characters into tokens, in a way similar
  to what is found in [the C++ runtime](../runtime/scanner.cpp).
  The bootstrap implementation lives in [`scanner.xlr8`](scanner.xlr8): a
  heavily commented XLR8 bytecode port of `runtime/scanner.cpp`, including
  `next()`, `scan()`, `validate()`, character classification, string/program
  buffers, LEB128 append, and the `do_*` directive handlers (`=`, `?`, `!`,
  `.`, `:`, `*`, `#`).
- A description of the priorities and operators
  * `infix <
- A parser
