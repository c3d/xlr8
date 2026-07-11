// ****************************************************************************
//  arm.cpp                                                       XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     32-bit ARM (AArch32) instruction helpers and opcode emitters
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

#include "arm.h"

#include "native-bridge.h"
#include "runtime.h"

#include <cstring>


arm::arm()
// ----------------------------------------------------------------------------
//   Construct a 32-bit ARM code generator
// ----------------------------------------------------------------------------
    : backend(ARCH_ARM)
{
}


cstring arm::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    return "__arm__\0__ARM_ARCH\0";
}


uint arm::nb_regs() const
// ----------------------------------------------------------------------------
//   Return number of bytecode registers
// ----------------------------------------------------------------------------
{
    return 25;
}


uint arm::ptr_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    return 4;
}


uint arm::reg_iret() const
// ----------------------------------------------------------------------------
//   Return integer return-value register index
// ----------------------------------------------------------------------------
{
    return 0;
}


void arm::emit32le(uint32_t insn)
// ----------------------------------------------------------------------------
//   Emit a 32-bit ARM instruction word
// ----------------------------------------------------------------------------
{
    emit32(insn);
}


void arm::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    if (n < ARM_SHADOW_COUNT)
    {
        if (r != rreg(n))
            emit32le(mov_rr(r, rreg(n)));
    }
    else
    {
        emit32le(ldr_off(r, HW_FRAME, uint(n) * ptr_size()));
    }
}


void arm::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local n
// ----------------------------------------------------------------------------
{
    if (n < ARM_SHADOW_COUNT)
    {
        if (r != rreg(n))
            emit32le(mov_rr(rreg(n), r));
        touch_local(n);
    }
    else
    {
        emit32le(str_off(r, HW_FRAME, uint(n) * ptr_size()));
    }
}


void arm::load_imm32(uint r, uint32_t v)
// ----------------------------------------------------------------------------
//   Load a 32-bit constant into ARM register r
// ----------------------------------------------------------------------------
{
    emit32le(movw(r, v & 0xFFFFu));
    if (v > 0xFFFFu)
        emit32le(movt(r, v >> 16));
}


void arm::call_abs(void *fn)
// ----------------------------------------------------------------------------
//   Emit an absolute call to fn using r12 as scratch
// ----------------------------------------------------------------------------
{
    load_imm32(HW_IP, uint32_t(uintptr_t(fn)));
    emit32le(blx(HW_IP));
}


void arm::touch_local(byte d)
// ----------------------------------------------------------------------------
//   Note that bytecode local d was modified in a hardware register
// ----------------------------------------------------------------------------
{
    if (d <= 1)
        spill_arg_regs = true;
}


void arm::spill_frame(void)
// ----------------------------------------------------------------------------
//   Store live bytecode locals in r0-r7 into the runtime frame at r9
// ----------------------------------------------------------------------------
{
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > ARM_SHADOW_COUNT)
        locals = ARM_SHADOW_COUNT;

    for (uint r = 0; r < locals; r++)
    {
        if (r <= 1 && !spill_arg_regs)
            continue;
        emit32le(str_off(rreg(byte(r)), HW_FRAME, r * ptr_size()));
    }
}


void arm::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload r9 from r0 after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    emit32le(mov_rr(HW_FRAME, 0));
}


void arm::reload_shadows(void)
// ----------------------------------------------------------------------------
//   Reload shadowed bytecode locals from the runtime frame after a syscall
// ----------------------------------------------------------------------------
{
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > ARM_SHADOW_COUNT)
        locals = ARM_SHADOW_COUNT;

    for (uint r = 0; r < locals; r++)
        emit32le(ldr_off(rreg(byte(r)), HW_FRAME, r * ptr_size()));
}


uint arm::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain
// ----------------------------------------------------------------------------
{
    uint pos = uint(cursor());
    emit32le(0xEA000000u | (t & 0x00FFFFFFu));
    return pos;
}


void arm::gsym_addr(uint t, uint a)
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
        sec.patch32(n - 4, 0xEA000000u | (uint32_t(offset) & 0x00FFFFFFu));
        t = next;
    }
}


void arm::begin_translation()
// ----------------------------------------------------------------------------
//   Save link register for return to native runner
// ----------------------------------------------------------------------------
{
    emit32le(mov_rr(HW_LINK, 14));
}


void arm::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    do_ret();
}


