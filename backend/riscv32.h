#ifndef RISCV32_H
#define RISCV32_H
// ****************************************************************************
//  riscv32.h                                                     XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     RISC-V 32 (ILP32) code generation backend — thin subclass of riscv64.
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

#include "riscv64.h"


RECORDER_DECLARE(riscv32);


struct riscv32 : riscv64
// ----------------------------------------------------------------------------
//   RISC-V 32 code generator (ILP32 overrides on the RV64 backend)
// ----------------------------------------------------------------------------
{
    static backend *make(runtime &rt) { return new riscv32(rt); }

    riscv32(runtime &rt);

    cstring     machine_defs() const                            override;
    uint        pointer_size() const                            override;

protected:
    void        emit_store_abi_word(riscv64reg_t rs2, riscv64reg_t rs1,
                                    int offs)                   override;
    void        emit_load_abi_word(riscv64reg_t rd, riscv64reg_t rs1,
                                   int offs)                    override;
    void        load_ptr_imm(riscv64reg_t r, uint64_t v)        override;
    void        emit_load_off(riscv64reg_t rd, riscv64reg_t rn,
                              int offs)                         override;
    void        emit_store_off(riscv64reg_t rn, riscv64reg_t rt,
                               int offs)                        override;
};


#endif // RISCV32_H
