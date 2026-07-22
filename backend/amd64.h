#ifndef AMD64_H
#define AMD64_H
// ****************************************************************************
//  amd64.h                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     64-bit x86 code generation backend.
//
//
//
//
//
//
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
//
//  ===========================================================================
//  REGISTER ALLOCATION CONVENTIONS (x86-64 / amd64)
//  ===========================================================================
//
//  Locals use [r12 + local*8]
//
//  Register map (x86-64/amd64):
//
//    Frame pointer     r12 (HW_FRAME)   Runtime frame for locals
//    Flags/predicates  r15 (HW_FLAGS)   rt.flags (64-bit), primed at entry
//    Scratch           r11 (HW_SCR)     general temp (swap, etc.)
//    Value temps       rax, rcx, rdx    used by opcode helpers
//    Callee-saved      rbx,rbp,r12,r15  saved/restored in prologue/epilogue
//    C args (SysV)     rdi, rsi         first two integer helper args
//    C args (Win64)    rcx, rdx         first two integer helper args
//    Stack pointer     rsp              native stack (Win64: 32-byte call shadow)
//
//  i386 variant: ularge_t slots (8 bytes), no shadow locals (registers == 8).
//    Frame pointer     edi (HW_RDI)     rt.frame, callee-saved (avoid ebx PIC/GOT)
//    Flags/predicates  esi (HW_RSI)     rt.flags, callee-saved
//    Scratch           ecx (HW_RCX)     general temp
//    Prologue saves    ebx,esi,edi,ebp  (SysV callee-saved); restored in do_ret
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "x86.h"


struct amd64 : x86
// ----------------------------------------------------------------------------
//   64-bit x86 code generator
// ----------------------------------------------------------------------------
{
    static backend *make(arch_t arch)           { return new amd64(arch); }

    amd64(arch_t arch): x86(arch) { }

protected:
    void            do_prologue()       override;
    void            do_ret()            override;
};

RECORDER_DECLARE(amd64);
RECORDER_DECLARE(amd64_ops);
RECORDER_DECLARE(amd64_error);
RECORDER_DECLARE(amd64_warning);


#endif // AMD64_H
