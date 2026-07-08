// ****************************************************************************
//  scanner.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Scanner for the XLR8 runtime
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

#include "scanner.h"

#include "leb128.h"
#include "recorder.h"
#include "runtime.h"
#include "save.h"

#include <cctype>
#include <cmath>
#include <functional>
#include <locale>
#include <memory>
#include <sstream>


const size_t LEB128_MAXSIZE = 16;

RECORDER(scanner,       16, "Scanner");
RECORDER(scanner_error, 16, "Errors during scanner");
RECORDER(macros,        16, "Macros");


bool scanner::scan(bool macro)
// ----------------------------------------------------------------------------
//   Scan input tokens from the stream and build program object
// ----------------------------------------------------------------------------
{
    while (!rt.error())
    {
        token tok = next();
        record(scanner, "Next token is %u: %s", tok, scanned.c_str());
        if (tok == DONE || tok == ERROR || tok == COMMENT)
            break;

        // If we are not in an expression,
        if (!in_expression())
            append_expressions();
    }

    if (rt.error())
        return false;
    return macro || validate();
}


void scanner::append_expressions()
// ----------------------------------------------------------------------------
//   Append all pending expressions
// ----------------------------------------------------------------------------
{
    for (auto &e : exprs)
    {
        record(scanner, "Append %u %16lld %016llx",
               e.type, e.xint, e.xnat);
        switch (e.type)
        {
        case expr::BYTE:        append_byte(e.xnat); break;
        case expr::NAT:         append(e.xnat); break;
        case expr::INT:         append(e.xint); break;
        case expr::FLT:         append(e.xflt); break;
        case expr::DBL:         append(e.xdbl); break;
        case expr::TXT:         append(rt.tok(*e.xtxt)); break;
        }
    }
    exprs.clear();
}


bool scanner::validate()
// ----------------------------------------------------------------------------
//   Check if what we scanned is OK
// ----------------------------------------------------------------------------
{
    if (ulabels.size())
    {
        record(scanner_error, "%zu remaining undefined labels", ulabels.size());
        for (auto &it : ulabels)
            record(scanner_error, "  %s", it.first.c_str());
        rt.error("Undefined label");
        return false;
    }
    if (uconstants.size())
    {
        record(scanner_error, "%zu remaining undefined symbols",
               uconstants.size());
        for (auto &it : uconstants)
            record(scanner_error, "  %s", it.first.c_str());
        rt.error("Undefined symbols");
        return false;
    }

    program += text(1, char(runtime::op_end));
    pgmid = rt.tok(program);
    rt.entry = byte_p(rt.tok(pgmid));
    rt.pc = rt.entry;
    record(scanner, "Scanned %lu bytes entry %p", program.size(), rt.entry);
    if (RECORDER_TRACE(scanner) > 1)
        for (size_t i = 0; i < program.size(); i++)
            record(scanner, "  [%zu] = %02x", i, (byte) program[i]);

    // Cleanup
    program = "";
    labels.clear();
    ulabels.clear();
    branches.clear();
    constants.clear();
    uconstants.clear();
    cstrefs.clear();
    exprs.clear();
    ops.clear();
    need_operator = false;
    raw = false;

    return true;
}


 scanner::token scanner::error(cstring msg)
// ----------------------------------------------------------------------------
//   Report a scanner error
// ----------------------------------------------------------------------------
{
    record(scanner_error, "%s at byte %zu near %c: %s",
           msg, input.tellg(), input.peek(), strerror(errno));
    rt.error(msg);
    return ERROR;
}


int scanner::skip_spaces()
// ----------------------------------------------------------------------------
//   Skip spaces, return peek at first non-space character
// ----------------------------------------------------------------------------
{
    int c = input.peek();
    if (std::isspace(c))
    {
        do { c = input.get(); } while (input.good() && std::isspace(c));
        if (input.good())
            input.putback(c);
    }
    return c;
}


scanner::token scanner::next()
// ----------------------------------------------------------------------------
//  Parse one token from input
// ----------------------------------------------------------------------------
{
    while (true)
    {
        token n = next_raw();
        if (n != COMMENT)
            return n;
    }
}


scanner::token scanner::next_raw()
// ----------------------------------------------------------------------------
//  Parse one token from input
// ----------------------------------------------------------------------------
{
    if (!input.good())
    {
        if (input.eof())
            return DONE;
        record(scanner_error, "I/O error on scanner input: %s",
               strerror(errno));
        return ERROR;
    }

    scanned = "";
    int c = skip_spaces();
    if (c < 0)
        return input.eof() ? DONE : ERROR;

    // Skip comments (C / C++ style syntax)
    if (c == '/')
        return do_comment();

    // Scan names
    if (std::isalpha(c) || c == '_')
        return do_name();

    // Scan numbers
    if (std::isdigit(c) || ((c == '+' || c == '-') && !expr_needs_operator()))
        return do_number();

    // Scan text
    if (c == '\'' || c == '"')
        return do_text();

    // Anything else is a symbol
    c = input.get();
    scanned += char(c);
    if (c == ';' && !parsing)
        return DONE;
    return do_symbol(c) ? SYMBOL : ERROR;
}


scanner::token scanner::do_comment()
// ----------------------------------------------------------------------------
//   Check if we have a comment
// ----------------------------------------------------------------------------
{
    int c = input.get();
    if (c != '/')
        return error("Expecting `/` to start comment");

    int nc = input.peek();
    if (nc == '/')
    {
        // C++ style // comment
        while (input.good() && c != '\n')
        {
            scanned += char(c);
            c = input.get();
        }
        return COMMENT;
    }
    else if (nc == '*')
    {
        // C style /* */ comment
        int last = 0;
        while (input.good() && (c != '/' || last != '*'))
        {
            scanned += char(c);
            last = c;
            c = input.get();
        }
        if (c == '/')
            scanned += char(c);
        return COMMENT;
    }

    // Otherwise `/` is a symbol
    return do_symbol(c) ? SYMBOL : ERROR;
}


scanner::token scanner::do_name()
// ----------------------------------------------------------------------------
//   Scan a name
// ----------------------------------------------------------------------------
{
    int c = input.get();
    if (!isalpha(c) && c != '_')
        return error("Expecting alphabetic of '_' to start name");

    while (input.good() && (std::isalnum(c) || c == '_'))
    {
        scanned += char(c);
        c = input.get();
    }
    if (input.good())
        input.putback(c);

    if (!raw)
    {
        // Check if we have a macro or bytecode
        if (auto found = macros.find(scanned); found != macros.end())
            return do_macro_use((*found).second) ? MACRO : ERROR;
        if (auto found = bytecodes.find(scanned); found != bytecodes.end())
            return do_bytecode_use((*found).second) ? MACRO : ERROR;

        // Check if we have a constant with that value
        if (constants.count(scanned))
            exprs.emplace_back(constants[scanned]);
        else
            exprs.emplace_back(rt.tok(scanned));
        need_operator = true;
    }
    return NAME;
}


