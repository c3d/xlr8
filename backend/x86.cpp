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

#include "leb128.h"
#include "native-bridge.h"
#include "runtime.h"

#include <cmath>
#include <cstring>


RECORDER(x86,             32, "x86 family code generation helpers");
RECORDER(x86_ops,         32, "x86 family opcode emission");
RECORDER(x86_error,        8, "x86 family code generation errors");
RECORDER(x86_warning,      8, "x86 family code generation warnings");


x86::x86(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an x86 family code generator
// ----------------------------------------------------------------------------
    : backend(rt, arch)
{
    record(x86, "New backend arch=%+s", arch_name(arch));
}



// ============================================================================
//
//   Backend standard interfacd
//
// ============================================================================

cstring x86::machine_defs() const
// ----------------------------------------------------------------------------
//   Machine definition for 32-bit x86
// ----------------------------------------------------------------------------
{
    return "__i386__\0__i386\0";
}


uint x86::registers() const
// ----------------------------------------------------------------------------
//   Return the number of hardware registers on this platform
// ----------------------------------------------------------------------------
{
    return HW_COUNT;
}


uint x86::pointer_size() const
// ----------------------------------------------------------------------------
//   The platform is 32-bit
// ----------------------------------------------------------------------------
{
    return 4;                   // 32-bit
}


void x86::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs, prime frame/flags, load shadow locals (x86-64)
// ----------------------------------------------------------------------------
{
    record(x86, "begin_translation");
    backend::begin_translation();
    prologue();
}


void x86::finish_translation()
// ----------------------------------------------------------------------------
//   Restore stack and callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(x86, "finish_translation");
    epilogue();
    backend::finish_translation();
}


void x86::patch(hwcode_t pos, hwcode_t pvalue, uint flags)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (rel8 or rel32) to jump to target_native
// ----------------------------------------------------------------------------
{
    record(x86, "patch pos=%llu target=%llu flags=0x%X",
           pos, pvalue, flags);

    section  &sec    = code();
    ptrdiff_t diff   = pvalue - pos;
    int32_t   diff32 = int32_t(diff);
    if (diff != diff32)
    {
        record(x86_error,
               "Patching %lu with overflow offset 0x%llX-0x%llX=%lld",
               pos, pvalue, pos, diff);
    }

    // Short forms return the disp8 site; opcode is the preceding byte.
    if (pos)
    {
        byte op = sec[pos - 1];
        if (op == 0xeb || (op & 0xf0) == 0x70)
        {
            diff32 -= 1;
            if (diff32 < -128 || diff32 > 127)
            {
                record(x86_error,
                       "rel8 branch out of range off=%d",
                       diff32);
                return;
            }
            sec.patch8(pos, byte(int8_t(diff32)));
            return;
        }
    }
    diff32 -= 4;
    sec.patch32le(pos, uint32_t(diff32));
}


void x86::call(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    record(x86, "call %p", target);
    call_ptr(target);
}


void x86::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        if (!spilled_args)
            hw_sys_arg_spill();
        record(x86, "syscall %p (direct-map spill)", target);
        call_ptr(target);
    }
    else
    {
        size_t spilled = spill_all();
        record(x86, "syscall %p spilled %zu", target, spilled);
        call_ptr(target);
    }
}


uint x86::relocate(void *base)
// ----------------------------------------------------------------------------
//   Relocate absolute addresses after mapping the code, return num of relocs
// ----------------------------------------------------------------------------
{
    size_t count = relocations.size();
    record(x86, "relocate hase=%p count=%zu", base, count);

    section &s = code();
    for (hwcode_t patch : relocations)
    {
        uint32_t  offset = s.value32le(patch);
        uintptr_t abs    = uintptr_t(byte_p(base) + offset);
        uint32_t  abs32  = uint32_t(abs);
        if (abs != abs32)
            record(x86_warning, "Relocating %p truncated to %u", abs, abs32);
        memcpy((void *) (byte_p(base) + patch), &abs32, sizeof(abs32));
    }
    relocations.clear();
    return count;
}


bool x86::move(hwreg_t to, hwreg_t from)
// ----------------------------------------------------------------------------
//   Move between registers
// ----------------------------------------------------------------------------
{
    emit_mov_rr(hw(to), hw(from));
    return true;
}


bool x86::load(hwreg_t h, local_t n)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register r
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw(h);
    record(x86, "load %u=[%u]", r, n);
    int disp = local_disp(n);

    if (r >= HW_XMM0D)
    {
        emit8(0xf2 | (r >= HW_XMM0F));
        emit_sse_rex_r(r);
        emit8(0x0f);
        emit8(0x10);
    }
    else
    {
        uint w = ptr64() ? 8 : 0;
        if (w || r >= 8 || HW_FRAME >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        emit8(0x8B);
    }
    emit_modrm_frame(r, disp);
    return true;
}


