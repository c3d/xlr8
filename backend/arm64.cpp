// ****************************************************************************
//  arm64.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     AArch64 instruction helpers and opcode emitters
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

#include "arm64.h"

#include <cstring>


arm64::arm64()
// ----------------------------------------------------------------------------
//   Construct an AArch64 code generator
// ----------------------------------------------------------------------------
    : backend(ARCH_ARM64)
{
}


cstring arm64::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    return "__aarch64__\0__arm64__\0";
}

uint arm64::nb_regs() const
// ----------------------------------------------------------------------------
//   Return number of bytecode registers
// ----------------------------------------------------------------------------
{
    return 28;
}

uint arm64::ptr_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    return 8;
}

uint arm64::reg_iret() const
// ----------------------------------------------------------------------------
//   Return integer return-value register index
// ----------------------------------------------------------------------------
{
    return 0;
}


void arm64::emit32le(uint32_t insn)
// ----------------------------------------------------------------------------
//   Emit a 32-bit instruction word
// ----------------------------------------------------------------------------
{
    emit32(insn);
}


uint arm64::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain
// ----------------------------------------------------------------------------
{
    uint pos = uint(cursor());
    emit32(t);
    return pos;
}

void arm64::gsym_addr(uint t, uint a)
// ----------------------------------------------------------------------------
//   Patch pending jumps to address a
// ----------------------------------------------------------------------------
{
    section &sec = code();
    while (t)
    {
        uint n = t;
        uint32_t next = 0;
        if (n >= 4)
        {
            size_t at = n - 4;
            next = uint32_t(sec[at]) | (uint32_t(sec[at+1])<<8)
                 | (uint32_t(sec[at+2])<<16) | (uint32_t(sec[at+3])<<24);
        }
        int32_t offset = (int32_t(a) - int32_t(n)) / 4;
        sec.patch32(n - 4, 0x14000000u | (uint32_t(offset) & 0x03ffffffu));
        t = next;
    }
}

static uint xreg(byte n)
// ----------------------------------------------------------------------------
//   Map bytecode register to hardware register
// ----------------------------------------------------------------------------
{
    return n;
}


void arm64::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    emit32le(0x8b000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
}

void arm64::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    emit32le(0x8b000000u | (xreg(s)<<16) | (9<<5) | 9);
    emit32le(0x8b000000u | (xreg(d)<<16) | (xreg(s)<<5) | xreg(s));
    emit32le(0x8b000000u | (9<<16) | (xreg(d)<<5) | xreg(d));
}

void arm64::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    emit32le(0xd2800000u | ((uint32_t(v)&0xffffu)<<5) | xreg(d));
}

void arm64::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    uint32_t bits = 0; memcpy(&bits, &v, 4);
    store_imm(d, value_t(bits));
}

void arm64::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    uint64_t bits = 0; memcpy(&bits, &v, 8);
    store_imm(d, value_t(bits));
}

void arm64::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    if (!strcmp(op, "add")) emit32le(0x8b000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "sub")) emit32le(0xcb000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "mul")) emit32le(0x9b007c00u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "and")) emit32le(0x8a000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "or"))  emit32le(0xaa000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "xor")) emit32le(0xca000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "shl")) emit32le(0x9ac02000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "shr")) emit32le(0x9ac02400u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "ushr")) emit32le(0x9ac02800u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else trap(op);
}

void arm64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    if (!strcmp(op, "addi")) emit32le(0x91000000u | ((uint32_t(v)&0xfffu)<<10) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "subi")) emit32le(0xd1000000u | ((uint32_t(v)&0xfffu)<<10) | (xreg(d)<<5) | xreg(d));
    else trap(op);
}

void arm64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    (void) on;
    emit32le(0xd2800000u | (1u<<(5+p)) | (15<<5) | 15);
}

void arm64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    emit32le(0xeb00001fu | (xreg(s)<<16) | (xreg(d)<<5));
    set_pred(p, !strcmp(op, "cmpeq"));
    (void) op;
}

void arm64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    emit32le(0xf100001fu | ((uint32_t(v)&0xfffu)<<10) | (xreg(d)<<5));
    set_pred(p, !strcmp(op, "cmpeqi"));
    (void) op;
}

void arm64::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), op);
}

void arm64::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
    (void)p; mov_local(d,s);
}

void arm64::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d
// ----------------------------------------------------------------------------
{
    (void)p; mov_local(d,s); (void)x;
}

void arm64::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(0xcb0003e0u | (xreg(d)<<5) | xreg(d));
}

void arm64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(0xaa2003e0u | (xreg(d)<<5) | xreg(d));
}

void arm64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(0x91000400u | (xreg(d)<<5) | xreg(d));
}

void arm64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(0xd1000400u | (xreg(d)<<5) | xreg(d));
}

void arm64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w; emit32le(0x9340fc00u | (63<<10) | (xreg(d)<<5) | xreg(d));
}

void arm64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w; emit32le(0x92400000u | (xreg(d)<<5) | xreg(d));
}

void arm64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address in local s
// ----------------------------------------------------------------------------
{
    (void)sz; emit32le(0xf9000000u | (xreg(s)<<16) | (xreg(d)<<5));
}

void arm64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    (void)sz; emit32le(0xf9400000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
}

void arm64::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address s plus offset
// ----------------------------------------------------------------------------
{
    (void)o; store_mem(d,s,sz);
}

void arm64::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address s plus offset into local d
// ----------------------------------------------------------------------------
{
    (void)o; load_mem(d,s,sz);
}

void arm64::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    load_mem(d,s,(w+7)/8); sext_local(d,w);
}

void arm64::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    load_mem_off(d,s,o,(w+7)/8); sext_local(d,w);
}

void arm64::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address into local d
// ----------------------------------------------------------------------------
{
    emit32le(0x91000000u | (xreg(s)<<5) | xreg(d));
}

void arm64::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    uint pos = gjmp(0); record_branch(b, pos);
}

void arm64::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    (void)p;(void)sense; bra_offset(b);
}

void arm64::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    (void)s;(void)eq; bra_offset(b);
}

void arm64::bra_signed(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    (void)s; bra_offset(b);
}

void arm64::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    decr_local(d); bra_zero(d,b,false);
}

void arm64::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record_tok_jump(t, gjmp(0));
}

void arm64::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record_tok_jump(t, gjmp(0));
}

void arm64::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    emit32le(0xd61f0000u | (xreg(s)<<5));
}

void arm64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    ijmp_local(s);
}

void arm64::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    emit32le(0xa9bf7bfdu);
}

void arm64::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    store_imm(d, value_t(cursor()));
}

void arm64::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load PC plus offset into local d
// ----------------------------------------------------------------------------
{
    store_imm(d, value_t(cursor() + size_t(b)));
}

void arm64::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    mov_local(d,s);
}

void arm64::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local d into argument slot s
// ----------------------------------------------------------------------------
{
    mov_local(s,d);
}

void arm64::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    emit32le(0xa9bf03e0u | xreg(s));
}

void arm64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    emit32le(0xa8c107e0u | xreg(d));
}

void arm64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    emit32le(0xa9bf03efu);
}

void arm64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    emit32le(0xa8c103efu);
}

void arm64::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    mov_local(d,s);
}

void arm64::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    mov_local(d,s);
}

void arm64::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    emit32le(0xd65f03c0u);
}

void arm64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    (void)msg; emit32le(0xd4200000u);
}

void arm64::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    (void)fn; trap(fn);
}

void arm64::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    (void)fn;(void)a; trap(fn);
}

void arm64::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    (void)fn;(void)a; trap(fn);
}



#include "arm64-emit.cpp"
