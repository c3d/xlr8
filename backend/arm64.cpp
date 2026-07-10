// ****************************************************************************
//  arm64.cpp                                                       XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     AArch64 code generator, inspired by TinyCC's arm64-gen.c
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

#define NB_REGS         28
#define PTR_SIZE        8
#define REG_IRET        0       // x0

#define TREG_R(x)       (x)
#define TREG_F(x)       ((x) + 20)


struct backend_arm64 : backend
// ----------------------------------------------------------------------------
//   AArch64 code generator
// ----------------------------------------------------------------------------
{
    backend_arm64(): backend(ARCH_ARM64) { }

    cstring machine_defs() const override
    {
        return "__aarch64__\0__arm64__\0";
    }

    uint nb_regs() const override    { return NB_REGS; }
    uint ptr_size() const override   { return PTR_SIZE; }
    uint reg_iret() const override   { return REG_IRET; }

    uint gjmp(uint t) override
    {
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
            int32_t offset = (int32_t(a) - int32_t(n)) / 4;
            uint32_t insn = 0x14000000u | (uint32_t(offset) & 0x03ffffffu);
            sec.patch32(n - 4, insn);
            t = next;
        }
    }

    void load_reg_imm(uint r, value_t imm) override
    {
        uint32_t insn = 0xd2800000u | ((uint32_t(imm) & 0xffffu) << 5) | r;
        emit32(insn);
    }

    void add_reg_reg(uint dst, uint src) override
    {
        uint32_t insn = 0x8b000000u | (src << 16) | (dst << 5) | dst;
        emit32(insn);
    }

    void ret() override
    {
        emit32(0xd65f03c0u);
    }
};


backend *backend_arm64_new()
{
    return new backend_arm64;
}
