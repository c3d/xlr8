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


x86::x86(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an x86 family code generator
// ----------------------------------------------------------------------------
    : backend(rt, arch)
{
    record(x86, "New backend arch=%+s", arch_name(arch));
}



// ============================================================================
//
//   Backend standard interfacd
//
// ============================================================================

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
    record(x86, "begin_translation");
    backend::begin_translation();
    prologue();
}


void x86::finish_translation()
// ----------------------------------------------------------------------------
//   Restore stack and callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(x86, "finish_translation");
    epilogue();
    backend::finish_translation();
}


void x86::patch_branch(hwcode_t data, hwcode_t target)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (rel8 or rel32) to jump to target_native
// ----------------------------------------------------------------------------
{
    record(x86, "patch_branch broff=%zu target=%zu", data, target);

    section &sec = code();
    // Short forms return the disp8 site; opcode is the preceding byte.
    if (data >= 1)
    {
        byte op = sec[data - 1];
        if (op == 0xeb || (op & 0xf0) == 0x70)
        {
            int32_t off = int32_t(target) - int32_t(data) - 1;
            if (off < -128 || off > 127)
            {
                record(x86_error, "rel8 branch out of range off=%d", off);
                return;
            }
            sec.patch8(data, byte(int8_t(off)));
            return;
        }
    }
    int32_t off = int32_t(target) - int32_t(data) - 4;
    sec.patch32le(data, uint32_t(off));
}


void x86::call(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    record(x86, "call %p", target);
    call_ptr(target);
}


void x86::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    size_t spilled = spill_all();
    record(x86, "syscall %p spilled %zu", target, spilled);
    call_ptr(target);
}


uint x86::relocate(void *base)
// ----------------------------------------------------------------------------
//   Relocate absolute addresses after mapping the code, return num of relocs
// ----------------------------------------------------------------------------
{
    size_t count = relocations.size();
    record(x86, "relocate hase=%p count=%zu", base, count);

    section &s = code();
    for (hwcode_t patch : relocations)
    {
        uint32_t  offset = s.value32le(patch);
        uintptr_t abs    = uintptr_t(byte_p(base) + offset);
        uint32_t  abs32  = uint32_t(abs);
        if (abs != abs32)
            record(x86_warning, "Relocating %p truncated to %u", abs, abs32);
        memcpy((void *) (byte_p(base) + patch), &abs32, sizeof(abs32));
    }
    relocations.clear();
    return count;
}


bool x86::move(hwreg_t to, hwreg_t from)
// ----------------------------------------------------------------------------
//   Move between registers
// ----------------------------------------------------------------------------
{
    emit_mov_rr(hw(to), hw(from));
    return true;
}


bool x86::load(hwreg_t h, local_t n)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register r
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw(h);
    record(x86, "load %u=[%u]", r, n);
    int disp = local_disp(n);

    if (r >= HW_XMM0D)
    {
        emit8(0xf2 | (r >= HW_XMM0F));
        emit_sse_rex_r(r);
        emit8(0x0f);
        emit8(0x10);
    }
    else
    {
        uint w = ptr64() ? 8 : 0;
        if (w || r >= 8 || HW_FRAME >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        emit(0x8B);
    }
    emit_modrm_frame(r, disp);
    return true;
}


bool x86::store(local_t n, hwreg_t h)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw(h);
    record(x86, "store [%u]=%u", n, r);
    int disp = local_disp(n);

    if (r >= HW_XMM0D)
    {
        emit8(0xf2 | (r >= HW_XMM0F));
        emit_sse_rex_r(r);
        emit8(0x0f);
        emit8(0x11);
    }
    else
    {
        uint w = ptr64() ? 8 : 0;
        if (w || r >= 8 || HW_FRAME >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        emit(0x89);
    }
    emit_modrm_frame(r, disp);
    return saved(loc, h);
}


