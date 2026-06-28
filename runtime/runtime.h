#ifndef RUNTIME_H
#define RUNTIME_H
// ****************************************************************************
//  runtime.h                                                      xlr8 project
// ****************************************************************************
//
//   File Description:
//
//     The runtime for XLR8 represents the execution environment that is
//     necessary to run XLR8 bytecode.
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

#include "leb128.h"
#include "types.h"

#include <cassert>
#include <map>
#include <cstring>


struct runtime
// ----------------------------------------------------------------------------
//  The execution runtime
// ----------------------------------------------------------------------------
{
    // Constructor and destructor
    runtime(size_t sz);
    runtime(const runtime &rt);
    ~runtime();

    const uint regs_per_double = sizeof(double) / sizeof(value_t);


    // ========================================================================
    //
    //   Data tyeps
    //
    // ========================================================================

    // Evaluator function for each opcode, return true to keep going
    typedef bool (*run_fn)(runtime &rt);

    enum op_t : byte
    // ------------------------------------------------------------------------
    //   Represent all the opcodes
    // ------------------------------------------------------------------------
    {
#define OP(op, code) op_##op,
#include "opcodes.tbl"
        OP_COUNT
    };


    // ========================================================================
    //
    //   Runtime helpers
    //
    // ========================================================================

    template <typename Int = size_t>
    Int arg(byte_p &pc)
    // ------------------------------------------------------------------------
    //   Read next signed or unsigned argument and skip it
    // ------------------------------------------------------------------------
    {
        return leb128<Int>(pc);
    }


    bool run(byte_p &pc,
             std::istream *in, std::ostream *out, std::ostream *err);
    // ------------------------------------------------------------------------
    //   Run all instructions at current code pointer
    // ------------------------------------------------------------------------


    byte *allocate(size_t sz, byte_p init);
    byte *allocate(size_t sz)
    // ------------------------------------------------------------------------
    //   Allocate bytes on the heap
    // ------------------------------------------------------------------------
    {
        if (heap + sz >= (byte_p) stack)
        {
            error("No free space on the heap to allocate memory");
            return nullptr;
        }
        byte *result = (byte *) heap;
        heap += sz;
        return result;
    }


    bool free(size_t sz)
    // ------------------------------------------------------------------------
    //   Free bytes from the heap
    // ------------------------------------------------------------------------
    {
        if (heap - sz <= memory)
        {
            error("Attempting to free unallocated memory");
            return false;
        }
        heap -= sz;
        return true;
    }


    bool args(size_t count, cstring msg)
    // ------------------------------------------------------------------------
    //   Check if we can take count args
    // ------------------------------------------------------------------------
    {
        if (stack + count > histack)
        {
            error(msg);
            return false;
        }
        return true;
    }


    bool push(value_t value)
    // ------------------------------------------------------------------------
    //   Push an value_t sized value on the stack
    // ------------------------------------------------------------------------
    {
        if (stack - 1 <= (stack_p) heap)
        {
            error("Out of stack space");
            return false;
        }
        *--stack = value;
        return true;
    }


    value_t pop()
    // ------------------------------------------------------------------------
    //   Pop an value_t sized value from the stack
    // ------------------------------------------------------------------------
    {
        if (stack + 1 > histack)
        {
            error("Popping outside of stack");
            return 0;
        }
        return *stack++;
    }


    bool drop(size_t n = 1)
    // ------------------------------------------------------------------------
    //   Pop an value_t sized value from the stack
    // ------------------------------------------------------------------------
    {
        if (stack + n > histack)
        {
            error("Dropping too many entries from stack");
            return false;
        }
        stack += n;
        return true;
    }


    bool swap(size_t n = 1)
    // ------------------------------------------------------------------------
    //   Swap element at index n with top of stack
    // ------------------------------------------------------------------------
    {
        if (stack + n + 1 > histack)
        {
            error("Swapping element outside of stack");
            return false;
        }
        std::swap(stack[0], stack[n]);
        return true;
    }


    bool dup(size_t n = 1)
    // ------------------------------------------------------------------------
    //   Duplicate n elements from the stack
    // ------------------------------------------------------------------------
    {
        if (stack - n  <= (stack_p) heap)
        {
            error("Duplicating items on a full stack");
            return false;
        }
        stack -= n;
        memcpy(stack, stack + n, n * sizeof(*stack));
        return true;
    }


    value_t *top()
    // ------------------------------------------------------------------------
    //   Return top of stack in writable form
    // ------------------------------------------------------------------------
    {
        if (stack + 1 > histack)
        {
            error("Can't return top of empty stack");
            return nullptr;
        }
        return (value_t *) stack;
    }


    value_t *at(size_t index)
    // ------------------------------------------------------------------------
    //   Return stack pointer at given index
    // ------------------------------------------------------------------------
    {
        if (stack + index < histack)
            return stack + index;
        error("Stack index is out of range");
        return nullptr;
    }


    byte_p dispatch(byte_p pc, value_t ref)
    // ------------------------------------------------------------------------
    //   Dispatch branches
    // ------------------------------------------------------------------------
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
        size_t ncases = arg<size_t>(pc);
        byte_p target  = nullptr;
        for (size_t n = 0; n < ncases; n++)
        {
            value_t test = arg<value_t>(pc);
            broff_t offs = arg<broff_t>(pc);
            if (ref >= test)
                target = pc + offs;
        }
        if (target)
            pc = target;
        return pc;
    }


    value_t *enter(size_t locals, byte flags = 0)
    // ------------------------------------------------------------------------
    //    Set the frame pointer at the current locals
    // ------------------------------------------------------------------------
    {
        bool   saveheap = flags & 1;
        bool   savetoks = flags & 2;
        size_t delta    = 1 + saveheap + savetoks;

        if (stack > frame)
        {
            error("Cannot allocate locals, stack is above frame");
            return nullptr;
        }
        if (stack - locals - delta <= (stack_p) heap)
        {
            error("Out of stack space entering frame");
            return nullptr;
        }
        stack -= locals;

        size_t fsz = frame - stack;
        frame      = stack;
        if (!push(fsz)                          ||
            (saveheap && !push(value_t(heap)))  ||
            (savetoks && !push(ntokens)))
        {
            error("Cannot push frame size - Inernal error");
            return nullptr;
        }
        return frame;
    }


    value_t *leave(bool saveheap = false, bool savetoks = false)
    // ------------------------------------------------------------------------
    //    Free the number of locals and return to previous frame
    // ------------------------------------------------------------------------
    {
        size_t delta = 1 + saveheap + savetoks;
        if (stack + delta >= frame || stack + delta > histack)
        {
            error("Stack is above frame pointer");
            return nullptr;
        }
        if (savetoks)
            ntokens = pop();
        if (saveheap)
            heap = byte_p(pop());

        size_t fsz = pop();
        frame += fsz;
        return frame;
    }


    value_t *local(size_t index)
    // ------------------------------------------------------------------------
    //   Return stack pointer for local at given index
    // ------------------------------------------------------------------------
    {
        if (frame + index < histack)
            return frame + index;
        error("Local index is out of range");
        return nullptr;
    }


    utf8 error()
    // ------------------------------------------------------------------------
    //   Return error message
    // ------------------------------------------------------------------------
    {
        return err;
    }


    runtime &error(utf8 errmsg)
    // ------------------------------------------------------------------------
    //  Set the error message
    // ------------------------------------------------------------------------
    {
        err = errmsg;
        return *this;
    }


    utf8 tok(tok_t t, size_t *len = nullptr)
    // ------------------------------------------------------------------------
    //   Return the text for a given token, and optionally its size
    // ------------------------------------------------------------------------
    {
        if (t >= ntokens)
        {
            if (len)
                *len = 0;
            return nullptr;
        }
        byte_p tp = tokens[t];
        size_t tlen = leb128<size_t>(tp);
        if (len)
            *len = tlen;
        return (utf8) tp;
    }


    tok_t tok(utf8 text, size_t len, bool clone = true);
    tok_t tok(utf8 text)
    // ------------------------------------------------------------------------
    //   Return the token for a given text
    // ------------------------------------------------------------------------
    {
        return tok(text, strlen(text) + 1); // Include trailing 0 of C strings
    }

    tok_t tok(text t)
    // ------------------------------------------------------------------------
    //   Return the token for a given text
    // ------------------------------------------------------------------------
    {
        return tok(t.data(), t.size()); // Include trailing 0 of C strings
    }


public:
    // ========================================================================
    //
    //   Data for the runtime
    //
    // ========================================================================

    using callmap = std::map<tok_t, tok_t>;
    callmap             defined;

    byte_p              memory;         // Memory allocated for that runtime
    byte_p              heap;           // Top of heap (lo marker for stack)
    stack_p             stack;          // Pointer to current stack
    stack_p             frame;         // Pointer to locals
    stack_p             histack;        // High marker for stack

    byte_p             *tokens;         // Table of tokens
    size_t              ntokens;        // Number of tokens
    size_t              itokens;        // Incremental tokens when running out

    cstring             err;            // Error message
    byte_p              entry;          // code entry point

protected:
    byte **             retoks(size_t size);
    byte **             relabel(size_t size);
};

#endif // RUNTIME_H
