// ****************************************************************************
//  backend.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Base class for machine-specific code generation backends
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

#include "arm.h"
#include "arm64.h"
#include "i386.h"
#include "leb128.h"
#include "native-bridge.h"
#include "riscv64.h"
#include "save.h"
#include "x86_64.h"

#include <cctype>
#include <cstring>


RECORDER(backend,           64, "Architecture-neutral code generation");
RECORDER(backend_ops,       64, "Architecture-neutral opcodes");
RECORDER(backend_args,      64, "Architecture-neutral bytecode arguments");
RECORDER(backend_error,     16, "Architecture-neutral code generation errors");
RECORDER(backend_warning,   16, "Architecture-neutral code generation warnings");


backend::backend(arch_t arch)
// ----------------------------------------------------------------------------
//   Initialize shared backend state
// ----------------------------------------------------------------------------
    : current_arch(arch),
      current_output(OUTPUT_MEMORY),
      nocode_wanted(false),
      verbose(false),
      text_section(),
      index()
{}


void backend::g(byte c)
// ----------------------------------------------------------------------------
//   Emit one byte into the current text section
// ----------------------------------------------------------------------------
{
    if (nocode_wanted)
        return;
    text_section.append(c);
}


void backend::emit32le(uint32_t c)
// ----------------------------------------------------------------------------
//   Emit a 32-bit little-endian value
// ----------------------------------------------------------------------------
{
    record(backend, "emit32le(0x%llX)", c);
    text_section.append32le(c);
}


void backend::gsym(uint t)
// ----------------------------------------------------------------------------
//   Patch pending jumps to the current code position
// ----------------------------------------------------------------------------
{
    gsym_addr(t, uint(index));
}


void backend::set_output(output_t output)
// ----------------------------------------------------------------------------
//   Select the output format (memory, object file, executable, ...)
// ----------------------------------------------------------------------------
{
    current_output = output;
}


bool backend::set_options(cstring options)
// ----------------------------------------------------------------------------
//   Parse backend options (TinyCC's tcc_set_options analogue)
// ----------------------------------------------------------------------------
{
    if (!options)
        return true;

    std::string opt = options;
    size_t pos = 0;
    while (pos < opt.size())
    {
        while (pos < opt.size() && std::isspace(opt[pos]))
            pos++;
        if (pos >= opt.size())
            break;

        size_t start = pos;
        while (pos < opt.size() && !std::isspace(opt[pos]))
            pos++;
        std::string token = opt.substr(start, pos - start);

        if (token == "-v" || token == "--verbose")
            verbose = true;
        else if (token == "-nostdlib")
            { /* reserved for future link options */ }
        else if (token == "-m32")
        {
            if (current_arch != ARCH_I386)
                return false;
        }
        else if (token == "-m64")
        {
            if (current_arch != ARCH_X86_64)
                return false;
        }
        else if (token == "-marm" || token == "-marm32")
        {
            if (current_arch != ARCH_ARM)
                return false;
        }
        else if (token == "-marm64" || token == "-maarch64")
        {
            if (current_arch != ARCH_ARM64)
                return false;
        }
        else if (token == "-mriscv64")
        {
            if (current_arch != ARCH_RISCV64)
                return false;
        }
        else if (token == "-r" || token == "-c")
            set_output(OUTPUT_OBJ);
        else if (token == "-shared")
            set_output(OUTPUT_DLL);
        else if (token.substr(0, 2) == "-O")
            { /* optimization level - reserved */ }
        else
            return false;
    }
    return true;
}



// ============================================================================
//
//   Code generation
//
// ============================================================================

static const cstring opcode_name_table[] =
// ----------------------------------------------------------------------------
//   Table giving the name for each opcode
// ----------------------------------------------------------------------------
{
#define OP(op, decl, code)      #op,
#include "opcodes.tbl"
};


static inline cstring opcode_name(tok_t op)
// ----------------------------------------------------------------------------
//   Return the name of the given opcode
// ----------------------------------------------------------------------------
{
    if (op < sizeof(opcode_name_table) / sizeof(opcode_name_table[0]))
        return opcode_name_table[op];
    return "UNKNOWN";
}


void backend::translate_subroutine(byte_p &pc, byte_p end)
// ----------------------------------------------------------------------------
//   Translate a bytecode range, or until failure (if open-ended)
// ----------------------------------------------------------------------------
{
    record(backend, "translate_sub(%p, %p)", pc, end);

    emit_args args(pc);
    while (translating && (!end || pc < end))
    {
        byte_p insn        = pc;

        // Check if this was already translated (e.g. through branch)
        auto bcp = bc_positions.find(insn);
        if (bcp != bc_positions.end())
        {
            uint j = gjmp(0);
            gsym_addr(j, (*bcp).second);
            break;
        }

        // Otherwise insert translation in map and run opcode
        op_t op            = op_t(*pc++);
        bc_positions[insn] = cursor();
        args.ppc           = insn;
        args.op            = op;
        args.pc            = pc;

        record(backend_ops, "%p: op %+s (%u)", insn, opcode_name(op), op);
        switch (op)
        {
#define TRC(n)  record(backend_args, #n "=", args.n);
#define REG(n)  args.n = *pc++;                         TRC(n)
#define DST     REG(dst)
#define SRC     REG(src)
#define XTRA    REG(xtra)
#define PRED    REG(pred)
#define LEN     REG(len)
#define IVAL    REG(ival)
#define UVAL    REG(uval)
#define TOK     REG(tok)
#define KSHIFT  REG(kshift)
#define BRA                           \
    args.bpc   = pc;                  \
    args.broff = leb128<broff_t>(pc); \
    TRC(bpc) TRC(broff)
#define KINT    args.kint = leb128<value_t>(pc);        TRC(kint)
#define KNAT    args.knat = leb128<uvalue_t>(pc);       TRC(knat)
#define KLEN    args.klen = leb128<size_t>(pc);         TRC(klen)
#define KTYPE(n) memcpy(&args.n, pc, sizeof(args.n)); pc += sizeof(args.n);
#define KFLT    KTYPE(kflt)
#define KDBL    KTYPE(kdbl)

#define OP(op, decl, code)                      \
        case op_##op:                           \
            decl;                               \
            args.pc = pc;                       \
            emit_##op(args);                    \
            break;
#include "opcodes.tbl"
        default:
            emit_user(args);
            break;
        }
    }
    if (args.error)
        record(backend_error, "Error during translation: %+s", args.error);
}


