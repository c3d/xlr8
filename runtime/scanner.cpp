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
#include "runtime.h"
#include "leb128.h"

#include <cctype>


const size_t LEB128_MAXSIZE = 16;


bool scanner::scan(runtime &rt)
// ----------------------------------------------------------------------------
//   Scan input tokens from the stream and build program object
// ----------------------------------------------------------------------------
{
    while (true)
    {
        token tok = next();
        switch (tok)
        {
        case DONE:
            return validate(rt);
        case ERROR:
            return false;
        case COMMENT:
            break;
        case SYMBOL:
            if (!do_symbol(rt))
                return false;
            break;
        case NAME:
            if (!do_name(rt))
                return false;
            break;
        case INTEGER:
            if (!do_integer(rt))
                return false;
            break;
        case REAL:
            if (!do_real(rt))
                return false;
            break;
        case TEXT:
            if (!do_text(rt))
                return false;
            break;
        }
    }
}


bool scanner::validate(runtime &rt)
// ----------------------------------------------------------------------------
//   Check if what we scanned is OK
// ----------------------------------------------------------------------------
{
    if (unknown.size())
    {
        rt.error("Undefined label");
        return false;
    }
    tok_t prog = rt.tok(program);
    rt.pc = byte_p(rt.tok(prog));
    return true;
}


