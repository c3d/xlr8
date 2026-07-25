#ifndef ARM64_H
#define ARM64_H
// ****************************************************************************
//  arm64.h                                                       XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     AArch64 code generation backend for all built-in opcodes.
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
//   This software is licensed under the terms outlined in the LICENSE.txt file
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
//  REGISTER ALLOCATION CONVENTIONS (AArch64)
//  ===========================================================================
//
//  Locals use [x29 - (local+1)*8]; x29 is adjusted by op_enter and op_leave.
//  Args use [x29 + arg*8 + 16] (skip saved FP+LR).
//  Flags (predicates) live in x22 (HW_FLAGS).
//
//  Register map:
//
//    Frame pointer     x29 (HW_FRAME)   Native FP for locals / args
//    Link register     x30 (HW_LR)      Saved with FP on enter
//    Flags/predicates  x22 (HW_FLAGS)   64-bit predicate bits
//    Scratch           x16 (HW_SCRATCH) Temps / call targets (not allocatable)
//    Allocatable GPRs  x19–x21,x23–x28  Callee-saved only (survive sys calls)
//                      x0–x18           Call-clobbered / reserved (not alloc)
//    FP                v0–v7           RSPEC_FLT / RSPEC_DBL banks
//    Stack pointer     sp              native stack
//
//  Bytecode PC is the translator cursor, not a fixed HW register.
//  Runtime is reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "backend.h"
#include "recorder.h"
#include "regmap.h"

#include <cassert>
#include <cstring>


RECORDER_DECLARE(arm64);
RECORDER_DECLARE(arm64_ops);
RECORDER_DECLARE(arm64_error);
RECORDER_DECLARE(arm64_warning);