scanner::token scanner::do_text()
// ----------------------------------------------------------------------------
//   Scan a text value
// ----------------------------------------------------------------------------
{
    int c = input.get();
    if (c != '\'' && c != '"')
        return error("Expecting quotes to start text constant");
    char delim = c;
    bool done  = false;
    text value;
    while (input.good() && !done)
    {
        scanned += char(c);
        c = input.get();
        if (c == delim)
        {
            scanned += char(c);
            c = input.get();
            if (c != delim)
                done = true;
            else
                value += c;
        }
        else
        {
            value += c;
        }
    }
    if (input.good())
        input.putback(c);
    if (!raw)
    {
        exprs.emplace_back(value);
        need_operator = true;
    }
    return delim == '"' ? TEXT : CHARACTER;
}


static inline uint digit_value(int c)
// ----------------------------------------------------------------------------
//   Return the digit value in the given base
// ----------------------------------------------------------------------------
{
    uint digit = c - '0';
    if (c >= 'a')
        digit = c - 'a' + 10;
    else if (c >= 'A')
        digit = c - 'A' + 10;
    return digit;
}


scanner::token scanner::do_number(uint *basep)
// ----------------------------------------------------------------------------
//   Scan the next number using the XL syntax
// ----------------------------------------------------------------------------
//   This scans a number, which can be decimal or not, with a base or not
{
    uint      base    = 10;
    uint      mbase   = 10;
    uvalue_t  value   = 0;
    double    dvalue  = 0.0;
    double    dmult   = 1.0;
    float     fvalue  = 0.0;
    float     fmult   = 1.0;
    bool      isfirst = true;
    bool      hasneg  = false;
    bool      hasnot  = false;
    bool      hasmneg = false;
    bool      hasdot  = false;
    bool      hasbase = false;
    bool      hasexp  = false;
    bool      hashash = false;
    bool      hasnum  = false;
    token     kind    = scanner::NATURAL;

    scanned = "";
    while (input.good())
    {
        int c = input.get();
        if (c == EOF)
            break;
        scanned += c;

        if (isfirst)
        {
            if ((c == '+' || c == '-') && kind == scanner::NATURAL)
                kind = scanner::INTEGER;
            if (c == '-' && !hasnot)
                hasneg = !hasneg;
            else if (c == '~' && !hasneg)
                hasnot = !hasnot;
            else if (c != '+')
                isfirst = false;
            if (isfirst)
                continue;
        }

        // Skip '_' in numbers
        if (c == '_')
            continue;

        // Based numbers, e.g. 16#FFFF_FFFF
        if (c == '#')
        {
            if (!hasbase)
            {
                if (value < 2 || value >= 36)
                {
                    record(scanner_error, "Invalid base %llu", value);
                    return ERROR;
                }
                base    = value;
                value   = 0;
                dvalue  = 0.0;
                fvalue  = 0.0;
                dmult   = 1.0;
                fmult   = 1.0;
                hasbase = true;
                continue;
            }

            // Case of something like 16#FF.FFFE#E3
            value   = 0;
            mbase   = base;
            base    = 10;
            hasdot  = false;
            hashash = true;
            isfirst = true;
            continue;
        }

        // Check decimal point
        if (c == '.')
        {
            int p = input.peek();
            if (p == '#' || digit_value(p) < base)
            {
                hasdot = true;
                kind = scanner::REAL;
                continue;
            }
            // Something like 1..2 or 16#1.X - The `.` is an operator
            input.putback(c);
            scanned.pop_back();
            break;
        }

        // Regular digits
        uint digit = digit_value(c);
        if (digit >= base)
        {
            char uc = toupper(c);
            if (uc == 'E')
            {
                if (!hasexp)
                {
                    hasexp  = true;
                    hasmneg = hasneg;
                    value   = 0;
                    base    = 10;
                    hasneg  = false;
                    isfirst = true;
                    kind    = scanner::REAL;
                    continue;
                }
            }
            if (kind == scanner::REAL)
            {
                if (uc == 'F')
                {
                    kind = scanner::FLOAT;
                    break;
                }
                else if (uc == 'D')
                {
                    kind = scanner::DOUBLE;
                    break;
                }
            }
            // we are done here
            input.putback(c);
            scanned.pop_back();
            break;
        }
        hasnum = true;
        value = value * base + digit;
        if (hasexp || hashash)
            continue;
        if (hasdot)
        {
            dmult /= base;
            dvalue += dmult * digit;
            fmult /= base;
            fvalue += fmult * digit;
        }
        else
        {
            dvalue = dvalue * base + digit;
            fvalue = fvalue * base + digit;
        }
    }

    // If just had '+' or '-', return symbol
    if (!hasnum)
    {
        if (scanned.size() != 1)
        {
            error("Invalid character in number");
            return ERROR;
        }
        return do_symbol(scanned[0]) ? SYMBOL : ERROR;
    }

    // Check if we had an exponent)
    if (hasexp)
    {
        float  fexp = 1.0;
        double dexp = 1.0;
        fmult = mbase;
        dmult = mbase;
        while (value)
        {
            if (value & 1)
            {
                fexp *= fmult;
                dexp *= dmult;
            }
            fmult *= fmult;
            dmult *= dmult;
            value >>= 1;
        }

        if (hasneg)
        {
            dvalue /= dexp;
            fvalue /= dexp;
        }
        else
        {
            dvalue *= dexp;
            fvalue *= fexp;
        }
        hasneg = hasmneg;
        base = mbase;
    }

    if (hasneg)
    {
        dvalue = -dvalue;
        fvalue = -fvalue;
        value = -value;
    }
    if (hasnot)
    {
        value = ~value;
        if (kind != scanner::NATURAL)
        {
            record(scanner_error, "Bitwise not for type %u", kind);
            return ERROR;
        }
    }

    if (basep)
        *basep = base;
    record(scanner, "Number [%s] kind %u base %u: %llu %g %g",
           scanned.c_str(), kind, base, value, dvalue, fvalue);
    if (raw)
        return kind;

    switch (kind)
    {
    case NATURAL:       exprs.emplace_back(expr::BYTE, value); break;
    case INTEGER:       exprs.emplace_back(value_t(value)); break;
    case FLOAT:         exprs.emplace_back(fvalue); break;
    case REAL:
    case DOUBLE:        exprs.emplace_back(dvalue); break;
    default:            record(scanner_error, "Unknown kind %u", kind); break;
    }
    need_operator = true;
    return kind;
}


