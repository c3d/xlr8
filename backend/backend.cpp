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

#include "leb128.h"
#include "native-bridge.h"
#include "save.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cassert>


RECORDER(backend,           16, "Architecture-neutral code generation");
RECORDER(backend_emit,      16, "Architecture-neutral bytecode emission");
RECORDER(backend_ops,       16, "Architecture-neutral opcodes");
RECORDER(backend_args,      16, "Architecture-neutral bytecode arguments");
RECORDER(backend_error,     16, "Architecture-neutral code generation errors");
RECORDER(backend_warning,   16, "Architecture-neutral code generation warnings");



// ============================================================================
//
//    General options
//
// ============================================================================

backend::backend(arch_t arch)
// ----------------------------------------------------------------------------
//   Initialize shared backend state
// ----------------------------------------------------------------------------
    : current_arch(arch),
      current_output(OUTPUT_MEMORY)
{}


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

    std::string opt     = options;
    size_t      pos     = 0;
    bool        verbose = false;
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
        {
            verbose = true;
        }
        else if (token == "-nostdlib")
        {
            /* reserved for future link options */
        }
        else if (token == "-m32")
        {
            if (current_arch != ARCH_I386)
                return false;
        }
        else if (token == "-m64")
        {
            if (current_arch != ARCH_AMD64 && current_arch != ARCH_AMD64_WIN)
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
        {
            set_output(OUTPUT_OBJ);
        }
        else if (token == "-shared")
        {
            set_output(OUTPUT_DLL);
        }
        else if (token.substr(0, 2) == "-O")
        {
            /* optimization level - reserved */
        }
        else
        {
            return false;
        }
    }

    if (verbose)
        recorder_trace_set("x86 amd64 arm arm64 riscv64");

    return true;
}



// ============================================================================
//
//   Low-level code emission (shared across architectures)
//
// ============================================================================

hwcode_t backend::emit8(byte c)
// ----------------------------------------------------------------------------
//   Emit one byte into the current text section
// ----------------------------------------------------------------------------
{
    record(backend_emit, "emit8 0x%02X", c);
    hwcode_t pos = text_section.size();
    text_section.append8(c);
    return pos;
}


void backend::emit32le(uint32_t c)
// ----------------------------------------------------------------------------
//   Emit a 32-bit little-endian value
// ----------------------------------------------------------------------------
{
    record(backend_emit, "emit32le 0x%08llX", c);
    text_section.append32le(c);
}


void backend::emit32be(uint32_t c)
// ----------------------------------------------------------------------------
//   Emit a 32-bit big-endian value
// ----------------------------------------------------------------------------
{
    record(backend_emit, "emit32be(0x%08llX)", c);
    text_section.append32be(c);
}


struct translation_compare
// ----------------------------------------------------------------------------
//   Comparator for translations based on bytecode, hwcode or lexicographic
// ----------------------------------------------------------------------------
{
    bool operator()(const backend::translation &t, byte_p bc)
    {
        return t.bytecode < bc;
    }
    bool operator()(byte_p bc, const backend::translation &t)
    {
        return bc < t.bytecode;
    }
    bool operator()(const backend::translation &t, hwcode_t mc)
    {
        return t.hwcode < mc;
    }
    bool operator()(hwcode_t mc, const backend::translation &t)
    {
        return mc < t.hwcode;
    }
    bool operator()(const backend::translation &t1,
                    const backend::translation &t2)
    {
        return (t1.bytecode < t2.bytecode ||
                (t1.bytecode == t2.bytecode && t1.hwcode < t2.hwcode));
    }
};


template<typename T>
auto backend::lower_bound(const translations &t, T v)
// ----------------------------------------------------------------------------
//   Find the lower bound by bytecode
// ----------------------------------------------------------------------------
{
    return std::lower_bound(t.begin(), t.end(), v, translation_compare{});
}


template<typename T>
auto backend::equal_range(const translations &t, T v)
// ----------------------------------------------------------------------------
//   Find the lower bound by bytecode
// ----------------------------------------------------------------------------
{
    return std::equal_range(t.begin(), t.end(), v, translation_compare{});
}


intptr_t backend::branch_offset(byte_p bytecode, hwcode_t hwcode)
// ----------------------------------------------------------------------------
//  Record that we have a branch, mapping bytecode to machine code
// ----------------------------------------------------------------------------
{
    // Lookup in labels to check if we already know the target
    byte_p  bpc    = bytecode;
    broff_t offset = leb128<broff_t>(bpc);
    byte_p  target = bytecode + offset;
    auto    label  = lower_bound(labels, target);

    // If it already exists, return the offset to the label
    if (label != labels.end() && label->bytecode == target)
        return label->hwcode - hwcode;

    // If it does not, add it to the list of branches to patch
    translation bt = { target, hwcode };
    auto branch = lower_bound(branches, bt);
    if (branch == branches.end() ||
        branch->bytecode != target || branch->hwcode != hwcode)
        branches.insert(branch, bt);
    return 0;
}