x86::reginfo x86::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for x86
// ----------------------------------------------------------------------------
{
    static const reginfo i386_registers[x86::HW_COUNT] =
    {
        {    "rax",                      0 },
        {    "rcx",                      0 },
        {    "rdx",                      0 },
        {    "rbx",              RSPEC_INT },
        {    "rsp",                      0 },
        {    "rbp",                      0 },
        {    "rsi", RSPEC_CALL | RSPEC_INT },
        {    "rdi", RSPEC_CALL | RSPEC_INT },
        {     "r8",                      0 },
        {     "r9",                      0 },
        {    "r10",                      0 },
        {    "r11",                      0 },
        {    "r12",                      0 },
        {    "r13",                      0 },
        {    "r14",                      0 },
        {    "r15",                      0 },

        {  "xmm0d",              RSPEC_DBL },
        {  "xmm1d",              RSPEC_DBL },
        {  "xmm2d",              RSPEC_DBL },
        {  "xmm3d",              RSPEC_DBL },
        {  "xmm4d",              RSPEC_DBL },
        {  "xmm5d",              RSPEC_DBL },
        {  "xmm6d",              RSPEC_DBL },
        {  "xmm7d",              RSPEC_DBL },
        {  "xmm8d",                      0 },
        {  "xmm9d",                      0 },
        { "xmm10d",                      0 },
        { "xmm11d",                      0 },
        { "xmm12d",                      0 },
        { "xmm13d",                      0 },
        { "xmm14d",                      0 },
        { "xmm15d",                      0 },

        {  "xmm0f",              RSPEC_FLT },
        {  "xmm1f",              RSPEC_FLT },
        {  "xmm2f",              RSPEC_FLT },
        {  "xmm3f",              RSPEC_FLT },
        {  "xmm4f",              RSPEC_FLT },
        {  "xmm5f",              RSPEC_FLT },
        {  "xmm6f",              RSPEC_FLT },
        {  "xmm7f",              RSPEC_FLT },
        {  "xmm8f",                      0 },
        {  "xmm9f",                      0 },
        { "xmm10f",                      0 },
        { "xmm11f",                      0 },
        { "xmm12f",                      0 },
        { "xmm13f",                      0 },
        { "xmm14f",                      0 },
        { "xmm15f",                      0 }
    };

    return reg < HW_COUNT ? i386_registers[reg] : reginfo{"INVALID", 0};
}



// ============================================================================
//
//   Internal helpers
//
// ============================================================================

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
    record(x86_error, "Invalid relation %+s", rel);
    return SETCC_EQ;
}


void x86::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to native address in rax
// ----------------------------------------------------------------------------
{
    record(x86, "br_via_ip");
    emit_jmp_rax();
}


int x86::local_disp(int n) const
// ----------------------------------------------------------------------------
//   Return stack displacement for bytecode local n (ularge_t slots)
// ----------------------------------------------------------------------------
{
    return n * sizeof(ularge_t);
}


void x86::orex(int ll, x86reg_t r, x86reg_t x, byte c)
// ----------------------------------------------------------------------------
//   Emit REX prefix and opcode byte
// ----------------------------------------------------------------------------
{
    record(x86, "orex l=%d r=%u x=%u c=%u", ll, r, x, c);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(x) ? 1 : 0));
    emit8(c);
}


void x86::emit_rex_modrm(int ll, x86reg_t r, x86reg_t x, x86reg_t b, byte op)
// ----------------------------------------------------------------------------
//   Emit REX prefix and ModR/M encoded instruction
// ----------------------------------------------------------------------------
{
    record(x86, "emit_rex_modrm ll=%d r=%u x=%u b=%u op=%u)", ll, r, x, b, op);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8 || b >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0)
              | (rex_x(x) ? 2 : 0)
              | (rex_b(b) ? 1 : 0));
    emit8(op);
    emit8(byte((reg_value(r) << 3) | reg_value(b)));
    if (reg_value(b) == 4)
        emit8(byte((reg_value(x) << 3) | reg_value(b)));
    if (reg_value(b) == 5 && x == 4)
        emit32le(0);
}


