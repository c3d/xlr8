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
//  frame memory only (load/store through frame pointer, to be implemented).
//  See regmap.h.
//
//  Register map:
//    Shadow locals     x10 .. x25    frame[0] .. frame[15] (HW_LOC0 = x10)
//    Frame pointer     x8  (HW_FRAME)   rt.frame (reserved, not yet primed)
//    Flags/predicates  x9  (HW_FLAGS)   rt.flags (reserved)
//    Scratch value     x28 (HW_SCR_VAL) swap and arithmetic temps
//    Scratch address   x29 (HW_SCR_ADDR)
//    Scratch offset    x30 (HW_SCR_OFF)
//    Stack pointer     sp (x2)
//
//  a0-a7 (x10-x17) overlap shadow locals 0-7 by design: generated JIT code is
//  not a normal RV callee.  Bytecode PC and runtime pointer follow the shared
//  model in regmap.h.
//
//  ===========================================================================

#include "backend.h"
#include "regmap.h"
#include "recorder.h"


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

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    enum hwreg_t : uint
    {
        HW_LOC0  = 10,      // shadow frame[0] .. frame[15] (x10+x)
        HW_FRAME = 8,       // s0, rt.frame (reserved)
        HW_FLAGS = 9,       // s1, rt.flags (reserved)
        HW_SCR_VAL  = 28,   // t3, swap and arithmetic temps
        HW_SCR_ADDR = 29,   // address scratch (reserved)
        HW_SCR_OFF  = 30,   // offset scratch (reserved)
    };

    static inline uint xreg(byte n)
    // ----------------------------------------------------------------------------
    //   Map bytecode local n to shadow hardware register (x10+n)
    // ----------------------------------------------------------------------------
    {
        return HW_LOC0 + n;
    }

    static inline uint add_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode add rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x00000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sub_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode sub rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x40000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint mul_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode mul rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x02000033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint and_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode and rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x00007033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint or_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode or rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x00006033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint xor_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode xor rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x00004033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sll_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode sll rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x00001033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint srl_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode srl rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x40005033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint sra_rr(uint rd, uint rs1, uint rs2)
    // ----------------------------------------------------------------------------
    //   Encode sra rd, rs1, rs2
    // ----------------------------------------------------------------------------
    {
        return 0x60005033u | (rs2 << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint mv_rr(uint rd, uint rs)
    // ----------------------------------------------------------------------------
    //   Encode mv rd, rs  (add rd, rs, x0)
    // ----------------------------------------------------------------------------
    {
        return add_rr(rd, rs, 0);
    }

    static inline uint addi(uint rd, uint rs1, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode addi rd, rs1, imm
    // ----------------------------------------------------------------------------
    {
        return 0x00000013u | ((imm & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint slti(uint rd, uint rs1, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode slti rd, rs1, imm
    // ----------------------------------------------------------------------------
    {
        return 0x00001013u | ((imm & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint subi(uint rd, uint rs1, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode subi rd, rs1, imm
    // ----------------------------------------------------------------------------
    {
        return 0x40000013u | ((imm & 0xfffu) << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint slli(uint rd, uint rs1, uint shamt)
    // ----------------------------------------------------------------------------
    //   Encode slli rd, rs1, shamt
    // ----------------------------------------------------------------------------
    {
        return 0x00001013u | (shamt << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint srli(uint rd, uint rs1, uint shamt)
    // ----------------------------------------------------------------------------
    //   Encode srli rd, rs1, shamt
    // ----------------------------------------------------------------------------
    {
        return 0x00005013u | (shamt << 20) | (rs1 << 15) | (rd << 7);
    }

    static inline uint neg_rr(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode neg rd, rd  (sub rd, x0, rd)
    // ----------------------------------------------------------------------------
    {
        return 0x40000033u | (rd << 20) | (rd << 7);
    }

    static inline uint not_rr(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode not rd, rd  (xori rd, rd, -1)
    // ----------------------------------------------------------------------------
    {
        return 0x00007013u | (rd << 15) | (rd << 7);
    }

    static inline uint lw(uint rd, uint rs1)
    // ----------------------------------------------------------------------------
    //   Encode lw rd, 0(rs1)
    // ----------------------------------------------------------------------------
    {
        return 0x00003003u | (rs1 << 20) | (rd << 7);
    }

    static inline uint sw(uint rs2, uint rs1)
    // ----------------------------------------------------------------------------
    //   Encode sw rs2, 0(rs1)
    // ----------------------------------------------------------------------------
    {
        return 0x00002023u | (rs2 << 20) | (rs1 << 15);
    }

    static inline uint jalr(uint rs1)
    // ----------------------------------------------------------------------------
    //   Encode jalr x0, rs1, 0
    // ----------------------------------------------------------------------------
    {
        return 0x00008067u | (rs1 << 15);
    }

    static inline uint ret_insn()
    // ----------------------------------------------------------------------------
    //   Encode ret  (jalr x0, x1, 0)
    // ----------------------------------------------------------------------------
    {
        return 0x00008067u;
    }

    static inline uint ebreak_insn()
    // ----------------------------------------------------------------------------
    //   Encode ebreak
    // ----------------------------------------------------------------------------
    {
        return 0x00100073u;
    }

    static inline uint j_imm(uint offset)
    // ----------------------------------------------------------------------------
    //   Encode j offset  (unconditional jump placeholder)
    // ----------------------------------------------------------------------------
    {
        return 0x0000006fu | ((offset & 0xfffff) << 12);
    }

    void emit32le(uint32_t insn);
    void mov_local(byte d, byte s);
    void swap_locals(byte d, byte s);
    void store_imm(byte d, value_t v);
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
    void bra_signed(byte s, broff_t b);
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
};

RECORDER_DECLARE(riscv64);
RECORDER_DECLARE(riscv64_ops);
RECORDER_DECLARE(riscv64_error);
RECORDER_DECLARE(riscv64_warning);

#endif // RISCV64_H
