// ****************************************************************************
//  amd64.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Implementation of amd64_64 / AMD64 code generation
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

#include "amd64.h"


RECORDER(amd64,             32, "X86-64/AMD64 family code generation helpers");
RECORDER(amd64_ops,         32, "X86-64/AMD64 family opcode emission");
RECORDER(amd64_error,       16, "X86-64/AMD64 family code generation errors");
RECORDER(amd64_warning,     16, "X86-64/AMD64 family code generation warnings");



amd64::amd64(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an amd64 family code generator
// ----------------------------------------------------------------------------
    : x86(rt, arch)
{
    record(amd64, "amd64(%+s)", arch_name(arch));
}


cstring amd64::machine_defs() const
// ----------------------------------------------------------------------------
//   Machine definition for 32-bit amd64
// ----------------------------------------------------------------------------
{
    return "__x86_64__\0__amd64__\0";
}


uint amd64::pointer_size() const
// ----------------------------------------------------------------------------
//   Pointer size for AMD64
// ----------------------------------------------------------------------------
{
    return 8;
}


amd64::reginfo amd64::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for amd64 direct-map allocator
// ----------------------------------------------------------------------------
//   RSPEC_INT marks registers usable as integer local homes or temps.
//   RSPEC_CALL marks callee-saved (survive C/sys without spill).
//   Registers with spec 0 are fixed-role (frame, stack, scratch, flags).
{
    static const reginfo amd64_registers[x86::HW_COUNT] =
    {
        {    "rax",              RSPEC_INT }, // HW_TEMP0
        {    "rcx",                      0 }, // HW_SCRATCH
        {    "rdx",              RSPEC_INT }, // HW_TEMP1
        {    "rbx", RSPEC_CALL | RSPEC_INT }, // iorder[0]
        {    "rsp",                      0 }, // stack
        {    "rbp",                      0 }, // HW_FRAME
        {    "rsi",              RSPEC_INT }, // iorder[5]
        {    "rdi",              RSPEC_INT }, // iorder[6]
        {     "r8",              RSPEC_INT }, // iorder[7]
        {     "r9",              RSPEC_INT }, // iorder[8]
        {    "r10",              RSPEC_INT }, // iorder[9]
        {    "r11",                      0 }, // HW_FLAGS
        {    "r12", RSPEC_CALL | RSPEC_INT }, // iorder[1]
        {    "r13", RSPEC_CALL | RSPEC_INT }, // iorder[2]
        {    "r14", RSPEC_CALL | RSPEC_INT }, // iorder[3]
        {    "r15", RSPEC_CALL | RSPEC_INT }, // iorder[4]

        {  "xmm0d",              RSPEC_DBL }, // FP shadow
        {  "xmm1d",              RSPEC_DBL },
        {  "xmm2d",              RSPEC_DBL },
        {  "xmm3d",              RSPEC_DBL },
        {  "xmm4d",              RSPEC_DBL },
        {  "xmm5d",              RSPEC_DBL },
        {  "xmm6d",              RSPEC_DBL },
        {  "xmm7d",              RSPEC_DBL },
        {  "xmm8d",              RSPEC_DBL }, // HW_DTEMP0
        {  "xmm9d",              RSPEC_DBL },
        { "xmm10d",                      0 },
        { "xmm11d",                      0 },
        { "xmm12d",                      0 },
        { "xmm13d",                      0 },
        { "xmm14d",                      0 },
        { "xmm15d",                      0 },

        {  "xmm0f",              RSPEC_FLT }, // FP shadow
        {  "xmm1f",              RSPEC_FLT },
        {  "xmm2f",              RSPEC_FLT },
        {  "xmm3f",              RSPEC_FLT },
        {  "xmm4f",              RSPEC_FLT },
        {  "xmm5f",              RSPEC_FLT },
        {  "xmm6f",              RSPEC_FLT },
        {  "xmm7f",              RSPEC_FLT },
        {  "xmm8f",              RSPEC_FLT }, // HW_FTEMP0
        {  "xmm9f",              RSPEC_FLT },
        { "xmm10f",                      0 },
        { "xmm11f",                      0 },
        { "xmm12f",                      0 },
        { "xmm13f",                      0 },
        { "xmm14f",                      0 },
        { "xmm15f",                      0 },
    };

    return reg < HW_COUNT ? amd64_registers[reg] : reginfo{ "INVALID", 0 };
}


// Prologue/epilogue for amd64 are handled by x86::prologue/epilogue.
// No amd64-specific override needed; x86 pushes/pops rbx, r12–r15.
