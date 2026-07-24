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

#include "leb128.h"
#include "native-bridge.h"
#include "runtime.h"

#include <cstring>


RECORDER(arm,            256, "32-bit ARM code generation helpers");
RECORDER(arm_ops,        256, "32-bit ARM opcode emission");
RECORDER(arm_error,       64, "32-bit ARM code generation errors");
RECORDER(arm_warning,     64, "32-bit ARM code generation warnings");


arm::arm(runtime &rt)
// ----------------------------------------------------------------------------
//   Construct a 32-bit ARM code generator
// ----------------------------------------------------------------------------
    : backend(rt, ARCH_ARM)
{
    record(arm, "New backend arch=%+s", arch_name(ARCH_ARM));
}



// ============================================================================
//
//   Backend standard interface
//
// ============================================================================

cstring arm::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    return "__arm__\0__ARM_ARCH\0";
}


uint arm::registers() const
// ----------------------------------------------------------------------------
//   Return number of hardware registers on this platform
// ----------------------------------------------------------------------------
{
    return HW_COUNT;
}


uint arm::pointer_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    return 4;
}


void arm::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs used by the JIT
// ----------------------------------------------------------------------------
{
    record(arm, "begin_translation");
    frame_active = false;
    backend::begin_translation();
    prologue();
}


void arm::finish_translation()
// ----------------------------------------------------------------------------
//   Restore callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(arm, "finish_translation");
    epilogue();
    backend::finish_translation();
}


void arm::patch(hwcode_t pos, hwcode_t pvalue, bool rel)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (B / BL / B.cond) to jump to target
// ----------------------------------------------------------------------------
{
    record(arm, "patch %+s pos=%llu target=%llu",
           rel ? "relative" : "absolute", pos, pvalue);

    section &sec = code();
    if (!rel)
    {
        record(arm_error, "Absolute patch not supported at %llu", pos);
        return;
    }

    int64_t byte_off = int64_t(pvalue) - int64_t(pos);
    // ARM PC reads as insn_addr + 8 in ARM mode
    int64_t pc_rel = byte_off - 8;
    if (pc_rel & 3)
    {
        record(arm_error, "Misaligned branch patch off=%lld", byte_off);
        return;
    }
    int64_t imm24 = pc_rel / 4;
    if (imm24 < -(1LL << 23) || imm24 >= (1LL << 23))
    {
        record(arm_error, "Branch out of range off=%lld", imm24);
        return;
    }

    uint32_t insn = sec.value32le(pos);
    uint32_t top  = insn & 0x0F000000u;
    byte     cond = byte(insn >> 28);
    int32_t  arm_off = int32_t(imm24);

    if (top == 0x0A000000u)
    {
        // B / B.cond  (imm24, signed)
        insn = (uint32_t(cond) << 28) | 0x0A000000u
             | (uint32_t(arm_off) & 0x00FFFFFFu);
    }
    else if (top == 0x0B000000u)
    {
        // BL (imm24, signed)
        insn = (uint32_t(cond) << 28) | 0x0B000000u
             | (uint32_t(arm_off) & 0x00FFFFFFu);
    }
    else
    {
        record(arm_error, "Unknown branch insn 0x%08X at %llu", insn, pos);
        return;
    }

    sec.patch32le(pos, insn);
}


void arm::call(void (*target)())
// ----------------------------------------------------------------------------
//   Spill all locals then call a C function pointer
// ----------------------------------------------------------------------------
{
    size_t spilled = spill_all();
    record(arm, "call %p spilled %zu", target, spilled);
    call_ptr(target);
}


void arm::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Spill all locals then call a runtime helper
// ----------------------------------------------------------------------------
{
    size_t spilled = spill_all();
    record(arm, "syscall %p spilled %zu", target, spilled);
    call_ptr(target);
}


uint arm::relocate(void *base)
// ----------------------------------------------------------------------------
//   Relocate absolute addresses after mapping the code
// ----------------------------------------------------------------------------
{
    size_t count = relocations.size();
    record(arm, "relocate base=%p count=%zu", base, count);
    relocations.clear();
    return uint(count);
}


bool arm::move(hwreg_t to, hwreg_t from)
// ----------------------------------------------------------------------------
//   Move between registers
// ----------------------------------------------------------------------------
{
    armreg_t d = hw(to);
    armreg_t s = hw(from);
    emit32le(mov_rr(rn(d), rn(s)));
    return true;
}


