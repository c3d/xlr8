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


RECORDER(arm,            256, "32-bit ARM code generation helpers");
RECORDER(arm_ops,        256, "32-bit ARM opcode emission");
RECORDER(arm_error,       64, "32-bit ARM code generation errors");
RECORDER(arm_warning,     64, "32-bit ARM code generation warnings");


arm::arm()
// ----------------------------------------------------------------------------
//   Construct a 32-bit ARM code generator
// ----------------------------------------------------------------------------
    : backend(ARCH_ARM)
{
    record(arm, "arm()");
}


cstring arm::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    record(arm, "machine_defs()");
    return "__arm__\0__ARM_ARCH\0";
}


uint arm::nb_regs() const
// ----------------------------------------------------------------------------
//   Return number of bytecode registers
// ----------------------------------------------------------------------------
{
    record(arm, "nb_regs()");
    return 25;
}
uint arm::ptr_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    record(arm, "ptr_size()");
    return 4;
}

uint arm::reg_iret() const
// ----------------------------------------------------------------------------
//   Return integer return-value register index
// ----------------------------------------------------------------------------
{
    record(arm, "reg_iret()");
    return 0;
}

void arm::emit32le(uint32_t insn)
// ----------------------------------------------------------------------------
//   Emit a 32-bit ARM instruction word
// ----------------------------------------------------------------------------
{
    record(arm, "emit32le(%lld)", insn);
    emit32(insn);
}


void arm::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    record(arm, "load_local(%u, %u)", n, r);
    if (n < ARM_SHADOW_COUNT)
    {
        if (r != rreg(n))
            emit32le(mov_rr(r, rreg(n)));
    }
    else
    {
        emit32le(ldr_off(r, HW_FRAME, slot_off(n)));
    }
}


void arm::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local n
// ----------------------------------------------------------------------------
{
    record(arm, "store_local(%u, %u)", r, n);
    if (n < ARM_SHADOW_COUNT)
    {
        if (r != rreg(n))
            emit32le(mov_rr(rreg(n), r));
        // Write-through like arm64/riscv: natives read rt.frame, not shadows.
        emit32le(str_off(rreg(n), HW_FRAME, slot_off(n)));
        emit32le(movw(HW_IP, 0));
        emit32le(str_off(HW_IP, HW_FRAME, slot_off(n) + 4));
        touch_local(n);
    }
    else
    {
        emit32le(str_off(r, HW_FRAME, slot_off(n)));
        emit32le(movw(HW_IP, 0));
        emit32le(str_off(HW_IP, HW_FRAME, slot_off(n) + 4));
    }
}


void arm::load_imm32(uint r, uint32_t v)
// ----------------------------------------------------------------------------
//   Load a 32-bit constant into ARM register r
// ----------------------------------------------------------------------------
{
    record(arm, "load_imm32(%u, %lld)", r, v);
    emit32le(movw(r, v & 0xFFFFu));
    if (v > 0xFFFFu)
        emit32le(movt(r, v >> 16));
}


void arm::call_abs(void *fn)
// ----------------------------------------------------------------------------
//   Emit an absolute call to fn using r12 as scratch
// ----------------------------------------------------------------------------
{
    record(arm, "call_abs(%p)", fn);
    load_imm32(HW_IP, uint32_t(uintptr_t(fn)));
    emit32le(blx(HW_IP));
}


void arm::touch_local(byte d)
// ----------------------------------------------------------------------------
//   Note that bytecode local d was modified in a hardware register
// ----------------------------------------------------------------------------
{
    record(arm, "touch_local(%u)", d);
    if (d <= 1)
        spill_arg_regs = true;
}


void arm::spill_frame(void)
// ----------------------------------------------------------------------------
//   Store live bytecode locals in r0-r7 into the runtime frame at r11
// ----------------------------------------------------------------------------
{
    record(arm, "spill_frame()");
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
        emit32le(str_off(rreg(byte(r)), HW_FRAME, slot_off(byte(r))));
        emit32le(movw(HW_IP, 0));
        emit32le(str_off(HW_IP, HW_FRAME, slot_off(byte(r)) + 4));
    }
}


void arm::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload r11 from r0 after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    record(arm, "reload_frame_reg()");
    emit32le(mov_rr(HW_FRAME, 0));
}


