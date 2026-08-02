// ****************************************************************************
//  arm64.cpp                                                      XLR8 project
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
//   This software is licensed under the terms outlined in the LICENSE.txt file
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

#include "leb128.h"
#include "native-bridge.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <strings.h> // strcasestr


RECORDER(arm64,             32, "AArch64 code generation helpers");
RECORDER(arm64_ops,         32, "AArch64 opcode emission");
RECORDER(arm64_error,        8, "AArch64 code generation errors");
RECORDER(arm64_warning,      8, "AArch64 code generation warnings");


arm64::arm64(runtime &rt)
// ----------------------------------------------------------------------------
//   Construct an AArch64 code generator
// ----------------------------------------------------------------------------
    : backend(rt, ARCH_ARM64),
      local_frame_size(0),
      spilled_args(0),
      num_functions(0),
      frame_entered(false)
{
    record(arm64, "New backend arch=%+s", arch_name(ARCH_ARM64));
}


// ============================================================================
//
//   Backend standard interface
//
// ============================================================================

cstring arm64::machine_defs() const
// ----------------------------------------------------------------------------
//   Machine definition for AArch64
// ----------------------------------------------------------------------------
{
    return "__aarch64__\0__ARM_ARCH_ISA_A64\0";
}


uint arm64::registers() const
// ----------------------------------------------------------------------------
//   Return the number of hardware registers on this platform
// ----------------------------------------------------------------------------
{
    return HW_COUNT;
}


hwreg_t arm64::register_for(local_t loc, hwspec_t spec, bool ld)
// ----------------------------------------------------------------------------
//   Find the register associated to the local
// ----------------------------------------------------------------------------
{
    // Check the register range to look at
    bool isint = spec & RSPEC_INT;
    bool isflt = spec & RSPEC_FLT;
    bool isdbl = spec & RSPEC_DBL;
    if (!isint && !isflt && !isdbl)
    {
        record(arm64_error, "Invalid register specification 0x%08X", spec);
        return NO_REGISTER;
    }

    // Select register range and check if we can allocate statically
    uint first = isint ? HW_X0 : isdbl ? HW_V0D : isflt ? HW_V0F : HW_COUNT;
    uint count = isint ? HW_SHADOWS : HW_FSHADOWS;
    uint last  = first + count;

    // Reserved registers (don't touch)
    if (loc < count && loc != HW_RESERVED)
    {
        if (ld)
        {
            if (isflt)
                emit32le(fmov_s_w(loc, loc));
            else if (isdbl)
                emit32le(fmov_d_x(loc, loc));
        }
        return hwreg_t(first + loc);
    }

    // We need a scratch register
    for (uint tmp = last; tmp < last + HW_TEMPS; tmp++)
    {
        local_t rloc = reg_local[tmp];
        if (rloc == NO_LOCAL || rloc == loc)
        {
            if (ld)
                load(tmp, loc);
            return tmp;
        }
    }

    record(arm64_error, "No register available for spec 0x%08X", spec);
    return NO_REGISTER;
}



struct regspill_compare
// ----------------------------------------------------------------------------
//   Comparator for regspills based on bytecode, spilled or lexicographic
// ----------------------------------------------------------------------------
{
    bool operator()(const arm64::regspill &t, byte_p bc)
    {
        return t.bytecode < bc;
    }
    bool operator()(byte_p bc, const arm64::regspill &t)
    {
        return bc < t.bytecode;
    }
    bool operator()(const arm64::regspill &t, uint mc)
    {
        return t.spilled < mc;
    }
    bool operator()(uint mc, const arm64::regspill &t)
    {
        return mc < t.spilled;
    }
    bool operator()(const arm64::regspill &t1,
                    const arm64::regspill &t2)
    {
        return (t1.bytecode < t2.bytecode ||
                (t1.bytecode == t2.bytecode && t1.spilled < t2.spilled));
    }
};


template<typename T>
auto arm64::lower_bound(const regspills &t, T v)
// ----------------------------------------------------------------------------
//   Find the lower bound by bytecode
// ----------------------------------------------------------------------------
{
    return std::lower_bound(t.begin(), t.end(), v, regspill_compare{});
}


template<typename T>
auto arm64::equal_range(const regspills &t, T v)
// ----------------------------------------------------------------------------
//   Find the lower bound by bytecode
// ----------------------------------------------------------------------------
{
    return std::equal_range(t.begin(), t.end(), v, regspill_compare{});
}


uint arm64::label(byte_p bytecode)
// ----------------------------------------------------------------------------
//   Record the spill status for each bytecode address
// ----------------------------------------------------------------------------
{
    uint brcount = backend::label(bytecode);

    // If already translated, return the offset to the translation
    auto spill  = lower_bound(spills, bytecode);
    if (spill == spills.end() || spill->bytecode != bytecode)
        spills.emplace(spill, regspill{ bytecode, spilled_args });

    return brcount;
}


void arm64::register_sync(byte_p pc)
// ----------------------------------------------------------------------------
//   Synchronize the temporaries
// ----------------------------------------------------------------------------
{
    // Spill anything that was loaded in a temporary
    for (uint t = 0; t < HW_TEMPS; t++)
        spill(HW_TEMP0 + t) || spill(HW_DTEMP0 + t) || spill(HW_FTEMP0 + t);

    // Move floating point values back into X registers (durable home).
    for (int sz = 0; sz < 2; sz++)
    {
        hwreg_t base = sz ? HW_V0D : HW_V0F;
        for (uint fp = 0; fp < HW_FSHADOWS; fp++)
        {
            local_t loc = reg_local[base + fp];
            if (loc != NO_LOCAL)
            {
                emit32le(sz ? fmov_x_d(fp, fp) : fmov_w_s(fp, fp));
                local_reg[loc]        = HW_X0 + fp;
                reg_local[HW_X0 + fp] = loc;
                reg_local[base + fp]  = NO_LOCAL;
            }
        }
    }

    // Check if we have a branch pointing here. If so reload everything
    // before we patch the branches. Forward branches are always unspilled.
    if (label_branches(pc))
        hw_sys_arg_reload();
}


