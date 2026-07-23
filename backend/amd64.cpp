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


cstring amd64::machine_defs() const override
// ----------------------------------------------------------------------------
//   Machine definition for 32-bit amd64
// ----------------------------------------------------------------------------
{
    return "__x86_64__\0__amd64__\0";
}


amd64::reginfo amd64::register_definition(hwreg_t reg) const
// ----------------------------------------------------------------------------
//   Return the register specification flags for amd64
// ----------------------------------------------------------------------------
{
    static const reginfo amd64_registers[x86::HW_COUNT] =
    {
        {    "rax",                      0 },
        {    "rcx",                      0 },
        {    "rdx",                      0 },
        {    "rbx", RSPEC_CALL | RSPEC_INT },
        {    "rsp",                      0 },
        {    "rbp",                      0 },
        {    "rsi",                      0 },
        {    "rdi",              RSPEC_INT },
        {     "r8",              RSPEC_INT },
        {     "r9",              RSPEC_INT },
        {    "r10",              RSPEC_INT },
        {    "r11", RSPEC_CALL | RSPEC_INT },
        {    "r12",              RSPEC_INT },
        {    "r13",              RSPEC_INT },
        {    "r14",              RSPEC_INT },
        {    "r15", RSPEC_CALL | RSPEC_INT },

        {  "xmm0d",              RSPEC_DBL },
        {  "xmm1d",              RSPEC_DBL },
        {  "xmm2d",              RSPEC_DBL },
        {  "xmm3d",              RSPEC_DBL },
        {  "xmm4d",              RSPEC_DBL },
        {  "xmm5d",              RSPEC_DBL },
        {  "xmm6d",              RSPEC_DBL },
        {  "xmm7d",              RSPEC_DBL },
        {  "xmm8d",              RSPEC_DBL },
        {  "xmm9d",              RSPEC_DBL },
        { "xmm10d",              RSPEC_DBL },
        { "xmm11d",              RSPEC_DBL },
        { "xmm12d",              RSPEC_DBL },
        { "xmm13d",              RSPEC_DBL },
        { "xmm14d",              RSPEC_DBL },
        { "xmm15d",              RSPEC_DBL },

        {  "xmm0f",              RSPEC_FLT },
        {  "xmm1f",              RSPEC_FLT },
        {  "xmm2f",              RSPEC_FLT },
        {  "xmm3f",              RSPEC_FLT },
        {  "xmm4f",              RSPEC_FLT },
        {  "xmm5f",              RSPEC_FLT },
        {  "xmm6f",              RSPEC_FLT },
        {  "xmm7f",              RSPEC_FLT },
        {  "xmm8f",              RSPEC_FLT },
        {  "xmm9f",              RSPEC_FLT },
        { "xmm10f",              RSPEC_FLT },
        { "xmm11f",              RSPEC_FLT },
        { "xmm12f",              RSPEC_FLT },
        { "xmm13f",              RSPEC_FLT },
        { "xmm14f",              RSPEC_FLT },
        { "xmm15f",              RSPEC_FLT },
    };

    return reg < HW_COUNT ? amd64_registers[reg] : reginfo{ "INVALID", 0 };
}


void amd64::prologue()
// ----------------------------------------------------------------------------
//   Emit standard function prologue
// ----------------------------------------------------------------------------
{
    record(amd64, "prologue");

    // SysV and Win64: rbx,rbp,r12,r15 are callee-saved.  r12/r15 hold
    // frame/flags for the whole translation
    emit_push_reg(HW_RBX);
    emit_push_reg(HW_R12);
    emit_push_reg(HW_R15);
    emit_push_reg(HW_RBP);
}


void amd64::epilogue()
// ----------------------------------------------------------------------------
//   Emit epilogue (restore callee-saved) and return
// ----------------------------------------------------------------------------
{
    record(amd64, "epilogue()");
    emit_pop_reg(HW_RBP);
    emit_pop_reg(HW_R15);
    emit_pop_reg(HW_R12);
    emit_pop_reg(HW_RBX);
    emit_insn_ret();
}
