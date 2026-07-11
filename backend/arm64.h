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
//  REGISTER ALLOCATION CONVENTIONS (AArch64)
//  ===========================================================================
//
//  Bytecode locals are frame[n] slots (uintptr_t).  Locals 0..15 live in
//  shadow registers x0..x15 while valid; locals >= 16 use frame memory only
//  (ldr/str via [x19, #local*8] through scratch registers).  See regmap.h.
//
//  Shadow validity: authoritative until spilled before sys, enter/leave, or
//  scratch reuse.  Locals 0 and 1 are also syscall argument registers.
//
//  Register map:
//    Shadow locals     x0 .. x15     frame[0] .. frame[15] when valid
//    Frame pointer     x19 (HW_FRAME)   rt.frame, primed at JIT entry
//    Saved link        x28 (HW_LINK)    invoke return (not a bytecode local)
//    Runtime (rsvd)    x21 (HW_RT)      &runtime, not yet loaded
//    Flags/predicates  x22 (HW_FLAGS)   rt.flags (64-bit), primed at entry
//    Scratch value     x23 (HW_SCR_VAL) swap, arithmetic, immediates
//    Scratch address   x24 (HW_SCR_ADDR)
//    Scratch offset    x25 (HW_SCR_OFF)
//    Intra-call IP     x16 (HW_IP)      call_abs target (AAPCS IP0)
//    Stack pointer     sp (x31)         native stack
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() today; HW_RT reserved for caching later.
//  For 32-bit ARM (AArch32) see arm.h.
//
//  ===========================================================================

#include "backend.h"
#include "regmap.h"
#include "recorder.h"


