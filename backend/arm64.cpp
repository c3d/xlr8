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


RECORDER(arm64,          256, "AArch64 code generation helpers");
RECORDER(arm64_ops,      256, "AArch64 opcode emission");
RECORDER(arm64_error,     64, "AArch64 code generation errors");
RECORDER(arm64_warning,   64, "AArch64 code generation warnings");


void arm64::load_local(byte n, uint r)
// ----------------------------------------------------------------------------
//   Load bytecode local n into hardware register r
// ----------------------------------------------------------------------------
{
    record(arm64, "load_local(%u, %u)", n, r);
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
    record(arm64, "store_local(%u, %u)", r, n);
    if (n < LOC_SHADOW_COUNT)
    {
        if (r != n)
            emit32le(mov_rr(n, r));
        emit32le(str_off(xreg(n), HW_FRAME, n));
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
    record(arm64, "arm64()");
}


cstring arm64::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    record(arm64, "machine_defs()");
    return "__aarch64__\0__arm64__\0";
}

uint arm64::nb_regs() const
// ----------------------------------------------------------------------------
//   Return number of bytecode registers
// ----------------------------------------------------------------------------
{
    record(arm64, "nb_regs()");
    return 28;
}

uint arm64::ptr_size() const
// ----------------------------------------------------------------------------
//   Return pointer size in bytes
// ----------------------------------------------------------------------------
{
    record(arm64, "ptr_size()");
    return 8;
}

uint arm64::reg_iret() const
// ----------------------------------------------------------------------------
//   Return integer return-value register index
// ----------------------------------------------------------------------------
{
    record(arm64, "reg_iret()");
    return 0;
}

void arm64::emit32le(uint32_t insn)
// ----------------------------------------------------------------------------
//   Emit a 32-bit instruction word
// ----------------------------------------------------------------------------
{
    record(arm64, "emit32le(%lld)", insn);
    emit32(insn);
}


void arm64::load_imm64(uint r, uint64_t v)
// ----------------------------------------------------------------------------
//   Load a 64-bit constant into AArch64 register r
// ----------------------------------------------------------------------------
{
    record(arm64, "load_imm64(%u, %lld)", r, v);
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
    record(arm64, "call_abs(%p)", fn);
    load_imm64(HW_IP, uint64_t(fn));
    emit32le(blr(HW_IP));
}


void arm64::touch_local(byte d)
// ----------------------------------------------------------------------------
//   Note that bytecode local d was modified in a hardware register
// ----------------------------------------------------------------------------
{
    record(arm64, "touch_local(%u)", d);
    if (d <= 1)
        spill_arg_regs = true;
}


void arm64::spill_frame(void)
// ----------------------------------------------------------------------------
//   Store live bytecode locals in x0-x15 into the runtime frame at x19
// ----------------------------------------------------------------------------
{
    record(arm64, "spill_frame()");
    restore_frame_ptr();
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    // Only spill slots that exist in the current frame record
    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > 16)
        locals = 16;

    // Pointer stores may update frame slots without touching shadow registers.
    // Reload shadows from frame instead of overwriting memory with stale values.
    if (frame_memory_touched)
    {
        for (uint r = 0; r < locals; r++)
            emit32le(ldr_off(xreg(byte(r)), HW_FRAME, r));
        frame_memory_touched = false;
        return;
    }

    for (uint r = 0; r < locals; r++)
    {
        // x0/x1 become syscall arguments; only sync them once after a change
        if (r <= 1 && !spill_arg_regs)
            continue;
        emit32le(str_off(xreg(byte(r)), HW_FRAME, r));
    }
}


void arm64::restore_frame_ptr()
// ----------------------------------------------------------------------------
//   Reload x19 from the runtime frame pointer (and refresh x27)
// ----------------------------------------------------------------------------
{
    record(arm64, "restore_frame_ptr()");
#if defined(__linux__) && !defined(__ANDROID__)
    rtcall("xlr8_get_frame");
    emit32le(mov_rr(HW_FRAME, 0));
    emit32le(mov_rr(HW_ANCHOR, 0));
#else
    emit32le(mov_rr(HW_FRAME, HW_ANCHOR));
#endif
}


void arm64::reload_frame_reg()
// ----------------------------------------------------------------------------
//   Reload x19 from x0 after xlr8_enter or xlr8_leave, then refresh x20
// ----------------------------------------------------------------------------
{
    record(arm64, "reload_frame_reg()");
    emit32le(reload_frame());
    emit32le(mov_rr(HW_ANCHOR, HW_FRAME));
}


