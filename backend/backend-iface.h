// ****************************************************************************
//  backend-iface.h                                                XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Virtual functions that each backend needs to override
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


// Machine definition
cstring         machine_defs() const                            override;
uint            registers() const                               override;
uint            pointer_size() const                            override;

// Setup and teardown translation
void            begin_translation()                             override;
void            finish_translation()                            override;

// Jumps and syscalls
void            patch_branch(hwcode_t data, hwcode_t tgt)       override;
void            call(void (*target)())                          override;
void            syscall(void (*target)())                       override;

// Relocations
uint            relocate(void *base)                            override;

// Hardware registers
bool            move(hwreg_t to, hwreg_t from)                  override;
bool            load(hwreg_t hw, local_t loc)                   override;
bool            store(local_t loc, hwreg_t hw)                  override;
reginfo         register_definition(hwreg_t hw) const           override;

// Code generation interface for opcodes
#define OP(op, code, decl)                                              \
virtual void    emit_##op(emit_args &args)                      override;
#include "opcodes.tbl"

// Code generation interface for sys opcodes
#define NATIVE(op, code, decl)                                          \
virtual void    emit_sys_##op(emit_args &args)                  override;
#include "native.tbl"

// Code generation for user-defined / unimplemented opcode
void            emit_user(emit_args &args)                      override;
