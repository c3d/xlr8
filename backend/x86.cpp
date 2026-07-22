// ****************************************************************************
//  x86.cpp                                                  XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Shared x86 family instruction helpers and opcode emitters
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

#include "x86.h"

#include "native-bridge.h"

#include <cstring>


RECORDER(x86,            256, "x86 family code generation helpers");
RECORDER(x86_ops,        256, "x86 family opcode emission");
RECORDER(x86_error,       64, "x86 family code generation errors");
RECORDER(x86_warning,     64, "x86 family code generation warnings");


x86::x86(arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an x86 family code generator
// ----------------------------------------------------------------------------
    : backend(arch)
{
    record(x86, "x86(%+s)", arch_name(arch));
}


cstring x86::machine_defs() const
// ----------------------------------------------------------------------------
//   Machine definition for 32-bit x86
// ----------------------------------------------------------------------------
{
    return "__i386__\0__i386\0";
}


uint x86::registers() const
// ----------------------------------------------------------------------------
//   Return the number of hardware registers on this platform
// ----------------------------------------------------------------------------
{
    return HW_COUNT;
}


uint x86::pointer_size() const
// ----------------------------------------------------------------------------
//   The platform is 32-bit
// ----------------------------------------------------------------------------
{
    return 4;                   // 32-bit
}


void x86::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs, prime frame/flags, load shadow locals (x86-64)
// ----------------------------------------------------------------------------
{
    record(x86, "begin_translation()");
    backend::begin_translation();
    do_prologue();
}


void x86::finish_translation()
// ----------------------------------------------------------------------------
//   Restore stack and callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(x86, "finish_translation()");
    do_ret();
    backend::finish_translation();
}



void x86::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    record(x86, "syscall(%p)", target);
    call_ptr(target);
}


size_t x86::skip_callees()
// ----------------------------------------------------------------------------
//   Emit a forward branch over callee bodies to the epilogue
// ----------------------------------------------------------------------------
{
    record(x86, "skip_callees()");
    return emit_near_jmp_rel32();
}


void x86::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (rel8 or rel32) to jump to target_native
// ----------------------------------------------------------------------------
{
    record(x86, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    section &sec = code();
    // Short forms return the disp8 site; opcode is the preceding byte.
    if (insn_pos >= 1)
    {
        byte op = sec[insn_pos - 1];
        if (op == 0xeb || (op & 0xf0) == 0x70)
        {
            int32_t off = int32_t(target_native) - int32_t(insn_pos) - 1;
            if (off < -128 || off > 127)
            {
                record(x86_error, "rel8 branch out of range off=%d", off);
                return;
            }
            sec.patch8(insn_pos, byte(int8_t(off)));
            return;
        }
    }
    int32_t off = int32_t(target_native) - int32_t(insn_pos) - 4;
    sec.patch32le(insn_pos, uint32_t(off));
}


byte x86::setcc_from_rel(cstring rel)
// ----------------------------------------------------------------------------
//   Map a comparison name to a SETcc opcode byte
// ----------------------------------------------------------------------------
{
    if (!strcmp(rel, "eq"))  return SETCC_EQ;
    if (!strcmp(rel, "ne"))  return SETCC_NE;
    if (!strcmp(rel, "lt"))  return SETCC_LT;
    if (!strcmp(rel, "le"))  return SETCC_LE;
    if (!strcmp(rel, "ltu")) return SETCC_LTU;
    if (!strcmp(rel, "leu")) return SETCC_LEU;
    return SETCC_EQ;
}


void x86::patch_pc_rel(size_t disp_at, size_t target)
// ----------------------------------------------------------------------------
//   Patch reserve_pc_in_rax so rax/eax holds target after the sequence
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        uintptr_t base = disp_at + 4;
        int32_t off = int32_t(target) - int32_t(base);
        code().patch32le(disp_at, uint32_t(off));
    }
    else
    {
        // i386: mov eax, imm32 — target is a code-buffer offset; relocate()
        // rewrites it to base+offset once the mmap address is known.
        code().patch32le(disp_at, uint32_t(target));
        relocations.push_back({ disp_at, target });
    }
}


