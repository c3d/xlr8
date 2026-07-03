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
        switch (tok)
        {
        case DONE:
            return macro ? true : validate();
        case ERROR:
            return false;
        case COMMENT:
            break;
        case SYMBOL:
            if (!do_symbol())
                return false;
            break;
        case NAME:
            if (!do_name())
                return false;
            break;
        case INTEGER:
            if (!do_integer())
                return false;
            break;
        case REAL:
            if (!do_real())
                return false;
            break;
        case TEXT:
            if (!do_text())
                return false;
            break;
        }
    }
    return !rt.error();
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
    tok_t prog = rt.tok(program);
    rt.entry = byte_p(rt.tok(prog));
    rt.pc = rt.entry;
    record(scanner, "Scanned %lu bytes entry %p", program.size(), rt.entry);
    if (RECORDER_TRACE(scanner) > 1)
        for (size_t i = 0; i < program.size(); i++)
            record(scanner, "  [%zu] = %02x", i, (byte) program[i]);
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
    {
        record(scanner_error, "I/O error on scanner input: %s",
               strerror(errno));
        return ERROR;
    }

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
        if (input.good())
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
        if (input.good())
            input.putback(c);
        return is_real ? REAL : INTEGER;
    }

    // Scan text
    if (c == '\'' || c == '"')
    {
        char delim = c;
        bool done  = false;
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
            }
        }
        if (input.good())
            input.putback(c);
        return TEXT;
    }

    // Anything else is a symbol
    scanned += char(c);
    return c == ';' ? DONE : SYMBOL;
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
            {
                record(scanner_error, "Invalid base %llu", value);
                return 0;
            }
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
        {
            record(scanner_error, "Invalid digit '%c' in base %u, value %u",
                   c, base, digit);
            break;
        }
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


size_t scanner::as_real(double &dval, float &fval, uint *basep)
// ----------------------------------------------------------------------------
//   Parse a number as a real number
// ----------------------------------------------------------------------------
{
    uint      base    = 10;
    uint      mbase   = 10;
    uintptr_t value   = 0;
    double    dvalue  = 0.0;
    double    dmult   = 1.0;
    float     fvalue  = 0.0;
    float     fmult   = 1.0;
    bool      isfirst = true;
    bool      hasneg  = false;
    bool      hasmneg = false;
    bool      hasdot  = false;
    bool      hasbase = false;
    bool      hasexp  = false;
    bool      hashash = false;
    uint      size    = sizeof(double);

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
            if (!hasbase)
            {
                if (value < 2 || value >= 36)
                {
                    record(scanner_error, "Invalid fp base %llu", value);
                    return 0;
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
            if (hasdot)
            {
                record(scanner_error, "Two dots in %s", scanned.c_str());
                break;
            }
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
        {
            char lc = tolower(c);
            if (lc == 'e')
            {
                if (!hasexp)
                {
                    hasexp  = true;
                    hasmneg = hasneg;
                    value   = 0;
                    base    = 10;
                    hasneg  = false;
                    isfirst = true;
                    continue;
                }
                if (hasbase)
                    continue;
            }
            if (lc == 'f')
                size = sizeof(float);
            else if (lc == 'd')
                size = sizeof(double);
            if (lc == 'f' || lc == 'd' || lc == 'e')
                continue;
            record(scanner_error, "Invalid fp digit '%c' in base %u, value %u",
                   c, base, digit);
            return 0;
        }
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
    }
    dval = dvalue;
    fval = fvalue;

    if (basep)
        *basep = base;
    record(scanner, "as_real %s -> d=%g f=%g sz=%zu",
           scanned.c_str(), dval, fval, size);
    return size;
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
        {
            delim = c;
            isfirst = false;
        }
        if (c != delim || last == delim)
            result += c;
        last = c;
    }
    return result;
}


bool scanner::do_integer()
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


bool scanner::do_real()
// ----------------------------------------------------------------------------
//  Insert a real number
// ----------------------------------------------------------------------------
{
    char buf[sizeof(double)];
    double d  = 0.0;
    float  f  = 0.0;
    size_t sz = as_real(d, f);
    memcpy(buf, sz == sizeof(float) ? (void *) &f : (void *) &d, sz);
    program += text(buf, sz);
    record(scanner, "do_real %s embedded %zu bytes", scanned.c_str(), sz);
    return sz != 0;
}


