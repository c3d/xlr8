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
    record(x86, "x86(%lld)", arch);
}


void x86::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(x86, "finish_translation()");
    do_ret();
}


size_t x86::emit_over_callees_branch()
// ----------------------------------------------------------------------------
//   Emit a forward branch over callee bodies to the epilogue
// ----------------------------------------------------------------------------
{
    record(x86, "emit_over_callees_branch()");
    g(0xe9);
    size_t pos = cursor();
    emit32(0);
    return pos;
}


void x86::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Patch a recorded branch instruction to jump to target_native
// ----------------------------------------------------------------------------
{
    record(x86, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    int32_t off = int32_t(target_native) - int32_t(insn_pos) - 4;
    code().patch32(insn_pos, uint32_t(off));
}


void x86::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload r12 from rax after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    record(x86, "reload_frame_reg()");
    g(0x49);
    g(0x89);
    g(0xc4);
}


void x86::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to native address in rax
// ----------------------------------------------------------------------------
{
    record(x86, "br_via_ip()");
    g(0xff);
    g(0xe0);
}


int x86::local_disp(byte n) const
// ----------------------------------------------------------------------------
//   Return stack displacement for bytecode local n
// ----------------------------------------------------------------------------
{
    record(x86, "local_disp(%u)", n);
    return int(n) * int(ptr_size());
}


void x86::orex(int ll, int r, int x, byte c)
// ----------------------------------------------------------------------------
//   Emit REX prefix and opcode byte
// ----------------------------------------------------------------------------
{
    record(x86, "orex(%lld, %lld, %lld, %u)", ll, r, x, c);
    if (ll || r >= 8 || x >= 8)
        g(0x40 | (ll ? 8 : 0) | (rex_r(r) ? 4 : 0) | (rex_b(x) ? 1 : 0));
    g(c);
}


void x86::emit_rex_modrm(int ll, int r, int x, int b, byte op)
// ----------------------------------------------------------------------------
//   Emit REX prefix and ModR/M encoded instruction
// ----------------------------------------------------------------------------
{
    record(x86, "emit_rex_modrm(%lld, %lld, %lld, %lld, %u)", ll, r, x, b, op);
    if (ll || r >= 8 || x >= 8 || b >= 8)
        g(0x48 | (rex_r(r) ? 4 : 0) | (rex_x(x) ? 2 : 0) | (rex_b(b) ? 1 : 0));
    g(op);
    g(byte((reg_value(r) << 3) | reg_value(b)));
    if (reg_value(b) == 4)
        g(byte((reg_value(x) << 3) | reg_value(b)));
    if (reg_value(b) == 5 && x == 4)
        emit32(0);
}


void x86::load_hw_imm(uint r, uint64_t imm)
// ----------------------------------------------------------------------------
//   Load immediate into a hardware register
// ----------------------------------------------------------------------------
{
    record(x86, "load_hw_imm(%u, %lld)", r, imm);
    if (ptr_size() == 8 || r >= 8)
        g(0x40 | (ptr_size() == 8 ? 8 : 0) | (rex_b(r) ? 1 : 0));
    g(0xb8 + reg_value(r));
    emit32(uint32_t(imm));
    if (ptr_size() == 8)
        emit32(uint32_t(imm >> 32));
}


void x86::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    record(x86, "load_local(%u, %u)", n, r);
    int disp = local_disp(n);
    if (disp)
    {
        if (1 || r >= 8 || HW_FRAME >= 8)
            g(0x48 | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        g(0x8b);
        g(byte((2 << 6) | (reg_value(r) << 3) | 4));
        g(byte((reg_value(HW_RSP) << 3) | reg_value(HW_FRAME)));
        emit32(uint32_t(disp));
    }
    else
    {
        emit_rex_modrm(1, r, HW_RSP, HW_FRAME, 0x8b);
    }
}