void arm64::reload_shadows(void)
// ----------------------------------------------------------------------------
//   Reload shadowed bytecode locals from the runtime frame after a syscall
// ----------------------------------------------------------------------------
{
    record(arm64, "reload_shadows()");
    restore_frame_ptr();
    runtime *rt = xlr8_get_runtime();
    if (!rt || !rt->frame)
        return;

    byte locals = byte(rt->frame[-1]);
    if (!locals)
        locals = byte(rt->CALL_CLOBBERED_REGS);
    if (locals > LOC_SHADOW_COUNT)
        locals = LOC_SHADOW_COUNT;

    for (uint r = 0; r < locals; r++)
        emit32le(ldr_off(xreg(byte(r)), HW_FRAME, r));
}


void arm64::reload_flags_reg()
// ----------------------------------------------------------------------------
//   Reload x22 from runtime flags after xlr8_leave
// ----------------------------------------------------------------------------
{
    record(arm64, "reload_flags_reg()");
    rtcall("xlr8_get_flags");
    emit32le(mov_rr(HW_FLAGS, 0));
}


uint arm64::gjmp(uint t)
// ----------------------------------------------------------------------------
//   Emit jump with deferred fixup chain (placeholder holds previous head)
// ----------------------------------------------------------------------------
{
    record(arm64, "gjmp(%u)", t);
    uint pos = uint(cursor());
    emit32le(b_imm(0));
    gjmp_chain[pos] = t;
    return pos;
}

void arm64::gsym_addr(uint t, uint a)
// ----------------------------------------------------------------------------
//   Patch pending jumps to address a
// ----------------------------------------------------------------------------
{
    record(arm64, "gsym_addr(%u, %u)", t, a);
    section &sec = code();
    while (t)
    {
        uint n = t;
        uint next = 0;
        auto it = gjmp_chain.find(n);
        if (it != gjmp_chain.end())
            next = it->second;
        int32_t offset = (int32_t(a) - int32_t(n)) / 4;
        record(arm64, "gsym_addr patch site=%u next=%u offset=%d", n, next, offset);
        sec.patch32(n, b_imm(offset));
        t = next;
    }
}


void arm64::begin_translation()
// ----------------------------------------------------------------------------
//   Save invoke LR on the JIT stack, then reload shadows
// ----------------------------------------------------------------------------
{
    record(arm64, "begin_translation()");
    frame_memory_touched = false;
    emit32le(stp_push(30));
    emit32le(mov_rr(HW_ANCHOR, HW_FRAME));
    reload_shadows();
}


void arm64::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(arm64, "finish_translation()");
    do_ret();
}


void arm64::mov_local(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm64, "mov_local(%u, %u)", d, s);
    if (d < LOC_SHADOW_COUNT && s < LOC_SHADOW_COUNT)
    {
        emit32le(mov_rr(xreg(d), xreg(s)));
        emit32le(str_off(xreg(d), HW_FRAME, d));
    }
    else
        load_local(s, HW_SCR_VAL), store_local(HW_SCR_VAL, d);
    touch_local(d);
}

void arm64::swap_locals(byte d, byte s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm64, "swap_locals(%u, %u)", d, s);
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    store_local(HW_SCR_VAL, d);
    store_local(HW_SCR_ADDR, s);
}


