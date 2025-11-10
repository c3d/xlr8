#ifndef RUNTIME_H
#  define RUNTIME_H
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

struct runtime;
typedef const char *cstring;
typedef const char *utf8;
typedef byte_p     object_p;
typedef bool (*run_fn)(runtime &rt);
typedef size_t op_t;


template <typename value_type>
struct save
// ----------------------------------------------------------------------------
//   Save a value and reset it to what it was on scope exit
// ----------------------------------------------------------------------------
{
    save(value_type &ref, value_type value): ref(ref), saved(ref)
    {
        ref = value;
    }
    ~save()
    {
        ref = saved;
    }
    value_type  &ref;
    value_type  saved;
};


struct runtime
// ----------------------------------------------------------------------------
//  The execution runtime
// ----------------------------------------------------------------------------
{
    // Constructor and destructor
    runtime(byte_p  start,
            size_t  memsize = 32 * 1024,
            size_t  opcodes = 0,
            run_fn *evaluators = nullptr);
    ~runtime();


    // ========================================================================
    //
    //   Opcode evaluation
    //
    // ========================================================================

    bool next()
    // ------------------------------------------------------------------------
    //    Execute the next instruction
    // ------------------------------------------------------------------------
    {
        ppc = pc;
        op = leb128<op_t>(pc) >> 1;
        return op < opcodes  ? evaluators[op](*this) : false;
    }


    bool run()
    // ------------------------------------------------------------------------
    //   Run until we reach a successful exit
    // ------------------------------------------------------------------------
    {
        while (next());
        return !error_message;
    }


    bool run(byte_p npc)
    // ------------------------------------------------------------------------
    //   Run until we reach a successful exit
    // ------------------------------------------------------------------------
    {
        pc = npc;
        return run();
    }



    // ========================================================================
    //
    //   Stack and root pointer growths
    //
    // ========================================================================

    bool grow(uint arena, size_t amount)
    // ------------------------------------------------------------------------
    //   Grow the given arena by the given number of pointers
    // ------------------------------------------------------------------------
    {
        size_t sz = amount * sizeof(object_p);
        if (available(sz) < sz)
            return false;
        for (uint a = 0; a <= arena; a++)
            root[a] -= amount;
        object_p *first = root[0];
        object_p *last = root[arena];
        for (object_p *p = first; p < last; p++)
            p[0] = p[amount];
        return true;
    }


    bool shrink(uint arena, size_t amount)
    // ------------------------------------------------------------------------
    //   Shrink the given arena by the given number of pointers
    // ------------------------------------------------------------------------
    {
        for (uint a = 0; a <= arena; a++)
            root[a] += amount;
        object_p *first = root[0];
        object_p *last = root[arena];
        for (object_p *p = last; p >= first; p--)
            p[amount] = p[0];
        return true;
    }


    bool adjust(uint arena, ptrdiff_t delta)
    // ------------------------------------------------------------------------
    //   Adjust size of arena up (grow) or down (shrink)
    // ------------------------------------------------------------------------
    {
        if (delta < 0)
            return shrink(arena, size_t(-delta));
        if (delta > 0)
            return grow(arena, size_t(delta));
        return true;
    }


    bool available(size_t sz)
    // ------------------------------------------------------------------------
    //  Check if the available size for dynamic allocation
    // ------------------------------------------------------------------------
    {
        size_t avail = byte_p(root[0]) - byte_p(heap);
        if (avail < sz)
        {
            gc();
            avail = byte_p(root[0]) - byte_p(heap);
            if (avail < sz)
            {
                out_of_memory_error();
                return false;
            }
        }
        return true;
    }


    size_t gc();
    // ------------------------------------------------------------------------
    //   Run garbage collection
    // ------------------------------------------------------------------------



    // ========================================================================
    //
    //   Error handling
    //
    // ========================================================================

    runtime &error(utf8 message)
    // ------------------------------------------------------------------------
    //   Set the error message
    // ------------------------------------------------------------------------
    {
        error_message = message;
        return *this;
    }


    utf8 error()
    // ------------------------------------------------------------------------
    //   Get the error message (as currently displayed)
    // ------------------------------------------------------------------------
    {
        return error_message;
    }


    runtime &source(utf8 spos, size_t len = 0)
    // ------------------------------------------------------------------------
    //   Set the source location for the current error
    // ------------------------------------------------------------------------
    {
        error_source = spos;
        error_source_length = len;
        return *this;
    }


    utf8 source()
    // ------------------------------------------------------------------------
    //   Get the pointer to the problem
    // ------------------------------------------------------------------------
    {
        return error_source;
    }


    size_t source_length()
    // ------------------------------------------------------------------------
    //   Get the pointer to the problem
    // ------------------------------------------------------------------------
    {
        return error_source_length;
    }


#define ERROR(name, msg)        runtime &name##_error();
#include "errors.tbl"



public:
    // ========================================================================
    //
    //   Data for the runtime
    //
    // ========================================================================

    // Opcodes
    run_fn       *evaluators;           // Evaluators for each opcode
    size_t        opcodes;              // Number of valid opcodes
    byte_p        pc;                   // Program counter
    byte_p        ppc;                  // Previous program counter
    op_t          op;                   // Current opcde
    utf8          error_message;        // Current error message
    utf8          error_source;         // Source code for error
    size_t        error_source_length;  // Length of error for source

    // Base of the runtime memory (heap)
    object_p      lomem;                // First byte of runtime memory
    object_p      globals;              // End of global objects
    object_p      heap;                 // End of heap
    object_p      himem;                // First byte past runtime memory

    // Root pointers (growing from top of memory down)
    enum { STACK, LOCALS, CALLSTACK, HIGHMEM, NUM_ROOTS };
    object_p *    root[NUM_ROOTS];
};

#endif // RUNTIME_H