bool x86::store(local_t n, hwreg_t h)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw(h);
    record(x86, "store [%u]=%u", n, r);
    int disp = local_disp(n);

    if (r >= HW_XMM0D)
    {
        emit8(0xf2 | (r >= HW_XMM0F));
        emit_sse_rex_r(r);
        emit8(0x0f);
        emit8(0x11);
    }
    else
    {
        uint w = ptr64() ? 8 : 0;
        if (w || r >= 8 || HW_FRAME >= 8)
            emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(HW_FRAME) ? 1 : 0));
        emit8(0x89);
    }
    emit_modrm_frame(r, disp);
    saved(n, h);
    return true;
}


hwreg_t x86::register_for(local_t loc, hwspec_t spec, bool ld)
// ----------------------------------------------------------------------------
//   Find the register associated to the local (amd64 direct-map / i386 stub)
// ----------------------------------------------------------------------------
{
    if (!ptr64())
    {
        record(x86_error, "i386 register_for not yet implemented");
        return NO_REGISTER;
    }

    bool isint = spec & RSPEC_INT;
    bool isflt = spec & RSPEC_FLT;
    bool isdbl = spec & RSPEC_DBL;
    if (!isint && !isflt && !isdbl)
    {
        record(x86_error, "Invalid register specification 0x%08X", spec);
        return NO_REGISTER;
    }

    uint count = isint ? uint(HW_SHADOWS) : uint(HW_FSHADOWS);

    if (loc < count)
    {
        x86reg_t ir = hw_int_order(loc);
        x86reg_t fr = isflt ? hw_flt_order(loc)
                    : isdbl ? hw_dbl_order(loc)
                            : x86reg_t(NO_REGISTER);
        if (ld && (isflt || isdbl))
        {
            // movd/movq xmm, gpr — load FP bits from durable GPR home
            emit8(0x66);
            if (isdbl)
                emit8(byte(0x48 | (rex_r(fr) ? 4 : 0) | (rex_b(ir) ? 1 : 0)));
            else if (rex_r(fr) || rex_b(ir))
                emit8(byte(0x40 | (rex_r(fr) ? 4 : 0) | (rex_b(ir) ? 1 : 0)));
            emit8(0x0f);
            emit8(0x6e);
            emit8(byte(0xc0 | (reg_value(fr) << 3) | reg_value(ir)));
        }
        return isint ? ir : fr;
    }

    // Overflow local: use a temp register
    x86reg_t temp = isint ? x86reg_t(HW_TEMP0)
                  : isflt ? x86reg_t(HW_FTEMP0)
                  : isdbl ? x86reg_t(HW_DTEMP0)
                          : x86reg_t(NO_REGISTER);
    for (uint tmp = temp; tmp < uint(temp) + HW_TEMPS; tmp++)
    {
        local_t rloc = reg_local[tmp];
        if (rloc == NO_LOCAL || rloc == loc)
        {
            if (ld)
                load(tmp, loc);
            return tmp;
        }
    }

    // All temps busy: spill the first and reuse it
    spill(temp);
    if (ld)
        load(temp, loc);
    return temp;
}


