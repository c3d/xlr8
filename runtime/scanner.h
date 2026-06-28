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
    using labels  = std::map<text, size_t>;
    using patches = std::vector<size_t>;
    using pending = std::map<text, patches>;

    scanner(istream &input) : input(input), scanned(), program() { }

    enum token { DONE, ERROR, COMMENT, NAME, SYMBOL, INTEGER, REAL, TEXT };

    bool        scan(runtime &rt);
    bool        validate(runtime &rt);

    token       next();
    text        spelling();

    uintptr_t   as_natural(uint *base = nullptr);
    intptr_t    as_integer(uint *base = nullptr);
    size_t      as_real(double &dval, float &fval, uint *base = nullptr);
    text        as_text();

    bool        do_name(runtime &rt);
    bool        do_symbol(runtime &rt);
    bool        do_integer(runtime &rt);
    bool        do_real(runtime &rt);
    bool        do_text(runtime &rt);
    bool        do_use(runtime &rt);
    bool        do_extern(runtime &rt);
    bool        do_define(runtime &rt);
    bool        do_label(runtime &rt);
    bool        do_branch_offset(runtime &rt);

    void        patch_branch(size_t &branch, size_t target);

    void        append(intptr_t i);
    void        append(uintptr_t u);

    pos_t       position()      { return input.tellg(); }
    size_t      pc()            { return program.size(); }

public:
    istream &   input;          // Input stream
    text        scanned;        // Last token scaned
    text        program;        // Bytecode being built
    patches     branches;       // Branch points in the code
    labels      known;          // Labels where we already know the address
    pending     unknown;        // Labels with unknown address
};

RECORDER_DECLARE(scanner);
#endif // SCANNER_H
