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


RECORDER(amd64,            256, "X86-64/AMD64 family code generation helpers");
RECORDER(amd64_ops,        256, "X86-64/AMD64 family opcode emission");
RECORDER(amd64_error,       64, "X86-64/AMD64 family code generation errors");
RECORDER(amd64_warning,     64, "X86-64/AMD64 family code generation warnings");



amd64::amd64(arch_t arch)
// ----------------------------------------------------------------------------
//   Construct an amd64 family code generator
// ----------------------------------------------------------------------------
    : x86(arch)
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


void amd64::begin_translation()
// ----------------------------------------------------------------------------
//   Save callee-saved regs, prime frame/flags and registers for locals
// ----------------------------------------------------------------------------
{
    record(amd64, "begin_translation()");
    backend::begin_translation();
    do_prologue();
}


void amd64::finish_translation()
// ----------------------------------------------------------------------------
//   Restore stack and callee-saved registers, then return
// ----------------------------------------------------------------------------
{
    record(amd64, "finish_translation()");
    do_ret();
    backend::finish_translation();
}


void amd64::syscall(void (*target)())
// ----------------------------------------------------------------------------
//   Implement a syscall to the runtime
// ----------------------------------------------------------------------------
{
    record(amd64, "syscall(%p)", target);
    call_ptr((void *) target);
}


void amd64::do_prologue()
// ----------------------------------------------------------------------------
//   Emit standard function prologue
// ----------------------------------------------------------------------------
{
    record(amd64, "do_prologue");

    // SysV and Win64: rbx,rbp,r12,r15 are callee-saved.  r12/r15 hold
    // frame/flags for the whole translation
    emit_push_reg(HW_RBX);
    emit_push_reg(HW_R12);
    emit_push_reg(HW_R15);
    emit_push_reg(HW_RBP);
}


void amd64::do_ret()
// ----------------------------------------------------------------------------
//   Emit epilogue (restore callee-saved) and return
// ----------------------------------------------------------------------------
{
    record(amd64, "do_ret()");
    emit_pop_reg(HW_RBP);
    emit_pop_reg(HW_R15);
    emit_pop_reg(HW_R12);
    emit_pop_reg(HW_RBX);
    emit_insn_ret();
}



#define EMIT(op)        void amd64::emit_##op(emit_args &e)

#include "x86-emit.cpp"
