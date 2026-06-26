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
3. Actor-based operation, local or remot
4. Remote operation similar to ELFE
5. Async filesystem and networking (transparent networking)
6. Async for 3D graphics similar  to Tao3D
7. Capability-based / visibility-based security


## Bytecode

The base execution model is an extensible stack-based bytecode.

Operators:

* `/` defines comments
* `*` imports a predefained opcode or `all` to import all
* `=` defines a new opcode
* `;` marks the end of the new opcode
* `.` defines a local label
* `:` inserts a delta to the given label

Example 1: Bytecode level factorial:


```xlr8
// Import some builtin opcodes (we could have used *all)
*end         // opcode 0
*int         // opcode 1
*sub         // opcode 2
*mul         // opcode 3
*beq         // opcode 4
*bra         // opcode 5
*swp         // opcode 6
*jmp         // opcode 7
*get         // opcode 8
*drp         // opcode 9

// Define `ret` as opcode 10
=ret
    swp 1       // 6 1      stk[0] <-> stk[1], get return PC
    jmp         // 7        jump to stk[0] (return PC)
;

// Define `fact` as opcode 11
=fact           // N -> N!
    int 1       // 1 1      push 1                                      1 N
.loop           //          label 0                                     P N
    get 1       // 8 1      push stk[1]                                 N P N
    beq :exit   // 4 1      Test if stk[0] eq 0, if so branch to .exit  P N
    get 1       // 8 1      push stk[1]                                 N P N
    mul         // 3        stk[0] = N * P                              N*P N
    swp 1       // 6 1      stk[0] <-> stk[1]                           N N*P
    int 1       // 1 1      push 1                                      1 N N*P
    sub         // 2        sub stk[0] from stk[1]                      N-1 N*P
    swp 1       // 6 1      stk[0] <-> stk[1]                           N*P N-1
    bra :loop   // 5 0      Branch to label 0                           P' N'
.exit           //          label 1                                     P N
    swp 1       // 6 1      stk[0] <-> stk[1]                           N P
    drp         // 9        Drop stk[0]                                 P
    ret         // 10
;
```

Encoding of the above: 25 bytes

```
6 1 7 1 1 8 1 4 1 8 1 3 6 1 1 1 2 6 1 5 0 6 1 9 10
```

Layout of runtime:

* stack[]       native values, uintptr_t, values, locals and activation records
* opcodes[]     pointer to code execution for opcodes, or to definition
* labels[]      labels in current opcode



## Scanning
