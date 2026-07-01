#ifndef SCANNER_H
#define SCANNER_H
// ****************************************************************************
//  scanner.h                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     XLR8 scanner
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
#include "types.h"

#include <iostream>
#include <map>
#include <vector>

struct runtime;

struct scanner
// ----------------------------------------------------------------------------
//   Scanner for some input stream
// ----------------------------------------------------------------------------
{
    using istream = std::istream;
    using pos_t   = std::streampos;
    using symbols = std::map<text, size_t>;
    using patches = std::vector<size_t>;
    using pending = std::map<text, patches>;

    scanner(istream &input, runtime &rt)
        : input(input), rt(rt),scanned(), program(), setting() { }

    enum token { DONE, ERROR, COMMENT, NAME, SYMBOL, INTEGER, REAL, TEXT };

    bool        scan();
    bool        validate();

    token       next();

    uintptr_t   as_natural(uint *base = nullptr);
    intptr_t    as_integer(uint *base = nullptr);
    size_t      as_real(double &dval, float &fval, uint *base = nullptr);
    text        as_text();

    bool        do_integer();
    bool        do_real();
    bool        do_text();
    bool        do_name();
    bool        do_symbol();
    bool        do_extern();
    bool        do_set();
    bool        do_use();
    bool        do_branch_offset();
    bool        do_bytes();
    bool        do_define();
    bool        do_label();

    bool        set(symbols &syms, text name, size_t target);
    void        patch_branch(size_t branch, size_t target);
    void        patch_cstref(size_t cstref, tok_t target);
    void        patch_label_references(size_t pos, size_t added);

    bool        set(uintptr_t u);
    void        append(intptr_t i);
    void        append(uintptr_t u);
    tok_t       undef(tok_t id);

    pos_t       position()      { return input.tellg(); }
    size_t      pc()            { return program.size(); }

public:
    istream &   input;          // Input stream
    runtime &   rt;             // Runtime to use
    text        scanned;        // Last token scanned
    text        program;        // Bytecode being built
    symbols     labels;         // Labels where we already know the address
    pending     ulabels;        // Labels with unknown address
    patches     branches;       // Branch points in the code
    symbols     constants;      // Constant values we already know
    pending     uconstants;     // Labels with unknown address
    patches     cstrefs;        // Reference to constants
    tok_t       setting;        // Value being set
};

RECORDER_DECLARE(scanner);
#endif // SCANNER_H
