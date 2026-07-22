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
    static backend *make()              { return new amd64; }

    amd64(): x86(ARCH_AMD64) { }

protected:
    enum hwreg_t : ::hwreg_t
    {
        HW_RAX,
        HW_RCX,
        HW_RDX,
        HW_RBX,
        HW_RSP,
        HW_RBP,
        HW_RSI,
        HW_RDI,
        HW_R8,
        HW_R9,
        HW_R10,
        HW_R11,
        HW_R12,
        HW_R13,
        HW_R14,
        HW_R15,
        HW_XMM0,
        HW_XMM1,
        HW_XMM2,
        HW_XMM3,
        HW_XMM4,
        HW_XMM5,
        HW_XMM6,
        HW_XMM7,
        HW_XMM8,
        HW_XMM9,
        HW_XMM10,
        HW_XMM11,
        HW_XMM12,
        HW_XMM13,
        HW_XMM14,
        HW_COUNT,

        HW_FRAME   = HW_R12, // rt.frame — see regmap.h
        HW_FLAGS   = HW_R15, // rt.flags (64-bit predicates)
        HW_SCRATCH = HW_R11, // general scratch (swap, etc.)
    };

    uint hw_arg0() const
    // ------------------------------------------------------------------------
    //   First integer C-helper argument register (SysV rdi / Win64 rcx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RCX : HW_RDI;
    }

    uint hw_arg1() const
    // ------------------------------------------------------------------------
    //   Second integer C-helper argument register (SysV rsi / Win64 rdx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RDX : HW_RSI;
    }

    uint registers() const override    { return 25; }
    uint pointer_size() const override   { return 8; }

    uint jump(uint t) override
    // ------------------------------------------------------------------------
    //   Emit jump with deferred fixup chain
    // ------------------------------------------------------------------------
    {
        emit8(0xe9);
        uint pos = uint(cursor());
        emit32le(t);
        return pos;
    }

    void resolve_jump(uint t, size_t a) override
    // ------------------------------------------------------------------------
    //   Patch pending jumps to address a
    // ------------------------------------------------------------------------
    {
        section &sec = code();
        while (t)
        {
            uint n = t;
            uint32_t next = 0;
            if (n + 4 <= sec.size())
            {
                next = uint32_t(sec[n])
                     | (uint32_t(sec[n + 1]) << 8)
                     | (uint32_t(sec[n + 2]) << 16)
                     | (uint32_t(sec[n + 3]) << 24);
            }
            int32_t offset = int32_t(a) - int32_t(n) - 4;
            sec.patch32le(n, uint32_t(offset));
            t = next;
        }
    }

    void            do_prologue();
    void            do_ret();
};

RECORDER_DECLARE(amd64);
RECORDER_DECLARE(amd64_ops);
RECORDER_DECLARE(amd64_error);
RECORDER_DECLARE(amd64_warning);


#endif // AMD64_H
