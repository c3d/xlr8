// ****************************************************************************
//  riscv32.cpp                                                   XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     RISC-V 32 (ILP32) overrides on the riscv64 backend.
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

#include "riscv32.h"


RECORDER(riscv32, 64, "RISC-V 32-bit code generation helpers");


riscv32::riscv32(runtime &rt)
// ----------------------------------------------------------------------------
//   Construct an RV32 ILP32 code generator
// ----------------------------------------------------------------------------
    : riscv64(rt, ARCH_RISCV32)
{
    record(riscv32, "New backend arch=%+s", arch_name(ARCH_RISCV32));
}


cstring riscv32::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    return "__riscv\0__riscv_xlen 32\0";
}


uint riscv32::pointer_size() const
// ----------------------------------------------------------------------------
//   Return size of a pointer on RV32
// ----------------------------------------------------------------------------
{
    return 4;
}


void riscv32::emit_store_abi_word(riscv64reg_t rs2, riscv64reg_t rs1, int offs)
// ----------------------------------------------------------------------------
//   Store one ABI pointer/link word (sw)
// ----------------------------------------------------------------------------
{
    emit32le(sw_off(xn(rs2), xn(rs1), offs));
}


void riscv32::emit_load_abi_word(riscv64reg_t rd, riscv64reg_t rs1, int offs)
// ----------------------------------------------------------------------------
//   Load one ABI pointer/link word (lw)
// ----------------------------------------------------------------------------
{
    emit32le(lw_off(xn(rd), xn(rs1), offs));
}


void riscv32::load_ptr_imm(riscv64reg_t r, uint64_t v)
// ----------------------------------------------------------------------------
//   Materialize a 32-bit code/data pointer
// ----------------------------------------------------------------------------
{
    load_uimm32(r, uint32_t(v));
}


void riscv32::emit_load_off(riscv64reg_t rd, riscv64reg_t rn, int offs)
// ----------------------------------------------------------------------------
//   Load low half of an ularge_t slot into rd (lw)
// ----------------------------------------------------------------------------
{
    uint d = xn(rd);
    uint n = xn(rn);
    if (offs >= -2048 && offs <= 2047)
    {
        emit32le(lw_off(d, n, offs));
    }
    else
    {
        load_uimm32(HW_IP, uint32_t(int32_t(offs)));
        emit32le(add_rr(xn(HW_IP), n, xn(HW_IP)));
        emit32le(lw_idx(d, xn(HW_IP)));
    }
}


void riscv32::emit_store_off(riscv64reg_t rn, riscv64reg_t rt, int offs)
// ----------------------------------------------------------------------------
//   Store 32-bit GPR into ularge_t slot; zero the high half
// ----------------------------------------------------------------------------
{
    uint n = xn(rn);
    uint t = xn(rt);
    if (offs >= -2048 && offs + 4 <= 2047)
    {
        emit32le(sw_off(t, n, offs));
        emit32le(sw_off(0, n, offs + 4)); // x0
    }
    else
    {
        load_uimm32(HW_IP, uint32_t(int32_t(offs)));
        emit32le(add_rr(xn(HW_IP), n, xn(HW_IP)));
        emit32le(sw_idx(t, xn(HW_IP)));
        emit32le(sw_off(0, xn(HW_IP), 4));
    }
}