struct arm64 : backend
// ----------------------------------------------------------------------------
//   AArch64 code generator implementing every built-in opcode
// ----------------------------------------------------------------------------
{
    static backend *make()              { return new arm64; }

    arm64();

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

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    void patch_imm64_at(void *mem, size_t at, uint r, uint64_t v);

    enum hwreg_t : uint
    {
        HW_LOC0  = 0,       // shadow frame[0] .. frame[15]
        HW_LOC15 = 15,
        HW_IP    = 16,      // intra-procedure call scratch (AAPCS IP0)
        HW_FRAME = 19,      // callee-saved, rt.frame base for spill
        HW_RT    = 21,      // reserved: &runtime
        HW_LINK  = 28,      // callee-saved, saved link (not a bytecode local)
        HW_FLAGS = 22,      // callee-saved, rt.flags (64-bit predicates)
        HW_SCR_VAL  = 23,   // scratch: values (swap, arithmetic)
        HW_SCR_ADDR = 24,   // scratch: addresses
        HW_SCR_OFF  = 25,   // scratch: offsets
    };

    static inline uint xreg(byte n)
    // ----------------------------------------------------------------------------
    //   Map bytecode local n to shadow hardware register xn
    // ----------------------------------------------------------------------------
    {
        return n;
    }

    static inline uint mov_rr(uint rd, uint rs)
    // ----------------------------------------------------------------------------
    //   Encode orr rd, xzr, rs  (mov alias)
    // ----------------------------------------------------------------------------
    {
        return 0xaa0003e0u | (rs << 16) | rd;
    }

    static inline uint ldr_off(uint rd, uint rn, uint off)
    // ----------------------------------------------------------------------------
    //   Encode ldr rd, [rn, #off]  (off byte offset, scale x8)
    // ----------------------------------------------------------------------------
    {
        return 0xf9400000u | (off << 10) | (rn << 5) | rd;
    }

    static inline uint str_off(uint rd, uint rn, uint off)
    // ----------------------------------------------------------------------------
    //   Encode str rd, [rn, #off]  (off byte offset, scale x8)
    // ----------------------------------------------------------------------------
    {
        return 0xf9000000u | (off << 10) | (rn << 5) | rd;
    }

    static inline uint str_w_off(uint wt, uint rn, uint slot)
    // ----------------------------------------------------------------------------
    //   Encode str wt, [rn, #slot*8]  (32-bit store into a bytecode slot)
    // ----------------------------------------------------------------------------
    {
        return 0xb9000000u | (slot << 8) | (rn << 5) | wt;
    }

    static inline uint ldr_w_off(uint wt, uint rn, uint slot)
    // ----------------------------------------------------------------------------
    //   Encode ldr wt, [rn, #slot*8]  (32-bit load from a bytecode slot)
    // ----------------------------------------------------------------------------
    {
        return 0xb9400000u | (slot << 8) | (rn << 5) | wt;
    }

    static inline uint uxtw_x(uint xd, uint ws)
    // ----------------------------------------------------------------------------
    //   Encode orr xd, xzr, ws  (zero-extend 32-bit W register to X)
    // ----------------------------------------------------------------------------
    {
        return 0xaa0003e0u | (ws << 16) | xd;
    }

    static inline uint ldr_idx(uint rd, uint rn)
    // ----------------------------------------------------------------------------
    //   Encode ldr rd, [rn]
    // ----------------------------------------------------------------------------
    {
        return 0xf9400000u | (rn << 5) | rd;
    }

    static inline uint ldr_w_idx(uint rd, uint rn)
    // ----------------------------------------------------------------------------
    //   Encode ldr w, [rn]
    // ----------------------------------------------------------------------------
    {
        return 0xb9400000u | (rn << 5) | rd;
    }

    static inline uint ldrh_idx(uint rd, uint rn)
    // ----------------------------------------------------------------------------
    //   Encode ldrh w, [rn]
    // ----------------------------------------------------------------------------
    {
        return 0x79400000u | (rn << 5) | rd;
    }

    static inline uint ldrb_idx(uint rd, uint rn)
    // ----------------------------------------------------------------------------
    //   Encode ldrb w, [rn]
    // ----------------------------------------------------------------------------
    {
        return 0x39400000u | (rn << 5) | rd;
    }

    static inline uint str_idx(uint rd, uint rn)
    // ----------------------------------------------------------------------------
    //   Encode str rd, [rn]
    // ----------------------------------------------------------------------------
    {
        return 0xf9000000u | (rn << 5) | rd;
    }

    static inline uint movz(uint rd, uint imm16)
    // ----------------------------------------------------------------------------
    //   Encode movz rd, #imm16
    // ----------------------------------------------------------------------------
    {
        return 0xd2800000u | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint movk(uint rd, uint imm16, uint shift)
    // ----------------------------------------------------------------------------
    //   Encode movk rd, #imm16, lsl #shift  (shift = 16, 32, or 48)
    // ----------------------------------------------------------------------------
    {
        uint hi = shift == 16 ? 0xf2a00000u : shift == 32 ? 0xf2c00000u : 0xf2e00000u;
        return hi | ((imm16 & 0xffffu) << 5) | rd;
    }

    static inline uint add_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode add rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x8b000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sub_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode sub rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0xcb000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint mul_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode mul rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x9b007c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint sdiv_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode sdiv rd, rn, rm  (64-bit signed divide)
    // ----------------------------------------------------------------------------
    {
        return 0x9ac00c00u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint udiv_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode udiv rd, rn, rm  (64-bit unsigned divide)
    // ----------------------------------------------------------------------------
    {
        return 0x9ac00800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint msub_rrr(uint rd, uint rn, uint rm, uint ra)
    // ----------------------------------------------------------------------------
    //   Encode msub rd, rn, rm, ra  (rd = ra - rn*rm)
    // ----------------------------------------------------------------------------
    {
        return 0x9b008000u | (ra << 10) | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint fmov_s_w(uint sd, uint wn)
    // ----------------------------------------------------------------------------
    //   Encode fmov Sd, Wn
    // ----------------------------------------------------------------------------
    {
        return 0x1e270000u | (wn << 5) | sd;
    }

    static inline uint fmov_w_s(uint wn, uint sn)
    // ----------------------------------------------------------------------------
    //   Encode fmov Wn, Sd
    // ----------------------------------------------------------------------------
    {
        return 0x1e260000u | (sn << 5) | wn;
    }

    static inline uint fmov_d_x(uint dd, uint xn)
    // ----------------------------------------------------------------------------
    //   Encode fmov Dd, Xn
    // ----------------------------------------------------------------------------
    {
        return 0x9e670000u | (xn << 5) | dd;
    }

    static inline uint fmov_x_d(uint xn, uint dm)
    // ----------------------------------------------------------------------------
    //   Encode fmov Xn, Dd
    // ----------------------------------------------------------------------------
    {
        return 0x9e660000u | (dm << 5) | xn;
    }

    static inline uint fop_ss(uint op, uint sd, uint sn, uint sm)
    // ----------------------------------------------------------------------------
    //   Encode single-precision fp op (fadd=0x2800, fsub=0x3800, ...)
    // ----------------------------------------------------------------------------
    {
        return 0x1e200000u | op | (sm << 16) | (sn << 5) | sd;
    }

    static inline uint fop_dd(uint op, uint dd, uint dn, uint dm)
    // ----------------------------------------------------------------------------
    //   Encode double-precision fp op
    // ----------------------------------------------------------------------------
    {
        return 0x1e600000u | op | (dm << 16) | (dn << 5) | dd;
    }

    static inline uint and_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode and rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x8a000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint orr_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode orr rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0xaa000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint eor_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode eor rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0xca000000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsl_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode lsl rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x9ac02000u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint asr_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode asr rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x9ac02400u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint lsr_rrr(uint rd, uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode lsr rd, rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0x9ac02800u | (rm << 16) | (rn << 5) | rd;
    }

    static inline uint add_imm(uint rd, uint rn, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode add rd, rn, #imm12
    // ----------------------------------------------------------------------------
    {
        return 0x91000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint sub_imm(uint rd, uint rn, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode sub rd, rn, #imm12
    // ----------------------------------------------------------------------------
    {
        return 0xd1000000u | ((imm & 0xfffu) << 10) | (rn << 5) | rd;
    }

    static inline uint b_cond_imm(byte cond, int32_t imm19)
    // ----------------------------------------------------------------------------
    //   Encode b.cond #imm19
    // ----------------------------------------------------------------------------
    {
        return 0x54000000u | ((uint32_t(imm19) & 0x7ffffu) << 5) | cond;
    }

    static inline uint sbfm_width(uint rd, uint rn, uint w)
    // ----------------------------------------------------------------------------
    //   Encode sbfm rd, rn, #0, #(w-1)
    // ----------------------------------------------------------------------------
    {
        return 0x93400000u | ((w - 1) << 16) | (rn << 5) | rd;
    }

    static inline uint ubfm_width(uint rd, uint rn, uint w)
    // ----------------------------------------------------------------------------
    //   Encode ubfm rd, rn, #0, #(w-1)
    // ----------------------------------------------------------------------------
    {
        return 0x93000000u | ((w - 1) << 16) | (rn << 5) | rd;
    }

    static inline uint cmp_rr(uint rn, uint rm)
    // ----------------------------------------------------------------------------
    //   Encode cmp rn, rm
    // ----------------------------------------------------------------------------
    {
        return 0xeb00001fu | (rm << 16) | (rn << 5);
    }

    static inline uint cmp_r_imm(uint rn, uint imm)
    // ----------------------------------------------------------------------------
    //   Encode cmp rn, #imm12
    // ----------------------------------------------------------------------------
    {
        return 0xf100001fu | ((imm & 0xfffu) << 10) | (rn << 5);
    }

    static inline byte invert_cond(byte cond)
    // ----------------------------------------------------------------------------
    //   Invert an AArch64 condition code (for CSET)
    // ----------------------------------------------------------------------------
    {
        static const byte inv[] =
            { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
        return inv[cond & 15];
    }

    static inline uint cset_x(uint rd, byte cond)
    // ----------------------------------------------------------------------------
    //   Encode cset xd, cond  (CSINC xd, xzr, xzr, invert(cond))
    // ----------------------------------------------------------------------------
    {
        return 0x9a9f07e0u | (uint(invert_cond(cond)) << 12) | rd;
    }

    static inline byte cond_from_op(cstring op)
    // ----------------------------------------------------------------------------
    //   Map comparison name to AArch64 condition code
    // ----------------------------------------------------------------------------
    {
        if (!strcmp(op, "eq")) return 0;
        if (!strcmp(op, "ne")) return 1;
        if (!strcmp(op, "lt")) return 11;
        if (!strcmp(op, "le")) return 13;
        if (!strcmp(op, "ltu")) return 3;
        if (!strcmp(op, "leu")) return 9;
        return 0;
    }

    static inline uint neg_rr(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode neg rd, rd
    // ----------------------------------------------------------------------------
    {
        return 0xcb0003e0u | (rd << 5) | rd;
    }

    static inline uint not_rr(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode orn rd, xzr, rd
    // ----------------------------------------------------------------------------
    {
        return 0xaa2003e0u | (rd << 16) | rd;
    }

    static inline uint frame_addr_rr(uint rd, uint rs)
    // ----------------------------------------------------------------------------
    //   Encode add rd, x19, rs, lsl #3
    // ----------------------------------------------------------------------------
    {
        return 0x8b000000u | (3u << 10) | (rs << 16) | (HW_FRAME << 5) | rd;
    }

    static inline uint sbfm_64(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode sbfm rd, rd, #0, #63  (sign-extend to 64 bits)
    // ----------------------------------------------------------------------------
    {
        return 0x9340fc00u | (63u << 10) | (rd << 5) | rd;
    }

    static inline uint ubfm_64(uint rd)
    // ----------------------------------------------------------------------------
    //   Encode ubfm rd, rd, #0, #63  (zero-extend to 64 bits)
    // ----------------------------------------------------------------------------
    {
        return 0x92400000u | (rd << 5) | rd;
    }

    static inline uint blr(uint rm)
    // ----------------------------------------------------------------------------
    //   Encode blr rm
    // ----------------------------------------------------------------------------
    {
        return 0xd63f0000u | (rm << 5);
    }

    static inline uint br(uint rm)
    // ----------------------------------------------------------------------------
    //   Encode br rm
    // ----------------------------------------------------------------------------
    {
        return 0xd61f0000u | (rm << 5);
    }

    static inline uint stp_push(uint rt)
    // ----------------------------------------------------------------------------
    //   Encode stp rt, xzr, [sp, #-16]!
    // ----------------------------------------------------------------------------
    {
        return 0xa9bf03e0u | rt;
    }

    static inline uint ldp_pop(uint rt)
    // ----------------------------------------------------------------------------
    //   Encode ldp rt, xzr, [sp], #16
    // ----------------------------------------------------------------------------
    {
        return 0xa8c103e0u | rt;
    }

    static inline uint mov_lr_save()
    // ----------------------------------------------------------------------------
    //   Encode mov HW_LINK, x30
    // ----------------------------------------------------------------------------
    {
        return mov_rr(HW_LINK, 30);
    }

    static inline uint mov_lr_restore()
    // ----------------------------------------------------------------------------
    //   Encode mov x30, HW_LINK
    // ----------------------------------------------------------------------------
    {
        return mov_rr(30, HW_LINK);
    }

    static inline uint reload_frame()
    // ----------------------------------------------------------------------------
    //   Encode mov x19, x0
    // ----------------------------------------------------------------------------
    {
        return 0xaa0003f3u;
    }

    static inline uint ret_insn()
    // ----------------------------------------------------------------------------
    //   Encode ret
    // ----------------------------------------------------------------------------
    {
        return 0xd65f03c0u;
    }

    static inline uint ret_lr_insn()
    // ----------------------------------------------------------------------------
    //   Encode ret HW_LINK  (return through saved link register)
    // ----------------------------------------------------------------------------
    {
        return 0xd65f0000u | (HW_LINK << 5);
    }

    static inline uint brk_insn()
    // ----------------------------------------------------------------------------
    //   Encode brk #0
    // ----------------------------------------------------------------------------
    {
        return 0xd4200000u;
    }

    static inline uint stp_fp_lr()
    // ----------------------------------------------------------------------------
    //   Encode stp x29, x30, [sp, #-16]!
    // ----------------------------------------------------------------------------
    {
        return 0xa9bf7bfdu;
    }

    static inline uint b_imm(int offset)
    // ----------------------------------------------------------------------------
    //   Encode b #offset  (PC-relative branch)
    // ----------------------------------------------------------------------------
    {
        return 0x14000000u | (uint32_t(offset) & 0x03ffffffu);
    }

    static inline uint adr(uint rd, int offset_bytes)
    // ----------------------------------------------------------------------------
    //   Encode adr xd, #offset_bytes  (PC-relative address)
    // ----------------------------------------------------------------------------
    {
        uint32_t imm = uint32_t(offset_bytes) & 0x1fffffu;
        return 0x10000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | rd;
    }

    static inline uint cbz_imm(uint rt, int offset)
    // ----------------------------------------------------------------------------
    //   Encode cbz rt, #offset
    // ----------------------------------------------------------------------------
    {
        return 0xb4000000u | ((uint32_t(offset) & 0x7ffffu) << 5) | rt;
    }

    static inline uint cbnz_imm(uint rt, int offset)
    // ----------------------------------------------------------------------------
    //   Encode cbnz rt, #offset
    // ----------------------------------------------------------------------------
    {
        return 0xb5000000u | ((uint32_t(offset) & 0x7ffffu) << 5) | rt;
    }

    void emit32le(uint32_t insn);
    void load_local(byte n, uint r);
    void store_local(uint r, byte n);
    void load_imm64(uint r, uint64_t v);
    void call_abs(void *fn);
    void spill_frame();
    void reload_frame_reg();
    void reload_shadows();
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

    bool spill_arg_regs = true;
};

RECORDER_DECLARE(arm64);
RECORDER_DECLARE(arm64_ops);
RECORDER_DECLARE(arm64_error);
RECORDER_DECLARE(arm64_warning);

#endif // ARM64_H
