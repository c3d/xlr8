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

#include "arch.h"
#include "op.h"
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
        OUTPUT_MEMORY   = 1,
        OUTPUT_EXE      = 2,
        OUTPUT_OBJ      = 3,
        OUTPUT_DLL      = 4,
    };

    virtual ~backend() = default;

    bool            set_options(cstring options);
    void            set_output(output_t output);
    arch_t          arch() const            { return current_arch; }
    output_t        output() const          { return current_output; }
    bool            nocode() const        { return nocode_wanted; }
    void            set_nocode(bool on)     { nocode_wanted = on; }

    section        &code()                  { return text_section; }
    size_t          cursor() const          { return ind; }
    void            set_cursor(size_t pos)  { ind = pos; }
    const std::map<uintptr_t, size_t> &bytecode_positions() const
                                            { return bc_positions; }
    int             frame_offset() const    { return loc; }
    void            set_frame_offset(int o) { loc = o; }

    bool            translate(byte_p &pc);
    virtual void    finish_translation();
    virtual void    begin_translation();
    void            bind(tok_t t)           { labels[t] = cursor(); }
    void            resolve_labels();
    void            set_branch_target(byte_p target, bool code_target = true);

    // Target constants (TARGET_DEFS_ONLY section in TinyCC)
    virtual cstring machine_defs() const = 0;
    virtual uint    nb_regs() const = 0;
    virtual uint    ptr_size() const = 0;
    virtual uint    reg_iret() const = 0;

    // Byte emission (g/o in TinyCC)
    virtual void    g(byte c);
    virtual void    o(uint c);
    virtual void    emit32(uint32_t c);

    // Control flow with deferred fixup (gjmp/gsym in TinyCC)
    virtual uint    gjmp(uint t) = 0;
    virtual void    gsym(uint t);
    virtual void    gsym_addr(uint t, uint a) = 0;
    virtual void    relocate(void *base)      { (void) base; }
    virtual size_t  emit_over_callees_branch() { return 0; }

    struct abs_fixup { size_t imm_at; uintptr_t offset; };
    std::vector<abs_fixup>  abs_fixups;

    // ========================================================================
    //
    //   Virtual emitters for every built-in opcode
    //
    // ========================================================================
#define OP(op, decl) virtual decl = 0;
#include "opover.tbl"
#undef OP

    virtual void    emit_user(op_t op, byte_p &pc);

protected:
    backend(arch_t arch);

    void            record_branch(broff_t offset, uint pos);
    void            record_tok_jump(tok_t t, uint pos);
    void            patch_branch(uint pos, broff_t offset);
    virtual void    patch_branch_site(size_t insn_pos, size_t target_native);
    void            translate_sub(byte_p &pc, byte_p end = nullptr);
    void            mark_entry(byte_p target);
    void            scan_sys_branches(tok_t t, byte_p args);
    void            redirect_after_bra(byte_p target, byte_p &pc);
    byte_p          next_live_code(byte_p from);
    void            ensure_branch_target_translated(uintptr_t target);

    arch_t                  current_arch;
    output_t                current_output;
    bool                    nocode_wanted;
    bool                    verbose;
    bool                    translating;
    section                 text_section;
    size_t                  ind;
    int                     loc;
    std::map<tok_t, size_t> labels;
    std::map<tok_t, uint>   tok_jumps;
    std::vector<uint>       branch_patches;
    std::map<uintptr_t, size_t> bc_positions;
    struct branch_fixup { uintptr_t target; size_t insn_pos; };
    std::vector<branch_fixup> branch_fixups;
    std::set<uintptr_t>     branch_targets;
    std::set<uintptr_t>     entry_targets;
    std::map<uint, uint>    gjmp_chain;
    byte_p                  translate_base = nullptr;
    uintptr_t               pending_branch_target = 0;
    size_t                  over_callees_at = 0;
};


backend *backend_new(arch_t arch = ARCH_NATIVE);

RECORDER_DECLARE(backend);
RECORDER_DECLARE(backend_error);
RECORDER_DECLARE(backend_warning);

#endif // BACKEND_H
