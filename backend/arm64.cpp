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


void arm64::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != n)
            emit32le(mov_rr(r, n));
    }
    else
    {
        emit32le(ldr_off(r, HW_FRAME, uint(n)));
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
            emit32le(mov_rr(n, r));
        touch_local(n);
    }
    else
    {
        emit32le(str_off(r, HW_FRAME, uint(n)));
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
    emit32le(movz(r, uint32_t(v) & 0xffffu));
    if ((v >> 16) & 0xffffu)
        emit32le(movk(r, uint32_t(v >> 16), 16));
    if ((v >> 32) & 0xffffu)
        emit32le(movk(r, uint32_t(v >> 32), 32));
    if ((v >> 48) & 0xffffu)
        emit32le(movk(r, uint32_t(v >> 48), 48));
}


void arm64::call_abs(void *fn)
// ----------------------------------------------------------------------------
//   Emit an absolute call to fn using x16 as scratch
// ----------------------------------------------------------------------------
{
    load_imm64(HW_IP, uint64_t(fn));
    emit32le(blr(HW_IP));
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
        emit32le(str_off(xreg(byte(r)), HW_FRAME, r));
    }
}


void arm64::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload x19 from x0 after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    emit32le(reload_frame());
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
        sec.patch32(n - 4, b_imm(offset));
        t = next;
    }
}


void arm64::begin_translation()
// ----------------------------------------------------------------------------
//   Save link register for return to native runner
// ----------------------------------------------------------------------------
{
    emit32le(mov_lr_save());
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
        emit32le(mov_rr(xreg(d), xreg(s)));
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
    emit32le(movz(xreg(d), uint32_t(v) & 0xffffu));
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
        emit32le(add_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "sub"))
        emit32le(sub_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "mul"))
        emit32le(mul_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "and"))
        emit32le(and_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "or"))
        emit32le(orr_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "xor"))
        emit32le(eor_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "shl"))
        emit32le(lsl_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "shr"))
        emit32le(asr_rrr(xreg(d), xreg(d), xreg(s)));
    else if (!strcmp(op, "ushr"))
        emit32le(lsr_rrr(xreg(d), xreg(d), xreg(s)));
    else trap(op);
    touch_local(d);
}

void arm64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    if (!strcmp(op, "add") || !strcmp(op, "addi"))
        emit32le(add_imm(xreg(d), xreg(d), uint32_t(v)));
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
        emit32le(sub_imm(xreg(d), xreg(d), uint32_t(v)));
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
        emit32le(orr_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
    else
    {
        load_imm64(HW_SCR_VAL, ~(1ULL << p));
        emit32le(and_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
}

void arm64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    emit32le(cmp_rr(xreg(d), xreg(s)));
    set_pred(p, !strcmp(op, "cmpeq"));
    (void) op;
}

void arm64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    emit32le(cmp_r_imm(xreg(d), uint32_t(v)));
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
    emit32le(neg_rr(xreg(d)));
    touch_local(d);
}

void arm64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(not_rr(xreg(d)));
    touch_local(d);
}

void arm64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(add_imm(xreg(d), xreg(d), 1));
    touch_local(d);
}

void arm64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    emit32le(sub_imm(xreg(d), xreg(d), 1));
    touch_local(d);
}

void arm64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    emit32le(sbfm_64(xreg(d)));
    touch_local(d);
}

void arm64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    (void)w;
    emit32le(ubfm_64(xreg(d)));
    touch_local(d);
}

void arm64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local d at address in local s
// ----------------------------------------------------------------------------
{
    (void)sz;
    emit32le(str_idx(xreg(d), xreg(s)));
}

void arm64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    (void)sz;
    emit32le(ldr_idx(xreg(d), xreg(s)));
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
    emit32le(frame_addr_rr(xreg(d), xreg(s)));
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
    emit32le(br(xreg(s)));
}

void arm64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    emit32le(blr(xreg(s)));
}

void arm64::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    emit32le(stp_fp_lr());
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
    emit32le(stp_push(xreg(s)));
}

void arm64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    emit32le(ldp_pop(xreg(d)));
    touch_local(d);
}

void arm64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    emit32le(stp_push(HW_FLAGS));
}

void arm64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    emit32le(ldp_pop(HW_FLAGS));
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
    emit32le(mov_lr_restore());
    emit32le(ret_insn());
}

void arm64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    (void)msg;
    emit32le(brk_insn());
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