uint arm64::hw_spills_at(byte_p target)
// ----------------------------------------------------------------------------
//   Return the value of spills at a given known branch
// ----------------------------------------------------------------------------
{
    auto spill = lower_bound(spills, target);
    if (spill != spills.end() && spill->bytecode == target)
        return spill->spilled;
    return 0;
}


void arm64::hw_sys_arg_reload(byte_p target)
// ----------------------------------------------------------------------------
//   Reload arguments we spilled before branching to given target
// ----------------------------------------------------------------------------
{
    uint dspill = hw_spills_at(target);
    uint cspill = spilled_args;
    uint diffs = dspill ^ cspill;
    if (diffs)
    {
        uint frame = local_frame_size;
        if (frame > HW_CLOBBERED)
            frame = HW_CLOBBERED;
        for (uint r = 0; r < frame; r += 2)
        {
            arm64reg_t r1   = arm64reg_t(r);
            arm64reg_t r2   = arm64reg_t(r + 1);
            size_t     offs = r * sizeof(ularge_t);
            uint       ds   = (dspill >> r) & 3;
            uint       cs   = (cspill >> r) & 3;
            uint       dd   = (diffs >> r) & 3;

            // Bit set  = value is in spill slot (may need load after clobber).
            // Bit clear = value live/dirty in the register (may need store).
            // When bits differ: dest has the bit → store; else load.
            // Deal with mixed cases, e.g. one load and one store
            switch (dd)
            {
            case 0: break;
            case 1:
                if (ds & dd)
                    emit_store_off(HW_SP, r1, offs);
                else
                    emit_load_off(r1, HW_SP, offs);
                break;
            case 2:
                if (ds & dd)
                    emit_store_off(HW_SP, r2, offs+8);
                else
                    emit_load_off(r2, HW_SP, offs+8);
                break;
            case 3:
                if ((ds & dd) == dd)
                {
                    emit32le(stp_off(r1, r2, HW_SP, offs));
                }
                else if ((cs & dd) == dd)
                {
                    emit32le(ldp_off(r1, r2, HW_SP, offs));
                }
                else if (ds & 1)
                {
                    emit_store_off(HW_SP, r1, offs);
                    emit_load_off(r2, HW_SP, offs+8);
                }
                else
                {
                    emit_load_off(r1, HW_SP, offs);
                    emit_store_off(HW_SP, r2, offs+8);
                }
                break;
            }
        }

        if ((diffs >> HW_FRAME) & 3)
        {
            if ((dspill >> HW_FRAME) & 3)
            {
                emit32le(sub_imm(HW_SP, HW_SP, 32 * sizeof(ularge_t)));
                emit32le(stp_off(HW_FRAME, HW_LR,
                                 HW_SP, HW_FRAME * sizeof(ularge_t)));

            }
            else
            {
                emit32le(ldp_off(HW_FRAME,HW_LR,
                                 HW_SP,HW_FRAME*sizeof(ularge_t)));
                emit32le(add_imm(HW_SP, HW_SP, 32 * sizeof(ularge_t)));
            }
        }
        spilled_args = dspill;
    }
}




uint arm64::pointer_size() const
// ----------------------------------------------------------------------------
//   The platform is 64-bit
// ----------------------------------------------------------------------------
{
    return 8;
}


void arm64::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs used by the JIT
// ----------------------------------------------------------------------------
{
    record(arm64, "begin_translation callee=%u", callee_entry);
    backend::begin_translation();
    spilled_args  = 0;
    frame_entered = false;

    // Logical 8-slot frame for spill/respill until bytecode enter says otherwise.
    // Top-level also saves FP/LR once for the invoke wrapper; callees keep the
    // return address in x30 and preserve it via the C-call spill area.
    local_frame_size = DEFAULT_FRAME_SIZE;
    if (num_functions == 0)
        emit32le(stp_pre(HW_FRAME, HW_LR, HW_SP, -16));
}


void arm64::finish_translation()
// ----------------------------------------------------------------------------
//   Restore callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(arm64, "finish_translation");
    backend::finish_translation();
    num_functions++;
}


