// ****************************************************************************
//  i386.cpp                                                        XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     i386 code generator, inspired by TinyCC's i386-gen.c
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

#include "backend.h"


// ============================================================================
//
//   TARGET_DEFS_ONLY — register and ABI constants
//
// ============================================================================

#define NB_REGS         8
#define PTR_SIZE        4
#define REG_IRET        0       // EAX

enum : uint
{
    TREG_EAX = 0,
    TREG_ECX = 1,
    TREG_EDX = 2,
    TREG_EBX = 3,
    TREG_ESP = 4,
    TREG_EBP = 5,
    TREG_ESI = 6,
    TREG_EDI = 7,
};


struct backend_i386 : backend
// ----------------------------------------------------------------------------
//   i386 code generator
// ----------------------------------------------------------------------------
{
    backend_i386(): backend(ARCH_I386) { }

    cstring machine_defs() const override
    {
        return "__i386__\0__i386\0";
    }

    uint nb_regs() const override    { return NB_REGS; }
    uint ptr_size() const override   { return PTR_SIZE; }
    uint reg_iret() const override   { return REG_IRET; }

    uint gjmp(uint t) override
    {
        g(0xe9);
        uint pos = uint(cursor());
        emit32(t);
        return pos;
    }

    void gsym_addr(uint t, uint a) override
    {
        section &sec = code();
        while (t)
        {
            uint n = t;
            uint32_t next = 0;
            if (n >= 4)
            {
                size_t at = n - 4;
                next = uint32_t(sec[at])
                     | (uint32_t(sec[at + 1]) << 8)
                     | (uint32_t(sec[at + 2]) << 16)
                     | (uint32_t(sec[at + 3]) << 24);
            }
            int32_t offset = int32_t(a) - int32_t(n);
            sec.patch32(n - 4, uint32_t(offset));
            t = next;
        }
    }

    void load_reg_imm(uint r, value_t imm) override
    {
        g(byte(0xb8 + r));
        emit32(uint32_t(imm));
    }

    void add_reg_reg(uint dst, uint src) override
    {
        g(0x01);
        g(byte(0xc0 | (dst << 3) | src));
    }

    void ret() override
    {
        g(0xc3);
    }
};


backend *backend_i386_new()
{
    return new backend_i386;
}
