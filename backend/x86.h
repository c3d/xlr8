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
//  Bytecode locals 0..3 (x86-64 only) shadow frame[0..3] in r8,r9,r10,rbx.
//  Locals >= 4 and all locals on i386 use [r12 + local*8] as today.
//
//  Register map (x86-64):
//    Shadow locals     r8,r9,r10,rbx  frame[0] .. frame[3] when valid
//    Frame pointer     r12 (HW_FRAME)   rt.frame, primed at JIT entry
//    Flags/predicates  r15 (HW_FLAGS)   rt.flags (64-bit), primed at entry
//    Scratch           r11 (HW_SCR)     general temp (swap, etc.)
//    Value temps       rax, rcx, rdx    used by opcode helpers
//    Callee-saved      rbx, rbp         rbx shadows local 3; rbp in prologue
//    Stack pointer     rsp              native stack
//
//  i386 variant: same model, ularge_t slots (8 bytes), nb_regs == 8.
//    Frame pointer     edi (HW_RDI)     rt.frame, callee-saved (avoid ebx PIC/GOT)
//    Flags/predicates  esi (HW_RSI)     rt.flags, callee-saved
//    Scratch           ecx (HW_RCX)     general temp
//
//  Bytecode PC is the translator cursor, not a fixed HW register.  Runtime is
//  reached via xlr8_get_runtime() in spill/sys paths.
//
//  ===========================================================================

#include "backend.h"
#include "regmap.h"
#include "recorder.h"

#include <cstring>


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

    static const uint X86_SHADOW_COUNT = 4;

    uint shadow_reg(byte n) const
    // ------------------------------------------------------------------------
    //   Map bytecode local n to its shadow hardware register (x86-64 only)
    // ------------------------------------------------------------------------
    {
        static const uint regs[X86_SHADOW_COUNT] =
            { HW_R8, HW_R9, HW_R10, HW_RBX };
        return regs[n];
    }

    bool has_shadow(byte n) const
    // ------------------------------------------------------------------------
    //   True when local n is kept in a dedicated shadow register
    // ------------------------------------------------------------------------
    {
        return ptr_size() == 8 && n < X86_SHADOW_COUNT;
    }

    x86(arch_t arch);

    uint hw_frame() const { return arch() == ARCH_I386 ? HW_RDI : HW_R12; }
    uint hw_flags() const { return arch() == ARCH_I386 ? HW_RSI : HW_R15; }
    uint hw_scr() const   { return arch() == ARCH_I386 ? HW_RCX : HW_R11; }

    void begin_translation() override;
    void finish_translation() override;
    size_t emit_over_callees_branch() override;
    void patch_branch_site(size_t insn_pos, size_t target_native) override;

    void emit_user(op_t op, byte_p &pc) override;

    // All opcode emitters — implemented in x86-emit.cpp
#define OP(op, sig) sig override;
#include "opover.tbl"
#undef OP

