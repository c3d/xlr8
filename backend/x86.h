#ifndef X86_H
#define X86_H
// ****************************************************************************
//  x86.h                                                   XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     32-bit x86 code generation backend (also basis for 64-bit)
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
//  REGISTER ALLOCATION CONVENTIONS (x86 / i386)
//  ===========================================================================
//
//  Locals use [ebp - (local+1)*8], ebp is adjusted by op_enter and op_leave
//  args are pointed to by ebp[2+flags+arg] (previous frame)
//  Flags 32-63 are at ebp[1]
//  Args   use [ebp + arg*8] before enter
//
//  Register map (x86 / i386):
//
//    Frame pointer     ebp (HW_FRAME)   Runtime frame for locals
//    Flags/predicates  esi (HW_FLAGS)   First 32 flags
//    Scratch           ecx (HW_SCRATCH) General temporary
//    Value temps       eax, ecx, edx    used by opcode helpers
//    Callee-saved      ebx,esi,edi,ebp  Saved/restored in prologue/epilogue
//    C args (SysV)     edi, esi         first two integer helper args
//    C args (Win64)    ecx, edx         first two integer helper args
//    Stack pointer     esp              native stack
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


RECORDER_DECLARE(x86);
RECORDER_DECLARE(x86_ops);
RECORDER_DECLARE(x86_error);
RECORDER_DECLARE(x86_warning);


