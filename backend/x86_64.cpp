// ****************************************************************************
//  x86_64.cpp                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     x86-64 code generator, inspired by TinyCC's x86_64-gen.c
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

#define NB_REGS         25
#define PTR_SIZE        8
#define REG_IRET        0       // RAX
#define RC_INT          0x0001

enum : uint
{
    TREG_RAX = 0,
    TREG_RCX = 1,
    TREG_RDX = 2,
    TREG_RSP = 4,
    TREG_RSI = 6,
    TREG_RDI = 7,
    TREG_R8  = 8,
    TREG_R9  = 9,
};

#define REX_BASE(reg)   (((reg) >> 3) & 1)
#define REG_VALUE(reg)  ((reg) & 7)


struct backend_x86_64 : backend
// ----------------------------------------------------------------------------
//   x86-64 code generator
// ----------------------------------------------------------------------------
{
    backend_x86_64(): backend(ARCH_X86_64) { }

    cstring machine_defs() const override
    {
        return "__x86_64__\0__amd64__\0";
    }

    uint nb_regs() const override    { return NB_REGS; }
    uint ptr_size() const override   { return PTR_SIZE; }
    uint reg_iret() const override   { return REG_IRET; }

    void orex(int ll, int r, int x, byte c)
    {
        if ((ll && r >= 8) || x >= 8)
            g(0x40 | (ll ? 1 : 0) | (Rex_b(r) ? 4 : 0) | (Rex_x(x) ? 2 : 0) | (Rex_r(r) ? 1 : 0));
        g(c);
    }

    void gen_modrm(int r, int x, int b)
    {
        (void) x;
        g(byte((REG_VALUE(r) << 3) | REG_VALUE(b)));
        if (REG_VALUE(b) == 4)
            g(byte((REG_VALUE(x) << 3) | 5));
        if (REG_VALUE(b) == 5)
            emit32(0);
    }

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
        orex(1, r, 0, 0xb8 + REG_VALUE(r));
        emit32(uint32_t(imm));
        if (PTR_SIZE == 8)
            emit32(uint32_t(uint64_t(imm) >> 32));
    }

    void add_reg_reg(uint dst, uint src) override
    {
        orex(1, dst, src, 0x01);
        g(byte(0xc0 | (REG_VALUE(dst) << 3) | REG_VALUE(src)));
    }

    void ret() override
    {
        g(0xc3);
    }

private:
    static bool Rex_b(int r)  { return (r >> 3) & 1; }
    static bool Rex_x(int x)  { return (x >> 3) & 1; }
    static bool Rex_r(int r)  { return (r >> 3) & 1; }
};


backend *backend_x86_64_new()
{
    return new backend_x86_64;
}