void x86::relocate(void *base)
// ----------------------------------------------------------------------------
//   Fix i386 absolute return-address immediates after mmap
// ----------------------------------------------------------------------------
{
    record(x86, "relocate(%p)", base);
    if (ptr64() || !base)
        return;
    for (relocation &fx : relocations)
    {
        uint32_t abs = uint32_t(uintptr_t(byte_p(base) + fx.offset));
        code().patch32le(fx.imm_at, abs);
        memcpy((void *) ((byte_p) base + fx.imm_at), &abs, sizeof(abs));
    }
}


void x86::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload r12 from rax after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    record(x86, "reload_frame_reg()");
    emit_mov_frame_rax();
}


void x86::reload_flags_reg()
// ----------------------------------------------------------------------------
//   Reload flags register from runtime after xlr8_leave or popf
// ----------------------------------------------------------------------------
{
    record(x86, "reload_flags_reg()");
    emit_runtime_call("xlr8_get_flags");
    emit_mov_rr(hw_flags(), HW_EAX);
}


void x86::reload_rt_regs()
// ----------------------------------------------------------------------------
//   Reload frame and flags callee-saved registers from runtime (i386 cdecl)
// ----------------------------------------------------------------------------
{
    if (ptr64())
        return;
    record(x86, "reload_rt_regs()");
    call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
    emit_mov_frame_rax();
    call_ptr(xlr8_runtime_fn("xlr8_get_flags"));
    emit_mov_rr(hw_flags(), HW_EAX);
}


void x86::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to native address in rax
// ----------------------------------------------------------------------------
{
    record(x86, "br_via_ip()");
    emit_jmp_rax();
}


int x86::local_disp(byte n) const
// ----------------------------------------------------------------------------
//   Return stack displacement for bytecode local n (ularge_t slots)
// ----------------------------------------------------------------------------
{
    record(x86, "local_disp(%u)", n);
    return int(n) * 8;
}


void x86::orex(int ll, int r, int x, byte c)
// ----------------------------------------------------------------------------
//   Emit REX prefix and opcode byte
// ----------------------------------------------------------------------------
{
    record(x86, "orex(%lld, %lld, %lld, %u)", ll, r, x, c);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(x) ? 1 : 0));
    emit8(c);
}


void x86::emit_rex_modrm(int ll, int r, int x, int b, byte op)
// ----------------------------------------------------------------------------
//   Emit REX prefix and ModR/M encoded instruction
// ----------------------------------------------------------------------------
{
    record(x86, "emit_rex_modrm(%lld, %lld, %lld, %lld, %u)", ll, r, x, b, op);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8 || b >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_x(x) ? 2 : 0) | (rex_b(b) ? 1 : 0));
    emit8(op);
    emit8(byte((reg_value(r) << 3) | reg_value(b)));
    if (reg_value(b) == 4)
        emit8(byte((reg_value(x) << 3) | reg_value(b)));
    if (reg_value(b) == 5 && x == 4)
        emit32le(0);
}


void x86::load_hw_imm(hwreg_t r, uint64_t imm)
// ----------------------------------------------------------------------------
//   Load immediate into a hardware register (prefer short encodings)
// ----------------------------------------------------------------------------
{
    record(x86, "load_hw_imm(%u, %lld)", r, imm);

    // i386 only ever materializes the low 32 bits
    if (pointer_size() == 4)
        imm = uint32_t(imm);

    // xor r32, r32 — 2–3 bytes; clears the full 64-bit register on x86-64
    if (imm == 0)
    {
        emit_op_rr(0x31, r, r, /*ll=*/0);
        return;
    }

    // mov r32, imm32 — 5–6 bytes; zero-extends into r64 on x86-64
    if (imm == uint32_t(imm))
    {
        if (rex_b(r))
            emit8(0x41);
        emit8(0xb8 + reg_value(r));
        emit32le(uint32_t(imm));
        return;
    }

    // mov r64, imm32 — 7 bytes; sign-extends (e.g. -1)
    if (int64_t(imm) == int32_t(imm))
    {
        orex(1, 0, r, 0xc7);
        emit8(byte(0xc0 | reg_value(r)));
        emit32le(uint32_t(imm));
        return;
    }

    // movabs r64, imm64 — 10 bytes
    emit8(0x48 | (rex_b(r) ? 1 : 0));
    emit8(0xb8 + reg_value(r));
    emit32le(uint32_t(imm));
    emit32le(uint32_t(imm >> 32));
}


