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

#include "leb128.h"
#include "native-bridge.h"
#include "runtime.h"

#include <cstring>


RECORDER(riscv64,          256, "RISC-V 64-bit code generation helpers");
RECORDER(riscv64_ops,      256, "RISC-V 64-bit opcode emission");
RECORDER(riscv64_error,     64, "RISC-V 64-bit code generation errors");
RECORDER(riscv64_warning,   64, "RISC-V 64-bit code generation warnings");


riscv64::riscv64(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Construct a RISC-V code generator (RV64 by default; RV32 may subclass)
// ----------------------------------------------------------------------------
    : backend(rt, arch)
{
    record(riscv64, "New backend arch=%+s", arch_name(arch));
}



// ============================================================================
//
//   Backend standard interface
//
// ============================================================================

cstring riscv64::machine_defs() const
// ----------------------------------------------------------------------------
//   Return preprocessor defines for this target
// ----------------------------------------------------------------------------
{
    return "__riscv\0__riscv_xlen 64\0";
}


uint riscv64::registers() const
// ----------------------------------------------------------------------------
//   Return number of hardware registers on this platform
// ----------------------------------------------------------------------------
{
    return HW_COUNT;
}


uint riscv64::pointer_size() const
// ----------------------------------------------------------------------------
//   The platform is 64-bit
// ----------------------------------------------------------------------------
{
    return 8;
}


void riscv64::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs used by the JIT
// ----------------------------------------------------------------------------
{
    record(riscv64, "begin_translation");
    backend::begin_translation();
    prologue();
}


void riscv64::finish_translation()
// ----------------------------------------------------------------------------
//   Restore callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(riscv64, "finish_translation");
    epilogue();
    backend::finish_translation();
}


void riscv64::patch(hwcode_t pos, hwcode_t pvalue, uint flags)
// ----------------------------------------------------------------------------
//   Patch a recorded branch (J-type / B-type) to jump to target
// ----------------------------------------------------------------------------
{
    record(riscv64, "patch pos=%llu target=%llu flags=0x%X",
           pos, pvalue, flags);

    section &sec = code();
    int32_t off = int32_t(int64_t(pvalue) - int64_t(pos));
    if (off & 1)
    {
        record(riscv64_error, "Misaligned branch patch off=%d", off);
        return;
    }

    uint32_t insn   = sec.value32le(pos);
    uint     opcode = insn & 0x7fu;

    if (opcode == 0x6fu)
    {
        // J-type (jal): signed 21-bit even offset (±1 MiB)
        if (off < -1048576 || off > 1048574)
        {
            record(riscv64_error, "J-type out of range off=%d", off);
            return;
        }
        uint rd = (insn >> 7) & 0x1fu;
        sec.patch32le(pos, enc_j_imm(off, rd));
    }
    else if (opcode == 0x63u)
    {
        // B-type: signed 13-bit even offset (±4 KiB)
        if (off < -4096 || off > 4094)
        {
            record(riscv64_error, "B-type out of range off=%d", off);
            return;
        }
        uint rs1 = (insn >> 15) & 0x1fu;
        uint rs2 = (insn >> 20) & 0x1fu;
        uint f3  = (insn >> 12) & 0x7u;
        sec.patch32le(pos, enc_b_imm(off, rs1, rs2, f3));
    }
    else
    {
        record(riscv64_error, "Unknown branch insn 0x%08X at %llu", insn, pos);
    }
}


void riscv64::call(void (*target)())
// ----------------------------------------------------------------------------
//   Emit a direct call to a C function pointer
// ----------------------------------------------------------------------------
{
    record(riscv64, "call %p", target);
    spill_all();
    call_ptr(target);
}


void riscv64::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Spill all locals then call a runtime helper
// ----------------------------------------------------------------------------
{
    size_t spilled = spill_all();
    record(riscv64, "syscall %p spilled %zu", target, spilled);
    call_ptr(target);
}


uint riscv64::relocate(void *base)
// ----------------------------------------------------------------------------
//   Relocate absolute addresses after mapping the code
// ----------------------------------------------------------------------------
{
    size_t count = relocations.size();
    record(riscv64, "relocate base=%p count=%zu", base, count);
    relocations.clear();
    return uint(count);
}


bool riscv64::move(hwreg_t to, hwreg_t from)
// ----------------------------------------------------------------------------
//   Move between registers
// ----------------------------------------------------------------------------
{
    riscv64reg_t d = hw(to);
    riscv64reg_t s = hw(from);
    emit32le(mv_rr(xn(d), xn(s)));
    return true;
}


bool riscv64::load(hwreg_t h, local_t n)
// ----------------------------------------------------------------------------
//   Load bytecode local n from frame memory into hardware register r
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw(h);
    record(riscv64, "load %u=[%u]", r, n);

    int disp = local_disp(n);
    emit_load_off(r, HW_FRAME, disp);
    return true;
}


bool riscv64::store(local_t n, hwreg_t h)
// ----------------------------------------------------------------------------
//   Store hardware register r into bytecode local frame slot n
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw(h);
    record(riscv64, "store [%u]=%u", n, r);
    int disp = local_disp(n);
    emit_store_off(HW_FRAME, r, disp);
    saved(n, h);
    return true;
}


hwreg_t riscv64::register_for(local_t loc, hwspec_t spec, bool ld)
// ----------------------------------------------------------------------------
//   Find the register associated to the local
// ----------------------------------------------------------------------------
{
    record(riscv64_error, "Not yet implemented");
    return NO_REGISTER;
}


void riscv64::register_sync()
// ----------------------------------------------------------------------------
//   Synchronize the temporaries
// ----------------------------------------------------------------------------
{
    record(riscv64_error, "Not yet implemented");
}