void x86::register_sync(byte_p pc)
// ----------------------------------------------------------------------------
//   Synchronize the temporaries (amd64 direct-map / i386 no-op)
// ----------------------------------------------------------------------------
{
    if (!ptr64())
        return;

    // Spill anything that was loaded in a temporary
    for (uint t = 0; t < HW_TEMPS; t++)
        spill(HW_TEMP0 + t) || spill(HW_DTEMP0 + t) || spill(HW_FTEMP0 + t);

    // Drop stale forward maps that still point at temps
    for (local_t loc = 0; loc < local_reg.size(); loc++)
    {
        hwreg_t r = local_reg[loc];
        if (r >= HW_TEMP0 && r < HW_TEMP0 + HW_TEMPS)
            local_reg[loc] = NO_REGISTER;
        else if (r >= HW_FTEMP0 && r < hwreg_t(HW_FTEMP0 + HW_TEMPS))
            local_reg[loc] = NO_REGISTER;
        else if (r >= HW_DTEMP0 && r < hwreg_t(HW_DTEMP0 + HW_TEMPS))
            local_reg[loc] = NO_REGISTER;
    }

    // Move floating point values back into GPR durable homes (iorder[loc]).
    for (int sz = 0; sz < 2; sz++)
    {
        hwreg_t base = sz ? HW_XMM0D : HW_XMM0F;
        for (uint fp = 0; fp < HW_FSHADOWS; fp++)
        {
            local_t loc = reg_local[base + fp];
            if (loc != NO_LOCAL)
            {
                x86reg_t xmm = x86reg_t(base + fp);
                if (loc >= HW_SHADOWS)
                {
                    // High local: movd/movq to temp GPR, then store to frame
                    x86reg_t tmp = x86reg_t(HW_TEMP0);
                    emit8(0x66);
                    if (sz)
                        emit8(byte(0x48 | (rex_r(xmm) ? 4 : 0)
                                        | (rex_b(tmp) ? 1 : 0)));
                    else if (rex_r(xmm) || rex_b(tmp))
                        emit8(byte(0x40 | (rex_r(xmm) ? 4 : 0)
                                        | (rex_b(tmp) ? 1 : 0)));
                    emit8(0x0f);
                    emit8(0x7e);
                    emit8(byte(0xc0 | (reg_value(xmm) << 3) | reg_value(tmp)));
                    store(loc, tmp);
                    reg_local[base + fp] = NO_LOCAL;
                    continue;
                }
                x86reg_t home = hw_int_order(loc);
                // movd/movq gpr, xmm — bit-copy back to durable GPR home
                emit8(0x66);
                if (sz)
                    emit8(byte(0x48 | (rex_r(xmm) ? 4 : 0)
                                    | (rex_b(home) ? 1 : 0)));
                else if (rex_r(xmm) || rex_b(home))
                    emit8(byte(0x40 | (rex_r(xmm) ? 4 : 0)
                                    | (rex_b(home) ? 1 : 0)));
                emit8(0x0f);
                emit8(0x7e);
                emit8(byte(0xc0 | (reg_value(xmm) << 3) | reg_value(home)));
                local_reg[loc]       = home;
                reg_local[home]      = loc;
                reg_local[base + fp] = NO_LOCAL;
            }
        }
    }

    // Forward branches: if this PC is a branch target, ensure consistent state.
    // Reload all spilled args so the register file is clean at merge points.
    if (ptr64())
    {
        if (label_branches(pc))
            hw_sys_arg_reload();
    }
    else
    {
        label_branches(pc);
    }
}


uint x86::hw_spills_at(byte_p target)
// ----------------------------------------------------------------------------
//   Return the value of spilled_args recorded at a given branch target
// ----------------------------------------------------------------------------
{
    auto spill = backend::lower_bound(spills, target);
    if (spill != spills.end() && spill->bytecode == target)
        return spill->spilled;
    return 0;
}


void x86::hw_sys_arg_reload(byte_p target)
// ----------------------------------------------------------------------------
//   Reconcile spill state to match what was recorded at branch target
// ----------------------------------------------------------------------------
{
    uint dspill = hw_spills_at(target);
    uint cspill = spilled_args;
    uint diffs  = dspill ^ cspill;
    if (diffs)
    {
        for (uint r = HW_RSI; r <= uint(HW_R11); r++)
        {
            if (!((HW_SPILL_MASK >> r) & 1))
                continue;
            uint dd = (diffs >> r) & 1;
            if (!dd)
                continue;
            if ((dspill >> r) & 1)
                emit_store_u64(HW_RSP, x86reg_t(r), spill_offset(r));
            else
                emit_load_u64(x86reg_t(r), HW_RSP, spill_offset(r));
        }

        // If destination has no spill area but we currently have one, dealloc
        if (!dspill && cspill)
        {
            emit_add_rsp_imm(int32_t(HW_SPILL_AREA * sizeof(ularge_t)));
        }
        // If destination has a spill area but we don't, allocate and spill
        else if (dspill && !cspill)
        {
            emit_sub_rsp_imm(int32_t(HW_SPILL_AREA * sizeof(ularge_t)));
            for (uint r = HW_RSI; r <= uint(HW_R11); r++)
            {
                if ((dspill >> r) & 1)
                    emit_store_u64(HW_RSP, x86reg_t(r), spill_offset(r));
            }
        }
        spilled_args = dspill;
    }
}