bool arm::load(hwreg_t h, local_t n)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register h
// ----------------------------------------------------------------------------
{
    armreg_t r = hw(h);
    record(arm, "load %u=[%u]", r, n);

    if (!frame_active)
    {
        emit32le(movw(rn(r), 0));
        return true;
    }

    int disp = local_disp(n);
    emit_load_off(r, HW_FRAME, disp);
    return true;
}


bool arm::store(local_t n, hwreg_t h)
// ----------------------------------------------------------------------------
//   Store hardware register h into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    armreg_t r = hw(h);
    record(arm, "store [%u]=%u", n, r);
    if (!frame_active)
    {
        saved(n, h);
        return true;
    }
    int disp = local_disp(n);
    emit_store_off(HW_FRAME, r, disp);
    saved(n, h);
    return true;
}


arm::reginfo arm::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for AArch32
// ----------------------------------------------------------------------------
{
    static const reginfo arm_registers[arm::HW_COUNT] =
    {
        {  "r0",                       0 },
        {  "r1",                       0 },
        {  "r2",                       0 },
        {  "r3",                       0 },
        {  "r4", RSPEC_CALL | RSPEC_INT },
        {  "r5", RSPEC_CALL | RSPEC_INT },
        {  "r6", RSPEC_CALL | RSPEC_INT },
        {  "r7", RSPEC_CALL | RSPEC_INT },
        {  "r8", RSPEC_CALL | RSPEC_INT },
        {  "r9",                       0 },
        { "r10",                       0 }, // HW_FLAGS
        { "r11",                       0 }, // HW_FRAME
        { "r12",                       0 }, // HW_IP (scratch)
        { "r13",                       0 }, // HW_SP
        { "r14",                       0 }, // HW_LR
        { "r15",                       0 }, // PC
    };

    return reg < HW_COUNT ? arm_registers[reg] : reginfo{"INVALID", 0};
}



// ============================================================================
//
//   Internal helpers
//
// ============================================================================

void arm::emit_load_off(armreg_t rd, armreg_t rn_base, int offs)
// ----------------------------------------------------------------------------
//   Load 32-bit value from [rn_base, #offs] (signed imm12)
// ----------------------------------------------------------------------------
{
    uint d = rn(rd);
    uint n = rn(rn_base);
    int absoff = offs < 0 ? -offs : offs;
    if (absoff <= 0xFFF)
    {
        emit32le(ldr_off(d, n, offs));
    }
    else
    {
        load_imm32(HW_IP, uint32_t(int32_t(offs)));
        emit32le(add_rrr(rn(HW_IP), n, rn(HW_IP)));
        emit32le(ldr_off(d, rn(HW_IP), 0));
    }
}


void arm::emit_store_off(armreg_t rn_base, armreg_t rt_reg, int offs)
// ----------------------------------------------------------------------------
//   Store 32-bit value to [rn_base, #offs] (signed imm12)
// ----------------------------------------------------------------------------
{
    uint n = rn(rn_base);
    uint t = rn(rt_reg);
    int absoff = offs < 0 ? -offs : offs;
    if (absoff <= 0xFFF)
    {
        emit32le(str_off(t, n, offs));
    }
    else
    {
        load_imm32(HW_IP, uint32_t(int32_t(offs)));
        emit32le(add_rrr(rn(HW_IP), n, rn(HW_IP)));
        emit32le(str_off(t, rn(HW_IP), 0));
    }
}


void arm::emit_push(armreg_t r)
// ----------------------------------------------------------------------------
//   Push a single register onto the stack (stmdb sp!, {r})
// ----------------------------------------------------------------------------
{
    emit32le(push_regs(1u << rn(r)));
}


void arm::emit_pop(armreg_t r)
// ----------------------------------------------------------------------------
//   Pop a single register from the stack (ldmia sp!, {r})
// ----------------------------------------------------------------------------
{
    emit32le(pop_regs(1u << rn(r)));
}


hwcode_t arm::emit_b_rel()
// ----------------------------------------------------------------------------
//   Emit placeholder B #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(b_imm(0));
    return site;
}


hwcode_t arm::emit_bl_rel()
// ----------------------------------------------------------------------------
//   Emit placeholder BL #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(bl_imm(0));
    return site;
}


