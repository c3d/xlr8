#ifndef RISCV64_H
#define RISCV64_H
// ****************************************************************************
//  riscv64.h                                                     XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     RISC-V 64 code generation backend for all built-in opcodes.
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
//  REGISTER ALLOCATION CONVENTIONS (RISC-V 64 / RV64GC SysV)
//  ===========================================================================
//
//  Locals use [x8 - (local+1)*8]; x8 is adjusted by op_enter and op_leave.
//  Args use [x8 + arg*8 + 2*pointer_size()] (skip saved FP+RA).
//  Flags (predicates) live in x9 (HW_FLAGS).
//  RV32 subclasses override pointer_size and ABI word load/store hooks.
//
//  Register map:
//
//    Frame pointer     x8  (HW_FRAME)   Native FP for locals / args
//    Return address    x1  (HW_RA)      Saved with FP on enter
//    Flags/predicates  x9  (HW_FLAGS)   Callee-saved predicate bits
//    Scratch           x5  (HW_IP)      Temps / call targets (not allocatable)
//    Allocatable GPRs  x18-x27          Callee-saved only (survive sys calls)
//                      x0-x17,x28-x31   Call-clobbered / reserved (not alloc)
//    Stack pointer     x2  (HW_SP)      Native stack
//
//  Bytecode PC is the translator cursor, not a fixed HW register.
//
//  ===========================================================================

#include "backend.h"
#include "recorder.h"
#include "regmap.h"


RECORDER_DECLARE(riscv64);
RECORDER_DECLARE(riscv64_ops);
RECORDER_DECLARE(riscv64_error);
RECORDER_DECLARE(riscv64_warning);


