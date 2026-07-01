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
#include "scanner.h"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <cmath>


RECORDER(runtime,       16, "Runtime");
RECORDER(runtime_error, 16, "Runtime errors");


runtime::runtime(size_t sz, size_t argc, char *argv[])
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
      outputs(),
      cmdline(argv, argv + argc)
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
    setup();
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
#define TOK             tok_t t = rt.arg<tok_t>(pc)
#define JMP_TOK         (pc = byte_p(tok(t)))
#define BR_OFF          byte_p bpc = pc; broff_t b = rt.arg<broff_t>(pc)
#define BRANCH          (pc = bpc + b)
#define KINT            value_t kint = rt.arg<value_t>(pc)
#define KNAT            uvalue_t knat = rt.arg<uvalue_t>(pc)
#define KSHIFT          byte kshift = *pc++
#define FLUSH           this->pc = pc; this->frame = frame
#define RESYNC          pc = this->pc; frame = this->frame; args = arguments()

// For native code
#define NATIVE_PC       byte_p &pc = rt.pc
#define NATIVE_FRAME    stack_p &frame = rt.frame
#define NATIVE_ARGS     stack_p args = rt.arguments()
#define ISTREAM         TOK; std::istream *in  = rt.istream(t)
#define OSTREAM         TOK; std::ostream *out = rt.ostream(t)
#define ISTREAM_RD(t,v) t value; if (in && ((*in) >> value)) v = value
#define OSTREAM_WR(val) (out && ((*out) << (val)))


bool runtime::run()
// ----------------------------------------------------------------------------
//    The main runtime loop
// ----------------------------------------------------------------------------
{
    bool          done   = false;
    runtime      &rt     = *this;
    byte_p        pc     = rt.pc;
    byte_p        pc0    = pc; (void) pc0;
    stack_p       frame  = rt.frame;
    stack_p       args   = rt.frame;
    std::istream *stdin  = inputs.size() > 1 ? inputs[0] : nullptr;
    std::ostream *stdout = outputs.size() > 2 ? outputs[1] : nullptr;
    std::ostream *stderr = outputs.size() > 3 ? outputs[2] : nullptr;
    tok_t         called = 0;

    record(runtime, "Running from pc=%p frame=%p", pc, frame);

    while (!done)
    {
        static_assert(OP_COUNT < 128);
        op_t op = op_t(*pc++);

#ifdef DEBUG
        static const cstring op_names[] = {
#define OP(op, code) #op,
#include "opcodes.tbl"
        };
        record(runtime, "%4u: %p %02X %+s",
               pc - pc0, frame,
               op, op < OP_COUNT ? op_names[op] : "INVALID");
#endif

        switch (op)
        {

#define OP(op, code)    case op_##op: { code; } break;
#include "opcodes.tbl"
        default:
            error("Invalid opcode");
            done = true;
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

    record(runtime, "Done pc=%p frame=%p %s",
           pc, frame, error() ? error() : "SUCCESS");
    return error() == nullptr;
}



// ============================================================================
//
//   Initialize native and opcode tables
//
// ============================================================================

#define NATIVE(op, code)                        \
    static bool xlr8_native_##op(runtime &rt)   \
    {                                           \
        byte_p  &pc    = rt.pc;                 \
        stack_p &frame = rt.frame;              \
        bool     done  = false;                 \
        (void) pc; (void) frame; (void) done;   \
        code;                                   \
        return !done;                           \
    }
#include "native.tbl"


void runtime::setup()
// ----------------------------------------------------------------------------
//   Setup the runtime
// ----------------------------------------------------------------------------
{
    // Initialize the opcode tokens (must come first so that tok == op_##op
#define OP(op, code)            tok(text(#op));
#include "opcodes.tbl"

    // Initialize the native tokens
#define NATIVE(op, code)        { native(tok(text(#op)), xlr8_native_##op); }
#include "native.tbl"
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

    // Add one mxore token
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


bool runtime::resolve(tok_t id, tok_t called, byte_p &pc)
// ----------------------------------------------------------------------------
//   Check if we have resolved the given external
// ----------------------------------------------------------------------------
{
    auto it = defined.find(id);
    if (it != defined.end())
    {
        // Find the actual implementation
        tok_t impl = (*it).second;
        pc = byte_p(tok(impl));

        // Patch the called token to match the implementation, no longer undef
        tokens[called] = tokens[impl];
        return true;
    }
    error("Undefined operation");
    return false;
}