riscv64::reginfo riscv64::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for RV64
// ----------------------------------------------------------------------------
{
    static const reginfo rv64_registers[HW_COUNT] =
    {
        {  "x0",                       0 },  // zero
        {  "x1",                       0 },  // ra  (HW_RA)
        {  "x2",                       0 },  // sp  (HW_SP)
        {  "x3",                       0 },  // gp
        {  "x4",                       0 },  // tp
        {  "x5",                       0 },  // t0  (HW_IP / scratch)
        {  "x6",                       0 },  // t1
        {  "x7",                       0 },  // t2
        {  "x8",                       0 },  // s0/fp (HW_FRAME)
        {  "x9",                       0 },  // s1    (HW_FLAGS)
        { "x10",                       0 },  // a0
        { "x11",                       0 },  // a1
        { "x12",                       0 },  // a2
        { "x13",                       0 },  // a3
        { "x14",                       0 },  // a4
        { "x15",                       0 },  // a5
        { "x16",                       0 },  // a6
        { "x17",                       0 },  // a7
        { "x18", RSPEC_CALL | RSPEC_INT },  // s2  (allocatable)
        { "x19", RSPEC_CALL | RSPEC_INT },  // s3
        { "x20", RSPEC_CALL | RSPEC_INT },  // s4
        { "x21", RSPEC_CALL | RSPEC_INT },  // s5
        { "x22", RSPEC_CALL | RSPEC_INT },  // s6
        { "x23", RSPEC_CALL | RSPEC_INT },  // s7
        { "x24", RSPEC_CALL | RSPEC_INT },  // s8
        { "x25", RSPEC_CALL | RSPEC_INT },  // s9
        { "x26", RSPEC_CALL | RSPEC_INT },  // s10
        { "x27", RSPEC_CALL | RSPEC_INT },  // s11
        { "x28",                       0 },  // t3
        { "x29",                       0 },  // t4
        { "x30",                       0 },  // t5
        { "x31",                       0 },  // t6
    };
    return reg < HW_COUNT ? rv64_registers[reg] : reginfo{"INVALID", 0};
}



// ============================================================================
//
//   Prologue, epilogue, and call helpers
//
// ============================================================================

void riscv64::prologue()
// ----------------------------------------------------------------------------
//   Save FP/RA and callee-saved registers used by the JIT
// ----------------------------------------------------------------------------
//   Saves s0 (HW_FRAME) for ABI restore but does not reassign it — invoke_*
//   already points s0 at the caller-provided 8-slot initial frame.
//   Do not mov fp, sp — enter owns FP.
// ----------------------------------------------------------------------------
{
    record(riscv64, "prologue");
    // 13 callee-saved: ra, s0(fp), s1(flags), s2-s11.  Align to 16 bytes.
    static const riscv64reg_t saved[] = {
        HW_RA, HW_FRAME, HW_FLAGS,
        HW_X18, HW_X19, HW_X20, HW_X21, HW_X22, HW_X23,
        HW_X24, HW_X25, HW_X26, HW_X27
    };
    const uint n = uint(sizeof(saved) / sizeof(saved[0]));
    const uint wsz = pointer_size();
    uint bytes = n * wsz;
    bytes = (bytes + 15u) & ~15u;
    emit32le(addi(xn(HW_SP), xn(HW_SP), -int(bytes)));
    for (uint i = 0; i < n; i++)
        emit_store_abi_word(saved[i], HW_SP, int(bytes - wsz * (i + 1)));
}


void riscv64::epilogue()
// ----------------------------------------------------------------------------
//   Pop prologue callee-saves and return (SP must already be at that block)
// ----------------------------------------------------------------------------
{
    record(riscv64, "epilogue");
    static const riscv64reg_t saved[] = {
        HW_RA, HW_FRAME, HW_FLAGS,
        HW_X18, HW_X19, HW_X20, HW_X21, HW_X22, HW_X23,
        HW_X24, HW_X25, HW_X26, HW_X27
    };
    const uint n = uint(sizeof(saved) / sizeof(saved[0]));
    const uint wsz = pointer_size();
    uint bytes = n * wsz;
    bytes = (bytes + 15u) & ~15u;
    for (uint i = n; i-- > 0; )
        emit_load_abi_word(saved[i], HW_SP, int(bytes - wsz * (i + 1)));
    emit32le(addi(xn(HW_SP), xn(HW_SP), int(bytes)));
    emit32le(ret_insn());
}


void riscv64::call_ptr(void (*fn)())
// ----------------------------------------------------------------------------
//   Emit a call through a function pointer loaded into t0 (HW_IP)
// ----------------------------------------------------------------------------
{
    record(riscv64, "call_ptr %p", fn);
    load_ptr_imm(HW_IP, uint64_t(uintptr_t(fn)));
    emit32le(jalr_ra(xn(HW_IP)));
}