protected:
    static inline uint reg_value(uint r)
    // ------------------------------------------------------------------------
    //   Low three bits of a hardware register number
    // ------------------------------------------------------------------------
    {
        return r & 7;
    }

    static inline bool rex_b(uint r)
    // ------------------------------------------------------------------------
    //   REX.B bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline bool rex_x(uint x)
    // ------------------------------------------------------------------------
    //   REX.X bit for extended index register x
    // ------------------------------------------------------------------------
    {
        return (x >> 3) & 1;
    }

    static inline bool rex_r(uint r)
    // ------------------------------------------------------------------------
    //   REX.R bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline byte modrm_rr(uint r, uint x)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, x (register-direct, both in registers)
    // ------------------------------------------------------------------------
    {
        return byte(0xc0 | (reg_value(r) << 3) | reg_value(x));
    }

    static inline byte modrm_rm(uint r, uint b)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, [b] (register r, memory at base b)
    // ------------------------------------------------------------------------
    {
        return byte(0x00 | (reg_value(r) << 3) | reg_value(b));
    }

    static inline byte modrm_mr(uint b, uint r)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op [b], r (memory at base b, register r)
    // ------------------------------------------------------------------------
    {
        return byte(0x00 | (reg_value(r) << 3) | reg_value(b));
    }

    // =========================================================================
    //  Instruction emitters
    //
    //  Helper names use Intel operand order (dest, src).  ModR/M encodes the
    //  source in the reg field and the destination in r/m, so every rr helper
    //  ends up calling modrm_rr(src, dest).
    // =========================================================================

    static constexpr byte JCC_JZ  = 0x84;   // je / jz
    static constexpr byte JCC_JNZ = 0x85;   // jne / jnz
    static constexpr byte JCC_JL  = 0x8c;   // jl / jnge
    static constexpr byte JCC_JLE = 0x8e;   // jle / jng
    static constexpr byte JCC_JGE = 0x8d;   // jge / jnl
    static constexpr byte JCC_JG  = 0x8f;   // jg / jnle

    static constexpr byte SETCC_EQ  = 0x94; // sete / setz
    static constexpr byte SETCC_NE  = 0x95; // setne / setnz
    static constexpr byte SETCC_LT  = 0x9c; // setl / setnge
    static constexpr byte SETCC_LE  = 0x9e; // setle / setng
    static constexpr byte SETCC_LTU = 0x92; // setb / setnae
    static constexpr byte SETCC_LEU = 0x96; // setbe / setna

    void emit_op_rr(byte op, uint dest, uint src, int ll = 1)
    // ------------------------------------------------------------------------
    //   Emit a two-register ALU opcode: op dest, src
    // ------------------------------------------------------------------------
    {
        orex(ll, src, dest, op);
        g(modrm_rr(src, dest));
    }

    void emit_mov_rr(uint dest, uint src)    { emit_op_rr(0x89, dest, src); }
    void emit_add_rr(uint dest, uint src)    { emit_op_rr(0x01, dest, src); }
    void emit_sub_rr(uint dest, uint src)    { emit_op_rr(0x29, dest, src); }
    void emit_and_rr(uint dest, uint src)    { emit_op_rr(0x21, dest, src); }
    void emit_or_rr(uint dest, uint src)     { emit_op_rr(0x09, dest, src); }
    void emit_xor_rr(uint dest, uint src)    { emit_op_rr(0x31, dest, src); }
    void emit_cmp_rr(uint dest, uint src)    { emit_op_rr(0x39, dest, src); }
    void emit_test_rr(uint dest, uint src)   { emit_op_rr(0x85, dest, src); }

    void emit_setcc_al(byte cc)
    // ------------------------------------------------------------------------
    //   Emit SETcc al (low byte of rax must hold the value being tested)
    // ------------------------------------------------------------------------
    {
        g(0x0f);
        g(cc);
        g(0xc0 | reg_value(HW_RAX));
    }

    void emit_movzx_eax_al()
    // ------------------------------------------------------------------------
    //   Emit movzx eax, al
    // ------------------------------------------------------------------------
    {
        g(0x0f);
        g(0xb6);
        g(0xc0 | reg_value(HW_RAX));
    }

    void emit_grp3_f7(uint r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 3 F7 /ext on 64-bit register r (neg/not)
    // ------------------------------------------------------------------------
    {
        orex(1, r, 0, 0xf7);
        g(ext | reg_value(r));
    }

    void emit_grp5_ff(uint r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 5 FF /ext on 64-bit register r (inc/dec)
    // ------------------------------------------------------------------------
    {
        orex(1, r, 0, 0xff);
        g(ext | reg_value(r));
    }

    void emit_shift_cl(uint dest, byte ext)
    // ------------------------------------------------------------------------
    //   Emit d3 /ext dest, cl (shl/sar/shr by rcx)
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RCX, dest, 0xd3);
        g(ext | reg_value(dest));
    }

    size_t emit_jcc_rel32(byte cc)
    // ------------------------------------------------------------------------
    //   Emit 0f cc rel32 and return the offset patch site
    // ------------------------------------------------------------------------
    {
        g(0x0f);
        g(cc);
        size_t pos = cursor();
        emit32(0);
        return pos;
    }

    static constexpr byte SSE_ADD = 0x58;
    static constexpr byte SSE_SUB = 0x5c;
    static constexpr byte SSE_MUL = 0x59;
    static constexpr byte SSE_DIV = 0x5e;

    size_t emit_near_jmp_rel32()
    // ------------------------------------------------------------------------
    //   Emit jmp rel32 (e9) and return the disp32 patch site
    // ------------------------------------------------------------------------
    {
        g(0xe9);
        size_t pos = cursor();
        emit32(0);
        return pos;
    }

    void emit_call_rax()
    // ------------------------------------------------------------------------
    //   Emit call rax (ff d0)
    // ------------------------------------------------------------------------
    {
        g(0xff);
        g(0xd0);
    }

    void emit_int3()
    // ------------------------------------------------------------------------
    //   Emit int3 / ud2 trap (cc)
    // ------------------------------------------------------------------------
    {
        g(0xcc);
    }

    void emit_cmp_rax_imm32(uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit cmp rax, imm32
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, 0, 0x3d);
        emit32(imm);
    }

    void emit_alu_rax_imm32(byte op, uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit ALU rax, imm32 (op = 05 add, 2d sub, 25 and, 0d or, 35 xor)
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, 0, op);
        emit32(imm);
    }

    void emit_imul_rax_rax_imm32(uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit imul rax, rax, imm32
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, 0, 0x69);
        g(modrm_rr(HW_RAX, HW_RAX));
        emit32(imm);
    }

    void emit_store_u8_at_rax_cl()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], cl
    // ------------------------------------------------------------------------
    {
        g(0x88);
        g(0x08);
    }

    void emit_store_u16_at_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], cx
    // ------------------------------------------------------------------------
    {
        g(0x66);
        g(0x89);
        g(0x08);
    }

    void emit_store_u32_at_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], ecx
    // ------------------------------------------------------------------------
    {
        g(0x89);
        g(0x08);
    }

    void emit_load_u8_zext_rax()
    // ------------------------------------------------------------------------
    //   Emit mov al, [rax]; movzx eax, al
    // ------------------------------------------------------------------------
    {
        g(0x8a);
        g(0x00);
        emit_movzx_eax_al();
    }

    void emit_load_u16_zext_rax()
    // ------------------------------------------------------------------------
    //   Emit mov ax, [rax]; movzx eax, ax
    // ------------------------------------------------------------------------
    {
        g(0x66);
        g(0x8b);
        g(0x00);
        g(0x0f);
        g(0xb7);
        g(0xc0 | reg_value(HW_RAX));
    }

    void emit_load_u32_rax()
    // ------------------------------------------------------------------------
    //   Emit mov eax, [rax]
    // ------------------------------------------------------------------------
    {
        g(0x8b);
        g(0x00);
    }

    void emit_load_s8_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsx rax/eax, byte ptr [rax]
    // ------------------------------------------------------------------------
    {
        if (ptr_size() == 8)
            g(0x48);
        g(0x0f);
        g(0xbe);
        g(0x00);
    }

    void emit_load_s16_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsx rax/eax, word ptr [rax]
    // ------------------------------------------------------------------------
    {
        g(0x66);
        if (ptr_size() == 8)
            g(0x48);
        g(0x0f);
        g(0xbf);
        g(0x00);
    }

    void emit_load_s32_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsxd rax, dword ptr [rax] (64-bit) or mov eax, [rax] (32-bit)
    // ------------------------------------------------------------------------
    {
        if (ptr_size() == 8)
        {
            g(0x48);
            g(0x63);
            g(0x00);
        }
        else
        {
            g(0x8b);
            g(0x00);
        }
    }

    void emit_sse_rex_r(uint xmm)
    // ------------------------------------------------------------------------
    //   REX prefix for SSE xmm load/store with [rsp+frame+disp] addressing
    // ------------------------------------------------------------------------
    {
        if (ptr_size() == 8)
            g(0x49 | (rex_r(xmm) ? 4 : 0));
        else if (rex_r(xmm))
            g(0x40 | 4);
    }

    void emit_sse_scalar_op(byte prefix, byte op, byte modrm)
    // ------------------------------------------------------------------------
    //   Emit SSE scalar op xmm0, xmm1 (prefix 0xf3=float, 0xf2=double)
    // ------------------------------------------------------------------------
    {
        g(prefix);
        g(0x0f);
        g(op);
        g(modrm);
    }

    void emit_sub_rsp_imm8(byte imm);
    void emit_add_rsp_imm8(byte imm);
    void emit_add_esp_imm(byte imm);

    void emit_insn_ret()
    // ------------------------------------------------------------------------
    //   Emit near return (c3)
    // ------------------------------------------------------------------------
    {
        g(0xc3);
    }

    void emit_jmp_rax()
    // ------------------------------------------------------------------------
    //   Emit jmp rax
    // ------------------------------------------------------------------------
    {
        g(0xff);
        g(0xe0);
    }

    void emit_cdq();
    void emit_xor_rdx_rdx();
    void emit_idiv_rcx();
    void emit_div_rcx();

    void emit_imul_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit imul rax, rcx
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, HW_RCX, 0x0f);
        g(0xaf);
        g(modrm_rr(HW_RAX, HW_RCX));
    }

    void emit_mov_frame_rax();
    void emit_set_arg0_rax();
    void emit_push_reg(uint r);
    void emit_pop_reg(uint r);
    void emit_set_arg0_r(uint r);
    void emit_set_arg0_imm(uint64_t imm);
    void emit_set_arg1_imm(uint64_t imm);
    void emit_cleanup_call_args(uint argc);
    size_t reserve_pc_in_rax();
    void patch_pc_rel(size_t disp_at, uintptr_t target);

    void emit_pred_clear_bit(byte p)
    // ------------------------------------------------------------------------
    //   Clear predicate bit p in HW_FLAGS (r15)
    // ------------------------------------------------------------------------
    {
        load_hw_imm(HW_RAX, ~(1ULL << p));
        emit_and_rr(hw_flags(), HW_RAX);
    }

    void emit_pred_set_bit(byte p)
    // ------------------------------------------------------------------------
    //   Set predicate bit p in HW_FLAGS (r15)
    // ------------------------------------------------------------------------
    {
        load_hw_imm(HW_RAX, 1ULL << p);
        emit_or_rr(hw_flags(), HW_RAX);
    }

    void emit_pred_test_bit(byte p)
    // ------------------------------------------------------------------------
    //   Test predicate bit p in HW_FLAGS (r15) without modifying r15
    // ------------------------------------------------------------------------
    {
        load_hw_imm(HW_RAX, 1ULL << p);
        emit_test_rr(hw_flags(), HW_RAX);
    }

    byte setcc_from_rel(cstring rel)
    // ------------------------------------------------------------------------
    //   Map a comparison name to a SETcc opcode byte
    // ------------------------------------------------------------------------
    {
        if (!strcmp(rel, "eq"))  return SETCC_EQ;
        if (!strcmp(rel, "ne"))  return SETCC_NE;
        if (!strcmp(rel, "lt"))  return SETCC_LT;
        if (!strcmp(rel, "le"))  return SETCC_LE;
        if (!strcmp(rel, "ltu")) return SETCC_LTU;
        if (!strcmp(rel, "leu")) return SETCC_LEU;
        return SETCC_EQ;
    }

    void            orex(int ll, int r, int x, byte c);
    void            emit_rex_modrm(int ll, int r, int x, int b, byte op);
    void            load_hw_imm(uint r, uint64_t imm);
    void            load_local_frame(byte n, uint r);
    void            store_local_frame(uint r, byte n);
    void            load_local(byte n, uint r);
    void            load_float_local(byte n, uint xmm);
    void            store_float_local(byte n, uint xmm);
    void            load_double_local(byte n, uint xmm);
    void            store_double_local(byte n, uint xmm);
    void            set_pred_cmp(byte p, byte cc);
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
    void            sext_reg(uint r, byte w);
    void            load_signext_mem(byte w);
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
    void            reload_frame_reg();
    void            reload_flags_reg();
    void            reload_shadows();
    void            reload_rt_regs();
    void            br_via_ip();
    void            do_ret();
    void            call_ptr(void *fn);
    void            emit_trap(cstring msg);
    void            emit_runtime_call(cstring fn);
    void            emit_runtime_call(cstring fn, tok_t a);
    void            emit_runtime_call(cstring fn, byte a);
    int             local_disp(byte n) const;
};

RECORDER_DECLARE(x86);
RECORDER_DECLARE(x86_ops);
RECORDER_DECLARE(x86_error);
RECORDER_DECLARE(x86_warning);

#endif // X86_H
