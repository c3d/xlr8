# XLR8 bootstrap compiler (C transpiler)

The compiler in this directory is a scaled-down and simplified version of XL
intended to bootstrap an XL compiler on machines that have a reasonably modern
C++ compiler. It is a transpiler from simplified XL to C.

The reduced version of XL has C++ level semantics, requires explicit type
annotations everywhere that directly match to C++ types, with a goal to reach
self-compilation status as quickly as possible.