x86::reginfo x86::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for x86
// ----------------------------------------------------------------------------
{
    static const reginfo i386_registers[x86::HW_COUNT] =
    {
        {    "rax",                      0 },
        {    "rcx",                      0 },
        {    "rdx",                      0 },
        {    "rbx",              RSPEC_INT },
        {    "rsp",                      0 },
        {    "rbp",                      0 },
        {    "rsi", RSPEC_CALL | RSPEC_INT },
        {    "rdi", RSPEC_CALL | RSPEC_INT },
        {     "r8",                      0 },
        {     "r9",                      0 },
        {    "r10",                      0 },
        {    "r11",                      0 },
        {    "r12",                      0 },
        {    "r13",                      0 },
        {    "r14",                      0 },
        {    "r15",                      0 },

        {  "xmm0d",              RSPEC_DBL },
        {  "xmm1d",              RSPEC_DBL },
        {  "xmm2d",              RSPEC_DBL },
        {  "xmm3d",              RSPEC_DBL },
        {  "xmm4d",              RSPEC_DBL },
        {  "xmm5d",              RSPEC_DBL },
        {  "xmm6d",              RSPEC_DBL },
        {  "xmm7d",              RSPEC_DBL },
        {  "xmm8d",                      0 },
        {  "xmm9d",                      0 },
        { "xmm10d",                      0 },
        { "xmm11d",                      0 },
        { "xmm12d",                      0 },
        { "xmm13d",                      0 },
        { "xmm14d",                      0 },
        { "xmm15d",                      0 },

        {  "xmm0f",              RSPEC_FLT },
        {  "xmm1f",              RSPEC_FLT },
        {  "xmm2f",              RSPEC_FLT },
        {  "xmm3f",              RSPEC_FLT },
        {  "xmm4f",              RSPEC_FLT },
        {  "xmm5f",              RSPEC_FLT },
        {  "xmm6f",              RSPEC_FLT },
        {  "xmm7f",              RSPEC_FLT },
        {  "xmm8f",                      0 },
        {  "xmm9f",                      0 },
        { "xmm10f",                      0 },
        { "xmm11f",                      0 },
        { "xmm12f",                      0 },
        { "xmm13f",                      0 },
        { "xmm14f",                      0 },
        { "xmm15f",                      0 }
    };

    return reg < HW_COUNT ? i386_registers[reg] : reginfo{"INVALID", 0};
}



// ============================================================================
//
//   Internal helpers
//
// ============================================================================

void x86::orex(int ll, x86reg_t r, x86reg_t x, byte c)
// ----------------------------------------------------------------------------
//   Emit REX prefix and opcode byte
// ----------------------------------------------------------------------------
{
    record(x86, "orex l=%d r=%u x=%u c=%u", ll, r, x, c);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0) | (rex_b(x) ? 1 : 0));
    emit8(c);
}


void x86::emit_rex_modrm(int ll, x86reg_t r, x86reg_t x, x86reg_t b, byte op)
// ----------------------------------------------------------------------------
//   Emit REX prefix and ModR/M encoded instruction
// ----------------------------------------------------------------------------
{
    record(x86, "emit_rex_modrm ll=%d r=%u x=%u b=%u op=%u)", ll, r, x, b, op);
    int w = (ll && ptr64()) ? 8 : 0;
    if (w || r >= 8 || x >= 8 || b >= 8)
        emit8(0x40 | w | (rex_r(r) ? 4 : 0)
              | (rex_x(x) ? 2 : 0)
              | (rex_b(b) ? 1 : 0));
    emit8(op);
    emit8(byte((reg_value(r) << 3) | reg_value(b)));
    if (reg_value(b) == 4)
        emit8(byte((reg_value(x) << 3) | reg_value(b)));
    if (reg_value(b) == 5 && x == 4)
        emit32le(0);
}


void x86::set_imm(x86reg_t r, uint64_t imm)
// ----------------------------------------------------------------------------
//   Load immediate into a hardware register (prefer short encodings)
// ----------------------------------------------------------------------------
{
    record(x86, "set_imm %u=%llu %lld 0x%llX", r, imm, imm, imm);

    // xor r32, r32 — 2–3 bytes; clears the full 64-bit register on x86-64
    if (imm == 0)
    {
        emit_op_rr(0x31, r, r, /*ll=*/0);
        return;
    }

    // mov r32, imm32 — 5–6 bytes; zero-extends into r64 on x86-64
    if (imm == uint32_t(imm))
    {
        if (rex_b(r))
            emit8(0x41);
        emit8(0xb8 + reg_value(r));
        emit32le(uint32_t(imm));
        return;
    }

    // mov r64, imm32 — 7 bytes; sign-extends (e.g. -1)
    if (int64_t(imm) == int32_t(imm))
    {
    force32:
        orex(1, HW_NONE, r, 0xc7);
        emit8(byte(0xc0 | reg_value(r)));
        emit32le(uint32_t(imm));
        return;
    }

    // i386 only ever materializes the low 32 bits
    if (!ptr64())
    {
        record(x86_error, "Materializing 0x16llX in 32-bit mode", imm);
        imm = uint32_t(imm);
        goto force32;
    }

    // movabs r64, imm64 — 10 bytes
    emit8(0x48 | (rex_b(r) ? 1 : 0));
    emit8(0xb8 + reg_value(r));
    emit32le(uint32_t(imm));
    emit32le(uint32_t(imm >> 32));
}