void arm::reload_shadows(void)
// ----------------------------------------------------------------------------
//   Reload shadowed bytecode locals from the runtime frame after a syscall
// ----------------------------------------------------------------------------
{
    record(arm, "reload_shadows()");
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > ARM_SHADOW_COUNT)
        locals = ARM_SHADOW_COUNT;

    for (uint r = 0; r < locals; r++)
        emit32le(ldr_off(rreg(byte(r)), HW_FRAME, slot_off(byte(r))));
}


void arm::reload_flags_reg()
// ----------------------------------------------------------------------------
//   Reload r10 from runtime flags after sys or leave
// ----------------------------------------------------------------------------
{
    record(arm, "reload_flags_reg()");
    rtcall("xlr8_get_flags");
    emit32le(mov_rr(HW_FLAGS, 0));
}


uint arm::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain
// ----------------------------------------------------------------------------
{
    record(arm, "gjmp(%u)", t);
    uint pos = uint(cursor());
    emit32le(0xEA000000u | (t & 0x00FFFFFFu));
    return pos;
}


void arm::gsym_addr(uint t, uint a)
// ----------------------------------------------------------------------------
//   Patch pending jumps to address a
// ----------------------------------------------------------------------------
{
    record(arm, "gsym_addr(%u, %u)", t, a);
    section &sec = code();
    while (t)
    {
        uint n = t;
        uint32_t next = 0;
        if (n + 4 <= sec.size())
        {
            // Chain link is in the 24-bit imm field of the placeholder B.
            next = (uint32_t(sec[n]) | (uint32_t(sec[n + 1]) << 8)
                 | (uint32_t(sec[n + 2]) << 16) | (uint32_t(sec[n + 3]) << 24))
                 & 0x00FFFFFFu;
        }
        // ARM PC = insn_addr + 8 (same as patch_branch_site).
        int32_t offset = (int32_t(a) - int32_t(n) - 8) / 4;
        sec.patch32(n, b_imm(offset));
        t = next;
    }
}


void arm::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs, reload frame/flags, then shadow locals
// ----------------------------------------------------------------------------
{
    record(arm, "begin_translation()");
    // invoke_arm only briefly sets r9/r10 in a separate asm blob before a C
    // call to the JIT entry, so those values are not trustworthy here.
    // Frame lives in r11 (not r9): some ARM Linux ABIs treat r9 as PIC/sb.
    // push {r4-r11,lr} is 9 words; pad 4 bytes so SP stays 8-byte aligned
    // for AAPCS calls into the runtime / libc.
    emit32le(push_callee_lr());
    emit32le(sub_imm(13, 13, 4));
    rtcall("xlr8_get_frame");
    emit32le(mov_rr(HW_FRAME, 0));
    rtcall("xlr8_get_flags");
    emit32le(mov_rr(HW_FLAGS, 0));
    reload_shadows();
}


void arm::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(arm, "finish_translation()");
    do_ret();
}


void arm::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm, "mov_local(%u, %u)", d, s);
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
    record(arm, "swap_locals(%u, %u)", d, s);
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
    record(arm, "store_imm(%u, %lld)", d, v);
    if (d < ARM_SHADOW_COUNT)
    {
        load_imm32(rreg(d), uint32_t(v));
        emit32le(str_off(rreg(d), HW_FRAME, slot_off(d)));
        emit32le(movw(HW_IP, 0));
        emit32le(str_off(HW_IP, HW_FRAME, slot_off(d) + 4));
    }
    else
        load_imm32(HW_SCR_VAL, uint32_t(v)), store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm, "store_float(%u, %g)", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, 4);
    store_imm(d, value_t(bits));
}


void arm::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local (full 64-bit ularge_t slot)
// ----------------------------------------------------------------------------
{
    record(arm, "store_double(%u, %g)", d, v);
    uint64_t bits = 0;
    memcpy(&bits, &v, 8);
    uint32_t lo = uint32_t(bits);
    uint32_t hi = uint32_t(bits >> 32);
    // Runtime frame[] is ularge_t; keep both halves even though GPRs are 32-bit.
    uint off = slot_off(d);
    load_imm32(HW_SCR_VAL, lo);
    load_imm32(HW_SCR_ADDR, hi);
    emit32le(str_off(HW_SCR_VAL, HW_FRAME, off));
    emit32le(str_off(HW_SCR_ADDR, HW_FRAME, off + 4));
    if (d < ARM_SHADOW_COUNT)
        emit32le(mov_rr(rreg(d), HW_SCR_VAL));
    touch_local(d);
}


