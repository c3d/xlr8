#ifndef X86_H
#define X86_H
// ****************************************************************************
//  x86.h                                                   XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Shared x86/x86-64 code generation for all built-in opcodes
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
//  REGISTER ALLOCATION CONVENTIONS (x86 / x86-64 / i386)
//  ===========================================================================
//
//  Bytecode locals live in frame memory; there are no shadow registers on
//  x86.  Every access is load/store at [r12 + local*ptr_size].  See regmap.h.
//
//  Register map (x86-64):
//    Frame pointer     r12 (HW_FRAME)   rt.frame, primed at JIT entry
//    Flags/predicates  r15 (HW_FLAGS)   rt.flags (64-bit), primed at entry
//    Scratch           r11 (HW_SCR)     general temp (swap, etc.)
//    Value temps       rax, rcx, rdx    used by opcode helpers
//    Stack pointer     rsp              native stack
//
//  i386 variant: same model, 32-bit slots (ptr_size == 4), nb_regs == 8.
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "backend.h"
#include "regmap.h"


struct x86 : backend
// ----------------------------------------------------------------------------
//   Shared x86 family code generator implementing every opcode
// ----------------------------------------------------------------------------
{
    enum hwreg_t : uint
    {
        HW_RAX = 0,
        HW_RCX = 1,
        HW_RDX = 2,
        HW_RBX = 3,
        HW_RSP = 4,
        HW_RBP = 5,
        HW_RSI = 6,
        HW_RDI = 7,
        HW_R8  = 8,
        HW_R9  = 9,
        HW_R10 = 10,
        HW_R11 = 11,
        HW_R12 = 12,
        HW_R13 = 13,
        HW_R14 = 14,
        HW_R15 = 15,
        HW_FRAME = HW_R12,  // rt.frame — see regmap.h
        HW_FLAGS = HW_R15,  // rt.flags (64-bit predicates)
        HW_SCR   = HW_R11,  // general scratch (swap, etc.)
    };

    x86(arch_t arch);

    void finish_translation() override;

    // All opcode emitters — implemented in x86-emit.cpp
#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    static uint     reg_value(uint r)   { return r & 7; }
    static bool     rex_b(uint r)       { return (r >> 3) & 1; }
    static bool     rex_x(uint x)       { return (x >> 3) & 1; }
    static bool     rex_r(uint r)       { return (r >> 3) & 1; }

    void            orex(int ll, int r, int x, byte c);
    void            emit_rex_modrm(int ll, int r, int x, int b, byte op);
    void            load_hw_imm(uint r, uint64_t imm);
    void            load_local(byte n, uint r);
    void            store_local(uint r, byte n);
    void            mov_local(byte d, byte s);
    void            swap_locals(byte d, byte s);
    void            store_imm(byte d, value_t v);
    void            store_float(byte d, float v);
    void            store_double(byte d, double v);
    void            binop_local(byte d, byte s, cstring op);
    void            binop_imm(byte d, value_t v, cstring op);
    void            cmp_local(byte p, byte d, byte s, cstring rel);
    void            cmp_imm(byte p, byte d, value_t v, cstring rel);
    void            cmp_uimm(byte p, byte d, uvalue_t v, cstring rel);
    void            set_pred(byte p, bool on);
    void            cond_mov_local(byte p, byte d, byte s);
    void            select_local(byte p, byte d, byte s, byte x);
    void            neg_local(byte d);
    void            not_local(byte d);
    void            incr_local(byte d);
    void            decr_local(byte d);
    void            sext_local(byte d, byte w);
    void            zext_local(byte d, byte w);
    void            store_mem(byte d, byte s, uint sz);
    void            load_mem(byte d, byte s, uint sz);
    void            store_mem_off(byte d, byte s, value_t o, uint sz);
    void            load_mem_off(byte d, byte s, value_t o, uint sz);
    void            load_signext(byte d, byte s, byte w);
    void            load_signext_off(byte d, byte s, byte w, value_t o);
    void            frame_addr(byte d, byte s);
    void            bra_offset(broff_t b);
    void            bra_pred(byte p, broff_t b, bool sense);
    void            bra_zero(byte s, broff_t b, bool eq);
    void            bra_signed(byte s, broff_t b, byte cc);
    void            dbra_local(byte d, broff_t b);
    void            jmp_tok(tok_t t);
    void            call_tok(tok_t t);
    void            ijmp_local(byte s);
    void            icall_local(byte s);
    void            push_pc();
    void            load_pc(byte d);
    void            load_pca(byte d, broff_t b);
    void            get_arg(byte d, byte s);
    void            set_arg(byte d, byte s);
    void            push_local(byte s);
    void            pop_local(byte d);
    void            push_flags();
    void            pop_flags();
    void            store_global(byte d, byte s);
    void            load_global(byte d, byte s);
    void            do_ret();
    void            call_ptr(void *fn);
    void            emit_trap(cstring msg);
    void            emit_runtime_call(cstring fn);
    void            emit_runtime_call(cstring fn, tok_t a);
    void            emit_runtime_call(cstring fn, byte a);
    int             local_disp(byte n) const;
};

#endif // X86_H