bool scanner::do_text()
// ----------------------------------------------------------------------------
//   Insert a token for a text directly in the byte code
// ----------------------------------------------------------------------------
{
    text   t    = as_text();
    tok_t  tok  = rt.tok(t);
    append(tok);
    return true;
}


bool scanner::do_name()
// ----------------------------------------------------------------------------
//   Process a name by adding the token to the token table
// ----------------------------------------------------------------------------
{
    if (auto found = macros.find(scanned); found != macros.end())
        return do_macro_use((*found).second);
    tok_t rtok = rt.tok(scanned);
    append(rtok);
    return !rt.error();
}


bool scanner::do_symbol()
// ----------------------------------------------------------------------------
//   Process a name by adding the token to the token table
// ----------------------------------------------------------------------------
{
    if (!scanned.size())
        return false;

    char c = scanned[0];
    switch (c)
    {
    case '=':
        return do_define();
    case '?':
        return do_extern();
    case '!':
        return do_set();
    case '.':
        return do_label();
    case ':':
        return do_branch_offset();
    case '*':
        return do_use();
    case '#':
        return do_bytes();
    case '[':
        return do_macro_definition();
    case ';':
        return false;
    default:
        record(scanner_error, "Unknown symbol %s", scanned.c_str());
        rt.error("Unknownsymbol");
    }
    return false;
}


bool scanner::do_macro_definition()
// ----------------------------------------------------------------------------
//   Enter a macro definition in our table
// ----------------------------------------------------------------------------
//   A macro definition has the form            [MYMACRO A B C].
//   Arguments can be placed between brackets:  [cmpge[r a b] cmple r b a]
//   Local macros inside macros are allowed:    [stuff[r] [x (r+1)] r x x r]
//
//   Arguments behave as local macros during expansion
{
    token t = next();
    if (t != NAME)
    {
        record(scanner_error, "Invalid macro name %s", scanned.c_str());
        rt.error("Invalid macro name");
        return false;
    }

    bool   first = true;
    bool   done  = false;
    bool   parms = false;
    uint   depth = 0;
    text   name  = scanned;
    macro  here;
    record(macros, "Define[%s]=%s", scanned.c_str(), here.definition.c_str());
    here.definition = "";
    while (!done)
    {
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
                continue;
            }
            break;
        case SYMBOL:
            switch (scanned[0])
            {
            case '[':
                depth++;
                if (first)
                {
                    parms = true;
                    first = false;
                    continue;
                }
                break;
            case ']':
                done = !depth;
                --depth;
                if (depth == 0 && parms)
                {
                    parms = false;
                    continue;
                }
                break;
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
                       "Invalid macro parameter %s", scanned.c_str());
                rt.error("Invalid macro parameter");
                return false;
            }
            if (here.definition.length())
                here.definition += ' ';
            here.definition += scanned;
        }
    }
    record(macros, "Defined[%s]=[%s]", name.c_str(), here.definition.c_str());
    macros[name] = here;
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
    save<macros_defs> saved(macros, macros);

    // Read parameters in the macros
    for (text parm : m.parameters)
    {
        // For each parameter, create a new macro (or overwite existing)
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


bool scanner::do_extern()
// ----------------------------------------------------------------------------
//   Declare a name that will be defined later ("extern")
// ----------------------------------------------------------------------------
//   In the bytecode, ?foo reserves token foo without using it in the stream
{
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Invalid extern declaration");
        return false;
    }
    text  name = t == TEXT ? as_text() : scanned;
    tok_t callee = rt.tok(name);
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
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Invalid assignment");
        return false;
    }
    text  name = t == TEXT ? as_text() : scanned;
    setting = rt.tok(name);
    return true;
}


bool scanner::do_bytes()
// ----------------------------------------------------------------------------
//   Emit a byte directly
// ----------------------------------------------------------------------------
//   In general, the bytecode generates LEB128 values. So 128 is two bytes
//   The `#` operator makes it possible to emit a byte directly
//   So #128 is a one-byte 128, and #16#FF is a one-byte all-bits-set value
//   Names are accepted as long as the associated token is 1-byte
//   Text is turned into a sequence of bytes
{
    token t = next();
    uintptr_t b = 0;

    switch (t)
    {
    case INTEGER:
        b = as_natural();
        break;
    case NAME:
        b = rt.tok(scanned);
        break;
    case TEXT:
        program += as_text();
        return true;
    default:
        record(scanner_error, "Invalid byte token %u: %+s", t, scanned.c_str());
        rt.error("Invalid byte syntax");
        return false;
    }

    if (b < 256)
    {
        program += char(b);
        return true;
    }
    record(scanner_error, "Invalid byte value %lu", b);
    rt.error("Invalid byte value");
    return false;
}


