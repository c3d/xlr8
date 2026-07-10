#ifndef REGMAP_H
#define REGMAP_H
// ****************************************************************************
//  regmap.h                                                       XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Register allocation model for native code generation.
//
//     Bytecode locals are slots in the runtime frame (stack_p, one uintptr_t
//     per slot).  The JIT may keep the first LOC_SHADOW_COUNT locals in
//     dedicated hardware registers that shadow frame[0..LOC_SHADOW_COUNT-1]
//     while they are valid.  Locals at index >= LOC_SHADOW_COUNT always live
//     in frame memory and are accessed with load/store through the frame
//     pointer.
//
//     Shadow validity: a shadow register is authoritative until (a) the
//     corresponding frame slot is written from outside the shadow (syscall,
//     enter/leave, explicit spill), or (b) the shadow is reused as scratch.
//     Before a syscall we spill shadowed locals back into the frame so C
//     helpers see consistent memory.  Locals 0 and 1 are also syscall argument
//     registers on AArch64 and 32-bit ARM; may be reloaded from the frame
//     after sys.
//
//     The bytecode PC is not held in a fixed hardware register during
//     translation: it is the translator's cursor.  Generated code records PC
//     values as immediates (push_pc, load_pc) or jumps through the runtime.
//     A dedicated HW_PC register is reserved for future threaded code but is
//     unused today.
//
//     The runtime pointer is currently reached through xlr8_get_runtime() in
//     spill/sys paths.  HW_RT is reserved so generated code can cache
//     &runtime in a callee-saved register later.
//
//
//  ---------------------------------------------------------------------------
//  32-bit ARM (arm / AArch32)
//  ---------------------------------------------------------------------------
//
//     Shadow locals     r0 .. r7      frame[0] .. frame[7] when valid
//     Scratch address   r8            address formation
//     Frame pointer     r9            rt.frame, primed at entry
//     Flags/predicates  r10           rt.flags (64-bit), primed at entry
//     Saved link        r11           LR across runtime calls
//     Intra-call IP     r12           load target for call_abs (AAPCS IP)
//     Stack pointer     r13 (sp)      native stack
//     Link register     r14 (lr)      restored from r11 on return
//
//     Locals 8..15 and >= 16: ldr/str via [r9, #local*4] using scratch regs.
//     Follows TinyCC ARM mode: r0-r3 and r12 are caller-saved temps in TCC;
//     this JIT shadows r0-r7 for the first eight locals (enter(8)).
//
//
//  ---------------------------------------------------------------------------
//  AArch64 (arm64)
//  ---------------------------------------------------------------------------
//
//     Shadow locals     x0 .. x15     frame[0] .. frame[15] when valid
//     Frame pointer     x19           rt.frame, callee-saved, primed at entry
//     Saved link        x20           LR across runtime calls
//     Runtime (rsvd)    x21           &runtime, not yet loaded
//     Flags/predicates  x22           rt.flags (64-bit), primed at entry
//     Scratch value     x23           temp for swap, arithmetic, immediates
//     Scratch address   x24           temp for address formation
//     Scratch offset    x25           temp for indexed addressing
//     Intra-call IP     x16           load target for call_abs (AAPCS IP0)
//     Stack pointer     sp (x31)      native stack; bytecode stack uses frame
//
//     Locals >= 16: ldr/str via [x19, #local*8] using scratch registers.
//     x17, x18, x26-x28, x29 (FP), x30 (LR) follow the platform ABI.
//
//
//  ---------------------------------------------------------------------------
//  x86-64 (x86_64)
//  ---------------------------------------------------------------------------
//
//     All bytecode locals live in frame memory at [r12 + local*8].
//     There are no shadow registers on x86: every access is load/store.
//
//     Frame pointer     r12           rt.frame, primed at entry
//     Flags/predicates  r15           rt.flags (64-bit), primed at entry
//     Scratch           r11           general temp (swap, etc.)
//     Value temps       rax, rcx, rdx used by opcode helpers
//     Stack pointer     rsp           native stack
//
//
//  ---------------------------------------------------------------------------
//  i386 (i386)
//  ---------------------------------------------------------------------------
//
//     Same model as x86-64 but 32-bit pointers and only 8 bytecode locals
//     in the ISA surface (nb_regs == 8).  Frame pointer ebx or a dedicated
//     reg would be needed; current backend inherits the x86_64 register enum
//     and uses [r12+disp] with 4-byte slots when built as i386.
//
//
//  ---------------------------------------------------------------------------
//  RISC-V 64 (riscv64)
//  ---------------------------------------------------------------------------
//
//     Shadow locals     x10 .. x25    frame[0] .. frame[15] (xreg = 10+n)
//     Frame pointer     x8  (s0)      rt.frame (reserved, not yet primed)
//     Flags/predicates  x9  (s1)      rt.flags (reserved)
//     Scratch value     x28 (t3)      swap and arithmetic temps
//     Scratch address   x29           address temp (reserved)
//     Scratch offset    x30           offset temp (reserved)
//     Stack pointer     sp (x2)
//
//     Locals >= 16: load/store through frame pointer (to be implemented).
//     a0-a7 (x10-x17) overlap shadow locals 0-7 by design in the current
//     JIT entry convention (generated code is not a normal RV callee).
//
//
// ****************************************************************************
//   (C) 2026 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of XLR8.
//
//   XLR8 is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   XLR8 is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "types.h"


static const uint LOC_SHADOW_COUNT = 16;


#endif // REGMAP_H
