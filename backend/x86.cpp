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


x86::x86(arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an x86 family code generator
// ----------------------------------------------------------------------------
: backend(arch)
{
}


void x86::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    do_ret();
}


int x86::local_disp(byte n) const
// ----------------------------------------------------------------------------
//   Return stack displacement for bytecode local n
// ----------------------------------------------------------------------------
{
    return int(n) * int(ptr_size());
}


void x86::orex(int ll, int r, int x, byte c)
// ----------------------------------------------------------------------------
//   Emit REX prefix and opcode byte
// ----------------------------------------------------------------------------
{
    if ((ll && r >= 8) || x >= 8)
        g(0x40 | (ll ? 1 : 0) | (rex_b(r) ? 4 : 0) | (rex_x(x) ? 2 : 0) | (rex_r(r) ? 1 : 0));
    g(c);
}


void x86::emit_rex_modrm(int ll, int r, int x, int b, byte op)
// ----------------------------------------------------------------------------
//   Emit REX prefix and ModR/M encoded instruction
// ----------------------------------------------------------------------------
{
    if (ll || r >= 8 || x >= 8 || b >= 8)
        g(0x48 | (rex_r(r) ? 4 : 0) | (rex_x(x) ? 2 : 0) | (rex_b(b) ? 1 : 0));
    g(op);
    g(byte((reg_value(r) << 3) | reg_value(b)));
    if (reg_value(b) == 4)
        g(byte(reg_value(x) << 3));
    if (reg_value(b) == 5 && x == 4)
        emit32(0);
}


void x86::load_hw_imm(uint r, uint64_t imm)
// ----------------------------------------------------------------------------
//   Load immediate into a hardware register
// ----------------------------------------------------------------------------
{
    orex(1, r, 0, 0xb8 + reg_value(r));
    emit32(uint32_t(imm));
    if (ptr_size() == 8)
        emit32(uint32_t(imm >> 32));
}


void x86::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    int disp = local_disp(n);
    emit_rex_modrm(1, r, HW_FRAME, HW_FRAME, 0x8b);
    if (disp)
    {
        g(byte(0x80 | (reg_value(r) << 3) | reg_value(HW_FRAME)));
        emit32(uint32_t(disp));
    }
}


void x86::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    int disp = local_disp(n);
    emit_rex_modrm(1, r, HW_FRAME, HW_FRAME, 0x89);
    if (disp)
    {
        g(byte(0x80 | (reg_value(r) << 3) | reg_value(HW_FRAME)));
        emit32(uint32_t(disp));
    }
}


void x86::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    load_local(s, HW_RAX);
    store_local(HW_RAX, d);
}


void x86::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
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
    load_hw_imm(HW_RAX, uint64_t(v));
    store_local(HW_RAX, d);
}


void x86::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
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
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x01); g(modrm_rr(HW_RAX, HW_RCX));
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x29); g(modrm_rr(HW_RAX, HW_RCX));
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
    orex(1, HW_RAX, HW_RCX, 0x21); g(modrm_rr(HW_RAX, HW_RCX));
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x09); g(modrm_rr(HW_RAX, HW_RCX));
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RCX, 0x31); g(modrm_rr(HW_RAX, HW_RCX));
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
    orex(1, HW_RAX, HW_RDX, 0x99); g(0xf1);
}

else if (!strcmp(op, "div"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, HW_RDX, 0xf7); g(0xf0);
}

