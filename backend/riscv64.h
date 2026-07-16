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
//  REGISTER ALLOCATION CONVENTIONS (RISC-V 64)
//  ===========================================================================
//
//  Bytecode locals are frame[n] slots (uintptr_t).  Locals 0..15 live in
//  shadow registers x10..x25 (xreg = 10+n) while valid; locals >= 16 use
//  frame memory only (load/store through frame pointer).  See regmap.h.
//
//  Register map:
//    Shadow locals     x10 .. x25    frame[0] .. frame[15] (HW_LOC0 = x10)
//    Frame pointer     x8  (HW_FRAME)   rt.frame, primed at JIT entry
//    Flags/predicates  x9  (HW_FLAGS)   rt.flags, primed at entry
//    Scratch value     x28 (HW_SCR_VAL) swap, arithmetic, immediates
//    Scratch address   x29 (HW_SCR_ADDR)
//    Scratch offset    x30 (HW_SCR_OFF)
//    Intra-call IP     x5  (HW_IP)      call_abs target (t0)
//    Return address    x1  (ra)         saved on stack across call_abs
//    Stack pointer     sp (x2)
//
//  a0-a7 (x10-x17) overlap shadow locals 0-7 by design: generated JIT code is
//  not a normal RV callee.  Bytecode PC and runtime follow regmap.h.
//
//  ===========================================================================

#include "backend.h"
#include "regmap.h"
#include "recorder.h"

#include <cstring>

