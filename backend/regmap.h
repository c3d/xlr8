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
//     Shadow locals     r0 .. r3      frame[0] .. frame[3] when valid
//     Scratch value     r4            temps (not a shadow)
//     Scratch offset    r5            temps (not a shadow)
//     Scratch address   r8            address formation
//     Frame pointer     r11           rt.frame, callee-saved, primed at entry
//     Flags/predicates  r10           rt.flags (low 32), primed at entry
//     Intra-call IP     r12           load target for call_abs (AAPCS IP)
//     Stack pointer     r13 (sp)      native stack
//
//     Locals >= 4: ldr/str via [r11, #local*8] using scratch regs.
//     Shadows are limited to r0-r3 so r4-r7 stay dedicated scratches
//     (BACKEND-REVIEW §6.11).  Prologue still saves r4-r11,lr.
//
//
//  ---------------------------------------------------------------------------
//  AArch64 (arm64)
//  ---------------------------------------------------------------------------
//
//     Direct-map locals (callee-saved first):
//       local 0…8   →  x19…x27   (survive C/`sys` without spill)
//       local 9…23  →  x0…x14    (caller-saved; spilled around C/`sys`)
//     Scratch           x15           HW_SCRATCH
//     Overflow temps    x16…x18       HW_TEMP0… (x18 reserved on macOS)
//     Flags/predicates  x28           rt.flags
//     Frame pointer     x29           HW_FRAME; local_disp(n) = ~(n)*8
//     Link register     x30
//     Stack pointer     sp (x31)
//
//     See docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md
//
//
//  ---------------------------------------------------------------------------
//  x86-64 (amd64) — direct-map allocator
//  ---------------------------------------------------------------------------
//
//     Direct-map locals (callee-saved first):
//       local 0…4   →  rbx, r12, r13, r14, r15  (survive C/sys without spill)
//       local 5…9   →  rsi, rdi, r8, r9, r10    (caller-saved; spilled around C)
//     Overflow temps    rax, rdx          HW_TEMP0… (locals >= 10)
//     Scratch           rcx               HW_SCRATCH (shift-count, encoding temp)
//     Flags/predicates  r11               HW_FLAGS; non-durable (spill around sys)
//     Frame pointer     rbp               HW_FRAME; local_disp(n) = ~(n)*8
//     Stack pointer     rsp               native stack
//
//     See docs/superpowers/specs/2026-08-06-x86-direct-map-allocator-design.md
//
//
//  ---------------------------------------------------------------------------
//  i386 (i386)
//  ---------------------------------------------------------------------------
//
//     Frame pointer     ebp (HW_FRAME)   Native frame via enter/leave
//     Flags/predicates  esi (HW_FLAGS)   low 32 flags; high half at [ebp+4]
//     Scratch           ecx (HW_SCRATCH) general temp
//     Prologue saves    ebx,esi,edi      (SysV callee-saved); restored in epilogue
//     Value temps       eax, ecx, edx    used by opcode helpers
//     Stack pointer     esp              native stack
//     Direct-map not yet active for i386 — locals accessed via frame memory.
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