void arm::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm, "binop_local(%u, %u, %s)", d, s, op);
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
    else if (!strcmp(op, "mul") || !strcmp(op, "imul"))
        emit32le(mul_rrr(rd, rd, rs));
    else if (!strcmp(op, "and"))
        emit32le(and_rrr(rd, rd, rs));
    else if (!strcmp(op, "or"))
        emit32le(orr_rrr(rd, rd, rs));
    else if (!strcmp(op, "xor"))
        emit32le(eor_rrr(rd, rd, rs));
    else if (!strcmp(op, "shl"))
        emit32le(shift_rrr(rd, rd, rs, 0));
    else if (!strcmp(op, "sar"))
        emit32le(shift_rrr(rd, rd, rs, 2));
    else if (!strcmp(op, "shr") || !strcmp(op, "ushr"))
        emit32le(shift_rrr(rd, rd, rs, 1));
    else if (!strcmp(op, "idiv"))
        emit32le(sdiv_rrr(rd, rd, rs));
    else if (!strcmp(op, "div"))
        emit32le(udiv_rrr(rd, rd, rs));
    else if (!strcmp(op, "idiv_mod"))
    {
        emit32le(sdiv_rrr(HW_SCR_ADDR, rd, rs));
        emit32le(mls_rrrr(rd, HW_SCR_ADDR, rs, rd));
    }
    else if (!strcmp(op, "div_mod"))
    {
        emit32le(udiv_rrr(HW_SCR_ADDR, rd, rs));
        emit32le(mls_rrrr(rd, HW_SCR_ADDR, rs, rd));
    }
    else if (!strcmp(op, "fadd"))
    {
        emit32le(vmov_s_r(0, rd));
        emit32le(vmov_s_r(1, rs));
        emit32le(0xEE300A20u); // vadd.f32 s0, s0, s1
        emit32le(vmov_r_s(rd, 0));
    }
    else if (!strcmp(op, "fsub"))
    {
        emit32le(vmov_s_r(0, rd));
        emit32le(vmov_s_r(1, rs));
        emit32le(0xEE300A60u); // vsub.f32 s0, s0, s1
        emit32le(vmov_r_s(rd, 0));
    }
    else if (!strcmp(op, "fmul"))
    {
        emit32le(vmov_s_r(0, rd));
        emit32le(vmov_s_r(1, rs));
        emit32le(0xEE200A20u); // vmul.f32 s0, s0, s1
        emit32le(vmov_r_s(rd, 0));
    }
    else if (!strcmp(op, "fdiv"))
    {
        emit32le(vmov_s_r(0, rd));
        emit32le(vmov_s_r(1, rs));
        emit32le(0xEE800A20u); // vdiv.f32 s0, s0, s1
        emit32le(vmov_r_s(rd, 0));
    }
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
    record(arm, "binop_imm(%u, %lld, %s)", d, v, op);
    uint rd = d < ARM_SHADOW_COUNT ? rreg(d) : HW_SCR_VAL;
    if (d >= ARM_SHADOW_COUNT)
        load_local(d, rd);
    if (!strcmp(op, "add") || !strcmp(op, "addi"))
        emit32le(add_imm(rd, rd, uint32_t(v)));
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
        emit32le(sub_imm(rd, rd, uint32_t(v)));
    else if (!strcmp(op, "imul")
          || !strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor")
          || !strcmp(op, "idiv_imm") || !strcmp(op, "div_imm")
          || !strcmp(op, "idiv_mod_imm") || !strcmp(op, "div_mod_imm"))
    {
        // r8 scratch avoids clobbering rd when rd aliases HW_SCR_VAL (r0).
        load_imm32(HW_SCR_ADDR, uint32_t(v));
        if (!strcmp(op, "imul"))
            emit32le(mul_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "and"))
            emit32le(and_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "or"))
            emit32le(orr_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "xor"))
            emit32le(eor_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "idiv_imm"))
            emit32le(sdiv_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "div_imm"))
            emit32le(udiv_rrr(rd, rd, HW_SCR_ADDR));
        else if (!strcmp(op, "idiv_mod_imm") || !strcmp(op, "div_mod_imm"))
        {
            // Quotient scratch must not alias rd (r0/r2 when d is 0/2).
            uint q = (rd == HW_SCR_OFF) ? HW_SCR_VAL : HW_SCR_OFF;
            if (!strcmp(op, "idiv_mod_imm"))
                emit32le(sdiv_rrr(q, rd, HW_SCR_ADDR));
            else
                emit32le(udiv_rrr(q, rd, HW_SCR_ADDR));
            emit32le(mls_rrrr(rd, q, HW_SCR_ADDR, rd));
        }
    }
    else if (!strcmp(op, "shl_imm") || !strcmp(op, "sar_imm")
          || !strcmp(op, "shr_imm"))
    {
        uint amt = uint(v) & 31u;
        if (v >= 0 && v < 32)
        {
            uint stype = !strcmp(op, "shl_imm") ? 0u
                       : !strcmp(op, "sar_imm") ? 2u : 1u;
            emit32le(shift_imm(rd, rd, amt, stype));
        }
        else
        {
            load_imm32(HW_SCR_ADDR, uint32_t(v));
            uint stype = !strcmp(op, "shl_imm") ? 0u
                       : !strcmp(op, "sar_imm") ? 2u : 1u;
            emit32le(shift_rrr(rd, rd, HW_SCR_ADDR, stype));
        }
    }
    else
        trap(op);
    store_local(rd, d);
    touch_local(d);
}