void arm::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    if (d < ARM_SHADOW_COUNT && s < ARM_SHADOW_COUNT)
        emit32le(mov_rr(rreg(d), rreg(s)));
    else
        load_local(s, HW_SCR_ADDR), store_local(HW_SCR_ADDR, d);
    touch_local(d);
}


void arm::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    store_local(HW_SCR_VAL, d);
    store_local(HW_SCR_ADDR, s);
}


void arm::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    if (d < ARM_SHADOW_COUNT)
        load_imm32(rreg(d), uint32_t(v));
    else
        load_imm32(HW_SCR_VAL, uint32_t(v)), store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    uint32_t bits = 0;
    memcpy(&bits, &v, 4);
    store_imm(d, value_t(bits));
}


void arm::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local (low 32 bits on 32-bit ARM)
// ----------------------------------------------------------------------------
{
    uint64_t bits = 0;
    memcpy(&bits, &v, 8);
    store_imm(d, value_t(bits));
}


void arm::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    uint rd = rreg(d);
    uint rs = rreg(s);
    if (d >= ARM_SHADOW_COUNT || s >= ARM_SHADOW_COUNT)
    {
        load_local(d, HW_SCR_VAL);
        load_local(s, HW_SCR_OFF);
        rd = HW_SCR_VAL;
        rs = HW_SCR_OFF;
    }
    if (!strcmp(op, "add"))
        emit32le(add_rrr(rd, rd, rs));
    else if (!strcmp(op, "sub"))
        emit32le(sub_rrr(rd, rd, rs));
    else if (!strcmp(op, "mul"))
        emit32le(mul_rrr(rd, rd, rs));
    else if (!strcmp(op, "and"))
        emit32le(and_rrr(rd, rd, rs));
    else if (!strcmp(op, "or"))
        emit32le(orr_rrr(rd, rd, rs));
    else if (!strcmp(op, "xor"))
        emit32le(eor_rrr(rd, rd, rs));
    else if (!strcmp(op, "shl"))
        emit32le(shift_rrr(rd, rd, rs, 0));
    else if (!strcmp(op, "shr"))
        emit32le(shift_rrr(rd, rd, rs, 2));
    else if (!strcmp(op, "ushr"))
        emit32le(shift_rrr(rd, rd, rs, 1));
    else
        trap(op);
    if (d >= ARM_SHADOW_COUNT)
        store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    uint rd = d < ARM_SHADOW_COUNT ? rreg(d) : HW_SCR_VAL;
    if (d >= ARM_SHADOW_COUNT)
        load_local(d, rd);
    if (!strcmp(op, "add") || !strcmp(op, "addi"))
        emit32le(add_imm(rd, rd, uint32_t(v)));
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
        emit32le(sub_imm(rd, rd, uint32_t(v)));
    else
        trap(op);
    if (d >= ARM_SHADOW_COUNT)
        store_local(rd, d);
    touch_local(d);
}


void arm::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    if (on)
    {
        load_imm32(HW_SCR_VAL, 1u << p);
        emit32le(orr_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
    else
    {
        load_imm32(HW_SCR_VAL, ~(1u << p));
        emit32le(and_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
}


void arm::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    load_local(d, HW_SCR_VAL);
    load_local(s, HW_SCR_OFF);
    emit32le(cmp_rr(HW_SCR_VAL, HW_SCR_OFF));
    set_pred(p, !strcmp(op, "cmpeq"));
    (void) op;
}


void arm::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    load_local(d, HW_SCR_VAL);
    emit32le(cmp_r_imm(HW_SCR_VAL, uint32_t(v)));
    set_pred(p, !strcmp(op, "cmpeqi"));
    (void) op;
}


void arm::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), op);
}


void arm::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
    (void)p;
    mov_local(d, s);
}


void arm::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d
// ----------------------------------------------------------------------------
{
    (void)p;
    mov_local(d, s);
    (void)x;
}


void arm::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    load_local(d, HW_SCR_VAL);
    emit32le(0xE2600000u | (HW_SCR_VAL << 12) | (HW_SCR_VAL << 16));
    store_local(HW_SCR_VAL, d);
}


void arm::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise not of bytecode local d
// ----------------------------------------------------------------------------
{
    load_local(d, HW_SCR_VAL);
    emit32le(0xE1E00000u | (HW_SCR_VAL << 12) | HW_SCR_VAL);
    store_local(HW_SCR_VAL, d);
}