scanner::token scanner::do_expression()
// ----------------------------------------------------------------------------
//   Parse an expression
// ----------------------------------------------------------------------------
{
    size_t nops = ops.size();
    ops.push_back('(');
    need_operator = false;

    while (ops.size() > nops)
    {
        token t = next();
        if (t == DONE || t == ERROR)
            return t;

        if (t == SYMBOL && scanned[0] == ')')
        {
            while (ops.size() > nops && ops.back() != '(')
            {
                if (!expr_apply(ops.back()))
                    return ERROR;
                ops.pop_back();
            }
            if (ops.back() == '(')
            {
                ops.pop_back();
                need_operator = true;
            }
        }
    }
    if (!exprs.size())
        return ERROR;
    expr &e = exprs.back();
    switch (e.type)
    {
    case expr::BYTE:
    case expr::NAT:     scanned = std::to_string(e.xnat); return NATURAL;
    case expr::INT:     scanned = std::to_string(e.xint); return INTEGER;
    case expr::FLT:     scanned = std::to_string(e.xflt); return FLOAT;
    case expr::DBL:     scanned = std::to_string(e.xdbl); return DOUBLE;
    case expr::TXT:     scanned = *e.xtxt;                return TEXT;
    }
    record(scanner_error, "Invalid expression type %u", e.type);
    return ERROR;
}


bool scanner::do_symbol(char c)
// ----------------------------------------------------------------------------
//   Process a name by adding the token to the token table
// ----------------------------------------------------------------------------
{
    if (raw)
        return true;

    switch (c)
    {
    case '?':
        return do_extern();
    case '!':
        return do_set();
    case '.':
        return do_label();
    case ':':
        return do_branch_offset();
    case '*':
        if (in_expression())
            return expr_binary('*');
        return do_use();
    case '#':
        return do_immediate();
    case '[':
        return do_macro_definition();
    case ']':
        return true;
    case '(':
        return do_expression() >= NAME;
    case ')':
        return true;
    case '{':
        return do_bytecode_definition();
    case '}':
        return true;
    case '+':
    case '-':
        if (!in_expression())
            return expr_unary(c);
    case '=':
        if (!in_expression())
            return do_define();
        // fall through
    case '<':
    case '>':
    case '/':
    case '%':
    case '&':
    case '|':
    case '^':
    case '~':
        if (in_expression())
            return expr_needs_operator() ? expr_binary(c) : expr_unary(c);
        // fall through
    default:
        break;
    }
    return true;
}


bool scanner::do_macro_definition(bool bytecode)
// ----------------------------------------------------------------------------
//   Enter a macro definition in our table
// ----------------------------------------------------------------------------
//   A macro definition has the form            [MYMACRO A B C].
//   Arguments can be placed between brackets:  [cmpge[r a b] cmple r b a]
//   Local macros inside macros are allowed:    [stuff[r] [x (r+1)] r x x r]
//
//   Arguments behave as local macros during expansion
{
    save  raw_symbols(raw, true);
    token t = next();
    if (t != NAME)
    {
        record(scanner_error, "Invalid %+s name %s",
               bytecode ? "bytecode" : "macro", scanned.c_str());
        rt.error(bytecode ? "Invalid bytecode name" : "Invalid macro name");
        return false;
    }

    bool   first = true;
    bool   done  = false;
    bool   parms = false;
    text   name  = scanned;
    char   open  = '[';
    char   close = ']';
    char   scan;
    macro  here;
    text   parens;

    record(macros, "Define[%s]=%u bytes %s",
           scanned.c_str(), here.definition.size(),
           bytecode ? "<bytecode>" : here.definition.c_str());
    while (!done)
    {
        if (bytecode && !first && !parms)
            raw = raw_symbols.saved;

        t = next();
        switch (t)
        {
        case DONE:
            done = true;
            break;
        case ERROR:
            return false;
        case NAME:
            if (parms)
            {
                here.parameters.push_back(scanned);
                if (bytecode)
                {
                    size_t index = 2 * here.parameters.size() - 1;
                    text   regdef = std::to_string(index);
                    macro  parm = regdef;
                    macros[scanned] = parm;
                }
                continue;
            }
            break;
        case SYMBOL:
            scan  = scanned.size() ? scanned[0] : 0;
            open  = scan == '[' || scan == '(' || scan == '{';
            close = scan == '}' || scan == ')' || scan == ']';
            if (first && open)
            {
                parms = true;
                first = false;
            }

            if (open)
            {
                open = scan;
                close = open == '[' ? ']'
                      : open == '(' ? ')'
                      : open == '{' ? '}'
                                    : 0;
                parens += close;
                if (parms)
                    continue;
            }
            else if (close)
            {
                done = parens.empty();
                if (!done)
                {
                    if (scan != parens.back())
                    {
                        error("Mismatched parentheses");
                        return false;
                    }
                    parens.pop_back();
                    if (parens.empty())
                    {
                        if (parms)
                        {
                            scanned = "";
                            parms   = false;
                        }
                        if (first)
                            continue;
                    }
                }
            }
            break;

        default:
            break;
        }

        if (!done)
        {
            first = false;
            if (parms && t != NAME)
            {
                record(scanner_error,
                       "Invalid %+s parameter %s",
                       bytecode ? "bytecode" : "macro", scanned.c_str());
                rt.error(bytecode ? "Invalid bytecode parameter"
                                  : "Invalid macro parameter");
                return false;
            }
            if (bytecode)
            {
                append_expressions();
            }
            else
            {
                if (here.definition.length())
                    here.definition += ' ';
                here.definition += scanned;
            }
        }
    }

    if (bytecode)
    {
        if (!validate())
            return false;
        here.definition = rt.as_text(pgmid);
    }

    record(macros,
           "Defined[%s]=%u bytes [%s]",
           name.c_str(),
           here.definition.size(),
           bytecode ? "<bytecode>" : here.definition.c_str());
    macro_defs &defs = bytecode ? bytecodes : macros;
    defs[name] = here;

    return true;
}


