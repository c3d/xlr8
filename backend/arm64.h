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
//  REGISTER ALLOCATION CONVENTIONS (AArch64) — direct mapping
//  ===========================================================================
//
//  Bytecode locals map to GPRs in callee-saved-first order so typical frames
//  (locals 0…8) survive C/`sys` calls without spill/fill:
//
//    local 0…8   →  x19…x27   (callee-saved)
//    local 9…23  →  x0…x14    (caller-saved / AAPCS64 args+temps)
//
//  x15 is scratch; x16–x18 are overflow temps (high locals); x18 also
//  reserved (kernel-trashed on macOS).  x28 = flags, x29 = frame, x30 = LR.
//
//  Since the live value is in the register, frame slots 0–23 hold the
//  *caller's* local homes (iorder[i], not physical xi) around enter/leave;
//  get/set read/write those slots.  Bytecode `call` passes args in-place in
//  local homes; only C/`sys` uses AAPCS64 x0–x7.
//
//  Calling C spills durable homes x0–x14 (HW_SPILL) only.  Temps are written
//  back to the bytecode frame before blr; locals 0…8 in x19–x27 need no spill.
//
//  Float/double local n maps to v0+n (n < 12) transiently; register_sync
//  moves bits back to the local's durable X home (iorder[n]).
//
//  Spec: docs/superpowers/specs/2026-07-27-register-allocation-direct-mapping.md
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
    uint        label(byte_p bytecode) override;