x86::x86reg_t x86::load_fp(local_t loc, byte sz)
// ----------------------------------------------------------------------------
//   Load a float / double local into an xmm register
// ----------------------------------------------------------------------------
{
    record(x86, "load_%+s [%u]", sz == 64 ? "dbl" : "flt", loc);
    if (sz != 32 && sz != 64)
    {
        record(x86_error, "Invalid size %u in load_fp", sz);
        sz = 64;
    }

    x86reg_t r = hw(local(loc, sz == 32 ? RSPEC_FLT : RSPEC_DBL, true));
    record(x86, "load_%+s %u=[%u]", sz == 64 ? "dbl" : "flt", r, loc);
    return r;
}


void x86::store_fp(local_t loc, x86reg_t r)
// ----------------------------------------------------------------------------
//   Store an xmm register into a bytecode float local
// ----------------------------------------------------------------------------
{
    record(x86, "store_%+s [%u]=%u", r >= HW_XMM0F ? "flt" : "dbl", loc, r);
    store(loc, r);
}


void x86::load_local(local_t loc, x86reg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
//   Warning: conventional order is reverse between backend and x86
{
    record(x86, "load_local [%u]=%u", loc, r);
    load(r, loc);
}


void x86::store_local(x86reg_t r, local_t loc)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
//   Warning: conventional order is reverse between backend and x86
{
    record(x86, "store_local %u=[%u]", r, loc);
    store(loc, r);
}


void x86::mov_local(local_t dst, local_t src)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "mov_local [%u]=[%u]", dst, src);
    x86reg_t sreg = hw_local(src);
    x86reg_t dreg = hw_dst_local(dst);
    emit_mov_rr(dreg, sreg);
}


void x86::swap_locals(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(x86, "swap_locals %u and %u", d, s);
    x86reg_t sreg = hw_local(s);
    x86reg_t dreg = hw_local(d); // WARNING: Not dst
    emit_xchg_rr(dreg, sreg);
}


void x86::store_imm(local_t d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_imm [%u]=%llu %lld 0x%llX", d, v, v, v);
    x86reg_t dreg = hw_dst_local(d);
    set_imm(dreg, uint64_t(v));
}


void x86::store_float(local_t d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_flt [%u] = %g", d, v);
    x86reg_t dreg = hw(local(d, RSPEC_FLT, false));
    uint32_t bits = *((uint32_t *) &v);
    emit_set_xmm_imm(dreg, bits, false);
}


void x86::store_double(local_t d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(x86, "store_dbl [%u] = %g", d, v);
    x86reg_t dreg = hw(local(d, RSPEC_DBL, false));
    uint64_t bits = *((uint64_t *) &v);
    emit_set_xmm_imm(dreg, bits, true);
}


void x86::binop_local(local_t dloc, local_t sloc, op_t op, byte fp)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    hwspec_t spec = fp == 64 ? RSPEC_DBL : fp == 32 ? RSPEC_FLT : RSPEC_INT;
    x86reg_t d = hw(local(dloc, spec, false));
    x86reg_t s = hw(local(sloc, spec, true));
    record(x86, "binop_local%+s [%u]=[%u] reg %u=%u",
           opcode_name(op), dloc, sloc, d, s);
    switch (op)
    {
    case op_add:        emit_add_rr(d, s); break;
    case op_sub:        emit_sub_rr(d, s); break;
    case op_mul:
    case op_umul:       emit_imul_rr(d, s); break;
    case op_div:        emit_idiv_rr(d, s); break;
    case op_udiv:       emit_udiv_rr(d, s); break;
    case op_mod:        emit_imod_rr(d, s); break;
    case op_umod:       emit_umod_rr(d, s); break;
    case op_and:        emit_and_rr (d, s); break;
    case op_or:         emit_or_rr  (d, s); break;
    case op_xor:        emit_xor_rr (d, s); break;
    case op_shl:        emit_shift_rr(d, s, 0xe0); break;
    case op_shr:        emit_shift_rr(d, s, 0xf8); break;
    case op_ushr:       emit_shift_rr(d, s, 0xe8); break;
    case op_fadd:       emit_sse_scalar_op(0xf3, SSE_ADD, d, s); break;
    case op_fsub:       emit_sse_scalar_op(0xf3, SSE_SUB, d, s); break;
    case op_fmul:       emit_sse_scalar_op(0xf3, SSE_MUL, d, s); break;
    case op_fdiv:       emit_sse_scalar_op(0xf3, SSE_DIV, d, s); break;
    case op_dadd:       emit_sse_scalar_op(0xf2, SSE_ADD, d, s); break;
    case op_dsub:       emit_sse_scalar_op(0xf2, SSE_SUB, d, s); break;
    case op_dmul:       emit_sse_scalar_op(0xf2, SSE_MUL, d, s); break;
    case op_ddiv:       emit_sse_scalar_op(0xf2, SSE_DIV, d, s); break;
    default:
        record(x86_error, "binop_local called with op %u", op);
        break;
    }
}


