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
#include "runtime.h"

#include <cstring>


RECORDER(riscv64,        256, "RISC-V 64-bit code generation helpers");
RECORDER(riscv64_ops,    256, "RISC-V 64-bit opcode emission");
RECORDER(riscv64_error,     64, "RISC-V 64-bit code generation errors");
RECORDER(riscv64_warning, 64, "RISC-V 64-bit code generation warnings");


void riscv64::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_local(%u, %u)", n, r);
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != xreg(n))
            emit32le(mv_rr(r, xreg(n)));
    }
    else
    {
        emit32le(ld_off(r, HW_FRAME, int(n) * int(ptr_size())));
    }
}


void riscv64::store_local(uint r, byte n)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local n
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_local(%u, %u)", r, n);
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != xreg(n))
            emit32le(mv_rr(xreg(n), r));
        emit32le(sd_off(xreg(n), HW_FRAME, int(n) * int(ptr_size())));
        touch_local(n);
    }
    else
    {
        emit32le(sd_off(r, HW_FRAME, int(n) * int(ptr_size())));
    }
}


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


void riscv64::load_uimm32(uint r, uint32_t v)
// ----------------------------------------------------------------------------
//   Load a 32-bit unsigned value into r with upper bits cleared
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_uimm32(%u, %u)", r, v);
    if (v < 0x800u)
    {
        emit32le(addi(r, 0, int(v)));
        return;
    }
    int32_t hi = int32_t((int64_t(int32_t(v)) + 0x800) >> 12);
    int32_t lo = int32_t(int32_t(v) - (hi << 12));
    emit32le(lui(r, uint32_t(hi) & 0xfffffu));
    if (lo)
        emit32le(addi(r, r, lo));
    if (v & 0x80000000u)
    {
        emit32le(slli(r, r, 32));
        emit32le(srli(r, r, 32));
    }
}


void riscv64::load_imm64(uint r, uint64_t v)
// ----------------------------------------------------------------------------
//   Load a 64-bit constant into RISC-V register r
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_imm64(%u, %lld)", r, v);
    int64_t s32 = int64_t(int32_t(uint32_t(v)));
    if (v == uint64_t(s32) && s32 >= -2048 && s32 < 2048)
    {
        emit32le(addi(r, 0, int(s32)));
        return;
    }
    uint32_t lo = uint32_t(v);
    uint32_t hi = uint32_t(v >> 32);
    load_uimm32(r, lo);
    if (!hi)
        return;
    load_uimm32(HW_SCR_VAL, hi);
    emit32le(slli(HW_SCR_VAL, HW_SCR_VAL, 32));
    emit32le(or_rr(r, r, HW_SCR_VAL));
}


void riscv64::call_abs(void *fn)
// ----------------------------------------------------------------------------
//   Emit an absolute call to fn using t0 as scratch; save ra on stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "call_abs(%p)", fn);
    emit32le(addi(2, 2, -16));
    emit32le(sd_off(1, 2, 0));
    load_imm64(HW_IP, uint64_t(fn));
    emit32le(jalr_ra(HW_IP));
    emit32le(ld_off(1, 2, 0));
    emit32le(addi(2, 2, 16));
}


void riscv64::touch_local(byte d)
// ----------------------------------------------------------------------------
//   Note that bytecode local d was modified in a hardware register
// ----------------------------------------------------------------------------
{
    record(riscv64, "touch_local(%u)", d);
    if (d <= 1)
        spill_arg_regs = true;
}


void riscv64::spill_frame(void)
// ----------------------------------------------------------------------------
//   Store live bytecode locals in x10-x25 into the runtime frame at s0
// ----------------------------------------------------------------------------
{
    record(riscv64, "spill_frame()");
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > 16)
        locals = 16;

    if (frame_memory_touched)
    {
        for (uint r = 0; r < locals; r++)
            emit32le(ld_off(xreg(byte(r)), HW_FRAME, int(r) * int(ptr_size())));
        frame_memory_touched = false;
        return;
    }

    for (uint r = 0; r < locals; r++)
    {
        if (r <= 1 && !spill_arg_regs)
            continue;
        emit32le(sd_off(xreg(byte(r)), HW_FRAME, int(r) * int(ptr_size())));
    }
}


void riscv64::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload s0 from a0 after xlr8_enter or xlr8_leave
// ----------------------------------------------------------------------------
{
    record(riscv64, "reload_frame_reg()");
    emit32le(reload_frame());
}


void riscv64::reload_shadows(void)
// ----------------------------------------------------------------------------
//   Reload shadowed bytecode locals from the runtime frame after a syscall
// ----------------------------------------------------------------------------
{
    record(riscv64, "reload_shadows()");
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > LOC_SHADOW_COUNT)
        locals = LOC_SHADOW_COUNT;

    for (uint r = 0; r < locals; r++)
        emit32le(ld_off(xreg(byte(r)), HW_FRAME, int(r) * int(ptr_size())));
}


