#ifndef ARM_H
#define ARM_H
// ****************************************************************************
//  arm.h                                                         XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     32-bit ARM (AArch32 / AAPCS) code generation backend for all built-in
//     opcodes.  Instruction encoding follows the ARM mode used by TinyCC.
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
//  REGISTER ALLOCATION CONVENTIONS (32-bit ARM / AArch32)
//  ===========================================================================
//
//  Bytecode locals are frame[n] slots (uintptr_t, 4 bytes).  Locals 0..7 live
//  in shadow registers r0..r7 while valid; locals 8..15 and >= 16 use frame
//  memory only (ldr/str via [r9, #local*4]).  See regmap.h.
//
//  TinyCC allocates r0-r3 and r12 for expression temps; this backend keeps
//  r0-r7 as shadows for the first eight locals (matching enter(8)) and still
//  uses r0-r1 as syscall argument registers before sys.
//
//  Register map:
//    Shadow locals     r0 .. r7      frame[0] .. frame[7] when valid
//    Scratch address   r8  (HW_SCR_ADDR)
//    Frame pointer     r9  (HW_FRAME)   rt.frame, primed at JIT entry
//    Flags/predicates  r10 (HW_FLAGS)   rt.flags (64-bit), primed at entry
//    Saved link        r11 (HW_LINK)    LR across runtime calls
//    Intra-call IP     r12 (HW_IP)      call_abs target (AAPCS IP)
//    Stack pointer     r13 (sp)         native stack
//    Link register     r14 (lr)         restored from r11 on return
//
//  Scratch value/offset reuse r0-r3 during operations that do not clobber live
//  shadow locals (HW_SCR_VAL = r0, HW_SCR_OFF = r2).
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() today.
//
//  ===========================================================================

#include "backend.h"
#include "recorder.h"
#include "regmap.h"

#include <cstring>