void x86::binop_imm(local_t dloc, value_t v, op_t op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    x86reg_t d = hw_local(dloc);
    record(x86, "binop_imm%+s [%u] %lld reg %u",
           opcode_name(op), dloc, v, d);
    switch (op)
    {
    case op_addi:
    case op_addu:       emit_alu_imm(d, /*ADD*/ 0, v); break;
    case op_subi:
    case op_subu:       emit_alu_imm(d, /*SUB*/ 5, v); break;
    case op_muli:
    case op_mulu:       emit_imul_imm(d, v); break;
    case op_divi:       emit_idiv_imm(d, uint64_t(v)); break;
    case op_divu:       emit_udiv_imm(d, uint64_t(v)); break;
    case op_modi:       emit_imod_imm(d, uint64_t(v)); break;
    case op_modu:       emit_umod_imm(d, uint64_t(v)); break;
    case op_andu:       emit_alu_imm(d, /*AND*/ 4, v); break;
    case op_oru:        emit_alu_imm(d, /*OR*/  1, v); break;
    case op_xoru:       emit_alu_imm(d, /*XOR*/ 6, v); break;
    case op_shlu:       emit_shift_imm8(d, 0xe0, byte(v)); break;
    case op_shru:       emit_shift_imm8(d, 0xf8, byte(v)); break;
    case op_ushru:      emit_shift_imm8(d, 0xe8, byte(v)); break;
    default:
        record(x86_error, "binop_imm called with op %u", op);
        break;
    }
}


void x86::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(x86, "set_pred(%u, %d)", p, on);
    if (on)
        emit_pred_set_bit(p);
    else
        emit_pred_clear_bit(p);
}


void x86::cmp_local(byte p, local_t d, local_t s, byte jcc)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_local(d);
    x86reg_t sr = hw_local(s);
    record(x86, "cmp_local cc=%u p=%u d=%u r%u s=%u r%u", jcc, p, d, dr, s, sr);
    emit_pred_cmp(p, dr, sr, jcc);
}


void x86::cmp_imm(byte p, local_t d, value_t v, byte jcc)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_local(d);
    set_imm(HW_RCX, v);
    record(x86, "cmp_imm cc=%u p=%u d=%u r%u v=%u", jcc, p, d, dr, v);
    emit_pred_cmp(p, dr, HW_RCX, jcc);
}


void x86::cmp_uimm(byte p, local_t d, uvalue_t v, byte jcc)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), jcc);
}


void x86::cond_mov_local(byte p, local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(x86, "cond_mov_local(%u, %u, %u)", p, d, s);
    emit_pred_test_bit(p);      // BT -> CF
    hwcode_t off = emit_jcc_rel8(JCC_JNC);     // skip if bit clear
    mov_local(d, s);
    patch(off, cursor());
}


void x86::select_local(byte p, local_t d, local_t s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(x86, "select_local(%u, %u, %u, %u)", p, d, s, x);
    emit_pred_test_bit(p);                     // BT -> CF
    hwcode_t jnc_pos = emit_jcc_rel8(JCC_JNC); // bit clear -> pick x
    mov_local(d, s);
    hwcode_t jmp_pos = emit_jmp_rel8();
    patch(jnc_pos, cursor());
    mov_local(d, x);
    patch(jmp_pos, cursor());
}


void x86::neg_local(local_t d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(d);
    emit_grp3_f7(r, 0xd8);
    record(x86, "neg_local %u reg %u", d, r);
}


void x86::not_local(local_t d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(d);
    emit_grp3_f7(r, 0xd0);
    record(x86, "not_local %u reg %u", d, r);
}


void x86::incr_local(local_t d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(d);
    emit_grp5_ff(r, 0xc0);
    record(x86, "incr_local %u reg %u", d, r);
}


void x86::decr_local(local_t d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(d);
    emit_grp5_ff(r, 0xc8);
    record(x86, "decr_local %u reg %u", d, r);
}