void riscv64::invoke(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated RISC-V code; s0/fp = initial frame, s1 = flags
// ----------------------------------------------------------------------------
//   Save ra and s2-s11 around the call: JIT uses s2–s11 as callee-saved
//   allocatable regs.  Do not list s2-s11 as clobbers — that starves GCC of
//   registers for the three "r" inputs.
{
#if defined(__riscv)
    alignas(16) ularge_t initial_frame[DEFAULT_FRAME_SIZE + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    asm volatile(
        "mv s0, %[frame]\n\t"
        "mv s1, %[flags]\n\t"
        "mv t0, %[code]\n\t"
        "addi sp, sp, -96\n\t"
        "sd s2, 0(sp)\n\t"
        "sd s3, 8(sp)\n\t"
        "sd s4, 16(sp)\n\t"
        "sd s5, 24(sp)\n\t"
        "sd s6, 32(sp)\n\t"
        "sd s7, 40(sp)\n\t"
        "sd s8, 48(sp)\n\t"
        "sd s9, 56(sp)\n\t"
        "sd s10, 64(sp)\n\t"
        "sd s11, 72(sp)\n\t"
        "sd ra, 80(sp)\n\t"
        "jalr ra, t0, 0\n\t"
        "ld s2, 0(sp)\n\t"
        "ld s3, 8(sp)\n\t"
        "ld s4, 16(sp)\n\t"
        "ld s5, 24(sp)\n\t"
        "ld s6, 32(sp)\n\t"
        "ld s7, 40(sp)\n\t"
        "ld s8, 48(sp)\n\t"
        "ld s9, 56(sp)\n\t"
        "ld s10, 64(sp)\n\t"
        "ld s11, 72(sp)\n\t"
        "ld ra, 80(sp)\n\t"
        "addi sp, sp, 96"
        :
        : [frame] "r"(frame), [flags] "r"(flags), [code] "r"(code)
        : "s0", "s1", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
          "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
          "ra", "memory", "cc");
#else
    (void) code;
    rt.error("Cannot invoke RISC-V JIT on this host");
#endif
}



// ============================================================================
//
//   Emit helpers
//
// ============================================================================

void riscv64::emit_store_abi_word(riscv64reg_t rs2, riscv64reg_t rs1, int offs)
// ----------------------------------------------------------------------------
//   Store one ABI pointer/link word (sd on RV64)
// ----------------------------------------------------------------------------
{
    emit32le(sd_off(xn(rs2), xn(rs1), offs));
}


void riscv64::emit_load_abi_word(riscv64reg_t rd, riscv64reg_t rs1, int offs)
// ----------------------------------------------------------------------------
//   Load one ABI pointer/link word (ld on RV64)
// ----------------------------------------------------------------------------
{
    emit32le(ld_off(xn(rd), xn(rs1), offs));
}


void riscv64::load_ptr_imm(riscv64reg_t r, uint64_t v)
// ----------------------------------------------------------------------------
//   Materialize a code/data pointer (64-bit on RV64)
// ----------------------------------------------------------------------------
{
    load_imm64(r, v);
}


void riscv64::emit_load_off(riscv64reg_t rd, riscv64reg_t rn, int offs)
// ----------------------------------------------------------------------------
//   Load 64-bit value from [rn + offs] into rd
// ----------------------------------------------------------------------------
{
    uint d = xn(rd);
    uint n = xn(rn);
    if (offs >= -2048 && offs <= 2047)
    {
        emit32le(ld_off(d, n, offs));
    }
    else
    {
        load_imm64(HW_IP, uint64_t(int64_t(offs)));
        emit32le(add_rr(xn(HW_IP), n, xn(HW_IP)));
        emit32le(ld_idx(d, xn(HW_IP)));
    }
}


void riscv64::emit_store_off(riscv64reg_t rn, riscv64reg_t rt, int offs)
// ----------------------------------------------------------------------------
//   Store 64-bit value from rt to [rn + offs]
// ----------------------------------------------------------------------------
{
    uint n = xn(rn);
    uint t = xn(rt);
    if (offs >= -2048 && offs <= 2047)
    {
        emit32le(sd_off(t, n, offs));
    }
    else
    {
        load_imm64(HW_IP, uint64_t(int64_t(offs)));
        emit32le(add_rr(xn(HW_IP), n, xn(HW_IP)));
        emit32le(sd_idx(t, xn(HW_IP)));
    }
}


void riscv64::emit_push(riscv64reg_t r)
// ----------------------------------------------------------------------------
//   Push a register (16-byte aligned stack slot)
// ----------------------------------------------------------------------------
{
    emit32le(addi(xn(HW_SP), xn(HW_SP), -16));
    emit_store_abi_word(r, HW_SP, 0);
}


void riscv64::emit_pop(riscv64reg_t r)
// ----------------------------------------------------------------------------
//   Pop a register from a 16-byte aligned stack slot
// ----------------------------------------------------------------------------
{
    emit_load_abi_word(r, HW_SP, 0);
    emit32le(addi(xn(HW_SP), xn(HW_SP), 16));
}



// ============================================================================
//
//   Load / store helpers
//
// ============================================================================

void riscv64::load_uimm32(riscv64reg_t r, uint32_t v)
// ----------------------------------------------------------------------------
//   Load a 32-bit unsigned value into r with upper bits cleared
// ----------------------------------------------------------------------------
{
    uint rd = xn(r);
    record(riscv64, "load_uimm32(%u, %u)", rd, v);
    if (v < 0x800u)
    {
        emit32le(addi(rd, 0, int(v)));
        return;
    }
    int32_t hi = int32_t((int64_t(int32_t(v)) + 0x800) >> 12);
    int32_t lo = int32_t(int32_t(v) - (hi << 12));
    emit32le(lui(rd, uint32_t(hi) & 0xfffffu));
    if (lo)
        emit32le(addi(rd, rd, lo));
    // On RV64, lui sign-extends into bits 63:32; clear when MSB of lo is set.
    if ((v & 0x80000000u) && pointer_size() == 8)
    {
        emit32le(slli(rd, rd, 32));
        emit32le(srli(rd, rd, 32));
    }
}


void riscv64::load_imm64(riscv64reg_t r, uint64_t v)
// ----------------------------------------------------------------------------
//   Load a 64-bit constant into RISC-V register r
// ----------------------------------------------------------------------------
{
    uint rd = xn(r);
    record(riscv64, "load_imm64(%u, 0x%llX)", rd, v);
    int64_t s32 = int64_t(int32_t(uint32_t(v)));
    if (v == uint64_t(s32) && s32 >= -2048 && s32 < 2048)
    {
        emit32le(addi(rd, 0, int(s32)));
        return;
    }
    uint32_t lo = uint32_t(v);
    uint32_t hi = uint32_t(v >> 32);
    load_uimm32(r, lo);
    if (!hi || pointer_size() < 8)
        return;
    load_uimm32(HW_IP, hi);
    emit32le(slli(xn(HW_IP), xn(HW_IP), 32));
    emit32le(or_rr(rd, rd, xn(HW_IP)));
}


void riscv64::load_local(local_t loc, riscv64reg_t r)
// ----------------------------------------------------------------------------
//   Load bytecode local into a hardware register
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_local [%u]->%u", loc, r);
    load(r, loc);
}


void riscv64::store_local(riscv64reg_t r, local_t loc)
// ----------------------------------------------------------------------------
//   Store hardware register into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_local %u->[%u]", r, loc);
    store(loc, r);
}


