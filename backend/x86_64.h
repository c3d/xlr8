#ifndef X86_64_H
#define X86_64_H
// ****************************************************************************
//  x86_64.h                                                      XLR8 project
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

#include "x86.h"


struct x86_64 : x86
// ----------------------------------------------------------------------------
//   64-bit x86 code generator
// ----------------------------------------------------------------------------
{
    static backend *make()              { return new x86_64; }

    x86_64(): x86(ARCH_X86_64) { }

    cstring machine_defs() const override
    {
        return "__x86_64__\0__amd64__\0";
    }

    uint nb_regs() const override    { return 25; }
    uint ptr_size() const override   { return 8; }
    uint reg_iret() const override   { return 0; }

    uint gjmp(uint t) override
    // ------------------------------------------------------------------------
    //   Emit jump with deferred fixup chain
    // ------------------------------------------------------------------------
    {
        g(0xe9);
        uint pos = uint(cursor());
        emit32le(t);
        return pos;
    }

    void gsym_addr(uint t, uint a) override
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
            sec.patch32(n, uint32_t(offset));
            t = next;
        }
    }
};

#endif // X86_64_H