void riscv64::reload_flags_reg()
// ----------------------------------------------------------------------------
//   Reload s1 from runtime flags after xlr8_leave
// ----------------------------------------------------------------------------
{
    record(riscv64, "reload_flags_reg()");
    rtcall("xlr8_get_flags");
    emit32le(mv_rr(HW_FLAGS, 10));
}


uint riscv64::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain (placeholder holds previous head)
// ----------------------------------------------------------------------------
{
    record(riscv64, "gjmp(%u)", t);
    uint pos = uint(cursor());
    emit32le(j_imm(0));
    gjmp_chain[pos] = t;
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
        uint next = 0;
        auto it = gjmp_chain.find(n);
        if (it != gjmp_chain.end())
            next = it->second;
        int32_t offset = int32_t(a) - int32_t(n);
        record(riscv64, "gsym_addr patch site=%u next=%u offset=%d", n, next, offset);
        sec.patch32(n, j_imm(offset));
        t = next;
    }
}


void riscv64::gsyscall(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    record(riscv64, "gsyscall(%p)", target);
    call_abs((void *) target);
}


void riscv64::begin_translation()
// ----------------------------------------------------------------------------
//   Prime frame/flags from the runtime, then reload shadow locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "begin_translation()");
    frame_memory_touched = false;
    // Match arm/x86: do not rely solely on invoke-primed s0/s1.  Helpers and
    // syscalls update rt.frame in C; re-read so spills/reloads hit the live
    // frame (write_token can succeed while write_integer reads a stale local).
    rtcall("xlr8_get_frame");
    emit32le(reload_frame());
    rtcall("xlr8_get_flags");
    emit32le(mv_rr(HW_FLAGS, 10));
    reload_shadows();
}


void riscv64::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(riscv64, "finish_translation()");
    do_ret();
}


void riscv64::patch_pc_rel(size_t auipc_at, uint rd, size_t target)
// ----------------------------------------------------------------------------
//   Patch auipc+addi pair at auipc_at to form address of target
// ----------------------------------------------------------------------------
{
    pc_rel_fixups.push_back({ auipc_at, rd, target });
    int off = int(target) - int(auipc_at);
    int hi = int((off + 0x800) >> 12);
    int lo = off - (hi << 12);
    code().patch32(auipc_at, auipc(rd, uint32_t(hi) & 0xfffffu));
    code().patch32(auipc_at + 4, addi(rd, rd, lo));
}


size_t riscv64::emit_return_addr_load(uint rd)
// ----------------------------------------------------------------------------
//   Emit a 16-byte load_imm64 slot for a return address patched at relocate
// ----------------------------------------------------------------------------
{
    size_t at = cursor();
    emit32le(lui(rd, 0));
    emit32le(addi(rd, rd, 0));
    emit32le(nop_insn());
    emit32le(nop_insn());
    return at;
}


void riscv64::fix_return_addr(size_t imm_at, uint rd, size_t target)
// ----------------------------------------------------------------------------
//   Record a return-address slot to patch once JIT code is relocated
// ----------------------------------------------------------------------------
{
    return_link_fixups.push_back({ imm_at, rd, target });
}


void riscv64::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "mov_local(%u, %u)", d, s);
    if (d < LOC_SHADOW_COUNT && s < LOC_SHADOW_COUNT)
    {
        emit32le(mv_rr(xreg(d), xreg(s)));
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
        load_local(s, HW_SCR_VAL), store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void riscv64::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "swap_locals(%u, %u)", d, s);
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    store_local(HW_SCR_VAL, d);
    store_local(HW_SCR_ADDR, s);
}


void riscv64::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store signed integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_imm(%u, %lld)", d, v);
    uint64_t imm = uint64_t(int64_t(int32_t(uint32_t(v))));
    if (d < LOC_SHADOW_COUNT)
        load_imm64(xreg(d), imm);
    else
        load_imm64(HW_SCR_VAL, imm),
        store_local(HW_SCR_VAL, d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    touch_local(d);
}


