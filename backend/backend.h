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
        ARCH_X86_64,
        ARCH_ARM,
        ARCH_ARM64,
        ARCH_RISCV64,

        ARCH_NATIVE =
#  if defined(__x86_64__) || defined(__amd64__)
        ARCH_X86_64
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

    virtual        ~backend()   = default;
    static backend *make(arch_t arch = ARCH_NATIVE);
    static cstring  arch_name(arch_t arch = ARCH_NATIVE);

    bool            set_options(cstring options);
    void            set_output(output_t output);
    arch_t          arch() const                { return current_arch; }
    output_t        output() const              { return current_output; }
    bool            nocode() const              { return nocode_wanted; }
    void            set_nocode(bool on)         { nocode_wanted = on; }

    section        &code()                      { return text_section; }
    size_t          cursor() const              { return text_section.size(); }
    auto            bytecode_positions() const  { return bc_positions; }
    void            bind(tok_t t)               { labels[t] = cursor(); }
    void            resolve_labels();

    virtual void    begin_translation()  = 0;
    virtual void    finish_translation() = 0;
    bool            translate(byte_p &pc);

    // Target constants (TARGET_DEFS_ONLY section in TinyCC)
    virtual cstring machine_defs() const = 0;
    virtual uint    nb_regs() const      = 0;
    virtual uint    ptr_size() const     = 0;
    virtual uint    reg_iret() const     = 0;

    // Byte emission (g in TinyCC, o seems really strange and was removed)
    void            g(byte c);
    void            emit32le(uint32_t c);

    // Control flow with deferred fixup (gjmp/gsym in TinyCC)
    virtual uint    gjmp(uint t) = 0;
    virtual void    gsym(uint t);
    virtual void    gsym_addr(uint t, uint a) = 0;
    virtual void    gsyscall(void (*target)()) = 0;
    virtual void    relocate(void *base)        { (void) base; }
    virtual size_t  emit_over_callees_branch()  { return 0; }


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
        emit_args(byte_p pc): error(), ppc(pc), pc(pc) {}

        cstring  error;  // Error message if any
        byte_p   ppc;    // Previous program counter (beginning of op)
        byte_p   pc;     // Program counter (end of op)
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

    virtual void    emit_user(emit_args &args);

protected:
    backend(arch_t arch);

    void            record_branch(broff_t offset, uint pos);
    void            record_tok_jump(tok_t t, uint pos);
    void            patch_branch(uint pos, broff_t offset);
    virtual void    patch_branch_site(size_t insn_pos, size_t target_native);
    void            translate_subroutine(byte_p &pc, byte_p end = nullptr);
    void            mark_entry(byte_p target);
    void            redirect_after_bra(byte_p target, byte_p &pc);
    byte_p          next_live_code(byte_p from);
    void            ensure_branch_target_translated(byte_p target);

    struct relocation   { size_t imm_at; size_t offset; };
    struct branch_fixup { byte_p target; size_t insn_pos; };

    using relocations_t = std::vector<relocation>;
    using labels_t      = std::map<tok_t, size_t>;
    using tok_map_t     = std::map<tok_t, uint>;
    using branches_t    = std::vector<uint>;
    using positions_t   = std::map<byte_p, size_t>;
    using fixups_t      = std::vector<branch_fixup>;
    using targets_t     = std::set<byte_p>;
    using jmpchain_t    = std::map<size_t, size_t>;

    arch_t        current_arch;
    output_t      current_output;
    bool          nocode_wanted;
    bool          verbose;
    bool          translating;
    section       text_section;
    size_t        index;
    relocations_t relocations;
    labels_t      labels;
    tok_map_t     tok_jumps;
    branches_t    branch_patches;
    positions_t   bc_positions;
    fixups_t      branch_fixups;
    targets_t     branch_targets;
    targets_t     entry_targets;
    jmpchain_t    gjmp_chain;
    byte_p        translate_base        = nullptr;
    byte_p        pending_branch_target = nullptr;
    size_t        over_callees_at       = 0;
};

RECORDER_DECLARE(backend);
RECORDER_DECLARE(backend_error);
RECORDER_DECLARE(backend_warning);

#endif // BACKEND_H
