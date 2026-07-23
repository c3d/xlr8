#ifndef BACKEND_H
#define BACKEND_H
// ****************************************************************************
//  backend.h                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Base class for machine-specific code generation backends.  Each target
//     derives from backend and overrides virtual emit_* hooks for every opcode.
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

#include "recorder.h"
#include "section.h"


#include <map>
#include <set>
#include <string>
#include <sys/types.h>
#include <vector>


struct backend
// ----------------------------------------------------------------------------
//   Code generation backend (TinyCC's TCCState analogue)
// ----------------------------------------------------------------------------
{
    enum output_t
    {
        OUTPUT_NONE,

        OUTPUT_MEMORY,
        OUTPUT_EXE,
        OUTPUT_OBJ,
        OUTPUT_DLL,
    };

    enum arch_t
    {
        ARCH_NONE,

        ARCH_I386,
        ARCH_AMD64,
        ARCH_AMD64_WIN,
        ARCH_ARM,
        ARCH_ARM64,
        ARCH_RISCV64,

        ARCH_NATIVE =
#  if defined(__x86_64__) || defined(__amd64__)
#    if defined(_WIN64)
        ARCH_AMD64_WIN,
#    else
        ARCH_AMD64,
#  endif
#  elif defined(__aarch64__)
        ARCH_ARM64
#  elif defined(__arm__)
        ARCH_ARM
#  elif defined(__riscv) && __riscv_xlen == 64
        ARCH_RISCV64
#  elif defined(__i386__) || defined(__i386)
        ARCH_I386
#  else
#    warning "Native architecture is not supported yet"
        ARCH_NONE
#  endif
    };

    enum hwflag_t : hwspec_t
    // ------------------------------------------------------------------------
    //   Specification flags for register selection
    // ------------------------------------------------------------------------
    {
        RSPEC_TEMP = 0,       // Temporary register, no persistent alloc
        RSPEC_I8   = 1 << 0,  // Can store   signed  8-bit value
        RSPEC_I16  = 1 << 1,  // Can store   signed 16-bit value
        RSPEC_I32  = 1 << 2,  // Can store   signed 32-bit value
        RSPEC_I64  = 1 << 3,  // Can store   signed 64-bit value
        RSPEC_U8   = 1 << 4,  // Can store unsigned  8-bit value
        RSPEC_U16  = 1 << 5,  // Can store unsigned 16-bit value
        RSPEC_U32  = 1 << 6,  // Can store unsigned 32-bit value
        RSPEC_U64  = 1 << 7,  // Can store unsigned 64-bit value
        RSPEC_PTR  = 1 << 8,  // Can store addresses
        RSPEC_FLT  = 1 << 9,  // Can store 32-bit FP
        RSPEC_DBL  = 1 << 10, // Can store 64-bit FP
        RSPEC_BRA  = 1 << 11, // Can store a branch target
        RSPEC_CMP  = 1 << 12, // Can store a comparision / predicate
        RSPEC_CALL = 1 << 13, // Safe across calls

        RSPEC_NEG  = RSPEC_I8  | RSPEC_I16 | RSPEC_I32 | RSPEC_I64,
        RSPEC_POS  = RSPEC_U8  | RSPEC_U16 | RSPEC_U32 | RSPEC_U64,
        RSPEC_INT  = RSPEC_NEG | RSPEC_POS,
        RSPEC_FP   = RSPEC_FLT | RSPEC_DBL,
    };


    virtual         ~backend()   = default;
    static backend  *make(arch_t arch = ARCH_NATIVE);
    static cstring   arch_name(arch_t arch = ARCH_NATIVE);

    bool             set_options(cstring options);
    void             set_output(output_t output);
    arch_t           arch() const               { return current_arch; }
    output_t         output() const             { return current_output; }
    static cstring   opcode_name(op_t op);

    bool             translate(byte_p start, byte_p end);

    section         &code()                     { return text_section; }
    hwcode_t         cursor() const             { return text_section.size(); }

    bool             translate(byte_p &pc);
    void             emit8(byte c);
    void             emit32le(uint32_t c);
    void             emit32be(uint32_t c);

    uint             translate_address(byte_p bytecode, hwcode_t hwcode);
    intptr_t         branch_offset(byte_p bytecode, hwcode_t hwcode);
    intptr_t         branch_offset(hwcode_t hwcode)
    {
        return branch_offset(translating, hwcode);
    }
    intptr_t         branch_offset()
    {
        return branch_offset(translating, cursor());
    }


    // ========================================================================
    //
    //   Backend interface - Functions with default behaviour
    //
    // ========================================================================

    virtual void     begin_translation();
    virtual void     finish_translation();
    virtual hwreg_t  local(local_t loc, hwspec_t spec = RSPEC_INT);
    virtual hwreg_t  saved(local_t loc, hwreg_t reg);

    virtual bool     spill(hwreg_t r);
    virtual size_t   spill_all();
    virtual size_t   frame_size();
    virtual size_t   frame_size(size_t slots);



    // ========================================================================
    //
    //    Target specification (to be overriden by actual targets)
    //
    // ========================================================================

    // Target definition
    virtual cstring  machine_defs() const                       = 0;
    virtual uint     registers() const                          = 0;
    virtual uint     pointer_size() const                       = 0;

    // Control flow with deferred fixup (jump/resolve_jump in TinyCC)
    virtual void     patch_branch(hwcode_t data, hwcode_t tgt)  = 0;
    virtual void     call(void (*target)())                     = 0;
    virtual void     syscall(void (*target)())                  = 0;
    virtual uint     relocate(void *base)                       = 0;


    // ========================================================================
    //
    //   Hardware register allocation
    //
    // ========================================================================

    virtual bool     move(hwreg_t to, hwreg_t from)             = 0;
    virtual bool     load(hwreg_t, local_t r, hwspec_t spec)    = 0;
    virtual bool     store(local_t r, hwreg_t, hwspec_t spec)   = 0;

    struct reginfo { cstring  name; hwspec_t spec; };
    virtual reginfo  register_definition(hwreg_t) const         = 0;
    hwspec_t         register_specification(hwreg_t r) const
    {
        return register_definition(r).spec;
    }
    cstring register_name(hwreg_t r) const
    {
        return register_definition(r).name;
    }

    const hwreg_t    NO_REGISTER        = hwreg_t(-1);
    const local_t    NO_LOCAL           = local_t(-1);
    const uint       DEFAULT_FRAME_SIZE = 8;



    // ========================================================================
    //
    //   Virtual emitters for every built-in opcode
    //
    // ========================================================================

    struct emit_args
    // ------------------------------------------------------------------------
    //   Arguments set by opcodes table
    // ------------------------------------------------------------------------
    {
        emit_args(byte_p pc, byte_p end): error(), ppc(pc), pc(pc), end(end) {}

        cstring  error;  // Error message if any
        byte_p   ppc;    // Previous program counter (beginning of op)
        byte_p   pc;     // Program counter (end of op)
        byte_p   end;    // End of translation unit
        tok_t    op;     // Opcode (first byte or sys name)
        bool     done;   // Indicates that we are done
        byte     dst;    // DST  Destination register index
        byte     src;    // SRC  Source register index
        byte     xtra;   // XTRA Extra register index
        byte     pred;   // PRED Predicate register
        byte     len;    // LEN  Length register (for native ops)
        byte     ival;   // IVAL Integer value register (for native ops)
        byte     uval;   // UVAL Unsigned value register (for native ops)
        tok_t    tok;    // TOK  Token value (e.g. indirect calls)
        byte_p   bpc;    // BRA  PC for branch offset
        broff_t  broff;  // BRA  Branch offset
        value_t  kint;   // KINT Signed integer value
        uvalue_t knat;   // KNAT Unsigned integer value
        size_t   klen;   // KLEN Unsigned size value
        byte     kshift; // KSHIFT Bit shift (0-64)
        float    kflt;   // KFLT Float value
        double   kdbl;   // KDBL Double value
    };

#define OP(op, code, decl) virtual void emit_##op(emit_args &args) = 0;
#include "opcodes.tbl"

#define NATIVE(op, code, decl) virtual void emit_sys_##op(emit_args &args) = 0;
#include "native.tbl"

    virtual void emit_user(emit_args &args);
    virtual bool branch(emit_args &args, byte_p target);

protected:
    backend(arch_t arch);       // Only create backends through the factory


    arch_t   current_arch;
    output_t current_output;
    byte_p   translating;
    section  text_section;

    using reg_local_t = std::vector<local_t>;
    using local_reg_t = std::vector<hwreg_t>;
    reg_local_t reg_local;
    local_reg_t local_reg;

    struct translation { byte_p   bytecode; hwcode_t hwcode; };
    using translations = std::vector<translation>;
    translations functions;
    translations branches;
    translations calls;
    translations labels;

    friend struct translation_compare;
    template<typename T> static auto lower_bound(const translations &, T);
    template<typename T> static auto equal_range(const translations &, T);
};

RECORDER_DECLARE(backend);
RECORDER_DECLARE(backend_error);
RECORDER_DECLARE(backend_warning);

#endif // BACKEND_H