void riscv64::mov_local(local_t dst, local_t src)
// ----------------------------------------------------------------------------
//   Copy value between bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "mov_local [%u]=[%u]", dst, src);
    riscv64reg_t sr = hw_local(src);
    riscv64reg_t dr = hw_dst_local(dst);
    move(dr, sr);
}


void riscv64::swap_locals(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Swap values of two bytecode locals
// ----------------------------------------------------------------------------
{
    record(riscv64, "swap_locals %u and %u", d, s);
    riscv64reg_t sr = hw_local(s);
    riscv64reg_t dr = hw_local(d);
    emit32le(mv_rr(xn(HW_IP), xn(dr)));
    emit32le(mv_rr(xn(dr), xn(sr)));
    emit32le(mv_rr(xn(sr), xn(HW_IP)));
}


void riscv64::store_imm(local_t d, value_t v)
// ----------------------------------------------------------------------------
//   Store integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_imm [%u]=%lld", d, v);
    riscv64reg_t dreg = hw_dst_local(d);
    load_imm64(dreg, uint64_t(v));
}


void riscv64::store_uimm(local_t d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Store unsigned integer immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_uimm [%u]=%llu", d, v);
    riscv64reg_t dreg = hw_dst_local(d);
    load_imm64(dreg, uint64_t(v));
}


void riscv64::store_float(local_t d, float v)
// ----------------------------------------------------------------------------
//   Store float immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_flt [%u]=%g", d, v);
    riscv64reg_t dreg = hw_dst_local(d);
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    load_imm64(dreg, uint64_t(bits));
}


void riscv64::store_double(local_t d, double v)
// ----------------------------------------------------------------------------
//   Store double immediate into bytecode local
// ----------------------------------------------------------------------------
{
    record(riscv64, "store_dbl [%u]=%g", d, v);
    riscv64reg_t dreg = hw_dst_local(d);
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    load_imm64(dreg, bits);
}


void riscv64::binop_local(local_t dloc, local_t sloc, op_t op, byte fp)
// ----------------------------------------------------------------------------
//   Apply binary operator to two bytecode locals
// ----------------------------------------------------------------------------
{
    riscv64reg_t d = hw_local(dloc);
    riscv64reg_t s = hw_local(sloc);
    record(riscv64, "binop_local%+s [%u]=[%u] reg %u=%u",
           opcode_name(op), dloc, sloc, d, s);

    (void) fp;
    switch (op)
    {
    case op_add:
        emit32le(add_rr(xn(d), xn(d), xn(s)));
        break;
    case op_sub:
        emit32le(sub_rr(xn(d), xn(d), xn(s)));
        break;
    case op_mul:
    case op_umul:
        emit32le(mul_rr(xn(d), xn(d), xn(s)));
        break;
    case op_div:
        emit32le(div_rr(xn(d), xn(d), xn(s)));
        break;
    case op_udiv:
        emit32le(divu_rr(xn(d), xn(d), xn(s)));
        break;
    case op_mod:
        emit32le(rem_rr(xn(d), xn(d), xn(s)));
        break;
    case op_umod:
        emit32le(remu_rr(xn(d), xn(d), xn(s)));
        break;
    case op_and:
        emit32le(and_rr(xn(d), xn(d), xn(s)));
        break;
    case op_or:
        emit32le(or_rr(xn(d), xn(d), xn(s)));
        break;
    case op_xor:
        emit32le(xor_rr(xn(d), xn(d), xn(s)));
        break;
    case op_shl:
        emit32le(sll_rr(xn(d), xn(d), xn(s)));
        break;
    case op_shr:
        emit32le(sra_rr(xn(d), xn(d), xn(s)));
        break;
    case op_ushr:
        emit32le(srl_rr(xn(d), xn(d), xn(s)));
        break;
    case op_fadd:
        emit32le(fmv_w_x(0, xn(d)));
        emit32le(fmv_w_x(1, xn(s)));
        emit32le(fadd_s(0, 0, 1));
        emit32le(fmv_x_w(xn(d), 0));
        break;
    case op_fsub:
        emit32le(fmv_w_x(0, xn(d)));
        emit32le(fmv_w_x(1, xn(s)));
        emit32le(fsub_s(0, 0, 1));
        emit32le(fmv_x_w(xn(d), 0));
        break;
    case op_fmul:
        emit32le(fmv_w_x(0, xn(d)));
        emit32le(fmv_w_x(1, xn(s)));
        emit32le(fmul_s(0, 0, 1));
        emit32le(fmv_x_w(xn(d), 0));
        break;
    case op_fdiv:
        emit32le(fmv_w_x(0, xn(d)));
        emit32le(fmv_w_x(1, xn(s)));
        emit32le(fdiv_s(0, 0, 1));
        emit32le(fmv_x_w(xn(d), 0));
        break;
    case op_dadd:
        emit32le(fmv_d_x(0, xn(d)));
        emit32le(fmv_d_x(1, xn(s)));
        emit32le(fadd_d(0, 0, 1));
        emit32le(fmv_x_d(xn(d), 0));
        break;
    case op_dsub:
        emit32le(fmv_d_x(0, xn(d)));
        emit32le(fmv_d_x(1, xn(s)));
        emit32le(fsub_d(0, 0, 1));
        emit32le(fmv_x_d(xn(d), 0));
        break;
    case op_dmul:
        emit32le(fmv_d_x(0, xn(d)));
        emit32le(fmv_d_x(1, xn(s)));
        emit32le(fmul_d(0, 0, 1));
        emit32le(fmv_x_d(xn(d), 0));
        break;
    case op_ddiv:
        emit32le(fmv_d_x(0, xn(d)));
        emit32le(fmv_d_x(1, xn(s)));
        emit32le(fdiv_d(0, 0, 1));
        emit32le(fmv_x_d(xn(d), 0));
        break;
    default:
        record(riscv64_error, "binop_local called with op %u", op);
        break;
    }
}