void riscv64::store_uimm(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Store unsigned integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_uimm(%u, %llu)", d, v);
    uint64_t imm = uint64_t(uint32_t(v));
    if (d < LOC_SHADOW_COUNT)
        load_imm64(xreg(d), imm);
    else
        load_imm64(HW_SCR_VAL, imm),
        store_local(HW_SCR_VAL, d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    touch_local(d);
}


void riscv64::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_float(%u, %g)", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, 4);
    if (d < LOC_SHADOW_COUNT)
    {
        load_imm64(xreg(d), uint64_t(bits));
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        load_imm64(HW_SCR_VAL, uint64_t(bits));
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_double(%u, %g)", d, v);
    uint64_t bits = 0;
    memcpy(&bits, &v, 8);
    if (d < LOC_SHADOW_COUNT)
    {
        load_imm64(xreg(d), bits);
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        load_imm64(HW_SCR_VAL, bits);
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "binop_local(%u, %u, %s)", d, s, op);
    load_local(d, HW_SCR_ADDR);
    load_local(s, HW_SCR_VAL);
    uint rd = HW_SCR_ADDR;
    uint rs = HW_SCR_VAL;

    if (!strcmp(op, "add"))
        emit32le(add_rr(rd, rd, rs));
    else if (!strcmp(op, "sub"))
        emit32le(sub_rr(rd, rd, rs));
    else if (!strcmp(op, "mul") || !strcmp(op, "imul"))
        emit32le(mul_rr(rd, rd, rs));
    else if (!strcmp(op, "and"))
        emit32le(and_rr(rd, rd, rs));
    else if (!strcmp(op, "or"))
        emit32le(or_rr(rd, rd, rs));
    else if (!strcmp(op, "xor"))
        emit32le(xor_rr(rd, rd, rs));
    else if (!strcmp(op, "shl"))
        emit32le(sll_rr(rd, rd, rs));
    else if (!strcmp(op, "sar") || !strcmp(op, "shr"))
        emit32le(sra_rr(rd, rd, rs));
    else if (!strcmp(op, "ushr"))
        emit32le(srl_rr(rd, rd, rs));
    else if (!strcmp(op, "idiv"))
        emit32le(div_rr(rd, rd, rs));
    else if (!strcmp(op, "div"))
        emit32le(divu_rr(rd, rd, rs));
    else if (!strcmp(op, "idiv_mod"))
    {
        emit32le(div_rr(HW_SCR_OFF, rd, rs));
        emit32le(rem_rr(rd, rd, rs));
    }
    else if (!strcmp(op, "div_mod"))
    {
        emit32le(divu_rr(HW_SCR_OFF, rd, rs));
        emit32le(remu_rr(rd, rd, rs));
    }
    else if (!strcmp(op, "fadd") || !strcmp(op, "fsub")
          || !strcmp(op, "fmul") || !strcmp(op, "fdiv"))
    {
        emit32le(fmv_w_x(0, rd));
        emit32le(fmv_w_x(1, rs));
        if (!strcmp(op, "fadd"))
            emit32le(fadd_s(0, 0, 1));
        else if (!strcmp(op, "fsub"))
            emit32le(fsub_s(0, 0, 1));
        else if (!strcmp(op, "fmul"))
            emit32le(fmul_s(0, 0, 1));
        else
            emit32le(fdiv_s(0, 0, 1));
        uint dst = d < LOC_SHADOW_COUNT ? xreg(d) : HW_SCR_VAL;
        emit32le(fmv_x_w(dst, 0));
        if (d >= LOC_SHADOW_COUNT)
            store_local(dst, d);
        else
            emit32le(sd_off(dst, HW_FRAME, int(d) * int(ptr_size())));
        touch_local(d);
        return;
    }
    else if (!strcmp(op, "dadd") || !strcmp(op, "dsub")
          || !strcmp(op, "dmul") || !strcmp(op, "ddiv"))
    {
        // Frame slots hold IEEE bit patterns; use fmv, not fcvt
        emit32le(fmv_d_x(0, rd));
        emit32le(fmv_d_x(1, rs));
        if (!strcmp(op, "dadd"))
            emit32le(fadd_d(0, 0, 1));
        else if (!strcmp(op, "dsub"))
            emit32le(fsub_d(0, 0, 1));
        else if (!strcmp(op, "dmul"))
            emit32le(fmul_d(0, 0, 1));
        else
            emit32le(fdiv_d(0, 0, 1));
        emit32le(fmv_x_d(rd, 0));
    }
    else
        trap(op);

    store_local(rd, d);
    touch_local(d);
}


void riscv64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    record(riscv64, "binop_imm(%u, %lld, %s)", d, v, op);
    load_local(d, HW_SCR_ADDR);
    uint rd = HW_SCR_ADDR;

    if (!strcmp(op, "add") || !strcmp(op, "addi"))
    {
        if (v >= -2048 && v < 2048)
            emit32le(addi(rd, rd, int(v)));
        else
        {
            load_imm64(HW_SCR_VAL, uint64_t(v));
            emit32le(add_rr(rd, rd, HW_SCR_VAL));
        }
    }
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
    {
        if (v >= -2048 && v < 2048)
            emit32le(addi(rd, rd, -int(v)));
        else
        {
            load_imm64(HW_SCR_VAL, uint64_t(v));
            emit32le(sub_rr(rd, rd, HW_SCR_VAL));
        }
    }
    else if (!strcmp(op, "imul"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(mul_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "and"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(and_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "or"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(or_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "xor"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(xor_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "shl_imm") || !strcmp(op, "sar_imm")
          || !strcmp(op, "shr_imm"))
    {
        uint amt = uint(v) & 63u;
        if (v >= 0 && v < 64)
        {
            if (!strcmp(op, "shl_imm"))
                emit32le(slli(rd, rd, amt));
            else if (!strcmp(op, "sar_imm"))
                emit32le(srai(rd, rd, amt));
            else
                emit32le(srli(rd, rd, amt));
        }
        else
        {
            load_imm64(HW_SCR_VAL, uint64_t(v));
            if (!strcmp(op, "shl_imm"))
                emit32le(sll_rr(rd, rd, HW_SCR_VAL));
            else if (!strcmp(op, "sar_imm"))
                emit32le(sra_rr(rd, rd, HW_SCR_VAL));
            else
                emit32le(srl_rr(rd, rd, HW_SCR_VAL));
        }
    }
    else if (!strcmp(op, "idiv_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(div_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "div_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(divu_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "idiv_mod_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(div_rr(HW_SCR_OFF, rd, HW_SCR_VAL));
        emit32le(rem_rr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "div_mod_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(divu_rr(HW_SCR_OFF, rd, HW_SCR_VAL));
        emit32le(remu_rr(rd, rd, HW_SCR_VAL));
    }
    else
        trap(op);

    store_local(rd, d);
    touch_local(d);
}


void riscv64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_pred(%u, %d)", p, on);
    if (on)
    {
        load_imm64(HW_SCR_VAL, 1ULL << p);
        emit32le(or_rr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
    else
    {
        load_imm64(HW_SCR_VAL, ~(1ULL << p));
        emit32le(and_rr(HW_FLAGS, HW_FLAGS, HW_SCR_VAL));
    }
}


void riscv64::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to comparison of HW_SCR_ADDR vs HW_SCR_VAL
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_pred_cmp(%u, %u)", p, cond);
    load_imm64(HW_SCR_OFF, ~(1ULL << p));
    emit32le(and_rr(HW_FLAGS, HW_FLAGS, HW_SCR_OFF));

    uint res = HW_SCR_OFF;
    switch (cond)
    {
    case 0: // EQ
        emit32le(sub_rr(res, HW_SCR_ADDR, HW_SCR_VAL));
        emit32le(sltiu(res, res, 1));
        break;
    case 1: // NE
        emit32le(sub_rr(res, HW_SCR_ADDR, HW_SCR_VAL));
        emit32le(sltu_rr(res, 0, res));
        break;
    case 11: // LT signed
        emit32le(slt_rr(res, HW_SCR_ADDR, HW_SCR_VAL));
        break;
    case 12: // GT signed
        emit32le(slt_rr(res, HW_SCR_VAL, HW_SCR_ADDR));
        break;
    case 13: // LE signed
        emit32le(slt_rr(res, HW_SCR_VAL, HW_SCR_ADDR));
        emit32le(xori(res, res, 1));
        break;
    case 10: // GE signed
        emit32le(slt_rr(res, HW_SCR_ADDR, HW_SCR_VAL));
        emit32le(xori(res, res, 1));
        break;
    case 3: // LTU
        emit32le(sltu_rr(res, HW_SCR_ADDR, HW_SCR_VAL));
        break;
    case 9: // LEU
        emit32le(sltu_rr(res, HW_SCR_VAL, HW_SCR_ADDR));
        emit32le(xori(res, res, 1));
        break;
    default:
        emit32le(mv_rr(res, 0));
        break;
    }

    size_t skip_pos = cursor();
    emit32le(beq_imm(res, 0, 0));
    load_imm64(HW_SCR_ADDR, 1ULL << p);
    emit32le(or_rr(HW_FLAGS, HW_FLAGS, HW_SCR_ADDR));
    int off = int((cursor() - skip_pos));
    code().patch32(skip_pos, beq_imm(res, 0, off));
}


void riscv64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_local(%u, %u, %u, %s)", p, d, s, op);
    load_local(d, HW_SCR_ADDR);
    load_local(s, HW_SCR_VAL);
    set_pred_cmp(p, cond_from_op(op));
}


void riscv64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_imm(%u, %u, %lld, %s)", p, d, v, op);
    load_local(d, HW_SCR_ADDR);
    load_imm64(HW_SCR_VAL, uint64_t(int64_t(int32_t(uint32_t(v)))));
    set_pred_cmp(p, cond_from_op(op));
}


void riscv64::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(riscv64, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, op);
    cmp_imm(p, d, value_t(v), op);
}


void riscv64::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(riscv64, "cond_mov_local(%u, %u, %u)", p, d, s);
    load_imm64(HW_SCR_VAL, uint64_t(1) << p);
    emit32le(and_rr(HW_SCR_VAL, HW_FLAGS, HW_SCR_VAL));
    size_t beq_pos = cursor();
    emit32le(beq_imm(HW_SCR_VAL, 0, 0));
    mov_local(d, s);
    patch_branch_site(beq_pos, cursor());
}


void riscv64::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(riscv64, "select_local(%u, %u, %u, %u)", p, d, s, x);
    load_imm64(HW_SCR_VAL, uint64_t(1) << p);
    emit32le(and_rr(HW_SCR_VAL, HW_FLAGS, HW_SCR_VAL));
    size_t beq_pos = cursor();
    emit32le(beq_imm(HW_SCR_VAL, 0, 0));
    mov_local(d, s);
    size_t j_pos = cursor();
    emit32le(j_imm(0));
    patch_branch_site(beq_pos, cursor());
    mov_local(d, x);
    patch_branch_site(j_pos, cursor());
}


void riscv64::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "neg_local(%u)", d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(neg_rr(xreg(d)));
    else
    {
        load_local(d, HW_SCR_VAL);
        emit32le(neg_rr(HW_SCR_VAL));
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "not_local(%u)", d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(not_rr(xreg(d)));
    else
    {
        load_local(d, HW_SCR_VAL);
        emit32le(not_rr(HW_SCR_VAL));
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "incr_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(addi(HW_SCR_VAL, HW_SCR_VAL, 1));
    store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void riscv64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "decr_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(addi(HW_SCR_VAL, HW_SCR_VAL, -1));
    store_local(HW_SCR_VAL, d);
    touch_local(d);
}


void riscv64::sext_reg(uint rd, uint rn, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register rn to w bits into rd (shared by sext and load paths)
// ----------------------------------------------------------------------------
{
    record(riscv64, "sext_reg(%u, %u, %u)", rd, rn, w);
    if (w < 64)
    {
        uint sh = 64 - w;
        emit32le(slli(rd, rn, sh));
        emit32le(srai(rd, rd, sh));
    }
}


void riscv64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "sext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_ADDR);
    sext_reg(HW_SCR_ADDR, HW_SCR_ADDR, w);
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}


void riscv64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "zext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_ADDR);
    if (w < 64)
    {
        load_imm64(HW_SCR_OFF, (1ull << w) - 1ull);
        emit32le(and_rr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}


void riscv64::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address of local slot s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "frame_addr(%u, %u)", d, s);
    if (s == 0)
        emit32le(mv_rr(HW_SCR_ADDR, HW_FRAME));
    else if (uint(s) * 8 <= 2047)
        emit32le(addi(HW_SCR_ADDR, HW_FRAME, int(s) * int(ptr_size())));
    else
    {
        load_imm64(HW_SCR_VAL, uint64_t(s) * 8);
        emit32le(add_rr(HW_SCR_ADDR, HW_FRAME, HW_SCR_VAL));
    }
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}


void riscv64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    switch (sz)
    {
    case 1: emit32le(sb_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    case 2: emit32le(sh_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    case 4: emit32le(sw_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    default: emit32le(sd_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    }
    frame_memory_touched = true;
}


void riscv64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_SCR_ADDR);
    if (d < LOC_SHADOW_COUNT)
    {
        switch (sz)
        {
        case 1: emit32le(lbu_idx(xreg(d), HW_SCR_ADDR)); break;
        case 2: emit32le(lhu_idx(xreg(d), HW_SCR_ADDR)); break;
        case 4: emit32le(lwu_idx(xreg(d), HW_SCR_ADDR)); break;
        default: emit32le(ld_idx(xreg(d), HW_SCR_ADDR)); break;
        }
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        switch (sz)
        {
        case 1: emit32le(lbu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 2: emit32le(lhu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 4: emit32le(lwu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        default: emit32le(ld_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        }
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d plus offset
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(d, HW_SCR_ADDR);
    if (o)
    {
        load_imm64(HW_SCR_OFF, uint64_t(o));
        emit32le(add_rr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    load_local(s, HW_SCR_VAL);
    switch (sz)
    {
    case 1: emit32le(sb_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    case 2: emit32le(sh_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    case 4: emit32le(sw_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    default: emit32le(sd_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    }
    frame_memory_touched = true;
}


void riscv64::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(s, HW_SCR_ADDR);
    if (o)
    {
        load_imm64(HW_SCR_OFF, uint64_t(o));
        emit32le(add_rr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    if (d < LOC_SHADOW_COUNT)
    {
        switch (sz)
        {
        case 1: emit32le(lbu_idx(xreg(d), HW_SCR_ADDR)); break;
        case 2: emit32le(lhu_idx(xreg(d), HW_SCR_ADDR)); break;
        case 4: emit32le(lwu_idx(xreg(d), HW_SCR_ADDR)); break;
        default: emit32le(ld_idx(xreg(d), HW_SCR_ADDR)); break;
        }
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        switch (sz)
        {
        case 1: emit32le(lbu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 2: emit32le(lhu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 4: emit32le(lwu_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        default: emit32le(ld_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        }
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void riscv64::load_signext_mem(uint rd, uint addr_rn, byte w)
// ----------------------------------------------------------------------------
//   Load from [addr_rn] and sign-extend to w bits into rd
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext_mem(%u, %u, %u)", rd, addr_rn, w);
    if (w == 8)
        emit32le(lb_idx(rd, addr_rn));
    else if (w == 16)
        emit32le(lh_idx(rd, addr_rn));
    else if (w == 32)
        emit32le(lw_idx(rd, addr_rn));
    else if (w >= 64)
        emit32le(ld_idx(rd, addr_rn));
    else
    {
        uint sz = (w + 7) / 8;
        switch (sz)
        {
        case 1: emit32le(lb_idx(rd, addr_rn)); break;
        case 2: emit32le(lh_idx(rd, addr_rn)); break;
        case 4: emit32le(lw_idx(rd, addr_rn)); break;
        default: emit32le(ld_idx(rd, addr_rn)); break;
        }
        sext_reg(rd, rd, w);
    }
}


void riscv64::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext(%u, %u, %u)", d, s, w);
    load_local(s, HW_SCR_ADDR);
    if (d < LOC_SHADOW_COUNT)
    {
        load_signext_mem(xreg(d), HW_SCR_ADDR, w);
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        load_signext_mem(HW_SCR_VAL, HW_SCR_ADDR, w);
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
    frame_memory_touched = true;
}

void riscv64::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_local(s, HW_SCR_ADDR);
    if (o)
    {
        load_imm64(HW_SCR_OFF, uint64_t(o));
        emit32le(add_rr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    if (d < LOC_SHADOW_COUNT)
    {
        load_signext_mem(xreg(d), HW_SCR_ADDR, w);
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    }
    else
    {
        load_signext_mem(HW_SCR_VAL, HW_SCR_ADDR, w);
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
    frame_memory_touched = true;
}


void riscv64::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_offset(%lld)", b);
    uint pos = uint(cursor());
    emit32le(j_imm(0));
    record_branch(b, pos);
}


void riscv64::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Patch a recorded branch instruction to jump to target_native
// ----------------------------------------------------------------------------
{
    record(riscv64, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    int32_t off = int32_t(target_native) - int32_t(insn_pos);
    uint32_t insn = uint32_t(code()[insn_pos])
                  | (uint32_t(code()[insn_pos + 1]) << 8)
                  | (uint32_t(code()[insn_pos + 2]) << 16)
                  | (uint32_t(code()[insn_pos + 3]) << 24);
    uint opcode = insn & 0x7fu;
    if (opcode == 0x6fu)
    {
        // J-type: signed 21-bit, even byte offset (±1MiB)
        if (off < -1048576 || off > 1048574 || (off & 1))
        {
            record(riscv64_error, "J-type branch out of range off=%d", off);
            return;
        }
        insn = j_imm(off);
    }
    else if (opcode == 0x63u)
    {
        // B-type: signed 13-bit, even byte offset (±4KiB)
        if (off < -4096 || off > 4094 || (off & 1))
        {
            record(riscv64_error, "B-type branch out of range off=%d", off);
            return;
        }
        uint rs1 = (insn >> 15) & 0x1fu;
        uint rs2 = (insn >> 20) & 0x1fu;
        uint f3 = (insn >> 12) & 0x7u;
        insn = enc_b_imm(off, rs1, rs2, f3);
    }
    code().patch32(insn_pos, insn);
}


void riscv64::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_SCR_VAL);
    // Far form: inverted short B over a J (±1MiB). Plain B is only ±4KiB.
    if (eq)
        emit32le(bne_imm(HW_SCR_VAL, 0, 8));
    else
        emit32le(beq_imm(HW_SCR_VAL, 0, 8));
    uint pos = uint(cursor());
    emit32le(j_imm(0));
    record_branch(b, pos);
}


void riscv64::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_pred(%u, %lld, %d)", p, b, sense);
    load_imm64(HW_SCR_VAL, uint64_t(1) << p);
    emit32le(and_rr(HW_SCR_VAL, HW_FLAGS, HW_SCR_VAL));
    if (sense)
        emit32le(beq_imm(HW_SCR_VAL, 0, 8));
    else
        emit32le(bne_imm(HW_SCR_VAL, 0, 8));
    uint pos = uint(cursor());
    emit32le(j_imm(0));
    record_branch(b, pos);
}


void riscv64::bra_signed(byte s, broff_t b, byte cond)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s vs zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_signed(%u, %lld, %u)", s, b, cond);
    load_local(s, HW_SCR_VAL);
    // Invert the condition into a short skip over a far J.
    switch (cond)
    {
    case 0xb: emit32le(bge_imm(HW_SCR_VAL, 0, 8)); break;      // LT -> skip if GE
    case 0xc: emit32le(bge_imm(0, HW_SCR_VAL, 8)); break;      // GT -> skip if LE (bge zero, val)
    case 0xd: emit32le(blt_imm(0, HW_SCR_VAL, 8)); break;      // LE -> skip if GT (blt zero, val)
    case 0xa: emit32le(blt_imm(HW_SCR_VAL, 0, 8)); break;      // GE -> skip if LT
    default:  emit32le(bne_imm(0, 0, 8)); break;
    }
    uint pos = uint(cursor());
    emit32le(j_imm(0));
    record_branch(b, pos);
}


void riscv64::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "dbra_local(%u, %lld)", d, b);
    decr_local(d);
    bra_zero(d, b, false);
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


void riscv64::patch_imm64_at(void *mem, size_t at, uint r, uint64_t v)
// ----------------------------------------------------------------------------
//   Rewrite a 16-byte load_imm64 slot at a fixed code offset
// ----------------------------------------------------------------------------
{
    auto patch32 = [&](size_t off, uint32_t insn)
    {
        code().patch32(off, insn);
        if (mem)
            memcpy((void *) ((byte_p) mem + off), &insn, sizeof(insn));
    };
    int64_t sv = int64_t(v);
    int32_t hi = int32_t((sv + 0x800) >> 12);
    int32_t lo = int32_t(sv - (int64_t(hi) << 12));
    patch32(at, lui(r, uint32_t(hi) & 0xfffffu));
    at += 4;
    if (lo)
        patch32(at, addi(r, r, lo));
    else
        patch32(at, nop_insn());
    at += 4;
    patch32(at, nop_insn());
    at += 4;
    patch32(at, nop_insn());
}


void riscv64::relocate(void *base)
// ----------------------------------------------------------------------------
//   Fix PC-relative addresses embedded in generated code after mmap
// ----------------------------------------------------------------------------
{
    record(riscv64, "relocate(%p)", base);
    section &sec = code();
    for (pc_rel_fixup &fx : pc_rel_fixups)
    {
        int off = int(fx.target) - int(fx.at);
        int hi = int((off + 0x800) >> 12);
        int lo = off - (hi << 12);
        sec.patch32(fx.at, auipc(fx.rd, uint32_t(hi) & 0xfffffu));
        sec.patch32(fx.at + 4, addi(fx.rd, fx.rd, lo));
        if (base)
        {
            uint32_t patched = auipc(fx.rd, uint32_t(hi) & 0xfffffu);
            memcpy((void *) ((byte_p) base + fx.at), &patched, sizeof(patched));
            patched = addi(fx.rd, fx.rd, lo);
            memcpy((void *) ((byte_p) base + fx.at + 4), &patched, sizeof(patched));
        }
    }
    for (return_link_fixup &fx : return_link_fixups)
    {
        uint64_t addr = uint64_t(byte_p(base) + fx.target);
        patch_imm64_at(base, fx.imm_at, fx.rd, addr);
    }
    for (relocation &fx : relocations)
    {
        byte_p bcpos = byte_p(fx.offset); // REVISIT
        auto it = bc_positions.find(bcpos);
        if (it == bc_positions.end())
        {
            auto next = bc_positions.lower_bound(bcpos);
            if (next == bc_positions.end())
                continue;
            it = next;
        }
        int off = int(it->second) - int(fx.imm_at);
        int hi = int((off + 0x800) >> 12);
        int lo = off - (hi << 12);
        uint rd = sec[fx.imm_at] >> 7 & 0x1fu;
        sec.patch32(fx.imm_at, auipc(rd, uint32_t(hi) & 0xfffffu));
        sec.patch32(fx.imm_at + 4, addi(rd, rd, lo));
        if (base)
        {
            uint32_t patched = auipc(rd, uint32_t(hi) & 0xfffffu);
            memcpy((void *) ((byte_p) base + fx.imm_at), &patched, sizeof(patched));
            patched = addi(rd, rd, lo);
            memcpy((void *) ((byte_p) base + fx.imm_at + 4), &patched, sizeof(patched));
        }
    }
}


size_t riscv64::emit_over_callees_branch()
// ----------------------------------------------------------------------------
//   Emit a forward branch over callee bodies to the epilogue
// ----------------------------------------------------------------------------
{
    record(riscv64, "emit_over_callees_branch()");
    size_t pos = cursor();
    emit32le(j_imm(0));
    return pos;
}


void riscv64::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(riscv64, "call_tok(%llu)", t);
    spill_frame();
    // Soft-stack return address must be a full 64-bit code pointer.  Absolute
    // lui+addi (return_link_fixups) only forms a 32-bit value and truncates
    // mmap'd JIT addresses on LP64 — use PC-relative auipc+addi like arm64 adr.
    size_t auipc_at = cursor();
    emit32le(auipc(10, 0));
    emit32le(addi(10, 10, 0));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
    patch_pc_rel(auipc_at, 10, pos + 4);
}


void riscv64::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to address in a0 without leaving a stale shadow in a0
// ----------------------------------------------------------------------------
{
    record(riscv64, "br_via_ip()");
    emit32le(mv_rr(HW_IP, 10));
    reload_shadows();
    emit32le(jalr(HW_IP));
}


void riscv64::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(riscv64, "ijmp_local(%u)", s);
    spill_frame();
    if (s < LOC_SHADOW_COUNT)
        emit32le(mv_rr(10, xreg(s)));
    else
        load_local(s, 10);
    rtcall("xlr8_jit_addr");
    br_via_ip();
}


void riscv64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(riscv64, "icall_local(%u)", s);
    spill_frame();
    size_t auipc_at = cursor();
    emit32le(auipc(10, 0));
    emit32le(addi(10, 10, 0));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    if (s < LOC_SHADOW_COUNT)
        emit32le(mv_rr(10, xreg(s)));
    else
        load_local(s, 10);
    rtcall("xlr8_jit_addr");
    // Soft-stack return (like call_tok / arm32); do not jalr_ra — that would
    // set hardware ra while emit_ret returns via the soft stack.
    br_via_ip();
    patch_pc_rel(auipc_at, 10, cursor());
}


void riscv64::push_pc()
// ----------------------------------------------------------------------------
//   Push continue address onto the bytecode soft stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_pc()");
    spill_frame();
    size_t auipc_at = cursor();
    emit32le(auipc(10, 0));
    emit32le(addi(10, 10, 0));
    rtcall("xlr8_push_value");
    reload_shadows();
    spill_arg_regs = true;
    // Continue address is the next insn after this push_pc sequence.
    patch_pc_rel(auipc_at, 10, cursor());
}


void riscv64::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_pc(%u)", d);
    uint r = d < LOC_SHADOW_COUNT ? xreg(d) : HW_SCR_VAL;
    emit32le(auipc(r, 0));
    if (d >= LOC_SHADOW_COUNT)
        store_local(r, d);
    else
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    touch_local(d);
}


void riscv64::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (interpreter semantics)
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_pca(%u, %lld)", d, b);
    (void) b;
    uint r = d < LOC_SHADOW_COUNT ? xreg(d) : HW_SCR_VAL;
    load_imm64(r, uint64_t(pending_branch_target));
    if (d >= LOC_SHADOW_COUNT)
        store_local(r, d);
    else
        emit32le(sd_off(xreg(d), HW_FRAME, int(d) * int(ptr_size())));
    touch_local(d);
}


void riscv64::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(riscv64, "get_arg(%u, %u)", d, s);
    spill_frame();
    load_imm64(10, uint64_t(d));
    load_imm64(11, uint64_t(s));
    rtcall("xlr8_get_arg");
    reload_shadows();
    touch_local(d);
}


void riscv64::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local s into argument slot d
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_arg(%u, %u)", d, s);
    spill_frame();
    load_imm64(10, uint64_t(d));
    load_imm64(11, uint64_t(s));
    rtcall("xlr8_set_arg");
    frame_memory_touched = true;
}


void riscv64::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_local(%u)", s);
    spill_frame();
    if (s < LOC_SHADOW_COUNT)
        emit32le(mv_rr(10, xreg(s)));
    else
        emit32le(ld_off(10, HW_FRAME, int(s) * int(ptr_size())));
    rtcall("xlr8_push_value");
    reload_shadows();
    spill_arg_regs = true;
}


void riscv64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "pop_local(%u)", d);
    rtcall("xlr8_pop_value");
    emit32le(mv_rr(HW_SCR_VAL, 10));
    // pop_value clobbers a0-a7; restore shadow locals from the frame like
    // arm64 before writing the popped value into d.
    runtime *rt = xlr8_get_runtime();
    if (rt && rt->frame)
    {
        byte locals = byte(rt->frame[-1]);
        if (!locals)
            locals = byte(rt->CALL_CLOBBERED_REGS);
        if (locals > LOC_SHADOW_COUNT)
            locals = LOC_SHADOW_COUNT;
        for (uint r = 1; r < locals; r++)
            emit32le(ld_off(xreg(byte(r)), HW_FRAME,
                            int(r) * int(ptr_size())));
    }
    if (d != 0)
        emit32le(ld_off(xreg(0), HW_FRAME, 0));
    store_local(HW_SCR_VAL, d);
}


void riscv64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_flags()");
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_push_flags_val");
    spill_arg_regs = true;
}


void riscv64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "pop_flags()");
    rtcall("xlr8_pop_flags_val");
    emit32le(mv_rr(HW_FLAGS, 10));
}


void riscv64::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_global(%u, %u)", d, s);
    spill_frame();
    load_imm64(10, uint64_t(d));
    load_imm64(11, uint64_t(s));
    rtcall("xlr8_store_global");
    reload_shadows();
}


void riscv64::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_global(%u, %u)", d, s);
    spill_frame();
    load_imm64(10, uint64_t(d));
    load_imm64(11, uint64_t(s));
    rtcall("xlr8_load_global");
    reload_shadows();
    touch_local(d);
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
    (void) msg;
    emit32le(ebreak_insn());
}


void riscv64::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s)", fn);
    call_abs(xlr8_runtime_fn(fn));
}


void riscv64::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function with token argument in a0
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s, %llu)", fn, a);
    load_imm64(10, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}


void riscv64::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function with byte argument in a0
// ----------------------------------------------------------------------------
{
    record(riscv64, "rtcall(%s, %u)", fn, a);
    load_imm64(10, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}



#include "riscv64-emit.cpp"
