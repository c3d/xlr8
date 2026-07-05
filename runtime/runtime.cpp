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
    : flags(),
      pc(),
      memory(),
      heap(),
      stack(),
      frame(),
      histack(),
      err(),
      entry(),
      tokens(),
      token_by_text(),
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


std::ostream *runtime::iosetup(size_t index, std::ostream *stream)
// ----------------------------------------------------------------------------
//   Setup the given I/O channel
// ----------------------------------------------------------------------------
{
    if (index >= outputs.size())
        outputs.resize(index + 1);
    std::swap(outputs[index], stream);
    return stream;
}


std::istream *runtime::iosetup(size_t index, std::istream *stream)
// ----------------------------------------------------------------------------
//   Setup the given I/O channel
// ----------------------------------------------------------------------------
{
    if (index >= inputs.size())
        inputs.resize(index + 1);
    std::swap(inputs[index], stream);
    return stream;
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

typedef int8_t          i8;
typedef int16_t         i16;
typedef int32_t         i32;
typedef int64_t         i64;
typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef uint64_t        u64;

#define TRC(s,x)
#define uvalue          ((uvalue_t) value)
#define DST             byte d = *pc++                  TRC("D%u", d)
#define SRC             byte s = *pc++                  TRC("S%u", s)
#define XTRA            byte x = *pc++                  TRC("X%u", x)
#define DALL            frame[d]
#define SALL            frame[s]
#define XALL            frame[x]
#define TYPE(T,X)       (*((T *) &(X)))
#define DTYPE(T)        TYPE(T, DALL)
#define STYPE(T)        TYPE(T, SALL)
#define XTYPE(T)        TYPE(T, XALL)
#define OTYPE(T,X,O)    (*((T *) (byte_p(X)+intptr_t(O))))
#define DINT            DTYPE(value_t)
#define SINT            STYPE(value_t)
#define DNAT            DTYPE(uvalue_t)
#define SNAT            STYPE(uvalue_t)
#define DFLT            DTYPE(float)
#define SFLT            STYPE(float)
#define DDBL            DTYPE(double)
#define SDBL            STYPE(double)
#define DRPTR           DTYPE(byte_p)
#define SRPTR           STYPE(byte_p)
#define DWPTR           DTYPE(byte *)
#define SWPTR           STYPE(byte *)
#define PRED            byte pred = *pc++;              TRC("?%u", pred)
#define PMASK           (1ULL<<pred)
#define PCOND(cond)     (flags = (cond) ? (flags | PMASK) : (flags & ~PMASK))
#define PTEST           (flags & PMASK)
#define ARG(type,name)  type name = rt.arg<type>(pc)
#define TOK             ARG(tok_t, t)                   TRC("T%zu", t)
#define JMP_TOK         (pc = byte_p(tok(t)))           TRC("=%p", pc)
#define BR_OFF          byte_p bpc = pc; ARG(broff_t,b) TRC("O%ld", long(b))
#define BRANCH          (pc = bpc + b)                  TRC(">%p", pc)
#define KINT            ARG(value_t, kint)              TRC("I%ld", long(kint))
#define KNAT            ARG(uvalue_t, knat)             TRC("N%lu", (unsigned long)(knat))
#define KSHIFT          byte kshift = *pc++             TRC("#%u", kshift)
#define FLUSH           this->flags = flags;            \
                        this->pc    = pc;               \
                        this->frame = frame
#define RESYNC          flags       = this->flags;      \
                        pc          = this->pc;         \
                        frame       = this->frame;      \
                        args        = arguments()

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
    if (RECORDER_TRACE(runtime) > 1)
        return trace();

    bool          done   = false;
    runtime      &rt     = *this;
    byte_p        pc     = rt.pc;
    stack_p       frame  = rt.frame;
    stack_p       args   = rt.frame;
    std::istream *stdin  = inputs.size()  > 1 ? inputs[0]  : nullptr;
    std::ostream *stdout = outputs.size() > 2 ? outputs[1] : nullptr;
    std::ostream *stderr = outputs.size() > 3 ? outputs[2] : nullptr;
    tok_t         called = 0;
    uint64_t      flags  = 0;

    record(runtime, "Running from pc=%p frame=%p", pc, frame);

    while (!done)
    {
        static_assert(OP_COUNT < 128);
        op_t op = op_t(*pc++);
        switch (op)
        {
#define OP(op, code)    case op_##op: { code; } break;
#include "opcodes.tbl"
        default:
            // Check if we have a user-defined op that matches
            FLUSH; done = !user_operation(op); RESYNC;
            break;
        }
    }

    // Pass state down to next runtime execution if any
    this->pc = pc;
    iosetup(0, stdin);
    iosetup(1, stdout);
    iosetup(2, stderr);

    record(runtime, "Done pc=%p frame=%p %s",
           pc, frame, error() ? error() : "SUCCESS");
    return error() == nullptr;
}