void x86::load_hw_imm(x86reg_t r, uint64_t imm)
// ----------------------------------------------------------------------------
//   Load immediate into a hardware register (prefer short encodings)
// ----------------------------------------------------------------------------
{
    record(x86, "load_hw_imm %u=%llu %lld 0x%llX", r, imm, imm, imm);

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
    force32:
        orex(1, HW_NONE, r, 0xc7);
        emit8(byte(0xc0 | reg_value(r)));
        emit32le(uint32_t(imm));
        return;
    }

    // i386 only ever materializes the low 32 bits
    if (!ptr64())
    {
        record(x86_error, "Materializing 0x16llX in 32-bit mode", imm);
        imm = uint32_t(imm);
        goto force32;
    }

    // movabs r64, imm64 — 10 bytes
    emit8(0x48 | (rex_b(r) ? 1 : 0));
    emit8(0xb8 + reg_value(r));
    emit32le(uint32_t(imm));
    emit32le(uint32_t(imm >> 32));
}


x86::x864reg_t x86::load_fp(local_t loc, byte sz)
// ----------------------------------------------------------------------------
//   Load a float / double local into an xmm register
// ----------------------------------------------------------------------------
{
    record(x86, "load_%+s [%u]", sz == 64 ? "dbl" : "flt", n);
    if (sz != 32 && sz != 64)
    {
        record(x86_error, "Invalid size %u in load_fp", sz);
        sz = 64;
    }

    hwreg_t r = local(loc, sz == 32 ? RSPEC_FLT : RSPEC_DBL);
    record(x86, "load_%+s %u=[%u]", sz == 64 ? "dbl" : "flt", r, n);
    return r;
}


void x86::store_fp(local_t loc, x86reg_t r)
// ----------------------------------------------------------------------------
//   Store an xmm register into a bytecode float local
// ----------------------------------------------------------------------------
{
    record(x86, "store_%+s [%u]=%u", r >= HW_XMM0F ? "flt" : "dbl", loc, r);
    store(loc, r);
}


void x86::load_local(local_t loc, x86reg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
//   Warning: conventional order is reverse between backend and x86
{
    record(x86, "load_local [%u]=%u", loc, r);
    load(r, loc);
}


void x86::store_local(x86reg_t r, local_t loc)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
//   Warning: conventional order is reverse between backend and x86
{
    record(x86, "store_local %u=[%u]", r, loc);
    store(loc, r);
}


void x86::mov_local(local_t dst, local_t src)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "mov_local [%u]=[%u]", dst, src);
    hwreg_t sreg = local(src);
    hwreg_t dreg = local(dst);
    emit_mov_rr(hw(dreg), hw(sreg));
}


void x86::swap_locals(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "swap_locals %u and %u", d, s);
    hwreg_t sreg = local(s);
    hwreg_t dreg = local(d);
    emit_xchg_rr(hw(dreg), hw(sreg));
}


void x86::store_imm(local_t d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_imm [%u]=%llu %lld 0x%llX", d, v, v, v);
    hwreg_t dreg = local(d);
    load_hw_imm(hw(dreg), uint64_t(v));
}


void x86::store_float(local_t d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_flt [%u] = %g", d, v);
    hwreg_t dreg = local(d, RSPEC_FLT);
    uint32_t bits = *((uint32t *) &v);
    emit_load_xmm_bin(dreg, bits, false);
}


void x86::store_double(local_t d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_dbl [%u] = %g", d, v);
    hwreg_t dreg = local(d, RSPEC_DBL);
    uint64_t bits = *((uint64_t *) &v);
    emit_load_xmm_bin(dreg, bits, false);
}


