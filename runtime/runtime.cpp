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
    : pc(), ppc(),
      opcodes(), nopcodes(),
      memory(), heap(), stack(), frame(), histack(),
      tokens(nullptr), ntokens(0), itokens(32),
      err()
{
    // Allocate space for the heap and stack
    memory = (byte_p) std::malloc(sz);
    if (!memory)
        error("Not enough memory for heap");
    heap = memory;
    stack = (stack_p) (memory + sz);
    frame = stack;
    histack = stack;
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
//   Builtin runtime opcodes
//
// ============================================================================

// Implmentation for an operation
#define IMPL(op)  bool runtime::op_##op(runtime &rt)

IMPL(end)
// ----------------------------------------------------------------------------
//   Terminating opcode, returning to caller
// ----------------------------------------------------------------------------
{
    return false;
}


IMPL(run)
// ----------------------------------------------------------------------------
//   Run the opcodes at the current pc
// ----------------------------------------------------------------------------
{
    return rt.run();
}


IMPL(get)
// ----------------------------------------------------------------------------
//  Get a stack item and push it on the stack
// ----------------------------------------------------------------------------
{
    size_t index = rt.arg();
    if (intptr_t *s = rt.at(index))
        return rt.push(*s);
    return false;
}


IMPL(set)
// ----------------------------------------------------------------------------
//   Set a stack item from top of stack
// ----------------------------------------------------------------------------
{
    op_t index = rt.arg();
    if (intptr_t *s = rt.at(index))
    {
        *s = rt.pop();
        return !rt.error();
    }
    return false;
}


IMPL(rcl)
// ----------------------------------------------------------------------------
//  Get a local and push it on the stack
// ----------------------------------------------------------------------------
{
    op_t index = rt.arg();
    if (intptr_t *s = rt.local(index))
        return rt.push(*s);
    return false;
}


IMPL(sto)
// ----------------------------------------------------------------------------
//   Set a local variable from top of stack
// ----------------------------------------------------------------------------
{
    op_t index = rt.arg();
    if (intptr_t *s = rt.local(index))
    {
        *s = rt.pop();
        return !rt.error();
    }
    return false;
}


IMPL(call)
// ----------------------------------------------------------------------------
//   Call - This is what is used for an opcode extension
// ----------------------------------------------------------------------------
{
    return rt.call();
}


IMPL(enter)
// ----------------------------------------------------------------------------
//   Enter a frame
// ----------------------------------------------------------------------------
{
    return rt.enter() != nullptr;
}


IMPL(leave)
// ----------------------------------------------------------------------------
//   Leave the current frame
// ----------------------------------------------------------------------------
{
    return rt.leave() != nullptr;
}


IMPL(ppc)
// ----------------------------------------------------------------------------
//   Push current PC on the stack
// ----------------------------------------------------------------------------
{
    return rt.push(intptr_t(rt.pc));
}


IMPL(jmp)
// ----------------------------------------------------------------------------
//   Jump to address on the stack
// ----------------------------------------------------------------------------
{
    if (intptr_t pc = rt.pop())
    {
        rt.pc = byte_p(pc);
        return true;
    }
    return false;
}


IMPL(dispatch)
// ----------------------------------------------------------------------------
//   Dispatch to one of multiple branches
// ----------------------------------------------------------------------------
//   The first argument gives number of cases
//   Then for each case, reference value and branch offset.
//   As soon as the value is greater or equal to the test, branch is taken
//   For example [dispatch 3 10 lbl1 20 lbl2 40 lbl3] will:
//   - branch to lbl1 if the value is >= 10 and < 20
//   - branch to lbl2 if the value is >= 20 and < 40
//   - branch to lbl3 for values over 40
//   - skip to next instruction otherwise
//   You can skip to the next instruction with an offset of 0
{
    size_t   ncases = rt.arg<size_t>();
    intptr_t delta  = 0;
    intptr_t ref    = rt.pop();
    if (rt.error())
        return false;
    for (size_t n = 0; n < ncases; n++)
    {
        intptr_t test = rt.arg<intptr_t>();
        intptr_t offs = rt.arg<intptr_t>();
        if (ref >= test)
            delta = offs;
    }
    rt.pc += delta;
    return true;
}


#define BRANCH(op, cond)                        \
IMPL(op)                                        \
{                                               \
    intptr_t delta = rt.arg<intptr_t>();        \
    if (cond)                                   \
        rt.pc += delta;                         \
    return !rt.error();                         \
}


BRANCH(bra, true);              // Unconditional branch
BRANCH(beq, rt.pop() == 0)      // Branch if value == 0
BRANCH(bne, rt.pop() != 0)      // Branch if value != 0
BRANCH(blt, rt.pop() <  0)      // Branch if value <  0
BRANCH(bgt, rt.pop() >  0)      // Branch if value >  0
BRANCH(ble, rt.pop() <= 0)      // Branch if value <= 0
BRANCH(bge, rt.pop() >= 0)      // Branch if value >= 0


#define BINARY(op, code)                        \
IMPL(op)                                        \
{                                               \
    intptr_t x = rt.pop();                      \
    intptr_t y = rt.pop();                      \
    if (rt.error())                             \
        return false;                           \
    intptr_t r = code;                          \
    return rt.push(r);                          \
}

BINARY(add, y+x)
BINARY(sub, y-x)
BINARY(rsub, x-y)
BINARY(mul, y*x)
BINARY(div, y/x)
BINARY(mod, y%x)
BINARY(mulu, intptr_t(uintptr_t(y)*uintptr_t(x)))
BINARY(divu, intptr_t(uintptr_t(y)/uintptr_t(x)))
BINARY(modu, intptr_t(uintptr_t(y)%uintptr_t(x)))
BINARY(and, y&x)
BINARY(or,  y|x)
BINARY(xor, y^x)
BINARY(shl, y<<x)
BINARY(shr, y >> x)
BINARY(shru, intptr_t(uintptr_t(y) >> uintptr_t(x)))

#define UNARY(op, code)                         \
    IMPL(op)                                    \
    {                                           \
        if (intptr_t *s = rt.top())             \
        {                                       \
            intptr_t x = *s;                    \
            intptr_t r = code;                  \
            *s         = r;                     \
            return true;                        \
        }                                       \
        return false;                           \
}

UNARY(neg, -x)
UNARY(not, ~x)


IMPL(int)
// ----------------------------------------------------------------------------
//  Push a signed integer constant
// ----------------------------------------------------------------------------
{
    intptr_t ival = rt.arg<intptr_t>();
    return rt.push(ival);
}


IMPL(uint)
// ----------------------------------------------------------------------------
//  Push an unsigned integer constant
// ----------------------------------------------------------------------------
{
    uintptr_t ival = rt.arg<uintptr_t>();
    return rt.push(uintptr_t(ival));
}


IMPL(str)
// ----------------------------------------------------------------------------
//  Push a tokenized string
// ----------------------------------------------------------------------------
{
    utf8   str = utf8(rt.pc);
    size_t len = rt.arg<size_t>();
    tok_t  tok = rt.tok(str, len, false);
    rt.pc += len;
    return rt.push(intptr_t(tok));
}


IMPL(dup)
// ----------------------------------------------------------------------------
//   Duplicate top of stack
// ----------------------------------------------------------------------------
{
    return rt.dup();
}


IMPL(swap)
// ----------------------------------------------------------------------------
//   Swap first two levels of stack
// ----------------------------------------------------------------------------
{
    return rt.swap();
}


IMPL(drop)
// ----------------------------------------------------------------------------
//   Drop top level of stack
// ----------------------------------------------------------------------------
{
    return rt.drop();
}


IMPL(dupn)
// ----------------------------------------------------------------------------
//   Duplicate the N top levels of the stack
// ----------------------------------------------------------------------------
{
    size_t sz = rt.arg<size_t>();
    return rt.dup(sz);
}


IMPL(swapn)
// ----------------------------------------------------------------------------
//   Swap top and nth level of the stack
// ----------------------------------------------------------------------------
{
    size_t sz = rt.arg<size_t>();
    return rt.swap(sz);
}


IMPL(dropn)
// ----------------------------------------------------------------------------
//   Drop the N top levels of the stack
// ----------------------------------------------------------------------------
{
    size_t sz = rt.arg<size_t>();
    return rt.drop(sz);
}


IMPL(dupi)
// ----------------------------------------------------------------------------
//   Duplicate the N top levels of the stack (indirect from stack)
// ----------------------------------------------------------------------------
{
    size_t sz = size_t(rt.pop());
    return rt.dup(sz);
}


IMPL(swapi)
// ----------------------------------------------------------------------------
//   Swap top and nth level of the stack (indirect from stack)
// ----------------------------------------------------------------------------
{
    size_t sz = size_t(rt.pop());
    return rt.swap(sz);
}


IMPL(dropi)
// ----------------------------------------------------------------------------
//   Drop the N top levels of the stack (indirect from stack)
// ----------------------------------------------------------------------------
{
    size_t sz = size_t(rt.pop());
    return rt.drop(sz);
}


IMPL(err)
// ----------------------------------------------------------------------------
//   Display the error message on stderr
// ----------------------------------------------------------------------------
{
    if (utf8 msg = rt.error())
        std::cerr << msg << std::endl;
    return true;
}


IMPL(clear)
// ----------------------------------------------------------------------------
//   Clear the error message
// ----------------------------------------------------------------------------
{
    rt.error(nullptr);
    return true;
}


IMPL(trap)
// ----------------------------------------------------------------------------
//   Branch to target if there is an error
// ----------------------------------------------------------------------------
{
    intptr_t delta = rt.arg<intptr_t>();
    if (rt.error())
        rt.pc += delta;
    return true;
}


IMPL(eof)
// ----------------------------------------------------------------------------
//   Check if input is EOF
// ----------------------------------------------------------------------------
{
    return rt.push(intptr_t(std::cin.good()));;
}



IMPL(inpc)
// ----------------------------------------------------------------------------
//   Read one character and put it on the stack
// ----------------------------------------------------------------------------
{
    wchar_t c;
    if (std::wcin >> c)
        return rt.push(intptr_t(c));
    else
        rt.error("Character input error");
    return false;
}


IMPL(inpi)
// ----------------------------------------------------------------------------
//   Read one integer and put it on the stack
// ----------------------------------------------------------------------------
{
    intptr_t i;
    if (std::cin >> i)
        return rt.push(intptr_t(i));
    return false;
}


IMPL(inpu)
// ----------------------------------------------------------------------------
//   Read one unsigned integer and put it on the stack
// ----------------------------------------------------------------------------
{
    uintptr_t i;
    if (std::cin >> i)
        return rt.push(intptr_t(i));
    return false;
}


IMPL(inpt)
// ----------------------------------------------------------------------------
//   Read one integer and put it on the stack
// ----------------------------------------------------------------------------
{
    std::string s;
    if (std::cin >> s)
    {
        tok_t tok = rt.tok(s.data(), s.length());
        return rt.push(tok);
    }
    return false;
}


IMPL(outc)
// ----------------------------------------------------------------------------
//   Read one character and put it on the stack
// ----------------------------------------------------------------------------
{
    char32_t c = rt.pop();
    if (!rt.error())
        return bool(std::cout << c);
    return false;
}


IMPL(outi)
// ----------------------------------------------------------------------------
//   Read one integer and put it on the stack
// ----------------------------------------------------------------------------
{
    intptr_t i = rt.pop();
    if (std::cin >> i)
        return rt.push(intptr_t(i));
    return false;
}


IMPL(outu)
// ----------------------------------------------------------------------------
//   Read one unsigned integer and put it on the stack
// ----------------------------------------------------------------------------
{
    uintptr_t i;
    if (std::cin >> i)
        return rt.push(intptr_t(i));
    return false;
}


IMPL(outt)
// ----------------------------------------------------------------------------
//   Read one integer and put it on the stack
// ----------------------------------------------------------------------------
{
    std::string s;
    if (std::cin >> s)
    {
        tok_t tok = rt.tok(s.data(), s.length());
        return rt.push(tok);
    }
    return false;
}


// ============================================================================
//
//   Support routines
//
// ============================================================================

bool runtime::use(text name)
// ----------------------------------------------------------------------------
//  Use a name
// ----------------------------------------------------------------------------
{
    if (name == "all")
    {
#define OP(n)                                   \
        if (!use(#n))                           \
            return false;
#include "runtime.tbl"
        return true;
    }

#define OP(n)                                   \
    if (name == #n)                             \
    {                                           \
        tok_t t = tok(name);                    \
        use(op_t(t), op_##n);                   \
        return true;                            \
    }
#include "runtime.tbl"

    error("Invalid built-in opcode");
    return false;
}


bool runtime::use(op_t op, run_fn run)
// ----------------------------------------------------------------------------
//   Set the given opcode for evaluation
// ----------------------------------------------------------------------------
{
    if (op >= nopcodes)
    {
        size_t nitems = op + 1;
        size_t newsize = nitems * sizeof(run_fn);
        run_fn *resized = (run_fn *) realloc(opcodes, newsize);
        if (!resized)
        {
            error("Failed to grow the opcodes table");
            return false;
        }
        for (size_t o = nopcodes; o < nitems; o++)
            resized[o] = nullptr;
        opcodes = resized;
        nopcodes = nitems;
    }
    opcodes[op] = run;
    return true;
}


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
