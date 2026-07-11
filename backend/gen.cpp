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


void backend::translate_sub(byte_p &pc, byte_p end)
// ----------------------------------------------------------------------------
//   Translate a subroutine until ret (if open-ended) or end of bytecode range
// ----------------------------------------------------------------------------
{
    record(backend, "translate_sub(%p, %p)", pc, end);
    while (!end || pc < end)
    {
        byte_p insn = pc;
        op_t op = op_t(*pc++);
        bc_positions[uintptr_t(insn)] = cursor();
        if (op == op_ret)
        {
            emit_ret();
            if (!end)
                return;
            continue;
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
    record(backend, "translate(%p)", &pc);
    translating = true;
    translate_base = pc;
    pending_branch_target = 0;
    begin_translation();
    while (translating)
    {
        byte_p insn = pc;
        op_t op = op_t(*pc++);
        bc_positions[uintptr_t(insn)] = cursor();
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

    over_callees_at = emit_over_callees_branch();

    bool progress = true;
    while (progress)
    {
        progress = false;
        for (auto &entry : tok_jumps)
        {
            tok_t impl_tok = entry.first;
            byte_p target = xlr8_resolve_impl(entry.first, &impl_tok);
            record(backend, "tok callee token=%llu impl=%llu -> %p",
                   entry.first, impl_tok, target);
            if (!target)
                continue;
            if (bc_positions.count(uintptr_t(target)))
                continue;
            progress = true;
            byte_p end = xlr8_bytecode_end(impl_tok);
            record(backend, "translate_sub callee %p .. %p", target, end);
            translate_sub(target, end);
        }
    }

    resolve_labels();
    size_t epilogue = cursor();
    finish_translation();
    if (over_callees_at)
        patch_branch_site(over_callees_at, epilogue);
    return true;
}


void backend::begin_translation()
// ----------------------------------------------------------------------------
//   Emit prologue at start of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "begin_translation()");
}


void backend::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "finish_translation()");
}


void backend::emit_user(op_t op, byte_p &pc)
// ----------------------------------------------------------------------------
//   Hook for user-defined opcodes (128..255)
// ----------------------------------------------------------------------------
{
    record(backend, "emit_user(%lld, %p)", op, &pc);
    (void) op;
    (void) pc;
}


void backend::set_branch_target(byte_p target)
// ----------------------------------------------------------------------------
//   Remember bytecode target offset for the next branch fixup
// ----------------------------------------------------------------------------
{
    record(backend, "set_branch_target(%p)", target);
    pending_branch_target = uintptr_t(target);
}


void backend::record_branch(broff_t offset, uint pos)
// ----------------------------------------------------------------------------
//   Record a PC-relative branch for later patching
// ----------------------------------------------------------------------------
{
    record(backend, "record_branch(target=%p, insn_pos=%u, leb=%lld)",
           (void *) pending_branch_target, pos, offset);
    if (!pending_branch_target)
        return;
    branch_fixups.push_back({ pending_branch_target, pos });
    pending_branch_target = 0;
}


void backend::patch_branch_site(size_t insn_pos, size_t target_native)
// ----------------------------------------------------------------------------
//   Default branch patching (overridden per target)
// ----------------------------------------------------------------------------
{
    record(backend, "patch_branch_site(%zu, %zu)", insn_pos, target_native);
    (void) insn_pos;
    (void) target_native;
}


void backend::record_tok_jump(tok_t t, uint pos)
// ----------------------------------------------------------------------------
//   Record a token-based jump for later patching
// ----------------------------------------------------------------------------
{
    record(backend, "record_tok_jump(%llu, %u)", t, pos);
    tok_jumps[t] = pos;
}


void backend::patch_branch(uint pos, broff_t offset)
// ----------------------------------------------------------------------------
//   Patch a recorded branch site
// ----------------------------------------------------------------------------
{
    record(backend, "patch_branch(%u, %lld)", pos, offset);
    code().patch32(pos - 4, uint32_t(offset));
}


void backend::resolve_labels()
// ----------------------------------------------------------------------------
//   Patch token-based jumps once all labels are known
// ----------------------------------------------------------------------------
{
    record(backend, "resolve_labels()");
    for (auto &entry : tok_jumps)
    {
        size_t native = 0;
        auto it = labels.find(entry.first);
        if (it != labels.end())
            native = it->second;
        else
        {
            tok_t impl_tok = entry.first;
            byte_p target = xlr8_resolve_impl(entry.first, &impl_tok);
            if (target)
            {
                auto bc = bc_positions.find(uintptr_t(target));
                if (bc != bc_positions.end())
                    native = bc->second;
            }
        }
        if (!native)
        {
            record(backend_warning, "tok_jump miss token=%llu site=%u",
                   entry.first, entry.second);
            continue;
        }
        record(backend, "patching tok_jump token=%llu site=%u -> native=%zu",
               entry.first, entry.second, native);
        gsym_addr(entry.second, uint(native));
    }

    for (branch_fixup &fx : branch_fixups)
    {
        auto it = bc_positions.find(fx.target);
        if (it == bc_positions.end())
        {
            auto next = bc_positions.lower_bound(fx.target);
            if (next != bc_positions.end())
                it = next;
        }
        if (it == bc_positions.end())
        {
            record(backend_warning, "branch_fixup miss target=%p insn_pos=%zu",
                   (void *) fx.target, fx.insn_pos);
            continue;
        }
        record(backend, "patching branch insn_pos=%zu -> native=%zu",
               fx.insn_pos, it->second);
        patch_branch_site(fx.insn_pos, it->second);
    }
}
