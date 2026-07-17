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
//  Bytecode locals are frame[n] slots (ularge_t, 8 bytes).  Locals 0..3 live
//  in shadow registers r0..r3 while valid; locals >= 4 use frame memory only
//  (ldr/str via [r11, #local*8]).  See regmap.h.
//
//  TinyCC allocates r0-r3 and r12 for expression temps; this backend shadows
//  only r0-r3 (syscall/arg regs) and keeps r4-r7 as dedicated scratches so
//  temps never silently clobber live locals (BACKEND-REVIEW §6.11).
//
//  Register map:
//    Shadow locals     r0 .. r3      frame[0] .. frame[3] when valid
//    Scratch value     r4  (HW_SCR_VAL)
//    Scratch offset    r5  (HW_SCR_OFF)
//    (free callee-saved r6, r7 — reserved for future temps)
//    Scratch address   r8  (HW_SCR_ADDR)
//    Frame pointer     r11 (HW_FRAME)  rt.frame (reloaded in prologue)
//    Flags/predicates  r10 (HW_FLAGS)   rt.flags low half (reloaded in prologue)
//    Callee-saved pack  r4-r11, lr      pushed in prologue; pop {r4-r11,pc}
//    Intra-call IP     r12 (HW_IP)      call_abs target (AAPCS IP)
//    Stack pointer     r13 (sp)         native stack (8-byte aligned at calls)
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
    size_t emit_over_callees_branch() override;
    void emit_user(op_t op, byte_p &pc) override;

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    enum hwreg_t : uint
    {
        HW_LOC0  = 0,       // shadow frame[0] .. frame[3]
        HW_LOC3  = 3,
        HW_SCR_VAL  = 4,    // dedicated scratch (not a shadow)
        HW_SCR_OFF  = 5,    // dedicated scratch (not a shadow)
        HW_SCR_ADDR = 8,
        HW_SB    = 9,       // leave r9 alone (platform/PIC sb on some ABIs)
        HW_FLAGS = 10,      // callee-saved, rt.flags (low 32 of predicates)
        HW_FRAME = 11,      // callee-saved fp, rt.frame base for spill
        HW_IP    = 12,      // intra-procedure call scratch (AAPCS IP)
    };

    static const uint ARM_SHADOW_COUNT = 4;
    static const uint ARM_SLOT_SIZE = 8; // sizeof(ularge_t)

    static inline uint slot_off(byte n)
    // ------------------------------------------------------------------------
    //   Byte offset of bytecode local n in the runtime frame
    // ------------------------------------------------------------------------
    {
        return uint(n) * ARM_SLOT_SIZE;
    }

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

    static inline uint ands_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode ands rd, rn, rm  (sets NZCV for following BEQ/BNE)
    // ------------------------------------------------------------------------
    {
        return 0xE0100000u | (rd << 12) | (rn << 16) | rm;
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
    //   Encode mov rd, rm, shift-by-rs (LSL=0, LSR=1, ASR=2).  Bit 4 selects
    //   the register-controlled form; without it this is lsl #imm (rs<<1).
    // ------------------------------------------------------------------------
    {
        return 0xE1A00000u | (rd << 12) | (rs << 8) | (stype << 5)
             | (1u << 4) | rm;
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

    static inline uint ldrb_off(uint rd, uint rn, uint off)
    // ------------------------------------------------------------------------
    //   Encode ldrb rd, [rn, #off]  (zero-extends)
    // ------------------------------------------------------------------------
    {
        return 0xE5D00000u | (rd << 12) | (rn << 16) | (off & 0xFFFu);
    }

    static inline uint strb_off(uint rd, uint rn, uint off)
    // ------------------------------------------------------------------------
    //   Encode strb rd, [rn, #off]
    // ------------------------------------------------------------------------
    {
        return 0xE5C00000u | (rd << 12) | (rn << 16) | (off & 0xFFFu);
    }

    static inline uint ldrh_off(uint rd, uint rn, uint imm8)
    // ------------------------------------------------------------------------
    //   Encode ldrh rd, [rn, #imm8]  (zero-extends; imm8 only)
    // ------------------------------------------------------------------------
    {
        return 0xE1D000B0u | (rn << 16) | (rd << 12)
             | ((imm8 & 0xF0u) << 4) | (imm8 & 0x0Fu);
    }

    static inline uint strh_off(uint rd, uint rn, uint imm8)
    // ------------------------------------------------------------------------
    //   Encode strh rd, [rn, #imm8]
    // ------------------------------------------------------------------------
    {
        return 0xE1C000B0u | (rn << 16) | (rd << 12)
             | ((imm8 & 0xF0u) << 4) | (imm8 & 0x0Fu);
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

    static inline uint push_callee_lr()
    // ------------------------------------------------------------------------
    //   Encode push {r4-r11, lr}  (stmfd sp!, ...). Caller must then
    //   sub sp,#4 so the stack stays 8-byte aligned for AAPCS C calls.
    // ------------------------------------------------------------------------
    {
        return 0xE92D4FF0u;
    }

    static inline uint pop_callee_pc()
    // ------------------------------------------------------------------------
    //   Encode pop {r4-r11, pc}  (ldmfd sp!, ...). Caller must add sp,#4
    //   first to undo the prologue alignment pad.
    // ------------------------------------------------------------------------
    {
        return 0xE8BD8FF0u;
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
    //   Encode vmov Sn, Rt  (ARM core → single). N is bit 7, Vn = Sn[4:1].
    // ------------------------------------------------------------------------
    {
        return 0xEE000A10u | (rt << 12)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7);
    }

    static inline uint vmov_r_s(uint rt, uint sn)
    // ------------------------------------------------------------------------
    //   Encode vmov Rt, Sn  (single → ARM core). N is bit 7, Vn = Sn[4:1].
    // ------------------------------------------------------------------------
    {
        return 0xEE100A10u | (rt << 12)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7);
    }

    static inline uint vfp_ss(uint base, uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode single-precision VFP 3-operand op (Sd, Sn, Sm).
    //   Vd/Vn/Vm = Sx[4:1]; D/N/M = Sx[0] at bits 22/7/5.
    // ------------------------------------------------------------------------
    {
        return base
             | ((sd >> 1) << 12) | ((sd & 1u) << 22)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7)
             | ((sm >> 1) << 0)  | ((sm & 1u) << 5);
    }

    static inline uint vadd_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vadd.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return vfp_ss(0xEE300A00u, sd, sn, sm);
    }

    static inline uint vsub_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vsub.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return vfp_ss(0xEE300A40u, sd, sn, sm);
    }

    static inline uint vmul_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vmul.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return vfp_ss(0xEE200A00u, sd, sn, sm);
    }

    static inline uint vdiv_ss(uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode vdiv.f32 sd, sn, sm
    // ------------------------------------------------------------------------
    {
        return vfp_ss(0xEE800A00u, sd, sn, sm);
    }

    static inline uint vldr_d(uint dd, uint rn, uint byte_off)
    // ------------------------------------------------------------------------
    //   Encode vldr dd, [rn, #byte_off]  (byte_off multiple of 4, 0..1020)
    // ------------------------------------------------------------------------
    {
        return 0xED900B00u | (rn << 16) | ((dd & 0xFu) << 12)
             | ((dd & 0x10u) << 18) | ((byte_off >> 2) & 0xFFu);
    }

    static inline uint vstr_d(uint dd, uint rn, uint byte_off)
    // ------------------------------------------------------------------------
    //   Encode vstr dd, [rn, #byte_off]
    // ------------------------------------------------------------------------
    {
        return 0xED800B00u | (rn << 16) | ((dd & 0xFu) << 12)
             | ((dd & 0x10u) << 18) | ((byte_off >> 2) & 0xFFu);
    }

    static inline uint vfp_dd(uint base, uint dd, uint dn, uint dm)
    // ------------------------------------------------------------------------
    //   Encode double-precision VFP 3-operand op (Dd, Dn, Dm), Dd/Dn/Dm < 16
    // ------------------------------------------------------------------------
    {
        return base
             | ((dd & 0xFu) << 12) | ((dd & 0x10u) << 18)
             | ((dn & 0xFu) << 16) | ((dn & 0x10u) << 3)
             | ((dm & 0xFu) << 0)  | ((dm & 0x10u) << 1);
    }

    static inline uint vadd_dd(uint dd, uint dn, uint dm)
    {
        return vfp_dd(0xEE300B00u, dd, dn, dm);
    }

    static inline uint vsub_dd(uint dd, uint dn, uint dm)
    {
        return vfp_dd(0xEE300B40u, dd, dn, dm);
    }

    static inline uint vmul_dd(uint dd, uint dn, uint dm)
    {
        return vfp_dd(0xEE200B00u, dd, dn, dm);
    }

    static inline uint vdiv_dd(uint dd, uint dn, uint dm)
    {
        return vfp_dd(0xEE800B00u, dd, dn, dm);
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
    void sext_reg(uint rd, uint rn, byte w);
    void sext_local(byte d, byte w);
    void zext_local(byte d, byte w);
    void add_addr_off(uint addr_r, value_t o);
    void emit_store_sized(uint val_r, uint addr_r, uint sz);
    void emit_load_sized(uint dst_r, uint addr_r, uint sz);
    void store_local64(uint lo_r, uint hi_r, byte d);
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
    bool frame_memory_touched = false;
};

RECORDER_DECLARE(arm);
RECORDER_DECLARE(arm_ops);
RECORDER_DECLARE(arm_error);
RECORDER_DECLARE(arm_warning);

#endif // ARM_H