void x86::binop_local(local_t dloc, local_t sloc, op_t op, byte fp)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    hwspec_t spec = fp == 64 ? RSPEC_DBL : fp == 32 ? RSPEC_FLT : RSPEC_INT;
    x86reg_t d = hw(local(dloc, spec));
    x86reg_t s = hw(local(sloc, spec));
    record(x86, "binop_local%+s [%u]=[%u] reg %u=%u",
           opcode_name(op), dloc, sloc, d, s);
    switch (op)
    {
    case op_add:        emit_add_rr(d, s); break;
    case op_sub:        emit_sub_rr(d, s); break;
    case op_mul:
    case op_umul:       emit_imul_rr(d, s); break;
    case op_div:        emit_idiv_rr(d, s); break;
    case op_udiv:       emit_udiv_rr(d, s); break;
    case op_mod:        emit_imod_rr(d, s); break;
    case op_umod:       emit_umod_rr(d, s); break;
    case op_and:        emit_and_rr (d, s); break;
    case op_or:         emit_or_rr  (d, s); break;
    case op_xor:        emit_xor_rr (d, s); break;
    case op_shl:        emit_shift_rr(d, s, 0xe0); break;
    case op_shr:        emit_shift_rr(d, s, 0xf8); break;
    case op_ushr:       emit_shift_rr(d, s, 0xe8); break;
    case op_fadd:       emit_sse_scalar_op(0xf3, SSE_ADD, d, s); break;
    case op_fsub:       emit_sse_scalar_op(0xf3, SSE_SUB, d, s); break;
    case op_fmul:       emit_sse_scalar_op(0xf3, SSE_MUL, d, s); break;
    case op_fdiv:       emit_sse_scalar_op(0xf3, SSE_DIV, d, s); break;
    case op_dadd:       emit_sse_scalar_op(0xf2, SSE_ADD, d, s); break;
    case op_dsub:       emit_sse_scalar_op(0xf2, SSE_SUB, d, s); break;
    case op_dmul:       emit_sse_scalar_op(0xf2, SSE_MUL, d, s); break;
    case op_ddiv:       emit_sse_scalar_op(0xf2, SSE_DIV, d, s); break;
    default:
        record(x86_error, "binop_local called with op %u", op);
        break;
    }
}


void x86::binop_imm(local_t dloc, value_t v, op_t op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    x86reg_t d = hw(local(dloc));
    record(x86, "binop_imm%+s [%u] %lld reg %u",
           opcode_name(op), dloc, v, d);
    switch (op)
    {
    case op_addi:
    case op_addu:       emit_alu_imm(d, /*ADD*/ 0, v); break;
    case op_subi:
    case op_subu:       emit_alu_imm(d, /*SUB*/ 5, v); break;
    case op_muli:
    case op_mulu:       emit_imul_imm(d, v); break;
    case op_divi:       emit_idiv_imm(d, uint64_t(v)); break;
    case op_divu:       emit_udiv_imm(d, uint64_t(v)); break;
    case op_modi:       emit_imod_imm(d, uint64_t(v)); break;
    case op_modu:       emit_umod_imm(d, uint64_t(v)); break;
    case op_andu:       emit_alu_imm(d, /*AND*/ 4, v); break;
    case op_oru:        emit_alu_imm(d, /*OR*/  1, v); break;
    case op_xoru:       emit_alu_imm(d, /*XOR*/ 6, v); break;
    case op_shlu:       emit_shift_imm8(d, 0xe0, byte(v)); break;
    case op_shru:       emit_shift_imm8(d, 0xf8, byte(v)); break;
    case op_ushru:      emit_shift_imm8(d, 0xe8, byte(v)); break;
    default:
        record(x86_error, "binop_imm called with op %u", op);
        break;
    }
}


void x86::set_pred_cmp(byte p, byte cc)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to x86 SETcc condition
// ----------------------------------------------------------------------------
{
    record(x86, "set_pred_cmp(%u, %u)", p, cc);
    emit_setcc_al(cc);
    emit_movzx_eax_al();
    emit_mov_rr(hw_scr(), HW_RAX);
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


void x86::cmp_local(byte p, local_t d, local_t s, cstring rel)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_local(%u, %u, %u, %s)", p, d, s, rel);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    emit_cmp_rr(HW_RAX, HW_RCX);
    set_pred_cmp(p, setcc_from_rel(rel));
}