void arm64::store_imm(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Store signed integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_imm(%u, %lld)", d, v);
    uint64_t imm = uint64_t(int64_t(int32_t(uint32_t(v))));
    if (d < LOC_SHADOW_COUNT)
        load_imm64(xreg(d), imm);
    else
        load_imm64(HW_SCR_VAL, imm),
        store_local(HW_SCR_VAL, d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(str_off(xreg(d), HW_FRAME, d));
    touch_local(d);
}

void arm64::store_uimm(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Store unsigned integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_uimm(%u, %llu)", d, v);
    uint64_t imm = uint64_t(uint32_t(v));
    if (d < LOC_SHADOW_COUNT)
        load_imm64(xreg(d), imm);
    else
        load_imm64(HW_SCR_VAL, imm),
        store_local(HW_SCR_VAL, d);
    if (d < LOC_SHADOW_COUNT)
        emit32le(str_off(xreg(d), HW_FRAME, d));
    touch_local(d);
}

void arm64::store_float(byte d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_float(%u, %g)", d, v);
    uint32_t bits = 0;
    memcpy(&bits, &v, 4);
    if (d < LOC_SHADOW_COUNT)
    {
        load_imm64(xreg(d), uint64_t(bits));
        emit32le(str_off(xreg(d), HW_FRAME, d));
    }
    else
    {
        load_imm64(HW_SCR_VAL, uint64_t(bits));
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}

void arm64::store_float_local(byte d, uint wn)
// ----------------------------------------------------------------------------
//   Store a 32-bit float from Wn into bytecode local d and its shadow
// ----------------------------------------------------------------------------
{
    record(arm64, "store_float_local(%u, %u)", d, wn);
    if (d < LOC_SHADOW_COUNT)
    {
        emit32le(uxtw_x(xreg(d), wn));
        emit32le(str_off(xreg(d), HW_FRAME, d));
        touch_local(d);
    }
    else
    {
        emit32le(uxtw_x(HW_SCR_VAL, wn));
        store_local(HW_SCR_VAL, d);
    }
}

void arm64::store_double(byte d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_double(%u, %g)", d, v);
    uint64_t bits = 0; memcpy(&bits, &v, 8);
    load_imm64(xreg(d), bits);
    emit32le(str_off(xreg(d), HW_FRAME, d));
    touch_local(d);
}

void arm64::binop_local(byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm64, "binop_local(%u, %u, %s)", d, s, op);
    load_local(d, HW_SCR_ADDR);
    load_local(s, HW_SCR_VAL);
    uint rd = HW_SCR_ADDR;
    uint rs = HW_SCR_VAL;

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
        emit32le(lsl_rrr(rd, rd, rs));
    else if (!strcmp(op, "sar") || !strcmp(op, "shr"))
        emit32le(asr_rrr(rd, rd, rs));
    else if (!strcmp(op, "ushr"))
        emit32le(lsr_rrr(rd, rd, rs));
    else if (!strcmp(op, "idiv"))
        emit32le(sdiv_rrr(rd, rd, rs));
    else if (!strcmp(op, "div"))
        emit32le(udiv_rrr(rd, rd, rs));
    else if (!strcmp(op, "idiv_mod"))
    {
        emit32le(sdiv_rrr(HW_SCR_OFF, rd, rs));
        emit32le(msub_rrr(rd, HW_SCR_OFF, rs, rd));
    }
    else if (!strcmp(op, "div_mod"))
    {
        emit32le(udiv_rrr(HW_SCR_OFF, rd, rs));
        emit32le(msub_rrr(rd, HW_SCR_OFF, rs, rd));
    }
    else if (!strcmp(op, "fadd") || !strcmp(op, "fsub")
          || !strcmp(op, "fmul") || !strcmp(op, "fdiv"))
    {
#if defined(__linux__) && !defined(__ANDROID__)
        unsigned opc = 0;
        if (!strcmp(op, "fsub")) opc = 1;
        else if (!strcmp(op, "fmul")) opc = 2;
        else if (!strcmp(op, "fdiv")) opc = 3;
        spill_frame();
        load_imm64(0, uint64_t(opc));
        load_imm64(1, uint64_t(d));
        load_imm64(2, uint64_t(s));
        call_abs(xlr8_runtime_fn("xlr8_float_binop"));
        reload_shadows();
        touch_local(d);
        return;
#else
        uint fop = 0x2800;
        if (!strcmp(op, "fsub")) fop = 0x3800;
        else if (!strcmp(op, "fmul")) fop = 0x0800;
        else if (!strcmp(op, "fdiv")) fop = 0x1800;
        uint wd = (d < LOC_SHADOW_COUNT) ? uint(d) : rd;
        uint ws = (s < LOC_SHADOW_COUNT) ? uint(s) : rs;
        emit32le(fmov_s_w(0, wd));
        emit32le(fmov_s_w(1, ws));
        emit32le(fop_ss(fop, 0, 0, 1));
        emit32le(fmov_w_s(wd, 0));
        store_float_local(d, wd);
        return;
#endif
    }
    else if (!strcmp(op, "dadd") || !strcmp(op, "dsub")
          || !strcmp(op, "dmul") || !strcmp(op, "ddiv"))
    {
        uint fop = 0x2800;
        if (!strcmp(op, "dsub")) fop = 0x3800;
        else if (!strcmp(op, "dmul")) fop = 0x0800;
        else if (!strcmp(op, "ddiv")) fop = 0x1800;
        emit32le(fmov_d_x(0, rd));
        emit32le(fmov_d_x(1, rs));
        emit32le(fop_dd(fop, 0, 0, 1));
        emit32le(fmov_x_d(rd, 0));
    }
    else
        trap(op);

    store_local(rd, d);
    touch_local(d);
}


void arm64::binop_imm(byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    record(arm64, "binop_imm(%u, %lld, %s)", d, v, op);
    load_local(d, HW_SCR_ADDR);
    uint rd = HW_SCR_ADDR;

    if (!strcmp(op, "add") || !strcmp(op, "addi"))
    {
        if (v >= 0 && v < 4096)
            emit32le(add_imm(rd, rd, uint32_t(v)));
        else if (v < 0 && v > -4096)
            emit32le(sub_imm(rd, rd, uint32_t(-v)));
        else
        {
            load_imm64(HW_SCR_VAL, uint64_t(v));
            emit32le(add_rrr(rd, rd, HW_SCR_VAL));
        }
    }
    else if (!strcmp(op, "sub") || !strcmp(op, "subi"))
    {
        if (v >= 0 && v < 4096)
            emit32le(sub_imm(rd, rd, uint32_t(v)));
        else if (v < 0 && v > -4096)
            emit32le(add_imm(rd, rd, uint32_t(-v)));
        else
        {
            load_imm64(HW_SCR_VAL, uint64_t(v));
            emit32le(sub_rrr(rd, rd, HW_SCR_VAL));
        }
    }
    else if (!strcmp(op, "imul"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(mul_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "and"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(and_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "or"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(orr_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "xor"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(eor_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "shl_imm") || !strcmp(op, "sar_imm")
          || !strcmp(op, "shr_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        if (!strcmp(op, "shl_imm"))
            emit32le(lsl_rrr(rd, rd, HW_SCR_VAL));
        else if (!strcmp(op, "sar_imm"))
            emit32le(asr_rrr(rd, rd, HW_SCR_VAL));
        else
            emit32le(lsr_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "idiv_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(sdiv_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "div_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(udiv_rrr(rd, rd, HW_SCR_VAL));
    }
    else if (!strcmp(op, "idiv_mod_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(sdiv_rrr(HW_SCR_OFF, rd, HW_SCR_VAL));
        emit32le(msub_rrr(rd, HW_SCR_OFF, HW_SCR_VAL, rd));
    }
    else if (!strcmp(op, "div_mod_imm"))
    {
        load_imm64(HW_SCR_VAL, uint64_t(v));
        emit32le(udiv_rrr(HW_SCR_OFF, rd, HW_SCR_VAL));
        emit32le(msub_rrr(rd, HW_SCR_OFF, HW_SCR_VAL, rd));
    }
    else
        trap(op);

    store_local(rd, d);
    touch_local(d);
}

void arm64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(arm64, "set_pred(%u, %d)", p, on);
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

void arm64::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to AArch64 condition code
// ----------------------------------------------------------------------------
{
    record(arm64, "set_pred_cmp(%u, %u)", p, cond);
    load_imm64(HW_SCR_ADDR, ~(1ULL << p));
    emit32le(and_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_ADDR));
    emit32le(cset_x(HW_SCR_VAL, cond));
    size_t cbz_pos = cursor();
    emit32le(cbz_imm(HW_SCR_VAL, 0));
    load_imm64(HW_SCR_ADDR, 1ULL << p);
    emit32le(orr_rrr(HW_FLAGS, HW_FLAGS, HW_SCR_ADDR));
    int off = int((cursor() - cbz_pos) / 4);
    code().patch32(cbz_pos, cbz_imm(HW_SCR_VAL, off));
}


void arm64::cmp_local(byte p, byte d, byte s, cstring op)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    record(arm64, "cmp_local(%u, %u, %u, %s)", p, d, s, op);
    emit32le(cmp_rr(xreg(d), xreg(s)));
    set_pred_cmp(p, cond_from_op(op));
}

void arm64::cmp_imm(byte p, byte d, value_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(arm64, "cmp_imm(%u, %u, %lld, %s)", p, d, v, op);
    emit32le(cmp_r_imm(xreg(d), uint32_t(v)));
    set_pred_cmp(p, cond_from_op(op));
}

void arm64::cmp_uimm(byte p, byte d, uvalue_t v, cstring op)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    record(arm64, "cmp_uimm(%u, %u, %llu, %s)", p, d, v, op);
    cmp_imm(p, d, value_t(v), op);
}

void arm64::cond_mov_local(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d
// ----------------------------------------------------------------------------
{
    record(arm64, "cond_mov_local(%u, %u, %u)", p, d, s);
    (void)p; mov_local(d,s);
}

void arm64::select_local(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d
// ----------------------------------------------------------------------------
{
    record(arm64, "select_local(%u, %u, %u, %u)", p, d, s, x);
    (void)p; mov_local(d,s); (void)x;
}

void arm64::neg_local(byte d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm64, "neg_local(%u)", d);
    emit32le(neg_rr(xreg(d)));
    touch_local(d);
}

void arm64::not_local(byte d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm64, "not_local(%u)", d);
    emit32le(not_rr(xreg(d)));
    touch_local(d);
}

void arm64::incr_local(byte d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm64, "incr_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(add_imm(HW_SCR_VAL, HW_SCR_VAL, 1));
    store_local(HW_SCR_VAL, d);
    touch_local(d);
}

void arm64::decr_local(byte d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    record(arm64, "decr_local(%u)", d);
    load_local(d, HW_SCR_VAL);
    emit32le(sub_imm(HW_SCR_VAL, HW_SCR_VAL, 1));
    store_local(HW_SCR_VAL, d);
    touch_local(d);
}

void arm64::sext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(arm64, "sext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_ADDR);
    if (w < 64)
        emit32le(sbfm_width(HW_SCR_ADDR, HW_SCR_ADDR, w));
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}

void arm64::zext_local(byte d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(arm64, "zext_local(%u, %u)", d, w);
    load_local(d, HW_SCR_ADDR);
    if (w < 64)
    {
        load_imm64(HW_SCR_OFF, (1ull << w) - 1ull);
        emit32le(and_rrr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}

void arm64::frame_addr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address of local slot s into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "frame_addr(%u, %u)", d, s);
    if (s == 0)
        emit32le(mov_rr(HW_SCR_ADDR, HW_FRAME));
    else if (uint(s) <= 511)
        emit32le(add_imm(HW_SCR_ADDR, HW_FRAME, uint(s) * 8));
    else
    {
        load_imm64(HW_SCR_VAL, uint64_t(s) * 8);
        emit32le(add_rrr(HW_SCR_ADDR, HW_FRAME, HW_SCR_VAL));
    }
    store_local(HW_SCR_ADDR, d);
    touch_local(d);
}


void arm64::store_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d
// ----------------------------------------------------------------------------
{
    record(arm64, "store_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_SCR_VAL);
    load_local(d, HW_SCR_ADDR);
    switch (sz)
    {
    case 1: emit32le(0x39000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    case 2: emit32le(0x79000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    case 4: emit32le(0xb9000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    default: emit32le(str_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    }
    frame_memory_touched = true;
}


void arm64::load_mem(byte d, byte s, uint sz)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "load_mem(%u, %u, %u)", d, s, sz);
    load_local(s, HW_SCR_ADDR);
    if (d < LOC_SHADOW_COUNT)
    {
        switch (sz)
        {
        case 1: emit32le(ldrb_idx(xreg(d), HW_SCR_ADDR)); break;
        case 2: emit32le(ldrh_idx(xreg(d), HW_SCR_ADDR)); break;
        case 4: emit32le(ldr_w_idx(xreg(d), HW_SCR_ADDR)); break;
        default: emit32le(ldr_idx(xreg(d), HW_SCR_ADDR)); break;
        }
        emit32le(str_off(xreg(d), HW_FRAME, d));
    }
    else
    {
        switch (sz)
        {
        case 1: emit32le(ldrb_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 2: emit32le(ldrh_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 4: emit32le(ldr_w_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        default: emit32le(ldr_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        }
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}


void arm64::store_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d plus offset
// ----------------------------------------------------------------------------
{
    record(arm64, "store_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(d, HW_SCR_ADDR);
    if (o)
    {
        load_imm64(HW_SCR_OFF, uint64_t(o));
        emit32le(add_rrr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    load_local(s, HW_SCR_VAL);
    switch (sz)
    {
    case 1: emit32le(0x39000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    case 2: emit32le(0x79000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    case 4: emit32le(0xb9000000u | (HW_SCR_ADDR << 5) | HW_SCR_VAL); break;
    default: emit32le(str_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
    }
    frame_memory_touched = true;
}


void arm64::load_mem_off(byte d, byte s, value_t o, uint sz)
// ----------------------------------------------------------------------------
//   Load from address s plus offset into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "load_mem_off(%u, %u, %lld, %u)", d, s, o, sz);
    load_local(s, HW_SCR_ADDR);
    if (o)
    {
        load_imm64(HW_SCR_OFF, uint64_t(o));
        emit32le(add_rrr(HW_SCR_ADDR, HW_SCR_ADDR, HW_SCR_OFF));
    }
    if (d < LOC_SHADOW_COUNT)
    {
        switch (sz)
        {
        case 1: emit32le(ldrb_idx(xreg(d), HW_SCR_ADDR)); break;
        case 2: emit32le(ldrh_idx(xreg(d), HW_SCR_ADDR)); break;
        case 4: emit32le(ldr_w_idx(xreg(d), HW_SCR_ADDR)); break;
        default: emit32le(ldr_idx(xreg(d), HW_SCR_ADDR)); break;
        }
        emit32le(str_off(xreg(d), HW_FRAME, d));
    }
    else
    {
        switch (sz)
        {
        case 1: emit32le(ldrb_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 2: emit32le(ldrh_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        case 4: emit32le(ldr_w_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        default: emit32le(ldr_idx(HW_SCR_VAL, HW_SCR_ADDR)); break;
        }
        store_local(HW_SCR_VAL, d);
    }
    touch_local(d);
}

void arm64::load_signext(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Load from memory with sign extension
// ----------------------------------------------------------------------------
{
    record(arm64, "load_signext(%u, %u, %u)", d, s, w);
    load_mem(d,s,(w+7)/8); sext_local(d,w);
}

void arm64::load_signext_off(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Load from memory at offset with sign extension
// ----------------------------------------------------------------------------
{
    record(arm64, "load_signext_off(%u, %u, %u, %lld)", d, s, w, o);
    load_mem_off(d,s,o,(w+7)/8); sext_local(d,w);
}

void arm64::bra_offset(broff_t b)
// ----------------------------------------------------------------------------
//   Emit unconditional branch with bytecode offset
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_offset(%lld)", b);
    uint pos = uint(cursor());
    emit32le(b_imm(0));
    record_branch(b, pos);
}

void arm64::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Patch a recorded branch instruction to jump to target_native
// ----------------------------------------------------------------------------
{
    record(arm64, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    int32_t off = int32_t(target_native - insn_pos) / 4;
    uint32_t insn = uint32_t(code().data[insn_pos])
                  | (uint32_t(code().data[insn_pos + 1]) << 8)
                  | (uint32_t(code().data[insn_pos + 2]) << 16)
                  | (uint32_t(code().data[insn_pos + 3]) << 24);
    uint32_t op = insn & 0xff000000u;
    if (op == 0x14000000u)
        insn = b_imm(off);
    else if ((insn & 0xff000010u) == 0x54000000u)
        insn = b_cond_imm(byte(insn & 0xfu), off);
    else if (op == 0xb4000000u)
        insn = cbz_imm(insn & 0x1fu, off);
    else if (op == 0xb5000000u)
        insn = cbnz_imm(insn & 0x1fu, off);
    code().patch32(insn_pos, insn);
}


void arm64::bra_zero(byte s, broff_t b, bool eq)
// ----------------------------------------------------------------------------
//   Emit branch when local s is zero or non-zero
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_zero(%u, %lld, %d)", s, b, eq);
    load_local(s, HW_SCR_VAL);
    uint pos = uint(cursor());
    if (eq)
        emit32le(cbz_imm(HW_SCR_VAL, 0));
    else
        emit32le(cbnz_imm(HW_SCR_VAL, 0));
    record_branch(b, pos);
}


void arm64::bra_pred(byte p, broff_t b, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_pred(%u, %lld, %d)", p, b, sense);
    (void) b;
    load_imm64(HW_SCR_VAL, uint64_t(1) << p);
    emit32le(and_rrr(HW_SCR_VAL, HW_FLAGS, HW_SCR_VAL));
    uint pos = uint(cursor());
    if (sense)
        emit32le(cbnz_imm(HW_SCR_VAL, 0));
    else
        emit32le(cbz_imm(HW_SCR_VAL, 0));
    record_branch(b, pos);
}


void arm64::bra_signed(byte s, broff_t b, byte cond)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_signed(%u, %lld, %u)", s, b, cond);
    load_local(s, HW_SCR_VAL);
    emit32le(cmp_r_imm(HW_SCR_VAL, 0));
    uint pos = uint(cursor());
    emit32le(b_cond_imm(cond, 0));
    record_branch(b, pos);
}

void arm64::dbra_local(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(arm64, "dbra_local(%u, %lld)", d, b);
    decr_local(d); bra_zero(d,b,false);
}

void arm64::jmp_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit jump to token-resolved label
// ----------------------------------------------------------------------------
{
    record(arm64, "jmp_tok(%llu)", t);
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
}

void arm64::patch_imm64_at(void *mem, size_t at, uint r, uint64_t v)
// ----------------------------------------------------------------------------
//   Rewrite a 16-byte load_imm64 slot at a fixed code offset
// ----------------------------------------------------------------------------
{
    static const uint32_t nop = 0xd503201fu;
    auto patch32 = [&](size_t off, uint32_t insn)
    {
        code().patch32(off, insn);
        if (mem)
            memcpy((void *) ((byte_p) mem + off), &insn, sizeof(insn));
    };
    patch32(at, movz(r, uint32_t(v) & 0xffffu));
    at += 4;
    if ((v >> 16) & 0xffffu)
        patch32(at, movk(r, uint32_t(v >> 16), 16));
    else
        patch32(at, nop);
    at += 4;
    if ((v >> 32) & 0xffffu)
        patch32(at, movk(r, uint32_t(v >> 32), 32));
    else
        patch32(at, nop);
    at += 4;
    if ((v >> 48) & 0xffffu)
        patch32(at, movk(r, uint32_t(v >> 48), 48));
}


void arm64::relocate(void *base)
// ----------------------------------------------------------------------------
//   Fix absolute addresses embedded in generated code after mmap
// ----------------------------------------------------------------------------
{
    record(arm64, "relocate(%p)", base);
    (void) base;
    section &sec = code();
    for (abs_fixup &fx : abs_fixups)
    {
        auto it = bc_positions.find(fx.offset);
        if (it == bc_positions.end())
        {
            auto next = bc_positions.lower_bound(fx.offset);
            if (next == bc_positions.end())
                continue;
            it = next;
        }
        int off = int(it->second) - int(fx.imm_at);
        uint32_t insn = uint32_t(sec[fx.imm_at])
                      | (uint32_t(sec[fx.imm_at + 1]) << 8)
                      | (uint32_t(sec[fx.imm_at + 2]) << 16)
                      | (uint32_t(sec[fx.imm_at + 3]) << 24);
        uint rd = insn & 0x1fu;
        sec.patch32(fx.imm_at, adr(rd, off));
        if (base)
        {
            uint32_t patched = adr(rd, off);
            memcpy((void *) ((byte_p) base + fx.imm_at), &patched, sizeof(patched));
        }
    }
}


size_t arm64::emit_over_callees_branch()
// ----------------------------------------------------------------------------
//   Emit a forward branch over callee bodies to the epilogue
// ----------------------------------------------------------------------------
{
    record(arm64, "emit_over_callees_branch()");
    size_t pos = cursor();
    emit32le(b_imm(0));
    return pos;
}


void arm64::call_tok(tok_t t)
// ----------------------------------------------------------------------------
//   Emit call to token-resolved label
// ----------------------------------------------------------------------------
{
    record(arm64, "call_tok(%llu)", t);
    static const uint32_t nop = 0xd503201fu;
    spill_frame();
    size_t adr_at = cursor();
    emit32le(nop);
    emit32le(mov_rr(0, HW_SCR_VAL));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    uint head = tok_jumps.count(t) ? tok_jumps[t] : 0;
    uint pos = gjmp(head);
    record_tok_jump(t, pos);
    int off = int(pos + 4) - int(adr_at);
    code().patch32(adr_at, adr(HW_SCR_VAL, off));
}

void arm64::br_via_ip()
// ----------------------------------------------------------------------------
//   Branch to address in x0 without leaving a stale shadow in x0
// ----------------------------------------------------------------------------
{
    record(arm64, "br_via_ip()");
    emit32le(mov_rr(HW_IP, 0));
    reload_shadows();
    emit32le(br(HW_IP));
}

void arm64::ijmp_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(arm64, "ijmp_local(%u)", s);
    spill_frame();
    load_local(s, 0);
    rtcall("xlr8_jit_addr");
    br_via_ip();
}

void arm64::icall_local(byte s)
// ----------------------------------------------------------------------------
//   Indirect call through local s (bytecode PC -> native)
// ----------------------------------------------------------------------------
{
    record(arm64, "icall_local(%u)", s);
    spill_frame();
    size_t adr_at = cursor();
    emit32le(adr(HW_SCR_VAL, 0));
    emit32le(mov_rr(0, HW_SCR_VAL));
    rtcall("xlr8_push_value");
    rtcall("xlr8_sync_args");
    reload_shadows();
    load_local(s, 0);
    rtcall("xlr8_jit_addr");
    emit32le(mov_rr(HW_IP, 0));
    reload_shadows();
    size_t blr_at = cursor();
    emit32le(blr(HW_IP));
    int off = int(blr_at + 4) - int(adr_at);
    code().patch32(adr_at, adr(HW_SCR_VAL, off));
}

void arm64::push_pc()
// ----------------------------------------------------------------------------
//   Push program counter onto stack
// ----------------------------------------------------------------------------
{
    record(arm64, "push_pc()");
    spill_frame();
    emit32le(adr(0, 0));
    rtcall("xlr8_push_value");
    reload_shadows();
    spill_arg_regs = true;
}

void arm64::load_pc(byte d)
// ----------------------------------------------------------------------------
//   Load current PC into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "load_pc(%u)", d);
    uint r = d < LOC_SHADOW_COUNT ? xreg(d) : HW_SCR_VAL;
    emit32le(adr(r, 0));
    if (d >= LOC_SHADOW_COUNT)
        store_local(r, d);
    else
        emit32le(str_off(xreg(d), HW_FRAME, d));
    touch_local(d);
}

void arm64::load_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (interpreter semantics)
// ----------------------------------------------------------------------------
{
    record(arm64, "load_pca(%u, %lld)", d, b);
    (void) b;
    uint r = d < LOC_SHADOW_COUNT ? xreg(d) : HW_SCR_VAL;
    load_imm64(r, uint64_t(pending_branch_target));
    if (d >= LOC_SHADOW_COUNT)
        store_local(r, d);
    else
        emit32le(str_off(xreg(d), HW_FRAME, d));
    touch_local(d);
}

void arm64::get_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy argument local s into d
// ----------------------------------------------------------------------------
{
    record(arm64, "get_arg(%u, %u)", d, s);
    spill_frame();
    load_imm64(0, uint64_t(d));
    load_imm64(1, uint64_t(s));
    rtcall("xlr8_get_arg");
    reload_shadows();
    touch_local(d);
}

void arm64::set_arg(byte d, byte s)
// ----------------------------------------------------------------------------
//   Copy local s into argument slot d (OP set: args[d] = local[s])
// ----------------------------------------------------------------------------
{
    record(arm64, "set_arg(%u, %u)", d, s);
    spill_frame();
    load_imm64(0, uint64_t(d));
    load_imm64(1, uint64_t(s));
    rtcall("xlr8_set_arg");
    frame_memory_touched = true;
}

void arm64::push_local(byte s)
// ----------------------------------------------------------------------------
//   Push local s onto stack
// ----------------------------------------------------------------------------
{
    record(arm64, "push_local(%u)", s);
    spill_frame();
    if (s < LOC_SHADOW_COUNT)
        emit32le(mov_rr(0, xreg(s)));
    else
        emit32le(ldr_off(0, HW_FRAME, s));
    rtcall("xlr8_push_value");
    reload_shadows();
    spill_arg_regs = true;
}


void arm64::pop_local(byte d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "pop_local(%u)", d);
    rtcall("xlr8_pop_value");
    emit32le(mov_rr(HW_SCR_VAL, 0));
    restore_frame_ptr();
    runtime *rt = xlr8_get_runtime();
    if (rt && rt->frame)
    {
        byte locals = byte(rt->frame[-1]);
        if (!locals)
            locals = byte(rt->CALL_CLOBBERED_REGS);
        if (locals > LOC_SHADOW_COUNT)
            locals = LOC_SHADOW_COUNT;
        for (uint r = 1; r < locals; r++)
            emit32le(ldr_off(xreg(byte(r)), HW_FRAME, r));
    }
    if (d != 0)
        emit32le(ldr_off(xreg(0), HW_FRAME, 0));
    store_local(HW_SCR_VAL, d);
}


void arm64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    record(arm64, "push_flags()");
    emit32le(mov_rr(0, HW_FLAGS));
    rtcall("xlr8_push_flags_val");
    spill_arg_regs = true;
}


void arm64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(arm64, "pop_flags()");
    rtcall("xlr8_pop_flags_val");
    emit32le(mov_rr(HW_FLAGS, 0));
}

void arm64::store_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d
// ----------------------------------------------------------------------------
{
    record(arm64, "store_global(%u, %u)", d, s);
    spill_frame();
    load_imm64(0, uint64_t(d));
    load_imm64(1, uint64_t(s));
    rtcall("xlr8_store_global");
    reload_shadows();
}

void arm64::load_global(byte d, byte s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d
// ----------------------------------------------------------------------------
{
    record(arm64, "load_global(%u, %u)", d, s);
    spill_frame();
    load_imm64(0, uint64_t(d));
    load_imm64(1, uint64_t(s));
    rtcall("xlr8_load_global");
    reload_shadows();
    touch_local(d);
}

void arm64::do_ret()
// ----------------------------------------------------------------------------
//   Return from generated code to the native runner
// ----------------------------------------------------------------------------
{
    record(arm64, "do_ret()");
    emit32le(ldp_pop(30));
    emit32le(ret_insn());
}

void arm64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(arm64_warning, "trap(%s)", msg);
    (void)msg;
    emit32le(brk_insn());
}

void arm64::rtcall(cstring fn)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(arm64, "rtcall(%s)", fn);
    call_abs(xlr8_runtime_fn(fn));
}

void arm64::rtcall(cstring fn, tok_t a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(arm64, "rtcall(%s, %llu)", fn, a);
    load_imm64(0, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}

void arm64::rtcall(cstring fn, byte a)
// ----------------------------------------------------------------------------
//   Emit call to runtime helper function
// ----------------------------------------------------------------------------
{
    record(arm64, "rtcall(%s, %u)", fn, a);
    load_imm64(0, uint64_t(a));
    call_abs(xlr8_runtime_fn(fn));
}



#include "arm64-emit.cpp"