scanner::token scanner::next()
// ----------------------------------------------------------------------------
//  Parse one token from input
// ----------------------------------------------------------------------------
{
    if (input.eof())
        return DONE;
    if (input.bad())
        return ERROR;

    scanned = "";

    int c = input.get();

    // Skip whitespace
    while (input.good() && std::isspace(c))
        c = input.get();
    if (!input.good())
        return input.eof() ? DONE : ERROR;

    // Skip comments (C / C++ style syntax)
    if (c == '/')
    {
        int nc = input.peek();
        if (nc == '/')
        {
            while (input.good() && c != '\n')
            {
                scanned += char(c);
                c = input.get();
            }
            return COMMENT;
        }
        else if (nc == '*')
        {
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
        return SYMBOL;
    }

    // Scan names
    if (std::isalpha(c))
    {
        while (input.good() && (std::isalnum(c) || c == '_'))
        {
            scanned += char(c);
            c = input.get();
        }
        input.putback(c);
        return NAME;
    }

    // Scan numbers
    if (std::isdigit(c) || c == '+' || c == '-')
    {
        // Accept specific XL syntax e.g. 16#2.AFE#E-3
        bool is_real = false;
        while (input.good() && (std::isalnum(c) || c == '_' || c == '#' ||
                                c == '+' || c == '-' || c == '.'))
        {
            scanned += char(c);
            if (c == '.')
                is_real = true;
            c = input.get();
        }
        return is_real ? REAL : INTEGER;
    }

    // Scan text
    if (c == '\'' || c == '"')
    {
        char delim = c;
        bool done = false;
        while (input.good() && !done)
        {
            scanned += char(c);
            if (c == delim)
                done = input.peek() != delim;
            c = input.get();
        }
        return TEXT;
    }

    // Anything else is a symbol
    scanned += char(c);
    return c == ';' ? DONE : SYMBOL;
}


text scanner::spelling()
// ----------------------------------------------------------------------------
//  Return the spelling for the last scanned token
// ----------------------------------------------------------------------------
{
    return scanned;
}


uintptr_t scanner::as_natural(uint *basep)
// ----------------------------------------------------------------------------
//   Parse the value as a positive integer
// ----------------------------------------------------------------------------
{
    uint      base    = 10;
    uintptr_t value   = 0;
    bool      hasneg  = false;
    bool      hasnot  = false;
    bool      isfirst = true;

    for (char c : scanned)
    {
        if (isfirst)
        {
            if (c == '+')
                ;
            else if (c == '-')
                hasneg = !hasneg;
            else if (c == '~')
                hasnot = !hasnot;
            else
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
            if (value < 2 || value >= 36)
                break;
            base = value;
            value = 0;
            continue;
        }

        // Regular digits
        uint digit = c - '0';
        if (c >= 'a')
            digit = c - 'a' + 10;
        else if (c >= 'A')
            digit = c - 'A' + 10;

        if (digit >= base)
            break;
        value = value * base + digit;
    }

    if (basep)
        *basep = base;
    if (hasneg)
        value = -value;
    if (hasnot)
        value = ~value;
    return value;
}


intptr_t scanner::as_integer(uint *basep)
// ----------------------------------------------------------------------------
//   Parse the value as a signed integer
// ----------------------------------------------------------------------------
{
    return intptr_t(as_natural(basep));
}


double scanner::as_real(uint *basep)
// ----------------------------------------------------------------------------
//   Parse a number as a real number
// ----------------------------------------------------------------------------
{
    uint      base    = 10;
    uintptr_t value   = 0;
    double    dvalue  = 0.0;
    double    dmult   = 1.0;
    bool      hasneg  = false;
    bool      hasdot  = false;
    bool      isfirst = true;

    for (char c : scanned)
    {
        if (isfirst)
        {
            if (c == '+')
                ;
            else if (c == '-')
                hasneg = !hasneg;
            else
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
            if (value < 2 || value >= 36)
                break;
            base = value;
            value = 0;
            continue;
        }

        // Check decimal point
        if (c == '.')
        {
            if (hasdot)
                break;
            hasdot = true;
            continue;
        }

        // Regular digits
        uint digit = c - '0';
        if (c >= 'a')
            digit = c - 'a' + 10;
        else if (c >= 'A')
            digit = c - 'A' + 10;

        if (digit >= base)
            break;
        value = value * base + digit;
        if (hasdot)
        {
            dmult /= base;
            dvalue += dmult * digit;
        }
        else
        {
            dvalue = dvalue * base + digit;
        }
    }

    if (basep)
        *basep = base;
    return dvalue;
}


text scanner::as_text()
// ----------------------------------------------------------------------------
//  Return the inner value, skipping double quotes
// ----------------------------------------------------------------------------
{
    text result  = "";
    bool isfirst = true;
    char delim   = 0;
    char last    = 0;
    for (char c : scanned)
    {
        if (isfirst)
            delim = c;
        if (c != delim || last == delim)
            result += c;
        last = c;
    }
    return result;
}


bool scanner::do_name(runtime &rt)
// ----------------------------------------------------------------------------
//   Process a name by adding the token to the token table
// ----------------------------------------------------------------------------
{
    tok_t rtok = rt.tok(scanned.data(), scanned.length());
    append(rtok);
    return !rt.error();
}


bool scanner::do_symbol(runtime &rt)
// ----------------------------------------------------------------------------
//   Process a name by adding the token to the token table
// ----------------------------------------------------------------------------
{
    if (!scanned.size())
        return false;

    char c = scanned[0];
    switch (c)
    {
    case '*':
        return do_use(rt);
    case '=':
        return do_define(rt);
    case '.':
        return do_label(rt);
    case ':':
        return do_branch_offset(rt);
    case ';':
        return false;
    default:
        rt.error("Unknown symbol");
    }
    return false;
}


bool scanner::do_use(runtime &rt)
// ----------------------------------------------------------------------------
//   Opcode use directive (`*')
// ----------------------------------------------------------------------------
{
    if (next() != NAME)
    {
        rt.error("Invalid opcode import");
        return false;
    }
    return rt.use(scanned);
}


bool scanner::do_integer(runtime &rt)
// ----------------------------------------------------------------------------
//   Process an integer value. If + or - treat it as signed, otherwise unsigned
// ----------------------------------------------------------------------------
{
    if (!scanned.size())
        return false;

    char c = scanned[0];
    if (c == '+' || c == '-')
    {
        intptr_t value = as_integer();
        append(value);
    }
    else
    {
        uintptr_t value = as_natural();
        append(value);
    }
    return true;
}


bool scanner::do_real(runtime &rt)
// ----------------------------------------------------------------------------
//  Insert a real number
// ----------------------------------------------------------------------------
{
    char buf[sizeof(double)];
    double d = as_real();
    memcpy(buf, &d, sizeof(double));
    program += text(buf, sizeof(double));
    return true;
}


bool scanner::do_text(runtime &rt)
// ----------------------------------------------------------------------------
//   Insert a text directly in the byte code
// ----------------------------------------------------------------------------
{
    byte   buffer[LEB128_MAXSIZE];
    text   t    = as_text();
    size_t len  = t.size();
    byte  *bp   = leb128(buffer, len);
    program += text((char *) buffer, bp - buffer);
    program += t;
    return true;
}


bool scanner::do_define(runtime &rt)
// ----------------------------------------------------------------------------
//   Define a new opcode
// ----------------------------------------------------------------------------
{
    if (next() != SYMBOL)
    {
        rt.error("Invalid opcode definition");
        return false;
    }
    text    name = scanned;
    scanner child(input);
    bool    result = child.scan(rt);
    tok_t   callee = rt.tok(child.program);
    rt.use(callee, runtime::op_call);
    return result;
}


bool scanner::do_label(runtime &rt)
// ----------------------------------------------------------------------------
//   Insert a label in the labels table
// ----------------------------------------------------------------------------
{
    token t = next();
    if (t != SYMBOL && t != TEXT)
    {
        rt.error("Label must be a symbol or text");
        return false;
    }
    text name = t == TEXT ? as_text() : scanned;
    known[name] = pc();

    // Check if this label was previously unkown, if so adjust branches
    auto it = unknown.find(name);
    if (it != unknown.end())
    {
        // Patch all the branches we already have
        patches &plist = (*it).second;
        for (size_t &branch : plist)
            patch_branch(branch, pc());
        unknown.erase(it);
    }
    return true;
}


bool scanner::do_branch_offset(runtime &rt)
// ----------------------------------------------------------------------------
//   Insert a branch offset for the label
// ----------------------------------------------------------------------------
{
    token t = next();
    if (t != SYMBOL && t != TEXT)
    {
        rt.error("Branch target must be a symbol or text");
        return false;
    }
    text     name   = t == TEXT ? as_text() : scanned;
    auto     it     = known.find(name);
    bool     seen   = it != known.end();
    intptr_t delta  = 0;
    size_t   branch = pc();
    if (seen)
        delta = intptr_t((*it).second - branch);
    else
        unknown[name].push_back(branch);
    branches.push_back(branch);
    append(delta);
    return true;
}


void scanner::patch_branch(size_t &branch, size_t target)
// ----------------------------------------------------------------------------
//   Patch a branch to point to the target
// ----------------------------------------------------------------------------
//   The simple and frequent case is when the patched branch has the same size
//   However, since branch distance is encoded using LEB128, a branch delta may
//   end up being 2 or 3 bytes instead of 1, in which case we need to adjust
//   and recompute all impacted branch targets
{
    // Location of branch delta in the program
    byte    *bp     = (byte *) program.data() + branch;
    byte_p   b      = byte_p(bp);
    intptr_t edelta = leb128<intptr_t>(b);
    size_t   esize  = b - byte_p(bp);

    // Compute new delta and new size
    intptr_t delta  = intptr_t(target - branch);
    size_t   dsize  = leb128size(delta);
    size_t   added  = dsize - esize;
    if (added)
    {
        // Ouch complicated case: We need to insert bytes
        program.insert(branch, added, 0);

        // Loop over all existng branches to adjust them
        for (size_t &ib : branches)
        {
            bp     = (byte *) program.data() + ib;
            b      = byte_p(bp);
            edelta = leb128<intptr_t>(b);

            // Check if the branch address itself needs to be patched
            if (ib > branch)
                ib += added;

            // Check branches that cross over
            if (ib < branch && edelta > 0 && ib + edelta > branch)
            {
                // Forward branch crossing over
                patch_branch(ib, ib + edelta + added);
            }
            else if (ib > branch && edelta < 0 && ib - edelta < branch)
            {
                // Backward branch crossing over
                patch_branch(ib, ib + edelta - added);
            }
        }

        // Change all labels that point beyond current
        for (auto &l : known)
            if (l.second > branch)
                l.second += added;

        // Change all unknown labels that point beyond current branch
        for (auto &p : unknown)
            for (size_t &ub : p.second)
                if (ub > branch)
                    ub += added;

        // Update byte pointer for final patching
        bp = (byte *) program.data() + branch;
    }

    leb128(bp, delta);
}


void scanner::append(intptr_t i)
// ----------------------------------------------------------------------------
//   Append a signed integer to the program
// ----------------------------------------------------------------------------
{
    byte   buffer[LEB128_MAXSIZE];
    byte  *bp = buffer;
    bp = leb128(bp, i);
    program += text((char *) buffer, bp - buffer);
}


void scanner::append(uintptr_t u)
// ----------------------------------------------------------------------------
//  Append an unsigned integer to the program
// ----------------------------------------------------------------------------
{
    byte   buffer[LEB128_MAXSIZE];
    byte  *bp = buffer;
    bp = leb128(bp, u);
    program += text((char *) buffer, bp - buffer);
}