hwcode_t arm::emit_bcond_rel(byte cond)
// ----------------------------------------------------------------------------
//   Emit placeholder B.cond #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(b_cond_imm(cond, 0));
    return site;
}


void arm::load_imm32(armreg_t r, uint32_t v)
// ----------------------------------------------------------------------------
//   Load a 32-bit constant into ARM register r (MOVW + MOVT)
// ----------------------------------------------------------------------------
{
    record(arm, "load_imm32(%u, 0x%X)", r, v);
    emit32le(movw(rn(r), v & 0xFFFFu));
    if (v > 0xFFFFu)
        emit32le(movt(rn(r), v >> 16));
}


void arm::load_local(local_t loc, armreg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    record(arm, "load_local [%u]->%u", loc, r);
    load(r, loc);
}


void arm::store_local(armreg_t r, local_t loc)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm, "store_local %u->[%u]", r, loc);
    store(loc, r);
}


void arm::mov_local(local_t dst, local_t src)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm, "mov_local [%u]=[%u]", dst, src);
    hwreg_t sreg = local(src);
    hwreg_t dreg = local(dst);
    move(dreg, sreg);
}


void arm::swap_locals(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm, "swap_locals %u and %u", d, s);
    armreg_t sr = hw_local(s);
    armreg_t dr = hw_local(d);
    emit32le(mov_rr(rn(HW_IP), rn(dr)));
    emit32le(mov_rr(rn(dr), rn(sr)));
    emit32le(mov_rr(rn(sr), rn(HW_IP)));
}


void arm::store_imm(local_t d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm, "store_imm [%u]=%lld", d, v);
    armreg_t dreg = hw_local(d);
    load_imm32(dreg, uint32_t(v));
}


void arm::store_float(local_t d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm, "store_float [%u]=%g", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    armreg_t dreg = hw_local(d);
    load_imm32(dreg, bits);
}


