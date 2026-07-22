#ifndef I386_H
#define I386_H
// ****************************************************************************
//  i386.h                                                        XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     32-bit x86 code generation backend.
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


struct i386_be : x86
// ----------------------------------------------------------------------------
//   32-bit x86 code generator (named i386_be: "i386" is a cpp macro on i686)
// ----------------------------------------------------------------------------
{
    static backend *make()              { return new i386_be; }

    i386_be(): x86(ARCH_I386) { }

    cstring machine_defs() const override
    {
        return "__i386__\0__i386\0";
    }

    uint registers() const override    { return 8; }
    uint pointer_size() const override   { return 4; }

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
};

#endif // I386_H