void riscv64::binop_imm(local_t dloc, value_t v, op_t op)
// ----------------------------------------------------------------------------
//   Apply binary operator with immediate to local
// ----------------------------------------------------------------------------
{
    riscv64reg_t d = hw_local(dloc);
    record(riscv64, "binop_imm%+s [%u] %lld reg %u",
           opcode_name(op), dloc, v, d);

    switch (op)
    {
    case op_addi:
    case op_addu:
        if (v >= -2048 && v < 2048)
            emit32le(addi(xn(d), xn(d), int(v)));
        else
        {
            load_imm64(HW_IP, uint64_t(v));
            emit32le(add_rr(xn(d), xn(d), xn(HW_IP)));
        }
        break;
    case op_subi:
    case op_subu:
        if (v >= -2048 && v < 2048)
            emit32le(addi(xn(d), xn(d), -int(v)));
        else
        {
            load_imm64(HW_IP, uint64_t(v));
            emit32le(sub_rr(xn(d), xn(d), xn(HW_IP)));
        }
        break;
    case op_muli:
    case op_mulu:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(mul_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_divi:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(div_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_divu:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(divu_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_modi:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(rem_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_modu:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(remu_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_andu:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(and_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_oru:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(or_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_xoru:
        load_imm64(HW_IP, uint64_t(v));
        emit32le(xor_rr(xn(d), xn(d), xn(HW_IP)));
        break;
    case op_shlu:
        emit32le(slli(xn(d), xn(d), uint(v) & 63u));
        break;
    case op_shru:
        emit32le(srai(xn(d), xn(d), uint(v) & 63u));
        break;
    case op_ushru:
        emit32le(srli(xn(d), xn(d), uint(v) & 63u));
        break;
    default:
        record(riscv64_error, "binop_imm called with op %u", op);
        break;
    }
}


void riscv64::set_pred(byte p, bool on)
// ----------------------------------------------------------------------------
//   Set or clear predicate bit p in flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_pred(%u, %d)", p, on);
    if (on)
    {
        load_imm64(HW_IP, 1ULL << p);
        emit32le(or_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));
    }
    else
    {
        load_imm64(HW_IP, ~(1ULL << p));
        emit32le(and_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));
    }
}


void riscv64::set_pred_cmp(byte p, byte cond)
// ----------------------------------------------------------------------------
//   Set predicate bit p according to comparison result in HW_IP
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_pred_cmp(%u, %u)", p, cond);
    riscv64reg_t d1 = hw_local(0);   // use already-allocated register
    riscv64reg_t d2 = hw_local(1);   // for comparands
    (void) d1; (void) d2;

    // Clear the predicate bit first
    load_imm64(HW_IP, ~(1ULL << p));
    emit32le(and_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));

    // Stub: full cond-code dispatch in Task 3
    record(riscv64_warning, "set_pred_cmp stub cond=%u", cond);
}


void riscv64::cmp_local(byte p, local_t d, local_t s, byte cond)
// ----------------------------------------------------------------------------
//   Compare two locals and set predicate p
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr = hw_local(d);
    riscv64reg_t sr = hw_local(s);
    record(riscv64, "cmp_local cc=%u p=%u d=%u r%u s=%u r%u",
           cond, p, d, dr, s, sr);

    load_imm64(HW_IP, ~(1ULL << p));
    emit32le(and_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));

    uint res = xn(HW_IP);
    switch (cond)
    {
    case COND_EQ:
        emit32le(sub_rr(res, xn(dr), xn(sr)));
        emit32le(sltiu(res, res, 1));
        break;
    case COND_NE:
        emit32le(sub_rr(res, xn(dr), xn(sr)));
        emit32le(sltu_rr(res, 0, res));
        break;
    case COND_LT:
        emit32le(slt_rr(res, xn(dr), xn(sr)));
        break;
    case COND_GT:
        emit32le(slt_rr(res, xn(sr), xn(dr)));
        break;
    case COND_LE:
        emit32le(slt_rr(res, xn(sr), xn(dr)));
        emit32le(xori(res, res, 1));
        break;
    case COND_GE:
        emit32le(slt_rr(res, xn(dr), xn(sr)));
        emit32le(xori(res, res, 1));
        break;
    case COND_CC:
        emit32le(sltu_rr(res, xn(dr), xn(sr)));
        break;
    case COND_LS:
        emit32le(sltu_rr(res, xn(sr), xn(dr)));
        emit32le(xori(res, res, 1));
        break;
    default:
        emit32le(addi(res, 0, 0));
        break;
    }

    // If result is non-zero, set the predicate bit
    hwcode_t skip_pos = cursor();
    emit32le(beq_imm(res, 0, 0));
    load_imm64(HW_IP, 1ULL << p);
    emit32le(or_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));
    int32_t off = int32_t(cursor() - skip_pos);
    code().patch32le(skip_pos, beq_imm(res, 0, off));
}


