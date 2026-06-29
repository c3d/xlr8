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
#include "recorder.h"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <cmath>


RECORDER(runtime,       16, "Runtime");
RECORDER(runtime_error, 16, "Runtime errors");


runtime::runtime(size_t sz)
    // ----------------------------------------------------------------------------
    //   Initialize the runtime with the given memory size
    // ----------------------------------------------------------------------------
    : pc(),
      memory(),
      heap(),
      stack(),
      frame(),
      histack(),
      err(),
      entry(),
      tokens(),
      defined(),
      inputs(),
      outputs()
{
    // Allocate space for the heap and stack
    memory = (byte_p) std::malloc(sz);
    if (!memory)
    {
        error("Not enough memory for heap");
        record(runtime_error, "Not enough memory for %zu bytes", sz);
    }
    heap = memory;
    stack = (stack_p) (memory + sz);
    frame = stack;
    histack = stack;

#define OP(op, code)            tok(text(#op));
#include "opcodes.tbl"
}


runtime::~runtime()
// ----------------------------------------------------------------------------
//   Free resources allocated by this runtime
// ----------------------------------------------------------------------------
{
    std::free((void *) memory);
}


bool runtime::iosetup(size_t index, std::ostream &stream)
// ----------------------------------------------------------------------------
//   Setup the given I/O channel
// ----------------------------------------------------------------------------
{
    if (index >= outputs.size())
        outputs.resize(index + 1);
    outputs[index] = &stream;
    return true;
}


bool runtime::iosetup(size_t index, std::istream &stream)
// ----------------------------------------------------------------------------
//   Setup the given I/O channel
// ----------------------------------------------------------------------------
{
    if (index >= inputs.size())
        inputs.resize(index + 1);
    inputs[index] = &stream;
    return true;
}



// ============================================================================
//
//   I/O helpers for opcode table
//
// ============================================================================

template<typename T> inline T input(std::istream &input)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    T v;
    input >> v;
    return v;
}


template<typename T> inline bool output(std::ostream &output, T v)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    return (output << v).good();
}


inline tok_t input_text(runtime &rt, std::istream &input)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    text v;
    input >> v;
    return rt.tok(v);
}


bool output_text(runtime &rt, std::ostream &output, value_t tok)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    size_t len  = 0;
    utf8   txt  = rt.tok(tok, &len);
    text   tval = text(txt, len);
    return (output << tval).good();
}


inline bool output_newline(std::ostream &output)
// ----------------------------------------------------------------------------
//   Return the next value of given type
// ----------------------------------------------------------------------------
{
    return (output << '\n').good();
}



// ============================================================================
//
//   Main runtime loop
//
// ============================================================================

#define uframe          ((uvalue_t *) frame)
#define uvalue          ((uvalue_t) value)
#define DST             byte d = *pc++
#define SRC             byte s = *pc++
#define DINT            (frame[d])
#define SINT            (frame[s])
#define DNAT            (uframe[d])
#define SNAT            (uframe[s])
#define DTYPE(T)        (*(T *) &frame[d])
#define STYPE(T)        (*(T *)&frame[s])
#define DFLT            DTYPE(float)
#define SFLT            STYPE(float)
#define DDBL            DTYPE(double)
#define SDBL            STYPE(double)
#define TOK             tok_t t = arg<tok_t>(pc)
#define JMP_TOK         (pc = byte_p(tok(t)))
#define BR_OFF          byte_p bpc = pc; broff_t b = arg<broff_t>(pc)
#define BRANCH          (pc = bpc + b)
#define KINT            value_t kint = arg<value_t>(pc)
#define KNAT            uvalue_t knat = arg<uvalue_t>(pc)
#define KSHIFT          byte kshift = *pc++


bool runtime::run()
// ----------------------------------------------------------------------------
//    The main runtime loop
// ----------------------------------------------------------------------------
{
    bool           done   = false;
    bool           ok     = true;
    byte_p         pc     = this->pc;
    std::istream *stdin  = inputs.size()  > 1 ?  inputs[0] : nullptr;
    std::ostream *stdout = outputs.size() > 2 ? outputs[1] : nullptr;
    std::ostream *stderr = outputs.size() > 3 ? outputs[2] : nullptr;
    while (!done)
    {
        static_assert(OP_COUNT < 128);
        op_t op = op_t(*pc++);
        switch (op)
        {

#define OP(op, code)    case op_##op: { code; } break;
#include "opcodes.tbl"
#undef uframe
        default:
            error("Invalid opcode");
            done = true;
            ok   = false;
            break;
        }
    }

    // Pass state down to next runtime execution if any
    this->pc = pc;
    if (stdin)
        iosetup(0, *stdin);
    if (stdout)
        iosetup(1, *stdout);
    if (stderr)
        iosetup(2, *stderr);
    return ok;
}



// ============================================================================
//
//   Support routines
//
// ============================================================================

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
    size_t ntokens = tokens.size();
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
            record(runtime_error, "Unable to allocate token %zu bytes", len);
            return 0;
        }

        // Check if that slot matches
        size_t elen = leb128<size_t>(existing);
        if (elen == len && memcmp(existing, text, len) == 0)
            return t;
    }

    // Add one ore token
    tok_t result = tokens.size();
    if (utf8 storage = clone ? utf8(allocate(len, byte_p(text))) : text)
    {
        tokens.push_back(byte_p(storage));
        return result;
    }

    error("Unable to allocate new token");
    record(runtime_error,
           "Unable to allocate %zu bytes for togen %u", len, result);

    return 0;
}