void x86::sext_reg(x86reg_t r, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register r to w bits (shared by sext and load paths)
// ----------------------------------------------------------------------------
{
    record(x86, "sext_reg %u width %u", r, w);

    byte modrm = byte(0xc0 | (reg_value(r) << 3) | reg_value(r));
    byte rex   = byte(0x40 | (ptr64() ? 8 : 0)
                      | (rex_r(r) ? 4 : 0)
                      | (rex_b(r) ? 1 : 0));
    switch (w)
    {
    case 8:
        // movsx r64/r32, r8b — REX also required for sil/dil/bpl/spl
        if (ptr64() || r >= 4)
            emit8(rex);
        emit8(0x0f);
        emit8(0xbe);
        emit8(modrm);
        break;
    case 16:
        // movsx r64/r32, r16 — no 0x66
        if (ptr64() || r >= 8)
            emit8(rex);
        emit8(0x0f);
        emit8(0xbf);
        emit8(modrm);
        break;
    case 32:
        if (ptr64())
        {
            emit8(rex);   // REX.W + R/B for r8–r15
            emit8(0x63);  // movsxd r64, r32
            emit8(modrm);
        }
        break;
    default:
        if (w < 64)
        {
            emit_shift_imm8(r, 0xe0, byte(64 - w));
            emit_shift_imm8(r, 0xf8, byte(64 - w));
        }
        break;
    }
}


void x86::sext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "sext_local%u %u", w, d);
    if (w < 64)
    {
        x86reg_t r = hw_local(d);
        sext_reg(r, w);
    }
}


void x86::zext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(x86, "zext_local%u %u", w, d);
    if (w < 64)
    {
        x86reg_t r = hw_local(d);
        set_imm(HW_RAX, (1ULL << w) - 1);
        emit_and_rr(r, HW_RAX);
    }
}


void x86::store_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_local(d);
    x86reg_t sr = hw_local(s);
    record(x86, "store_mem%u [%u]=%u", sz, d, s);
    switch(sz)
    {
    default:            record(x86_error, "Invalid store size %u", sz);
    case 1:             emit_store_u8 (dr, sr, offs); break;
    case 2:             emit_store_u16(dr, sr, offs); break;
    case 4:             emit_store_u32(dr, sr, offs); break;
    case 8:             emit_store_u64(dr, sr, offs); break;
    }
}


void x86::load_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Load from address in local s into local d
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_local(d);
    x86reg_t sr = hw_local(s);
    record(x86, "load_mem%u %u=[%u] r%u=[r%u]", sz, d, s, dr, sr);
    switch(sz)
    {
    default:            record(x86_error, "Invalid load size %u", sz);
    case 1:             emit_load_u8 (dr, sr, offs); break;
    case 2:             emit_load_u16(dr, sr, offs); break;
    case 4:             emit_load_u32(dr, sr, offs); break;
    case 8:             emit_load_u64(dr, sr, offs); break;
    }
}


void x86::load_sext(local_t d, local_t s, byte w, value_t offs)
// ----------------------------------------------------------------------------
//   Load from [rax+offs] and sign-extend to w bits into rax
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_local(d);
    x86reg_t sr = hw_local(s);
    size_t sz = (w + 7) / 8;
    record(x86, "load_sext%u %u=[%u] r%u=[r%u] sz %u", w, d, s, dr, sr, sz);
    switch(sz)
    {
    default:            record(x86_error, "Invalid load_sext width %u", w);
    case 1:             emit_load_s8 (dr, sr, offs); break;
    case 2:             emit_load_s16(dr, sr, offs); break;
    case 4:             emit_load_s32(dr, sr, offs); break;
    case 8:             emit_load_u64(dr, sr, offs); break;
    }
    if (w & 7)
        sext_reg(dr, w);
}


void x86::bra_pred(byte p, byte_p target, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(x86, "bra_%+s %u, %lld", sense ? "true" : "false" ,p, target);
    if (ptr64())
        hw_sys_arg_reload(target);
    emit_pred_test_bit(p);                              // BT -> CF
    hwcode_t pos = emit_jcc_rel32(sense ? JCC_JC : JCC_JNC);
    rel32_branch(target, pos);
}


void x86::bra_test(local_t s, byte_p target, byte cc)
// ----------------------------------------------------------------------------
//   Emit signed comparison branch on local s
// ----------------------------------------------------------------------------
{
    record(x86, "bra_test %u, %lld cc=%u", s, target, cc);
    if (ptr64())
        hw_sys_arg_reload(target);
    x86reg_t r = hw_local(s);
    emit_test_rr(r, r);
    hwcode_t pos = emit_jcc_rel32(cc);
    rel32_branch(target, pos);
}