void x86::cmp_imm(byte p, local_t d, value_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_imm(%u, %u, %lld, %s)", p, d, v, rel);
    load_local(d, HW_RAX);
    emit_cmp_rax_imm32(uint32_t(v));
    set_pred_cmp(p, setcc_from_rel(rel));
}


void x86::cmp_uimm(byte p, local_t d, uvalue_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, rel);
    cmp_imm(p, d, value_t(v), rel);
}


void x86::cond_mov_local(byte p, local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(x86, "cond_mov_local(%u, %u, %u)", p, d, s);
    emit_pred_test_bit(p);      // BT -> CF
    hwcode_t off = emit_jcc_rel8(JCC_JNC);     // skip if bit clear
    mov_local(d, s);
    patch_branch(off, cursor());
}


void x86::select_local(byte p, local_t d, local_t s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "select_local(%u, %u, %u, %u)", p, d, s, x);
    emit_pred_test_bit(p);                     // BT -> CF
    hwcode_t jnc_pos = emit_jcc_rel8(JCC_JNC); // bit clear -> pick x
    mov_local(d, s);
    hwcode_t jmp_pos = emit_jmp_rel8();
    patch_branch(jnc_pos, cursor());
    mov_local(d, x);
    patch_branch(jmp_pos, cursor());
}


void x86::neg_local(local_t d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    hwreg_t r = local(d);
    emit_grp3_f7(r, 0xd8);
    record(x86, "neg_local %u reg %u", d, r);
}


void x86::not_local(local_t d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    hwreg_t r = local(d);
    emit_grp3_f7(r, 0xd0);
    record(x86, "not_local %u reg %u", d, r);
}


void x86::incr_local(local_t d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    hwreg_t r = local(d);
    emit_grp5_ff(r, 0xc0);
    record(x86, "incr_local %u reg %u", d, r);
}


void x86::decr_local(local_t d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    hwreg_t r = local(d);
    emit_grp5_ff(r, 0xc8);
    record(x86, "decr_local %u reg %u", d, r);
}


void x86::sext_reg(x86reg_t r, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register r to w bits (shared by sext and load paths)
// ----------------------------------------------------------------------------
{
    record(x86, "sext_reg %u width %u", r, w);

    byte modrm = byte(0xc0 | (reg_value(r) << 3) | reg_value(r));
    byte rex   = byte(0x40 | (ptr64() ? 8 : 0)
                      | (rex_r(r) ? 4 : 0)
                      | (rex_b(r) ? 1 : 0));
    switch (w)
    {
    case 8:
        // movsx r64/r32, r8b — REX also required for sil/dil/bpl/spl
        if (ptr64() || r >= 4)
            emit8(rex);
        emit8(0x0f);
        emit8(0xbe);
        emit8(modrm);
        break;
    case 16:
        // movsx r64/r32, r16 — no 0x66
        if (ptr64() || r >= 8)
            emit8(rex);
        emit8(0x0f);
        emit8(0xbf);
        emit8(modrm);
        break;
    case 32:
        if (ptr64())
        {
            emit8(rex);   // REX.W + R/B for r8–r15
            emit8(0x63);  // movsxd r64, r32
            emit8(modrm);
        }
        break;
    default:
        if (w < 64)
        {
            emit_shift_imm8(r, 0xe0, byte(64 - w));
            emit_shift_imm8(r, 0xf8, byte(64 - w));
        }
        break;
    }
}


void x86::sext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "sext_local(%u, %u)", d, w);
    hwreg_t r = local(d);
    sext_reg(r, w);
}


void x86::zext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "zext_local(%u, %u)", d, w);
    if (w < 64)
    {
        hwreg_t r = local(d);
        load_hw_imm(HW_RCX, (1ULL << w) - 1);
        emit_and_rr(r, HW_RCX);
    }
}