void arm::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    if (d < ARM_SHADOW_COUNT)
        emit32le(add_imm(rreg(d), rreg(d), 1));
    else
        load_local(d, HW_SCR_VAL),
        emit32le(add_imm(HW_SCR_VAL, HW_SCR_VAL, 1)),
        store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    if (d < ARM_SHADOW_COUNT)
        emit32le(sub_imm(rreg(d), rreg(d), 1));
    else
        load_local(d, HW_SCR_VAL),
        emit32le(sub_imm(HW_SCR_VAL, HW_SCR_VAL, 1)),
        store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d from w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    touch_local(d);
}


void arm::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d from w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    touch_local(d);
}


void arm::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local s to memory addressed by local d
// ----------------------------------------------------------------------------
{
    (void)sz;
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    emit32le(str_off(HW_SCR_VAL, HW_SCR_ADDR, 0));
}


void arm::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from memory addressed by local s into local d
// ----------------------------------------------------------------------------
{
    (void)sz;
    load_local(s, HW_SCR_ADDR);
    emit32le(ldr_off(HW_SCR_VAL, HW_SCR_ADDR, 0));
    store_local(HW_SCR_VAL, d);
}


void arm::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store local s to memory at local d plus offset
// ----------------------------------------------------------------------------
{
    (void)sz;
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    emit32le(str_off(HW_SCR_VAL, HW_SCR_ADDR, uint32_t(o)));
}


void arm::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from memory at local s plus offset into local d
// ----------------------------------------------------------------------------
{
    (void)sz;
    load_local(s, HW_SCR_ADDR);
    emit32le(ldr_off(HW_SCR_VAL, HW_SCR_ADDR, uint32_t(o)));
    store_local(HW_SCR_VAL, d);
}


void arm::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load with sign extension into local d
// ----------------------------------------------------------------------------
{
    (void)w;
    load_mem(d, s, 1);
}


void arm::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load with sign extension and offset into local d
// ----------------------------------------------------------------------------
{
    (void)w;
    load_mem_off(d, s, o, 1);
}


void arm::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address into local d
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_OFF);
    emit32le(0xE0800080u | (HW_SCR_ADDR << 12) | (HW_FRAME << 16) | HW_SCR_OFF);
    store_local(HW_SCR_ADDR, d);
}


void arm::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    uint pos = gjmp(0);
    record_branch(b, pos);
}


void arm::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    (void)p;
    (void)sense;
    bra_offset(b);
}


void arm::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    (void)s;
    (void)eq;
    bra_offset(b);
}


void arm::bra_signed(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    (void)s;
    bra_offset(b);
}


void arm::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    decr_local(d);
    bra_zero(d, b, false);
}


void arm::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record_tok_jump(t, gjmp(0));
}


void arm::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record_tok_jump(t, gjmp(0));
}


void arm::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_VAL);
    emit32le(0xE12FFF10u | HW_SCR_VAL);
}


void arm::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_VAL);
    emit32le(blx(HW_SCR_VAL));
}


void arm::push_pc()
// ----------------------------------------------------------------------------
//   Push link register onto stack
// ----------------------------------------------------------------------------
{
    emit32le(0xE92D4000u);
}


void arm::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    store_imm(d, value_t(cursor()));
}


void arm::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load PC plus offset into local d
// ----------------------------------------------------------------------------
{
    store_imm(d, value_t(cursor() + size_t(b)));
}


void arm::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void arm::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local d into argument slot s
// ----------------------------------------------------------------------------
{
    mov_local(s, d);
}


void arm::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    load_local(s, HW_SCR_VAL);
    emit32le(0xE92D0000u | (1u << HW_SCR_VAL));
}


void arm::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    uint r = d < ARM_SHADOW_COUNT ? rreg(d) : HW_SCR_ADDR;
    emit32le(0xE8BD0000u | (1u << r));
    if (d >= ARM_SHADOW_COUNT)
        store_local(r, d);
    else
        touch_local(d);
}


void arm::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    emit32le(0xE92D0400u);
}


void arm::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    emit32le(0xE8BD0400u);
}


void arm::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void arm::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    mov_local(d, s);
}


void arm::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    emit32le(bx_reg(HW_LINK));
}


void arm::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    (void)msg;
    emit32le(0xE7F001F0u);
}


void arm::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    call_abs(xlr8_runtime_fn(fn));
}


void arm::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper with token argument
// ----------------------------------------------------------------------------
{
    load_imm32(0, uint32_t(a));
    call_abs(xlr8_runtime_fn(fn));
}


void arm::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper with byte argument
// ----------------------------------------------------------------------------
{
    load_imm32(0, uint32_t(a));
    call_abs(xlr8_runtime_fn(fn));
}



#include "arm-emit.cpp"