bool scanner::do_macro_use(macro m)
// ----------------------------------------------------------------------------
//    Expand macro
// ----------------------------------------------------------------------------
{
    record(macros,
           "Macro[%s(%zu args)]=%s",
           scanned.c_str(), m.parameters.size(), m.definition);
    save saved(macros, macros);

    // Clear macro definition for stuff like: [fsin[d s] sys fsin d s]
    macros.erase(scanned);

    // Read parameters in the macros
    for (text parm : m.parameters)
    {
        // For each parameter, create a new macro (or overwite existing)
        save rawparms(raw, true);
        macro p = macros[parm];
        token n = next();
        if (n == ERROR || n == DONE)
        {
            rt.error("Invalid macro argument");
            record(scanner_error, "Invalid macro argument %s: %s",
                   parm.c_str(), scanned.c_str());
            return false;
        }

        p.definition = scanned;
        record(macros, "Parm[%s]=%s", parm.c_str(), scanned.c_str());

        // Append scanned token to definition
        // If the argument has the form [A B C] return A B C
        if (n == SYMBOL && scanned == "[")
        {
            uint depth = 1;
            p.definition = "";
            while (depth)
            {
                bool skip = false;
                n = next();
                if (n == SYMBOL && scanned == "]")
                    if (--depth == 0)
                        skip = true;
                if (!skip)
                {
                    if (p.definition.length())
                        p.definition += ' ';
                    p.definition += scanned;
                }
            }
        }
        macros[parm] = p;
    }

    // Switch to the definition and parse that
    text               mval = m.definition;
    std::istringstream definition(mval);
    std::streambuf    *sbuf = input.rdbuf(definition.rdbuf());
    bool               ok   = scan(true);
    input.rdbuf(sbuf);
    return ok;
}


bool scanner::do_bytecode_definition()
// ----------------------------------------------------------------------------
//   Enter a bytecode definition
// ----------------------------------------------------------------------------
//   A bytecode definition is very similar to a macro definition, but the
//   bytecode is already compiled. Furthermore, it is evaluated on use
{
    save args(macros, macros);
    save pgm(program, text());
    save lbls(labels, symbols());
    save ulbls(ulabels, pending());
    save brs(branches, patches());
    save csts(constants, symbols());
    save ucsts(uconstants, pending());
    save crefs(cstrefs, patches());
    return do_macro_definition(true);
}


bool scanner::do_bytecode_use(macro m)
// ----------------------------------------------------------------------------
//   Evaluate a bytecode
// ----------------------------------------------------------------------------
{
    record(macros,
           "Bytecode[%s(%zu args)]=%s",
           scanned.c_str(), m.parameters.size(), m.definition);

    // Read parameters in the macros
    size_t arg = 0;
    rt.enter(2 * m.parameters.size());
    for (text parm : m.parameters)
    {
        // For each parameter, create a new macro (or overwite existing)
        ops.push_back(' ');
        token n = next();
        ops.pop_back();
        if (n == ERROR || n == DONE || exprs.size() == 0)
        {
            rt.error("Invalid bytecode argument");
            record(scanner_error, "Invalid bytecode argument %s: %s",
                   parm.c_str(), scanned.c_str());
            return false;
        }

        expr e = exprs.back();
        exprs.pop_back();
        rt.frame[arg++] = e.type;
        rt.frame[arg++] = e.type == expr::TXT ? rt.tok(*e.xtxt) : e.xnat;
    }

    save pc(rt.pc, byte_p(m.definition.data()));
    std::ostringstream out;
    ostream *outp = rt.iosetup(0, &out);
    bool ok = rt.run();
    rt.iosetup(0, outp);
    rt.leave();
    if (ok)
        program += out.str();
    return ok;
}


int scanner::expr_precedence(char op) const
// ----------------------------------------------------------------------------
//   Operator precedence (higher binds tighter)
// ----------------------------------------------------------------------------
{
    switch (op)
    {
    case '*':
    case '/':
    case '%':   return 60;
    case '+':
    case '-':   return 50;
    case '&':   return 40;
    case '^':   return 30;
    case '|':   return 20;
    case '=':
    case '<':
    case '>':   return 10;
    default:    return -1;
    }
}


bool scanner::expr_binary(char op)
// ----------------------------------------------------------------------------
//   Process a binary operator in an expression
// ----------------------------------------------------------------------------
{
    int prec = expr_precedence(op);
    if (prec < 0)
    {
        error("Invalid operator in expression");
        return false;
    }
    while (!ops.empty() && ops.back() != '(' &&
           expr_precedence(ops.back()) >= prec)
    {
        if (!expr_apply(ops.back()))
            return false;
        ops.pop_back();
    }
    ops.push_back(op);
    return true;
}


static inline text operator *(uvalue_t n, text t)
// ----------------------------------------------------------------------------
//   Repeat a text pattern
// ----------------------------------------------------------------------------
{
    text result = "";
    text added  = t;
    while (n)
    {
        if (n & 1)
            result += added;
        added += added;
        n >>= 1;
    }
    return result;
}


template <typename T>
static inline text format(text fmt, T value)
// ----------------------------------------------------------------------------
//   Format a single argument in a text
// ----------------------------------------------------------------------------
{
    int sz = std::snprintf(nullptr, 0, fmt.c_str(), value);
    if (sz <= 0)
        return fmt;
    std::unique_ptr<char[]> buf(new char[size_t(sz+1)]);
    char *buffer = buf.get();
    std::snprintf(buffer, sz+1, fmt.c_str(), value);
    return text(buffer, size_t(sz));
}


