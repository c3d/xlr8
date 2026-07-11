// ****************************************************************************
//  riscv64.cpp                                                  XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     RISC-V 64 instruction helpers and opcode emitters
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

#include "riscv64.h"

#include "native-bridge.h"

#include <cstring>


RECORDER(riscv64,        256, "RISC-V 64-bit code generation helpers");
RECORDER(riscv64_ops,    256, "RISC-V 64-bit opcode emission");
RECORDER(riscv64_error,   64, "RISC-V 64-bit code generation errors");
RECORDER(riscv64_warning, 64, "RISC-V 64-bit code generation warnings");


riscv64::riscv64()
// ----------------------------------------------------------------------------
//   Construct a RISC-V 64 code generator
// ----------------------------------------------------------------------------
    : backend(ARCH_RISCV64)
{
    record(riscv64, "riscv64()");
}


cstring riscv64::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    record(riscv64, "machine_defs()");
    return "__riscv\0__riscv_xlen 64\0";
}

uint riscv64::nb_regs() const
// ----------------------------------------------------------------------------
//   Return number of bytecode registers
// ----------------------------------------------------------------------------
{
    record(riscv64, "nb_regs()");
    return 19;
}
uint riscv64::ptr_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    record(riscv64, "ptr_size()");
    return 8;
}

uint riscv64::reg_iret() const
// ----------------------------------------------------------------------------
//   Return integer return-value register index
// ----------------------------------------------------------------------------
{
    record(riscv64, "reg_iret()");
    return 0;
}

void riscv64::emit32le(uint32_t insn)
// ----------------------------------------------------------------------------
//   Emit a 32-bit instruction word
// ----------------------------------------------------------------------------
{
    record(riscv64, "emit32le(%lld)", insn);
    emit32(insn);
}


uint riscv64::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain
// ----------------------------------------------------------------------------
{
    record(riscv64, "gjmp(%u)", t);
    uint pos = uint(cursor());
    emit32(t);
    return pos;
}

void riscv64::gsym_addr(uint t, uint a)
// ----------------------------------------------------------------------------
//   Patch pending jumps to address a
// ----------------------------------------------------------------------------
{
    record(riscv64, "gsym_addr(%u, %u)", t, a);
    section &sec = code();
    while (t)
    {
        uint n = t;
        uint32_t next = 0;
        if (n + 4 <= sec.size())
        {
            next = uint32_t(sec[n]) | (uint32_t(sec[n + 1]) << 8)
                 | (uint32_t(sec[n + 2]) << 16) | (uint32_t(sec[n + 3]) << 24);
        }
        int32_t offset = (int32_t(a) - int32_t(n)) / 4;
        sec.patch32(n, j_imm(offset));
        t = next;
    }
}


void riscv64::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(riscv64, "finish_translation()");
    do_ret();
}


void riscv64::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "mov_local(%u, %u)", d, s);
    emit32le(mv_rr(xreg(d), xreg(s)));
}

void riscv64::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "swap_locals(%u, %u)", d, s);
    emit32le(mv_rr(HW_SCR_VAL, xreg(s)));
    emit32le(mv_rr(xreg(s), xreg(d)));
    emit32le(mv_rr(xreg(d), HW_SCR_VAL));
}

void riscv64::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_imm(%u, %lld)", d, v);
    emit32le(addi(xreg(d), 0, uint32_t(v & 0xfff)));
}

void riscv64::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_float(%u, %g)", d, v);
    uint32_t bits = 0; memcpy(&bits, &v, 4);
    store_imm(d, value_t(bits));
}

void riscv64::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_double(%u, %g)", d, v);
    uint64_t bits = 0; memcpy(&bits, &v, 8);
    store_imm(d, value_t(bits));
}

void riscv64::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "binop_local(%u, %u, %s)", d, s, op);
    if (!strcmp(op, "add")) emit32le(add_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "sub")) emit32le(sub_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "mul")) emit32le(mul_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "and")) emit32le(and_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "or"))  emit32le(or_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "xor")) emit32le(xor_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "shl")) emit32le(sll_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "shr")) emit32le(srl_rr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "ushr")) emit32le(sra_rr(xreg(d), xreg(d), xreg(s)));
    else trap(op);
}

void riscv64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    record(riscv64, "binop_imm(%u, %lld, %s)", d, v, op);
    if (!strcmp(op, "addi")) emit32le(addi(xreg(d), xreg(d), uint32_t(v & 0xfff)));
    else if (!strcmp(op, "subi")) emit32le(subi(xreg(d), xreg(d), uint32_t(v & 0xfff)));
    else trap(op);
}

void riscv64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_pred(%u, %d)", p, on);
    (void)on; emit32le(addi(15, 0, 1u << p));
}

