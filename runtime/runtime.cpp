// ****************************************************************************
//  runtime.cpp                                                    xlr8 project
// ****************************************************************************
//
//   File Description:
//
//     Runtime that evaluates xlr8 bytecode, manages memory, etc
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2025 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of xlr8.
//
//   xlr8 is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   xlr8 is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "runtime.h"
#include <cstdlib>
#include <iostream>
#include <locale>


runtime::runtime(size_t sz)
// ----------------------------------------------------------------------------
//   Initialize the runtime with the given memory size
// ----------------------------------------------------------------------------
    : defined(),
      memory(), heap(), stack(), frame(), histack(),
      tokens(nullptr), ntokens(0), itokens(32),
      err(), entry()
{
    // Allocate space for the heap and stack
    memory = (byte_p) std::malloc(sz);
    if (!memory)
        error("Not enough memory for heap");
    heap = memory;
    stack = (stack_p) (memory + sz);
    frame = stack;
    histack = stack;

#define OP(op, code)            tok(text(#op));
#include "runtime.tbl"
}


runtime::~runtime()
// ----------------------------------------------------------------------------
//   Free resources allocated by this runtime
// ----------------------------------------------------------------------------
{
    std::free((void *) memory);
    std::free(tokens);
}



// ============================================================================
//
//   Main runtime loop
//
// ============================================================================

template<typename T> inline T input()
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    T v;
    std::cin >> v;
    return v;
}


template<typename T> inline bool output(T v)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    return (std::cout << v).good();
}


inline tok_t input_text(runtime &rt)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    text v;
    std::cin >> v;
    return rt.tok(v);
}


bool output_text(runtime &rt, value_t tok)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    size_t len  = 0;
    utf8   txt  = rt.tok(tok, &len);
    text   tval = text(txt, len);
    return (std::cout << tval).good();
}


inline bool output_newline()
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    return (std::cout << '\n').good();
}


bool runtime::run(byte_p &pc)
// ----------------------------------------------------------------------------
//    The main runtime loop
// ----------------------------------------------------------------------------
{
    bool    done = false;
    bool    ok   = true;
    byte    s, d;
    tok_t   t;
    broff_t b;
    byte_p  bpc;

    while (!done)
    {
        op_t op = op_t(*pc++);
        switch (op)
        {
#define uframe          ((uvalue_t *) frame)
#define uvalue          ((uvalue_t) value)
#define DST             d = *pc++
#define SRC             s = *pc++
#define DINT            frame[d]
#define SINT            frame[s]
#define DNAT            uframe[d]
#define SNAT            uframe[s]
#define TOK             t = arg<tok_t>(pc)
#define JMP_TOK         pc = byte_p(tok(t))
#define BR_OFF          bpc = pc; b = arg<broff_t>(pc)
#define BRANCH          pc = bpc + b;
#define OP(op, code)    case op_##op: { code; } break;
#include "runtime.tbl"
#undef uframe
        default:
            error("Invalid opcode");
            done = true;
            ok   = false;
            break;
        }
    }
    return ok;
}



// ============================================================================
//
//   Support routines
//
// ============================================================================

byte **runtime::retoks(size_t size)
// ----------------------------------------------------------------------------
//   Resize the number of tokens
// ----------------------------------------------------------------------------
{
    if (size <= ntokens)
    {
        error("Cannot shrink tokens table");
        return nullptr;
    }

    // We ran out of token, make pool bigger
    size_t newsz = size * sizeof(*tokens);
    byte_p *retok = (byte_p *) realloc(tokens, newsz);
    if (!retok)
    {
        error("Out of memory growing tokens table");
        return nullptr;
    }

    tokens = retok;
    for (size_t i = ntokens; i < size; i++)
        tokens[i] = nullptr;
    byte **result = (byte **) (tokens + ntokens);
    ntokens = size;
    return result;
}


byte *runtime::allocate(size_t len, byte_p data)
// ----------------------------------------------------------------------------
//   Allocate and initialize some sized data
// ----------------------------------------------------------------------------
{
    size_t size = leb128size(len) + len;
    byte  *storage = allocate(size);
    if (!storage)
    {
        error("Unable to allocate new data");
        return nullptr;
    }

    // Allocate enough memory to include length
    byte *dp = leb128(storage, len);
    memcpy(dp, data, len);
    return storage;
}


tok_t runtime::tok(utf8 text, size_t len, bool clone)
// ----------------------------------------------------------------------------
//   Hash the input token based on number of bits in tokens
// ----------------------------------------------------------------------------
{
    for (tok_t t = 0; t < ntokens; t++)
    {
        // Check if we reached an empty slot
        byte_p  &slot     = tokens[t];
        byte_p   existing = slot;
        if (!existing)
        {
            if (utf8 storage = clone ? utf8(allocate(len, byte_p(text))) : text)
            {
                slot = byte_p(storage);
                return t;
            }
            error("Unable to allocate token");
            return 0;
        }

        // Check if that slot matches
        size_t elen = leb128<size_t>(existing);
        if (elen == len && memcmp(existing, text, len) == 0)
            return t;
    }

    // We need some additional room
    size_t nt = ntokens;
    if (utf8 *newtoks = (utf8 *) retoks(ntokens + itokens))
    {
        if (utf8 storage = clone ? utf8(allocate(len, byte_p(text))) : text)
        {
            *newtoks = storage;
            return nt;
        }
        error("Unable to allocate additional token");
    }
    else
    {
        error("Unable to grow tokens table");
    }
    return 0;
}