void riscv64::cmp_imm(byte p, local_t d, value_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with signed immediate and set predicate
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr = hw_local(d);
    record(riscv64, "cmp_imm cc=%u p=%u d=%u r%u v=%lld", cond, p, d, dr, v);
    // Load immediate into HW_IP, then compare d against it
    riscv64reg_t tmp = HW_X28;    // t3 as temporary
    load_imm64(tmp, uint64_t(v));

    load_imm64(HW_IP, ~(1ULL << p));
    emit32le(and_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));

    uint res = xn(HW_IP);
    switch (cond)
    {
    case COND_EQ:
        emit32le(sub_rr(res, xn(dr), xn(tmp)));
        emit32le(sltiu(res, res, 1));
        break;
    case COND_NE:
        emit32le(sub_rr(res, xn(dr), xn(tmp)));
        emit32le(sltu_rr(res, 0, res));
        break;
    case COND_LT:
        emit32le(slt_rr(res, xn(dr), xn(tmp)));
        break;
    case COND_GT:
        emit32le(slt_rr(res, xn(tmp), xn(dr)));
        break;
    case COND_LE:
        emit32le(slt_rr(res, xn(tmp), xn(dr)));
        emit32le(xori(res, res, 1));
        break;
    case COND_GE:
        emit32le(slt_rr(res, xn(dr), xn(tmp)));
        emit32le(xori(res, res, 1));
        break;
    default:
        emit32le(addi(res, 0, 0));
        break;
    }

    hwcode_t skip_pos = cursor();
    emit32le(beq_imm(res, 0, 0));
    load_imm64(HW_IP, 1ULL << p);
    emit32le(or_rr(xn(HW_FLAGS), xn(HW_FLAGS), xn(HW_IP)));
    int32_t off = int32_t(cursor() - skip_pos);
    code().patch32le(skip_pos, beq_imm(res, 0, off));
}


void riscv64::cmp_uimm(byte p, local_t d, uvalue_t v, byte cond)
// ----------------------------------------------------------------------------
//   Compare local with unsigned immediate and set predicate
// ----------------------------------------------------------------------------
{
    cmp_imm(p, d, value_t(v), cond);
}


void riscv64::cond_mov_local(byte p, local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Conditionally move local s into d when predicate p is set
// ----------------------------------------------------------------------------
{
    record(riscv64, "cond_mov_local(%u, %u, %u)", p, d, s);
    load_imm64(HW_IP, uint64_t(1) << p);
    emit32le(and_rr(xn(HW_IP), xn(HW_FLAGS), xn(HW_IP)));
    hwcode_t skip = cursor();
    emit32le(beq_imm(xn(HW_IP), 0, 0));
    mov_local(d, s);
    patch(skip, cursor());
}


void riscv64::select_local(byte p, local_t d, local_t s, byte x)
// ----------------------------------------------------------------------------
//   Select between locals s and x into d based on predicate p
// ----------------------------------------------------------------------------
{
    record(riscv64, "select_local(%u, %u, %u, %u)", p, d, s, x);
    load_imm64(HW_IP, uint64_t(1) << p);
    emit32le(and_rr(xn(HW_IP), xn(HW_FLAGS), xn(HW_IP)));
    hwcode_t jclear = cursor();
    emit32le(beq_imm(xn(HW_IP), 0, 0));
    mov_local(d, s);
    hwcode_t jend = cursor();
    emit32le(j_imm(0));
    patch(jclear, cursor());
    mov_local(d, x);
    patch(jend, cursor());
}


void riscv64::neg_local(local_t d)
// ----------------------------------------------------------------------------
//   Negate bytecode local d
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(d);
    emit32le(sub_rr(xn(r), 0, xn(r)));
    record(riscv64, "neg_local %u reg %u", d, r);
}


void riscv64::not_local(local_t d)
// ----------------------------------------------------------------------------
//   Bitwise complement of bytecode local d
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(d);
    emit32le(not_rr(xn(r)));
    record(riscv64, "not_local %u reg %u", d, r);
}


void riscv64::incr_local(local_t d)
// ----------------------------------------------------------------------------
//   Increment bytecode local d
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(d);
    emit32le(addi(xn(r), xn(r), 1));
    record(riscv64, "incr_local %u reg %u", d, r);
}


void riscv64::decr_local(local_t d)
// ----------------------------------------------------------------------------
//   Decrement bytecode local d
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(d);
    emit32le(addi(xn(r), xn(r), -1));
    record(riscv64, "decr_local %u reg %u", d, r);
}


void riscv64::sext_reg(riscv64reg_t rd, riscv64reg_t rn, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend register rn to w bits into rd
// ----------------------------------------------------------------------------
{
    record(riscv64, "sext_reg %u %u width %u", rd, rn, w);
    if (w > 0 && w < 64)
    {
        uint sh = 64 - w;
        emit32le(slli(xn(rd), xn(rn), sh));
        emit32le(srai(xn(rd), xn(rd), sh));
    }
}


void riscv64::sext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Sign-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "sext_local%u %u", w, d);
    if (w < 64)
    {
        riscv64reg_t r = hw_local(d);
        sext_reg(r, r, w);
    }
}


void riscv64::zext_local(local_t d, byte w)
// ----------------------------------------------------------------------------
//   Zero-extend local d to w bits
// ----------------------------------------------------------------------------
{
    record(riscv64, "zext_local%u %u", w, d);
    if (w < 64)
    {
        riscv64reg_t r = hw_local(d);
        load_imm64(HW_IP, (1ull << w) - 1ull);
        emit32le(and_rr(xn(r), xn(r), xn(HW_IP)));
    }
}


void riscv64::load_signext_mem(riscv64reg_t rd, riscv64reg_t addr, byte w)
// ----------------------------------------------------------------------------
//   Load from [addr] and sign-extend to w bits into rd
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_signext_mem %u [%u] w=%u", rd, addr, w);
    if (w == 8)
        emit32le(lb_idx(xn(rd), xn(addr)));
    else if (w == 16)
        emit32le(lh_idx(xn(rd), xn(addr)));
    else if (w == 32)
        emit32le(lw_idx(xn(rd), xn(addr)));
    else if (w >= 64)
        emit32le(ld_idx(xn(rd), xn(addr)));
    else
    {
        uint sz = (w + 7) / 8;
        switch (sz)
        {
        case 1: emit32le(lb_idx(xn(rd), xn(addr))); break;
        case 2: emit32le(lh_idx(xn(rd), xn(addr))); break;
        case 4: emit32le(lw_idx(xn(rd), xn(addr))); break;
        default: emit32le(ld_idx(xn(rd), xn(addr))); break;
        }
        sext_reg(rd, rd, w);
    }
}