void x86::store_mem(local_t d, local_t s, size_t sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem(%u, %u, %u)", d, s, sz);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    if (sz == 1)
    {
        emit_store_u8_at_rax_cl();
    }
    else if (sz == 2)
    {
        emit_store_u16_at_rax_rcx();
    }

    else if (sz == 4)
    {
        emit_store_u32_at_rax_rcx();
    }

    else { emit_rex_modrm(1, HW_RCX, HW_RAX, HW_RAX, 0x89); }
}


void x86::load_mem(local_t d, local_t s, size_t sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_RAX);
    if (sz == 1)
    {
        emit_load_u8_zext_rax();
    }

    else if (sz == 2)
    {
        emit_load_u16_zext_rax();
    }

    else if (sz == 4)
    {
        emit_load_u32_rax();
    }

    else { emit_rex_modrm(1, HW_RAX, HW_RAX, HW_RAX, 0x8b); }
    store_local(HW_RAX, d);
}


void x86::store_mem_off(local_t d, local_t s, value_t o, size_t sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d plus offset
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(d, HW_RAX);
    if (o)
    {
        load_hw_imm(HW_RDX, uint64_t(o));
        emit_add_rr(HW_RAX, HW_RDX);
    }
    load_local(s, HW_RCX);
    if (sz == 1)
    {
        emit_store_u8_at_rax_cl();
    }

    else if (sz == 2)
    {
        emit_store_u16_at_rax_rcx();
    }

    else if (sz == 4)
    {
        emit_store_u32_at_rax_rcx();
    }

    else { emit_rex_modrm(1, HW_RCX, HW_RAX, HW_RAX, 0x89); }
}


void x86::load_mem_off(local_t d, local_t s, value_t o, size_t sz)
// ----------------------------------------------------------------------------
//   Load from address in local s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(s, HW_RAX);
    if (o)
    {
        load_hw_imm(HW_RCX, uint64_t(o));
        emit_add_rr(HW_RAX, HW_RCX);
    }
    if (sz == 1)
    {
        emit_load_u8_zext_rax();
    }

    else if (sz == 2)
    {
        emit_load_u16_zext_rax();
    }

    else if (sz == 4)
    {
        emit_load_u32_rax();
    }

    else { emit_rex_modrm(1, HW_RAX, HW_RAX, HW_RAX, 0x8b); }
    store_local(HW_RAX, d);
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
        emit_rex_modrm(1, HW_RAX, HW_RAX, HW_RAX, 0x8b);
    else
    {
        size_t sz = (w + 7) / 8;
        if (sz == 1)
            emit_load_u8_zext_rax();
        else if (sz == 2)
            emit_load_u16_zext_rax();
        else if (sz == 4)
            emit_load_u32_rax();
        else
            emit_rex_modrm(1, HW_RAX, HW_RAX, HW_RAX, 0x8b);
        sext_reg(HW_RAX, w);
    }
}


void x86::load_signext(local_t d, local_t s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext(%u, %u, %u)", d, s, w);
    load_local(s, HW_RAX);
    load_signext_mem(w);
    store_local(HW_RAX, d);
}

void x86::load_signext_off(local_t d, local_t s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_local(s, HW_RAX);
    if (o)
    {
        load_hw_imm(HW_RCX, uint64_t(o));
        emit_add_rr(HW_RAX, HW_RCX);
    }
    load_signext_mem(w);
    store_local(HW_RAX, d);
}


void x86::frame_addr(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address of local slot s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "frame_addr(%u, %u)", d, s);
    load_hw_imm(HW_RAX, uint64_t(local_disp(s)));
    emit_add_rr(HW_RAX, HW_FRAME);
    store_local(HW_RAX, d);
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


void x86::bra_zero(local_t s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_RAX);
    emit_test_rr(HW_RAX, HW_RAX);
    size_t pos = emit_jcc_rel32(eq ? JCC_JZ : JCC_JNZ);
    record_branch(b, uint(pos));
}