void arm::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(arm, "set_pred(%u, %d)", p, on);
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


void arm::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p from CPSR after a prior CMP (preserves NZCV)
// ----------------------------------------------------------------------------
{
    record(arm, "set_pred_cmp(%u, %u)", p, cond);
    // AND without S keeps CMP flags for the conditional ORR below.
    load_imm32(HW_SCR_ADDR, ~(1u << p));
    emit32le(and_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_ADDR));
    load_imm32(HW_SCR_ADDR, 1u << p);
    emit32le(orr_rrr_cond(cond, HW_FLAGS, HW_FLAGS, HW_SCR_ADDR));
}


void arm::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(arm, "cmp_local(%u, %u, %u, %s)", p, d, s, op);
    load_local(d, HW_SCR_VAL);
    load_local(s, HW_SCR_OFF);
    emit32le(cmp_rr(HW_SCR_VAL, HW_SCR_OFF));
    set_pred_cmp(p, cond_from_op(op));
}


void arm::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(arm, "cmp_imm(%u, %u, %lld, %s)", p, d, v, op);
    load_local(d, HW_SCR_VAL);
    emit32le(cmp_r_imm(HW_SCR_VAL, uint32_t(v)));
    set_pred_cmp(p, cond_from_op(op));
}


void arm::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(arm, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, op);
    cmp_imm(p, d, value_t(v), op);
}


void arm::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(arm, "cond_mov_local(%u, %u, %u)", p, d, s);
    load_imm32(HW_SCR_ADDR, 1u << p);
    emit32le(ands_rrr(HW_SCR_ADDR, HW_FLAGS, HW_SCR_ADDR));
    size_t beq_pos = cursor();
    emit32le(b_cond_imm(0x0, 0));
    mov_local(d, s);
    patch_branch_site(beq_pos, cursor());
}


void arm::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(arm, "select_local(%u, %u, %u, %u)", p, d, s, x);
    load_imm32(HW_SCR_ADDR, 1u << p);
    emit32le(ands_rrr(HW_SCR_ADDR, HW_FLAGS, HW_SCR_ADDR));
    size_t beq_pos = cursor();
    emit32le(b_cond_imm(0x0, 0));
    mov_local(d, s);
    size_t b_pos = cursor();
    emit32le(b_imm(0));
    patch_branch_site(beq_pos, cursor());
    mov_local(d, x);
    patch_branch_site(b_pos, cursor());
}


void arm::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm, "neg_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(0xE2600000u | (HW_SCR_VAL << 12) | (HW_SCR_VAL << 16));
    store_local(HW_SCR_VAL, d);
}


void arm::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise not of bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm, "not_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(0xE1E00000u | (HW_SCR_VAL << 12) | HW_SCR_VAL);
    store_local(HW_SCR_VAL, d);
}