void riscv64::store_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Store value from local s at address in local d (+ offs)
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr = hw_local(d);
    riscv64reg_t sr = hw_local(s);
    record(riscv64, "store_mem%zu [%u]=%u offs %lld", sz, d, s, offs);

    riscv64reg_t addr = dr;
    if (offs)
    {
        load_imm64(HW_IP, uint64_t(offs));
        emit32le(add_rr(xn(HW_IP), xn(dr), xn(HW_IP)));
        addr = HW_IP;
    }

    switch (sz)
    {
    default:
        record(riscv64_error, "Invalid store size %zu", sz);
        // fallthrough
    case 1: emit32le(sb_off(xn(sr), xn(addr), 0)); break;
    case 2: emit32le(sh_off(xn(sr), xn(addr), 0)); break;
    case 4: emit32le(sw_off(xn(sr), xn(addr), 0)); break;
    case 8:
        if (pointer_size() == 8)
        {
            emit32le(sd_off(xn(sr), xn(addr), 0));
        }
        else
        {
            // ILP32: store low word; zero high half of ularge slot
            emit32le(sw_off(xn(sr), xn(addr), 0));
            riscv64reg_t zero = (addr == HW_IP) ? HW_X6 : HW_IP;
            emit32le(addi(xn(zero), 0, 0));
            emit32le(sw_off(xn(zero), xn(addr), 4));
        }
        break;
    }
}


void riscv64::load_mem(local_t d, local_t s, size_t sz, value_t offs)
// ----------------------------------------------------------------------------
//   Load from address in local s (+ offs) into local d
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr = hw_local(d);
    riscv64reg_t sr = hw_local(s);
    record(riscv64, "load_mem%zu %u=[%u] offs %lld", sz, d, s, offs);

    riscv64reg_t addr = sr;
    if (offs)
    {
        load_imm64(HW_IP, uint64_t(offs));
        emit32le(add_rr(xn(HW_IP), xn(sr), xn(HW_IP)));
        addr = HW_IP;
    }

    switch (sz)
    {
    default:
        record(riscv64_error, "Invalid load size %zu", sz);
        // fallthrough
    case 1: emit32le(lbu_off(xn(dr), xn(addr), 0)); break;
    case 2: emit32le(lhu_off(xn(dr), xn(addr), 0)); break;
    case 4:
        // lwu is RV64-only; RV32 uses lw
        if (pointer_size() == 8)
            emit32le(lwu_off(xn(dr), xn(addr), 0));
        else
            emit32le(lw_off(xn(dr), xn(addr), 0));
        break;
    case 8:
        if (pointer_size() == 8)
            emit32le(ld_off(xn(dr), xn(addr), 0));
        else
            emit32le(lw_off(xn(dr), xn(addr), 0)); // low half of ularge
        break;
    }
}


void riscv64::load_sext(local_t d, local_t s, byte w, value_t offs)
// ----------------------------------------------------------------------------
//   Load from [s+offs] and sign-extend to w bits into d
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr = hw_local(d);
    riscv64reg_t sr = hw_local(s);
    record(riscv64, "load_sext%u %u=[%u] offs %lld", w, d, s, offs);

    if (offs)
    {
        load_imm64(HW_IP, uint64_t(offs));
        emit32le(add_rr(xn(HW_IP), xn(sr), xn(HW_IP)));
        load_signext_mem(dr, HW_IP, w);
    }
    else
    {
        load_signext_mem(dr, sr, w);
    }
    if (w & 7)
        sext_reg(dr, dr, w);
}


void riscv64::frame_addr(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Compute frame-relative address of local slot s into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "frame_addr(%u, %u)", d, s);
    riscv64reg_t dr = hw_local(d);
    int disp = local_disp(s);
    if (disp >= -2048 && disp <= 2047)
        emit32le(addi(xn(dr), xn(HW_FRAME), disp));
    else
    {
        load_imm64(HW_IP, uint64_t(int64_t(disp)));
        emit32le(add_rr(xn(dr), xn(HW_FRAME), xn(HW_IP)));
    }
}


void riscv64::bra_pred(byte p, byte_p target, bool sense)
// ----------------------------------------------------------------------------
//   Emit branch on predicate p with given sense
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_%+s %u, %p", sense ? "true" : "false", p, target);
    load_imm64(HW_IP, uint64_t(1) << p);
    emit32le(and_rr(xn(HW_IP), xn(HW_FLAGS), xn(HW_IP)));
    if (sense)
        emit32le(beq_imm(xn(HW_IP), 0, 8));
    else
        emit32le(bne_imm(xn(HW_IP), 0, 8));
    hwcode_t pos = cursor();
    emit32le(j_imm(0));
    rel_branch(target, pos);
}


