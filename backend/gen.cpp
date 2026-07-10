// ****************************************************************************
//  gen.cpp                                                         XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Opcode dispatcher for bytecode to native code translation
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

#include "backend.h"

#include "leb128.h"
#include "native-bridge.h"

#include <cstring>


bool backend::translate(byte_p &pc)
// ----------------------------------------------------------------------------
//   Translate bytecode until an end opcode is seen
// ----------------------------------------------------------------------------
{
    translating = true;
    while (translating)
    {
        op_t op = op_t(*pc++);
        switch (op)
        {
#define OP(op, code) case op_##op: { code; } break;
#include "emit.tbl"
#undef OP
        default:
            emit_user(op, pc);
            break;
        }
    }
    resolve_labels();
    finish_translation();
    return true;
}


void backend::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
}


void backend::emit_user(op_t op, byte_p &pc)
// ----------------------------------------------------------------------------
//   Hook for user-defined opcodes (128..255)
// ----------------------------------------------------------------------------
{
    (void) op;
    (void) pc;
}


void backend::record_branch(broff_t offset, uint pos)
// ----------------------------------------------------------------------------
//   Record a PC-relative branch for later patching
// ----------------------------------------------------------------------------
{
    (void) offset;
    branch_patches.push_back(pos);
}


void backend::record_tok_jump(tok_t t, uint pos)
// ----------------------------------------------------------------------------
//   Record a token-based jump for later patching
// ----------------------------------------------------------------------------
{
    tok_jumps[t] = pos;
}


void backend::patch_branch(uint pos, broff_t offset)
// ----------------------------------------------------------------------------
//   Patch a recorded branch site
// ----------------------------------------------------------------------------
{
    code().patch32(pos - 4, uint32_t(offset));
}


void backend::resolve_labels()
// ----------------------------------------------------------------------------
//   Patch token-based jumps once all labels are known
// ----------------------------------------------------------------------------
{
    for (auto &entry : tok_jumps)
    {
        auto it = labels.find(entry.first);
        if (it == labels.end())
            continue;
        gsym_addr(entry.second, uint(it->second));
    }
}