void x86::rel32_branch(byte_p target, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a rel32 site to bytecode target (Intel −4 via patch)
// ----------------------------------------------------------------------------
{
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off);
}


void x86::rel32_function(tok_t tok, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a rel32 site to a function token (Intel −4 via patch)
// ----------------------------------------------------------------------------
{
    intptr_t off = function(tok, site);
    if (off)
        patch(site, site + off);
}


void x86::prologue()
// ----------------------------------------------------------------------------
//   Push callee-saved registers; each JIT function gets its own set
// ----------------------------------------------------------------------------
//   On amd64 the 5 pushes (40 bytes) plus the 8-byte return address give
//   48 bytes total, keeping RSP 16-byte aligned inside the function body.
//   jmp/ijmp pop these before jumping so the target function's prologue
//   can push its own set without stack growth.
{
    record(x86, "prologue");

    if (ptr64())
    {
        emit_push_reg(HW_RBX);
        emit_push_reg(HW_R12);
        emit_push_reg(HW_R13);
        emit_push_reg(HW_R14);
        emit_push_reg(HW_R15);
    }
    else
    {
        emit_push_reg(HW_RBX);
        emit_push_reg(HW_RSI);
        emit_push_reg(HW_RDI);
    }
}


void x86::epilogue()
// ----------------------------------------------------------------------------
//   Pop prologue callee-saves and return
// ----------------------------------------------------------------------------
{
    record(x86, "epilogue()");
    if (ptr64())
    {
        emit_pop_reg(HW_R15);
        emit_pop_reg(HW_R14);
        emit_pop_reg(HW_R13);
        emit_pop_reg(HW_R12);
        emit_pop_reg(HW_RBX);
    }
    else
    {
        emit_pop_reg(HW_RDI);
        emit_pop_reg(HW_RSI);
        emit_pop_reg(HW_RBX);
    }
    emit_insn_ret();
}


void x86::call_ptr(void (*fn)())
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in RAX
// ----------------------------------------------------------------------------
{
    record(x86, "call_ptr %p", fn);
    // Microsoft x64: caller provides 32 bytes of register parameter home space
    if (win64_abi())
        emit_sub_rsp_imm(0x20);
    set_imm(HW_RAX, uint64_t(fn));
    emit_call_rr(HW_RAX);
    if (win64_abi())
        emit_add_rsp_imm(0x20);
}


void x86::invoke(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated x86 code with FP = initial frame and flags in HW_FLAGS
// ----------------------------------------------------------------------------
{
#if defined(__x86_64__) || defined(__amd64__)
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    alignas(16) ularge_t initial_frame[DEFAULT_FRAME_SIZE + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    asm volatile(
        "mov %1, %%rbp\n\t"
        "mov %2, %%r11\n\t"
        "call *%0"
        :
        : "r"(entry), "r"(frame), "r"(flags)
        : "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11", "rbp",
          "memory", "cc");
#elif defined(__i386__) || defined(__i386)
    typedef void (*entry_fn)(void);
    entry_fn  entry  = (entry_fn) code;
    alignas(16) ularge_t initial_frame[DEFAULT_FRAME_SIZE + 2];
    uintptr_t framep = uintptr_t(initial_jit_fp(initial_frame));
    uint32_t  flags  = uint32_t(rt.flags);
    asm volatile(
        "mov %1, %%ebp\n\t"
        "call *%0"
        :
        : "r"(entry), "r"(framep), "S"(flags)
        : "eax", "ecx", "edx", "ebp", "memory", "cc");
#else
    (void) code;
    rt.error("Cannot invoke x86 JIT on this host");
#endif
}


void x86::dispatch(local_t loc, byte_p &pc, byte jcc)
// ----------------------------------------------------------------------------
//   Dispatch a branch table
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();
    x86reg_t ref = hw_local(loc);
    size_t   ncases  = leb128<size_t>(pc);
    for (size_t n = 0; n < ncases; n++)
    {
        value_t test = leb128<value_t>(pc);
        byte_p  bpc  = pc;
        broff_t offs = leb128<broff_t>(pc);
        byte_p  tgt  = bpc + offs;
        if (ptr64())
            hw_sys_arg_reload(tgt);
        set_imm(HW_RAX, test);
        emit_cmp_rr(ref, HW_RAX);
        hwcode_t pos = emit_jcc_rel32(jcc);
        rel32_branch(tgt, pos);
    }
}



#define EMIT(op)        void x86::emit_##op(emit_args &e)

#include "x86-emit.cpp"