void riscv64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_local(%u, %u, %u, %s)", p, d, s, op);
    emit32le(sub_rr(31, xreg(d), xreg(s)));
    set_pred(p, !strcmp(op, "cmpeq"));
    (void) op;
}
void riscv64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_imm(%u, %u, %lld, %s)", p, d, v, op);
    emit32le(slti(31, xreg(d), uint32_t(v)));
    set_pred(p, !strcmp(op, "cmpeqi"));
    (void) op;
}
void riscv64::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, op);
    cmp_imm(p,d,value_t(v),op);
}

void riscv64::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
    record(riscv64, "cond_mov_local(%u, %u, %u)", p, d, s);
    (void)p; mov_local(d,s);
}

void riscv64::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d
// ----------------------------------------------------------------------------
{
    record(riscv64, "select_local(%u, %u, %u, %u)", p, d, s, x);
    (void)p; mov_local(d,s); (void)x;
}

void riscv64::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "neg_local(%u)", d);
    emit32le(neg_rr(xreg(d)));
}

void riscv64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "not_local(%u)", d);
    emit32le(not_rr(xreg(d)));
}

void riscv64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "incr_local(%u)", d);
    emit32le(addi(xreg(d), xreg(d), 1));
}

void riscv64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "decr_local(%u)", d);
    emit32le(subi(xreg(d), xreg(d), 1));
}

void riscv64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "sext_local(%u, %u)", d, w);
    (void)w; emit32le(slli(xreg(d), xreg(d), 0));
}

void riscv64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "zext_local(%u, %u)", d, w);
    (void)w; emit32le(srli(xreg(d), xreg(d), 0));
}

void riscv64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address in local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_mem(%u, %u, %u)", d, s, sz);
    (void)sz; emit32le(sw(xreg(d), xreg(s)));
}

void riscv64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_mem(%u, %u, %u)", d, s, sz);
    (void)sz; emit32le(lw(xreg(d), xreg(s)));
}

void riscv64::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address s plus offset
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    (void)o; store_mem(d,s,sz);
}

void riscv64::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    (void)o; load_mem(d,s,sz);
}

void riscv64::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext(%u, %u, %u)", d, s, w);
    load_mem(d,s,(w+7)/8); sext_local(d,w);
}

void riscv64::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_mem_off(d,s,o,(w+7)/8); sext_local(d,w);
}

void riscv64::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "frame_addr(%u, %u)", d, s);
    emit32le(addi(xreg(d), xreg(s), 0));
}

void riscv64::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_offset(%lld)", b);
    uint pos = gjmp(0); record_branch(b, pos);
}

void riscv64::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_pred(%u, %lld, %d)", p, b, sense);
    (void)p;(void)sense; bra_offset(b);
}

void riscv64::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_zero(%u, %lld, %d)", s, b, eq);
    (void)s;(void)eq; bra_offset(b);
}

void riscv64::bra_signed(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_signed(%u, %lld)", s, b);
    (void)s; bra_offset(b);
}

void riscv64::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "dbra_local(%u, %lld)", d, b);
    decr_local(d); bra_zero(d,b,false);
}

void riscv64::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(riscv64, "jmp_tok(%llu)", t);
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
}

void riscv64::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(riscv64, "call_tok(%llu)", t);
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
}

void riscv64::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "ijmp_local(%u)", s);
    emit32le(jalr(xreg(s)));
}

void riscv64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "icall_local(%u)", s);
    ijmp_local(s);
}

void riscv64::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_pc()");
    emit32le(0x00011023u);
}

void riscv64::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_pc(%u)", d);
    store_imm(d, value_t(cursor()));
}

void riscv64::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load PC plus offset into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_pca(%u, %lld)", d, b);
    store_imm(d, value_t(cursor() + size_t(b)));
}

void riscv64::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(riscv64, "get_arg(%u, %u)", d, s);
    mov_local(d,s);
}

void riscv64::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local d into argument slot s
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_arg(%u, %u)", d, s);
    mov_local(s,d);
}

void riscv64::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_local(%u)", s);
    emit32le(0x00012023u | (xreg(s) << 20));
}

void riscv64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "pop_local(%u)", d);
    emit32le(0x00013003u | (xreg(d) << 7));
}

void riscv64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_flags()");
    emit32le(0x00012023u | (15u << 20));
}

void riscv64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "pop_flags()");
    emit32le(0x00013003u | (15u << 7));
}

void riscv64::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_global(%u, %u)", d, s);
    mov_local(d,s);
}

void riscv64::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_global(%u, %u)", d, s);
    mov_local(d,s);
}

void riscv64::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    record(riscv64, "do_ret()");
    emit32le(ret_insn());
}

void riscv64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "trap(%s)", msg);
    (void)msg; emit32le(ebreak_insn());
}

void riscv64::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s)", fn);
    (void)fn; trap(fn);
}

void riscv64::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s, %llu)", fn, a);
    (void)fn;(void)a; trap(fn);
}

void riscv64::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s, %u)", fn, a);
    (void)fn;(void)a; trap(fn);
}



#include "riscv64-emit.cpp"

