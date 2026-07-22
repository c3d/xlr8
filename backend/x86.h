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
//  Locals use [ebp + local*8], ebp is adjusted by op_enter and op_leave
//  args are pointed to by ebp[-1]
//  Flags 32-63 are at ebp[-2]
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
    x86(arch_t arch);
    static x86 *make() { return new x86(ARCH_I386); }

#include "backend-iface.h"

protected:
    enum hwreg_t : ::hwreg_t
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
        HW_XMM0,
        HW_XMM1,
        HW_XMM2,
        HW_XMM3,
        HW_XMM4,
        HW_XMM5,
        HW_XMM6,
        HW_XMM7,
        HW_XMM8,
        HW_XMM9,
        HW_XMM10,
        HW_XMM11,
        HW_XMM12,
        HW_XMM13,
        HW_XMM14,
        HW_COUNT,

        HW_FRAME   = HW_RBP,
        HW_SCRATCH = HW_RCX,
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

    hwreg_t hw_flags() const
    // ------------------------------------------------------------------------
    //   Register used for hardware flags
    // ------------------------------------------------------------------------
    //   In 32-bit mode, it only holds flags 0..31. The rest is in ebp[-2]
    {
        return ptr64() ? HW_R15 : HW_RSI;
    }

    hwreg_t hw_arg0() const
    // ------------------------------------------------------------------------
    //   First integer C-helper argument register (SysV rdi / Win64 rcx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RCX : HW_RDI;
    }

    hwreg_t hw_arg1() const
    // ------------------------------------------------------------------------
    //   Second integer C-helper argument register (SysV rsi / Win64 rdx)
    // ------------------------------------------------------------------------
    {
        return win64_abi() ? HW_RDX : HW_RSI;
    }

    static inline uint reg_value(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Low three bits of a hardware register number
    // ------------------------------------------------------------------------
    {
        return r & 7;
    }

    static inline bool rex_b(hwreg_t r)
    // ------------------------------------------------------------------------
    //   REX.B bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline bool rex_x(hwreg_t x)
    // ------------------------------------------------------------------------
    //   REX.X bit for extended index register x
    // ------------------------------------------------------------------------
    {
        return (x >> 3) & 1;
    }

    static inline bool rex_r(hwreg_t r)
    // ------------------------------------------------------------------------
    //   REX.R bit for extended register r
    // ------------------------------------------------------------------------
    {
        return (r >> 3) & 1;
    }

    static inline byte modrm_rr(hwreg_t r, hwreg_t x)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, x (register-direct, both in registers)
    // ------------------------------------------------------------------------
    {
        return byte(0xc0 | (reg_value(r) << 3) | reg_value(x));
    }

    static inline byte modrm_rm(hwreg_t r, hwreg_t b)
    // ------------------------------------------------------------------------
    //   ModR/M byte for op r, [b] (register r, memory at base b)
    // ------------------------------------------------------------------------
    {
        return byte(0x00 | (reg_value(r) << 3) | reg_value(b));
    }

    static inline byte modrm_mr(hwreg_t b, hwreg_t r)
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

    void emit_op_rr(byte op, hwreg_t dest, hwreg_t src, int ll = 1)
    // ------------------------------------------------------------------------
    //   Emit a two-register ALU opcode: op dest, src
    // ------------------------------------------------------------------------
    {
        orex(ll, src, dest, op);
        emit8(modrm_rr(src, dest));
    }

    void emit_mov_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x89, dst, src); }
    void emit_add_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x01, dst, src); }
    void emit_sub_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x29, dst, src); }
    void emit_and_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x21, dst, src); }
    void emit_or_rr  (hwreg_t dst, hwreg_t src) { emit_op_rr(0x09, dst, src); }
    void emit_xor_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x31, dst, src); }
    void emit_cmp_rr (hwreg_t dst, hwreg_t src) { emit_op_rr(0x39, dst, src); }
    void emit_test_rr(hwreg_t dst, hwreg_t src) { emit_op_rr(0x85, dst, src); }

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

    void emit_grp3_f7(hwreg_t r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 3 F7 /ext on 64-bit register r (neg/not)
    // ------------------------------------------------------------------------
    {
        // /ext targets r/m; high registers need REX.B (orex x), not REX.R
        orex(1, 0, r, 0xf7);
        emit8(ext | reg_value(r));
    }

    void emit_grp5_ff(hwreg_t r, byte ext)
    // ------------------------------------------------------------------------
    //   Emit Group 5 FF /ext on 64-bit register r (inc/dec)
    // ------------------------------------------------------------------------
    {
        // /ext targets r/m; high registers need REX.B (orex x), not REX.R
        orex(1, 0, r, 0xff);
        emit8(ext | reg_value(r));
    }

    void emit_modrm_frame(byte reg_field, int disp)
    // ------------------------------------------------------------------------
    //   Emit ModRM(+SIB)+disp for [HW_FRAME+disp]; reg_field is ModRM.reg
    // ------------------------------------------------------------------------
    {
        hwreg_t frame = HW_FRAME;
        if (disp)
        {
            emit8(byte((2 << 6) | ((reg_field & 7) << 3) | 4));
            emit8(byte((reg_value(HW_RSP) << 3) | reg_value(frame)));
            emit32le(uint32_t(disp));
            return;
        }
        // rbp/r13 cannot use mod=00 without a displacement
        if (reg_value(frame) == 5)
        {
            emit8(byte((1 << 6) | ((reg_field & 7) << 3) | reg_value(frame)));
            emit8(0);
            return;
        }
        emit8(byte(((reg_field & 7) << 3) | reg_value(frame)));
        if (reg_value(frame) == 4)
            emit8(byte((reg_value(HW_RSP) << 3) | reg_value(frame)));
    }

    void emit_rex_frame_rm()
    // ------------------------------------------------------------------------
    //   REX.W (x86-64) and REX.B when HW_FRAME is r8–r15
    // ------------------------------------------------------------------------
    {
        hwreg_t frame = HW_FRAME;
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

    void emit_shift_cl(hwreg_t dst, byte ext)
    // ------------------------------------------------------------------------
    //   Emit d3 /ext dst, cl (shl/sar/shr by rcx)
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RCX, dst, 0xd3);
        emit8(ext | reg_value(dst));
    }

    void emit_shift_imm8(hwreg_t dst, byte ext, byte amt)
    // ------------------------------------------------------------------------
    //   Emit c1 /ext dst, imm8 (shl/sar/shr by immediate); amt==1 uses d1
    // ------------------------------------------------------------------------
    {
        byte mask = ptr64() ? 63 : 31;
        amt = byte(amt & mask);
        if (amt == 1)
        {
            orex(1, 0, dst, 0xd1);
            emit8(ext | reg_value(dst));
            return;
        }
        orex(1, 0, dst, 0xc1);
        emit8(ext | reg_value(dst));
        emit8(amt);
    }

    void emit_jcc_rel32(byte cc)
    // ------------------------------------------------------------------------
    //   Emit 0f cc rel32 and return the offset patch site
    // ------------------------------------------------------------------------
    {
        emit8(0x0f);
        emit8(cc);
        emit32le(branch_offset());
    }

    void emit_jcc_rel8(byte cc)
    // ------------------------------------------------------------------------
    //   Emit 7x rel8 (cc is the near form 0x8x; short is cc-0x10)
    // ------------------------------------------------------------------------
    {
        emit8(byte(cc - 0x10));
        emit8(branch_offset8());
    }

    void emit_near_jmp_rel32()
    // ------------------------------------------------------------------------
    //   Emit jmp rel32 (e9) and return the disp32 patch site
    // ------------------------------------------------------------------------
    {
        emit8(0xe9);
        emit32le(branch_offset());
    }

    void emit_jmp_rel8()
    // ------------------------------------------------------------------------
    //   Emit jmp rel8 (eb) and return the disp8 patch site
    // ------------------------------------------------------------------------
    {
        emit8(0xeb);
        emit8(branch_offset8());
    }

    void emit_jmp_to(hwcode_t target)
    // ------------------------------------------------------------------------
    //   Emit jmp to a known native address (rel8 when in range)
    // ------------------------------------------------------------------------
    {
        int32_t off8 = int32_t(target - cursor()) - 2;
        if (off8 >= -128 && off8 <= 127)
        {
            emit8(0xeb);
            emit8(byte(int8_t(off8)));
            return;
        }
        emit8(0xe9);
        size_t  pos = cursor();
        int32_t off = int32_t(target - pos) - 4;
        emit32le(off);
    }

    static constexpr byte SSE_ADD = 0x58;
    static constexpr byte SSE_SUB = 0x5c;
    static constexpr byte SSE_MUL = 0x59;
    static constexpr byte SSE_DIV = 0x5e;

    void emit_call_rax()
    // ------------------------------------------------------------------------
    //   Emit call rax (ff d0)
    // ------------------------------------------------------------------------
    {
        emit8(0xff);
        emit8(0xd0);
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
        orex(1, HW_RAX, 0, 0x3d);
        emit32le(imm);
    }

    void emit_alu_rax_imm32(byte op, uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit ALU rax, imm32 (op = 05 add, 2d sub, 25 and, 0d or, 35 xor)
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, 0, op);
        emit32le(imm);
    }

    void emit_imul_rax_rax_imm32(uint32_t imm)
    // ------------------------------------------------------------------------
    //   Emit imul rax, rax, imm32
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, 0, 0x69);
        emit8(modrm_rr(HW_RAX, HW_RAX));
        emit32le(imm);
    }

    void emit_store_u8_at_rax_cl()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], cl
    // ------------------------------------------------------------------------
    {
        emit8(0x88);
        emit8(0x08);
    }

    void emit_store_u16_at_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], cx
    // ------------------------------------------------------------------------
    {
        emit8(0x66);
        emit8(0x89);
        emit8(0x08);
    }

    void emit_store_u32_at_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit mov [rax], ecx
    // ------------------------------------------------------------------------
    {
        emit8(0x89);
        emit8(0x08);
    }

    void emit_load_u8_zext_rax()
    // ------------------------------------------------------------------------
    //   Emit mov al, [rax]; movzx eax, al
    // ------------------------------------------------------------------------
    {
        emit8(0x8a);
        emit8(0x00);
        emit_movzx_eax_al();
    }

    void emit_load_u16_zext_rax()
    // ------------------------------------------------------------------------
    //   Emit mov ax, [rax]; movzx eax, ax
    // ------------------------------------------------------------------------
    {
        emit8(0x66);
        emit8(0x8b);
        emit8(0x00);
        emit8(0x0f);
        emit8(0xb7);
        emit8(0xc0 | reg_value(HW_RAX));
    }

    void emit_load_u32_rax()
    // ------------------------------------------------------------------------
    //   Emit mov eax, [rax]
    // ------------------------------------------------------------------------
    {
        emit8(0x8b);
        emit8(0x00);
    }

    void emit_load_s8_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsx rax/eax, byte ptr [rax]
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x0f);
        emit8(0xbe);
        emit8(0x00);
    }

    void emit_load_s16_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsx eax/rax, word ptr [rax] (no 66 — that would target ax only)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x0f);
        emit8(0xbf);
        emit8(0x00);
    }

    void emit_load_s32_sext_rax()
    // ------------------------------------------------------------------------
    //   Emit movsxd rax, dword ptr [rax] (64-bit) or mov eax, [rax] (32-bit)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
        {
            emit8(0x48);
            emit8(0x63);
            emit8(0x00);
        }
        else
        {
            emit8(0x8b);
            emit8(0x00);
        }
    }

    void emit_sse_rex_r(hwreg_t xmm)
    // ------------------------------------------------------------------------
    //   REX prefix for SSE xmm load/store with [rsp+frame+disp] addressing
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x49 | (rex_r(xmm) ? 4 : 0));
        else if (rex_r(xmm))
            emit8(0x40 | 4);
    }

    void emit_sse_scalar_op(byte prefix, byte op, byte modrm)
    // ------------------------------------------------------------------------
    //   Emit SSE scalar op xmm0, xmm1 (prefix 0xf3=float, 0xf2=double)
    // ------------------------------------------------------------------------
    {
        emit8(prefix);
        emit8(0x0f);
        emit8(op);
        emit8(modrm);
    }

    void emit_sub_rsp_imm8(byte imm)
    // ------------------------------------------------------------------------
    //   Emit sub rsp/esp, imm8
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x83);
        emit8(0xec);
        emit8(imm);
    }

    void emit_add_rsp_imm8(byte imm)
    // ------------------------------------------------------------------------
    //   Emit add rsp/esp, imm8
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x83);
        emit8(0xc4);
        emit8(imm);
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

    void emit_jmp_rax()
    // ------------------------------------------------------------------------
    //   Emit jmp rax
    // ------------------------------------------------------------------------
    {
        emit8(0xff);
        emit8(0xe0);
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
        orex(1, 0, HW_RCX, 0xf7);
        emit8(0xf9);
    }

    void emit_div_rcx()
    // ------------------------------------------------------------------------
    //   Unsigned divide edx:eax by rcx/ecx
    // ------------------------------------------------------------------------
    {
        orex(1, 0, HW_RCX, 0xf7);
        emit8(0xf1);
    }

    void emit_imul_rax_rcx()
    // ------------------------------------------------------------------------
    //   Emit imul rax, rcx
    // ------------------------------------------------------------------------
    {
        orex(1, HW_RAX, HW_RCX, 0x0f);
        emit8(0xaf);
        emit8(modrm_rr(HW_RAX, HW_RCX));
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

    void emit_push_reg(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Push a general-purpose register (cdecl / SysV argument)
    // ------------------------------------------------------------------------
    {
        if (ptr64() && rex_b(r))
            emit8(0x41);
        emit8(byte(0x50 + reg_value(r)));
    }

    void emit_pop_reg(hwreg_t r)
    // ------------------------------------------------------------------------
    //   Pop into a general-purpose register
    // ------------------------------------------------------------------------
    {
        if (ptr64() && rex_b(r))
            emit8(0x41);
        emit8(byte(0x58 + reg_value(r)));
    }

    void emit_set_arg0_r(hwreg_t r)
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
            load_hw_imm(hw_arg0(), imm);
        else
        {
            load_hw_imm(HW_RAX, imm);
            emit_push_reg(HW_RAX);
        }
    }

    void emit_set_arg1_imm(uint64_t imm)
    // ------------------------------------------------------------------------
    //   Pass immediate as second C helper argument (cdecl: push first)
    // ------------------------------------------------------------------------
    {
        if (ptr64())
            load_hw_imm(hw_arg1(), imm);
        else
        {
            load_hw_imm(HW_RAX, imm);
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

    void emit_btx_imm8(hwreg_t r, byte ext, byte bit)
    // ------------------------------------------------------------------------
    //   Emit 0f ba /ext r, imm8 (BT/BTS/BTR/BTC); sets CF from the bit
    // ------------------------------------------------------------------------
    {
        assert (bit < (ptr64() ? 64 : 32));
        orex(1, 0, r, 0x0f);
        emit8(0xba);
        emit8(byte(0xc0 | (ext << 3) | reg_value(r)));
        emit8(bit);
    }

    void emit_btx_frame_imm8(int offs, byte ext, byte bit)
    // ------------------------------------------------------------------------
    //   Emit 0f ba /ext r, imm8 (BT/BTS/BTR/BTC); sets CF from the bit
    // ------------------------------------------------------------------------
    {
        assert (bit < (ptr64() ? 64 : 32));
        orex(1, 0, HW_FRAME, 0x0f);
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
            emit_btx_frame_imm8(-8, op_ext, p & 31);
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
    void            patch_pc_rel(size_t disp_at, size_t target);
    void            orex(int ll, int r, int x, byte c);
    void            emit_rex_modrm(int ll, int r, int x, int b, byte op);
    void            load_hw_imm(hwreg_t r, uint64_t imm);
    void            load_local_frame(byte n, hwreg_t r);
    void            store_local_frame(hwreg_t r, byte n);
    void            load_local(byte n, hwreg_t r);
    void            load_float_local(byte n, hwreg_t xmm);
    void            store_float_local(byte n, hwreg_t xmm);
    void            load_double_local(byte n, hwreg_t xmm);
    void            store_double_local(byte n, hwreg_t xmm);
    void            set_pred_cmp(byte p, byte cc);
    void            store_local(hwreg_t r, byte n);
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
    void            sext_reg(hwreg_t r, byte w);
    void            load_signext_mem(byte w);
    void            store_mem(byte d, byte s, size_t sz);
    void            load_mem(byte d, byte s, size_t sz);
    void            store_mem_off(byte d, byte s, value_t o, size_t sz);
    void            load_mem_off(byte d, byte s, value_t o, size_t sz);
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
    void            reload_rt_regs();
    void            br_via_ip();
    virtual void    do_prologue();
    virtual void    do_ret();
    void            call_ptr(void (*fn)());
    template <typename T, typename ...U>
    void            call_ptr(T (*fn)(U...)) { return call(((void (*)()) fn)); }
    void            emit_trap(cstring msg);
    void            emit_runtime_call(cstring fn);
    void            emit_runtime_call(cstring fn, tok_t a);
    void            emit_runtime_call(cstring fn, byte a);
    int             local_disp(byte n) const;
};

#endif // X86_H