struct riscv64 : backend
// ----------------------------------------------------------------------------
//   RISC-V 64 code generator implementing every built-in opcode
// ----------------------------------------------------------------------------
{
    static backend *make()              { return new riscv64; }

    riscv64();

    cstring machine_defs() const override;
    uint nb_regs() const override;
    uint ptr_size() const override;
    uint reg_iret() const override;

    uint gjmp(uint t) override;
    void gsym_addr(uint t, uint a) override;
    void finish_translation() override;
    void begin_translation() override;
    void patch_branch_site(size_t insn_pos, size_t target_native) override;
    void relocate(void *base) override;
    size_t emit_over_callees_branch() override;
    void emit_user(op_t op, byte_p &pc) override;

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    void patch_imm64_at(void *mem, size_t at, uint r, uint64_t v);

    enum hwreg_t : uint
    {
        HW_IP    = 5,       // t0, intra-procedure call scratch
        HW_FRAME = 8,       // s0, rt.frame base for spill
        HW_FLAGS = 9,       // s1, rt.flags (64-bit predicates)
        HW_LOC0  = 10,      // shadow frame[0] .. frame[15]
        HW_SCR_VAL  = 28,   // t3, scratch: values
        HW_SCR_ADDR = 29,   // scratch: addresses
        HW_SCR_OFF  = 30,   // scratch: offsets
    };

    static inline uint xreg(byte n)
    // ------------------------------------------------------------------------
    //   Map bytecode local n to shadow hardware register (x10+n)
    // ------------------------------------------------------------------------
    {
        return HW_LOC0 + n;
    }

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
        return 0x00000013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
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
        return 0x00001013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sltiu(uint rd, uint rs1, uint imm)
    // ------------------------------------------------------------------------
    //   Encode sltiu rd, rs1, imm
    // ------------------------------------------------------------------------
    {
        return 0x00003013u | ((uint32_t(imm) & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
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
        return 0xfff04013u | (rd << 15) | (rd << 7);  // funct3=4 = XORI
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

    static inline uint lb_idx(uint rd, uint rs1)
    // ------------------------------------------------------------------------
    //   Encode lb rd, 0(rs1)
    // ------------------------------------------------------------------------
    {
        return lb_off(rd, rs1, 0);
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
        return 0x00008067u | (rs1 << 15);
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

    static inline uint reload_frame()
    // ------------------------------------------------------------------------
    //   Encode mv HW_FRAME, x10  (frame pointer returned in a0)
    // ------------------------------------------------------------------------
    {
        return mv_rr(HW_FRAME, 10);
    }

    static inline byte cond_from_op(cstring op)
    // ------------------------------------------------------------------------
    //   Map comparison name to AArch64-style condition code (for set_pred_cmp)
    // ------------------------------------------------------------------------
    {
        if (!strcmp(op, "eq")) return 0;
        if (!strcmp(op, "ne")) return 1;
        if (!strcmp(op, "lt")) return 11;
        if (!strcmp(op, "le")) return 13;
        if (!strcmp(op, "ltu")) return 3;
        if (!strcmp(op, "leu")) return 9;
        return 0;
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
        return 0xe0000053u | (fs << 15) | (rd << 7);  // funct3=0
    }

    static inline uint fmv_d_x(uint fd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode fmv.d.x fd, rs  (bitwise int→double, not a conversion)
    // ------------------------------------------------------------------------
    {
        return 0xf2000053u | (rs << 15) | (fd << 7);
    }

    static inline uint fmv_x_d(uint rd, uint fs)
    // ------------------------------------------------------------------------
    //   Encode fmv.x.d rd, fs  (bitwise double→int, not a conversion)
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
        return 0xc2200253u | (fs << 15) | (rd << 7);
    }

    void emit32le(uint32_t insn);
    void load_local(byte n, uint r);
    void store_local(uint r, byte n);
    void load_uimm32(uint r, uint32_t v);
    void load_imm64(uint r, uint64_t v);
    void call_abs(void *fn);
    void spill_frame();
    void reload_frame_reg();
    void reload_shadows();
    void reload_flags_reg();
    void set_pred_cmp(byte p, byte cond);
    void touch_local(byte d);
    void mov_local(byte d, byte s);
    void swap_locals(byte d, byte s);
    void store_imm(byte d, value_t v);
    void store_uimm(byte d, uvalue_t v);
    void store_float(byte d, float v);
    void store_double(byte d, double v);
    void binop_local(byte d, byte s, cstring op);
    void binop_imm(byte d, value_t v, cstring op);
    void cmp_local(byte p, byte d, byte s, cstring op);
    void cmp_imm(byte p, byte d, value_t v, cstring op);
    void cmp_uimm(byte p, byte d, uvalue_t v, cstring op);
    void set_pred(byte p, bool on);
    void cond_mov_local(byte p, byte d, byte s);
    void select_local(byte p, byte d, byte s, byte x);
    void neg_local(byte d);
    void not_local(byte d);
    void incr_local(byte d);
    void decr_local(byte d);
    void sext_local(byte d, byte w);
    void zext_local(byte d, byte w);
    void sext_reg(uint rd, uint rn, byte w);
    void load_signext_mem(uint rd, uint addr_rn, byte w);
    void store_mem(byte d, byte s, uint sz);
    void load_mem(byte d, byte s, uint sz);
    void store_mem_off(byte d, byte s, value_t o, uint sz);
    void load_mem_off(byte d, byte s, value_t o, uint sz);
    void load_signext(byte d, byte s, byte w);
    void load_signext_off(byte d, byte s, byte w, value_t o);
    void frame_addr(byte d, byte s);
    void bra_offset(broff_t b);
    void bra_pred(byte p, broff_t b, bool sense);
    void bra_zero(byte s, broff_t b, bool eq);
    void bra_signed(byte s, broff_t b, byte cond);
    void dbra_local(byte d, broff_t b);
    void jmp_tok(tok_t t);
    void call_tok(tok_t t);
    void ijmp_local(byte s);
    void icall_local(byte s);
    void push_pc();
    void load_pc(byte d);
    void load_pca(byte d, broff_t b);
    void get_arg(byte d, byte s);
    void set_arg(byte d, byte s);
    void push_local(byte s);
    void pop_local(byte d);
    void push_flags();
    void pop_flags();
    void store_global(byte d, byte s);
    void load_global(byte d, byte s);
    void do_ret();
    void trap(cstring msg);
    void rtcall(cstring fn);
    void rtcall(cstring fn, tok_t a);
    void rtcall(cstring fn, byte a);
    void br_via_ip();
    void patch_pc_rel(size_t auipc_at, uint rd, size_t target);
    size_t emit_return_addr_load(uint rd);
    void fix_return_addr(size_t imm_at, uint rd, size_t target);

    struct pc_rel_fixup { size_t at; uint rd; size_t target; };
    std::vector<pc_rel_fixup> pc_rel_fixups;
    struct return_link_fixup { size_t imm_at; uint rd; size_t target; };
    std::vector<return_link_fixup> return_link_fixups;

    bool spill_arg_regs = true;
    bool frame_memory_touched = false;
};

RECORDER_DECLARE(riscv64);
RECORDER_DECLARE(riscv64_ops);
RECORDER_DECLARE(riscv64_error);
RECORDER_DECLARE(riscv64_warning);

#endif // RISCV64_H