void x86::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_local(%u, %u)", r, n);
    int disp = local_disp(n);
    if (disp)
    {
        if (1 || r >= 8 || HW_FRAME >= 8)
            g(0x48 | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        g(0x89);
        g(byte((2 << 6) | (reg_value(r) << 3) | 4));
        g(byte((reg_value(HW_RSP) << 3) | reg_value(HW_FRAME)));
        emit32(uint32_t(disp));
    }
    else
    {
        emit_rex_modrm(1, r, HW_RSP, HW_FRAME, 0x89);
    }
}


void x86::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "mov_local(%u, %u)", d, s);
    load_local(s, HW_RAX);
    store_local(HW_RAX, d);
}


void x86::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "swap_locals(%u, %u)", d, s);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    store_local(HW_RCX, d);
    store_local(HW_RAX, s);
}


void x86::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_imm(%u, %lld)", d, v);
    load_hw_imm(HW_RAX, uint64_t(v));
    store_local(HW_RAX, d);
}


void x86::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_float(%u, %g)", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    load_hw_imm(HW_RAX, bits);
    store_local(HW_RAX, d);
}


void x86::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_double(%u, %g)", d, v);
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    load_hw_imm(HW_RAX, bits);
    store_local(HW_RAX, d);
}


void x86::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "binop_local(%u, %u, %s)", d, s, op);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x01); g(modrm_rr(HW_RCX, HW_RAX));
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x29); g(modrm_rr(HW_RCX, HW_RAX));
}

else if (!strcmp(op, "imul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x0f); g(0xaf); g(modrm_rr(HW_RAX, HW_RCX));
}

else if (!strcmp(op, "and"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x21); g(modrm_rr(HW_RCX, HW_RAX));
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x09); g(modrm_rr(HW_RCX, HW_RAX));
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x31); g(modrm_rr(HW_RCX, HW_RAX));
}

else if (!strcmp(op, "shl"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RCX, HW_RAX, 0xd3); g(0xe0 | reg_value(HW_RAX));
}

else if (!strcmp(op, "sar"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RCX, HW_RAX, 0xd3); g(0xf8 | reg_value(HW_RAX));
}

else if (!strcmp(op, "shr"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RCX, HW_RAX, 0xd3); g(0xe8 | reg_value(HW_RAX));
}

else if (!strcmp(op, "idiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48);
    g(0x99);
    g(0x48);
    g(0xf7);
    g(0xf9);
}

else if (!strcmp(op, "udiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48);
    g(0x31);
    g(0xd2);
    g(0x48);
    g(0xf7);
    g(0xf1);
}

else if (!strcmp(op, "div"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48);
    g(0x31);
    g(0xd2);
    g(0x48);
    g(0xf7);
    g(0xf1);
}

else if (!strcmp(op, "fadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0xf3); g(0x0f); g(0x6e); g(0xc9);
    g(0xf3); g(0x0f); g(0x58); g(0xc1);
    g(0xf3); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "fsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0xf3); g(0x0f); g(0x6e); g(0xc9);
    g(0xf3); g(0x0f); g(0x5c); g(0xc1);
    g(0xf3); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "fmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0xf3); g(0x0f); g(0x6e); g(0xc9);
    g(0xf3); g(0x0f); g(0x59); g(0xc1);
    g(0xf3); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "fdiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0xf3); g(0x0f); g(0x6e); g(0xc9);
    g(0xf3); g(0x0f); g(0x5e); g(0xc1);
    g(0xf3); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "dadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc9);
    g(0xf2); g(0x0f); g(0x58); g(0xc1);
    g(0x48); g(0x66); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "dsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc9);
    g(0xf2); g(0x0f); g(0x5c); g(0xc1);
    g(0x48); g(0x66); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "dmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc9);
    g(0xf2); g(0x0f); g(0x59); g(0xc1);
    g(0x48); g(0x66); g(0x0f); g(0x7e); g(0xc0);
}

else if (!strcmp(op, "ddiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc0);
    load_local(s, HW_RCX);
    g(0x48); g(0x66); g(0x0f); g(0x6e); g(0xc9);
    g(0xf2); g(0x0f); g(0x5e); g(0xc1);
    g(0x48); g(0x66); g(0x0f); g(0x7e); g(0xc0);
}

    store_local(HW_RAX, d);
}


void x86::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    record(x86, "binop_imm(%u, %lld, %s)", d, v, op);
    load_local(d, HW_RAX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x05); emit32(uint32_t(v));
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x2d); emit32(uint32_t(v));
}

else if (!strcmp(op, "imul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x69); g(modrm_rr(HW_RAX, HW_RAX)); emit32(uint32_t(v));
}

else if (!strcmp(op, "and"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x25); emit32(uint32_t(v));
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x0d); emit32(uint32_t(v));
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x35); emit32(uint32_t(v));
}