// ============================================================================
//
//   Initialize native and opcode tables
//
// ============================================================================

#define NATIVE(op, code)                      \
    static bool xlr8_native_##op(runtime &rt) \
    {                                         \
        byte_p   &pc    = rt.pc;              \
        uint64_t &flags = rt.flags;           \
        stack_p  &frame = rt.frame;           \
        bool      done  = false;              \
        (void) pc;                            \
        (void) flags;                         \
        (void) frame;                         \
        (void) done;                          \
        code;                                 \
        return !done;                         \
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
//   Return existing token or intern a new one
// ----------------------------------------------------------------------------
{
    std::string_view key(text, len);
    if (auto it = token_by_text.find(key); it != token_by_text.end())
        return it->second;

    byte_p storage = clone ? byte_p(allocate(len, byte_p(text))) : byte_p(text);
    if (!storage)
    {
        error("Unable to allocate new token");
        record(runtime_error, "Unable to allocate %zu bytes for token", len);
        return 0;
    }

    tok_t id = tokens.size();
    tokens.push_back(storage);

    byte_p tp = storage;
    size_t elen = leb128<size_t>(tp);
    token_by_text.emplace(std::string_view((char *) tp, elen), id);
    return id;
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



// ============================================================================
//
//   Main tracing loop
//
// ============================================================================

#undef TRC
#define TRC(format, arg)                                                \
    ;                                                                   \
    toffs += snprintf(tbuffer + toffs, tsize - toffs, " " format, arg)


bool runtime::trace()
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
    uint64_t      flags  = 0;

    char          tbuffer[256];
    const size_t  tsize = sizeof(tbuffer);
    static const cstring op_names[] = {
#define OP(op, code) #op,
#include "opcodes.tbl"
    };

    record(runtime, "Tracing from pc=%p frame=%p", pc, frame);
    while (!done)
    {
        static_assert(OP_COUNT < 128);
        byte_p ppc = pc;
        op_t op = op_t(*pc++);
        size_t toffs = 0;
        cstring opname = op < OP_COUNT     ? op_names[op]
                         : user_opcode(op) ? "USER"
                                           : "INVALID";
        record(runtime, "%p: %02X %6s", ppc, op, opname);
        switch (op)
        {
#define OP(op, code)    case op_##op: { code; } break;
#include "opcodes.tbl"
        default:
            // Check if we have a user-defined op that matches
            FLUSH; done = !user_operation(op); RESYNC;
            break;
        }

        char obuf[32];
        snprintf(obuf, sizeof(obuf), "%s%ld",
                 pc >= ppc ? "+" : "", long(pc - ppc));
        record(runtime, "%p: %02X %6s %6s %s", pc, op, opname, obuf, tbuffer);
    }

    // Pass state down to next runtime execution if any
    this->pc = pc;
    iosetup(0, stdin);
    iosetup(1, stdout);
    iosetup(2, stderr);

    record(runtime, "Done pc=%p frame=%p %s",
           pc, frame, error() ? error() : "SUCCESS");
    return error() == nullptr;
}