void arm64::patch(hwcode_t pos, hwcode_t pvalue, uint flags)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (B / B.cond / CBZ / TBZ) to jump to target
// ----------------------------------------------------------------------------
{
    record(arm64, "patch pos=%llu target=%llu flags=0x%X", pos, pvalue, flags);

    section &sec      = code();
    int64_t  byte_off = int64_t(pvalue) - int64_t(pos);
    if (byte_off & 3)
    {
        record(arm64_error, "Unaligned branch patch off=%lld", byte_off);
        return;
    }
    int32_t  insn_off = int32_t(byte_off / 4);

    uint32_t insn     = sec.value32le(pos);
    uint32_t op       = insn & 0xff000000u;

    if (op == 0x14000000u || op == 0x94000000u)
    {
        // B / BL imm26
        if (insn_off < -(1 << 25) || insn_off >= (1 << 25))
        {
            record(arm64_error, "B/BL out of range off=%d", insn_off);
            return;
        }
        insn = (op == 0x94000000u) ? bl_imm(insn_off) : b_imm(insn_off);
    }
    else if ((insn & 0xff000010u) == 0x54000000u)
    {
        // B.cond imm19
        if (insn_off < -(1 << 18) || insn_off >= (1 << 18))
        {
            record(arm64_error, "B.cond out of range off=%d", insn_off);
            return;
        }
        insn = b_cond_imm(byte(insn & 0xfu), insn_off);
    }
    else if (op == 0xb4000000u || op == 0xb5000000u)
    {
        // CBZ / CBNZ imm19
        if (insn_off < -(1 << 18) || insn_off >= (1 << 18))
        {
            record(arm64_error, "CBZ/CBNZ out of range off=%d", insn_off);
            return;
        }
        uint rt = insn & 0x1fu;
        insn    = (op == 0xb4000000u) ? cbz_imm(rt, insn_off)
                                      : cbnz_imm(rt, insn_off);
    }
    else if ((insn & 0x7f000000u) == 0x36000000u ||
             (insn & 0x7f000000u) == 0x37000000u)
    {
        // TBZ / TBNZ imm14
        if (insn_off < -(1 << 13) || insn_off >= (1 << 13))
        {
            record(arm64_error, "TBZ/TBNZ out of range off=%d", insn_off);
            return;
        }
        uint rt  = insn & 0x1fu;
        uint bit = ((insn >> 26) & 0x20u) | ((insn >> 19) & 0x1fu);
        insn     = ((insn & 0x7f000000u) == 0x36000000u)
                     ? tbz_imm(rt, bit, insn_off)
                     : tbnz_imm(rt, bit, insn_off);
    }
    else
    {
        record(arm64_error, "Unknown branch insn 0x%08X at %llu", insn, pos);
        return;
    }

    sec.patch32le(pos, insn);
}


void arm64::call(void (*target)())
// ----------------------------------------------------------------------------
//   Emit a direct call to a C function pointer
// ----------------------------------------------------------------------------
{
    record(arm64, "call %p", target);
    call_ptr(target);
}


void arm64::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Spill all locals then call a runtime helper
// ----------------------------------------------------------------------------
{
    record(arm64, "syscall %p", target);
    call_ptr(target);
}


uint arm64::relocate(void *base)
// ----------------------------------------------------------------------------
//   Relocate absolute addresses after mapping the code
// ----------------------------------------------------------------------------
// Arm64 currently emits PC-relative / absolute via movz/movk sequences;
// no deferred absolute reloc list is required for the smoke path.
{
    return 0;
}


bool arm64::move(hwreg_t to, hwreg_t from)
// ----------------------------------------------------------------------------
//   Move between registers (GPR↔GPR, V↔V, or int↔fp via fmov)
// ----------------------------------------------------------------------------
{
    arm64reg_t d = hw(to);
    arm64reg_t s = hw(from);
    if (d == s)
        return true;

    if (is_fp(d) && is_fp(s))
        if (is_flt(d) || is_flt(s))
            emit32le(fmov_ss(vn(d), vn(s)));
        else
            emit32le(fmov_dd(vn(d), vn(s)));
    else if (is_fp(d))
        if (is_flt(d))
            emit32le(fmov_s_w(vn(d), xn(s)));
        else
            emit32le(fmov_d_x(vn(d), xn(s)));
    else if (is_fp(s))
        if (is_flt(s))
            emit32le(fmov_w_s(xn(d), vn(s)));
        else
            emit32le(fmov_x_d(xn(d), vn(s)));
    else
        emit32le(mov_rr(xn(d), xn(s)));
    return true;
}


bool arm64::load(hwreg_t h, local_t n)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register r
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw(h);
    record(arm64, "load %u=[%u]", r, n);

    int disp = local_disp(n);
    if (is_fp(r))
    {
        // Load via scratch GPR then fmov into V
        emit_load_off(HW_SCRATCH, HW_FRAME, disp);
        if (is_flt(r))
            emit32le(fmov_s_w(vn(r), xn(HW_SCRATCH)));
        else
            emit32le(fmov_d_x(vn(r), xn(HW_SCRATCH)));
    }
    else
    {
        emit_load_off(r, HW_FRAME, disp);
    }
    return true;
}


bool arm64::store(local_t n, hwreg_t h)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw(h);
    record(arm64, "store [%u]=%u", n, r);
    int disp = local_disp(n);

    if (is_fp(r))
    {
        if (is_flt(r))
            emit32le(fmov_w_s(xn(HW_SCRATCH), vn(r)));
        else
            emit32le(fmov_x_d(xn(HW_SCRATCH), vn(r)));
        emit_store_off(HW_FRAME, HW_SCRATCH, disp);
    }
    else
    {
        emit_store_off(HW_FRAME, r, disp);
    }
    saved(n, h);
    return true;
}


