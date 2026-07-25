#ifndef ARM_H
#define ARM_H
// ****************************************************************************
//  arm.h                                                         XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     32-bit ARM (AArch32 / AAPCS, ARM mode) code generation backend.
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2026 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt file
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
//  Locals use [r11 - (local+1)*8]; r11 is adjusted by op_enter and op_leave.
//  Args use [r11 + arg*8 + 8] (skip saved FP+LR).
//  Flags (predicates) live in r10 (HW_FLAGS).
//
//  Register map:
//
//    Frame pointer     r11 (HW_FRAME)   Native FP for locals / args
//    Link register     r14 (HW_LR)      Saved with FP on enter
//    Flags/predicates  r10 (HW_FLAGS)   Callee-saved predicate bits
//    Scratch           r12 (HW_IP)      Temps / call targets (not allocatable)
//    Allocatable GPRs  r4-r8            Callee-saved only (survive sys calls)
//                      r0-r3,r12-r15    Call-clobbered / reserved (not alloc)
//                      r9               Avoid on some ABIs
//    Stack pointer     r13 (HW_SP)      Native stack
//
//  Bytecode PC is the translator cursor, not a fixed HW register.
//  Runtime is reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "backend.h"
#include "recorder.h"
#include "regmap.h"


RECORDER_DECLARE(arm);
RECORDER_DECLARE(arm_ops);
RECORDER_DECLARE(arm_error);
RECORDER_DECLARE(arm_warning);


