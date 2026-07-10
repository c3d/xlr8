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

#include "native-bridge.h"
#include "runtime.h"

#include <cstring>


static uint xreg(byte n)
// ----------------------------------------------------------------------------
//   Map bytecode local n to shadow hardware register xn (n < LOC_SHADOW_COUNT)
// ----------------------------------------------------------------------------
{
    return n;
}


void arm64::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != n)
            // orr x<r>, xzr, x<n>  (mov alias)
            emit32le(0xaa0003e0u | (n << 16) | r);
    }
    else
    {
        // ldr x<r>, [x19, #n*8]
        emit32le(0xf9400000u | (uint(n) << 10) | (HW_FRAME << 5) | r);
    }
}


void arm64::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local n
// ----------------------------------------------------------------------------
{
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != n)
            // orr x<n>, xzr, x<r>  (mov alias)
            emit32le(0xaa0003e0u | (r << 16) | n);
        touch_local(n);
    }
    else
    {
        // str x<r>, [x19, #n*8]
        emit32le(0xf9000000u | (uint(n) << 10) | (HW_FRAME << 5) | r);
    }
}


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


void arm64::load_imm64(uint r, uint64_t v)
// ----------------------------------------------------------------------------
//   Load a 64-bit constant into AArch64 register r
// ----------------------------------------------------------------------------
{
    // movz x<r>, #imm16
    emit32le(0xd2800000u | ((uint32_t(v) & 0xffffu) << 5) | r);
    if ((v >> 16) & 0xffffu)
        // movk x<r>, #imm16, lsl #16
        emit32le(0xf2a00000u | (((v >> 16) & 0xffffu) << 5) | r);
    if ((v >> 32) & 0xffffu)
        // movk x<r>, #imm16, lsl #32
        emit32le(0xf2c00000u | (((v >> 32) & 0xffffu) << 5) | r);
    if ((v >> 48) & 0xffffu)
        // movk x<r>, #imm16, lsl #48
        emit32le(0xf2e00000u | (((v >> 48) & 0xffffu) << 5) | r);
}


void arm64::call_abs(void *fn)
// ----------------------------------------------------------------------------
//   Emit an absolute call to fn using x16 as scratch
// ----------------------------------------------------------------------------
{
    load_imm64(HW_IP, uint64_t(fn));
    // blr x16
    emit32le(0xd63f0200u);
}


void arm64::touch_local(byte d)
// ----------------------------------------------------------------------------
//   Note that bytecode local d was modified in a hardware register
// ----------------------------------------------------------------------------
{
    if (d <= 1)
        spill_arg_regs = true;
}


void arm64::spill_frame(void)
// ----------------------------------------------------------------------------
//   Store live bytecode locals in x0-x15 into the runtime frame at x19
// ----------------------------------------------------------------------------
{
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    // Only spill slots that exist in the current frame record
    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > 16)
        locals = 16;

    for (uint r = 0; r < locals; r++)
    {
        // x0/x1 become syscall arguments; only sync them once after a change
        if (r <= 1 && !spill_arg_regs)
            continue;
        // str x<r>, [x19, #r*8]
        emit32le(0xf9000000u | (r << 10) | (HW_FRAME << 5) | r);
    }
}


void arm64::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload x19 from x0 after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    // mov x19, x0
    emit32le(0xaa0003f3u);
}


uint arm64::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain
// ----------------------------------------------------------------------------
{
    uint pos = uint(cursor());
    // b #0  (placeholder branch, patched later)
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
        // b #offset
        sec.patch32(n - 4, 0x14000000u | (uint32_t(offset) & 0x03ffffffu));
        t = next;
    }
}


void arm64::begin_translation()
// ----------------------------------------------------------------------------
//   Save link register for return to native runner
// ----------------------------------------------------------------------------
{
    // mov x20, x30
    emit32le(0xaa1e03f4u);
}


void arm64::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    do_ret();
}


void arm64::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    if (d < LOC_SHADOW_COUNT && s < LOC_SHADOW_COUNT)
        // orr x<d>, xzr, x<s>  (mov alias)
        emit32le(0xaa0003e0u | (xreg(s)<<16) | xreg(d));
    else
        load_local(s, HW_SCR_VAL), store_local(HW_SCR_VAL, d);
    touch_local(d);
}

void arm64::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    store_local(HW_SCR_VAL, d);
    store_local(HW_SCR_ADDR, s);
}


void arm64::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    // movz x<d>, #imm16
    emit32le(0xd2800000u | ((uint32_t(v)&0xffffu)<<5) | xreg(d));
    touch_local(d);
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
    load_imm64(xreg(d), bits);
    touch_local(d);
}