void arm::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm, "incr_local(%u)", d);
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
    record(arm, "decr_local(%u)", d);
    if (d < ARM_SHADOW_COUNT)
        emit32le(sub_imm(rreg(d), rreg(d), 1));
    else
        load_local(d, HW_SCR_VAL),
        emit32le(sub_imm(HW_SCR_VAL, HW_SCR_VAL, 1)),
        store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void arm::sext_reg(uint rd, uint rn, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register rn from w bits into rd (shared by sext / signed loads)
// ----------------------------------------------------------------------------
{
    record(arm, "sext_reg(%u, %u, %u)", rd, rn, w);
    // ILP32 ALU is 32-bit; store_local keeps the high ularge half clear.
    if (w >= 32)
    {
        if (rd != rn)
            emit32le(mov_rr(rd, rn));
        return;
    }
    uint sh = 32u - w;
    emit32le(shift_imm(rd, rn, sh, 0)); // LSL
    emit32le(shift_imm(rd, rd, sh, 2)); // ASR
}


void arm::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d from w bits
// ----------------------------------------------------------------------------
{
    record(arm, "sext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_VAL);
    sext_reg(HW_SCR_VAL, HW_SCR_VAL, w);
    store_local(HW_SCR_VAL, d);
}


void arm::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d from w bits
// ----------------------------------------------------------------------------
{
    record(arm, "zext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_VAL);
    if (w < 32)
    {
        load_imm32(HW_SCR_ADDR, (1u << w) - 1u);
        emit32le(and_rrr(HW_SCR_VAL, HW_SCR_VAL, HW_SCR_ADDR));
    }
    store_local(HW_SCR_VAL, d);
}


void arm::add_addr_off(uint addr_r, value_t o)
// ----------------------------------------------------------------------------
//   Add signed byte offset to address register (uses HW_IP as scratch)
// ----------------------------------------------------------------------------
{
    record(arm, "add_addr_off(%u, %lld)", addr_r, o);
    if (!o)
        return;
    load_imm32(HW_IP, uint32_t(int32_t(o)));
    emit32le(add_rrr(addr_r, addr_r, HW_IP));
}


void arm::emit_store_sized(uint val_r, uint addr_r, uint sz)
// ----------------------------------------------------------------------------
//   Store val_r to [addr_r] using access size sz (1/2/4; 8 uses hi in HW_IP)
// ----------------------------------------------------------------------------
{
    record(arm, "emit_store_sized(%u, %u, %u)", val_r, addr_r, sz);
    switch (sz)
    {
    case 1:  emit32le(strb_off(val_r, addr_r, 0)); break;
    case 2:  emit32le(strh_off(val_r, addr_r, 0)); break;
    case 4:  emit32le(str_off(val_r, addr_r, 0)); break;
    default: // 8: lo in val_r, hi already in HW_IP
        emit32le(str_off(val_r, addr_r, 0));
        emit32le(str_off(HW_IP, addr_r, 4));
        break;
    }
}


void arm::emit_load_sized(uint dst_r, uint addr_r, uint sz)
// ----------------------------------------------------------------------------
//   Load [addr_r] into dst_r (1/2/4 zero-extend; 8 leaves hi in HW_IP)
// ----------------------------------------------------------------------------
{
    record(arm, "emit_load_sized(%u, %u, %u)", dst_r, addr_r, sz);
    switch (sz)
    {
    case 1:  emit32le(ldrb_off(dst_r, addr_r, 0)); break;
    case 2:  emit32le(ldrh_off(dst_r, addr_r, 0)); break;
    case 4:  emit32le(ldr_off(dst_r, addr_r, 0)); break;
    default:
        emit32le(ldr_off(dst_r, addr_r, 0));
        emit32le(ldr_off(HW_IP, addr_r, 4));
        break;
    }
}


void arm::store_local64(uint lo_r, uint hi_r, byte d)
// ----------------------------------------------------------------------------
//   Write a full 64-bit ularge_t slot (do not clear the high half)
// ----------------------------------------------------------------------------
{
    record(arm, "store_local64(%u, %u, %u)", lo_r, hi_r, d);
    uint off = slot_off(d);
    emit32le(str_off(lo_r, HW_FRAME, off));
    emit32le(str_off(hi_r, HW_FRAME, off + 4));
    if (d < ARM_SHADOW_COUNT && lo_r != rreg(d))
        emit32le(mov_rr(rreg(d), lo_r));
    touch_local(d);
}


void arm::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store local s to memory addressed by local d
// ----------------------------------------------------------------------------
{
    record(arm, "store_mem(%u, %u, %u)", d, s, sz);
    store_mem_off(d, s, 0, sz);
}


void arm::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from memory addressed by local s into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_mem(%u, %u, %u)", d, s, sz);
    load_mem_off(d, s, 0, sz);
}


void arm::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store local s to memory at local d plus offset
// ----------------------------------------------------------------------------
{
    record(arm, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    // Read from frame write-through: HW_SCR_VAL aliases r0 (local 0), so
    // load_local can see a stale shadow after an intervening store_imm.
    emit32le(ldr_off(HW_SCR_ADDR, HW_FRAME, slot_off(d)));
    add_addr_off(HW_SCR_ADDR, o);
    emit32le(ldr_off(HW_SCR_VAL, HW_FRAME, slot_off(s)));
    if (sz >= 8)
        emit32le(ldr_off(HW_IP, HW_FRAME, slot_off(s) + 4));
    emit_store_sized(HW_SCR_VAL, HW_SCR_ADDR, sz);
}


void arm::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from memory at local s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    emit32le(ldr_off(HW_SCR_ADDR, HW_FRAME, slot_off(s)));
    add_addr_off(HW_SCR_ADDR, o);
    emit_load_sized(HW_SCR_VAL, HW_SCR_ADDR, sz);
    if (sz >= 8)
        store_local64(HW_SCR_VAL, HW_IP, d);
    else
        store_local(HW_SCR_VAL, d);
}


void arm::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load with sign extension into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_signext(%u, %u, %u)", d, s, w);
    uint sz = w <= 8 ? 1u : w <= 16 ? 2u : w <= 32 ? 4u : 8u;
    load_mem(d, s, sz);
    sext_local(d, w);
}


void arm::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load with sign extension and offset into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    uint sz = w <= 8 ? 1u : w <= 16 ? 2u : w <= 32 ? 4u : 8u;
    load_mem_off(d, s, o, sz);
    sext_local(d, w);
}