void x86::load_float_local(byte n, uint xmm)
// ----------------------------------------------------------------------------
//   Load a bytecode float local into an xmm register
// ----------------------------------------------------------------------------
{
    record(x86, "load_float_local(%u, %u)", n, xmm);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    emit8(0xf3);
    emit_sse_rex_r(xmm);
    emit8(0x0f);
    emit8(0x10);
    emit8(byte((disp ? 2 : 0) << 6 | (reg_value(xmm) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    if (disp)
        emit32le(uint32_t(disp));
}


void x86::store_float_local(byte n, uint xmm)
// ----------------------------------------------------------------------------
//   Store an xmm register into a bytecode float local
// ----------------------------------------------------------------------------
{
    record(x86, "store_float_local(%u, %u)", n, xmm);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    emit8(0xf3);
    emit_sse_rex_r(xmm);
    emit8(0x0f);
    emit8(0x11);
    emit8(byte((disp ? 2 : 0) << 6 | (reg_value(xmm) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    if (disp)
        emit32le(uint32_t(disp));
}


void x86::load_double_local(byte n, uint xmm)
// ----------------------------------------------------------------------------
//   Load a bytecode double local into an xmm register
// ----------------------------------------------------------------------------
{
    record(x86, "load_double_local(%u, %u)", n, xmm);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    emit8(0xf2);
    emit_sse_rex_r(xmm);
    emit8(0x0f);
    emit8(0x10);
    emit8(byte((disp ? 2 : 0) << 6 | (reg_value(xmm) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    if (disp)
        emit32le(uint32_t(disp));
}


void x86::store_double_local(byte n, uint xmm)
// ----------------------------------------------------------------------------
//   Store an xmm register into a bytecode double local
// ----------------------------------------------------------------------------
{
    record(x86, "store_double_local(%u, %u)", n, xmm);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    emit8(0xf2);
    emit_sse_rex_r(xmm);
    emit8(0x0f);
    emit8(0x11);
    emit8(byte((disp ? 2 : 0) << 6 | (reg_value(xmm) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    if (disp)
        emit32le(uint32_t(disp));
}


void x86::load_local_frame(byte n, hwreg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register r
// ----------------------------------------------------------------------------
{
    record(x86, "load_local_frame(%u, %u)", n, r);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    if (disp)
    {
        int w = (ptr64()) ? 8 : 0;
        if (w || r >= 8 || frame >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(frame) ? 1 : 0));
        emit8(0x8b);
        emit8(byte((2 << 6) | (reg_value(r) << 3) | 4));
        emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
        emit32le(uint32_t(disp));
    }
    else
    {
        emit_rex_modrm(1, r, HW_ESP, frame, 0x8b);
    }
}


void x86::store_local_frame(hwreg_t r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    record(x86, "store_local_frame(%u, %u)", r, n);
    int disp = local_disp(n);
    uint frame = HW_FRAME;
    if (disp)
    {
        int w = (ptr64()) ? 8 : 0;
        if (w || r >= 8 || frame >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(frame) ? 1 : 0));
        emit8(0x89);
        emit8(byte((2 << 6) | (reg_value(r) << 3) | 4));
        emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
        emit32le(uint32_t(disp));
    }
    else
    {
        emit_rex_modrm(1, r, HW_ESP, frame, 0x89);
    }
}


void x86::load_local(byte n, hwreg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    record(x86, "load_local(%u, %u)", n, r);
    if (has_shadow(n))
    {
        uint s = shadow_reg(n);
        if (r != s)
            emit_mov_rr(r, s);
        return;
    }
    load_local_frame(n, r);
}


void x86::store_local(hwreg_t r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_local(%u, %u)", r, n);
    if (has_shadow(n))
    {
        uint s = shadow_reg(n);
        if (r != s)
            emit_mov_rr(s, r);
    }
    store_local_frame(has_shadow(n) ? shadow_reg(n) : r, n);
}


void x86::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "mov_local(%u, %u)", d, s);
    if (has_shadow(d) && has_shadow(s))
    {
        emit_mov_rr(shadow_reg(d), shadow_reg(s));
        store_local_frame(shadow_reg(d), d);
        return;
    }
    load_local(s, HW_EAX);
    store_local(HW_EAX, d);
}


void x86::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "swap_locals(%u, %u)", d, s);
    load_local(d, HW_EAX);
    load_local(s, HW_ECX);
    store_local(HW_ECX, d);
    store_local(HW_EAX, s);
}


void x86::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_imm(%u, %lld)", d, v);
    load_hw_imm(HW_EAX, uint64_t(v));
    store_local(HW_EAX, d);
}


void x86::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_float(%u, %g)", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    load_hw_imm(HW_EAX, bits);
    store_local(HW_EAX, d);
}


void x86::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_double(%u, %g)", d, v);
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    if (ptr64())
    {
        load_hw_imm(HW_EAX, bits);
        store_local(HW_EAX, d);
        return;
    }
    // i386: load_hw_imm truncates to 32 bits — write both halves of the slot.
    int disp = local_disp(d);
    uint frame = HW_FRAME;
    load_hw_imm(HW_EAX, uint32_t(bits));
    emit8(0x89);
    emit8(byte((2 << 6) | (reg_value(HW_EAX) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    emit32le(uint32_t(disp));
    load_hw_imm(HW_EAX, uint32_t(bits >> 32));
    emit8(0x89);
    emit8(byte((2 << 6) | (reg_value(HW_EAX) << 3) | 4));
    emit8(byte((reg_value(HW_ESP) << 3) | reg_value(frame)));
    emit32le(uint32_t(disp + 4));
}


void x86::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "binop_local(%u, %u, %s)", d, s, op);
    bool stored = false;
    load_local(d, HW_EAX);
    load_local(s, HW_ECX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_add_rr(HW_EAX, HW_ECX);
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_sub_rr(HW_EAX, HW_ECX);
}

else if (!strcmp(op, "imul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_imul_rax_rcx();
}

else if (!strcmp(op, "and"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_and_rr(HW_EAX, HW_ECX);
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_or_rr(HW_EAX, HW_ECX);
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_xor_rr(HW_EAX, HW_ECX);
}

else if (!strcmp(op, "shl"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_cl(HW_EAX, 0xe0);
}

else if (!strcmp(op, "sar"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_cl(HW_EAX, 0xf8);
}

else if (!strcmp(op, "shr"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_cl(HW_EAX, 0xe8);
}

else if (!strcmp(op, "idiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_cdq();
    emit_idiv_rcx();
}

else if (!strcmp(op, "udiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_xor_rdx_rdx();
    emit_div_rcx();
}

else if (!strcmp(op, "div"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_xor_rdx_rdx();
    emit_div_rcx();
}

else if (!strcmp(op, "idiv_mod"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_cdq();
    emit_idiv_rcx();
    emit_mov_rr(HW_EAX, HW_EDX);
}

else if (!strcmp(op, "div_mod"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_xor_rdx_rdx();
    emit_div_rcx();
    emit_mov_rr(HW_EAX, HW_EDX);
}

else if (!strcmp(op, "fadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_float_local(d, 0);
    load_float_local(s, 1);
    emit_sse_scalar_op(0xf3, SSE_ADD, 0xc1);
    store_float_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "fsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_float_local(d, 0);
    load_float_local(s, 1);
    emit_sse_scalar_op(0xf3, SSE_SUB, 0xc1);
    store_float_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "fmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_float_local(d, 0);
    load_float_local(s, 1);
    emit_sse_scalar_op(0xf3, SSE_MUL, 0xc1);
    store_float_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "fdiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_float_local(d, 0);
    load_float_local(s, 1);
    emit_sse_scalar_op(0xf3, SSE_DIV, 0xc1);
    store_float_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "dadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_double_local(d, 0);
    load_double_local(s, 1);
    emit_sse_scalar_op(0xf2, SSE_ADD, 0xc1);
    store_double_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "dsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_double_local(d, 0);
    load_double_local(s, 1);
    emit_sse_scalar_op(0xf2, SSE_SUB, 0xc1);
    store_double_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "dmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_double_local(d, 0);
    load_double_local(s, 1);
    emit_sse_scalar_op(0xf2, SSE_MUL, 0xc1);
    store_double_local(d, 0);
    stored = true;
}

else if (!strcmp(op, "ddiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_double_local(d, 0);
    load_double_local(s, 1);
    emit_sse_scalar_op(0xf2, SSE_DIV, 0xc1);
    store_double_local(d, 0);
    stored = true;
}

    if (!stored)
        store_local(HW_EAX, d);
}


void x86::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    record(x86, "binop_imm(%u, %lld, %s)", d, v, op);

    // Memory-destination add/sub for non-shadow locals (no load/store pair)
    if (!has_shadow(d) && int64_t(v) == int32_t(v))
    {
        if (!strcmp(op, "add"))
        {
            emit_alu_imm_frame(d, /*ADD*/ 0, int32_t(v));
            return;
        }
        if (!strcmp(op, "sub"))
        {
            emit_alu_imm_frame(d, /*SUB*/ 5, int32_t(v));
            return;
        }
    }

    load_local(d, HW_EAX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_alu_rax_imm32(0x05, uint32_t(v));
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_alu_rax_imm32(0x2d, uint32_t(v));
}

else if (!strcmp(op, "imul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_imul_rax_rax_imm32(uint32_t(v));
}

else if (!strcmp(op, "and"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_alu_rax_imm32(0x25, uint32_t(v));
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_alu_rax_imm32(0x0d, uint32_t(v));
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_alu_rax_imm32(0x35, uint32_t(v));
}

else if (!strcmp(op, "shl_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_imm8(HW_EAX, 0xe0, byte(uint64_t(v)));
}

else if (!strcmp(op, "sar_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_imm8(HW_EAX, 0xf8, byte(uint64_t(v)));
}

else if (!strcmp(op, "shr_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_shift_imm8(HW_EAX, 0xe8, byte(uint64_t(v)));
}

else if (!strcmp(op, "idiv_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_ECX, uint64_t(v));
    emit_cdq();
    emit_idiv_rcx();
}

else if (!strcmp(op, "div_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_ECX, uint64_t(v));
    emit_xor_rdx_rdx();
    emit_div_rcx();
}

else if (!strcmp(op, "idiv_mod_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_ECX, uint64_t(v));
    emit_cdq();
    emit_idiv_rcx();
    emit_mov_rr(HW_EAX, HW_EDX);
}

else if (!strcmp(op, "div_mod_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_ECX, uint64_t(v));
    emit_xor_rdx_rdx();
    emit_div_rcx();
    emit_mov_rr(HW_EAX, HW_EDX);
}

    store_local(HW_EAX, d);
}


void x86::set_pred_cmp(byte p, byte cc)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to x86 SETcc condition
// ----------------------------------------------------------------------------
{
    record(x86, "set_pred_cmp(%u, %u)", p, cc);
    emit_setcc_al(cc);
    emit_movzx_eax_al();
    emit_mov_rr(hw_scr(), HW_EAX);
    emit_pred_clear_bit(p);
    emit_test_rr(hw_scr(), hw_scr());
    size_t pos = emit_jcc_rel8(JCC_JZ);
    emit_pred_set_bit(p);
    patch_branch_site(pos, cursor());
}


void x86::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(x86, "set_pred(%u, %d)", p, on);
    if (on)
        emit_pred_set_bit(p);
    else
        emit_pred_clear_bit(p);
}


void x86::cmp_local(byte p, byte d, byte s, cstring rel)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_local(%u, %u, %u, %s)", p, d, s, rel);
    load_local(d, HW_EAX);
    load_local(s, HW_ECX);
    emit_cmp_rr(HW_EAX, HW_ECX);
    set_pred_cmp(p, setcc_from_rel(rel));
}


void x86::cmp_imm(byte p, byte d, value_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_imm(%u, %u, %lld, %s)", p, d, v, rel);
    load_local(d, HW_EAX);
    emit_cmp_rax_imm32(uint32_t(v));
    set_pred_cmp(p, setcc_from_rel(rel));
}


void x86::cmp_uimm(byte p, byte d, uvalue_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, rel);
    cmp_imm(p, d, value_t(v), rel);
}


void x86::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(x86, "cond_mov_local(%u, %u, %u)", p, d, s);
    emit_pred_test_bit(p);              // BT -> CF
    size_t pos = emit_jcc_rel8(JCC_JNC); // skip if bit clear
    mov_local(d, s);
    patch_branch_site(pos, cursor());
}


void x86::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "select_local(%u, %u, %u, %u)", p, d, s, x);
    emit_pred_test_bit(p);                 // BT -> CF
    size_t jnc_pos = emit_jcc_rel8(JCC_JNC); // bit clear -> pick x
    mov_local(d, s);
    size_t jmp_pos = emit_jmp_rel8();
    patch_branch_site(jnc_pos, cursor());
    mov_local(d, x);
    patch_branch_site(jmp_pos, cursor());
}


void x86::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "neg_local(%u)", d);
    load_local(d, HW_EAX);
    emit_grp3_f7(HW_EAX, 0xd8);
    store_local(HW_EAX, d);
}


void x86::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "not_local(%u)", d);
    load_local(d, HW_EAX);
    emit_grp3_f7(HW_EAX, 0xd0);
    store_local(HW_EAX, d);
}


void x86::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "incr_local(%u)", d);
    if (has_shadow(d))
    {
        uint s = shadow_reg(d);
        emit_grp5_ff(s, 0xc0);
        store_local_frame(s, d);
        return;
    }
    emit_grp5_frame(d, /*INC*/ 0);
}


void x86::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "decr_local(%u)", d);
    if (has_shadow(d))
    {
        uint s = shadow_reg(d);
        emit_grp5_ff(s, 0xc8);
        store_local_frame(s, d);
        return;
    }
    emit_grp5_frame(d, /*DEC*/ 1);
}


void x86::sext_reg(hwreg_t r, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register r to w bits (shared by sext and load paths)
// ----------------------------------------------------------------------------
{
    record(x86, "sext_reg(%u, %u)", r, w);
    if (w >= 64)
        return;
    if (w == 8)
    {
        if (ptr64())
            emit8(0x48);
        emit8(0x0f);
        emit8(0xbe);
        emit8(byte(0xc0 | (r & 7)));
    }
    else if (w == 16)
    {
        emit8(0x66);
        if (ptr64())
            emit8(0x48);
        emit8(0x0f);
        emit8(0xbf);
        emit8(byte(0xc0 | (r & 7)));
    }
    else if (w == 32 && ptr64())
    {
        emit8(0x48);
        emit8(0x63);
        emit8(byte(0xc0 | ((r & 7) << 3) | (r & 7)));
    }
    else
    {
        emit_shift_imm8(r, 0xe0, byte(64 - w));
        emit_shift_imm8(r, 0xf8, byte(64 - w));
    }
}


void x86::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "sext_local(%u, %u)", d, w);
    load_local(d, HW_EAX);
    sext_reg(HW_EAX, w);
    store_local(HW_EAX, d);
}


void x86::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "zext_local(%u, %u)", d, w);
    load_local(d, HW_EAX);
    if (w < 64)
    {
        load_hw_imm(HW_ECX, (1ULL << w) - 1);
        emit_and_rr(HW_EAX, HW_ECX);
    }
    store_local(HW_EAX, d);
}


void x86::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem(%u, %u, %u)", d, s, sz);
    load_local(d, HW_EAX);
    load_local(s, HW_ECX);
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u8_at_rax_cl();
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u16_at_rax_rcx();
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u32_at_rax_rcx();
}

    else { emit_rex_modrm(1, HW_ECX, HW_EAX, HW_EAX, 0x89); }
}


void x86::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_EAX);
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u8_zext_rax();
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u16_zext_rax();
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u32_rax();
}

    else { emit_rex_modrm(1, HW_EAX, HW_EAX, HW_EAX, 0x8b); }
    store_local(HW_EAX, d);
}


void x86::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d plus offset
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(d, HW_EAX);
    if (o)
    {
        load_hw_imm(HW_EDX, uint64_t(o));
        emit_add_rr(HW_EAX, HW_EDX);
    }
    load_local(s, HW_ECX);
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u8_at_rax_cl();
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u16_at_rax_rcx();
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_store_u32_at_rax_rcx();
}

    else { emit_rex_modrm(1, HW_ECX, HW_EAX, HW_EAX, 0x89); }
}


void x86::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(s, HW_EAX);
    if (o)
    {
        load_hw_imm(HW_ECX, uint64_t(o));
        emit_add_rr(HW_EAX, HW_ECX);
    }
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u8_zext_rax();
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u16_zext_rax();
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    emit_load_u32_rax();
}

    else { emit_rex_modrm(1, HW_EAX, HW_EAX, HW_EAX, 0x8b); }
    store_local(HW_EAX, d);
}