uint backend::translate_address(byte_p bytecode, hwcode_t hwcode)
// ----------------------------------------------------------------------------
//   Add an address translation, resolve labels if necessary
// ----------------------------------------------------------------------------
{
    uint count = 0;

    // Record what we are currently translating
    translating = bytecode;

    // Check if there are branches that need patching
    auto range = equal_range(branches, hwcode);
    for (auto b = range.first; b != range.second; ++b)
    {
        patch_branch(b->hwcode, hwcode);
        count++;
    }
    if (count)
        branches.erase(range.first, range.second);

    // Record the mapping from bytecode to machine code for future branches
    labels.emplace_back(translation{bytecode, hwcode});

    return count;
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


cstring backend::opcode_name(op_t op)
// ----------------------------------------------------------------------------
//   Return the name of the given opcode
// ----------------------------------------------------------------------------
{
    if (op < sizeof(opcode_name_table) / sizeof(opcode_name_table[0]))
        return opcode_name_table[op];
    return "UNKNOWN";
}


bool backend::translate(byte_p pc, byte_p end)
// ----------------------------------------------------------------------------
//   Translate a bytecode range, or until failure
// ----------------------------------------------------------------------------
{
    record(backend, "translate subroutine %p..%p", pc, end);

    emit_args args(pc, end);
    while (pc < end && !args.error)
    {
        // Insert translation in address map
        byte_p insn = pc;
        translate_address(pc, cursor());

        // Run that opcode
        op_t op            = op_t(*pc++);
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

        // It's possible that the opcode changed the pc or end
        pc  = args.pc;
        end = args.end;
    }
    if (args.error)
        record(backend_error, "Error during translation: %+s", args.error);
    return !args.error;
}


void backend::begin_translation()
// ----------------------------------------------------------------------------
//   Emit prologue at start of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "begin_translation");

    reg_local.clear();
    local_reg.clear();
    functions.clear();
    branches.clear();
    calls.clear();
    labels.clear();

    // Default assignment for registers and locals
    reg_local.resize(registers(), NO_LOCAL);
    local_reg.resize(DEFAULT_FRAME_SIZE, NO_REGISTER);
}


void backend::finish_translation()
// ----------------------------------------------------------------------------
//   Emit epilogue at end of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "finish_translation");
}


void backend::emit_user(backend::emit_args &args)
// ----------------------------------------------------------------------------
//   Hook for user-defined opcodes (128..255)
// ----------------------------------------------------------------------------
{
    record(backend, "emit_user op=%u pc=%p", args.op, args.pc);
    record(backend_warning,
           "emit_user not implemented op=%u pc=%p",
           args.op, args.pc);
    args.error = "Not implemented";
}


bool backend::branch(emit_args &args, byte_p target)
// ----------------------------------------------------------------------------
//   Check next thing to translate if we hit an uncnoditional branch
// ----------------------------------------------------------------------------
{
    record(backend, "branch %p to %p", args.pc, target);

    // Check if we already dealt with that code
    auto label = lower_bound(labels, target);
    if (label == labels.end() || label->bytecode != target)
    {
        // Label was not translated yet (e.g. forward branch):
        // Translate that instruction next
        // In that case, we can actually skip the branch
        record(backend, "Not translated yet, jumping to %p", target);
        args.pc = target;
        return true;
    }

    // Pick up the next item to translate
    auto branch = branches.begin();
    if (branch != branches.end())
    {
        record(backend,
               "Picking up branch %p at offset %u",
               branch->bytecode, branch->hwcode);
        args.pc = branch->bytecode;
        return false;
    }

    // No branch left, pick up the next function
    auto fn = functions.begin();
    if (fn != functions.end())
    {
        byte_p fndef = fn->bytecode;
        size_t sz    = leb128<size_t>(fndef);
        byte_p fnend = fndef + sz;
        args.pc      = fndef;
        args.end     = fnend;
        record(backend, "Picking up function %p..%p", args.pc, args.end);
        return false;
    }

    // Nothing left to do - Stop
    record(backend, "Done, %p moving to %p", args.pc, args.end);
    args.pc = args.end;
    return false;
}





// ============================================================================
//
//   Hardware register allocation
//
// ============================================================================