else if (!strcmp(op, "shl_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RCX, uint64_t(v)); orex(1, HW_RCX, HW_RAX, 0xd3); g(0xe0|reg_value(HW_RAX));
}

else if (!strcmp(op, "sar_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RCX, uint64_t(v)); orex(1, HW_RCX, HW_RAX, 0xd3); g(0xf8|reg_value(HW_RAX));
}

else if (!strcmp(op, "shr_imm"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RCX, uint64_t(v)); orex(1, HW_RCX, HW_RAX, 0xd3); g(0xe8|reg_value(HW_RAX));
}

    store_local(HW_RAX, d);
}


void x86::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(x86, "set_pred(%u, %d)", p, on);
    if (on)
    {
        load_hw_imm(HW_RAX, 1ULL << p);
        orex(1, HW_FLAGS, HW_RAX, 0x09);
        g(modrm_rr(HW_FLAGS, HW_RAX));
    }
    else
    {
        load_hw_imm(HW_RAX, ~(1ULL << p));
        orex(1, HW_FLAGS, HW_RAX, 0x21);
        g(modrm_rr(HW_FLAGS, HW_RAX));
    }
}


void x86::cmp_local(byte p, byte d, byte s, cstring rel)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_local(%u, %u, %u, %s)", p, d, s, rel);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    orex(1, HW_RAX, HW_RCX, 0x39);
    g(modrm_rr(HW_RCX, HW_RAX));
    byte cc = 0x94;
    if (!strcmp(rel, "eq")) cc = 0x94;
    else if (!strcmp(rel, "ne")) cc = 0x95;
    else if (!strcmp(rel, "lt")) cc = 0x9c;
    else if (!strcmp(rel, "le")) cc = 0x9e;
    else if (!strcmp(rel, "ltu")) cc = 0x92;
    else if (!strcmp(rel, "leu")) cc = 0x96;
    g(0x0f); g(0x90 | (cc & 0x0f));
    g(modrm_rr(HW_RAX, HW_RAX));
    set_pred(p, true);
}


void x86::cmp_imm(byte p, byte d, value_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(x86, "cmp_imm(%u, %u, %lld, %s)", p, d, v, rel);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0x3d);
    emit32(uint32_t(v));
    (void) rel;
    set_pred(p, true);
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
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
    record(x86, "cond_mov_local(%u, %u, %u)", p, d, s);
    load_local(s, HW_RAX);
    uint pos = gjmp(0);
    store_local(HW_RAX, d);
    gsym(pos);
    (void) p;
}


void x86::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d
// ----------------------------------------------------------------------------
{
    record(x86, "select_local(%u, %u, %u, %u)", p, d, s, x);
    load_local(s, HW_RAX);
    uint pos = gjmp(0);
    load_local(x, HW_RAX);
    gsym(pos);
    store_local(HW_RAX, d);
    (void) p;
}