arm64::reginfo arm64::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for AArch64
// ----------------------------------------------------------------------------
{
    // Direct map: local n ↔ xn / vn (see register_for). RSPEC_ARG marks
    // AAPCS64 arg/return regs; RSPEC_CALL marks callee-saved. Scratch x27 and
    // flags x28 remain named roles but still appear in the table for dumps.
    // x29 FRAME, x30 LR, sp: not allocatable (spec 0).
    static const reginfo arm64_registers[arm64::HW_COUNT] = {
        // x0–x7: AAPCS64 args/return — also direct homes for locals 0–7.
        {   "x0",  RSPEC_ARG | RSPEC_INT },
        {   "x1",  RSPEC_ARG | RSPEC_INT },
        {   "x2",  RSPEC_ARG | RSPEC_INT },
        {   "x3",  RSPEC_ARG | RSPEC_INT },
        {   "x4",  RSPEC_ARG | RSPEC_INT },
        {   "x5",  RSPEC_ARG | RSPEC_INT },
        {   "x6",  RSPEC_ARG | RSPEC_INT },
        {   "x7",  RSPEC_ARG | RSPEC_INT },
        {   "x8",              RSPEC_INT },
        {   "x9",              RSPEC_INT },
        {  "x10",              RSPEC_INT },
        {  "x11",              RSPEC_INT },
        {  "x12",              RSPEC_INT },
        {  "x13",              RSPEC_INT },
        {  "x14",              RSPEC_INT },
        {  "x15",              RSPEC_INT },
        {  "x16",              RSPEC_INT },
        {  "x17",              RSPEC_INT },
        {  "x18",              RSPEC_INT },
        {  "x19", RSPEC_CALL | RSPEC_INT },
        {  "x20", RSPEC_CALL | RSPEC_INT },
        {  "x21", RSPEC_CALL | RSPEC_INT },
        {  "x22", RSPEC_CALL | RSPEC_INT },
        {  "x23", RSPEC_CALL | RSPEC_INT },
        {  "x24", RSPEC_CALL | RSPEC_INT }, // HW_NONLOCAL
        {  "x25", RSPEC_CALL | RSPEC_INT },
        {  "x26", RSPEC_CALL | RSPEC_INT },
        {  "x27", RSPEC_CALL | RSPEC_INT }, // HW_SCRATCH
        {  "x28", RSPEC_CALL | RSPEC_INT }, // HW_FLAGS
        {  "x29",                      0 }, // HW_FRAME
        {  "x30",                      0 }, // HW_LR
        {   "sp",                      0 },

        {  "v0d",              RSPEC_DBL },
        {  "v1d",              RSPEC_DBL },
        {  "v2d",              RSPEC_DBL },
        {  "v3d",              RSPEC_DBL },
        {  "v4d",              RSPEC_DBL },
        {  "v5d",              RSPEC_DBL },
        {  "v6d",              RSPEC_DBL },
        {  "v7d",              RSPEC_DBL },
        {  "v8d", RSPEC_CALL | RSPEC_DBL },
        {  "v9d", RSPEC_CALL | RSPEC_DBL },
        { "v10d", RSPEC_CALL | RSPEC_DBL },
        { "v11d", RSPEC_CALL | RSPEC_DBL },
        { "v12d", RSPEC_CALL | RSPEC_DBL },
        { "v13d", RSPEC_CALL | RSPEC_DBL },
        { "v14d", RSPEC_CALL | RSPEC_DBL },
        { "v15d", RSPEC_CALL | RSPEC_DBL },

        {  "v0f",              RSPEC_FLT },
        {  "v1f",              RSPEC_FLT },
        {  "v2f",              RSPEC_FLT },
        {  "v3f",              RSPEC_FLT },
        {  "v4f",              RSPEC_FLT },
        {  "v5f",              RSPEC_FLT },
        {  "v6f",              RSPEC_FLT },
        {  "v7f",              RSPEC_FLT },
        {  "v8f", RSPEC_CALL | RSPEC_FLT },
        {  "v9f", RSPEC_CALL | RSPEC_FLT },
        { "v10f", RSPEC_CALL | RSPEC_FLT },
        { "v11f", RSPEC_CALL | RSPEC_FLT },
        { "v12f", RSPEC_CALL | RSPEC_FLT },
        { "v13f", RSPEC_CALL | RSPEC_FLT },
        { "v14f", RSPEC_CALL | RSPEC_FLT },
        { "v15f", RSPEC_CALL | RSPEC_FLT },
    };

    return reg < HW_COUNT ? arm64_registers[reg] : reginfo{ "INVALID", 0 };
}


// ============================================================================
//
//   Internal helpers
//
// ============================================================================

void arm64::emit_load_off(arm64reg_t rd, arm64reg_t rn, int offs)
// ----------------------------------------------------------------------------
//   Load 64-bit value from [rn, #offs] (LDUR for negative / unaligned)
// ----------------------------------------------------------------------------
{
    uint d = xn(rd);
    uint n = xn(rn);
    if (offs >= 0 && (offs % 8) == 0 && (offs / 8) <= 0xfff)
    {
        emit32le(ldr_off(d, n, uint(offs / 8)));
    }
    else if (offs >= -256 && offs <= 255)
    {
        emit32le(ldur_imm(d, n, offs));
    }
    else
    {
        // Large offset: add scratch base
        set_imm(HW_SCRATCH, uint64_t(int64_t(offs)));
        emit32le(add_rrr(xn(HW_SCRATCH), n, xn(HW_SCRATCH)));
        emit32le(ldr_idx(d, xn(HW_SCRATCH)));
    }
}


void arm64::emit_store_off(arm64reg_t rn, arm64reg_t rt, int offs)
// ----------------------------------------------------------------------------
//   Store 64-bit value to [rn, #offs] (STUR for negative / unaligned)
// ----------------------------------------------------------------------------
{
    uint n = xn(rn);
    uint t = xn(rt);
    if (offs >= 0 && (offs % 8) == 0 && (offs / 8) <= 0xfff)
    {
        emit32le(str_off(t, n, uint(offs / 8)));
    }
    else if (offs >= -256 && offs <= 255)
    {
        emit32le(stur_imm(t, n, offs));
    }
    else
    {
        set_imm(HW_SCRATCH, uint64_t(int64_t(offs)));
        emit32le(add_rrr(xn(HW_SCRATCH), n, xn(HW_SCRATCH)));
        emit32le(str_idx(t, xn(HW_SCRATCH)));
    }
}


void arm64::emit_push(arm64reg_t r)
// ----------------------------------------------------------------------------
//   Push a register (16-byte aligned: stp r, xzr, [sp, #-16]!)
// ----------------------------------------------------------------------------
{
    emit32le(stp_pre(xn(r), 31 /*xzr*/, xn(HW_SP), -16));
}