struct arm64 : backend
// ----------------------------------------------------------------------------
//   AArch64 code generator implementing every opcode
// ----------------------------------------------------------------------------
{
    arm64(runtime &rt);
    static backend *make(runtime &rt) { return new arm64(rt); }

#include "backend-iface.h"

protected:
    enum arm64reg_t : hwreg_t
    // ------------------------------------------------------------------------
    //    Hardware register names for AArch64
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
        HW_SP,

        HW_V0D,
        HW_V1D,
        HW_V2D,
        HW_V3D,
        HW_V4D,
        HW_V5D,
        HW_V6D,
        HW_V7D,
        HW_V8D,
        HW_V9D,
        HW_V10D,
        HW_V11D,
        HW_V12D,
        HW_V13D,
        HW_V14D,
        HW_V15D,

        HW_V0F,
        HW_V1F,
        HW_V2F,
        HW_V3F,
        HW_V4F,
        HW_V5F,
        HW_V6F,
        HW_V7F,
        HW_V8F,
        HW_V9F,
        HW_V10F,
        HW_V11F,
        HW_V12F,
        HW_V13F,
        HW_V14F,
        HW_V15F,

        HW_COUNT,

        HW_FRAME   = HW_X29,
        HW_LR      = HW_X30,
        HW_FLAGS   = HW_X22,
        HW_SCRATCH = HW_X16,
        HW_NONE    = 0,
    };

    // AArch64 condition codes (B.cond / CSET)
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

    arm64reg_t hw(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Convert hwreg_t to arm64reg_t
    // ------------------------------------------------------------------------
    {
        if (r >= HW_COUNT)
        {
            record(arm64_error, "Invalid hardware register index %u", r);
            r = HW_X0;
        }
        return arm64reg_t(r);
    }

    arm64reg_t hw_local(local_t loc)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local
    // ------------------------------------------------------------------------
    {
        return hw(local(loc));
    }

    static inline uint xn(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   Physical X/SP register number for encoding (0..31)
    // ------------------------------------------------------------------------
    {
        if (r <= HW_SP)
            return uint(r);
        record(arm64_error, "xn() on non-GPR %u", r);
        return 0;
    }

    static inline uint vn(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   Physical V register number for FP encoding
    // ------------------------------------------------------------------------
    {
        if (r >= HW_V0F && r <= HW_V15F)
        {
            if (r >= HW_V8F)
                return 8u + uint(r - HW_V8F);
            return uint(r - HW_V0F);
        }
        if (r >= HW_V0D && r <= HW_V15D)
        {
            if (r >= HW_V8D)
                return 8u + uint(r - HW_V8D);
            return uint(r - HW_V0D);
        }
        record(arm64_error, "vn() on non-V register %u", r);
        return 0;
    }

    static inline bool is_fp(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   True when r is a V-register bank entry
    // ------------------------------------------------------------------------
    {
        return (r >= HW_V0D && r <= HW_V15D) || (r >= HW_V0F && r <= HW_V15F);
    }

    static inline bool is_flt(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   True when r is in the float (S) bank
    // ------------------------------------------------------------------------
    {
        return r >= HW_V0F && r <= HW_V15F;
    }

    int local_disp(int n) const
    // ------------------------------------------------------------------------
    //   Return stack displacement for bytecode local n (ularge_t slots)
    // ------------------------------------------------------------------------
    {
        return ~n * int(sizeof(ularge_t));
    }

    // Prologue pushes x19–x28 (5×16) then FP+LR (16), without moving FP.
    // After enter (stp FP,LR; mov FP,SP), those bytes sit above the enter
    // record, so get/set must skip them as well as the enter's FP+LR pair.
    static constexpr uint PROLOGUE_SAVE_BYTES = 96;

    int arg_disp(int n) const
    // ------------------------------------------------------------------------
    //   Displacement from FP to argument n (above enter record + prologue)
    // ------------------------------------------------------------------------
    {
        return n * int(sizeof(ularge_t))
             + 2 * int(sizeof(ularge_t))   // enter's saved FP+LR
             + int(PROLOGUE_SAVE_BYTES);
    }


    // ========================================================================
    //
    //    AArch64 instruction encoders
    //
    // ========================================================================

    static inline uint mov_rr(uint rd, uint rs)
    // ------------------------------------------------------------------------
    //   Encode orr rd, xzr, rs  (mov alias); SP uses add rd, rs, #0
    // ------------------------------------------------------------------------
    {
        if (rd == 31 || rs == 31)
            return 0x91000000u | (rs << 5) | rd; // add rd, rs, #0
        return 0xaa0003e0u | (rs << 16) | rd;
    }

    static inline uint ldr_off(uint rd, uint rn, uint slot)
    // ------------------------------------------------------------------------
    //   Encode ldr rd, [rn, #slot*8]  (unsigned imm12 scaled ×8)
    // ------------------------------------------------------------------------
    {
        return 0xf9400000u | ((slot & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint str_off(uint rt, uint rn, uint slot)
    // ------------------------------------------------------------------------
    //   Encode str rt, [rn, #slot*8]  (unsigned imm12 scaled ×8)
    // ------------------------------------------------------------------------
    {
        return 0xf9000000u | ((slot & 0xfffu) << 10) | (rn << 5) | rt;
    }

    static inline uint ldur_imm(uint rt, uint rn, int imm9)
    // ------------------------------------------------------------------------
    //   Encode ldur rt, [rn, #imm9]  (unscaled signed byte offset)
    // ------------------------------------------------------------------------
    {
        return 0xf8400000u | ((uint32_t(imm9) & 0x1ffu) << 12) | (rn << 5) | rt;
    }

    static inline uint stur_imm(uint rt, uint rn, int imm9)
    // ------------------------------------------------------------------------
    //   Encode stur rt, [rn, #imm9]  (unscaled signed byte offset)
    // ------------------------------------------------------------------------
    {
        return 0xf8000000u | ((uint32_t(imm9) & 0x1ffu) << 12) | (rn << 5) | rt;
    }

    static inline uint ldr_w_off(uint wt, uint rn, uint slot)
    // ------------------------------------------------------------------------
    //   Encode ldr wt, [rn, #slot*8]  (32-bit; scale-4 imm = slot*2)
    // ------------------------------------------------------------------------
    {
        return 0xb9400000u | ((slot * 2) << 10) | (rn << 5) | wt;
    }

    static inline uint str_w_off(uint wt, uint rn, uint slot)
    // ------------------------------------------------------------------------
    //   Encode str wt, [rn, #slot*8]
    // ------------------------------------------------------------------------
    {
        return 0xb9000000u | ((slot * 2) << 10) | (rn << 5) | wt;
    }

    static inline uint ldr_idx(uint rd, uint rn)
    {
        return 0xf9400000u | (rn << 5) | rd;
    }

    static inline uint ldr_w_idx(uint rd, uint rn)
    {
        return 0xb9400000u | (rn << 5) | rd;
    }

    static inline uint ldrh_idx(uint rd, uint rn)
    {
        return 0x79400000u | (rn << 5) | rd;
    }

    static inline uint ldrb_idx(uint rd, uint rn)
    {
        return 0x39400000u | (rn << 5) | rd;
    }

    static inline uint ldrsb_idx(uint rd, uint rn)
    {
        return 0x39800000u | (rn << 5) | rd;
    }

    static inline uint ldrsh_idx(uint rd, uint rn)
    {
        return 0x79800000u | (rn << 5) | rd;
    }

    static inline uint ldrsw_idx(uint rd, uint rn)
    {
        return 0xb9800000u | (rn << 5) | rd;
    }

    static inline uint str_idx(uint rt, uint rn)
    {
        return 0xf9000000u | (rn << 5) | rt;
    }

    static inline uint strb_idx(uint rt, uint rn)
    {
        return 0x39000000u | (rn << 5) | rt;
    }

    static inline uint strh_idx(uint rt, uint rn)
    {
        return 0x79000000u | (rn << 5) | rt;
    }

    static inline uint str_w_idx(uint rt, uint rn)
    {
        return 0xb9000000u | (rn << 5) | rt;
    }

    static inline uint uxtw_x(uint xd, uint ws)
    {
        return 0xaa0003e0u | (ws << 16) | xd;
    }

    static inline uint movz(uint rd, uint imm16)
    {
        return 0xd2800000u | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint movn(uint rd, uint imm16, uint shift)
    {
        return 0x92800000u | ((shift / 16) << 21) | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint movk(uint rd, uint imm16, uint shift)
    {
        uint hi = shift == 16 ? 0xf2a00000u
                : shift == 32 ? 0xf2c00000u
                              : 0xf2e00000u;
        return hi | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint add_rrr(uint rd, uint rn, uint rm)
    {
        return 0x8b000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sub_rrr(uint rd, uint rn, uint rm)
    {
        return 0xcb000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint mul_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9b007c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sdiv_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9ac00c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint udiv_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9ac00800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint msub_rrr(uint rd, uint rn, uint rm, uint ra)
    {
        return 0x9b008000u | (ra << 10) | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint and_rrr(uint rd, uint rn, uint rm)
    {
        return 0x8a000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint orr_rrr(uint rd, uint rn, uint rm)
    {
        return 0xaa000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint eor_rrr(uint rd, uint rn, uint rm)
    {
        return 0xca000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsl_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9ac02000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint asr_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9ac02400u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsr_rrr(uint rd, uint rn, uint rm)
    {
        return 0x9ac02800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint add_imm(uint rd, uint rn, uint imm)
    {
        return 0x91000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint sub_imm(uint rd, uint rn, uint imm)
    {
        return 0xd1000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint lsr_imm64(uint rd, uint rn, uint amt)
    {
        return 0xd340fc00u | ((amt & 0x3fu) << 16) | (rn << 5) | rd;
    }

    static inline uint lsl_imm64(uint rd, uint rn, uint amt)
    {
        return 0xd3400000u | (((64 - amt) & 0x3fu) << 16)
               | (((63 - amt) & 0x3fu) << 10) | (rn << 5) | rd;
    }

    static inline uint asr_imm64(uint rd, uint rn, uint amt)
    {
        return 0x9340fc00u | ((amt & 0x3fu) << 16) | (rn << 5) | rd;
    }

    static inline uint sbfm_width(uint rd, uint rn, uint w)
    {
        return 0x93400000u | ((w - 1) << 10) | (rn << 5) | rd;
    }

    static inline uint ubfm_width(uint rd, uint rn, uint w)
    {
        return 0xd3400000u | ((w - 1) << 10) | (rn << 5) | rd;
    }

    static inline uint cmp_rr(uint rn, uint rm)
    {
        return 0xeb00001fu | (rm << 16) | (rn << 5);
    }

    static inline uint cmp_r_imm(uint rn, uint imm)
    {
        return 0xf100001fu | ((imm & 0xfffu) << 10) | (rn << 5);
    }

    static inline uint neg_rr(uint rd, uint rm)
    {
        return 0xcb0003e0u | (rm << 16) | rd;
    }

    static inline uint not_rr(uint rd, uint rm)
    {
        return 0xaa2003e0u | (rm << 16) | rd;
    }

    static inline byte invert_cond(byte cond)
    {
        static const byte inv[] =
            { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
        return inv[cond & 15];
    }

    static inline uint cset_x(uint rd, byte cond)
    {
        return 0x9a9f07e0u | (uint(invert_cond(cond)) << 12) | rd;
    }

    static inline uint csel_rrr(uint rd, uint rn, uint rm, byte cond)
    {
        return 0x9a800000u | (rm << 16) | (uint(cond) << 12) | (rn << 5) | rd;
    }

    static inline uint blr(uint rm)
    {
        return 0xd63f0000u | (rm << 5);
    }

    static inline uint br(uint rm)
    {
        return 0xd61f0000u | (rm << 5);
    }

    static inline uint ret_insn()
    {
        return 0xd65f03c0u;
    }

    static inline uint brk_insn()
    {
        return 0xd4200000u;
    }

    static inline uint b_imm(int32_t imm26)
    {
        return 0x14000000u | (uint32_t(imm26) & 0x03ffffffu);
    }

    static inline uint bl_imm(int32_t imm26)
    {
        return 0x94000000u | (uint32_t(imm26) & 0x03ffffffu);
    }

    static inline uint b_cond_imm(byte cond, int32_t imm19)
    {
        return 0x54000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | cond;
    }

    static inline uint cbz_imm(uint rt, int32_t imm19)
    {
        return 0xb4000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | rt;
    }

    static inline uint cbnz_imm(uint rt, int32_t imm19)
    {
        return 0xb5000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | rt;
    }

    static inline uint tbz_imm(uint rt, uint bit, int32_t imm14)
    {
        return 0x36000000u
             | ((bit & 0x20u) << 26)
             | ((bit & 0x1fu) << 19)
             | ((uint32_t(imm14) & 0x3fffu) << 5)
             | (rt & 0x1fu);
    }

    static inline uint tbnz_imm(uint rt, uint bit, int32_t imm14)
    {
        return 0x37000000u
             | ((bit & 0x20u) << 26)
             | ((bit & 0x1fu) << 19)
             | ((uint32_t(imm14) & 0x3fffu) << 5)
             | (rt & 0x1fu);
    }

    static inline uint adr(uint rd, int offset_bytes)
    {
        uint32_t imm = uint32_t(offset_bytes) & 0x1fffffu;
        return 0x10000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | rd;
    }

    static inline uint stp_pre(uint rt, uint rt2, uint rn, int imm7)
    // ------------------------------------------------------------------------
    //   Encode stp rt, rt2, [rn, #imm]!  (imm in bytes, multiple of 8)
    // ------------------------------------------------------------------------
    {
        int scaled = imm7 / 8;
        return 0xa9800000u
             | ((uint32_t(scaled) & 0x7fu) << 15)
             | (rt2 << 10) | (rn << 5) | rt;
    }

    static inline uint ldp_post(uint rt, uint rt2, uint rn, int imm7)
    // ------------------------------------------------------------------------
    //   Encode ldp rt, rt2, [rn], #imm  (imm in bytes, multiple of 8)
    // ------------------------------------------------------------------------
    {
        int scaled = imm7 / 8;
        return 0xa8c00000u
             | ((uint32_t(scaled) & 0x7fu) << 15)
             | (rt2 << 10) | (rn << 5) | rt;
    }

    static inline uint fmov_s_w(uint sd, uint wn)
    {
        return 0x1e270000u | (wn << 5) | sd;
    }

    static inline uint fmov_w_s(uint wn, uint sn)
    {
        return 0x1e260000u | (sn << 5) | wn;
    }

    static inline uint fmov_d_x(uint dd, uint xn)
    {
        return 0x9e670000u | (xn << 5) | dd;
    }

    static inline uint fmov_x_d(uint xn, uint dm)
    {
        return 0x9e660000u | (dm << 5) | xn;
    }

    static inline uint fop_ss(uint op, uint sd, uint sn, uint sm)
    {
        return 0x1e200000u | op | (sm << 16) | (sn << 5) | sd;
    }

    static inline uint fop_dd(uint op, uint dd, uint dn, uint dm)
    {
        return 0x1e600000u | op | (dm << 16) | (dn << 5) | dd;
    }

    static inline uint fmov_ss(uint sd, uint sn)
    {
        return 0x1e204000u | (sn << 5) | sd;
    }

    static inline uint fmov_dd(uint dd, uint dn)
    {
        return 0x1e604000u | (dn << 5) | dd;
    }

    // Signed/unsigned int <-> FP and FP precision converts
    static inline uint scvtf_s_x(uint sd, uint xn)
    {
        return 0x9e220000u | (xn << 5) | sd;
    }
    static inline uint scvtf_d_x(uint dd, uint xn)
    {
        return 0x9e620000u | (xn << 5) | dd;
    }
    static inline uint ucvtf_s_x(uint sd, uint xn)
    {
        return 0x9e230000u | (xn << 5) | sd;
    }
    static inline uint ucvtf_d_x(uint dd, uint xn)
    {
        return 0x9e630000u | (xn << 5) | dd;
    }
    static inline uint fcvtzs_x_s(uint xd, uint sn)
    {
        return 0x9e380000u | (sn << 5) | xd;
    }
    static inline uint fcvtzs_x_d(uint xd, uint dn)
    {
        return 0x9e780000u | (dn << 5) | xd;
    }
    static inline uint fcvtzu_x_s(uint xd, uint sn)
    {
        return 0x9e390000u | (sn << 5) | xd;
    }
    static inline uint fcvtzu_x_d(uint xd, uint dn)
    {
        return 0x9e790000u | (dn << 5) | xd;
    }
    static inline uint fcvt_d_s(uint dd, uint sn)
    {
        return 0x1e22c000u | (sn << 5) | dd;
    }
    static inline uint fcvt_s_d(uint sd, uint dn)
    {
        return 0x1e624000u | (dn << 5) | sd;
    }
    static inline uint fcmp_ss(uint sn, uint sm)
    {
        return 0x1e202000u | (sm << 16) | (sn << 5);
    }
    static inline uint fcmp_dd(uint dn, uint dm)
    {
        return 0x1e602000u | (dm << 16) | (dn << 5);
    }

    // FP op field fragments for fop_ss / fop_dd
    static constexpr uint FOP_MUL = 0x0800;
    static constexpr uint FOP_DIV = 0x1800;
    static constexpr uint FOP_ADD = 0x2800;
    static constexpr uint FOP_SUB = 0x3800;


    // ========================================================================
    //
    //    Emit helpers (encode + emit32le)
    //
    // ========================================================================

    void         emit_load_off(arm64reg_t rd, arm64reg_t rn, int offs);
    void         emit_store_off(arm64reg_t rn, arm64reg_t rt, int offs);
    void         emit_push(arm64reg_t r);
    void         emit_pop(arm64reg_t r);
    hwcode_t     emit_b_rel();
    hwcode_t     emit_bl_rel();
    hwcode_t     emit_bcond_rel(byte cond);
    hwcode_t     emit_cbz_rel(arm64reg_t rt);
    hwcode_t     emit_cbnz_rel(arm64reg_t rt);
    hwcode_t     emit_tbz_rel(arm64reg_t rt, uint bit);
    hwcode_t     emit_tbnz_rel(arm64reg_t rt, uint bit);

    void         set_imm(arm64reg_t r, uint64_t imm);
    void         load_local(local_t loc, arm64reg_t r);
    void         store_local(arm64reg_t r, local_t loc);
    arm64reg_t   load_fp(local_t loc, byte sz);
    void         store_fp(local_t loc, arm64reg_t reg);
    void         mov_local(local_t d, local_t s);
    void         swap_locals(local_t d, local_t s);
    void         store_imm(local_t d, value_t v);
    void         store_float(local_t d, float v);
    void         store_double(local_t d, double v);
    void         binop_local(local_t d, local_t s, op_t op, byte fpsz = 0);
    void         binop_imm(local_t d, value_t v, op_t op);
    void         cmp_local(byte p, local_t d, local_t s, byte cond);
    void         cmp_imm(byte p, local_t d, value_t v, byte cond);
    void         cmp_uimm(byte p, local_t d, uvalue_t v, byte cond);
    void         set_pred(byte p, bool on);
    void         set_pred_cmp(byte p, byte cond);
    void         cond_mov_local(byte p, local_t d, local_t s);
    void         select_local(byte p, local_t d, local_t s, byte x);
    void         neg_local(local_t d);
    void         not_local(local_t d);
    void         incr_local(local_t d);
    void         decr_local(local_t d);
    void         sext_local(local_t d, byte w);
    void         zext_local(local_t d, byte w);
    void         sext_reg(arm64reg_t r, byte w);
    void         store_mem(local_t d, local_t s, size_t sz, value_t off = 0);
    void         load_mem(local_t d, local_t s, size_t sz, value_t off = 0);
    void         load_sext(local_t d, local_t s, byte w, value_t off = 0);
    void         bra_pred(byte p, byte_p target, bool sense);
    void         bra_test(local_t s, byte_p target, byte cond);
    void         rel_branch(byte_p target, hwcode_t site);
    void         rel_function(tok_t tok, hwcode_t site);
    virtual void prologue();
    virtual void epilogue();
    void         call_ptr(void (*fn)());
    template <typename T, typename... U>
    void call_ptr(T (*fn)(U...))
    {
        return call(((void (*)()) fn));
    }
    void dispatch(local_t loc, byte_p &pc, byte cond);

protected:
    using relocations_t = std::vector<hwcode_t>;
    relocations_t relocations;
};

#endif // ARM64_H