void arm64::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    if (!strcmp(op, "add"))
        // add x<d>, x<d>, x<s>
        emit32le(0x8b000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "sub"))
        // sub x<d>, x<d>, x<s>
        emit32le(0xcb000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "mul"))
        // mul x<d>, x<d>, x<s>
        emit32le(0x9b007c00u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "and"))
        // and x<d>, x<d>, x<s>
        emit32le(0x8a000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "or"))
        // orr x<d>, x<d>, x<s>
        emit32le(0xaa000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "xor"))
        // eor x<d>, x<d>, x<s>
        emit32le(0xca000000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "shl"))
        // lsl x<d>, x<d>, x<s>
        emit32le(0x9ac02000u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "shr"))
        // asr x<d>, x<d>, x<s>
        emit32le(0x9ac02400u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "ushr"))
        // lsr x<d>, x<d>, x<s>
        emit32le(0x9ac02800u | (xreg(s)<<16) | (xreg(d)<<5) | xreg(d));
    else trap(op);
    touch_local(d);
}

void arm64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    if (!strcmp(op, "add") || !strcmp(op, "addi"))
        // add x<d>, x<d>, #imm12
        emit32le(0x91000000u | ((uint32_t(v)&0xfffu)<<10) | (xreg(d)<<5) | xreg(d));
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
        // sub x<d>, x<d>, #imm12
        emit32le(0xd1000000u | ((uint32_t(v)&0xfffu)<<10) | (xreg(d)<<5) | xreg(d));
    else trap(op);
    touch_local(d);
}

void arm64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    if (on)
    {
        load_imm64(HW_SCR_VAL, 1ULL << p);
        // orr x22, x22, x23
        emit32le(0xaa000000u | (HW_SCR_VAL << 16) | (HW_FLAGS << 5) | HW_FLAGS);
    }
    else
    {
        load_imm64(HW_SCR_VAL, ~(1ULL << p));
        // and x22, x22, x23
        emit32le(0x8a000000u | (HW_SCR_VAL << 16) | (HW_FLAGS << 5) | HW_FLAGS);
    }
}

void arm64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    // cmp x<d>, x<s>
    emit32le(0xeb00001fu | (xreg(s)<<16) | (xreg(d)<<5));
    set_pred(p, !strcmp(op, "cmpeq"));
    (void) op;
}

void arm64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    // cmp x<d>, #imm12
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
    // neg x<d>, x<d>
    emit32le(0xcb0003e0u | (xreg(d)<<5) | xreg(d));
    touch_local(d);
}

void arm64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    // orn x<d>, xzr, x<d>
    emit32le(0xaa2003e0u | (xreg(d)<<16) | xreg(d));
    touch_local(d);
}

void arm64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    // add x<d>, x<d>, #1
    emit32le(0x91000400u | (xreg(d)<<5) | xreg(d));
    touch_local(d);
}

void arm64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    // sub x<d>, x<d>, #1
    emit32le(0xd1000400u | (xreg(d)<<5) | xreg(d));
    touch_local(d);
}

void arm64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    // sbfm x<d>, x<d>, #0, #63
    emit32le(0x9340fc00u | (63<<10) | (xreg(d)<<5) | xreg(d));
    touch_local(d);
}

void arm64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    // ubfm x<d>, x<d>, #0, #63
    emit32le(0x92400000u | (xreg(d)<<5) | xreg(d));
    touch_local(d);
}

void arm64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address in local s
// ----------------------------------------------------------------------------
{
    (void)sz;
    // str x<d>, [x<s>]
    emit32le(0xf9000000u | (xreg(s)<<5) | xreg(d));
}

void arm64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    (void)sz;
    // ldr x<d>, [x<s>]
    emit32le(0xf9400000u | (xreg(s)<<5) | xreg(d));
    touch_local(d);
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
    // add x<d>, x19, x<s>, lsl #3
    emit32le(0x8b000000u | (3u<<10) | (xreg(s)<<16) | (HW_FRAME<<5) | xreg(d));
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
    // br x<s>
    emit32le(0xd61f0000u | (xreg(s)<<5));
}

void arm64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    // blr x<s>
    emit32le(0xd63f0000u | (xreg(s)<<5));
}

void arm64::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    // stp x29, x30, [sp, #-16]!
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
    // stp x<s>, xzr, [sp, #-16]!
    emit32le(0xa9bf03e0u | xreg(s));
}

void arm64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    // ldp x<d>, xzr, [sp], #16
    emit32le(0xa8c107e0u | xreg(d));
    touch_local(d);
}

void arm64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    // stp x22, xzr, [sp, #-16]!
    emit32le(0xa9bf03e0u | HW_FLAGS);
}

void arm64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    // ldp x22, xzr, [sp], #16
    emit32le(0xa8c103e0u | HW_FLAGS);
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
    // mov x30, x20
    emit32le(0xaa1403feu);
    // ret
    emit32le(0xd65f03c0u);
}

void arm64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    (void)msg;
    // brk #0
    emit32le(0xd4200000u);
}

void arm64::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    call_abs(xlr8_runtime_fn(fn));
}

void arm64::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    load_imm64(0, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}

void arm64::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    load_imm64(0, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}



#include "arm64-emit.cpp"