struct riscv64 : backend
// ----------------------------------------------------------------------------
//   RISC-V 64 code generator implementing every built-in opcode
// ----------------------------------------------------------------------------
{
    riscv64(runtime &rt, arch_t arch = ARCH_RISCV64);
    static backend *make(runtime &rt) { return new riscv64(rt); }

#include "backend-iface.h"

protected:
    enum riscv64reg_t : hwreg_t
    // ------------------------------------------------------------------------
    //    Hardware register names for RV64 integer registers
    // ------------------------------------------------------------------------
    {
        HW_X0,
        HW_X1,
        HW_X2,
        HW_X3,
        HW_X4,
        HW_X5,
        HW_X6,
        HW_X7,
        HW_X8,
        HW_X9,
        HW_X10,
        HW_X11,
        HW_X12,
        HW_X13,
        HW_X14,
        HW_X15,
        HW_X16,
        HW_X17,
        HW_X18,
        HW_X19,
        HW_X20,
        HW_X21,
        HW_X22,
        HW_X23,
        HW_X24,
        HW_X25,
        HW_X26,
        HW_X27,
        HW_X28,
        HW_X29,
        HW_X30,
        HW_X31,

        HW_COUNT,

        HW_RA    = HW_X1,
        HW_SP    = HW_X2,
        HW_IP    = HW_X5,
        HW_FRAME = HW_X8,
        HW_FLAGS = HW_X9,
    };

    static constexpr byte COND_EQ = 0;
    static constexpr byte COND_NE = 1;
    static constexpr byte COND_CS = 2;
    static constexpr byte COND_CC = 3;
    static constexpr byte COND_MI = 4;
    static constexpr byte COND_PL = 5;
    static constexpr byte COND_VS = 6;
    static constexpr byte COND_VC = 7;
    static constexpr byte COND_HI = 8;
    static constexpr byte COND_LS = 9;
    static constexpr byte COND_GE = 10;
    static constexpr byte COND_LT = 11;
    static constexpr byte COND_GT = 12;
    static constexpr byte COND_LE = 13;
    static constexpr byte COND_AL = 14;

    riscv64reg_t hw(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Convert hwreg_t to riscv64reg_t
    // ------------------------------------------------------------------------
    {
        if (r >= HW_COUNT)
        {
            record(riscv64_error, "Invalid hardware register index %u", r);
            r = HW_X0;
        }
        return riscv64reg_t(r);
    }

    riscv64reg_t hw_local(local_t loc)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local
    // ------------------------------------------------------------------------
    {
        return hw(local(loc));
    }

    static inline uint xn(riscv64reg_t r)
    // ------------------------------------------------------------------------
    //   Physical RV64 GPR number for encoding (0..31)
    // ------------------------------------------------------------------------
    {
        if (r < HW_COUNT)
            return uint(r);
        record(riscv64_error, "xn() on invalid register %u", r);
        return 0;
    }

    static inline byte invert_cond(byte cond)
    {
        static const byte inv[] =
            { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
        return inv[cond & 15];
    }

    int local_disp(int n) const
    // ------------------------------------------------------------------------
    //   Return stack displacement for bytecode local n (ularge_t slots)
    // ------------------------------------------------------------------------
    {
        return ~n * int(sizeof(ularge_t));
    }

    int arg_disp(int n) const
    // ------------------------------------------------------------------------
    //   Return stack displacement for argument (skip saved FP+RA)
    // ------------------------------------------------------------------------
    {
        return n * int(sizeof(ularge_t)) + 2 * int(pointer_size());
    }


    // ========================================================================
    //
    //    RV64 instruction encoders
    //
    // ========================================================================

    static inline uint lui(uint rd, uint imm20)
    // ------------------------------------------------------------------------
    //   Encode lui rd, imm20
    // ------------------------------------------------------------------------
    {
        return 0x00000037u | ((imm20 & 0xfffffu) << 12) | (rd << 7);
    }

    static inline uint auipc(uint rd, uint imm20)
    // ------------------------------------------------------------------------
    //   Encode auipc rd, imm20
    // ------------------------------------------------------------------------
    {
        return 0x00000017u | ((imm20 & 0xfffffu) << 12) | (rd << 7);
    }

    static inline uint addi(uint rd, uint rs1, int imm)
    // ------------------------------------------------------------------------
    //   Encode addi rd, rs1, imm
    // ------------------------------------------------------------------------
    {
        return 0x00000013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15)
               | (rd << 7);
    }

    static inline uint add_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode add rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sub_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x40000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint mul_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode mul rd, rs1, rs2  (M extension)
    // ------------------------------------------------------------------------
    {
        return 0x02000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint div_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode div rd, rs1, rs2  (signed divide)
    // ------------------------------------------------------------------------
    {
        return 0x02004033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint divu_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode divu rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x02005033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint rem_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode rem rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x02006033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint remu_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode remu rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x02007033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint and_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode and rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00007033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint or_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode or rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00006033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint xor_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode xor rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00004033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sll_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode sll rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00001033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint srl_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode srl rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00005033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sra_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode sra rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x40005033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint slt_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode slt rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00002033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sltu_rr(uint rd, uint rs1, uint rs2)
    // ------------------------------------------------------------------------
    //   Encode sltu rd, rs1, rs2
    // ------------------------------------------------------------------------
    {
        return 0x00003033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint slti(uint rd, uint rs1, int imm)
    // ------------------------------------------------------------------------
    //   Encode slti rd, rs1, imm
    // ------------------------------------------------------------------------
    {
        return 0x00002013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15)
               | (rd << 7);
    }

    static inline uint sltiu(uint rd, uint rs1, uint imm)
    // ------------------------------------------------------------------------
    //   Encode sltiu rd, rs1, imm
    // ------------------------------------------------------------------------
    {
        return 0x00003013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15)
               | (rd << 7);
    }

    static inline uint xori(uint rd, uint rs1, uint imm)
    // ------------------------------------------------------------------------
    //   Encode xori rd, rs1, imm
    // ------------------------------------------------------------------------
    {
        return 0x00004013u | ((imm & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint mv_rr(uint rd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode mv rd, rs  (add rd, rs, x0)
    // ------------------------------------------------------------------------
    {
        return add_rr(rd, rs, 0);
    }

    static inline uint slli(uint rd, uint rs1, uint shamt)
    // ------------------------------------------------------------------------
    //   Encode slli rd, rs1, shamt
    // ------------------------------------------------------------------------
    {
        return 0x00001013u | (shamt << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint srli(uint rd, uint rs1, uint shamt)
    // ------------------------------------------------------------------------
    //   Encode srli rd, rs1, shamt
    // ------------------------------------------------------------------------
    {
        return 0x00005013u | (shamt << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint srai(uint rd, uint rs1, uint shamt)
    // ------------------------------------------------------------------------
    //   Encode srai rd, rs1, shamt
    // ------------------------------------------------------------------------
    {
        return 0x40005013u | (shamt << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint neg_rr(uint rd)
    // ------------------------------------------------------------------------
    //   Encode neg rd, rd  (sub rd, x0, rd)
    // ------------------------------------------------------------------------
    {
        return 0x40000033u | (rd << 20) | (rd << 7);
    }

    static inline uint not_rr(uint rd)
    // ------------------------------------------------------------------------
    //   Encode not rd, rd  (xori rd, rd, -1)
    // ------------------------------------------------------------------------
    {
        return 0xfff04013u | (rd << 15) | (rd << 7);
    }

    static inline uint ld_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode ld rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00003003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint sd_off(uint rs2, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode sd rs2, off(rs1)
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x00003023u | ((imm & 0xfe0u) << 20) | ((imm & 0x01fu) << 7)
               | (rs2 << 20) | (rs1 << 15);
    }

    static inline uint lw_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lw rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00002003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint sw_off(uint rs2, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode sw rs2, off(rs1)
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x00002023u | ((imm & 0xfe0u) << 20) | ((imm & 0x01fu) << 7)
               | (rs2 << 20) | (rs1 << 15);
    }

    static inline uint lh_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lh rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00001003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint lhu_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lhu rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00005003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint lwu_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lwu rd, off(rs1) - zero-extend word on RV64
    // ------------------------------------------------------------------------
    {
        return 0x00006003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint lb_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lb rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00000003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint lbu_off(uint rd, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode lbu rd, off(rs1)
    // ------------------------------------------------------------------------
    {
        return 0x00004003u | ((uint32_t(off) & 0xfffu) << 20)
               | (rs1 << 15) | (rd << 7);
    }

    static inline uint sh_off(uint rs2, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode sh rs2, off(rs1)
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x00001023u | ((imm & 0xfe0u) << 20) | ((imm & 0x01fu) << 7)
               | (rs2 << 20) | (rs1 << 15);
    }

    static inline uint sb_off(uint rs2, uint rs1, int off)
    // ------------------------------------------------------------------------
    //   Encode sb rs2, off(rs1)
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x00000023u | ((imm & 0xfe0u) << 20) | ((imm & 0x01fu) << 7)
               | (rs2 << 20) | (rs1 << 15);
    }

    static inline uint ld_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode ld rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return ld_off(rd, rs1, 0);
    }

    static inline uint sd_idx(uint rs2, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode sd rs2, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return sd_off(rs2, rs1, 0);
    }

    static inline uint lw_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lw rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lw_off(rd, rs1, 0);
    }

    static inline uint sw_idx(uint rs2, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode sw rs2, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return sw_off(rs2, rs1, 0);
    }

    static inline uint lh_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lh rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lh_off(rd, rs1, 0);
    }

    static inline uint lhu_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lhu rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lhu_off(rd, rs1, 0);
    }

    static inline uint lwu_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lwu rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lwu_off(rd, rs1, 0);
    }

    static inline uint lb_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lb rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lb_off(rd, rs1, 0);
    }

    static inline uint lbu_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lbu rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lbu_off(rd, rs1, 0);
    }

    static inline uint sh_idx(uint rs2, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode sh rs2, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return sh_off(rs2, rs1, 0);
    }

    static inline uint sb_idx(uint rs2, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode sb rs2, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return sb_off(rs2, rs1, 0);
    }

    static inline uint enc_b_imm(int32_t off, uint rs1, uint rs2, uint funct3)
    // ------------------------------------------------------------------------
    //   Encode B-type branch with byte offset
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x00000063u | (funct3 << 12) | (rs1 << 15) | (rs2 << 20)
               | ((imm & 0x00001000u) << 19)
               | ((imm & 0x000007e0u) << 20)
               | ((imm & 0x0000001eu) << 7)
               | ((imm & 0x00000800u) >> 4);
    }

    static inline uint beq_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode beq rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 0);
    }

    static inline uint bne_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode bne rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 1);
    }

    static inline uint blt_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode blt rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 4);
    }

    static inline uint bge_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode bge rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 5);
    }

    static inline uint bltu_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode bltu rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 6);
    }

    static inline uint bgeu_imm(uint rs1, uint rs2, int32_t off)
    // ------------------------------------------------------------------------
    //   Encode bgeu rs1, rs2, off
    // ------------------------------------------------------------------------
    {
        return enc_b_imm(off, rs1, rs2, 7);
    }

    static inline uint enc_j_imm(int32_t off, uint rd = 0)
    // ------------------------------------------------------------------------
    //   Encode J-type jump with byte offset
    // ------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(off);
        return 0x0000006fu | (rd << 7) | (imm & 0x000ff000u)
               | ((imm & 0x00000800u) << 9)
               | ((imm & 0x000007feu) << 20)
               | ((imm & 0x00100000u) << 11);
    }

    static inline uint j_imm(int32_t off)
    // ------------------------------------------------------------------------
    //   Encode j off  (jal x0, off)
    // ------------------------------------------------------------------------
    {
        return enc_j_imm(off, 0);
    }

    static inline uint jalr(uint rs1)
    // ------------------------------------------------------------------------
    //   Encode jalr x0, rs1, 0
    // ------------------------------------------------------------------------
    {
        return 0x00000067u | (rs1 << 15);
    }

    static inline uint jalr_ra(uint rs1)
    // ------------------------------------------------------------------------
    //   Encode jalr x1, rs1, 0
    // ------------------------------------------------------------------------
    {
        return 0x00000067u | (1u << 7) | (rs1 << 15);
    }

    static inline uint ret_insn()
    // ------------------------------------------------------------------------
    //   Encode ret  (jalr x0, x1, 0)
    // ------------------------------------------------------------------------
    {
        return 0x00008067u;
    }

    static inline uint ebreak_insn()
    // ------------------------------------------------------------------------
    //   Encode ebreak
    // ------------------------------------------------------------------------
    {
        return 0x00100073u;
    }

    static inline uint nop_insn()
    // ------------------------------------------------------------------------
    //   Encode nop  (addi x0, x0, 0)
    // ------------------------------------------------------------------------
    {
        return 0x00000013u;
    }

    static inline uint fmv_w_x(uint fd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode fmv.w.x fd, rs
    // ------------------------------------------------------------------------
    {
        return 0xf0000053u | (rs << 15) | (fd << 7);
    }

    static inline uint fmv_x_w(uint rd, uint fs)
    // ------------------------------------------------------------------------
    //   Encode fmv.x.w rd, fs
    // ------------------------------------------------------------------------
    {
        return 0xe0000053u | (fs << 15) | (rd << 7);
    }

    static inline uint fmv_d_x(uint fd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode fmv.d.x fd, rs  (bitwise int->double, not a conversion)
    // ------------------------------------------------------------------------
    {
        return 0xf2000053u | (rs << 15) | (fd << 7);
    }

    static inline uint fmv_x_d(uint rd, uint fs)
    // ------------------------------------------------------------------------
    //   Encode fmv.x.d rd, fs  (bitwise double->int, not a conversion)
    // ------------------------------------------------------------------------
    {
        return 0xe2000053u | (fs << 15) | (rd << 7);
    }

    static inline uint fadd_s(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fadd.s fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x00000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fsub_s(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fsub.s fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x08000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fmul_s(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fmul.s fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x10000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fdiv_s(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fdiv.s fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x18000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fadd_d(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fadd.d fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x02000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fsub_d(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fsub.d fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x0a000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fmul_d(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fmul.d fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x12000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fdiv_d(uint fd, uint fs1, uint fs2)
    // ------------------------------------------------------------------------
    //   Encode fdiv.d fd, fs1, fs2
    // ------------------------------------------------------------------------
    {
        return 0x1a000053u | (fs2 << 20) | (fs1 << 15) | (fd << 7);
    }

    static inline uint fcvt_d_x(uint fd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode fcvt.d.x fd, rs  (integer to double)
    // ------------------------------------------------------------------------
    {
        return 0xd2200053u | (rs << 15) | (fd << 7);
    }

    static inline uint fcvt_x_d(uint rd, uint fs)
    // ------------------------------------------------------------------------
    //   Encode fcvt.x.d rd, fs  (double to integer)
    // ------------------------------------------------------------------------
    {
        return 0xc2200053u | (fs << 15) | (rd << 7);
    }


    // ========================================================================
    //
    //    Emit helpers
    //
    // ========================================================================

    virtual void    emit_store_abi_word(riscv64reg_t rs2, riscv64reg_t rs1,
                                        int offs);
    virtual void    emit_load_abi_word(riscv64reg_t rd, riscv64reg_t rs1,
                                       int offs);
    virtual void    load_ptr_imm(riscv64reg_t r, uint64_t v);
    virtual void    emit_load_off(riscv64reg_t rd, riscv64reg_t rn, int offs);
    virtual void    emit_store_off(riscv64reg_t rn, riscv64reg_t rt, int offs);
    virtual void    emit_push(riscv64reg_t r);
    virtual void    emit_pop(riscv64reg_t r);

    void            load_local(local_t loc, riscv64reg_t r);
    void            store_local(riscv64reg_t r, local_t loc);
    void            load_uimm32(riscv64reg_t r, uint32_t v);
    void            load_imm64(riscv64reg_t r, uint64_t v);
    void            mov_local(local_t d, local_t s);
    void            swap_locals(local_t d, local_t s);
    void            store_imm(local_t d, value_t v);
    void            store_uimm(local_t d, uvalue_t v);
    void            store_float(local_t d, float v);
    void            store_double(local_t d, double v);
    void            binop_local(local_t d, local_t s, op_t op, byte fpsz=0);
    void            binop_imm(local_t d, value_t v, op_t op);
    void            cmp_local(byte p, local_t d, local_t s, byte cond);
    void            cmp_imm(byte p, local_t d, value_t v, byte cond);
    void            cmp_uimm(byte p, local_t d, uvalue_t v, byte cond);
    void            set_pred(byte p, bool on);
    void            set_pred_cmp(byte p, byte cond);
    void            cond_mov_local(byte p, local_t d, local_t s);
    void            select_local(byte p, local_t d, local_t s, byte x);
    void            neg_local(local_t d);
    void            not_local(local_t d);
    void            incr_local(local_t d);
    void            decr_local(local_t d);
    void            sext_local(local_t d, byte w);
    void            zext_local(local_t d, byte w);
    void            sext_reg(riscv64reg_t rd, riscv64reg_t rn, byte w);
    void            load_signext_mem(riscv64reg_t rd, riscv64reg_t addr, byte w);
    void            store_mem(local_t d, local_t s, size_t sz, value_t off=0);
    void            load_mem(local_t d, local_t s, size_t sz, value_t off=0);
    void            load_sext(local_t d, local_t s, byte w, value_t off=0);
    void            frame_addr(local_t d, local_t s);
    void            bra_pred(byte p, byte_p target, bool sense);
    void            bra_test(local_t s, byte_p target, byte cond);
    void            dbra_local(local_t d, byte_p target);
    void            rel_branch(byte_p target, hwcode_t site);
    void            rel_function(tok_t tok, hwcode_t site);
    void            ijmp_local(local_t s);
    void            icall_local(local_t s);
    void            push_pc();
    void            load_pc(local_t d);
    void            load_pca(local_t d, broff_t b);
    void            get_arg(local_t d, local_t s);
    void            set_arg(local_t d, local_t s);
    void            push_local(local_t s);
    void            pop_local(local_t d);
    void            push_flags();
    void            pop_flags();
    void            store_global(local_t d, local_t s);
    void            load_global(local_t d, local_t s);
    virtual void    prologue();
    virtual void    epilogue();
    virtual void    call_ptr(void (*fn)());
    template <typename T, typename ...U>
    void            call_ptr(T (*fn)(U...)) { return call(((void (*)()) fn)); }
    void            dispatch(local_t loc, byte_p &pc, byte cond);
    void            trap(cstring msg);

protected:
    using relocations_t = std::vector<hwcode_t>;
    relocations_t relocations;
    bool          frame_active = false; // true after op_enter allocates locals
};

#endif // RISCV64_H