bool backend::translate(byte_p &pc)
// ----------------------------------------------------------------------------
//   Translate bytecode until an end opcode is seen
// ----------------------------------------------------------------------------
{
    record(backend, "translate(%p)", &pc);
    translate_base        = pc;
    pending_branch_target = 0;
    branch_targets.clear();
    entry_targets.clear();

    begin_translation();
    translate_subroutine(pc, nullptr);

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
            if (bc_positions.count(target))
                continue;
            progress = true;
            byte_p end = xlr8_bytecode_end(impl_tok);
            record(backend, "translate_subroutine callee %p..%p", target, end);
            translate_subroutine(target, end);
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
    record(backend, "begin_translation");
    translating = true;
}


void backend::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "finish_translation");
    translating = false;
}


void backend::emit_user(backend::emit_args &args)
// ----------------------------------------------------------------------------
//   Hook for user-defined opcodes (128..255)
// ----------------------------------------------------------------------------
{
    record(backend, "emit_user op=%u pc=%p", args.op, args.pc);
}


void backend::mark_entry(byte_p target)
// ----------------------------------------------------------------------------
//   Remember a label that may be entered without a branch insn (pca, etc.)
// ----------------------------------------------------------------------------
{
    record(backend, "mark_entry target=%p", target);
    entry_targets.insert(target);
}


byte_p backend::next_live_code(byte_p from)
// ----------------------------------------------------------------------------
//   Find next not-yet-translated branch target at or after from
// ----------------------------------------------------------------------------
{
    for (auto it = branch_targets.lower_bound(from);
         it != branch_targets.end();
         ++it)
    {
        if (!bc_positions.count(*it))
            return *it;
    }
    return nullptr;
}


void backend::redirect_after_bra(byte_p target, byte_p &pc)
// ----------------------------------------------------------------------------
//   After unconditional bra, do not translate dead fall-through bytes
// ----------------------------------------------------------------------------
{
    byte_p fallthrough = pc;
    byte_p dest        = target;
    byte_p ft          = fallthrough;

    if (dest > ft)
    {
        for (byte_p t : branch_targets)
        {
            if (t >= ft && t < dest && !bc_positions.count(t))
            {
                record(backend, "redirect_after_bra resume %p", t);
                pc = t;
                return;
            }
        }
        if (!bc_positions.count(dest))
        {
            byte_p ft_addr = fallthrough;
            if (!branch_targets.count(ft_addr) && !entry_targets.count(ft_addr))
            {
                record(backend, "redirect_after_bra forward %p", target);
                pc = target;
                return;
            }
            record(backend, "redirect_after_bra linear %p", fallthrough);
            return;
        }
    }

    if (!bc_positions.count(dest))
    {
        record(backend, "redirect_after_bra to %p", target);
        pc = target;
        return;
    }

    record(backend, "redirect_after_bra back-edge %p fallthrough %p",
           target, fallthrough);
    byte_p next = next_live_code(fallthrough);
    if (next)
    {
        record(backend, "redirect_after_bra resume %p", next);
        pc = next;
    }
    else
    {
        record(backend, "redirect_after_bra done");
        translating = false;
    }
}


void backend::ensure_branch_target_translated(byte_p target)
// ----------------------------------------------------------------------------
//   Translate bytecode at target if not already done (for branch fixups)
// ----------------------------------------------------------------------------
{
    if (bc_positions.count(target))
        return;
    record(backend, "ensure_branch_target_translated %p", target);
    byte_p pc = target;
    save   st(translating, true);
    translate_subroutine(pc, nullptr);
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
                auto bc = bc_positions.find(target);
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
        if (!bc_positions.count(fx.target))
        {
            if (!fx.target)
            {
                record(backend_warning,
                       "branch_fixup skip null target insn_pos=%zu",
                       fx.insn_pos);
                continue;
            }
            ensure_branch_target_translated(fx.target);
        }
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


// ============================================================================
//
//   Architecture management
//
// ============================================================================

backend *backend::make(arch_t arch)
// ----------------------------------------------------------------------------
//   Create a code generator for the requested architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:     return i386_be::make();
    case ARCH_X86_64:   return x86_64::make();
    case ARCH_ARM:      return arm::make();
    case ARCH_ARM64:    return arm64::make();
    case ARCH_RISCV64:  return riscv64::make();
    default:
        record(backend_error, "Unknown arch %u, defaulting to x86-64", arch);
    }
    return x86_64::make();
}


cstring backend::arch_name(arch_t arch)
// ----------------------------------------------------------------------------
//   Return a human-readable name for an architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:     return "i386";
    case ARCH_X86_64:   return "x86_64";
    case ARCH_ARM:      return "arm";
    case ARCH_ARM64:    return "arm64";
    case ARCH_RISCV64:  return "riscv64";
    case ARCH_NONE:     return "none";
    }
    record(backend_error, "arch_name for unknown %u", arch);
    return "unknown";
}