protected:
    enum arm64reg_t : hwreg_t
    // ------------------------------------------------------------------------
    //    Hardware register names for AArch64
    // ------------------------------------------------------------------------
    {
        HW_X0, // First 1-1 caller-saved mapped register, local 9
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
        HW_X14, // Last 1-1 caller-saved mapped regiser, local 23
        HW_X15, // Scratch 0
        HW_X16, // Scratch 1
        HW_X17, // Scratch 2
        HW_X18, // Scratch 3 / Reserved (kernel-trashed on macOS)
        HW_X19, // First 1-1 callee-saved mapped register, local 0
        HW_X20,
        HW_X21,
        HW_X22,
        HW_X23,
        HW_X24,
        HW_X25,
        HW_X26,
        HW_X27, // Last 1-1 callee-saved mapped, local 8
        HW_X28, // Flags
        HW_X29, // Frame pointer
        HW_X30, // Link register
        HW_SP,  // Stack pointer

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

        HW_ARGS      = 8,
        HW_CLOBBERED = 19, // AAPCS64: x0–x18 are call-clobbered
        HW_SPILL     = 15, // x0–x14 inclusive
        HW_SHADOWS   = 24,
        HW_FSHADOWS  = 12,
        HW_TEMPS     = 3,  // X16–X18 only (X15=scratch; X19+=locals 0…)

        HW_RESERVED  = HW_X18,
        HW_TEMP0     = HW_X16,
        HW_FTEMP0    = HW_V12F,
        HW_DTEMP0    = HW_V12D,
        HW_SCRATCH   = HW_X15,
        HW_FLAGS     = HW_X28,
        HW_FRAME     = HW_X29,
        HW_LR        = HW_X30,
        HW_NONE      = 0,
    };

    enum cond_t : byte
    // ------------------------------------------------------------------------
    // AArch64 condition codes (B.cond / CSET)
    // ------------------------------------------------------------------------
    {
        COND_EQ = 0,
        COND_NE = 1,
        COND_CS = 2,
        COND_CC = 3,
        COND_MI = 4,
        COND_PL = 5,
        COND_VS = 6,
        COND_VC = 7,
        COND_HI = 8,
        COND_LS = 9,
        COND_GE = 10,
        COND_LT = 11,
        COND_GT = 12,
        COND_LE = 13,
        COND_AL = 14,
    };

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

    static arm64reg_t hw_int_order(local_t loc)
    // ------------------------------------------------------------------------
    //   Register direct map for integer shadow registers
    // ------------------------------------------------------------------------
    {
        static const arm64reg_t iorder[] =
        {
            HW_X0, // First 1-1 caller-saved mapped register, local 0
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
            HW_X14, // Last 1-1 caller-saved mapped register, local 14

            HW_X19, // First 1-1 callee-saved mapped register, local 15
            HW_X20,
            HW_X21,
            HW_X22,
            HW_X23,
            HW_X24,
            HW_X25,
            HW_X26,
            HW_X27, // Last 1-1 callee-saved mapped, local 23
        };
        static_assert(sizeof(iorder) / sizeof(iorder[0]) == HW_SHADOWS);
        if (loc >= HW_SHADOWS)
        {
            record(arm64_error, "hw_int_order local %u out of range", loc);
            return HW_X19;
        }
        return iorder[loc];
    }

    static arm64reg_t hw_flt_order(local_t loc)
    // ------------------------------------------------------------------------
    //    Register direct map for float shadow registers
    // ------------------------------------------------------------------------
    {
        static const arm64reg_t forder[] =
        {
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
        };
        static_assert(sizeof(forder) / sizeof(forder[0]) == HW_FSHADOWS);
        if (loc >= HW_FSHADOWS)
        {
            record(arm64_error, "hw_flt_order local %u out of range", loc);
            return HW_V12F;
        }
        return forder[loc];
    }

    static arm64reg_t hw_dbl_order(local_t loc)
    // ------------------------------------------------------------------------
    //    Register direct map for double shadow registers
    // ------------------------------------------------------------------------
    {
        static const arm64reg_t dorder[] =
        {
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
        };
        static_assert(sizeof(dorder) / sizeof(dorder[0]) == HW_FSHADOWS);
        if (loc >= HW_FSHADOWS)
        {
            record(arm64_error, "hw_dbl_order local %u out of range", loc);
            return HW_V12D;
        }
        return dorder[loc];
    }


    arm64reg_t hw_local(local_t loc, hwspec_t spec = RSPEC_INT)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local
    // ------------------------------------------------------------------------
    {
        if (loc > local_frame_size)
            record(arm64_warning,
                   "Reading local %u outside of frame %u",
                   loc,

                   local_frame_size);
        // FP shadows are transient V regs. Durable bits live in iorder[loc].
        // Reload that X home when it is in the C-spill set
        bool isfp = (spec & (RSPEC_FLT | RSPEC_DBL)) != 0 &&
                    (spec & RSPEC_INT) == 0;
        if (isfp && loc < HW_SHADOWS)
            hw_sys_arg_reload(hw_int_order(loc), true);
        hwreg_t r = local(loc, spec, true);
        // Temps are filled from the bytecode frame; the C-call spill slot is
        // not their home.
        if (r >= HW_TEMP0 && r < HW_TEMP0 + HW_TEMPS)
            spilled_args &= ~(1u << r);
        else if (!isfp)
            hw_sys_arg_reload(r, true);
        return hw(r);
    }

    arm64reg_t hw_dst_local(local_t loc, hwspec_t spec = RSPEC_INT)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local
    // ------------------------------------------------------------------------
    {
        if (loc > local_frame_size)
            record(arm64_warning, "Writing local %u outside of frame %u",
                   loc, local_frame_size);
        hwreg_t r = local(loc, spec, false);

        // Mark durable home live/dirty (bit clear → respill will store).
        bool isfp = (spec & (RSPEC_FLT | RSPEC_DBL)) != 0 &&
                    (spec & RSPEC_INT) == 0;
        if (isfp && loc < HW_SHADOWS)
            hw_sys_arg_reload(hw_int_order(loc), false);
        else
            hw_sys_arg_reload(r, false);
        return hw(r);
    }

    arm64reg_t hw_rw_local(local_t loc, hwspec_t spec = RSPEC_INT)
    // ------------------------------------------------------------------------
    //   Return hardware register associated with a local that is read/written
    // ------------------------------------------------------------------------
    {
        if (loc > local_frame_size)
            record(arm64_warning, "Updating local %u outside of frame %u",
                   loc, local_frame_size);
        bool isfp = (spec & (RSPEC_FLT | RSPEC_DBL)) != 0 &&
                    (spec & RSPEC_INT) == 0;
        if (isfp && loc < HW_SHADOWS)
            hw_sys_arg_reload(hw_int_order(loc), true);
        hwreg_t r = local(loc, spec, true);
        if (r >= HW_TEMP0 && r < HW_TEMP0 + HW_TEMPS)
            spilled_args &= ~(1u << r);
        else if (!isfp)
            hw_sys_arg_reload(r, true);
        if (isfp && loc < HW_SHADOWS)
            hw_sys_arg_reload(hw_int_order(loc), false);
        else
            hw_sys_arg_reload(r, false);
        return hw(r);
    }

    arm64reg_t hw_sys_arg(uint arg, local_t loc)
    // ------------------------------------------------------------------------
    //   Load a local into the corresponding ABI argument register (x0–x7)
    // ------------------------------------------------------------------------
    {
        assert(arg < HW_ARGS);
        // ld=true: overflow locals live in temps and must be filled from the
        // frame; direct-map locals (0…23) already match and skip the load.
        arm64reg_t src = hw(local(loc, RSPEC_INT, true));
        arm64reg_t dst = arm64reg_t(HW_X0 + arg);
        hw_sys_arg_spill(dst);
        if (src >= HW_TEMP0 && src < HW_TEMP0 + HW_TEMPS)
        {
            // Frame fill is authoritative; do not use the stale C-spill slot.
            spilled_args &= ~(1u << src);
            if (dst != src)
                move(dst, src);
        }
        else if (src < HW_SPILL && (spilled_args & (1 << src)))
        {
            // Direct-map local whose durable home is still in the C spill area
            emit_load_off(dst, HW_SP, int(src) * sizeof(ularge_t));
        }
        else if (dst != src)
        {
            move(dst, src);
        }
        return dst;
    }

    arm64reg_t hw_sys_imm(uint arg, value_t imm)
    // ------------------------------------------------------------------------
    //   Load a local into the corresponding ABI argument register (x0–x7)
    // ------------------------------------------------------------------------
    {
        assert(arg < HW_ARGS);
        arm64reg_t dst = arm64reg_t(HW_X0 + arg);
        hw_sys_arg_spill(dst);
        set_imm(dst, imm);
        return dst;
    }

    arm64reg_t hw_sys_ret(local_t loc, byte bsize = 64)
    // ------------------------------------------------------------------------
    //    Move the value in the target return location, may block its reload
    // ------------------------------------------------------------------------
    {
        arm64reg_t dst = hw_dst_local(loc);
        if (bsize != 64)
            emit32le(sbfm_width(xn(dst), xn(HW_X0), bsize));
        else if (dst != HW_X0)
            emit32le(mov_rr(dst, HW_X0));
        return dst;
    }

    void hw_sys_arg_spill(hwreg_t r)
    // ------------------------------------------------------------------------
    //  Spill one register
    // ------------------------------------------------------------------------
    {
        if (!spilled_args)
            hw_sys_arg_spill();
        if (r < HW_SPILL && ~spilled_args & (1 << r))
        {
            emit_store_off(HW_SP, arm64reg_t(r), r * sizeof(ularge_t));
            spilled_args |= 1 << r;
        }
    }

    void hw_sys_arg_spill()
    // ------------------------------------------------------------------------
    //   Spill durable caller-saved local homes (x0–x14) before a C call
    // ------------------------------------------------------------------------
    //   Slot index = physical register number.  Scratch x15 and temps x16–x18
    //   are not preserved here — temps are frame-synced before blr.  HW_SPILL
    //   is odd (15): pairs cover x0–x13, x14 alone.  Never touch x19 (local 0).
    {
        emit32le(sub_imm(HW_SP, HW_SP, 32 * sizeof(ularge_t)));
        for (uint r = 0; r + 1 < HW_SPILL; r += 2)
            emit32le(stp_off(r, r + 1, HW_SP, r * sizeof(ularge_t)));
        if (HW_SPILL & 1)
            emit_store_off(HW_SP, arm64reg_t(HW_SPILL - 1),
                           (HW_SPILL - 1) * sizeof(ularge_t));
        emit32le(stp_off(HW_FRAME, HW_LR, HW_SP, HW_FRAME * sizeof(ularge_t)));
        spilled_args  = (1u << HW_SPILL) - 1u;
        spilled_args |= (3u << HW_FRAME);
    }


    void hw_sys_arg_respill()
    // ------------------------------------------------------------------------
    //   Respill durable caller-saved homes before overwriting them for syscall
    // ------------------------------------------------------------------------
    {
        if (spilled_args)
        {
            for (uint r = 0; r + 1 < HW_SPILL; r += 2)
            {
                arm64reg_t r1   = arm64reg_t(r);
                arm64reg_t r2   = arm64reg_t(r + 1);
                size_t     offs = r * sizeof(ularge_t);
                switch ((spilled_args >> r) & 3)
                {
                case 0: emit32le(stp_off(r1, r2, HW_SP, offs)); break;
                case 1: emit_store_off(HW_SP, r2, offs+8); break;
                case 2: emit_store_off(HW_SP, r1, offs); break;
                case 3: break;
                }
                spilled_args |= 3 << r;
            }
            if (HW_SPILL & 1)
            {
                uint r = HW_SPILL - 1;
                if (~spilled_args & (1u << r))
                {
                    emit_store_off(HW_SP, arm64reg_t(r), r * sizeof(ularge_t));
                    spilled_args |= 1u << r;
                }
            }
        }
    }

    void hw_sys_arg_reload(hwreg_t r, bool load)
    // ------------------------------------------------------------------------
    //   Reload one hardware register from the C-call spill area if needed
    // ------------------------------------------------------------------------
    //   `r` is a hardware register index (not a bytecode local).
    {
        if (r < HW_SPILL && spilled_args & (1 << r))
        {
            arm64reg_t dst = arm64reg_t(r);
            if (load)
                emit_load_off(dst, HW_SP, r * sizeof(ularge_t));
            else
                // About to write the register; mark it dirty for respill
                spilled_args &= ~(1 << r);
        }
    }

    void hw_sys_arg_reload()
    // ------------------------------------------------------------------------
    //   Reload durable caller-saved homes we spilled before calling C code
    // ------------------------------------------------------------------------
    {
        if (spilled_args)
        {
            for (uint r = 0; r + 1 < HW_SPILL; r += 2)
            {
                arm64reg_t r1   = arm64reg_t(r);
                arm64reg_t r2   = arm64reg_t(r + 1);
                size_t     offs = r * sizeof(ularge_t);
                switch ((spilled_args >> r) & 3)
                {
                case 0: break;
                case 1: emit_load_off(r1, HW_SP, offs); break;
                case 2: emit_load_off(r2, HW_SP, offs+8); break;
                case 3: emit32le(ldp_off(r1, r2, HW_SP, offs)); break;
                }
            }
            if (HW_SPILL & 1)
            {
                uint r = HW_SPILL - 1;
                if (spilled_args & (1u << r))
                    emit_load_off(arm64reg_t(r), HW_SP, r * sizeof(ularge_t));
            }
            emit32le(ldp_off(HW_FRAME,HW_LR,HW_SP,HW_FRAME*sizeof(ularge_t)));
            emit32le(add_imm(HW_SP, HW_SP, 32 * sizeof(ularge_t)));
            spilled_args = 0;
        }
    }

    void hw_keep_scratch()
    // ------------------------------------------------------------------------
    //   Do not reload x15 over a value stashed there across spill reconcile
    // ------------------------------------------------------------------------
    {
        spilled_args &= ~(1u << HW_SCRATCH);
    }

    uint hw_spills_at(byte_p target);
    void hw_sys_arg_reload(byte_p target);


    static inline uint xn(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   Physical X/SP register number for encoding (0..31)
    // ------------------------------------------------------------------------
    {
        if (r > HW_SP)
            record(arm64_error, "xn() on non-GPR register %u", r);
        return uint(r & 31);
    }

    static inline uint vn(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   Physical V register number for FP encoding
    // ------------------------------------------------------------------------
    {
        if (r < HW_V0D)
            record(arm64_error, "X%u is Not a V register", r);
        return uint(r & 15);
    }

    static inline bool is_fp(arm64reg_t r)
    // ------------------------------------------------------------------------
    //   True when r is a V-register bank entry
    // ------------------------------------------------------------------------
    {
        return r >= HW_V0D;
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
    // ------------------------------------------------------------------------
    //   Encode ldr rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0xf9400000u | (rn << 5) | rd;
    }

    static inline uint ldr_w_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldr wt, [rn] (32-bit)
    // ------------------------------------------------------------------------
    {
        return 0xb9400000u | (rn << 5) | rd;
    }

    static inline uint ldrh_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldrh rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x79400000u | (rn << 5) | rd;
    }

    static inline uint ldrb_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldrb rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x39400000u | (rn << 5) | rd;
    }

    static inline uint ldrsb_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldrsb rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x39800000u | (rn << 5) | rd;
    }

    static inline uint ldrsh_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldrsh rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x79800000u | (rn << 5) | rd;
    }

    static inline uint ldrsw_idx(uint rd, uint rn)
    // ------------------------------------------------------------------------
    //   Encode ldrsw rd, [rn]
    // ------------------------------------------------------------------------
    {
        return 0xb9800000u | (rn << 5) | rd;
    }

    static inline uint str_idx(uint rt, uint rn)
    // ------------------------------------------------------------------------
    //   Encode str rt, [rn]
    // ------------------------------------------------------------------------
    {
        return 0xf9000000u | (rn << 5) | rt;
    }

    static inline uint strb_idx(uint rt, uint rn)
    // ------------------------------------------------------------------------
    //   Encode strb rt, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x39000000u | (rn << 5) | rt;
    }

    static inline uint strh_idx(uint rt, uint rn)
    // ------------------------------------------------------------------------
    //   Encode strh rt, [rn]
    // ------------------------------------------------------------------------
    {
        return 0x79000000u | (rn << 5) | rt;
    }

    static inline uint str_w_idx(uint rt, uint rn)
    // ------------------------------------------------------------------------
    //   Encode str wt, [rn] (32-bit)
    // ------------------------------------------------------------------------
    {
        return 0xb9000000u | (rn << 5) | rt;
    }

    static inline uint uxtw_x(uint xd, uint ws)
    // ------------------------------------------------------------------------
    //   Encode uxtw xd, ws (zero-extend 32→64)
    // ------------------------------------------------------------------------
    {
        return 0xaa0003e0u | (ws << 16) | xd;
    }

    static inline uint movz(uint rd, uint imm16)
    // ------------------------------------------------------------------------
    //   Encode movz rd, #imm16
    // ------------------------------------------------------------------------
    {
        return 0xd2800000u | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint movn(uint rd, uint imm16, uint shift)
    // ------------------------------------------------------------------------
    //   Encode movn rd, #imm16, lsl #shift
    // ------------------------------------------------------------------------
    {
        return 0x92800000u | ((shift / 16) << 21) | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint movk(uint rd, uint imm16, uint shift)
    // ------------------------------------------------------------------------
    //   Encode movk rd, #imm16, lsl #shift
    // ------------------------------------------------------------------------
    {
        uint hi = shift == 16 ? 0xf2a00000u
                : shift == 32 ? 0xf2c00000u
                              : 0xf2e00000u;
        return hi | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint add_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode add rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x8b000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sub_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xcb000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint mul_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode mul rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9b007c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sdiv_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode sdiv rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9ac00c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint udiv_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode udiv rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9ac00800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint msub_rrr(uint rd, uint rn, uint rm, uint ra)
    // ------------------------------------------------------------------------
    //   Encode msub rd, rn, rm, ra
    // ------------------------------------------------------------------------
    {
        return 0x9b008000u | (ra << 10) | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint and_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode and rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x8a000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint orr_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode orr rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xaa000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint eor_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode eor rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xca000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsl_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode lsl rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9ac02000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint asr_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode asr rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9ac02400u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsr_rrr(uint rd, uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode lsr rd, rn, rm
    // ------------------------------------------------------------------------
    {
        return 0x9ac02800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint add_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode add rd, rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0x91000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint sub_imm(uint rd, uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xd1000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint sub_ext(uint rd, uint rn, uint rm,
                               uint option = 7 /* UXTX */, uint imm3 = 0)
    // ------------------------------------------------------------------------
    //   Encode sub rd, rn, rm{, extend #imm3} (Rd/Rn=31 is SP)
    // ------------------------------------------------------------------------
    {
        return 0xcb200000u | rm<<16 | option<<13 | imm3<<10 | rn<<5 | rd;
    }

    static inline uint lsr_imm64(uint rd, uint rn, uint amt)
    // ------------------------------------------------------------------------
    //   Encode lsr rd, rn, #amt (64-bit)
    // ------------------------------------------------------------------------
    {
        return 0xd340fc00u | ((amt & 0x3fu) << 16) | (rn << 5) | rd;
    }

    static inline uint lsl_imm64(uint rd, uint rn, uint amt)
    // ------------------------------------------------------------------------
    //   Encode lsl rd, rn, #amt (64-bit)
    // ------------------------------------------------------------------------
    {
        return 0xd3400000u | (((64 - amt) & 0x3fu) << 16)
               | (((63 - amt) & 0x3fu) << 10) | (rn << 5) | rd;
    }

    static inline uint asr_imm64(uint rd, uint rn, uint amt)
    // ------------------------------------------------------------------------
    //   Encode asr rd, rn, #amt (64-bit)
    // ------------------------------------------------------------------------
    {
        return 0x9340fc00u | ((amt & 0x3fu) << 16) | (rn << 5) | rd;
    }

    static inline uint sbfm_width(uint rd, uint rn, uint w)
    // ------------------------------------------------------------------------
    //   Encode sbfx/sxt-style BFM of width w
    // ------------------------------------------------------------------------
    {
        return 0x93400000u | ((w - 1) << 10) | (rn << 5) | rd;
    }

    static inline uint ubfm_width(uint rd, uint rn, uint w)
    // ------------------------------------------------------------------------
    //   Encode ubfx/uxt-style BFM of width w
    // ------------------------------------------------------------------------
    {
        return 0xd3400000u | ((w - 1) << 10) | (rn << 5) | rd;
    }

    static inline uint bfm64(uint rd, uint rn, uint immr, uint imms)
    // ------------------------------------------------------------------------
    //   Encode BFM (64-bit): bitfield move
    // ------------------------------------------------------------------------
    {
        return 0xb3400000u | ((immr & 0x3fu) << 16) | ((imms & 0x3fu) << 10)
               | (rn << 5) | rd;
    }

    static inline uint bfc64(uint rd, uint lsb, uint width)
    // ------------------------------------------------------------------------
    //   Encode BFC (64-bit): clear `width` bits at `lsb` in rd
    // ------------------------------------------------------------------------
    {
        return bfm64(rd, 31 /* XZR */, (64u - lsb) & 63u, lsb + width - 1);
    }

    static inline uint bfi64(uint rd, uint rn, uint lsb, uint width)
    // ------------------------------------------------------------------------
    //   Encode BFI (64-bit): insert low `width` bits of rn into rd at `lsb`
    // ------------------------------------------------------------------------
    {
        return bfm64(rd, rn, (64u - lsb) & 63u, width - 1);
    }

    static inline uint orr_imm64_bit(uint rd, uint rn, uint bit)
    // ------------------------------------------------------------------------
    //   Encode ORR rd, rn, #(1<<bit) (logical immediate)
    // ------------------------------------------------------------------------
    {
        return 0xb2400000u | (((64u - bit) & 0x3fu) << 16) | (rn << 5) | rd;
    }

    static inline uint cmp_rr(uint rn, uint rm)
    // ------------------------------------------------------------------------
    //   Encode cmp rn, rm
    // ------------------------------------------------------------------------
    {
        return 0xeb00001fu | (rm << 16) | (rn << 5);
    }

    static inline uint cmp_r_imm(uint rn, uint imm)
    // ------------------------------------------------------------------------
    //   Encode cmp rn, #imm12
    // ------------------------------------------------------------------------
    {
        return 0xf100001fu | ((imm & 0xfffu) << 10) | (rn << 5);
    }

    static inline uint neg_rr(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode neg rd, rm
    // ------------------------------------------------------------------------
    {
        return 0xcb0003e0u | (rm << 16) | rd;
    }

    static inline uint not_rr(uint rd, uint rm)
    // ------------------------------------------------------------------------
    //   Encode mvn rd, rm
    // ------------------------------------------------------------------------
    {
        return 0xaa2003e0u | (rm << 16) | rd;
    }

    static inline byte invert_cond(byte cond)
    // ------------------------------------------------------------------------
    //   Invert AArch64 condition code
    // ------------------------------------------------------------------------
    {
        static const byte inv[] =
            { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
        return inv[cond & 15];
    }

    static inline uint cset_x(uint rd, byte cond)
    // ------------------------------------------------------------------------
    //   Encode cset rd, cond
    // ------------------------------------------------------------------------
    {
        return 0x9a9f07e0u | (uint(invert_cond(cond)) << 12) | rd;
    }

    static inline uint csel_rrr(uint rd, uint rn, uint rm, byte cond)
    // ------------------------------------------------------------------------
    //   Encode csel rd, rn, rm, cond
    // ------------------------------------------------------------------------
    {
        return 0x9a800000u | (rm << 16) | (uint(cond) << 12) | (rn << 5) | rd;
    }

    static inline uint blr(uint rm)
    // ------------------------------------------------------------------------
    //   Encode blr rm
    // ------------------------------------------------------------------------
    {
        return 0xd63f0000u | (rm << 5);
    }

    static inline uint br(uint rm)
    // ------------------------------------------------------------------------
    //   Encode br rm
    // ------------------------------------------------------------------------
    {
        return 0xd61f0000u | (rm << 5);
    }

    static inline uint ret_insn()
    // ------------------------------------------------------------------------
    //   Encode ret
    // ------------------------------------------------------------------------
    {
        return 0xd65f03c0u;
    }

    static inline uint brk_insn()
    // ------------------------------------------------------------------------
    //   Encode brk #0
    // ------------------------------------------------------------------------
    {
        return 0xd4200000u;
    }

    static inline uint b_imm(int32_t imm26)
    // ------------------------------------------------------------------------
    //   Encode b #imm26
    // ------------------------------------------------------------------------
    {
        return 0x14000000u | (uint32_t(imm26) & 0x03ffffffu);
    }

    static inline uint bl_imm(int32_t imm26)
    // ------------------------------------------------------------------------
    //   Encode bl #imm26
    // ------------------------------------------------------------------------
    {
        return 0x94000000u | (uint32_t(imm26) & 0x03ffffffu);
    }

    static inline uint b_cond_imm(byte cond, int32_t imm19)
    // ------------------------------------------------------------------------
    //   Encode b.cond #imm19
    // ------------------------------------------------------------------------
    {
        return 0x54000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | cond;
    }

    static inline uint cbz_imm(uint rt, int32_t imm19)
    // ------------------------------------------------------------------------
    //   Encode cbz rt, #imm19
    // ------------------------------------------------------------------------
    {
        return 0xb4000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | rt;
    }

    static inline uint cbnz_imm(uint rt, int32_t imm19)
    // ------------------------------------------------------------------------
    //   Encode cbnz rt, #imm19
    // ------------------------------------------------------------------------
    {
        return 0xb5000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | rt;
    }

    static inline uint tbz_imm(uint rt, uint bit, int32_t imm14)
    // ------------------------------------------------------------------------
    //   Encode tbz rt, #bit, #imm14
    // ------------------------------------------------------------------------
    {
        return 0x36000000u
             | ((bit & 0x20u) << 26)
             | ((bit & 0x1fu) << 19)
             | ((uint32_t(imm14) & 0x3fffu) << 5)
             | (rt & 0x1fu);
    }

    static inline uint tbnz_imm(uint rt, uint bit, int32_t imm14)
    // ------------------------------------------------------------------------
    //   Encode tbnz rt, #bit, #imm14
    // ------------------------------------------------------------------------
    {
        return 0x37000000u
             | ((bit & 0x20u) << 26)
             | ((bit & 0x1fu) << 19)
             | ((uint32_t(imm14) & 0x3fffu) << 5)
             | (rt & 0x1fu);
    }

    static inline uint adr(uint rd, int offset_bytes)
    // ------------------------------------------------------------------------
    //   Encode adr rd, #offset
    // ------------------------------------------------------------------------
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

    static inline uint stp_off(uint rt, uint rt2, uint rn, int imm7)
    // ------------------------------------------------------------------------
    //   Encode stp rt, rt2, [rn, #imm]  (no writeback; imm multiple of 8)
    // ------------------------------------------------------------------------
    {
        int scaled = imm7 / 8;
        return 0xa9000000u
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

    static inline uint ldp_off(uint rt, uint rt2, uint rn, int imm7)
    // ------------------------------------------------------------------------
    //   Encode ldp rt, rt2, [rn, #imm]  (no writeback; imm multiple of 8)
    // ------------------------------------------------------------------------
    {
        int scaled = imm7 / 8;
        return 0xa9400000u
             | ((uint32_t(scaled) & 0x7fu) << 15)
             | (rt2 << 10) | (rn << 5) | rt;
    }

    static inline uint fmov_s_w(uint sd, uint wn)
    // ------------------------------------------------------------------------
    //   Encode fmov sd, wn
    // ------------------------------------------------------------------------
    {
        return 0x1e270000u | (wn << 5) | sd;
    }

    static inline uint fmov_w_s(uint wn, uint sn)
    // ------------------------------------------------------------------------
    //   Encode fmov wn, sn
    // ------------------------------------------------------------------------
    {
        return 0x1e260000u | (sn << 5) | wn;
    }

    static inline uint fmov_d_x(uint dd, uint xn)
    // ------------------------------------------------------------------------
    //   Encode fmov dd, xn
    // ------------------------------------------------------------------------
    {
        return 0x9e670000u | (xn << 5) | dd;
    }

    static inline uint fmov_x_d(uint xn, uint dm)
    // ------------------------------------------------------------------------
    //   Encode fmov xn, dm
    // ------------------------------------------------------------------------
    {
        return 0x9e660000u | (dm << 5) | xn;
    }

    static inline uint fop_ss(uint op, uint sd, uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode FP scalar op (32-bit) from FOP_* fragment
    // ------------------------------------------------------------------------
    {
        return 0x1e200000u | op | (sm << 16) | (sn << 5) | sd;
    }

    static inline uint fop_dd(uint op, uint dd, uint dn, uint dm)
    // ------------------------------------------------------------------------
    //   Encode FP scalar op (64-bit) from FOP_* fragment
    // ------------------------------------------------------------------------
    {
        return 0x1e600000u | op | (dm << 16) | (dn << 5) | dd;
    }

    static inline uint fmov_ss(uint sd, uint sn)
    // ------------------------------------------------------------------------
    //   Encode fmov sd, sn
    // ------------------------------------------------------------------------
    {
        return 0x1e204000u | (sn << 5) | sd;
    }

    static inline uint fmov_dd(uint dd, uint dn)
    // ------------------------------------------------------------------------
    //   Encode fmov dd, dn
    // ------------------------------------------------------------------------
    {
        return 0x1e604000u | (dn << 5) | dd;
    }

    // Signed/unsigned int <-> FP and FP precision converts
    static inline uint scvtf_s_x(uint sd, uint xn)
    // ------------------------------------------------------------------------
    //   Encode scvtf sd, xn
    // ------------------------------------------------------------------------
    {
        return 0x9e220000u | (xn << 5) | sd;
    }
    static inline uint scvtf_d_x(uint dd, uint xn)
    // ------------------------------------------------------------------------
    //   Encode scvtf dd, xn
    // ------------------------------------------------------------------------
    {
        return 0x9e620000u | (xn << 5) | dd;
    }
    static inline uint ucvtf_s_x(uint sd, uint xn)
    // ------------------------------------------------------------------------
    //   Encode ucvtf sd, xn
    // ------------------------------------------------------------------------
    {
        return 0x9e230000u | (xn << 5) | sd;
    }
    static inline uint ucvtf_d_x(uint dd, uint xn)
    // ------------------------------------------------------------------------
    //   Encode ucvtf dd, xn
    // ------------------------------------------------------------------------
    {
        return 0x9e630000u | (xn << 5) | dd;
    }
    static inline uint fcvtzs_x_s(uint xd, uint sn)
    // ------------------------------------------------------------------------
    //   Encode fcvtzs xd, sn
    // ------------------------------------------------------------------------
    {
        return 0x9e380000u | (sn << 5) | xd;
    }
    static inline uint fcvtzs_x_d(uint xd, uint dn)
    // ------------------------------------------------------------------------
    //   Encode fcvtzs xd, dn
    // ------------------------------------------------------------------------
    {
        return 0x9e780000u | (dn << 5) | xd;
    }
    static inline uint fcvtzu_x_s(uint xd, uint sn)
    // ------------------------------------------------------------------------
    //   Encode fcvtzu xd, sn
    // ------------------------------------------------------------------------
    {
        return 0x9e390000u | (sn << 5) | xd;
    }
    static inline uint fcvtzu_x_d(uint xd, uint dn)
    // ------------------------------------------------------------------------
    //   Encode fcvtzu xd, dn
    // ------------------------------------------------------------------------
    {
        return 0x9e790000u | (dn << 5) | xd;
    }
    static inline uint fcvt_d_s(uint dd, uint sn)
    // ------------------------------------------------------------------------
    //   Encode fcvt dd, sn
    // ------------------------------------------------------------------------
    {
        return 0x1e22c000u | (sn << 5) | dd;
    }
    static inline uint fcvt_s_d(uint sd, uint dn)
    // ------------------------------------------------------------------------
    //   Encode fcvt sd, dn
    // ------------------------------------------------------------------------
    {
        return 0x1e624000u | (dn << 5) | sd;
    }
    static inline uint fcmp_ss(uint sn, uint sm)
    // ------------------------------------------------------------------------
    //   Encode fcmp sn, sm
    // ------------------------------------------------------------------------
    {
        return 0x1e202000u | (sm << 16) | (sn << 5);
    }
    static inline uint fcmp_dd(uint dn, uint dm)
    // ------------------------------------------------------------------------
    //   Encode fcmp dn, dm
    // ------------------------------------------------------------------------
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
    void         dispatch(local_t loc, byte_p &pc, byte cond);
    void         call_ptr(void (*fn)());
    template <typename T, typename... U>
    void         call_ptr(T (*fn)(U...))
    {
        return call(((void (*)()) fn));
    }

protected:
    uint          local_frame_size;
    uint          spilled_args;
    uint          num_functions;
    uint          pre_enter_frame_size;

    struct regspill { byte_p bytecode; uint spilled; };
    using regspills = std::vector<regspill>;
    regspills     spills;

    friend struct regspill_compare;
    template<typename T> static auto lower_bound(const regspills &, T);
    template<typename T> static auto equal_range(const regspills &, T);
};

#endif // ARM64_H