void arm64::emit_pop(arm64reg_t r)
// ----------------------------------------------------------------------------
//   Pop a register (ldp r, xzr, [sp], #16)
// ----------------------------------------------------------------------------
{
    emit32le(ldp_post(xn(r), 31 /*xzr*/, xn(HW_SP), 16));
}


hwcode_t arm64::emit_b_rel()
// ----------------------------------------------------------------------------
//   Emit placeholder B #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(b_imm(0));
    return site;
}


hwcode_t arm64::emit_bl_rel()
// ----------------------------------------------------------------------------
//   Emit placeholder BL #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(bl_imm(0));
    return site;
}


hwcode_t arm64::emit_bcond_rel(byte cond)
// ----------------------------------------------------------------------------
//   Emit placeholder B.cond #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(b_cond_imm(cond, 0));
    return site;
}


hwcode_t arm64::emit_cbz_rel(arm64reg_t rt)
// ----------------------------------------------------------------------------
//   Emit placeholder CBZ rt, #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(cbz_imm(xn(rt), 0));
    return site;
}


hwcode_t arm64::emit_cbnz_rel(arm64reg_t rt)
// ----------------------------------------------------------------------------
//   Emit placeholder CBNZ rt, #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(cbnz_imm(xn(rt), 0));
    return site;
}


hwcode_t arm64::emit_tbz_rel(arm64reg_t rt, uint bit)
// ----------------------------------------------------------------------------
//   Emit placeholder TBZ rt, #bit, #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(tbz_imm(xn(rt), bit, 0));
    return site;
}


hwcode_t arm64::emit_tbnz_rel(arm64reg_t rt, uint bit)
// ----------------------------------------------------------------------------
//   Emit placeholder TBNZ rt, #bit, #0; return site for patch
// ----------------------------------------------------------------------------
{
    hwcode_t site = cursor();
    emit32le(tbnz_imm(xn(rt), bit, 0));
    return site;
}


void arm64::set_imm(arm64reg_t r, uint64_t v)
// ----------------------------------------------------------------------------
//   Load a 64-bit constant into register r (MOVZ / MOVN + MOVK)
// ----------------------------------------------------------------------------
{
    uint rd = xn(r);
    record(arm64, "set_imm %u = 0x%llX", rd, v);

    uint64_t inv = ~v;
    int      nz = 0, nz_inv = 0;
    for (int i = 0; i < 4; i++)
    {
        if (((v >> (16 * i)) & 0xffffu) != 0)
            nz++;
        if (((inv >> (16 * i)) & 0xffffu) != 0)
            nz_inv++;
    }

    if (nz_inv < nz)
    {
        int start = 0;
        for (int i = 0; i < 4; i++)
        {
            if (((v >> (16 * i)) & 0xffffu) != 0xffffu)
            {
                start = i;
                break;
            }
        }
        uint shift = uint(start) * 16;
        emit32le(movn(rd, uint32_t((inv >> shift) & 0xffffu), shift));
        for (int i = 0; i < 4; i++)
        {
            if (i == start)
                continue;
            uint16_t hw = uint16_t((v >> (16 * i)) & 0xffffu);
            if (hw != 0xffffu)
                emit32le(movk(rd, hw, uint(i) * 16));
        }
        return;
    }

    emit32le(movz(rd, uint32_t(v) & 0xffffu));
    if ((v >> 16) & 0xffffu)
        emit32le(movk(rd, uint32_t(v >> 16), 16));
    if ((v >> 32) & 0xffffu)
        emit32le(movk(rd, uint32_t(v >> 32), 32));
    if ((v >> 48) & 0xffffu)
        emit32le(movk(rd, uint32_t(v >> 48), 48));
}


arm64::arm64reg_t arm64::load_fp(local_t loc, byte sz)
// ----------------------------------------------------------------------------
//   Allocate an FP register for local and ensure it holds the value
// ----------------------------------------------------------------------------
{
    hwspec_t   spec = sz == 64 ? RSPEC_DBL : RSPEC_FLT;
    arm64reg_t r    = hw_local(loc, spec);
    record(arm64, "load_fp%u [%u]=%u", sz, loc, r);
    return r;
}


void arm64::store_fp(local_t loc, arm64reg_t r)
// ----------------------------------------------------------------------------
//   Mark FP register as holding local (already in register)
// ----------------------------------------------------------------------------
{
    record(arm64, "store_fp [%u]=%u", loc, r);
    store(loc, r);
}


void arm64::load_local(local_t loc, arm64reg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    record(arm64, "load_local [%u]->%u", loc, r);
    load(r, loc);
}


void arm64::store_local(arm64reg_t r, local_t loc)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_local %u->[%u]", r, loc);
    store(loc, r);
}


void arm64::mov_local(local_t dst, local_t src)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm64, "mov_local [%u]=[%u]", dst, src);
    arm64reg_t sreg = hw_local(src);
    arm64reg_t dreg = hw_dst_local(dst);
    move(dreg, sreg);
}


void arm64::swap_locals(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(arm64, "swap_locals %u and %u", d, s);
    arm64reg_t sr = hw_rw_local(s);
    arm64reg_t dr = hw_rw_local(d);
    emit32le(mov_rr(xn(HW_SCRATCH), xn(dr)));
    emit32le(mov_rr(xn(dr), xn(sr)));
    emit32le(mov_rr(xn(sr), xn(HW_SCRATCH)));
}


void arm64::store_imm(local_t d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_imm [%u]=%lld", d, v);
    arm64reg_t dreg = hw_dst_local(d);
    set_imm(dreg, uint64_t(v));
}


void arm64::store_float(local_t d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_flt [%u] = %g", d, v);
    arm64reg_t dreg = hw_dst_local(d, RSPEC_FLT);
    uint32_t   bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    set_imm(HW_SCRATCH, bits);
    emit32le(fmov_s_w(vn(dreg), xn(HW_SCRATCH)));
}