void x86::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "neg_local(%u)", d);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0xf7);
    g(0xd8 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "not_local(%u)", d);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0xf7);
    g(0xd0 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "incr_local(%u)", d);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0xff);
    g(0xc0 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    record(x86, "decr_local(%u)", d);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0xff);
    g(0xc8 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "sext_local(%u, %u)", d, w);
    load_local(d, HW_RAX);
    load_hw_imm(HW_RCX, 64 - w);
    orex(1, HW_RCX, HW_RAX, 0xd3);
    g(0xf8 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "zext_local(%u, %u)", d, w);
    load_local(d, HW_RAX);
    load_hw_imm(HW_RCX, w);
    orex(1, HW_RCX, HW_RAX, 0xd3);
    g(0xe0 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address in local s
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem(%u, %u, %u)", d, s, sz);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x88); g(0x08);
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x66); g(0x89); g(0x08);
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x89); g(0x08);
}

    else { emit_rex_modrm(1, HW_RCX, HW_RAX, HW_RAX, 0x89); }
}


void x86::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_RAX);
if (sz == 1)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x8a); g(0x00);
}

else if (sz == 2)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x66); g(0x8b); g(0x00);
}

else if (sz == 4)
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0x8b); g(0x00);
}

    else { emit_rex_modrm(1, HW_RAX, HW_RAX, HW_RAX, 0x8b); }
    store_local(HW_RAX, d);
}


void x86::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address s plus offset
// ----------------------------------------------------------------------------
{
    record(x86, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    load_hw_imm(HW_RDX, uint64_t(o));
    orex(1, HW_RDX, HW_RAX, 0x01);
    g(modrm_rr(HW_RDX, HW_RAX));
    store_mem(d, s, sz);
}


void x86::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(s, HW_RAX);
    load_hw_imm(HW_RCX, uint64_t(o));
    orex(1, HW_RCX, HW_RAX, 0x01);
    g(modrm_rr(HW_RCX, HW_RAX));
    load_mem(d, s, sz);
}


void x86::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext(%u, %u, %u)", d, s, w);
    load_mem(d, s, (w + 7) / 8);
    sext_local(d, w);
}


void x86::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(x86, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_mem_off(d, s, o, (w + 7) / 8);
    sext_local(d, w);
}


void x86::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address into local d
// ----------------------------------------------------------------------------
{
    record(x86, "frame_addr(%u, %u)", d, s);
    load_local(s, HW_RAX);
    orex(1, HW_FRAME, HW_RAX, 0x8d);
    g(0x04 | (reg_value(HW_FRAME) << 3) | reg_value(HW_RAX));
    store_local(HW_RAX, d);
}


void x86::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(x86, "bra_offset(%lld)", b);
    uint pos = gjmp(0);
    record_branch(b, pos);
}


void x86::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(x86, "bra_pred(%u, %lld, %d)", p, b, sense);
    load_hw_imm(HW_RAX, 1ULL << p);
    orex(1, HW_RAX, HW_FLAGS, 0x21);
    g(modrm_rr(HW_RAX, HW_FLAGS));
    orex(1, HW_FLAGS, 0, 0x85);
    g(modrm_rr(HW_FLAGS, HW_FLAGS));
    g(0x0f);
    g(sense ? 0x85 : 0x84);
    uint pos = uint(cursor());
    emit32(0);
    record_branch(b, pos);
}


void x86::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_RAX);
    orex(1, HW_RAX, 0, 0x85);
    g(modrm_rr(HW_RAX, HW_RAX));
    g(0x0f);
    g(eq ? 0x84 : 0x85);
    uint pos = uint(cursor());
    emit32(0);
    record_branch(b, pos);
}


void x86::bra_signed(byte s, broff_t b, byte cc)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(x86, "bra_signed(%u, %lld, %u)", s, b, cc);
    load_local(s, HW_RAX);
    orex(1, HW_RAX, 0, 0x85);
    g(modrm_rr(HW_RAX, HW_RAX));
    g(0x0f); g(cc);
    uint pos = uint(cursor());
    emit32(0);
    record_branch(b, pos);
}


void x86::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(x86, "dbra_local(%u, %lld)", d, b);
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0xff);
    g(0xc8 | reg_value(HW_RAX));
    store_local(HW_RAX, d);
    bra_zero(d, b, false);
}


void x86::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(x86, "jmp_tok(%llu)", t);
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
}


