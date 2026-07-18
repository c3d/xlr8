#ifndef OP_H
#define OP_H
// ****************************************************************************
//  op.h                                                           XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Built-in opcode enumeration for the code generation backend.
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

#include "types.h"


enum op_t : byte
{
#define OP(op, decl, code) op_##op,
#include "opcodes.tbl"
    OP_COUNT
};
#undef OP

#endif // OP_H