void x86::load_signext_mem(byte w)
// ----------------------------------------------------------------------------
//   Load from [rax] and sign-extend to w bits into rax
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext_mem(%u)", w);
    if (w == 8)
        emit_load_s8_sext_rax();
    else if (w == 16)
        emit_load_s16_sext_rax();
    else if (w == 32)
        emit_load_s32_sext_rax();
    else if (w >= 64)
        emit_rex_modrm(1, HW_EAX, HW_EAX, HW_EAX, 0x8b);
    else
    {
        uint sz = (w + 7) / 8;
        if (sz == 1)
            emit_load_u8_zext_rax();
        else if (sz == 2)
            emit_load_u16_zext_rax();
        else if (sz == 4)
            emit_load_u32_rax();
        else
            emit_rex_modrm(1, HW_EAX, HW_EAX, HW_EAX, 0x8b);
        sext_reg(HW_EAX, w);
    }
}


void x86::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext(%u, %u, %u)", d, s, w);
    load_local(s, HW_EAX);
    load_signext_mem(w);
    store_local(HW_EAX, d);
}

void x86::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_local(s, HW_EAX);
    if (o)
    {
        load_hw_imm(HW_ECX, uint64_t(o));
        emit_add_rr(HW_EAX, HW_ECX);
    }
    load_signext_mem(w);
    store_local(HW_EAX, d);
}