void arm::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute address of bytecode local slot s into local d (frame + s*8)
// ----------------------------------------------------------------------------
{
    record(arm, "frame_addr(%u, %u)", d, s);
    // `s` is the slot index (immediate), not a runtime value in local s.
    if (s == 0)
        emit32le(mov_rr(HW_SCR_ADDR, HW_FRAME));
    else
    {
        uint off = slot_off(s);
        if (off <= 0xFFu)
            emit32le(add_imm(HW_SCR_ADDR, HW_FRAME, off));
        else
        {
            load_imm32(HW_SCR_VAL, off);
            emit32le(add_rrr(HW_SCR_ADDR, HW_FRAME, HW_SCR_VAL));
        }
    }
    store_local(HW_SCR_ADDR, d);
}


void arm::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Patch a recorded branch instruction to jump to target_native
// ----------------------------------------------------------------------------
{
    record(arm, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    int32_t off = int32_t(target_native - insn_pos - 8) / 4;
    uint32_t insn = uint32_t(code()[insn_pos])
                  | (uint32_t(code()[insn_pos + 1]) << 8)
                  | (uint32_t(code()[insn_pos + 2]) << 16)
                  | (uint32_t(code()[insn_pos + 3]) << 24);
    if ((insn & 0xFF000000u) == 0xEA000000u)
        insn = b_imm(off);
    else
        insn = b_cond_imm(byte(insn >> 28), off);
    code().patch32(insn_pos, insn);
}


void arm::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(arm, "bra_offset(%lld)", b);
    uint pos = uint(cursor());
    emit32le(b_imm(0));
    record_branch(b, pos);
}


void arm::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(arm, "bra_pred(%u, %lld, %d)", p, b, sense);
    load_imm32(HW_SCR_ADDR, 1u << p);
    emit32le(ands_rrr(HW_SCR_ADDR, HW_FLAGS, HW_SCR_ADDR));
    uint pos = uint(cursor());
    emit32le(b_cond_imm(sense ? 0x1 : 0x0, 0));
    record_branch(b, pos);
}


void arm::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(arm, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_SCR_ADDR);
    emit32le(cmp_r_imm(HW_SCR_ADDR, 0));
    uint pos = uint(cursor());
    emit32le(b_cond_imm(eq ? 0x0 : 0x1, 0));
    record_branch(b, pos);
}