void arm::store_double(local_t d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local (full 64-bit ularge_t slot)
// ----------------------------------------------------------------------------
{
    record(arm, "store_double [%u]=%g", d, v);
    uint64_t bits = 0;
    memcpy(&bits, &v, 8);
    uint32_t lo = uint32_t(bits);
    uint32_t hi = uint32_t(bits >> 32);
    if (!frame_active)
    {
        armreg_t dreg = hw_local(d);
        load_imm32(dreg, lo);
        return;
    }
    int disp = local_disp(d);
    load_imm32(HW_IP, lo);
    emit32le(str_off(rn(HW_IP), rn(HW_FRAME), disp));
    load_imm32(HW_IP, hi);
    emit32le(str_off(rn(HW_IP), rn(HW_FRAME), disp + 4));
}


void arm::binop_local(local_t d, local_t s, op_t op, byte fpsz)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    armreg_t sr = hw_local(s);
    record(arm, "binop_local%+s [%u]=[%u] reg %u=%u fp=%u",
           opcode_name(op), d, s, dr, sr, fpsz);

    switch (op)
    {
    case op_add:
        emit32le(add_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_sub:
        emit32le(sub_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_mul:
    case op_umul:
        emit32le(mul_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_div:
        emit32le(sdiv_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_udiv:
        emit32le(udiv_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_mod:
        emit32le(sdiv_rrr(rn(HW_IP), rn(dr), rn(sr)));
        emit32le(mls_rrrr(rn(dr), rn(HW_IP), rn(sr), rn(dr)));
        break;
    case op_umod:
        emit32le(udiv_rrr(rn(HW_IP), rn(dr), rn(sr)));
        emit32le(mls_rrrr(rn(dr), rn(HW_IP), rn(sr), rn(dr)));
        break;
    case op_and:
        emit32le(and_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_or:
        emit32le(orr_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_xor:
        emit32le(eor_rrr(rn(dr), rn(dr), rn(sr)));
        break;
    case op_shl:
        emit32le(shift_rrr(rn(dr), rn(dr), rn(sr), 0));
        break;
    case op_shr:
        emit32le(shift_rrr(rn(dr), rn(dr), rn(sr), 2));
        break;
    case op_ushr:
        emit32le(shift_rrr(rn(dr), rn(dr), rn(sr), 1));
        break;
    case op_fadd:
    case op_fsub:
    case op_fmul:
    case op_fdiv:
    case op_dadd:
    case op_dsub:
    case op_dmul:
    case op_ddiv:
        record(arm_error, "FP binop_local %u not yet implemented", op);
        trap("FP binop_local");
        break;
    default:
        record(arm_error, "binop_local called with op %u", op);
        break;
    }
}


void arm::binop_imm(local_t d, value_t v, op_t op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    record(arm, "binop_imm%+s [%u] %lld reg %u",
           opcode_name(op), d, v, dr);

    switch (op)
    {
    case op_addi:
    case op_addu:
        if (v >= 0 && v <= 255)
            emit32le(add_imm(rn(dr), rn(dr), uint(v)));
        else if (v < 0 && v >= -255)
            emit32le(sub_imm(rn(dr), rn(dr), uint(-v)));
        else
        {
            load_imm32(HW_IP, uint32_t(v));
            emit32le(add_rrr(rn(dr), rn(dr), rn(HW_IP)));
        }
        break;
    case op_subi:
    case op_subu:
        if (v >= 0 && v <= 255)
            emit32le(sub_imm(rn(dr), rn(dr), uint(v)));
        else if (v < 0 && v >= -255)
            emit32le(add_imm(rn(dr), rn(dr), uint(-v)));
        else
        {
            load_imm32(HW_IP, uint32_t(v));
            emit32le(sub_rrr(rn(dr), rn(dr), rn(HW_IP)));
        }
        break;
    case op_muli:
    case op_mulu:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(mul_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_divi:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(sdiv_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_divu:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(udiv_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_modi:
    {
        load_imm32(HW_IP, uint32_t(v));
        armreg_t tmp = HW_R0;
        if (dr == HW_R0)
            tmp = HW_R1;
        emit32le(sdiv_rrr(rn(tmp), rn(dr), rn(HW_IP)));
        emit32le(mls_rrrr(rn(dr), rn(tmp), rn(HW_IP), rn(dr)));
        break;
    }
    case op_modu:
    {
        load_imm32(HW_IP, uint32_t(v));
        armreg_t tmp = HW_R0;
        if (dr == HW_R0)
            tmp = HW_R1;
        emit32le(udiv_rrr(rn(tmp), rn(dr), rn(HW_IP)));
        emit32le(mls_rrrr(rn(dr), rn(tmp), rn(HW_IP), rn(dr)));
        break;
    }
    case op_andu:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(and_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_oru:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(orr_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_xoru:
        load_imm32(HW_IP, uint32_t(v));
        emit32le(eor_rrr(rn(dr), rn(dr), rn(HW_IP)));
        break;
    case op_shlu:
        emit32le(shift_imm(rn(dr), rn(dr), uint(v) & 31u, 0));
        break;
    case op_shru:
        emit32le(shift_imm(rn(dr), rn(dr), uint(v) & 31u, 2));
        break;
    case op_ushru:
        emit32le(shift_imm(rn(dr), rn(dr), uint(v) & 31u, 1));
        break;
    default:
        record(arm_error, "binop_imm called with op %u", op);
        break;
    }
}


void arm::cmp_local(byte p, local_t d, local_t s, byte cond)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    armreg_t sr = hw_local(s);
    record(arm, "cmp_local cc=%u p=%u d=%u r%u s=%u r%u",
           cond, p, d, dr, s, sr);
    emit32le(cmp_rr(rn(dr), rn(sr)));
    set_pred_cmp(p, cond);
}


void arm::cmp_imm(byte p, local_t d, value_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    record(arm, "cmp_imm cc=%u p=%u d=%u r%u v=%lld", cond, p, d, dr, v);
    if (v >= 0 && v <= 255)
        emit32le(cmp_r_imm(rn(dr), uint(v)));
    else
    {
        load_imm32(HW_IP, uint32_t(v));
        emit32le(cmp_rr(rn(dr), rn(HW_IP)));
    }
    set_pred_cmp(p, cond);
}


void arm::cmp_uimm(byte p, local_t d, uvalue_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate (stub — Task 3)
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), cond);
}


void arm::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(arm, "set_pred(%u, %d)", p, on);
    load_imm32(HW_IP, 1u << p);
    if (on)
        emit32le(orr_rrr(rn(HW_FLAGS), rn(HW_FLAGS), rn(HW_IP)));
    else
        emit32le(bic_rrr(rn(HW_FLAGS), rn(HW_FLAGS), rn(HW_IP)));
}


void arm::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to ARM condition code (after CMP)
// ----------------------------------------------------------------------------
{
    record(arm, "set_pred_cmp(%u, %u)", p, cond);
    load_imm32(HW_IP, 1u << p);
    emit32le(bic_rrr(rn(HW_FLAGS), rn(HW_FLAGS), rn(HW_IP)));
    emit32le(orr_rrr_cond(cond, rn(HW_FLAGS), rn(HW_FLAGS), rn(HW_IP)));
}


void arm::cond_mov_local(byte p, local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(arm, "cond_mov_local(%u, %u, %u)", p, d, s);
    load_imm32(HW_IP, 1u << p);
    emit32le(tst_rr(rn(HW_FLAGS), rn(HW_IP)));
    hwcode_t skip = emit_bcond_rel(COND_EQ);
    mov_local(d, s);
    patch(skip, cursor(), true);
}


void arm::select_local(byte p, local_t d, local_t s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(arm, "select_local(%u, %u, %u, %u)", p, d, s, x);
    load_imm32(HW_IP, 1u << p);
    emit32le(tst_rr(rn(HW_FLAGS), rn(HW_IP)));
    hwcode_t jclear = emit_bcond_rel(COND_EQ);
    mov_local(d, s);
    hwcode_t jend = emit_b_rel();
    patch(jclear, cursor(), true);
    mov_local(d, x);
    patch(jend, cursor(), true);
}


void arm::neg_local(local_t d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    armreg_t r = hw_local(d);
    emit32le(rsb_imm(rn(r), rn(r), 0));
    record(arm, "neg_local %u reg %u", d, r);
}


void arm::not_local(local_t d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    armreg_t r = hw_local(d);
    emit32le(mvn_rr(rn(r), rn(r)));
    record(arm, "not_local %u reg %u", d, r);
}


void arm::incr_local(local_t d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    armreg_t r = hw_local(d);
    emit32le(add_imm(rn(r), rn(r), 1));
    record(arm, "incr_local %u reg %u", d, r);
}


void arm::decr_local(local_t d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    armreg_t r = hw_local(d);
    emit32le(sub_imm(rn(r), rn(r), 1));
    record(arm, "decr_local %u reg %u", d, r);
}


void arm::sext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d from w bits
// ----------------------------------------------------------------------------
{
    record(arm, "sext_local%u %u", w, d);
    if (w < 32)
        sext_reg(hw_local(d), w);
}


void arm::zext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d from w bits
// ----------------------------------------------------------------------------
{
    record(arm, "zext_local%u %u", w, d);
    if (w < 32)
    {
        armreg_t r = hw_local(d);
        switch (w)
        {
        case 8:
            emit32le(uxtb(rn(r), rn(r)));
            break;
        case 16:
            emit32le(uxth(rn(r), rn(r)));
            break;
        default:
            load_imm32(HW_IP, (1u << w) - 1);
            emit32le(and_rrr(rn(r), rn(r), rn(HW_IP)));
            break;
        }
    }
}


void arm::sext_reg(armreg_t r, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register r from low w bits to 32
// ----------------------------------------------------------------------------
{
    record(arm, "sext_reg %u width %u", r, w);
    if (w == 0 || w >= 32)
        return;
    switch (w)
    {
    case 8:
        emit32le(sxtb(rn(r), rn(r)));
        break;
    case 16:
        emit32le(sxth(rn(r), rn(r)));
        break;
    default:
    {
        uint shift = 32 - w;
        emit32le(shift_imm(rn(r), rn(r), shift, 0));
        emit32le(shift_imm(rn(r), rn(r), shift, 2));
        break;
    }
    }
}


void arm::store_mem(local_t d, local_t s, size_t sz, value_t off)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d (+ offs)
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    armreg_t sr = hw_local(s);
    record(arm, "store_mem%zu [%u]=%u offs %lld", sz, d, s, off);

    armreg_t addr = dr;
    if (off)
    {
        load_imm32(HW_IP, uint32_t(int32_t(off)));
        emit32le(add_rrr(rn(HW_IP), rn(dr), rn(HW_IP)));
        addr = HW_IP;
    }

    switch (sz)
    {
    default:
        record(arm_error, "Invalid store size %zu", sz);
        // fallthrough
    case 1: emit32le(strb_off(rn(sr), rn(addr), 0)); break;
    case 2: emit32le(strh_off(rn(sr), rn(addr), 0)); break;
    case 4: emit32le(str_off(rn(sr), rn(addr), 0));  break;
    case 8:
        emit32le(str_off(rn(sr), rn(addr), 0));
        {
            armreg_t zero = (addr == HW_IP) ? HW_LR : HW_IP;
            load_imm32(zero, 0);
            emit32le(str_off(rn(zero), rn(addr), 4));
        }
        break;
    }
}


void arm::load_mem(local_t d, local_t s, size_t sz, value_t off)
// ----------------------------------------------------------------------------
//   Load from address in local s (+ offs) into local d
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    armreg_t sr = hw_local(s);
    record(arm, "load_mem%zu %u=[%u] offs %lld", sz, d, s, off);

    armreg_t addr = sr;
    if (off)
    {
        load_imm32(HW_IP, uint32_t(int32_t(off)));
        emit32le(add_rrr(rn(HW_IP), rn(sr), rn(HW_IP)));
        addr = HW_IP;
    }

    switch (sz)
    {
    default:
        record(arm_error, "Invalid load size %zu", sz);
        // fallthrough
    case 1: emit32le(ldrb_off(rn(dr), rn(addr), 0)); break;
    case 2: emit32le(ldrh_off(rn(dr), rn(addr), 0)); break;
    case 4: emit32le(ldr_off(rn(dr), rn(addr), 0));  break;
    case 8:
        // ILP32: ularge_t slots are 8 bytes but locals hold the low 32 bits.
        emit32le(ldr_off(rn(dr), rn(addr), 0));
        if (addr != HW_IP)
            emit32le(ldr_off(rn(HW_IP), rn(addr), 4));
        break;
    }
}


void arm::load_sext(local_t d, local_t s, byte w, value_t off)
// ----------------------------------------------------------------------------
//   Load from [s+offs] and sign-extend to w bits into d
// ----------------------------------------------------------------------------
{
    armreg_t dr = hw_local(d);
    armreg_t sr = hw_local(s);
    size_t sz = (w + 7) / 8;
    record(arm, "load_sext%u %u=[%u] sz %zu", w, d, s, sz);

    armreg_t addr = sr;
    if (off)
    {
        load_imm32(HW_IP, uint32_t(int32_t(off)));
        emit32le(add_rrr(rn(HW_IP), rn(sr), rn(HW_IP)));
        addr = HW_IP;
    }

    switch (sz)
    {
    default:
        record(arm_error, "Invalid load_sext width %u", w);
        // fallthrough
    case 1: emit32le(ldrsb_off(rn(dr), rn(addr), 0)); break;
    case 2: emit32le(ldrsh_off(rn(dr), rn(addr), 0)); break;
    case 4: emit32le(ldr_off(rn(dr), rn(addr), 0));   break;
    case 8: emit32le(ldr_off(rn(dr), rn(addr), 0));   break;
    }
    if (w & 7)
        sext_reg(dr, w);
}


void arm::bra_pred(byte p, byte_p target, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense (TST + B.cond)
// ----------------------------------------------------------------------------
{
    record(arm, "bra_%+s %u, %p", sense ? "true" : "false", p, target);
    load_imm32(HW_IP, 1u << p);
    emit32le(tst_rr(rn(HW_FLAGS), rn(HW_IP)));
    hwcode_t pos = emit_bcond_rel(sense ? COND_NE : COND_EQ);
    rel_branch(target, pos);
}


void arm::bra_test(local_t s, byte_p target, byte cond)
// ----------------------------------------------------------------------------
//   Emit comparison-to-zero branch on local s
// ----------------------------------------------------------------------------
{
    record(arm, "bra_test %u, %p cc=%u", s, target, cond);
    armreg_t r = hw_local(s);
    emit32le(cmp_r_imm(rn(r), 0));
    hwcode_t pos = emit_bcond_rel(cond);
    rel_branch(target, pos);
}


void arm::rel_branch(byte_p target, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative branch site to bytecode target
// ----------------------------------------------------------------------------
{
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off, true);
}


void arm::rel_function(tok_t tok, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative site to a function token
// ----------------------------------------------------------------------------
{
    intptr_t off = function(tok, site);
    if (off)
        patch(site, site + off, true);
}


void arm::push_pc()
// ----------------------------------------------------------------------------
//   Push continue address (minimal — not used by smoke path)
// ----------------------------------------------------------------------------
{
    record(arm, "push_pc()");
    trap("push_pc not yet implemented");
}


void arm::load_pc(local_t d)
// ----------------------------------------------------------------------------
//   Load current PC into local d (minimal — not used by smoke path)
// ----------------------------------------------------------------------------
{
    record(arm, "load_pc(%u)", d);
    armreg_t r = hw_local(d);
    emit32le(mov_rr(rn(r), 15));
    record(arm, "load_pc: loaded PC into r%u (approximate)", r);
}


void arm::load_pca(local_t d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset (minimal — not used by smoke path)
// ----------------------------------------------------------------------------
{
    record(arm, "load_pca(%u, %lld)", d, b);
    (void) b;
    armreg_t r = hw_local(d);
    emit32le(mov_rr(rn(r), 15));
}


void arm::prologue()
// ----------------------------------------------------------------------------
//   Save FP/LR and callee-saved registers used by the JIT
// ----------------------------------------------------------------------------
{
    record(arm, "prologue");
    // push {r4-r8, r10, fp, lr}
    uint reg_mask = (1u << rn(HW_R4))
                  | (1u << rn(HW_R5))
                  | (1u << rn(HW_R6))
                  | (1u << rn(HW_R7))
                  | (1u << rn(HW_R8))
                  | (1u << rn(HW_FLAGS))
                  | (1u << rn(HW_FRAME))
                  | (1u << rn(HW_LR));
    emit32le(push_regs(reg_mask));
    // mov fp, sp
    emit32le(mov_rr(rn(HW_FRAME), rn(HW_SP)));
}


void arm::epilogue()
// ----------------------------------------------------------------------------
//   Restore callee-saved registers, FP/LR, and return via bx lr
// ----------------------------------------------------------------------------
{
    record(arm, "epilogue");
    // pop {r4-r8, r10, fp, lr}
    uint reg_mask = (1u << rn(HW_R4))
                  | (1u << rn(HW_R5))
                  | (1u << rn(HW_R6))
                  | (1u << rn(HW_R7))
                  | (1u << rn(HW_R8))
                  | (1u << rn(HW_FLAGS))
                  | (1u << rn(HW_FRAME))
                  | (1u << rn(HW_LR));
    emit32le(pop_regs(reg_mask));
    emit32le(bx_lr());
}


void arm::call_ptr(void (*fn)())
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer loaded into r12
// ----------------------------------------------------------------------------
{
    record(arm, "call_ptr %p", fn);
    load_imm32(HW_IP, uint32_t(uintptr_t(fn)));
    emit32le(mov_rr(rn(HW_R3), rn(HW_SP)));
    emit32le(bic_imm(rn(HW_SP), rn(HW_SP), 7));
    emit32le(blx(rn(HW_IP)));
    emit32le(mov_rr(rn(HW_SP), rn(HW_R3)));
}


void arm::dispatch(local_t loc, byte_p &pc, byte cond)
// ----------------------------------------------------------------------------
//   Dispatch a branch table
// ----------------------------------------------------------------------------
{
    armreg_t ref = hw_local(loc);
    size_t ncases = leb128<size_t>(pc);
    for (size_t n = 0; n < ncases; n++)
    {
        value_t test = leb128<value_t>(pc);
        byte_p  bpc  = pc;
        broff_t offs = leb128<broff_t>(pc);
        byte_p  tgt  = bpc + offs;
        load_imm32(HW_IP, uint32_t(test));
        emit32le(cmp_rr(rn(ref), rn(HW_IP)));
        hwcode_t pos = emit_bcond_rel(cond);
        rel_branch(tgt, pos);
    }
}


void arm::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap (UDF)
// ----------------------------------------------------------------------------
{
    record(arm_warning, "trap(%s)", msg);
    (void)msg;
    emit32le(0xE7F001F0u);
}



// ============================================================================
//
//   Opcode emitters — included from arm-emit.cpp (mirrors arm64 pattern)
//
// ============================================================================

#define EMIT(op)        void arm::emit_##op(emit_args &e)

#include "arm-emit.cpp"