void x86::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address of local slot s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "frame_addr(%u, %u)", d, s);
    load_hw_imm(HW_EAX, uint64_t(local_disp(s)));
    emit_add_rr(HW_EAX, HW_FRAME);
    store_local(HW_EAX, d);
}


void x86::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(x86, "bra_offset(%lld)", b);
    uint pos = jump(0);
    record_branch(b, pos);
}


void x86::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(x86, "bra_pred(%u, %lld, %d)", p, b, sense);
    emit_pred_test_bit(p);                              // BT -> CF
    size_t pos = emit_jcc_rel32(sense ? JCC_JC : JCC_JNC);
    record_branch(b, uint(pos));
}


void x86::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_EAX);
    emit_test_rr(HW_EAX, HW_EAX);
    size_t pos = emit_jcc_rel32(eq ? JCC_JZ : JCC_JNZ);
    record_branch(b, uint(pos));
}


void x86::bra_signed(byte s, broff_t b, byte cc)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(x86, "bra_signed(%u, %lld, %u)", s, b, cc);
    load_local(s, HW_EAX);
    emit_test_rr(HW_EAX, HW_EAX);
    size_t pos = emit_jcc_rel32(cc);
    record_branch(b, uint(pos));
}


void x86::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "dbra_local(%u, %lld)", d, b);
    load_local(d, HW_EAX);
    emit_grp5_ff(HW_EAX, 0xc8);
    store_local(HW_EAX, d);
    bra_zero(d, b, false);
}