void arm::bra_signed(byte s, broff_t b, byte cond)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(arm, "bra_signed(%u, %lld, %u)", s, b, cond);
    load_local(s, HW_SCR_ADDR);
    emit32le(cmp_r_imm(HW_SCR_ADDR, 0));
    uint pos = uint(cursor());
    emit32le(b_cond_imm(cond, 0));
    record_branch(b, pos);
}


void arm::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(arm, "dbra_local(%u, %lld)", d, b);
    decr_local(d);
    bra_zero(d, b, false);
}


void arm::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(arm, "jmp_tok(%llu)", t);
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
}


void arm::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(arm, "call_tok(%llu)", t);
    spill_frame();
    size_t adr_at = cursor();
    emit32le(add_imm(0, 15, 0));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
    int off = int(pos + 4) - int(adr_at + 8);
    if (off < 0)
        code().patch32(adr_at, sub_imm(0, 15, uint32_t(-off)));
    else
        code().patch32(adr_at, add_imm(0, 15, uint32_t(off)));
}


void arm::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to address in r12 without leaving a stale shadow in r0
// ----------------------------------------------------------------------------
{
    record(arm, "br_via_ip()");
    emit32le(mov_rr(HW_IP, 0));
    reload_shadows();
    emit32le(bx_reg(HW_IP));
}


void arm::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    record(arm, "ijmp_local(%u)", s);
    spill_frame();
    load_local(s, 0);
    rtcall("xlr8_jit_addr");
    br_via_ip();
}


void arm::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    record(arm, "icall_local(%u)", s);
    spill_frame();
    size_t adr_at = cursor();
    emit32le(add_imm(0, 15, 0));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    load_local(s, 0);
    rtcall("xlr8_jit_addr");
    emit32le(mov_rr(HW_IP, 0));
    reload_shadows();
    size_t blx_at = cursor();
    emit32le(blx(HW_IP));
    int off = int(blx_at + 4) - int(adr_at + 8);
    if (off < 0)
        code().patch32(adr_at, sub_imm(0, 15, uint32_t(-off)));
    else
        code().patch32(adr_at, add_imm(0, 15, uint32_t(off)));
}


void arm::push_pc()
// ----------------------------------------------------------------------------
//   Push link register onto stack
// ----------------------------------------------------------------------------
{
    record(arm, "push_pc()");
    emit32le(0xE92D4000u);
}


void arm::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_pc(%u)", d);
    store_imm(d, value_t(cursor()));
}


void arm::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load PC plus offset into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_pca(%u, %lld)", d, b);
    store_imm(d, value_t(cursor() + size_t(b)));
}


void arm::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(arm, "get_arg(%u, %u)", d, s);
    mov_local(d, s);
}


void arm::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local d into argument slot s
// ----------------------------------------------------------------------------
{
    record(arm, "set_arg(%u, %u)", d, s);
    mov_local(s, d);
}


void arm::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(arm, "push_local(%u)", s);
    load_local(s, HW_SCR_VAL);
    emit32le(0xE92D0000u | (1u << HW_SCR_VAL));
}


void arm::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(arm, "pop_local(%u)", d);
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
    record(arm, "push_flags()");
    emit32le(0xE92D0400u);
}


void arm::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(arm, "pop_flags()");
    emit32le(0xE8BD0400u);
}


void arm::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(arm, "store_global(%u, %u)", d, s);
    mov_local(d, s);
}


void arm::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(arm, "load_global(%u, %u)", d, s);
    mov_local(d, s);
}


void arm::do_ret()
// ----------------------------------------------------------------------------
//   Emit return instruction
// ----------------------------------------------------------------------------
{
    record(arm, "do_ret()");
    emit32le(add_imm(13, 13, 4));
    emit32le(pop_callee_pc());
}


void arm::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(arm_warning, "trap(%s)", msg);
    (void)msg;
    emit32le(0xE7F001F0u);
}


void arm::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(arm, "rtcall(%s)", fn);
    call_abs(xlr8_runtime_fn(fn));
}


void arm::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper with token argument
// ----------------------------------------------------------------------------
{
    record(arm, "rtcall(%s, %llu)", fn, a);
    load_imm32(0, uint32_t(a));
    call_abs(xlr8_runtime_fn(fn));
}


void arm::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper with byte argument
// ----------------------------------------------------------------------------
{
    record(arm, "rtcall(%s, %u)", fn, a);
    load_imm32(0, uint32_t(a));
    call_abs(xlr8_runtime_fn(fn));
}



#include "arm-emit.cpp"

