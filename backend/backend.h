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
#include "section.h"

#include <map>
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
    int             frame_offset() const    { return loc; }
    void            set_frame_offset(int o) { loc = o; }

    bool            translate(byte_p &pc);
    void            bind(tok_t t)           { labels[t] = cursor(); }
    void            resolve_labels();

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
};


backend *backend_new(arch_t arch = ARCH_NATIVE);

#endif // BACKEND_H