void x86::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(x86, "jmp_tok(%llu)", t);
    auto it = labels.find(t);
    if (it != labels.end())
    {
        emit_jmp_to(it->second);
        return;
    }
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = jump(head);
    record_tok_jump(t, pos);
}


void x86::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(x86, "call_tok(%llu)", t);
    size_t adr_at = reserve_pc_in_rax();
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_push_value");
    emit_cleanup_call_args(1);
    emit_runtime_call("xlr8_sync_args");
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = jump(head);
    record_tok_jump(t, pos);
    patch_pc_rel(adr_at, uintptr_t(pos + 4));
}


void x86::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(x86, "ijmp_local(%u)", s);
    load_local(s, HW_EAX);
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_jit_addr");
    emit_cleanup_call_args(1);
    br_via_ip();
}


void x86::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(x86, "icall_local(%u)", s);
    size_t adr_at = reserve_pc_in_rax();
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_push_value");
    emit_cleanup_call_args(1);
    emit_runtime_call("xlr8_sync_args");
    load_local(s, HW_EAX);
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_jit_addr");
    emit_cleanup_call_args(1);
    br_via_ip();
    patch_pc_rel(adr_at, cursor());
}


void x86::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_pc()");
    size_t adr_at = reserve_pc_in_rax();
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_push_value");
    emit_cleanup_call_args(1);
    patch_pc_rel(adr_at, adr_at - 1);
}