void x86::bra_signed(local_t s, broff_t b, byte cc)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(x86, "bra_signed(%u, %lld, %u)", s, b, cc);
    load_local(s, HW_RAX);
    emit_test_rr(HW_RAX, HW_RAX);
    size_t pos = emit_jcc_rel32(cc);
    record_branch(b, uint(pos));
}


void x86::dbra_local(local_t d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "dbra_local(%u, %lld)", d, b);
    load_local(d, HW_RAX);
    emit_grp5_ff(HW_RAX, 0xc8);
    store_local(HW_RAX, d);
    bra_zero(d, b, false);
}


void x86::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(x86, "jmp_tok %llu", t);
    hwcode_t target = translate(t);
    emit_jmp_to(target);
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


void x86::ijmp_local(local_t s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(x86, "ijmp_local(%u)", s);
    load_local(s, HW_RAX);
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_jit_addr");
    emit_cleanup_call_args(1);
    br_via_ip();
}


void x86::icall_local(local_t s)
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
    load_local(s, HW_RAX);
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


void x86::load_pc(local_t d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_pc(%u)", d);
    load_hw_imm(HW_RAX, cursor());
    store_local(HW_RAX, d);
}


void x86::load_pca(local_t d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (interpreter semantics)
// ----------------------------------------------------------------------------
{
    record(x86, "load_pca(%u, %lld)", d, b);
    (void) b;
    load_hw_imm(HW_RAX, uint64_t(pending_branch_target));
    store_local(HW_RAX, d);
}


void x86::get_arg(local_t d, local_t s)
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


void x86::set_arg(local_t d, local_t s)
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


void x86::push_local(local_t s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_local(%u)", s);
    load_local(s, HW_RAX);
    emit_set_arg0_rax();
    emit_runtime_call("xlr8_push_value");
    emit_cleanup_call_args(1);
}


void x86::pop_local(local_t d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(x86, "pop_local(%u)", d);
    emit_runtime_call("xlr8_pop_value");
    store_local(HW_RAX, d);
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


void x86::store_global(local_t d, local_t s)
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


void x86::load_global(local_t d, local_t s)
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


void x86::prologue()
// ----------------------------------------------------------------------------
//   Emit standard function prologues
// ----------------------------------------------------------------------------
{
    record(x86, "prologue");

    // i386 SysV: ebx/esi/edi/ebp are callee-saved.  We keep frame in edi
    // and flags in esi for the whole translation (see hw_frame/hw_flags).
    emit_push_reg(HW_RBX);
    emit_push_reg(HW_RSI);
    emit_push_reg(HW_RDI);
    emit_push_reg(HW_RBP);
}


void x86::epilogue()
// ----------------------------------------------------------------------------
//   Emit epilogue (restore callee-saved) and return
// ----------------------------------------------------------------------------
{
    record(x86, "epilogue()");
    emit_pop_reg(HW_RBP);
    emit_pop_reg(HW_RDI);
    emit_pop_reg(HW_RSI);
    emit_pop_reg(HW_RBX);
    emit_insn_ret();
}


void x86::call_ptr(void *fn)
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in RAX
// ----------------------------------------------------------------------------
{
    record(x86, "call_ptr %p", fn);
#ifdef _WIN64
    // Microsoft x64: caller provides 32 bytes of register parameter home space
    if (ptr64())
        emit_sub_rsp_imm8(0x20);
#endif
    load_hw_imm(HW_RAX, uint64_t(fn));
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
        emit_push_reg(HW_RAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_RAX);
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
        emit_push_reg(HW_RAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_RAX);
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
        emit_push_reg(HW_RAX);
        call_ptr(xlr8_runtime_fn("xlr8_get_frame"));
        emit_mov_frame_rax();
        emit_pop_reg(HW_RAX);
    }
}


#define EMIT(op)        void x86::emit_##op(emit_args &e)

#include "x86-emit.cpp"
