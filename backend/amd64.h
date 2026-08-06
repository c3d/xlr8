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
//  Locals use [rbp - (local+1)*8]; rbp is set by op_enter / op_leave
//
//  Register map (x86-64/amd64):
//
//    Frame pointer     rbp (HW_FRAME)   Native frame for locals (via enter)
//    Flags/predicates  r15 (HW_FLAGS)   rt.flags (64-bit)
//    Scratch           r11              general temp (swap, etc.)
//    Value temps       rax, rcx, rdx    used by opcode helpers
//    Callee-saved      rbx,r12,r15      saved/restored in prologue/epilogue
//                       (rbp saved by enter; r12 allocatable shadow)
//    C args (SysV)     rdi, rsi         first two integer helper args
//    C args (Win64)    rcx, rdx         first two integer helper args
//    Stack pointer     rsp              native stack (Win64: 32-byte call shadow)
//
//  i386 variant: ularge_t slots (8 bytes), no shadow locals.
//    Frame pointer     ebp (HW_FRAME)   Native frame via enter/leave
//    Flags/predicates  esi (HW_FLAGS)   low 32 flags; high half at [ebp+4]
//    Scratch           ecx (HW_RCX)     general temp
//    Prologue saves    ebx,esi,edi      (SysV callee-saved); restored in epilogue
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "x86.h"


RECORDER_DECLARE(amd64);
RECORDER_DECLARE(amd64_ops);
RECORDER_DECLARE(amd64_error);
RECORDER_DECLARE(amd64_warning);


struct amd64 : x86
// ----------------------------------------------------------------------------
//   64-bit x86 code generator
// ----------------------------------------------------------------------------
{
    static backend *make(runtime &rt, arch_t arch)
    {
        return new amd64(rt, arch);
    }

    amd64(runtime &rt, arch_t arch);

    cstring     machine_defs() const                            override;
    reginfo     register_definition(hwreg_t reg) const          override;
    uint        pointer_size() const                            override;


};


#endif // AMD64_H