void x86::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_pc(%u)", d);
    load_hw_imm(HW_EAX, cursor());
    store_local(HW_EAX, d);
}


void x86::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (interpreter semantics)
// ----------------------------------------------------------------------------
{
    record(x86, "load_pca(%u, %lld)", d, b);
    (void) b;
    load_hw_imm(HW_EAX, uint64_t(pending_branch_target));
    store_local(HW_EAX, d);
}


void x86::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(x86, "get_arg(%u, %u)", d, s);
    emit_set_arg1_imm(uint64_t(s));
    emit_set_arg0_imm(uint64_t(d));
    emit_runtime_call("xlr8_get_arg");
    emit_cleanup_call_args(2);
}


void x86::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local s into argument slot d
// ----------------------------------------------------------------------------
{
    record(x86, "set_arg(%u, %u)", d, s);
    emit_set_arg1_imm(uint64_t(s));
    emit_set_arg0_imm(uint64_t(d));
    emit_runtime_call("xlr8_set_arg");
    emit_cleanup_call_args(2);
}


void x86::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_local(%u)", s);
    load_local(s, HW_EAX);
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_push_value");
    emit_cleanup_call_args(1);
}


void x86::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(x86, "pop_local(%u)", d);
    emit_runtime_call("xlr8_pop_value");
    store_local(HW_EAX, d);
}