else if (!strcmp(op, "fadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x58); g(0xc0);
}

else if (!strcmp(op, "fsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x5c); g(0xc0);
}

else if (!strcmp(op, "fmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x59); g(0xc0);
}

else if (!strcmp(op, "fdiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf3); g(0x0f); g(0x5e); g(0xc0);
}

else if (!strcmp(op, "dadd"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf2); g(0x0f); g(0x58); g(0xc0);
}

else if (!strcmp(op, "dsub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf2); g(0x0f); g(0x5c); g(0xc0);
}

else if (!strcmp(op, "dmul"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf2); g(0x0f); g(0x59); g(0xc0);
}

else if (!strcmp(op, "ddiv"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    g(0xf2); g(0x0f); g(0x5e); g(0xc0);
}

    store_local(HW_RAX, d);
}


void x86::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    load_local(d, HW_RAX);
if (!strcmp(op, "add"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x05); emit32(uint32_t(v)); if (ptr_size()==8) emit32(uint32_t(uint64_t(v)>>32));
}

else if (!strcmp(op, "sub"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x2d); emit32(uint32_t(v)); if (ptr_size()==8) emit32(uint32_t(uint64_t(v)>>32));
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
    orex(1, HW_RAX, 0, 0x25); emit32(uint32_t(v)); if (ptr_size()==8) emit32(uint32_t(uint64_t(v)>>32));
}

else if (!strcmp(op, "or"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x0d); emit32(uint32_t(v)); if (ptr_size()==8) emit32(uint32_t(uint64_t(v)>>32));
}

else if (!strcmp(op, "xor"))
// ----------------------------------------------------------------------------
//   If
// ----------------------------------------------------------------------------
{
    orex(1, HW_RAX, 0, 0x35); emit32(uint32_t(v)); if (ptr_size()==8) emit32(uint32_t(uint64_t(v)>>32));
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
    load_local(d, HW_RAX);
    load_local(s, HW_RCX);
    orex(1, HW_RAX, HW_RCX, 0x39);
    g(modrm_rr(HW_RAX, HW_RCX));
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
    load_local(d, HW_RAX);
    orex(1, HW_RAX, 0, 0x3d);
    emit32(uint32_t(v));
    if (ptr_size() == 8) emit32(uint32_t(uint64_t(v) >> 32));
    (void) rel;
    set_pred(p, true);
}


void x86::cmp_uimm(byte p, byte d, uvalue_t v, cstring rel)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), rel);
}


void x86::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
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
    load_mem(d, s, (w + 7) / 8);
    sext_local(d, w);
}


void x86::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    load_mem_off(d, s, o, (w + 7) / 8);
    sext_local(d, w);
}


void x86::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address into local d
// ----------------------------------------------------------------------------
{
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
    uint pos = gjmp(0);
    record_branch(b, pos);
}


void x86::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    (void) p; (void) sense;
    bra_offset(b);
}


void x86::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    load_local(s, HW_RAX);
    orex(1, HW_RAX, 0, 0x85);
    g(modrm_rr(HW_RAX, HW_RAX));
    g(eq ? 0x74 : 0x75);
    uint pos = uint(cursor());
    emit32(0);
    record_branch(b, pos);
}


void x86::bra_signed(byte s, broff_t b, byte cc)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
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
    record_tok_jump(t, gjmp(0));
}


void x86::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record_tok_jump(t, gjmp(0));
}


void x86::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    load_local(s, HW_RAX);
    orex(1, HW_RAX, 0, 0xff);
    g(0xe0 | reg_value(HW_RAX));
}


void x86::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    ijmp_local(s);
}


void x86::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    g(0x68);
    emit32(uint32_t(cursor()));
}


void x86::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RAX, cursor());
    store_local(HW_RAX, d);
}


void x86::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load PC plus offset into local d
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RAX, cursor() + size_t(b));
    store_local(HW_RAX, d);
}


void x86::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void x86::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local d into argument slot s
// ----------------------------------------------------------------------------
{
    mov_local(s, d);
}


void x86::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    load_local(s, HW_RAX);
    orex(1, HW_RSP, 0, 0xff);
    g(0x30 | reg_value(HW_RSP));
}


void x86::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    orex(1, HW_RSP, 0, 0x8f);
    g(0x00 | reg_value(HW_RSP));
    store_local(HW_RAX, d);
}


void x86::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    orex(1, HW_FLAGS, 0, 0xff);
    g(0x30 | reg_value(HW_RSP));
}


void x86::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    orex(1, HW_RSP, 0, 0x8f);
    g(0x00 | reg_value(HW_RSP));
}


void x86::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void x86::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void x86::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    g(0xc3);
}


void x86::call_ptr(void *fn)
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in RAX
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RAX, uint64_t(fn));
    g(0xff);
    g(0xd0);
}


void x86::emit_trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    (void) msg;
    g(0xcc);
}


void x86::emit_runtime_call(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    call_ptr(xlr8_runtime_fn(fn));
}


void x86::emit_runtime_call(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RDI, uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
}


void x86::emit_runtime_call(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    load_hw_imm(HW_RDI, uint64_t(a));
    call_ptr(xlr8_runtime_fn(fn));
    if (!strcmp(fn, "xlr8_enter") || !strcmp(fn, "xlr8_leave"))
    {
        g(0x49);
        g(0x89);
        g(0xc4);
    }
}



#include "x86-emit.cpp"
