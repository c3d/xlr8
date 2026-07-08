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
    using istream     = std::istream;
    using ostream     = std::ostream;
    using pos_t       = std::streampos;
    using symbols     = std::map<text, size_t>;
    using patches     = std::vector<size_t>;
    using pending     = std::map<text, patches>;
    using precedences = std::map<text, int>;
    using texts       = std::vector<text>;
    using text_map    = std::map<text, text>;

    struct macro;
    struct expr;

    scanner(istream &input, runtime &rt)
        : input(input), rt(rt), pgmid(), program(), scanned() { }
    scanner(const scanner &s)
        : input(s.input), rt(s.rt), pgmid(), program(), scanned(),
          macros(s.macros), bytecodes(s.bytecodes),
          infixen(s.infixen), prefixen(s.prefixen),
          postfixen(s.postfixen), blocks(s.blocks),
          closing(s.closing),
          parsing(s.parsing),
          need_operator(false), raw(false) {}

    enum token
    {
        DONE, ERROR, COMMENT, MACRO,
        NAME, SYMBOL,
        INTEGER, NATURAL,
        FLOAT, DOUBLE, REAL,
        TEXT, CHARACTER
    };

    bool        scan(bool macro = false);
    void        append_expressions();
    bool        validate();
    token       error(cstring error);
    int         skip_spaces();
    static bool scan(istream *input, byte itok, byte ival, runtime &rt);

    token       next();
    token       next_raw();

    token       do_comment();
    token       do_name();
    token       do_text();
    token       do_number(uint *base = nullptr);
    token       do_expression();
    bool        do_symbol(char c);
    bool        do_macro_definition(bool bytecode=false);
    bool        do_macro_use(macro m);
    bool        do_bytecode_definition();
    bool        do_bytecode_use(macro m);
    bool        do_expr_name();
    bool        do_expr_symbol();
    bool        expr_binary(char op);
    bool        expr_unary(char op);
    bool        expr_apply(char op);
    int         expr_precedence(char op) const;
    bool        do_extern();
    bool        do_set();
    bool        do_use();
    bool        do_branch_offset();
    bool        do_immediate();
    bool        do_define();
    bool        do_label();

    bool        set(symbols &syms, text name, size_t target);
    void        patch_branch(size_t branch, size_t target);
    void        patch_cstref(size_t cstref, tok_t target);
    void        patch_label_references(size_t pos, size_t added);

    void        append_byte(uvalue_t b);
    void        append(value_t i);
    void        append(uvalue_t u);
    void        append(float f);
    void        append(double d);

    static void append(ostream *out, value_t value);
    static void append(ostream *out, uvalue_t u);
    static void append(ostream *out, float f);
    static void append(ostream *out, double d);

    tok_t       undef(tok_t id);

    bool        label_name();
    bool        label_value(expr &e, uint accepted = ~0U);
    bool        expr_needs_operator() const     { return need_operator; }
    bool        in_expression() const           { return !ops.empty(); }
    pos_t       position()                      { return input.tellg(); }
    size_t      pc()                            { return program.size(); }

    void        infix(text tok, int prec)       { infixen[tok] = prec; }
    void        prefix(text tok, int prec)      { prefixen[tok] = prec; }
    void        postfix(text tok, int prec)     { postfixen[tok] = prec; }
    void        block(text op, text cl, int prec)
    {
        blocks[op] = prec;
        closing[op] = cl;
    }

public:
    istream &   input;          // Input stream
    runtime &   rt;             // Runtime to use
    tok_t       pgmid;          // Program ID
    text        program;        // Bytecode being built
    text        scanned;        // Last token scanned
    symbols     labels;         // Labels where we already know the address
    pending     ulabels;        // Labels with unknown address
    patches     branches;       // Branch points in the code
    symbols     constants;      // Constant values we already know
    pending     uconstants;     // Labels with unknown address
    patches     cstrefs;        // Reference to constants

    struct macro
    // ------------------------------------------------------------------------
    //   Implementation of macros
    // ------------------------------------------------------------------------
    {
        macro(text def = ""): parameters(), definition(def) {}
        texts   parameters;
        text    definition;
    };
    using macro_defs = std::map<text, macro>;
    macro_defs  macros;
    macro_defs  bytecodes;


    struct expr
    // ------------------------------------------------------------------------
    //    Implementation of expressions, e.g. (1+2*3)
    // ------------------------------------------------------------------------
    {
        enum expr_type { BYTE, NAT, INT, FLT, DBL, TXT };

        expr(bool x = 0) : type(BYTE), xnat(x) {}
        expr(byte x)     : type(BYTE), xnat(x) {}
        expr(char x)     : type(BYTE), xnat(x) {}
        expr(uintptr_t x): type(NAT),  xnat(x) {}
        expr(intptr_t  x): type(INT),  xint(x) {}
        expr(float     x): type(FLT),  xflt(x) {}
        expr(double    x): type(DBL),  xdbl(x) {}
        expr(text      x): type(TXT),  xtxt(new text(x)) {}
        expr(expr_type type, uintptr_t x): type(type),  xnat(x) {}
        expr(uintptr_t x, bool): type(x < 256 ? BYTE : NAT),  xnat(x) {}
        expr(const expr &e): type(e.type), xnat(e.xnat)
        {
            if (type == TXT)
                xtxt = new text(*e.xtxt);
        }
        expr(expr &&e): type(e.type), xnat(e.xnat)
        {
            if (type == TXT)
                e.type = NAT;
        }
        ~expr()
        {
            if (type == TXT)
                delete xtxt;
        }
        expr &operator=(expr other)
        {
            type = other.type;
            xnat = other.xnat;
            if (type == TXT)
                xtxt = new text(*xtxt);
            return *this;
        }

        expr_type type;
        union
        {
            uintptr_t xnat;
            intptr_t  xint;
            float     xflt;
            double    xdbl;
            text     *xtxt;
        };
    };
    std::vector<expr> exprs;
    std::vector<char> ops;

    precedences       infixen;
    precedences       prefixen;
    precedences       postfixen;
    precedences       blocks;
    text_map          closing;

    bool              parsing           = false;
    bool              need_operator     = false;
    bool              raw               = false;
};

RECORDER_DECLARE(scanner);
#endif // SCANNER_H