void riscv64::bra_test(local_t s, byte_p target, byte cond)
// ----------------------------------------------------------------------------
//   Emit comparison-to-zero branch on local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "bra_test %u, %p cc=%u", s, target, cond);
    riscv64reg_t r = hw_local(s);
    if (cond == COND_EQ)
    {
        emit32le(bne_imm(xn(r), 0, 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else if (cond == COND_NE)
    {
        emit32le(beq_imm(xn(r), 0, 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else if (cond == COND_LT)
    {
        emit32le(bge_imm(xn(r), 0, 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else if (cond == COND_GE)
    {
        emit32le(blt_imm(xn(r), 0, 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else if (cond == COND_GT)
    {
        emit32le(bge_imm(0, xn(r), 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else if (cond == COND_LE)
    {
        emit32le(blt_imm(0, xn(r), 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
    else
    {
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(target, pos);
    }
}


void riscv64::dbra_local(local_t d, byte_p target)
// ----------------------------------------------------------------------------
//   Decrement local d and branch if non-zero
// ----------------------------------------------------------------------------
{
    record(riscv64, "dbra_local(%u, %p)", d, target);
    decr_local(d);
    bra_test(d, target, COND_NE);
}


void riscv64::rel_branch(byte_p target, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative branch site to bytecode target
// ----------------------------------------------------------------------------
{
    intptr_t off = branch(target, site);
    if (off)
        patch(site, site + off);
}


void riscv64::rel_function(tok_t tok, hwcode_t site)
// ----------------------------------------------------------------------------
//   Record or patch a relative site to a function token
// ----------------------------------------------------------------------------
{
    intptr_t off = function(tok, site);
    if (off)
        patch(site, site + off);
}


void riscv64::ijmp_local(local_t s)
// ----------------------------------------------------------------------------
//   Indirect jump through local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "ijmp_local(%u)", s);
    riscv64reg_t r = hw_local(s);
    emit32le(mv_rr(xn(HW_IP), xn(r)));
    emit32le(jalr(xn(HW_IP)));
}


void riscv64::icall_local(local_t s)
// ----------------------------------------------------------------------------
//   Indirect call through local s
// ----------------------------------------------------------------------------
{
    record(riscv64, "icall_local(%u)", s);
    riscv64reg_t r = hw_local(s);
    emit32le(mv_rr(xn(HW_IP), xn(r)));
    emit32le(jalr_ra(xn(HW_IP)));
}


void riscv64::push_pc()
// ----------------------------------------------------------------------------
//   Push continue address onto the bytecode soft stack (stub for Task 3)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "push_pc stub");
    emit32le(ebreak_insn());
}


void riscv64::load_pc(local_t d)
// ----------------------------------------------------------------------------
//   Load current native PC into local d
// ----------------------------------------------------------------------------
{
    record(riscv64, "load_pc(%u)", d);
    riscv64reg_t r = hw_local(d);
    emit32le(auipc(xn(r), 0));
}


void riscv64::load_pca(local_t d, broff_t b)
// ----------------------------------------------------------------------------
//   Load bytecode PC plus offset into local d (stub for Task 3)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "load_pca stub d=%u b=%lld", d, b);
    (void) b;
    riscv64reg_t r = hw_local(d);
    emit32le(addi(xn(r), 0, 0));
}


void riscv64::get_arg(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Load argument s into local d via frame
// ----------------------------------------------------------------------------
{
    record(riscv64, "get_arg(%u, %u)", d, s);
    riscv64reg_t dr = hw_local(d);
    int disp = arg_disp(s);
    emit_load_off(dr, HW_FRAME, disp);
}


void riscv64::set_arg(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Store local s into argument slot d via frame
// ----------------------------------------------------------------------------
{
    record(riscv64, "set_arg(%u, %u)", d, s);
    riscv64reg_t sr = hw_local(s);
    int disp = arg_disp(d);
    emit_store_off(HW_FRAME, sr, disp);
}


void riscv64::push_local(local_t s)
// ----------------------------------------------------------------------------
//   Push local s onto native stack (stub for Task 3 soft-stack)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "push_local stub s=%u", s);
    riscv64reg_t r = hw_local(s);
    emit_push(r);
}


void riscv64::pop_local(local_t d)
// ----------------------------------------------------------------------------
//   Pop stack value into local d (stub for Task 3 soft-stack)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "pop_local stub d=%u", d);
    riscv64reg_t r = hw_local(d);
    emit_pop(r);
}


void riscv64::push_flags()
// ----------------------------------------------------------------------------
//   Push flags register onto stack
// ----------------------------------------------------------------------------
{
    record(riscv64, "push_flags()");
    emit_push(HW_FLAGS);
}


void riscv64::pop_flags()
// ----------------------------------------------------------------------------
//   Pop stack value into flags register
// ----------------------------------------------------------------------------
{
    record(riscv64, "pop_flags()");
    emit_pop(HW_FLAGS);
}


void riscv64::store_global(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Store local s into global slot d (stub for Task 3)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "store_global stub d=%u s=%u", d, s);
    (void) d; (void) s;
    emit32le(ebreak_insn());
}


void riscv64::load_global(local_t d, local_t s)
// ----------------------------------------------------------------------------
//   Load global slot s into local d (stub for Task 3)
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "load_global stub d=%u s=%u", d, s);
    (void) d; (void) s;
    emit32le(ebreak_insn());
}


void riscv64::dispatch(local_t loc, byte_p &pc, byte cond)
// ----------------------------------------------------------------------------
//   Dispatch a branch table
// ----------------------------------------------------------------------------
{
    riscv64reg_t ref = hw_local(loc);
    size_t ncases = leb128<size_t>(pc);
    for (size_t n = 0; n < ncases; n++)
    {
        value_t test = leb128<value_t>(pc);
        byte_p  bpc  = pc;
        broff_t offs = leb128<broff_t>(pc);
        byte_p  tgt  = bpc + offs;

        load_imm64(HW_IP, uint64_t(test));
        // Compare ref == HW_IP; skip if not equal
        emit32le(bne_imm(xn(ref), xn(HW_IP), 8));
        hwcode_t pos = cursor();
        emit32le(j_imm(0));
        rel_branch(tgt, pos);
    }
}


void riscv64::trap(cstring msg)
// ----------------------------------------------------------------------------
//   Emit debug trap with message
// ----------------------------------------------------------------------------
{
    record(riscv64_warning, "trap(%s)", msg);
    emit32le(ebreak_insn());
}



// ============================================================================
//
//   Table-generated opcode emitters
//
// ============================================================================

#define EMIT(op)        void riscv64::emit_##op(emit_args &e)

#include "riscv64-emit.cpp"