struct arm : backend
// ----------------------------------------------------------------------------
//   32-bit ARM code generator implementing every opcode
// ----------------------------------------------------------------------------
{
    arm(runtime &rt);
    static backend *make(runtime &rt) { return new arm(rt); }

#include "backend-iface.h"

protected:
    enum armreg_t : hwreg_t
    // ------------------------------------------------------------------------
    //    Hardware register names for AArch32 ARM mode
    // ------------------------------------------------------------------------
    {
        HW_R0,
        HW_R1,
        HW_R2,
        HW_R3,
        HW_R4,
        HW_R5,
        HW_R6,
        HW_R7,
        HW_R8,
        HW_R9,
        HW_R10,
        HW_R11,
        HW_R12,
        HW_R13,
        HW_R14,
        HW_R15,

        HW_COUNT,

        HW_FRAME = HW_R11,
        HW_FLAGS = HW_R10,
        HW_IP    = HW_R12,
        HW_SP    = HW_R13,
        HW_LR    = HW_R14,
        HW_PC    = HW_R15,
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

    armreg_t hw(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Convert hwreg_t to armreg_t
    // ------------------------------------------------------------------------
    {
        if (r >= HW_COUNT)
        {
            record(arm_error, "Invalid hardware register index %u", r);
            r = HW_R0;
        }
        return armreg_t(r);
    }

    armreg_t hw_local(local_t loc)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local
    // ------------------------------------------------------------------------
    {
        return hw(local(loc));
    }

    static inline uint rn(armreg_t r)
    // ------------------------------------------------------------------------
    //   Physical ARM register number for encoding (0..15)
    // ------------------------------------------------------------------------
    {
        if (r < HW_COUNT)
            return uint(r);
        record(arm_error, "rn() on invalid register %u", r);
        return 0;
    }

    static inline byte invert_cond(byte cond)
    {
        static const byte inv[] =
            { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 14, 15 };
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
    //   Displacement from FP to argument n (enter FP+LR + prologue)
    // ------------------------------------------------------------------------
    {
        return n * int(sizeof(ularge_t))
             + 2 * int(pointer_size())
             + int(PROLOGUE_SAVE_BYTES);
    }

    // push {r4-r8, r10, fp, lr} — 8 registers × 4 bytes
    static constexpr uint PROLOGUE_SAVE_BYTES = 32;


    // ========================================================================
    //
    //    AArch32 ARM-mode instruction encoders
    //
    // ========================================================================

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
    //   Encode ands rd, rn, rm  (sets NZCV)
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

    static inline uint bic_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode bic rd, rn, rm  (rd = rn AND NOT rm)
    // ------------------------------------------------------------------------
    {
        return 0xE1C00000u | (rd << 12) | (rn << 16) | rm;
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

    static inline uint mvn_rr(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode mvn rd, rm  (bitwise NOT)
    // ------------------------------------------------------------------------
    {
        return 0xE1E00000u | (rd << 12) | rm;
    }

    static inline uint rsb_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode rsb rd, rn, #imm  (rd = imm - rn; use imm=0 for negate)
    // ------------------------------------------------------------------------
    {
        return 0xE2600000u | (rd << 12) | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint tst_rr(uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode tst rn, rm  (AND, set flags, discard result)
    // ------------------------------------------------------------------------
    {
        return 0xE1100000u | (rn << 16) | rm;
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

    static inline uint and_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode and rd, rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xE2000000u | (rd << 12) | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint bic_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode bic rd, rn, #imm12  (rd = rn AND NOT imm)
    // ------------------------------------------------------------------------
    {
        return 0xE3C00000u | (rd << 12) | (rn << 16) | (imm & 0xFFFu);
    }

    static inline uint ldr_off(uint rd, uint rn, int off)
    // ------------------------------------------------------------------------
    //   Encode ldr rd, [rn, #off]  (signed imm12, pre-indexed)
    // ------------------------------------------------------------------------
    {
        uint imm12 = uint(off < 0 ? -off : off) & 0xFFFu;
        uint up    = off >= 0 ? 0x00800000u : 0u;
        return 0xE5100000u | up | (rn << 16) | (rd << 12) | imm12;
    }

    static inline uint str_off(uint rd, uint rn, int off)
    // ------------------------------------------------------------------------
    //   Encode str rd, [rn, #off]  (signed imm12, pre-indexed)
    // ------------------------------------------------------------------------
    {
        uint imm12 = uint(off < 0 ? -off : off) & 0xFFFu;
        uint up    = off >= 0 ? 0x00800000u : 0u;
        return 0xE5000000u | up | (rn << 16) | (rd << 12) | imm12;
    }

    static inline uint ldr_reg(uint rd, uint rn, uint rm, bool up = true)
    // ------------------------------------------------------------------------
    //   Encode ldr rd, [rn, +/-rm]
    // ------------------------------------------------------------------------
    {
        return (up ? 0xE7900000u : 0xE7100000u)
             | (rn << 16) | (rd << 12) | (rm & 0xFu);
    }

    static inline uint str_reg(uint rd, uint rn, uint rm, bool up = true)
    // ------------------------------------------------------------------------
    //   Encode str rd, [rn, +/-rm]
    // ------------------------------------------------------------------------
    {
        return (up ? 0xE7800000u : 0xE7000000u)
             | (rn << 16) | (rd << 12) | (rm & 0xFu);
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

    static inline uint ldrsb_off(uint rd, uint rn, uint imm8)
    // ------------------------------------------------------------------------
    //   Encode ldrsb rd, [rn, #imm8]  (sign-extends byte; imm8 only)
    // ------------------------------------------------------------------------
    {
        return 0xE1D000D0u | (rn << 16) | (rd << 12)
             | ((imm8 & 0xF0u) << 4) | (imm8 & 0x0Fu);
    }

    static inline uint ldrsh_off(uint rd, uint rn, uint imm8)
    // ------------------------------------------------------------------------
    //   Encode ldrsh rd, [rn, #imm8]  (sign-extends halfword; imm8 only)
    // ------------------------------------------------------------------------
    {
        return 0xE1D000F0u | (rn << 16) | (rd << 12)
             | ((imm8 & 0xF0u) << 4) | (imm8 & 0x0Fu);
    }

    static inline uint sxtb(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sxtb rd, rm  (sign-extend byte, ARMv6+)
    // ------------------------------------------------------------------------
    {
        return 0xE6AF0070u | (rd << 12) | rm;
    }

    static inline uint sxth(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sxth rd, rm  (sign-extend halfword, ARMv6+)
    // ------------------------------------------------------------------------
    {
        return 0xE6BF0070u | (rd << 12) | rm;
    }

    static inline uint uxtb(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode uxtb rd, rm  (zero-extend byte, ARMv6+)
    // ------------------------------------------------------------------------
    {
        return 0xE6EF0070u | (rd << 12) | rm;
    }

    static inline uint uxth(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode uxth rd, rm  (zero-extend halfword, ARMv6+)
    // ------------------------------------------------------------------------
    {
        return 0xE6FF0070u | (rd << 12) | rm;
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

    static inline uint push_regs(uint reg_mask)
    // ------------------------------------------------------------------------
    //   Encode push {reg_mask}  (stmdb sp!, reg_mask)
    // ------------------------------------------------------------------------
    {
        return 0xE92D0000u | (reg_mask & 0xFFFFu);
    }

    static inline uint pop_regs(uint reg_mask)
    // ------------------------------------------------------------------------
    //   Encode pop {reg_mask}  (ldmia sp!, reg_mask)
    // ------------------------------------------------------------------------
    {
        return 0xE8BD0000u | (reg_mask & 0xFFFFu);
    }

    static inline uint b_imm(int32_t imm24)
    // ------------------------------------------------------------------------
    //   Encode b #imm24  (word-aligned PC-relative immediate field)
    // ------------------------------------------------------------------------
    {
        return 0xEA000000u | (uint32_t(imm24) & 0x00FFFFFFu);
    }

    static inline uint bl_imm(int32_t imm24)
    // ------------------------------------------------------------------------
    //   Encode bl #imm24  (word-aligned PC-relative immediate field)
    // ------------------------------------------------------------------------
    {
        return 0xEB000000u | (uint32_t(imm24) & 0x00FFFFFFu);
    }

    static inline uint b_cond_imm(byte cond, int32_t imm24)
    // ------------------------------------------------------------------------
    //   Encode b{cond} #imm24
    // ------------------------------------------------------------------------
    {
        return (uint32_t(cond) << 28) | 0x0A000000u
             | (uint32_t(imm24) & 0x00FFFFFFu);
    }

    static inline uint vmov_s_r(uint sn, uint rt)
    // ------------------------------------------------------------------------
    //   Encode vmov Sn, Rt  (ARM core -> single)
    // ------------------------------------------------------------------------
    {
        return 0xEE000A10u | (rt << 12)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7);
    }

    static inline uint vmov_r_s(uint rt, uint sn)
    // ------------------------------------------------------------------------
    //   Encode vmov Rt, Sn  (single -> ARM core)
    // ------------------------------------------------------------------------
    {
        return 0xEE100A10u | (rt << 12)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7);
    }

    static inline uint vfp_ss(uint base, uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode single-precision VFP 3-operand op (Sd, Sn, Sm)
    // ------------------------------------------------------------------------
    {
        return base
             | ((sd >> 1) << 12) | ((sd & 1u) << 22)
             | ((sn >> 1) << 16) | ((sn & 1u) << 7)
             | ((sm >> 1) << 0)  | ((sm & 1u) << 5);
    }

    static inline uint vadd_ss(uint sd, uint sn, uint sm)
    {
        return vfp_ss(0xEE300A00u, sd, sn, sm);
    }

    static inline uint vsub_ss(uint sd, uint sn, uint sm)
    {
        return vfp_ss(0xEE300A40u, sd, sn, sm);
    }

    static inline uint vmul_ss(uint sd, uint sn, uint sm)
    {
        return vfp_ss(0xEE200A00u, sd, sn, sm);
    }

    static inline uint vdiv_ss(uint sd, uint sn, uint sm)
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
    //   Encode double-precision VFP 3-operand op (Dd, Dn, Dm)
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


    // ========================================================================
    //
    //    Emit helpers
    //
    // ========================================================================

    void            emit_load_off(armreg_t rd, armreg_t rn, int offs);
    void            emit_store_off(armreg_t rn, armreg_t rt, int offs);
    void            emit_push(armreg_t r);
    void            emit_pop(armreg_t r);
    hwcode_t        emit_b_rel();
    hwcode_t        emit_bl_rel();
    hwcode_t        emit_bcond_rel(byte cond);

    void            load_imm32(armreg_t r, uint32_t v);
    void            load_local(local_t loc, armreg_t r);
    void            store_local(armreg_t r, local_t loc);
    void            mov_local(local_t d, local_t s);
    void            swap_locals(local_t d, local_t s);
    void            store_imm(local_t d, value_t v);
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
    void            sext_reg(armreg_t r, byte w);
    void            store_mem(local_t d, local_t s, size_t sz, value_t off=0);
    void            load_mem(local_t d, local_t s, size_t sz, value_t off=0);
    void            load_sext(local_t d, local_t s, byte w, value_t off=0);
    void            bra_pred(byte p, byte_p target, bool sense);
    void            bra_test(local_t s, byte_p target, byte cond);
    void            rel_branch(byte_p target, hwcode_t site);
    void            rel_function(tok_t tok, hwcode_t site);
    void            push_pc();
    void            load_pc(local_t d);
    void            load_pca(local_t d, broff_t b);
    virtual void    prologue();
    virtual void    epilogue();
    void            call_ptr(void (*fn)());
    template <typename T, typename ...U>
    void            call_ptr(T (*fn)(U...)) { return call(((void (*)()) fn)); }
    void            dispatch(local_t loc, byte_p &pc, byte cond);
    void            trap(cstring msg);

protected:
    using relocations_t = std::vector<hwcode_t>;
    relocations_t relocations;
};

#endif // ARM_H
