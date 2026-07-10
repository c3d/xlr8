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
//    Saved link        x20 (HW_LINK)    LR across runtime calls
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

#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    enum hwreg_t : uint
    {
        HW_LOC0  = 0,       // shadow frame[0] .. frame[15]
        HW_LOC15 = 15,
        HW_IP    = 16,      // intra-procedure call scratch (AAPCS IP0)
        HW_FRAME = 19,      // callee-saved, rt.frame base for spill
        HW_LINK  = 20,      // callee-saved, saved link register
        HW_RT    = 21,      // reserved: &runtime
        HW_FLAGS = 22,      // callee-saved, rt.flags (64-bit predicates)
        HW_SCR_VAL  = 23,   // scratch: values (swap, arithmetic)
        HW_SCR_ADDR = 24,   // scratch: addresses
        HW_SCR_OFF  = 25,   // scratch: offsets
    };

    void emit32le(uint32_t insn);
    void load_local(byte n, uint r);
    void store_local(uint r, byte n);
    void load_imm64(uint r, uint64_t v);
    void call_abs(void *fn);
    void spill_frame();
    void reload_frame_reg();
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

    bool spill_arg_regs = true;
};

#endif // ARM64_H
