// ****************************************************************************
//  riscv64.cpp                                                     XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     RISC-V 64-bit code generator, inspired by TinyCC's riscv64-gen.c
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

#define NB_REGS         19
#define PTR_SIZE        8
#define REG_IRET        0       // a0 (x10)

#define TREG_R(x)       (x)
#define TREG_F(x)       ((x) + 8)


struct backend_riscv64 : backend
// ----------------------------------------------------------------------------
//   RISC-V 64-bit code generator
// ----------------------------------------------------------------------------
{
    backend_riscv64(): backend(ARCH_RISCV64) { }

    cstring machine_defs() const override
    {
        return "__riscv\0__riscv_xlen 64\0";
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
            uint32_t insn = 0x0000006fu | (uint32_t(offset & 0xfffff) << 12);
            sec.patch32(n - 4, insn);
            t = next;
        }
    }

    void load_reg_imm(uint r, value_t imm) override
    {
        uint rd = 10 + r;
        int32_t simm = int32_t(imm);
        uint32_t insn = 0x00000013u | (uint32_t(r) << 7)
                      | (uint32_t(simm & 0xfff) << 20)
                      | (rd << 7);
        emit32(insn);
    }

    void add_reg_reg(uint dst, uint src) override
    {
        uint rd = 10 + dst;
        uint rs1 = 10 + dst;
        uint rs2 = 10 + src;
        uint32_t insn = 0x00000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
        emit32(insn);
    }

    void ret() override
    {
        emit32(0x00008067u);
    }
};


backend *backend_riscv64_new()
{
    return new backend_riscv64;
}