struct x86 : backend
// ----------------------------------------------------------------------------
//   Shared x86 family code generator implementing every opcode
// ----------------------------------------------------------------------------
{
    x86(runtime &rt, arch_t arch);
    static x86 *make(runtime &rt) { return new x86(rt, ARCH_I386); }

#include "backend-iface.h"

protected:
    enum x86reg_t : hwreg_t
    // ------------------------------------------------------------------------
    //    Shared register names for x86 and amd64
    // ------------------------------------------------------------------------
    {
        HW_RAX,
        HW_RCX,
        HW_RDX,
        HW_RBX,
        HW_RSP,
        HW_RBP,
        HW_RSI,
        HW_RDI,
        HW_R8,
        HW_R9,
        HW_R10,
        HW_R11,
        HW_R12,
        HW_R13,
        HW_R14,
        HW_R15,

        HW_XMM0D,
        HW_XMM1D,
        HW_XMM2D,
        HW_XMM3D,
        HW_XMM4D,
        HW_XMM5D,
        HW_XMM6D,
        HW_XMM7D,
        HW_XMM8D,
        HW_XMM9D,
        HW_XMM10D,
        HW_XMM11D,
        HW_XMM12D,
        HW_XMM13D,
        HW_XMM14D,
        HW_XMM15D,

        HW_XMM0F,
        HW_XMM1F,
        HW_XMM2F,
        HW_XMM3F,
        HW_XMM4F,
        HW_XMM5F,
        HW_XMM6F,
        HW_XMM7F,
        HW_XMM8F,
        HW_XMM9F,
        HW_XMM10F,
        HW_XMM11F,
        HW_XMM12F,
        HW_XMM13F,
        HW_XMM14F,
        HW_XMM15F,

        HW_COUNT,

        HW_FRAME   = HW_RBP,
        HW_SCRATCH = HW_RCX,
        HW_NONE    = 0,
    };

    bool win64_abi() const
    // ------------------------------------------------------------------------
    //   True when generating code for the Microsoft x64 calling convention
    // ------------------------------------------------------------------------
    {
        return arch() == ARCH_AMD64_WIN;
    }

    bool ptr64() const
    // ------------------------------------------------------------------------
    //   Check if we have 64-bit pointers
    // ------------------------------------------------------------------------
    {
        return arch() != ARCH_I386;
    }

    x86reg_t hw(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Convert hwreg_t to x86reg_t
    // ------------------------------------------------------------------------
    {
        if (r >= HW_COUNT)
        {
            record(x86_error, "Invalid hardware register index %u", r);
            r = HW_RAX;
        }
        return x86reg_t(r);
    }

    x86reg_t hw_flags() const
    // ------------------------------------------------------------------------
    //   Register used for hardware flags
    // ------------------------------------------------------------------------
    //   In 32-bit mode, it only holds flags 0..31. The rest is in ebp[-2]
    {
        return ptr64() ? HW_R15 : HW_RSI;
    }

    x86reg_t hw_arg0() const
    // ------------------------------------------------------------------------
    //   First integer C-helper argument register (SysV rdi / Win64 rcx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RCX : HW_RDI;
    }

    x86reg_t hw_arg1() const
    // ------------------------------------------------------------------------
    //   Second integer C-helper argument register (SysV rsi / Win64 rdx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RDX : HW_RSI;
    }

    int local_disp(int n) const
    // ------------------------------------------------------------------------
    //   Return stack displacement for bytecode local n (ularge_t slots)
    // ------------------------------------------------------------------------
    {
        return ~n * sizeof(ularge_t);
    }

    int arg_disp(int n) const
    // ------------------------------------------------------------------------
    //   Return stack displacement for argument
    // ------------------------------------------------------------------------
    {
        // Base is after FP, optional flag in 32-bit mode, and PC
        const int base = pointer_size() * (2 + !ptr64());
        return n * sizeof(ularge_t) + base;
    }


    // ========================================================================
    //
    //    x86 code generation helpers
    //
    // ========================================================================

    static inline uint reg_value(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Low three bits of a hardware register number
    // ------------------------------------------------------------------------
    {
        return r & 7;
    }

    static inline bool rex_b(x86reg_t r)
    // ------------------------------------------------------------------------
    //   REX.B bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline bool rex_x(x86reg_t x)
    // ------------------------------------------------------------------------
    //   REX.X bit for extended index register x
    // ------------------------------------------------------------------------
    {
        return (x >> 3) & 1;
    }

    static inline bool rex_r(x86reg_t r)
    // ------------------------------------------------------------------------
    //   REX.R bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline byte modrm_rr(x86reg_t r, x86reg_t x)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, x (register-direct, both in registers)
    // ------------------------------------------------------------------------
    {
        return byte(0xc0 | (reg_value(r) << 3) | reg_value(x));
    }

    static inline byte modrm_rm(x86reg_t r, x86reg_t b)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, [b] (register r, memory at base b)
    // ------------------------------------------------------------------------
    {
        return byte(0x00 | (reg_value(r) << 3) | reg_value(b));
    }

    static inline byte modrm_mr(x86reg_t b, x86reg_t r)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op [b], r (memory at base b, register r)
    // ------------------------------------------------------------------------
    {
        return byte(0x00 | (reg_value(r) << 3) | reg_value(b));
    }

    byte branch_offset8()
    // ------------------------------------------------------------------------
    //   Emit an 8-bit branch offset
    // ------------------------------------------------------------------------
    {
        intptr_t offset = branch_offset();
        if (offset < -128 || offset > 127)
            record(x86_error, "Invalid 8-bit branch offset %ld", offset);
        return byte(offset);
    }



    // =========================================================================
    //
    //  Instruction emitters
    //
    // =========================================================================
    //  Helper names use Intel operand order (dest, src).  ModR/M encodes the
    //  source in the reg field and the destination in r/m, so every rr helper
    //  ends up calling modrm_rr(src, dest).

    static constexpr byte JCC_JC    = 0x82; // jb / jc
    static constexpr byte JCC_JNC   = 0x83; // jae / jnc
    static constexpr byte JCC_JZ    = 0x84; // je / jz
    static constexpr byte JCC_JNZ   = 0x85; // jne / jnz
    static constexpr byte JCC_JL    = 0x8c; // jl / jnge
    static constexpr byte JCC_JLE   = 0x8e; // jle / jng
    static constexpr byte JCC_JGE   = 0x8d; // jge / jnl
    static constexpr byte JCC_JG    = 0x8f; // jg / jnle

    static constexpr byte SETCC_EQ  = 0x94; // sete / setz
    static constexpr byte SETCC_NE  = 0x95; // setne / setnz
    static constexpr byte SETCC_LT  = 0x9c; // setl / setnge
    static constexpr byte SETCC_LE  = 0x9e; // setle / setng
    static constexpr byte SETCC_LTU = 0x92; // setb / setnae
    static constexpr byte SETCC_LEU = 0x96; // setbe / setna

    void emit_op_rr(byte op, x86reg_t dest, x86reg_t src, int ll = 1)
    // ------------------------------------------------------------------------
    //   Emit a two-register ALU opcode: op dest, src
    // ------------------------------------------------------------------------
    {
        orex(ll, src, dest, op);
        emit8(modrm_rr(src, dest));
    }

    void emit_mov_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x89, d, s); }
    void emit_add_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x01, d, s); }
    void emit_sub_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x29, d, s); }
    void emit_and_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x21, d, s); }
    void emit_or_rr  (x86reg_t d, x86reg_t s) { emit_op_rr(0x09, d, s); }
    void emit_xor_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x31, d, s); }
    void emit_cmp_rr (x86reg_t d, x86reg_t s) { emit_op_rr(0x39, d, s); }
    void emit_test_rr(x86reg_t d, x86reg_t s) { emit_op_rr(0x85, d, s); }
    void emit_xchg_rr(x86reg_t d, x86reg_t s) { emit_op_rr(0x87, d, s); }

    void emit_imul_rr(x86reg_t d, x86reg_t s)
    // ------------------------------------------------------------------------
    //   Emit imul d, s (0F AF /r — dest in ModRM.reg, unlike emit_op_rr)
    // ------------------------------------------------------------------------
    {
        orex(1, d, s, 0x0f);
        emit8(0xaf);
        emit8(modrm_rr(d, s));
    }

    void emit_idiv_r(x86reg_t s)
    // ------------------------------------------------------------------------
    //   Emit idiv s (F7 /7); dividend is implicit rdx:rax
    // ------------------------------------------------------------------------
    {
        emit_grp3_f7(s, 0xf8);
    }

    void emit_div_r(x86reg_t s)
    // ------------------------------------------------------------------------
    //   Emit div s (F7 /6); dividend is implicit rdx:rax
    // ------------------------------------------------------------------------
    {
        emit_grp3_f7(s, 0xf0);
    }

    void emit_divmod_rr(x86reg_t d, x86reg_t s, bool mod, bool sgn)
    // ------------------------------------------------------------------------
    //   Emit (i)div s with dividend in d; result (quot/rem) written back to d
    // ------------------------------------------------------------------------
    {
        if (d != HW_RAX)
            emit_mov_rr(HW_RAX, d);
        if (sgn)
            emit_cdq();
        else
            emit_xor_rdx_rdx();
        emit_grp3_f7(s, sgn ? 0xf8 : 0xf0);
        if (mod && d != HW_RDX)
            emit_mov_rr(d, HW_RDX);
        else if (!mod && d != HW_RAX)
            emit_mov_rr(d, HW_RAX);
    }
    void emit_udiv_rr(x86reg_t d, x86reg_t s) { emit_divmod_rr(d, s, 0, 0); }
    void emit_idiv_rr(x86reg_t d, x86reg_t s) { emit_divmod_rr(d, s, 0, 1); }
    void emit_umod_rr(x86reg_t d, x86reg_t s) { emit_divmod_rr(d, s, 1, 0); }
    void emit_imod_rr(x86reg_t d, x86reg_t s) { emit_divmod_rr(d, s, 1, 1); }

    void emit_divmod_imm(x86reg_t d, uint64_t imm, bool mod, bool sgn)
    // ------------------------------------------------------------------------
    //   Emit (i)div by immediate; dividend in d, result written back to d
    // ------------------------------------------------------------------------
    {
        if (d != HW_RAX)
            emit_mov_rr(HW_RAX, d);
        set_imm(HW_RCX, imm);
        if (sgn)
            emit_cdq();
        else
            emit_xor_rdx_rdx();
        emit_grp3_f7(HW_RCX, sgn ? 0xf8 : 0xf0);
        if (mod && d != HW_RDX)
            emit_mov_rr(d, HW_RDX);
        else if (!mod && d != HW_RAX)
            emit_mov_rr(d, HW_RAX);
    }
    void emit_udiv_imm(x86reg_t d, uint64_t i) { emit_divmod_imm(d, i, 0, 0); }
    void emit_idiv_imm(x86reg_t d, uint64_t i) { emit_divmod_imm(d, i, 0, 1); }
    void emit_umod_imm(x86reg_t d, uint64_t i) { emit_divmod_imm(d, i, 1, 0); }
    void emit_imod_imm(x86reg_t d, uint64_t i) { emit_divmod_imm(d, i, 1, 1); }

    void emit_setcc_al(byte cc)
    // ------------------------------------------------------------------------
    //   Emit SETcc al (low byte of rax must hold the value being tested)
    // ------------------------------------------------------------------------
    {
        emit8(0x0f);
        emit8(cc);
        emit8(0xc0 | reg_value(HW_RAX));
    }

    void emit_movzx_eax_al()
    // ------------------------------------------------------------------------
    //   Emit movzx eax, al
    // ------------------------------------------------------------------------
    {
        emit8(0x0f);
        emit8(0xb6);
        emit8(0xc0 | reg_value(HW_RAX));
    }

    void emit_grp3_f7(x86reg_t r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 3 F7 /ext on 64-bit register r (neg/not)
    // ------------------------------------------------------------------------
    {
        // /ext targets r/m; high registers need REX.B (orex x), not REX.R
        orex(1, HW_NONE, r, 0xf7);
        emit8(ext | reg_value(r));
    }

    void emit_grp5_ff(x86reg_t r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 5 FF /ext on 64-bit register r (inc/dec)
    // ------------------------------------------------------------------------
    {
        // /ext targets r/m; high registers need REX.B (orex x), not REX.R
        orex(1, HW_NONE, r, 0xff);
        emit8(ext | reg_value(r));
    }

    void emit_modrm_frame(byte reg_field, int disp)
    // ------------------------------------------------------------------------
    //   Emit ModRM(+SIB)+disp for [HW_FRAME+disp]; reg_field is ModRM.reg
    // ------------------------------------------------------------------------
    {
        if (disp < -128 || disp > 127)
        {
            // [HW_FRAME + disp32]
            emit8(byte(0x80 | ((reg_field & 7) << 3) | 4));
            emit8(byte((reg_value(HW_RSP) << 3) | reg_value(HW_FRAME)));
            emit32le(uint32_t(disp));
            return;
        }

        // rbp/r13 cannot use mod=00 without a displacement, emit imm8(0)
        uint fr = reg_value(HW_FRAME);
        if (fr == 5 || disp)
        {
            emit8(byte((1 << 6) | ((reg_field & 7) << 3) | fr));
            emit8(byte(disp));
            return;
        }
        emit8(byte(((reg_field & 7) << 3) | fr));
        if (fr == 4)
            emit8(byte((reg_value(HW_RSP) << 3) | fr);
    }

    void emit_rex_frame_rm()
    // ------------------------------------------------------------------------
    //   REX.W (x86-64) and REX.B when HW_FRAME is r8–r15
    // ------------------------------------------------------------------------
    {
        x86reg_t frame = HW_FRAME;
        int w = (ptr64()) ? 8 : 0;
        if (w || frame >= 8)
            emit8(0x40 | w | (rex_b(frame) ? 1 : 0));
    }

    void emit_grp5_frame(byte n, byte op_ext)
    // ------------------------------------------------------------------------
    //   Emit FF /op_ext on [frame+local_disp(n)] (inc=0, dec=1)
    // ------------------------------------------------------------------------
    {
        emit_rex_frame_rm();
        emit8(0xff);
        emit_modrm_frame(op_ext, local_disp(n));
    }

    void emit_alu_imm_frame(byte n, byte op_ext, int32_t imm)
    // ------------------------------------------------------------------------
    //   Emit add/sub/… [frame+disp], imm (83 ib or 81 id); op_ext=/0 add /5 sub
    // ------------------------------------------------------------------------
    {
        emit_rex_frame_rm();
        if (imm == int8_t(imm))
        {
            emit8(0x83);
            emit_modrm_frame(op_ext, local_disp(n));
            emit8(byte(int8_t(imm)));
        }
        else
        {
            emit8(0x81);
            emit_modrm_frame(op_ext, local_disp(n));
            emit32le(uint32_t(imm));
        }
    }

    void emit_alu_imm(x86reg_t d, byte op_ext, int64_t imm)
    // ------------------------------------------------------------------------
    //   Emit add/sub/and/or/xor d, imm (83/81); large imm via scratch + rr
    // ------------------------------------------------------------------------
    {
        if (imm != int32_t(imm))
        {
            x86reg_t t = d == HW_RCX ? HW_RAX : HW_RCX;
            set_imm(t, uint64_t(imm));
            switch (op_ext)
            {
            case 0: emit_add_rr(d, t); break;
            case 5: emit_sub_rr(d, t); break;
            case 4: emit_and_rr(d, t); break;
            case 1: emit_or_rr (d, t); break;
            case 6: emit_xor_rr(d, t); break;
            default:
                record(x86_error, "emit_alu_imm bad ext %u", op_ext);
                break;
            }
            return;
        }
        int32_t i32 = int32_t(imm);
        if (i32 == int8_t(i32))
        {
            orex(1, HW_NONE, d, 0x83);
            emit8(byte(0xc0 | (op_ext << 3) | reg_value(d)));
            emit8(byte(int8_t(i32)));
        }
        else
        {
            orex(1, HW_NONE, d, 0x81);
            emit8(byte(0xc0 | (op_ext << 3) | reg_value(d)));
            emit32le(uint32_t(i32));
        }
    }

    void emit_imul_imm(x86reg_t d, int64_t imm)
    // ------------------------------------------------------------------------
    //   Emit imul d, d, imm (6B/69); large imm via scratch + imul_rr
    // ------------------------------------------------------------------------
    {
        if (imm != int32_t(imm))
        {
            x86reg_t t = d == HW_RCX ? HW_RAX : HW_RCX;
            set_imm(t, uint64_t(imm));
            emit_imul_rr(d, t);
            return;
        }
        int32_t i32 = int32_t(imm);
        if (i32 == int8_t(i32))
        {
            orex(1, d, d, 0x6b);
            emit8(modrm_rr(d, d));
            emit8(byte(int8_t(i32)));
        }
        else
        {
            orex(1, d, d, 0x69);
            emit8(modrm_rr(d, d));
            emit32le(uint32_t(i32));
        }
    }

    void emit_shift_rr(x86reg_t d, x86reg_t s, byte ext)
    // ------------------------------------------------------------------------
    //   Emit d = d shift s (D3 /ext). Count must be in CL; move/xchg if needed.
    // ------------------------------------------------------------------------
    {
        if (s != HW_RCX)
        {
            if (d == HW_RCX)
            {
                // rcx=value, s=count → xchg, shift s by cl, xchg back
                emit_xchg_rr(HW_RCX, s);
                orex(1, HW_NONE, s, 0xd3);
                emit8(ext | reg_value(s));
                emit_xchg_rr(HW_RCX, s);
                return;
            }
            emit_mov_rr(HW_RCX, s);
        }
        orex(1, HW_NONE, d, 0xd3);
        emit8(ext | reg_value(d));
    }

    void emit_shift_imm8(x86reg_t dst, byte ext, byte amt)
    // ------------------------------------------------------------------------
    //   Emit c1 /ext dst, imm8 (shl/sar/shr by immediate); amt==1 uses d1
    // ------------------------------------------------------------------------
    {
        byte mask = ptr64() ? 63 : 31;
        amt = byte(amt & mask);
        if (amt == 1)
        {
            orex(1, HW_NONE, dst, 0xd1);
            emit8(ext | reg_value(dst));
            return;
        }
        orex(1, HW_NONE, dst, 0xc1);
        emit8(ext | reg_value(dst));
        emit8(amt);
    }

    hwcode_t emit_jcc_rel32(byte cc)
    // ------------------------------------------------------------------------
    //   Emit 0f cc rel32 and return the offset patch site
    // ------------------------------------------------------------------------
    {
        emit8(0x0f);
        emit8(cc);
        emit32le(0);
        return cursor() - 4;
    }

    hwcode_t emit_jcc_rel8(byte cc)
    // ------------------------------------------------------------------------
    //   Emit 7x rel8 (cc is the near form 0x8x; short is cc-0x10)
    // ------------------------------------------------------------------------
    {
        emit8(byte(cc - 0x10));
        emit8(0);
        return cursor() - 1;
    }

    hwcode_t emit_near_jmp_rel32()
    // ------------------------------------------------------------------------
    //   Emit jmp rel32 (e9) and return the disp32 patch site
    // ------------------------------------------------------------------------
    {
        emit8(0xe9);
        emit32le(0);
        return cursor() - 4;
    }

    hwcode_t emit_jmp_rel8()
    // ------------------------------------------------------------------------
    //   Emit jmp rel8 (eb) and return the disp8 patch site
    // ------------------------------------------------------------------------
    {
        emit8(0xeb);
        emit8(0);
        return cursor() - 1;
    }

    static constexpr byte SSE_ADD = 0x58;
    static constexpr byte SSE_SUB = 0x5c;
    static constexpr byte SSE_MUL = 0x59;
    static constexpr byte SSE_DIV = 0x5e;

    void emit_jmp_rr(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Emit jmp r (FF /4); REX.B for r8–r15
    // ------------------------------------------------------------------------
    {
        orex(0, HW_NONE, r, 0xff);
        emit8(byte(0xe0 | reg_value(r)));
    }

    void emit_call_rr(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Emit call r (FF /2); REX.B for r8–r15
    // ------------------------------------------------------------------------
    {
        orex(0, HW_NONE, r, 0xff);
        emit8(byte(0xd0 | reg_value(r)));
    }

    void emit_int3()
    // ------------------------------------------------------------------------
    //   Emit int3 / ud2 trap (cc)
    // ------------------------------------------------------------------------
    {
        emit8(0xcc);
    }

    void emit_cmp_rax_imm32(uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit cmp rax, imm32
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, HW_NONE, 0x3d);
        emit32le(imm);
    }

    void emit_alu_rax_imm32(byte op, uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit ALU rax, imm32 (op = 05 add, 2d sub, 25 and, 0d or, 35 xor)
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, HW_NONE, op);
        emit32le(imm);
    }

    void emit_imul_rax_rax_imm32(uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit imul rax, rax, imm32
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, HW_NONE, 0x69);
        emit8(modrm_rr(HW_RAX, HW_RAX));
        emit32le(imm);
    }

    void emit_rex_mem(int w, x86reg_t reg, x86reg_t base, bool force = false)
    // ------------------------------------------------------------------------
    //   REX for op reg, [base]: W, R=reg, B=base; force for sil/dil/bpl/spl
    // ------------------------------------------------------------------------
    {
        if (w || force || reg >= 8 || base >= 8)
            emit8(byte(0x40 | w | (rex_r(reg) ? 4 : 0) | (rex_b(base) ? 1 : 0)));
    }

    void emit_modrm_reg_base(x86reg_t reg, x86reg_t base, int disp = 0)
    // ------------------------------------------------------------------------
    //   ModRM(+SIB+disp) for op reg, [base+disp]
    // ------------------------------------------------------------------------
    {
        uint r = reg_value(reg);
        uint b = reg_value(base);

        if (disp < -128 || disp > 127)
        {
            emit8(byte(0x80 | (r << 3) | b));
            if (b == 4)
                emit8(0x24); // SIB: no index, base = rsp/r12
            emit32le(uint32_t(disp));
            return;
        }
        // rbp/r13 need a displacement even when disp==0
        if (disp != 0 || b == 5)
        {
            emit8(byte(0x40 | (r << 3) | b));
            if (b == 4)
                emit8(0x24);
            emit8(byte(disp));
            return;
        }
        emit8(byte((r << 3) | b));
        if (b == 4)
            emit8(0x24);
    }

    void emit_store_u8(x86reg_t dstptr, x86reg_t src, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov byte ptr [dstptr+offs], src.l
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(0, src, dstptr, reg_value(src) >= 4);
        emit8(0x88);
        emit_modrm_reg_base(src, dstptr, offs);
    }

    void emit_store_u16(x86reg_t dstptr, x86reg_t src, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov word ptr [dstptr+offs], src
    // ------------------------------------------------------------------------
    {
        emit8(0x66);
        emit_rex_mem(0, src, dstptr);
        emit8(0x89);
        emit_modrm_reg_base(src, dstptr, offs);
    }

    void emit_store_u32(x86reg_t dstptr, x86reg_t src, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov dword ptr [dstptr+offs], src
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(0, src, dstptr);
        emit8(0x89);
        emit_modrm_reg_base(src, dstptr, offs);
    }

    void emit_store_u64(x86reg_t dstptr, x86reg_t src, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov [dstptr+offs], src (64-bit; 32-bit store on i386)
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(ptr64() ? 8 : 0, src, dstptr);
        emit8(0x89);
        emit_modrm_reg_base(src, dstptr, offs);
    }

    void emit_load_u8(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit movzx dst, byte ptr [srcptr+offs]
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(0, dst, srcptr);
        emit8(0x0f);
        emit8(0xb6);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_u16(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit movzx dst, word ptr [srcptr+offs]
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(0, dst, srcptr);
        emit8(0x0f);
        emit8(0xb7);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_u32(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov dst.e, dword ptr [srcptr+offs] (zero-extends to 64-bit)
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(0, dst, srcptr);
        emit8(0x8b);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_u64(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit mov dst, qword ptr [srcptr+offs] (32-bit load on i386)
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(ptr64() ? 8 : 0, dst, srcptr);
        emit8(0x8b);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_s8(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit movsx dst, byte ptr [srcptr+offs]
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(ptr64() ? 8 : 0, dst, srcptr);
        emit8(0x0f);
        emit8(0xbe);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_s16(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit movsx dst, word ptr [srcptr+offs]
    // ------------------------------------------------------------------------
    {
        emit_rex_mem(ptr64() ? 8 : 0, dst, srcptr);
        emit8(0x0f);
        emit8(0xbf);
        emit_modrm_reg_base(dst, srcptr, offs);
    }

    void emit_load_s32(x86reg_t dst, x86reg_t srcptr, int offs = 0)
    // ------------------------------------------------------------------------
    //   Emit movsxd dst, dword ptr [srcptr+offs] (64-bit) or mov
    // ------------------------------------------------------------------------
    {
        if (ptr64())
        {
            emit_rex_mem(8, dst, srcptr);
            emit8(0x63);
            emit_modrm_reg_base(dst, srcptr, offs);
        }
        else
        {
            emit_load_u32(dst, srcptr, offs);
        }
    }

    void emit_sse_rex_r(x86reg_t xmm)
    // ------------------------------------------------------------------------
    //   REX prefix for SSE xmm load/store with [rbp+disp] addressing
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x40 | (rex_r(xmm) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        else if (rex_r(xmm))
            emit8(0x40 | 4);
    }

    void emit_sse_scalar_op(byte prefix, byte op, x86reg_t d, x86reg_t s)
    // ------------------------------------------------------------------------
    //   Emit SSE scalar op d, s (prefix 0xf3=float, 0xf2=double)
    // ------------------------------------------------------------------------
    {
        emit8(prefix);
        if (rex_r(d) || rex_b(s))
            emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
        emit8(0x0f);
        emit8(op);
        emit8(modrm_rr(d, s));
    }

    void emit_set_xmm_imm(x86reg_t xmm, uint64_t bits, bool is_double)
    // ------------------------------------------------------------------------
    //   Emit a movd xmm, r32 or movq xmm, r64 to load a float/double constant
    // ------------------------------------------------------------------------
    {
        set_imm(HW_RAX, bits);
        emit8(0x66);         // movd/movq xmm, rax
        if (is_double && ptr64())
            emit8(0x48 | (rex_r(xmm) ? 4 : 0));  // REX.W + R
        else if (rex_r(xmm))
            emit8(0x40 | 4);
        emit8(0x0f);
        emit8(0x6e);
        emit8(byte(0xc0 | (reg_value(xmm) << 3) | reg_value(HW_RAX)));
    }

    void emit_sub_rsp_imm(int32_t imm)
    // ------------------------------------------------------------------------
    //   Emit sub rsp/esp, imm (83 ib or 81 id)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        if (imm == int8_t(imm))
        {
            emit8(0x83);
            emit8(0xec);
            emit8(byte(int8_t(imm)));
        }
        else
        {
            emit8(0x81);
            emit8(0xec);
            emit32le(uint32_t(imm));
        }
    }

    void emit_add_rsp_imm(int32_t imm)
    // ------------------------------------------------------------------------
    //   Emit add rsp/esp, imm (83 ib or 81 id)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        if (imm == int8_t(imm))
        {
            emit8(0x83);
            emit8(0xc4);
            emit8(byte(int8_t(imm)));
        }
        else
        {
            emit8(0x81);
            emit8(0xc4);
            emit32le(uint32_t(imm));
        }
    }

    void emit_add_esp_imm(byte imm)
    // ------------------------------------------------------------------------
    //   Emit add esp, imm8 (cdecl stack cleanup on i386)
    // ------------------------------------------------------------------------
    {
        emit8(0x83);
        emit8(0xc4);
        emit8(byte(imm));
    }

    void emit_insn_ret()
    // ------------------------------------------------------------------------
    //   Emit near return (c3)
    // ------------------------------------------------------------------------
    {
        emit8(0xc3);
    }

    void emit_cdq()
    // ------------------------------------------------------------------------
    //   Sign-extend eax into edx:eax (cdq/cqo)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x99);
    }

    void emit_xor_rdx_rdx()
    // ------------------------------------------------------------------------
    //   Clear edx/rdx
    // ------------------------------------------------------------------------
    {
        emit_xor_rr(HW_RDX, HW_RDX);
    }

    void emit_idiv_rcx()
    // ------------------------------------------------------------------------
    //   Signed divide edx:eax by rcx/ecx
    // ------------------------------------------------------------------------
    {
        emit_idiv_r(HW_RCX);
    }

    void emit_div_rcx()
    // ------------------------------------------------------------------------
    //   Unsigned divide edx:eax by rcx/ecx
    // ------------------------------------------------------------------------
    {
        emit_div_r(HW_RCX);
    }

    void emit_mov_frame_rax()
    // ------------------------------------------------------------------------
    //   Reload frame pointer register from rax
    // ------------------------------------------------------------------------
    {
        emit_mov_rr(HW_FRAME, HW_RAX);
    }

    void emit_set_arg0_rax()
    // ------------------------------------------------------------------------
    //   Pass rax as first C helper argument
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit_mov_rr(hw_arg0(), HW_RAX);
        else
            emit_push_reg(HW_RAX);
    }

    void emit_push_reg(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Push a general-purpose register (cdecl / SysV argument)
    // ------------------------------------------------------------------------
    {
        if (ptr64() && rex_b(r))
            emit8(0x41);
        emit8(byte(0x50 + reg_value(r)));
    }

    void emit_pop_reg(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Pop into a general-purpose register
    // ------------------------------------------------------------------------
    {
        if (ptr64() && rex_b(r))
            emit8(0x41);
        emit8(byte(0x58 + reg_value(r)));
    }

    void emit_set_arg0_r(x86reg_t r)
    // ------------------------------------------------------------------------
    //   Pass register r as first C helper argument
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit_mov_rr(hw_arg0(), r);
        else
            emit_push_reg(r);
    }

    void emit_set_arg0_imm(uint64_t imm)
    // ------------------------------------------------------------------------
    //   Pass immediate as first C helper argument
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            set_imm(hw_arg0(), imm);
        else
        {
            set_imm(HW_RAX, imm);
            emit_push_reg(HW_RAX);
        }
    }

    void emit_set_arg1_imm(uint64_t imm)
    // ------------------------------------------------------------------------
    //   Pass immediate as second C helper argument (cdecl: push first)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            set_imm(hw_arg1(), imm);
        else
        {
            set_imm(HW_RAX, imm);
            emit_push_reg(HW_RAX);
        }
    }

    void emit_cleanup_call_args(uint argc)
    // ------------------------------------------------------------------------
    //   Caller pops stack arguments after cdecl call on i386
    // ------------------------------------------------------------------------
    {
        if (pointer_size() == 4 && argc)
            emit_add_esp_imm(byte(argc * pointer_size()));
    }

    void emit_cmov_rr(byte cc, x86reg_t d, x86reg_t s)
    // ------------------------------------------------------------------------
    //   Emit cmovcc d, s (0F 4x /r). cc is SETCC/JCC opcode (low nibble used).
    // ------------------------------------------------------------------------
    {
        orex(1, d, s, 0x0f);
        emit8(byte(0x40 | (cc & 0x0f)));
        emit8(modrm_rr(d, s));
    }

    void emit_btx_imm8(x86reg_t r, byte ext, byte bit)
    // ------------------------------------------------------------------------
    //   Emit 0f ba /ext r, imm8 (BT/BTS/BTR/BTC); sets CF from the bit
    // ------------------------------------------------------------------------
    {
        assert (bit < (ptr64() ? 64 : 32));
        orex(1, HW_NONE, r, 0x0f);
        emit8(0xba);
        emit8(byte(0xc0 | (ext << 3) | reg_value(r)));
        emit8(bit);
    }

    void emit_pred_cmp(byte p, x86reg_t x, x86reg_t y, byte cc)
    // ------------------------------------------------------------------------
    //    Set predicate p according to comparison between x and y
    // ------------------------------------------------------------------------
    //   Force bit n of a to 0/1 from (cmp b,c) under condition cc:
    //     mov tmp,a; btr a,n; bts tmp,n; cmp b,c; cmovcc a,tmp
    //   a must be distinct from b, c, and tmp.
    {
        x86reg_t flags = hw_flags();
        emit_mov_rr(HW_RAX, flags);
        emit_btx_imm8(flags, /*BTR*/ 6, n);
        emit_btx_imm8(HW_RAX, /*BTS*/ 5, n);
        emit_cmp_rr(x, y);
        emit_cmov_rr(cc, flags, HW_RAX);
    }

    void emit_btx_frame_imm8(int offs, byte ext, byte bit)
    // ------------------------------------------------------------------------
    //   Emit 0f ba /ext r, imm8 (BT/BTS/BTR/BTC); sets CF from the bit
    // ------------------------------------------------------------------------
    {
        assert (bit < (ptr64() ? 64 : 32));
        orex(1, HW_NONE, HW_FRAME, 0x0f);
        emit8(0xba);
        emit_modrm_frame(ext, offs);
        emit8(bit);
    }

    void emit_pred_op(byte p, byte op_ext)
    // ------------------------------------------------------------------------
    //   Predicate operations
    // ------------------------------------------------------------------------
    {
        if (ptr64() || p < 32)
        {
            emit_btx_imm8(hw_flags(), op_ext, p);
        }
        else
        {
            emit_btx_frame_imm8(8, op_ext, p & 31);
        }
    }

    void emit_pred_clear_bit(byte p)
    // ------------------------------------------------------------------------
    //   Clear predicate bit p in HW_FLAGS (BTR; CF = old bit)
    // ------------------------------------------------------------------------
    {
        emit_pred_op(p, /* BTR */ 6);
    }

    void emit_pred_set_bit(byte p)
    // ------------------------------------------------------------------------
    //   Set predicate bit p in HW_FLAGS (BTS; CF = old bit)
    // ------------------------------------------------------------------------
    {
        emit_pred_op(p, /* BTS */ 5);
    }

    void emit_pred_test_bit(byte p)
    // ------------------------------------------------------------------------
    //   Test predicate bit p in HW_FLAGS (BT; CF = bit, register unchanged)
    // ------------------------------------------------------------------------
    {
        emit_pred_op(p, /* BT */ 4);
    }

    byte            setcc_from_rel(cstring rel);
    void            orex(int ll, x86reg_t r, x86reg_t x, byte c);
    void            emit_rex_modrm(int l,
                                   x86reg_t r, x86reg_t x, x86reg_t b, byte op);
    void            set_imm(x86reg_t r, uint64_t imm);
    void            load_local(local_t loc, x86reg_t r);
    void            store_local(x86reg_t r, local_t loc);
    x864reg_t       load_fp(local_t loc, byte sz);
    void            store_fp(local_t loc, x86reg_t reg);
    void            mov_local(local_t d, local_t s);
    void            swap_locals(local_t d, local_t s);
    void            store_imm(local_t d, value_t v);
    void            store_float(local_t d, float v);
    void            store_double(local_t d, double v);
    void            binop_local(local_t d, local_t s, op_t op, byte fpsz=0);
    void            binop_imm(local_t d, value_t v, op_t op);
    void            cmp_local(byte p, local_t d, local_t s, byte jcc);
    void            cmp_imm(byte p, local_t d, value_t v, byte jcc);
    void            cmp_uimm(byte p, local_t d, uvalue_t v, byte jcc);
    void            set_pred(byte p, bool on);
    void            cond_mov_local(byte p, local_t d, local_t s);
    void            select_local(byte p, local_t d, local_t s, byte x);
    void            neg_local(local_t d);
    void            not_local(local_t d);
    void            incr_local(local_t d);
    void            decr_local(local_t d);
    void            sext_local(local_t d, byte w);
    void            zext_local(local_t d, byte w);
    void            sext_reg(x86reg_t r, byte w);
    void            load_signext_mem(byte w, int offs = 0);
    void            store_mem(local_t d, local_t s, size_t sz);
    void            load_mem(local_t d, local_t s, size_t sz);
    void            store_mem_off(local_t d, local_t s, value_t o, size_t sz);
    void            load_mem_off(local_t d, local_t s, value_t o, size_t sz);
    void            load_signext(local_t d, local_t s, byte w);
    void            load_signext_off(local_t d, local_t s, byte w, value_t o);
    void            frame_addr(local_t d, local_t s);
    void            bra_pred(byte p, byte_p target, bool sense);
    void            bra_test(local_t s, byte_p target, byte cc);
    void            push_pc();
    void            load_pc(local_t d);
    void            load_pca(local_t d, broff_t b);
    virtual void    prologue();
    virtual void    epilogue();
    void            call_ptr(void (*fn)());
    template <typename T, typename ...U>
    void            call_ptr(T (*fn)(U...)) { return call(((void (*)()) fn)); }
    void            dispatch(local_t loc, byte_p &pc, byte jcc);

protected:
    using relocations_t = std::vector<hwcode_t>;
    relocations_t relocations;
};

#endif // X86_H