bool scanner::do_use()
// ----------------------------------------------------------------------------
//   Using the value defined in the runtime for a given name
// ----------------------------------------------------------------------------
//   After using !foo 2, *foo will be the same as insert 2
{
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Can only use a symbol or text");
        return false;
    }
    text      name  = t == TEXT ? as_text() : scanned;
    tok_t     ntok  = rt.tok(name);
    tok_t     value = 0;
    size_t    pos   = pc();

    if (constants.count(name))
        value = constants[name];
    else if (rt.defined.count(ntok))
        value = rt.defined[ntok];
    else
        uconstants[name].push_back(pos);
    cstrefs.push_back(pos);
    append(value);
    return true;
}


bool scanner::do_branch_offset()
// ----------------------------------------------------------------------------
//   Insert a branch offset for the label
// ----------------------------------------------------------------------------
{
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Branch target must be a symbol or text");
        return false;
    }
    text     name   = t == TEXT ? as_text() : scanned;
    auto     it     = labels.find(name);
    bool     seen   = it != labels.end();
    intptr_t delta  = 0;
    size_t   branch = pc();
    if (seen)
        delta = intptr_t((*it).second - branch);
    else
        ulabels[name].push_back(branch);
    record(scanner,
           "Branch %zu index %zu to %+s is %+s, delta=%ld",
           branch, branches.size(), name.c_str(),
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
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Invalid name for definition");
        return false;
    }
    text    name = t == TEXT ? as_text() : scanned;
    if (labels.count(name))
    {
        record(scanner_error, "Redefining label %s from %u to definition %u",
               name.c_str(), labels[name], pc());
        rt.error("Symbol / label redefinition");
        return false;
    }

    tok_t ntok = rt.tok(name);
    if (rt.defined.count(ntok))
    {
        if (rt.defined[ntok] != undef(ntok))
        {
            record(scanner_error, "Redefinition of %s, previously %zu",
                   name.c_str(), rt.defined[ntok]);
            rt.error("Symbol redefinition");
            return false;
        }
    }
    else
    {
        rt.defined[ntok] = undef(ntok);
    }

    scanner child(input, rt);
    bool    result = child.scan();
    if (!result)
        return false;
    text &cpgm = child.program;
    tok_t callee = rt.tok(cpgm);
    rt.defined[ntok] = callee;
    return set(constants, name, callee);
}


bool scanner::do_label()
// ----------------------------------------------------------------------------
//   Insert a label in the labels table
// ----------------------------------------------------------------------------
{
    token t = next();
    if (t != NAME && t != TEXT)
    {
        rt.error("Label must be a symbol or text");
        return false;
    }
    text name = t == TEXT ? as_text() : scanned;
    return set(labels, name, pc());
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
    byte    *bp    = (byte *) program.data() + branch;
    size_t   esize = leb128size<intptr_t>(bp);

    // Compute new delta and new size
    intptr_t delta = intptr_t(target - branch);
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
        intptr_t edelta = leb128<intptr_t>(b);

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


bool scanner::set(uintptr_t u)
// ----------------------------------------------------------------------------
//   Set the value of a symbol
// ----------------------------------------------------------------------------
{
    if (setting)
    {
        size_t sz = 0;
        utf8 txt = rt.tok(setting, &sz);
        text name(txt, sz);
        rt.defined[setting] = u;
        setting = 0;
        return set(constants, name, u);
    }
    return false;
}


void scanner::append(intptr_t i)
// ----------------------------------------------------------------------------
//   Append a signed integer to the program
// ----------------------------------------------------------------------------
{
    if (!set(uintptr_t(i)))
    {
        byte   buffer[LEB128_MAXSIZE];
        byte  *bp = buffer;
        bp = leb128(bp, i);
        program += text((char *) buffer, bp - buffer);
    }
}


void scanner::append(uintptr_t u)
// ----------------------------------------------------------------------------
//  Append an unsigned integer to the program
// ----------------------------------------------------------------------------
{
    if (!set(u))
    {
        byte   buffer[LEB128_MAXSIZE];
        byte  *bp = buffer;
        bp = leb128(bp, u);
        program += text((char *) buffer, bp - buffer);
    }
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
