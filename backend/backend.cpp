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
#include "runtime.h"
#include "save.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>


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

backend::backend(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Initialize shared backend state
// ----------------------------------------------------------------------------
    : rt(rt),
      current_arch(arch),
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

void backend::emit8(byte c)
// ----------------------------------------------------------------------------
//   Emit one byte into the current text section
// ----------------------------------------------------------------------------
{
    record(backend_emit, "emit8 0x%02X", c);
    text_section.append8(c);
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


intptr_t backend::branch(byte_p target, hwcode_t hwcode)
// ----------------------------------------------------------------------------
//  Record that we have a branch, mapping bytecode to machine code
// ----------------------------------------------------------------------------
{
    // If already translated, return the offset to the translation
    auto label  = lower_bound(labels, target);
    if (label != labels.end() && label->bytecode == target)
        return label->hwcode - hwcode;

    // Otherwise, add it to the list of branches to patch
    translation bt = { target, hwcode };
    auto branch = lower_bound(branches, bt);
    if (branch == branches.end() ||
        branch->bytecode != target || branch->hwcode != hwcode)
        branches.insert(branch, bt);
    return 0;
}


intptr_t backend::function(tok_t tok, hwcode_t hwcode)
// ----------------------------------------------------------------------------
//  Record that we have a call to a function offset
// ----------------------------------------------------------------------------
{
    // Check if the function was already compiled, if so return its offset
    byte_p start = byte_p(rt.tok(tok));
    auto   fn    = lower_bound(labels, start);
    if (fn != labels.end() && fn->bytecode == start)
        return fn->hwcode - hwcode;

    // Otherwise, add it to the list of branches to patch
    translation bt = { start, hwcode };
    auto branch = lower_bound(branches, bt);
    if (branch == branches.end() ||
        branch->bytecode != start || branch->hwcode != hwcode)
        branches.insert(branch, bt);

    // And add it to the list of functions to translate
    functions.push_back(tok);
    return 0;
}


uint backend::label(byte_p bytecode, hwcode_t hwcode)
// ----------------------------------------------------------------------------
//   Add an address translation, resolve labels if necessary
// ----------------------------------------------------------------------------
{
    uint count = 0;

    // Pending branches are keyed by bytecode target, not by the patch site
    auto brange = equal_range(branches, bytecode);
    for (auto b = brange.first; b != brange.second; ++b)
    {
        patch(b->hwcode, hwcode, true);
        count++;
    }
    branches.erase(brange.first, brange.second);

    // Absolute jumps / calls waiting on this bytecode address
    auto jrange = equal_range(jumps, bytecode);
    for (auto j = jrange.first; j != jrange.second; ++j)
    {
        patch(j->hwcode, hwcode, false);
        count++;
    }
    jumps.erase(jrange.first, jrange.second);

    // Record the mapping from bytecode to machine code for future branches
    labels.emplace_back(translation{bytecode, hwcode});

    return count;
}


hwcode_t backend::translated_address(byte_p pc)
// ----------------------------------------------------------------------------
//   Translate bytecode address to native address
// ----------------------------------------------------------------------------
{
    auto label = lower_bound(labels, pc);
    if (label != labels.end() && label->bytecode == pc)
        return label->hwcode;
    return hwcode_t(-1);
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

    begin_translation();

    emit_args args(pc, end);
    while (pc < end && !args.error)
    {
        // Insert translation in address map
        byte_p insn = pc;
        label(pc, cursor());

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
#define TOK     args.tok = leb128<tok_t>(pc);        TRC(tok)
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
#define ISTREAM KNAT
#define OSTREAM KNAT

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

        // If we are done here, pick up the next item to translate
        if (pc >= end)
        {
            // Pick up the next label to translate
            auto branch = branches.begin();
            if (branch != branches.end())
            {
                record(backend,
                       "Picking up branch %p at offset %u",
                       branch->bytecode, branch->hwcode);
                pc = branch->bytecode;
                continue;
            }

            // No branch left in this function, pick up the next function
            auto fn = functions.begin();
            if (fn != functions.end())
            {
                tok_t  tok   = *fn;
                size_t sz    = 0;
                byte_p fndef = byte_p(rt.tok(tok, &sz));
                byte_p fnend = fndef + sz;
                args.pc      = fndef;
                args.end     = fnend;
                record(backend,
                       "Picking up function %p..%p", args.pc, args.end);
                functions.erase(fn);
                continue;
            }
        }
    }

    finish_translation();

    if (args.error)
        record(backend_error, "Error during translation: %+s", args.error);
    return !args.error;
}


bool backend::dispatch_sys(emit_args &args)
// ----------------------------------------------------------------------------
//   Parse syscall arguments from sys opcode
// ----------------------------------------------------------------------------
{
    byte_p pc  = args.pc;
    byte_p end = args.end;
    tok_t  sys = args.tok;

    // Parsing macros for native.tbl argument lists (must match interpreter)
#undef DST
#undef SRC
#undef XTRA
#undef PRED
#undef LEN
#undef IVAL
#undef UVAL
#undef TOK
#undef KSHIFT
#undef KINT
#undef KNAT
#undef KLEN
#undef KFLT
#undef KDBL
#undef BRA
#undef ISTREAM
#undef OSTREAM
#define DST     args.dst  = *pc++;
#define SRC     args.src  = *pc++;
#define XTRA    args.xtra = *pc++;
#define PRED    args.pred = *pc++;
#define LEN     args.len  = *pc++;
#define IVAL    args.ival = *pc++;
#define UVAL    args.uval = *pc++;
#define TOK     args.tok  = leb128<tok_t>(pc);
#define KSHIFT  args.kshift = *pc++;
#define KINT    args.kint = leb128<value_t>(pc);
#define KNAT    args.knat = leb128<uvalue_t>(pc);
#define KLEN    args.klen = leb128<size_t>(pc);
#define KFLT    memcpy(&args.kflt, pc, sizeof(args.kflt)); pc += sizeof(args.kflt);
#define KDBL    memcpy(&args.kdbl, pc, sizeof(args.kdbl)); pc += sizeof(args.kdbl);
#define BRA     args.bpc = pc; args.broff = leb128<broff_t>(pc);
#define ISTREAM KNAT
#define OSTREAM KNAT

#define NATIVE(op, decl, code)                  \
    if (sys == runtime::sys_##op)               \
    {                                           \
        decl;                                   \
        args.pc = pc;                           \
        if (pc <= end)                          \
            emit_sys_##op(args);                \
        args.pc = pc;                           \
        return pc <= end;                       \
    }
#include "native.tbl"
#undef NATIVE

    return false;
}


void backend::begin_translation()
// ----------------------------------------------------------------------------
//   Emit prologue at start of translated code
// ----------------------------------------------------------------------------
{
    record(backend, "begin_translation");

    reg_local.clear();
    local_reg.clear();
    labels.clear();
    branches.clear();
    jumps.clear();
    functions.clear();

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


void backend::saved(local_t loc, hwreg_t reg)
// ----------------------------------------------------------------------------
//   After a store, record where we saved a given register
// ----------------------------------------------------------------------------
{
    reg_local[reg] = loc;
    local_reg[loc] = reg;
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

#include "arm64.h"
#include "x86.h"
#include "amd64.h"
// arm.h / riscv64.h omitted until those backends are refactored like arm64

backend *backend::make(runtime &rt, arch_t arch)
// ----------------------------------------------------------------------------
//   Create a code generator for the requested architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:             return x86::make(rt);
    case ARCH_AMD64:
    case ARCH_AMD64_WIN:        return amd64::make(rt, arch);
    case ARCH_ARM:
        record(backend_error, "arm32 backend not yet refactored");
        return nullptr;
    case ARCH_ARM64:            return arm64::make(rt);
    case ARCH_RISCV64:
        record(backend_error, "riscv64 backend not yet refactored");
        return nullptr;
    default:
        record(backend_error, "Unknown arch %u, defaulting to AMD64", arch);
    }
    return amd64::make(rt, ARCH_AMD64);
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