static inline text format(text fmt, const scanner::expr &e)
// ----------------------------------------------------------------------------
//   Apply printf-style formats to the expression
// ----------------------------------------------------------------------------
{
    auto pos    = fmt.begin();
    bool infmt  = false;
    bool done   = false;
    text result = "";
    text style  = "";
    while (pos != fmt.end() && !done)
    {
        char c = *pos++;
        if (infmt)
        {
            style += c;
            switch (c)
            {
            case '%':
                result += c;
                infmt = false;
                break;

            case 'u':
            case 'x':
            case 'p':
            case 'X':
            case 'o':
            case 'b':
                if (e.type == scanner::expr::NAT ||
                    e.type == scanner::expr::BYTE)
                    result += format(style, e.xnat);
                done = true;
                break;

            case 'd':
            case 'i':
                if (e.type == scanner::expr::INT)
                    result += format(style, e.xint);
                done = true;
                break;


            case 'f':
            case 'g':
            case 'e':
            case 'G':
            case 'E':
                if (e.type == scanner::expr::FLT)
                    result += format(style, double(e.xflt));
                else if (e.type == scanner::expr::DBL)
                    result += format(style, e.xdbl);
                done = true;
                break;

            case 'c':
                if (e.type == scanner::expr::BYTE ||
                    e.type == scanner::expr::NAT ||
                    e.type == scanner::expr::INT)
                    result += format(style, e.xint);
                else if (e.type == scanner::expr::TXT && e.xtxt->length() > 0)
                    result += format(style, (*e.xtxt)[0]);
                done = true;
                break;

            case 's':
            {
                text astxt;
                switch (e.type)
                {
                case scanner::expr::BYTE:
                case scanner::expr::NAT: astxt = std::to_string(e.xnat); break;
                case scanner::expr::INT: astxt = std::to_string(e.xint); break;
                case scanner::expr::FLT: astxt = std::to_string(e.xflt); break;
                case scanner::expr::DBL: astxt = std::to_string(e.xdbl); break;
                case scanner::expr::TXT: astxt = *e.xtxt; break;
                }
                result += format(style, astxt.c_str());
                done = true;
                break;
            }
            }
        }
        else if (c == '%')
        {
            infmt = true;
            style = c;
        }
        else
        {
            result += c;
        }
    }
    result += text(pos, fmt.end());
    return result;
}