hwreg_t backend::local(local_t loc, hwspec_t spec)
// ----------------------------------------------------------------------------
//  The default register allocator when loading locals
// ----------------------------------------------------------------------------
{
    if (loc >= local_reg.size())
    {
        record(backend_warning,
               "Local %u is out of frame size %u", loc, local_reg.size());
        local_reg.resize(loc + 1);
    }

    // Check if we have a register currently holding this local
    hwreg_t reg = local_reg[loc];
    if (reg != NO_REGISTER)
    {
        assert(reg < reg_local.size() && "Register beyound hardware count");
        local_t rloc = reg_local[reg];
        assert(rloc == loc && "Inconsistent register <-> local mapping");

        // Check if this matches the new spec
        hwspec_t rspec = register_specification(reg);
        if (rspec & spec)
            return reg;
    }

    // Try to find some register satisfying the constraint (unused first)
    hwreg_t nregs = registers();
    for (uint used = 0; used < 2; used++)
    {
        for (hwreg_t r = 0; r < nregs; r++)
        {
            hwspec_t rspec = register_specification(r);
            if (rspec & spec)
            {
                local_t rloc = reg_local[r];

                // Check if we can move from a register that did not match
                if (reg != NO_REGISTER && reg != r)
                {
                    if (move(r, reg))
                    {
                        local_reg[loc] = r;
                        reg_local[reg] = NO_LOCAL;
                        reg_local[r] = loc;
                        return r;
                    }
                }

                if (used || rloc == NO_LOCAL)
                {
                    if (used && rloc != NO_LOCAL)
                    {
                        if (!store(rloc, r))
                            continue;
                        local_reg[rloc] = NO_REGISTER;
                        reg_local[r] = NO_LOCAL;
                    }
                    if (load(r, loc))
                    {
                        local_reg[loc] = r;
                        reg_local[r] = loc;
                        return r;
                    }
                }
            }
        }
    }

    record(backend_error,
           "Unable to find register for loc %u with spec 0x%X", loc, spec);
    return NO_REGISTER;
}


bool backend::saved(local_t loc, hwreg_t reg)
// ----------------------------------------------------------------------------
//   After a store, record where we saved a given register
// ----------------------------------------------------------------------------
{
    reg_local[reg] = loc;
    local_reg[loc] = reg;
    return true;
}


bool backend::spill(hwreg_t r)
// ----------------------------------------------------------------------------
//   Store all live hardware registers back to their slot
// ----------------------------------------------------------------------------
{
    local_t rloc  = reg_local[r];
    bool    found = rloc != NO_LOCAL;
    if (found)
    {
        assert(local_reg[rloc] == r && "Spilling inconsistent reg/local");
        store(rloc, r);
        reg_local[r] = NO_LOCAL;
        local_reg[rloc] = NO_REGISTER;
    }

    return found;
}


size_t backend::spill_all()
// ----------------------------------------------------------------------------
//   Store all live hardware registers back to their slot
// ----------------------------------------------------------------------------
{
    size_t  count = 0;
    hwreg_t nregs = registers();
    for (hwreg_t r = 0; r < nregs; r++)
        if (spill(r))
            count++;

    record(backend, "Spilled %zu registers", count);
    return count;
}


size_t backend::frame_size()
// ----------------------------------------------------------------------------
//    Return current frame size in slots
// ----------------------------------------------------------------------------
{
    return local_reg.size();
}


size_t backend::frame_size(size_t slots)
// ----------------------------------------------------------------------------
//   Allocate the given number of slots
// ----------------------------------------------------------------------------
{
    size_t before = local_reg.size();
    if (size_t spilled = spill_all())
        record(backend_warning, "Frame resizing with %zu live registers",
               spilled);
    local_reg.resize(slots, NO_REGISTER);
    return before;
}



// ============================================================================
//
//   Architecture management
//
// ============================================================================

#include "arm.h"
#include "arm64.h"
#include "x86.h"
#include "amd64.h"
#include "riscv64.h"

backend *backend::make(arch_t arch)
// ----------------------------------------------------------------------------
//   Create a code generator for the requested architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:             return x86::make();
    case ARCH_AMD64:
    case ARCH_AMD64_WIN:        return amd64::make(arch);
    case ARCH_ARM:              return arm::make();
    case ARCH_ARM64:            return arm64::make();
    case ARCH_RISCV64:          return riscv64::make();
    default:
        record(backend_error, "Unknown arch %u, defaulting to AMD64", arch);
    }
    return amd64::make(ARCH_AMD64);
}


cstring backend::arch_name(arch_t arch)
// ----------------------------------------------------------------------------
//   Return a human-readable name for an architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:     return "i386";
    case ARCH_AMD64:    return "amd64";
    case ARCH_AMD64_WIN:return "amd64_windows";
    case ARCH_ARM:      return "arm";
    case ARCH_ARM64:    return "arm64";
    case ARCH_RISCV64:  return "riscv64";
    case ARCH_NONE:     return "none";
    }
    record(backend_error, "arch_name for unknown %u", arch);
    return "unknown";
}