void x86::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(x86, "call_tok(%llu)", t);
    size_t adr_at = cursor();
    g(0x48);
    g(0x8d);
    g(0x05);
    emit32(0);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_push_value");
    emit_runtime_call("xlr8_sync_args");
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
    int off = int(pos + 4) - int(adr_at + 7);
    code().patch32(adr_at + 3, uint32_t(off));
}


void x86::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(x86, "ijmp_local(%u)", s);
    load_local(s, HW_RAX);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_jit_addr");
    br_via_ip();
}


void x86::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(x86, "icall_local(%u)", s);
    size_t adr_at = cursor();
    g(0x48);
    g(0x8d);
    g(0x05);
    emit32(0);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_push_value");
    emit_runtime_call("xlr8_sync_args");
    load_local(s, HW_RAX);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_jit_addr");
    size_t call_at = cursor();
    g(0xff);
    g(0xd0);
    int off = int(call_at + 2) - int(adr_at + 7);
    code().patch32(adr_at + 3, uint32_t(off));
}


void x86::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_pc()");
    size_t adr_at = cursor();
    g(0x48);
    g(0x8d);
    g(0x05);
    emit32(0);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_push_value");
    code().patch32(adr_at + 3, uint32_t(-7));
}


void x86::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_pc(%u)", d);
    load_hw_imm(HW_RAX, cursor());
    store_local(HW_RAX, d);
}


void x86::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (interpreter semantics)
// ----------------------------------------------------------------------------
{
    record(x86, "load_pca(%u, %lld)", d, b);
    (void) b;
    load_hw_imm(HW_RAX, uint64_t(pending_branch_target));
    store_local(HW_RAX, d);
}


void x86::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(x86, "get_arg(%u, %u)", d, s);
    load_hw_imm(HW_RDI, uint64_t(d));
    load_hw_imm(HW_RSI, uint64_t(s));
    emit_runtime_call("xlr8_get_arg");
}


void x86::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local s into argument slot d
// ----------------------------------------------------------------------------
{
    record(x86, "set_arg(%u, %u)", d, s);
    load_hw_imm(HW_RDI, uint64_t(d));
    load_hw_imm(HW_RSI, uint64_t(s));
    emit_runtime_call("xlr8_set_arg");
}


void x86::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(x86, "push_local(%u)", s);
    load_local(s, HW_RAX);
    g(0x48);
    g(0x89);
    g(0xc7);
    emit_runtime_call("xlr8_push_value");
}


void x86::pop_local(byte d)
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
    orex(1, HW_FLAGS, 0, 0xff);
    g(0x30 | reg_value(HW_RSP));
}


void x86::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(x86, "pop_flags()");
    orex(1, HW_RSP, 0, 0x8f);
    g(0x00 | reg_value(HW_RSP));
}


void x86::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(x86, "store_global(%u, %u)", d, s);
    mov_local(d, s);
}


void x86::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(x86, "load_global(%u, %u)", d, s);
    mov_local(d, s);
}


void x86::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    record(x86, "do_ret()");
    g(0xc3);
}


void x86::call_ptr(void *fn)
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in RAX
// ----------------------------------------------------------------------------
{
    record(x86, "call_ptr(%p)", fn);
    load_hw_imm(HW_RAX, uint64_t(fn));
    g(0xff);
    g(0xd0);
}


void x86::emit_trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(x86_warning, "emit_trap(%s)", msg);
    (void) msg;
    g(0xcc);
}


void x86::emit_runtime_call(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s)", fn);
    call_ptr(xlr8_runtime_fn(fn));
}


void x86::emit_runtime_call(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s, %llu)", fn, a);
    load_hw_imm(HW_RDI, uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
}


void x86::emit_runtime_call(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(x86, "emit_runtime_call(%s, %u)", fn, a);
    load_hw_imm(HW_RDI, uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
    if (!strcmp(fn, "xlr8_enter") || !strcmp(fn, "xlr8_leave"))
        reload_frame_reg();
}



#include "x86-emit.cpp"