bool scanner::expr_apply(char op)
// ----------------------------------------------------------------------------
//   Apply the top operator to the two operands on the stack
// ----------------------------------------------------------------------------
{
    if (exprs.size() < 2)
    {
        rt.error("Missing operand in expression");
        return false;
    }
    expr r = exprs.back();
    exprs.pop_back();
    expr l  = exprs.back();
    exprs.pop_back();

    record(scanner, "Expression[%zu] %u %c %u",
           exprs.size(), l.type, op, r.type);

    // Check all text operations
    if (op == '&')
    {
        // Deal with text concatenation cases
        if (l.type == expr::TXT || r.type == expr::TXT)
        {
            switch (l.type)
            {
            case expr::BYTE:
            case expr::NAT:     l = std::to_string(l.xnat); break;
            case expr::INT:     l = std::to_string(l.xint); break;
            case expr::FLT:     l = std::to_string(l.xflt); break;
            case expr::DBL:     l = std::to_string(l.xdbl); break;
            case expr::TXT:                                       break;
            }
            switch (r.type)
            {
            case expr::BYTE:
            case expr::NAT:     r = std::to_string(r.xnat); break;
            case expr::INT:     r = std::to_string(r.xint); break;
            case expr::FLT:     r = std::to_string(r.xflt); break;
            case expr::DBL:     r = std::to_string(r.xdbl); break;
            case expr::TXT:                                       break;
            }

            record(scanner, "TextUpcast[%zu] %u %c %u",
                   exprs.size(), l.type, op, r.type);

            exprs.emplace_back(*l.xtxt + *r.xtxt);
            return true;
        }
    }
    else if (op == '+' && l.type == expr::TXT && r.type == expr::TXT)
    {
        exprs.emplace_back(*l.xtxt + *r.xtxt);
        return true;
    }
    else if (op == '+' && l.type == expr::TXT && r.type <= expr::NAT)
    {
        size_t pos = std::min(r.xnat, l.xtxt->length());
        exprs.emplace_back(l.xtxt->substr(pos));
        return true;
    }
    else if (op == '-' && l.type == expr::TXT && r.type <= expr::NAT)
    {
        size_t len = l.xtxt->length();
        len = len > r.xnat ? len - r.xnat : 0;
        exprs.emplace_back(l.xtxt->substr(0, len));
        return true;
    }
    else if (op == '*' && l.type <= expr::NAT && r.type == expr::TXT)
    {
        exprs.emplace_back(l.xnat * *r.xtxt);
        return true;
    }
    else if (op == '*' && r.type <= expr::NAT && l.type == expr::TXT)
    {
        exprs.emplace_back(r.xnat * *l.xtxt);
        return true;
    }
    else if (op == '%' && l.type == expr::TXT)
    {
        exprs.emplace_back(format(*l.xtxt, r));
        return true;
    }
    else if (l.type == expr::TXT || r.type == expr::TXT)
    {
        record(scanner_error,
               "Invalid operands for operator: %u %c %u",
               l.type, op, r.type);
        rt.error("Invalid operands for operator");
        return false;
    }

    // Check promotions
#define PROMOTE(lt, rt, code)                         \
    do                                                \
    {                                                 \
        if (l.type == expr::lt && r.type == expr::rt) \
        {                                             \
            code;                                     \
        }                                             \
    } while (0)

    PROMOTE(NAT,BYTE, r = r.xnat);
    PROMOTE(BYTE,NAT, l = l.xnat);
    PROMOTE(INT,BYTE, r = value_t(r.xnat));
    PROMOTE(BYTE,INT, l = value_t(l.xnat));

    PROMOTE(DBL, FLT, r = double(r.xflt));
    PROMOTE(DBL, INT, r = double(r.xint));
    PROMOTE(DBL, NAT, r = double(r.xnat));
    PROMOTE(FLT, INT, r = float (r.xint));
    PROMOTE(FLT, NAT, r = float(r.xnat));
    PROMOTE(INT, NAT, r = value_t(r.xnat));

    PROMOTE(FLT, DBL, l = double(l.xflt));
    PROMOTE(INT, DBL, l = double(l.xint));
    PROMOTE(NAT, DBL, l = double(l.xnat));
    PROMOTE(INT, FLT, l = float(l.xint));
    PROMOTE(NAT, FLT, l = float(l.xnat));
    PROMOTE(NAT, INT, l = value_t(l.xnat));

    if (l.type != r.type)
    {
        record(scanner_error,
               "Invalid operands for operator: %u %c %u",
               l.type, op, r.type);
        rt.error("Invalid operands for operator");
        return false;
    }

#define OP(spelling, oper)                                              \
    do                                                                  \
    {                                                                   \
        if (op == spelling)                                             \
        {                                                               \
            switch (l.type)                                             \
            {                                                           \
            case expr::BYTE: l = expr(l.xnat oper r.xnat, true); break; \
            case expr::NAT:  l = l.xnat oper r.xnat; break;             \
            case expr::INT:  l = l.xint oper r.xint; break;             \
            case expr::DBL:  l = l.xdbl oper r.xdbl; break;             \
            case expr::FLT:  l = l.xflt oper r.xflt; break;             \
            default: error("Invalid types for " #spelling);             \
            }                                                           \
        }                                                               \
    } while (0)

    OP('+', + );
    OP('-', - );
    OP('*', * );
    OP('/', / );
    if (op == '%')
    {
        switch (l.type)
        {
        case expr::BYTE: l = expr(l.xnat % r.xnat, true); break;
        case expr::NAT:  l = l.xnat % r.xnat; break;
        case expr::INT:  l = l.xint % r.xint; break;
        case expr::DBL:  l = std::fmod(l.xdbl, r.xdbl); break;
        case expr::FLT:  l = std::fmod(l.xflt, r.xflt); break;
        default: error("Invalid types for '%'");
        }
    }

#define ROP(spelling, oper)                                             \
    do                                                                  \
    {                                                                   \
        if (op == spelling)                                             \
        {                                                               \
            switch (l.type)                                             \
            {                                                           \
            case expr::BYTE: l = byte(l.xnat oper r.xnat); break;       \
            case expr::NAT:  l = byte(l.xnat oper r.xnat); break;       \
            case expr::INT:  l = byte(l.xint oper r.xint); break;       \
            case expr::DBL:  l = byte(l.xdbl oper r.xdbl); break;       \
            case expr::FLT:  l = byte(l.xflt oper r.xflt); break;       \
            default: error("Invalid types for " #spelling);             \
            }                                                           \
        }                                                               \
    } while (0)

    ROP('<', < );
    ROP('=', == );
    ROP('>', > );

#define IOP(spelling, oper)                                             \
    do                                                                  \
    {                                                                   \
        if (op == spelling)                                             \
        {                                                               \
            switch (l.type)                                             \
            {                                                           \
            case expr::BYTE: l = byte(l.xnat oper r.xnat); break;       \
            case expr::NAT:  l = l.xnat oper r.xnat; break;             \
            case expr::INT:  l = l.xint oper r.xint; break;             \
            default: error("Invalid types for " #spelling);             \
            }                                                           \
        }                                                               \
    } while (0)

    IOP('&', & );
    IOP('|', | );
    IOP('^', ^ );

    exprs.emplace_back(l);
    return true;
}


bool scanner::expr_unary(char op)
// ----------------------------------------------------------------------------
//   Unary operator in an expression
// ----------------------------------------------------------------------------
{
    token t = next();
    if (t == DONE)
        return true;
    if (t == ERROR || !exprs.size())
        return false;

    expr  x = exprs.back();
    exprs.pop_back();

    switch (op)
    {
    case '+':
        // Turn natural numbers into signed integer
        if (x.type == expr::NAT || x.type == expr::BYTE)
            x = value_t(x.xnat);
        else if (x.type == expr::TXT)
            return error("Operator + applying to text"), false;
        break;
    case '-':
        switch (x.type)
        {
        case expr::BYTE:
        case expr::NAT:
        case expr::INT:
            x = -value_t(x.xnat);
            break;
        case expr::FLT:
            x = -x.xflt;
            break;
        case expr::DBL:
            x = -x.xdbl;
            break;
        case expr::TXT:
            return error("Operator - applying to text"), false;
        }
        break;
    case '~':
        switch (x.type)
        {
        case expr::BYTE:
        case expr::NAT:
        case expr::INT:
            x = ~value_t(x.xnat);
            break;
        case expr::FLT:
            x = value_t(x.xflt);
            break;
        case expr::DBL:
            x = value_t(x.xdbl);
            break;
        case expr::TXT:
            x = x.xtxt->length();
            break;
        }
        break;
    }

    exprs.emplace_back(x);
    return true;
}


bool scanner::label_value(expr &e, uint accepted)
// ----------------------------------------------------------------------------
//   Check an arbitrary expression
// ----------------------------------------------------------------------------
{
    ops.push_back('!');
    token t = next();
    ops.pop_back();
    if (~accepted & (1 << t))
        return false;
    if (!exprs.size())
        return false;
    e = exprs.back();
    exprs.pop_back();
    return true;
}


bool scanner::label_name()
// ----------------------------------------------------------------------------
//   A label name can be either a plain name or a text (possibly expression)
// ----------------------------------------------------------------------------
{
    expr e;
    if (!label_value(e, (1<<TEXT) | (1<<CHARACTER) | (1<<NAME)))
        return false;
    if (e.type == expr::TXT)
        scanned = *e.xtxt;
    return true;
}


bool scanner::do_extern()
// ----------------------------------------------------------------------------
//   Declare a name that will be defined later ("extern")
// ----------------------------------------------------------------------------
//   In the bytecode, ?foo reserves token foo without using it in the stream
{
    if (!label_name())
    {
        record(scanner_error,
               "Extern directive followed by %s",scanned.c_str());
        rt.error("Invalid '?' extern declaration");
        return false;
    }
    tok_t callee = rt.tok(scanned);
    if (!rt.defined.count(callee))
        rt.defined[callee] = undef(callee);
    return true;
}


bool scanner::do_set()
// ----------------------------------------------------------------------------
//   Declare a name for a constant (assignment)
// ----------------------------------------------------------------------------
//   In the bytecode, !foo defines value in runtime to whatever is coming next
//   For example, !foo 2 will define *foo as being 2
{
    if (!label_name())
    {
        record(scanner_error, "Set directive followed by %s", scanned.c_str());
        rt.error("Invalid '!' assignment name");
        return false;
    }
    text name = scanned;
    expr e;
    if (!label_value(e) || e.type > expr::INT ||
        (e.type == expr::INT && e.xint < 0))
    {
        record(scanner_error, "Set directive value %s", scanned.c_str());
        rt.error("Invalid '!' assigned value");
        return false;
    }

    tok_t setting = rt.tok(name);
    rt.defined[setting] = e.xnat;
    return set(constants, name, e.xnat);
}


bool scanner::do_immediate()
// ----------------------------------------------------------------------------
//   Convert input bytes to natural values
// ----------------------------------------------------------------------------
//   In general, the bytecode generates LEB128 values. So 128 is two bytes
//   By default, `128` will emit a 128 byte, #128 will convert to natural 128
//   in LEB128 encoding.
//   For text, "ABC" is by default a token, #"ABC" emits the sequence of bytes
{
    ops.push_back('#');
    token t = next();
    ops.pop_back();
    if (t == DONE || t == ERROR || exprs.size() == 0)
    {
        error("Invalid code following #");
        return false;
    }

    expr &e = exprs.back();

    // If the value is text, just insert as is
    if (e.type == expr::TXT)
    {
        program += *e.xtxt;
        exprs.pop_back();
        return true;
    }

    // If the value is a BYTE or an INT, convert to a NAT
    if (e.type == expr::BYTE || e.type == expr::INT || e.type == expr::NAT)
    {
        e.type = expr::NAT;
        return true;
    }

    record(scanner_error, "Invalid immedaite value %lu", e.type);
    rt.error("Invalid immediate value");
    return false;
}


bool scanner::do_use()
// ----------------------------------------------------------------------------
//   Using the value defined in the runtime for a given name
// ----------------------------------------------------------------------------
//   After using !foo 2, *foo will be the same as insert 2
{
    if (!label_name())
    {
        record(scanner_error, "Use directive followed by %s", scanned.c_str());
        rt.error("Can only use a symbol or text");
        return false;
    }
    tok_t     ntok  = rt.tok(scanned);
    tok_t     value = 0;
    size_t    pos   = pc();

    if (constants.count(scanned))
        value = constants[scanned];
    else if (rt.defined.count(ntok))
        value = rt.defined[ntok];
    else
        uconstants[scanned].push_back(pos);
    cstrefs.push_back(pos);
    append(value);
    return true;
}


bool scanner::do_branch_offset()
// ----------------------------------------------------------------------------
//   Insert a branch offset for the label
// ----------------------------------------------------------------------------
{
    if (!label_name())
    {
        record(scanner_error, "Use directive followed by %s", scanned.c_str());
        rt.error("Branch target must be a symbol or text");
        return false;
    }
    auto    it     = labels.find(scanned);
    bool    seen   = it != labels.end();
    value_t delta  = 0;
    size_t  branch = pc();
    if (seen)
        delta = value_t((*it).second - branch);
    else
        ulabels[scanned].push_back(branch);
    record(scanner,
           "Branch %zu index %zu to %+s is %+s, delta=%ld",
           branch, branches.size(), scanned.c_str(),
           seen ? "known" : "unknown", delta);
    branches.push_back(branch);
    append(delta);
    return true;
}


bool scanner::do_define()
// ----------------------------------------------------------------------------
//   Record a new definition
// ----------------------------------------------------------------------------
{
    if (!label_name())
    {
        record(scanner_error, "Definition '=' followed by %s", scanned.c_str());
        rt.error("Invalid name for definition");
        return false;
    }
    if (labels.count(scanned))
    {
        record(scanner_error, "Redefining label %s from %u to definition %u",
               scanned.c_str(), labels[scanned], pc());
        rt.error("Symbol / label redefinition");
        return false;
    }

    tok_t ntok = rt.tok(scanned);
    if (rt.defined.count(ntok))
    {
        if (rt.defined[ntok] != undef(ntok))
        {
            record(scanner_error, "Redefinition of %s, previously %zu",
                   scanned.c_str(), rt.defined[ntok]);
            rt.error("Symbol redefinition");
            return false;
        }
    }
    else
    {
        rt.defined[ntok] = undef(ntok);
    }

    scanner child(*this);
    bool    result = child.scan();
    if (!result)
        return false;
    tok_t callee = child.pgmid;
    rt.defined[ntok] = callee;
    return set(constants, scanned, callee);
}


bool scanner::do_label()
// ----------------------------------------------------------------------------
//   Insert a label in the labels table
// ----------------------------------------------------------------------------
{
    if (!label_name())
    {
        record(scanner_error, "Label '.' followed by %s", scanned.c_str());
        rt.error("Label must be a symbol or text");
        return false;
    }
    return set(labels, scanned, pc());
}


bool scanner::set(symbols &syms, text name, size_t target)
// ----------------------------------------------------------------------------
//  Set a label with a new value
// ----------------------------------------------------------------------------
{
    record(scanner, "Set %s=%zu", name.c_str(), target);

    if (syms.count(name))
    {
        record(scanner_error, "Redefining %s from %u to %u",
               name.c_str(), syms[name], pc());
        rt.error("Label redefinition");
        return false;
    }

    size_t *value = &syms[name];
    *value = target;

    // Check if this label was previously unkown in relative branches
    auto ulabel = ulabels.find(name);
    if (ulabel != ulabels.end())
    {
        // Patch all the branches we already have
        patches &plist = (*ulabel).second;
        for (size_t &branch : plist)
            patch_branch(branch, target);
        ulabels.erase(ulabel);
    }

    // Check if this label was previously unkown in absolute values
    auto uconstant = uconstants.find(name);
    if (uconstant != uconstants.end())
    {
        // Patch all the constant refs we already have
        patches &plist = (*uconstant).second;
        for (size_t &cstref : plist)
            patch_cstref(cstref, target);
        uconstants.erase(uconstant);
    }

    return true;
}


void scanner::patch_branch(size_t branch, size_t target)
// ----------------------------------------------------------------------------
//   Patch a branch to point to the target
// ----------------------------------------------------------------------------
//   The simple and frequent case is when the patched branch has the same size
//   However, since branch distance is encoded using LEB128, a branch delta may
//   end up being 2 or 3 bytes instead of 1, in which case we need to adjust
//   and recompute all impacted branch targets
{
    record(scanner, "Patching branch %u to %u", branch, target);

    // Location of branch delta in the program
    byte   *bp    = (byte *) program.data() + branch;
    size_t  esize = leb128size<value_t>(bp);

    // Compute new delta and new size
    value_t delta = value_t(target - branch);
retry:
    size_t dsize = leb128size(delta);
    size_t added = dsize - esize;
    if (added)
    {
        record(scanner, "  Adding %u bytes (%u to %u) delta=%ld",
               added, esize, dsize, delta);
        if (target > branch)
        {
            delta += added;
            if (leb128size(delta) > dsize)
                goto retry;
        }

        // Ouch complicated case: We need to insert bytes
        program.insert(branch, added, 0);
        bp = (byte *) program.data() + branch;
        leb128(bp, delta);
        patch_label_references(branch, added);
    }
    else
    {
        record(scanner, "  Same size delta=%ld", delta);
        leb128(bp, delta);
    }
}


void scanner::patch_cstref(size_t cstref, tok_t value)
// ----------------------------------------------------------------------------
//   Patch a constant reference to a new value
// ----------------------------------------------------------------------------
//   The simple and frequent case is when the patched branch has the same size
//   However, since branch distance is encoded using LEB128, a branch delta may
//   end up being 2 or 3 bytes instead of 1, in which case we need to adjust
//   and recompute all impacted branch targets
{
    record(scanner, "Patching constant %u to %u", cstref, value);

    // Location of branch delta in the program
    byte  *bp    = (byte *) program.data() + cstref;
    size_t esize = leb128size<tok_t>(bp);

    // Compute new delta and new size
    size_t nsize = leb128size(value);
    size_t added = nsize - esize;
    if (added)
    {
        record(scanner, "  Adding %u bytes (%u to %u) value=%lu",
               added, esize, nsize, value);

        // Ouch complicated case: We need to insert bytes
        program.insert(cstref, added, 0);
        bp = (byte *) program.data() + cstref;
        leb128(bp, value);
        patch_label_references(cstref, added);
    }
    else
    {
        record(scanner, "  Same size value=%lu", value);
        leb128(bp, value);
    }
}


void scanner::patch_label_references(size_t pos, size_t added)
// ----------------------------------------------------------------------------
//   Patch all possible references to a label that are above pc
// ----------------------------------------------------------------------------
{
    record(scanner,
           "Patching references to %zu adding %zu bytes to %zu branches,",
           pos, added, branches.size());
    record(scanner, "  %zu+%zu labels, %zu+%zu constants",
           labels.size(), ulabels.size(),
           cstrefs.size(), uconstants.size());

    // Change all existing cstrefs that are after branch
    for (size_t &ic : cstrefs)
    {
        record(scanner, "  cstref %zu +%zu", ic, ic > pos ? added : 0);
        if (ic > pos)
            ic += added;
    }
    // Change all labels that point beyond current
    for (auto &l : labels)
    {
        record(scanner, "  label %zu +%zu %+s",
               l.second, l.second > pos ? added : 0, l.first.c_str());
        if (l.second > pos)
            l.second += added;
    }

    // Change all unknown labels that point beyond current branch
    for (auto &p : ulabels)
    {
        record(scanner, "  unknown label %+s", p.first.c_str());
        for (size_t &ub : p.second)
        {
            record(scanner, "    ulabel %zu +%zu", ub, ub > pos ? added : 0);
            if (ub > pos)
                ub += added;
        }
    }

    // Change all constant references that point beyond current branch
    for (auto &p : uconstants)
    {
        record(scanner, "  unknown constant %+s", p.first.c_str());
        for (size_t &uc : p.second)
        {
            record(scanner, "    uconst %zu +%zu", uc, uc > pos ? added : 0);
            if (uc > pos)
                uc += added;
        }
    }

    // Loop over all existng branches to adjust them
    for (size_t &ib : branches)
    {
        size_t   ibd    = ib > pos ? added : 0;
        byte    *bp     = (byte *) program.data() + ib + ibd;
        byte_p   b      = byte_p(bp);
        value_t  edelta = leb128<value_t>(b);

        // Check if the branch address itself needs to be patched
        record(scanner, "  branch %zu+%zu delta=%ld +%zu",
               ib, ibd, edelta, ibd);
        if (ibd)
            ib += added;

        // Check branches that cross over
        if (ib < pos && edelta > 0 && ib + edelta > pos)
        {
            // Forward branch crossing over
            record(scanner, "  backward %zu new=%ld", ib, edelta + added);
            patch_branch(ib, ib + edelta + added);
        }
        else if (ib > pos && edelta < 0 && ib + edelta < pos)
        {
            // Backward branch crossing over
            record(scanner, "  forward %zu new=%ld", ib, edelta - added);
            patch_branch(ib, ib + edelta - added);
        }
    }
}


void scanner::append_byte(uvalue_t b)
// ----------------------------------------------------------------------------
//   Append a signed integer to the program
// ----------------------------------------------------------------------------
{
    if (b > 255)
        record(scanner_error, "Byte value %u is too large", b);
    program += byte(b);
}


void scanner::append(value_t i)
// ----------------------------------------------------------------------------
//   Append a signed integer to the program
// ----------------------------------------------------------------------------
{
    byte   buffer[LEB128_MAXSIZE];
    byte  *bp = buffer;
    bp = leb128(bp, i);
    program += text((char *) buffer, bp - buffer);
}


void scanner::append(uvalue_t u)
// ----------------------------------------------------------------------------
//  Append an unsigned integer to the program
// ----------------------------------------------------------------------------
{
    byte   buffer[LEB128_MAXSIZE];
    byte  *bp = buffer;
    bp = leb128(bp, u);
    program += text((char *) buffer, bp - buffer);
}


void scanner::append(float f)
// ----------------------------------------------------------------------------
//   Append a float value to the program
// ----------------------------------------------------------------------------
{
    program += text((char *) &f, sizeof(f));
}


void scanner::append(double d)
// ----------------------------------------------------------------------------
//   Append a float value to the program
// ----------------------------------------------------------------------------
{
    program += text((char *) &d, sizeof(d));
}


void scanner::append(ostream *out, value_t i)
// ----------------------------------------------------------------------------
//   Append a signed integer to the stream
// ----------------------------------------------------------------------------
{
    if (out)
    {
        byte   buffer[LEB128_MAXSIZE];
        byte  *bp = buffer;
        bp = leb128(bp, i);
        *out << text((char *) buffer, bp - buffer);
    }
}


void scanner::append(ostream *out, uvalue_t u)
// ----------------------------------------------------------------------------
//  Append an unsigned integer to the program
// ----------------------------------------------------------------------------
{
    if (out)
    {
        byte   buffer[LEB128_MAXSIZE];
        byte  *bp = buffer;
        bp = leb128(bp, u);
        *out << text((char *) buffer, bp - buffer);
    }
}


void scanner::append(ostream *out, float f)
// ----------------------------------------------------------------------------
//   Append a float value to the program
// ----------------------------------------------------------------------------
{
    if (out)
        *out << text((char *) &f, sizeof(f));
}


void scanner::append(ostream *out, double d)
// ----------------------------------------------------------------------------
//   Append a float value to the program
// ----------------------------------------------------------------------------
{
    if (out)
        *out << text((char *) &d, sizeof(d));
}


tok_t scanner::undef(tok_t id)
// ----------------------------------------------------------------------------
//   Generate an opcode to trap undefined externals
// ----------------------------------------------------------------------------
{
    byte buffer[16];
    byte *bp = buffer;
    bp       = leb128(bp, runtime::op_undef);
    bp       = leb128(bp, id);
    return rt.tok(utf8(buffer), bp - buffer);
}


bool scanner::scan(istream *in, byte itok, byte ival, runtime &rt)
// ----------------------------------------------------------------------------
//   Scanner interface for generated code
// ----------------------------------------------------------------------------
{
    if (!in)
        return false;

    scanner s(*in, rt);
    expr    e;
    if (!s.label_value(e))
        return false;
    rt.frame[itok] = e.type;
    rt.frame[ival] = e.xnat;
    return true;
}