void x86::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_flags()");
    emit_set_arg0_r(hw_flags());
    emit_runtime_call("xlr8_push_flags_val");
    emit_cleanup_call_args(1);
}


void x86::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(x86, "pop_flags()");
    emit_runtime_call("xlr8_pop_flags_val");
    reload_flags_reg();
}


void x86::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(x86, "store_global(%u, %u)", d, s);
    emit_set_arg1_imm(uint64_t(s));
    emit_set_arg0_imm(uint64_t(d));
    emit_runtime_call("xlr8_store_global");
    emit_cleanup_call_args(2);
}


void x86::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_global(%u, %u)", d, s);
    emit_set_arg1_imm(uint64_t(s));
    emit_set_arg0_imm(uint64_t(d));
    emit_runtime_call("xlr8_load_global");
    emit_cleanup_call_args(2);
}


void x86::do_prologue()
// ----------------------------------------------------------------------------
//   Emit standard function prologues
// ----------------------------------------------------------------------------
{
    record(x86, "do_prologue");

    // i386 SysV: ebx/esi/edi/ebp are callee-saved.  We keep frame in edi
    // and flags in esi for the whole translation (see hw_frame/hw_flags).
    emit_push_reg(HW_EBX);
    emit_push_reg(HW_ESI);
    emit_push_reg(HW_EDI);
    emit_push_reg(HW_EBP);
}


void x86::do_ret()
// ----------------------------------------------------------------------------
//   Emit epilogue (restore callee-saved) and return
// ----------------------------------------------------------------------------
{
    record(x86, "do_ret()");
    emit_pop_reg(HW_EBP);
    emit_pop_reg(HW_EDI);
    emit_pop_reg(HW_ESI);
    emit_pop_reg(HW_EBX);
    emit_insn_ret();
}


void x86::call_ptr(void *fn)
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in RAX
// ----------------------------------------------------------------------------
{
    record(x86, "call_ptr(%p)", fn);
#ifdef _WIN64
    // Microsoft x64: caller provides 32 bytes of register parameter home space
    if (ptr64())
        emit_sub_rsp_imm8(0x20);
#endif
    load_hw_imm(HW_EAX, uint64_t(fn));
    emit_call_rax();
#ifdef _WIN64
    if (ptr64())
        emit_add_rsp_imm8(0x20);
#endif
}


void x86::emit_trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(x86_warning, "emit_trap(%s)", msg);
    (void) msg;
    emit_int3();
}


void x86::emit_runtime_call(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s)", fn);
    call_ptr(xlr8_runtime_fn(fn));
    // xlr8_leave/enter return the new frame in rax — keep it for the reload.
    if (!strcmp(fn, "xlr8_leave") || !strcmp(fn, "xlr8_enter"))
        reload_frame_reg();
    else if (pointer_size() == 4)
    {
        // Preserve helper return in eax.  Refresh frame only — flags live in
        // esi until emit_sys syncs them; get_flags would wipe BTS/BTR updates.
        emit_push_reg(HW_EAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_EAX);
    }
}


void x86::emit_runtime_call(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s, %llu)", fn, a);
    emit_set_arg0_imm(uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
    emit_cleanup_call_args(1);
    if (pointer_size() == 4)
    {
        emit_push_reg(HW_EAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_EAX);
    }
}


void x86::emit_runtime_call(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s, %u)", fn, a);
    emit_set_arg0_imm(uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
    emit_cleanup_call_args(1);
    if (!strcmp(fn, "xlr8_enter") || !strcmp(fn, "xlr8_leave"))
        reload_frame_reg();
    else if (pointer_size() == 4)
    {
        emit_push_reg(HW_EAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_EAX);
    }
}


#define EMIT(op)        void x86::emit_##op(emit_args &e)

#include "x86-emit.cpp"
