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


#include <cstring>


void backend::translate_sub(byte_p &pc)
// ----------------------------------------------------------------------------
//   Translate a subroutine until a ret opcode
// ----------------------------------------------------------------------------
{
    while (true)
    {
        byte_p insn = pc;
        op_t op = op_t(*pc++);
        bc_positions[size_t(insn - translate_base)] = cursor();
        if (op == op_ret)
        {
            emit_ret();
            return;
        }
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
}


bool backend::translate(byte_p &pc)
// ----------------------------------------------------------------------------
//   Translate bytecode until an end opcode is seen
// ----------------------------------------------------------------------------
{
    translating = true;
    translate_base = pc;
    begin_translation();
    while (translating)
    {
        byte_p insn = pc;
        op_t op = op_t(*pc++);
        bc_positions[size_t(insn - translate_base)] = cursor();
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

    for (auto &entry : tok_jumps)
    {
        byte_p target = xlr8_resolve(entry.first);
        if (!target || target < translate_base)
            continue;
        size_t off = size_t(target - translate_base);
        if (bc_positions.count(off))
            continue;
        translate_sub(target);
    }

    resolve_labels();
    finish_translation();
    return true;
}


void backend::begin_translation()
// ----------------------------------------------------------------------------
//   Emit prologue at start of translated code
// ----------------------------------------------------------------------------
{
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


void backend::set_branch_target(byte_p target)
// ----------------------------------------------------------------------------
//   Remember bytecode target offset for the next branch fixup
// ----------------------------------------------------------------------------
{
    if (translate_base)
        pending_branch_off = size_t(target - translate_base);
}


void backend::record_branch(broff_t offset, uint pos)
// ----------------------------------------------------------------------------
//   Record a PC-relative branch for later patching
// ----------------------------------------------------------------------------
{
    (void) offset;
    branch_fixups.push_back({ pending_branch_off, pos - 4 });
}


void backend::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Default branch patching (overridden per target)
// ----------------------------------------------------------------------------
{
    (void) insn_pos;
    (void) target_native;
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
        size_t native = 0;
        auto it = labels.find(entry.first);
        if (it != labels.end())
            native = it->second;
        else if (translate_base)
        {
            byte_p target = xlr8_resolve(entry.first);
            if (target)
            {
                auto bc = bc_positions.find(size_t(target - translate_base));
                if (bc != bc_positions.end())
                    native = bc->second;
            }
        }
        if (native)
            gsym_addr(entry.second, uint(native));
    }

    for (branch_fixup &fx : branch_fixups)
    {
        auto it = bc_positions.find(fx.target_off);
        if (it == bc_positions.end())
            continue;
        patch_branch_site(fx.insn_pos, it->second);
    }
}