struct arm : backend
// ----------------------------------------------------------------------------
//   32-bit ARM code generator implementing every built-in opcode
// ----------------------------------------------------------------------------
{
    static backend *make()              { return new arm; }

    arm();

    cstring machine_defs() const override;
    uint nb_regs() const override;
    uint ptr_size() const override;
    uint reg_iret() const override;

    uint gjmp(uint t) override;
    void gsym_addr(uint t, uint a) override;
    void finish_translation() override;
    void begin_translation() override;
    void patch_branch_site(size_t insn_pos, size_t target_native) override;

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    enum hwreg_t : uint
    {
        HW_LOC0  = 0,       // shadow frame[0] .. frame[7]
        HW_LOC7  = 7,
        HW_SCR_ADDR = 8,
        HW_FRAME = 9,       // callee-saved, rt.frame base for spill
        HW_FLAGS = 10,      // callee-saved, rt.flags (64-bit predicates)
        HW_LINK  = 11,      // callee-saved, saved link register
        HW_IP    = 12,      // intra-procedure call scratch (AAPCS IP)
        HW_SCR_VAL  = 0,    // shares r0 with local 0 / syscall arg
        HW_SCR_OFF  = 2,    // shares r2 with local 2
    };

    static const uint ARM_SHADOW_COUNT = 8;

    static inline uint rreg(byte n)
    // ------------------------------------------------------------------------
    //   Map bytecode local n to shadow hardware register rn
    // ------------------------------------------------------------------------
    {
        return n;
    }

    static inline uint mov_rr(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode mov rd, rm
    // ------------------------------------------------------------------------
    {
        return 0xE1A00000u | (rd << 12) | rm;
    }

    static inline uint add_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode add rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE0800000u | (rd << 12) | (rn << 16) | rm;
    }

    static inline uint sub_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE0400000u | (rd << 12) | (rn << 16) | rm;
    }

    static inline uint and_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode and rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE0000000u | (rd << 12) | (rn << 16) | rm;
    }

    static inline uint orr_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode orr rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE1800000u | (rd << 12) | (rn << 16) | rm;
    }

    static inline uint orr_rrr_cond(byte cond, uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode orr{cond} rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return (uint32_t(cond) << 28) | 0x01800000u
             | (rd << 12) | (rn << 16) | rm;
    }

    static inline byte cond_from_op(cstring op)
    // ------------------------------------------------------------------------
    //   Map comparison name to ARM condition code
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

    static inline uint eor_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode eor rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE0200000u | (rd << 12) | (rn << 16) | rm;
    }

    static inline uint mul_rrr(uint rd, uint rm, uint rs)
    // ------------------------------------------------------------------------
    //   Encode mul rd, rm, rs
    // ------------------------------------------------------------------------
    {
        return 0xE0000090u | (rd << 16) | (rs << 8) | rm;
    }

    static inline uint sdiv_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sdiv rd, rn, rm  (ARMv7 AE / hwdiv-arm)
    // ------------------------------------------------------------------------
    {
        return 0xE710F010u | (rd << 16) | (rm << 8) | rn;
    }

    static inline uint udiv_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode udiv rd, rn, rm  (ARMv7 AE / hwdiv-arm)
    // ------------------------------------------------------------------------
    {
        return 0xE730F010u | (rd << 16) | (rm << 8) | rn;
    }

    static inline uint mls_rrrr(uint rd, uint rn, uint rm, uint ra)
    // ------------------------------------------------------------------------
    //   Encode mls rd, rn, rm, ra  (rd = ra - rn*rm)
    // ------------------------------------------------------------------------
    {
        return 0xE0600090u | (rd << 16) | (ra << 12) | (rm << 8) | rn;
    }

    static inline uint shift_rrr(uint rd, uint rm, uint rs, uint stype)
    // ------------------------------------------------------------------------
    //   Encode mov rd, rm, shift-by-rs (LSL=0, LSR=1, ASR=2)
    // ------------------------------------------------------------------------
    {
        return 0xE1B00000u | (rd << 12) | (rs << 8) | (stype << 5) | rm;
    }

    static inline uint shift_imm(uint rd, uint rm, uint imm, uint stype)
    // ------------------------------------------------------------------------
    //   Encode mov rd, rm, shift-#imm (LSL=0, LSR=1, ASR=2)
    // ------------------------------------------------------------------------
    {
        return 0xE1A00000u | (rd << 12) | ((imm & 31u) << 7)
             | (stype << 5) | rm;
    }

    static inline uint cmp_rr(uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode cmp rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xE1500000u | (rn << 16) | rm;
    }

    static inline uint add_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode add rd, rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xE2800000u | (rd << 12) | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint sub_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xE2400000u | (rd << 12) | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint cmp_r_imm(uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode cmp rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xE3500000u | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint ldr_off(uint rd, uint rn, uint off)
    // ------------------------------------------------------------------------
    //   Encode ldr rd, [rn, #off]
    // ------------------------------------------------------------------------
    {
        return 0xE5900000u | (rd << 12) | (rn << 16) | (off & 0xFFFu);
    }

    static inline uint str_off(uint rd, uint rn, uint off)
    // ------------------------------------------------------------------------
    //   Encode str rd, [rn, #off]
    // ------------------------------------------------------------------------
    {
        return 0xE5800000u | (rd << 12) | (rn << 16) | (off & 0xFFFu);
    }

    static inline uint movw(uint rd, uint imm16)
    // ------------------------------------------------------------------------
    //   Encode movw rd, #imm16
    // ------------------------------------------------------------------------
    {
        return 0xE3000000u | (rd << 12) | (imm16 & 0xFFFu)
             | ((imm16 & 0xF000u) << 4);
    }

    static inline uint movt(uint rd, uint imm16)
    // ------------------------------------------------------------------------
    //   Encode movt rd, #imm16
    // ------------------------------------------------------------------------
    {
        return 0xE3400000u | (rd << 12) | (imm16 & 0xFFFu)
             | ((imm16 & 0xF000u) << 4);
    }

    static inline uint blx(uint rm)
    // ------------------------------------------------------------------------
    //   Encode blx rm
    // ------------------------------------------------------------------------
    {
        return 0xE12FFF30u | rm;
    }

    static inline uint bx_lr()
    // ------------------------------------------------------------------------
    //   Encode bx lr
    // ------------------------------------------------------------------------
    {
        return 0xE12FFF1Eu;
    }

    static inline uint bx_reg(uint rm)
    // ------------------------------------------------------------------------
    //   Encode bx rm
    // ------------------------------------------------------------------------
    {
        return 0xE12FFF10u | (rm & 0xFu);
    }

    static inline uint b_imm(int offset)
    // ------------------------------------------------------------------------
    //   Encode b #offset  (ARM PC = insn_addr + 8)
    // ------------------------------------------------------------------------
    {
        return 0xEA000000u | (uint32_t(offset) & 0x00FFFFFFu);
    }

    static inline uint b_cond_imm(byte cond, int offset)
    // ------------------------------------------------------------------------
    //   Encode b{cond} #offset
    // ------------------------------------------------------------------------
    {
        return (uint32_t(cond) << 28) | 0x0A000000u
             | (uint32_t(offset) & 0x00FFFFFFu);
    }

    static inline uint vmov_s_r(uint sn, uint rt)
    // ------------------------------------------------------------------------
    //   Encode vmov.f32 sn, rt
    // ------------------------------------------------------------------------
    {
        return 0xEE000A10u | (rt << 12) | ((sn & 1u) << 22) | ((sn >> 1) << 16);
    }

    static inline uint vmov_r_s(uint rt, uint sn)
    // ------------------------------------------------------------------------
    //   Encode vmov.f32 rt, sn
    // ------------------------------------------------------------------------
    {
        return 0xEE100A10u | (rt << 12) | ((sn & 1u) << 22) | ((sn >> 1) << 16);
    }

    static inline uint vadd_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vadd.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return 0xEE300A00u | (sd << 12) | (sn << 16) | (sm << 20)
             | ((sd & 1u) << 6) | ((sn & 1u) << 7) | ((sm & 1u) << 5);
    }

    static inline uint vsub_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vsub.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return 0xEE300A40u | (sd << 12) | (sn << 16) | (sm << 20)
             | ((sd & 1u) << 6) | ((sn & 1u) << 7) | ((sm & 1u) << 5);
    }

    static inline uint vmul_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vmul.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return 0xEE200A00u | (sd << 12) | (sn << 16) | (sm << 20)
             | ((sd & 1u) << 6) | ((sn & 1u) << 7) | ((sm & 1u) << 5);
    }

    static inline uint vdiv_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vdiv.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return 0xEE800A00u | (sd << 12) | (sn << 16) | (sm << 20)
             | ((sd & 1u) << 6) | ((sn & 1u) << 7) | ((sm & 1u) << 5);
    }

    void emit32le(uint32_t insn);
    void load_local(byte n, uint r);
    void store_local(uint r, byte n);
    void load_imm32(uint r, uint32_t v);
    void call_abs(void *fn);
    void spill_frame();
    void reload_frame_reg();
    void reload_shadows();
    void reload_flags_reg();
    void touch_local(byte d);
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
    void set_pred_cmp(byte p, byte cond);
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
    void br_via_ip();
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

    bool spill_arg_regs = true;
};

RECORDER_DECLARE(arm);
RECORDER_DECLARE(arm_ops);
RECORDER_DECLARE(arm_error);
RECORDER_DECLARE(arm_warning);

#endif // ARM_H