void arm64::store_double(local_t d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(arm64, "store_dbl [%u] = %g", d, v);
    arm64reg_t dreg = hw_dst_local(d, RSPEC_DBL);
    uint64_t   bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    set_imm(HW_SCRATCH, bits);
    emit32le(fmov_d_x(vn(dreg), xn(HW_SCRATCH)));
}


void arm64::binop_local(local_t dloc, local_t sloc, op_t op, byte fp)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    hwspec_t   spec = fp == 64 ? RSPEC_DBL : fp == 32 ? RSPEC_FLT : RSPEC_INT;
    arm64reg_t s    = hw_local(sloc, spec);
    arm64reg_t d    = hw_dst_local(dloc, spec);

    record(arm64,
           "binop_local%+s [%u]=[%u] reg %u=%u",
           opcode_name(op),
           dloc,
           sloc,
           d,
           s);

    switch (op)
    {
    case op_add: emit32le(add_rrr(xn(d), xn(d), xn(s))); break;
    case op_sub: emit32le(sub_rrr(xn(d), xn(d), xn(s))); break;
    case op_mul:
    case op_umul: emit32le(mul_rrr(xn(d), xn(d), xn(s))); break;
    case op_div: emit32le(sdiv_rrr(xn(d), xn(d), xn(s))); break;
    case op_udiv: emit32le(udiv_rrr(xn(d), xn(d), xn(s))); break;
    case op_mod:
        emit32le(sdiv_rrr(xn(HW_SCRATCH), xn(d), xn(s)));
        emit32le(msub_rrr(xn(d), xn(HW_SCRATCH), xn(s), xn(d)));
        break;
    case op_umod:
        emit32le(udiv_rrr(xn(HW_SCRATCH), xn(d), xn(s)));
        emit32le(msub_rrr(xn(d), xn(HW_SCRATCH), xn(s), xn(d)));
        break;
    case op_and: emit32le(and_rrr(xn(d), xn(d), xn(s))); break;
    case op_or: emit32le(orr_rrr(xn(d), xn(d), xn(s))); break;
    case op_xor: emit32le(eor_rrr(xn(d), xn(d), xn(s))); break;
    case op_shl: emit32le(lsl_rrr(xn(d), xn(d), xn(s))); break;
    case op_shr: emit32le(asr_rrr(xn(d), xn(d), xn(s))); break;
    case op_ushr: emit32le(lsr_rrr(xn(d), xn(d), xn(s))); break;
    case op_fadd: emit32le(fop_ss(FOP_ADD, vn(d), vn(d), vn(s))); break;
    case op_fsub: emit32le(fop_ss(FOP_SUB, vn(d), vn(d), vn(s))); break;
    case op_fmul: emit32le(fop_ss(FOP_MUL, vn(d), vn(d), vn(s))); break;
    case op_fdiv: emit32le(fop_ss(FOP_DIV, vn(d), vn(d), vn(s))); break;
    case op_dadd: emit32le(fop_dd(FOP_ADD, vn(d), vn(d), vn(s))); break;
    case op_dsub: emit32le(fop_dd(FOP_SUB, vn(d), vn(d), vn(s))); break;
    case op_dmul: emit32le(fop_dd(FOP_MUL, vn(d), vn(d), vn(s))); break;
    case op_ddiv: emit32le(fop_dd(FOP_DIV, vn(d), vn(d), vn(s))); break;
    default: record(arm64_error, "binop_local called with op %u", op); break;
    }
}


void arm64::binop_imm(local_t dloc, value_t v, op_t op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    arm64reg_t d = hw_rw_local(dloc);
    record(arm64, "binop_imm%+s [%u] %lld reg %u", opcode_name(op), dloc, v, d);

    switch (op)
    {
    case op_addi:
    case op_addu:
        if (v >= 0 && v < 4096)
            emit32le(add_imm(xn(d), xn(d), uint(v)));
        else if (v < 0 && v > -4096)
            emit32le(sub_imm(xn(d), xn(d), uint(-v)));
        else
        {
            set_imm(HW_SCRATCH, uint64_t(v));
            emit32le(add_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        }
        break;
    case op_subi:
    case op_subu:
        if (v >= 0 && v < 4096)
            emit32le(sub_imm(xn(d), xn(d), uint(v)));
        else if (v < 0 && v > -4096)
            emit32le(add_imm(xn(d), xn(d), uint(-v)));
        else
        {
            set_imm(HW_SCRATCH, uint64_t(v));
            emit32le(sub_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        }
        break;
    case op_muli:
    case op_mulu:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(mul_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_divi:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(sdiv_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_divu:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(udiv_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_modi:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(sdiv_rrr(xn(HW_TEMP0), xn(d), xn(HW_SCRATCH)));
        emit32le(msub_rrr(xn(d), xn(HW_TEMP0), xn(HW_SCRATCH), xn(d)));
        break;
    case op_modu:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(udiv_rrr(xn(HW_TEMP0), xn(d), xn(HW_SCRATCH)));
        emit32le(msub_rrr(xn(d), xn(HW_TEMP0), xn(HW_SCRATCH), xn(d)));
        break;
    case op_andu:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(and_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_oru:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(orr_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_xoru:
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(eor_rrr(xn(d), xn(d), xn(HW_SCRATCH)));
        break;
    case op_shlu: emit32le(lsl_imm64(xn(d), xn(d), uint(v) & 63u)); break;
    case op_shru: emit32le(asr_imm64(xn(d), xn(d), uint(v) & 63u)); break;
    case op_ushru: emit32le(lsr_imm64(xn(d), xn(d), uint(v) & 63u)); break;
    default: record(arm64_error, "binop_imm called with op %u", op); break;
    }
}


void arm64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(arm64, "set_pred(%u, %d)", p, on);
    if (on)
        emit32le(orr_imm64_bit(xn(HW_FLAGS), xn(HW_FLAGS), p));
    else
        emit32le(bfc64(xn(HW_FLAGS), p, 1));
}


void arm64::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to AArch64 condition (after CMP)
// ----------------------------------------------------------------------------
{
    record(arm64, "set_pred_cmp(%u, %u)", p, cond);
    emit32le(cset_x(xn(HW_SCRATCH), cond));
    emit32le(bfi64(xn(HW_FLAGS), xn(HW_SCRATCH), p, 1));
}


void arm64::cmp_local(byte p, local_t d, local_t s, byte cond)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    arm64reg_t dr = hw_local(d);
    arm64reg_t sr = hw_local(s);
    record(arm64,
           "cmp_local cc=%u p=%u d=%u r%u s=%u r%u",
           cond,
           p,
           d,
           dr,
           s,
           sr);
    emit32le(cmp_rr(xn(dr), xn(sr)));
    set_pred_cmp(p, cond);
}


void arm64::cmp_imm(byte p, local_t d, value_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    arm64reg_t dr = hw_local(d);
    record(arm64, "cmp_imm cc=%u p=%u d=%u r%u v=%lld", cond, p, d, dr, v);
    if (v >= 0 && v < 4096)
        emit32le(cmp_r_imm(xn(dr), uint(v)));
    else
    {
        set_imm(HW_SCRATCH, uint64_t(v));
        emit32le(cmp_rr(xn(dr), xn(HW_SCRATCH)));
    }
    set_pred_cmp(p, cond);
}


void arm64::cmp_uimm(byte p, local_t d, uvalue_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), cond);
}


void arm64::cond_mov_local(byte p, local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(arm64, "cond_mov_local(%u, %u, %u)", p, d, s);
    hwcode_t skip = emit_tbz_rel(HW_FLAGS, p);
    mov_local(d, s);
    patch(skip, cursor());
}


void arm64::select_local(byte p, local_t d, local_t s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(arm64, "select_local(%u, %u, %u, %u)", p, d, s, x);
    hwcode_t jclear = emit_tbz_rel(HW_FLAGS, p);
    mov_local(d, s);
    hwcode_t jend = emit_b_rel();
    patch(jclear, cursor());
    mov_local(d, x);
    patch(jend, cursor());
}


void arm64::neg_local(local_t d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_rw_local(d);
    emit32le(neg_rr(xn(r), xn(r)));
    record(arm64, "neg_local %u reg %u", d, r);
}


void arm64::not_local(local_t d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_rw_local(d);
    emit32le(not_rr(xn(r), xn(r)));
    record(arm64, "not_local %u reg %u", d, r);
}


void arm64::incr_local(local_t d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_rw_local(d);
    emit32le(add_imm(xn(r), xn(r), 1));
    record(arm64, "incr_local %u reg %u", d, r);
}


void arm64::decr_local(local_t d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_rw_local(d);
    emit32le(sub_imm(xn(r), xn(r), 1));
    record(arm64, "decr_local %u reg %u", d, r);
}


void arm64::sext_reg(arm64reg_t r, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register r from low w bits to 64
// ----------------------------------------------------------------------------
{
    record(arm64, "sext_reg %u width %u", r, w);
    if (w == 0 || w >= 64)
        return;
    emit32le(sbfm_width(xn(r), xn(r), w));
}


void arm64::sext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(arm64, "sext_local%u %u", w, d);
    if (w < 64)
        sext_reg(hw_rw_local(d), w);
}


void arm64::zext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(arm64, "zext_local%u %u", w, d);
    if (w < 64)
    {
        arm64reg_t r = hw_rw_local(d);
        emit32le(ubfm_width(xn(r), xn(r), w));
    }
}


void arm64::store_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d (+ offs)
// ----------------------------------------------------------------------------
{
    arm64reg_t dr = hw_local(d);
    arm64reg_t sr = hw_local(s);
    record(arm64, "store_mem%zu [%u]=%u offs %lld", sz, d, s, offs);

    arm64reg_t addr = dr;
    if (offs)
    {
        set_imm(HW_SCRATCH, uint64_t(offs));
        emit32le(add_rrr(xn(HW_SCRATCH), xn(dr), xn(HW_SCRATCH)));
        addr = HW_SCRATCH;
    }

    switch (sz)
    {
    default:
        record(arm64_error, "Invalid store size %zu", sz);
        // fallthrough
    case 1: emit32le(strb_idx(xn(sr), xn(addr))); break;
    case 2: emit32le(strh_idx(xn(sr), xn(addr))); break;
    case 4: emit32le(str_w_idx(xn(sr), xn(addr))); break;
    case 8: emit32le(str_idx(xn(sr), xn(addr))); break;
    }
}


void arm64::load_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Load from address in local s (+ offs) into local d
// ----------------------------------------------------------------------------
{
    arm64reg_t sr = hw_local(s);
    arm64reg_t dr = hw_dst_local(d);
    record(arm64, "load_mem%zu %u=[%u] offs %lld", sz, d, s, offs);

    arm64reg_t addr = sr;
    if (offs)
    {
        set_imm(HW_SCRATCH, uint64_t(offs));
        emit32le(add_rrr(xn(HW_SCRATCH), xn(sr), xn(HW_SCRATCH)));
        addr = HW_SCRATCH;
    }

    switch (sz)
    {
    default:
        record(arm64_error, "Invalid load size %zu", sz);
        // fallthrough
    case 1: emit32le(ldrb_idx(xn(dr), xn(addr))); break;
    case 2: emit32le(ldrh_idx(xn(dr), xn(addr))); break;
    case 4: emit32le(ldr_w_idx(xn(dr), xn(addr))); break;
    case 8: emit32le(ldr_idx(xn(dr), xn(addr))); break;
    }
}


void arm64::load_sext(local_t d, local_t s, byte w, value_t offs)
// ----------------------------------------------------------------------------
//   Load from [s+offs] and sign-extend to w bits into d
// ----------------------------------------------------------------------------
{
    arm64reg_t sr = hw_local(s);
    arm64reg_t dr = hw_dst_local(d);
    size_t     sz = (w + 7) / 8;
    record(arm64, "load_sext%u %u=[%u] sz %zu", w, d, s, sz);

    arm64reg_t addr = sr;
    if (offs)
    {
        set_imm(HW_SCRATCH, uint64_t(offs));
        emit32le(add_rrr(xn(HW_SCRATCH), xn(sr), xn(HW_SCRATCH)));
        addr = HW_SCRATCH;
    }

    switch (sz)
    {
    default:
        record(arm64_error, "Invalid load_sext width %u", w);
        // fallthrough
    case 1: emit32le(ldrsb_idx(xn(dr), xn(addr))); break;
    case 2: emit32le(ldrsh_idx(xn(dr), xn(addr))); break;
    case 4: emit32le(ldrsw_idx(xn(dr), xn(addr))); break;
    case 8: emit32le(ldr_idx(xn(dr), xn(addr))); break;
    }
    if (w & 7)
        sext_reg(dr, w);
}


void arm64::bra_pred(byte p, byte_p target, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense (TBZ/TBNZ)
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_%+s %u, %p", sense ? "true" : "false", p, target);
    hw_sys_arg_reload(target);
    hwcode_t pos =
        sense ? emit_tbnz_rel(HW_FLAGS, p) : emit_tbz_rel(HW_FLAGS, p);
    rel_branch(target, pos);
}


void arm64::bra_test(local_t s, byte_p target, byte cond)
// ----------------------------------------------------------------------------
//   Emit comparison-to-zero branch on local s
// ----------------------------------------------------------------------------
{
    record(arm64, "bra_test %u, %p cc=%u", s, target, cond);
    hw_sys_arg_reload(target);
    arm64reg_t r = hw_local(s);
    if (cond == COND_EQ)
    {
        hwcode_t pos = emit_cbz_rel(r);
        rel_branch(target, pos);
    }
    else if (cond == COND_NE)
    {
        hwcode_t pos = emit_cbnz_rel(r);
        rel_branch(target, pos);
    }
    else
    {
        emit32le(cmp_r_imm(xn(r), 0));
        hwcode_t pos = emit_bcond_rel(cond);
        rel_branch(target, pos);
    }
}


void arm64::rel_branch(byte_p target, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative branch site to bytecode target
// ----------------------------------------------------------------------------
{
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off);
}


void arm64::rel_function(tok_t tok, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative site to a function token
// ----------------------------------------------------------------------------
{
    intptr_t off = function(tok, site);
    if (off)
        patch(site, site + off);
}


void arm64::call_ptr(void (*fn)())
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer in x16
// ----------------------------------------------------------------------------
{
    record(arm64, "call_ptr %p", fn);
    // Bytecode enter keeps LR in the frame; the implicit 8-slot frame does not.
    bool save_lr = !frame_entered;
    if (save_lr)
        emit32le(stp_pre(HW_FRAME, HW_LR, HW_SP, -16));
    set_imm(HW_SCRATCH, uint64_t(fn));
    emit32le(blr(xn(HW_SCRATCH)));
    if (save_lr)
        emit32le(ldp_post(HW_FRAME, HW_LR, HW_SP, 16));
}


void arm64::invoke(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated AArch64 code; x29 = initial frame, x22 = flags
// ----------------------------------------------------------------------------
{
#if defined(__aarch64__)
    alignas(16) ularge_t initial_frame[DEFAULT_FRAME_SIZE + 2];
    stack_p              frame = initial_jit_fp(initial_frame);
    uint64_t             flags = rt.flags;
    // x29 is a reserved register on Darwin — save/restore it in the asm
    // rather than listing it as a clobber.
    asm volatile(
        "stp x29, lr, [sp, #-16]!\n\t"
        "mov x29, %0\n\t"
        "mov x28, %1\n\t"
        "mov x16, %2\n\t"
        "blr x16\n\t"
        "ldp x29, lr, [sp], #16"
        :
        : "r"(frame), "r"(flags), "r"(code)
        : "x16", "x22", "x30", "memory", "cc");
#else
    (void) code;
    rt.error("Cannot invoke AArch64 JIT on this host");
#endif
}


void arm64::dispatch(local_t loc, byte_p &pc, byte cond)
// ----------------------------------------------------------------------------
//   Dispatch a branch table
// ----------------------------------------------------------------------------
{
    hw_sys_arg_reload();
    arm64reg_t ref    = hw_local(loc);
    size_t     ncases = leb128<size_t>(pc);
    for (size_t n = 0; n < ncases; n++)
    {
        value_t test = leb128<value_t>(pc);
        byte_p  bpc  = pc;
        broff_t offs = leb128<broff_t>(pc);
        byte_p  tgt  = bpc + offs;
        hw_sys_arg_reload(tgt);
        set_imm(HW_SCRATCH, uint64_t(test));
        emit32le(cmp_rr(xn(ref), xn(HW_SCRATCH)));
        hwcode_t pos = emit_bcond_rel(cond);
        rel_branch(tgt, pos);
    }
}


#define EMIT(op) void arm64::emit_##op(emit_args &e)

#include "arm64-emit.cpp"
