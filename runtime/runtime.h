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
#include <cstring>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>


struct scanner;


struct runtime
// ----------------------------------------------------------------------------
//  The execution runtime
// ----------------------------------------------------------------------------
{
    // Constructor and destructor
    runtime(size_t sz, size_t argc, char *argv[]);
    runtime(const runtime &rt);
    ~runtime();
    void setup();

    // Regs in the caller that a callee is allowed to clobber
    const uint CALL_CLOBBERED_REGS = 8;



    // ========================================================================
    //
    //   Data types
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


    template <typename Int = size_t>
    Int arg()
    // ------------------------------------------------------------------------
    //   Read an argument from the current PC
    // ------------------------------------------------------------------------
    {
        return leb128<Int>(pc);
    }


    bool run();
    bool run_native(byte_p entry = nullptr);
    bool trace();
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


    stack_p alloca(intptr_t size)
    // ------------------------------------------------------------------------
    //   Allocate (size>0) or free (size<0) the number of bytes, rounded up
    // ------------------------------------------------------------------------
    {
        if (size > 0)
        {
            size = ((size + 7) & ~7LL) / 8;
            if (stack - size <= (stack_p) heap)
            {
                error("Stack allocation below heap");
                return nullptr;
            }
        }
        else
        {
            size = -((-size + 7) & ~7LL) / 8;
            if (stack - size > histack)
            {
                error("Stack deallocation is outside of stack");
                return nullptr;
            }
        }
        stack -= size;
        return stack;
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


    template<typename stack_t = ularge_t>
    bool push(stack_t value)
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


    template<typename stack_t = ularge_t>
    stack_t pop()
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


    template<typename stack_t = value_t>
    stack_t *top()
    // ------------------------------------------------------------------------
    //   Return top of stack in writable form
    // ------------------------------------------------------------------------
    {
        if (stack + 1 > histack)
        {
            error("Can't return top of empty stack");
            return nullptr;
        }
        return (stack_t *) stack;
    }


    template <typename stack_t = value_t>
    stack_t *at(size_t index)
    // ------------------------------------------------------------------------
    //   Return stack pointer at given index
    // ------------------------------------------------------------------------
    {
        if (stack + index < histack)
            return (stack_t *) (stack + index);
        error("Stack index is out of range");
        return nullptr;
    }


    template <typename Compare>
    byte_p dispatch(byte_p pc, value_t ref, Compare compare)
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
            byte_p  bpc  = pc;
            broff_t offs = arg<broff_t>(pc);
            if (compare(ref, test))
                target = bpc + offs;
        }
        if (target)
            pc = target;
        return pc;
    }


    stack_p enter(byte locals)
    // ------------------------------------------------------------------------
    //    Set the frame pointer at the current locals
    // ------------------------------------------------------------------------
    {
        if (stack > frame)
        {
            error("Cannot allocate locals, stack is above frame");
            return nullptr;
        }
        if (stack - locals - 1 <= (stack_p) heap)
        {
            error("Out of stack space entering frame");
            return nullptr;
        }
        stack -= locals;
        size_t finfo = ((frame - stack) << 16) | (byte(flags) << 8) | locals;
        frame = stack;
        if (!push(finfo))
        {
            error("Cannot push frame size - Inernal error");
            return nullptr;
        }
        return frame;
    }


    stack_p leave()
    // ------------------------------------------------------------------------
    //    Free the number of locals and return to previous frame
    // ------------------------------------------------------------------------
    {
        if (stack + 1 > frame)
        {
            error("Stack is above frame pointer");
            return nullptr;
        }
        size_t finfo  = frame[-1];
        size_t locals = byte(finfo);
        flags = byte(finfo >> 8) | ((flags >> 8) << 8);
        stack = frame + locals;
        frame += finfo >> 16;
        return frame;
    }


    stack_p arguments()
    // ------------------------------------------------------------------------
    //   Return address of argugments
    // ------------------------------------------------------------------------
    {
        size_t finfo = frame[-1];
        return frame + (finfo >> 16);
    }


    template <typename stack_t = value_t>
    stack_t *local(size_t index)
    // ------------------------------------------------------------------------
    //   Return stack pointer for local at given index
    // ------------------------------------------------------------------------
    {
        if (frame + index < histack)
            return (stack_t *) (frame + index);
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
        if (t >= tokens.size())
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


    value_t tokaddr(tok_t t)
    // ------------------------------------------------------------------------
    //   Return pointer to token or nullptr
    // ------------------------------------------------------------------------
    {
        return value_t(t < tokens.size() ? tokens[t] : nullptr);
    }


    text as_text(tok_t t)
    // ------------------------------------------------------------------------
    //   Return the text for a given token
    // ------------------------------------------------------------------------
    {
        size_t len = 0;
        utf8 txt = tok(t, &len);
        return text(txt, len);
    }


    tok_t tok(utf8 text, size_t len, bool clone = true);
    tok_t tok(utf8 text)
    // ------------------------------------------------------------------------
    //   Return the token for a given text
    // ------------------------------------------------------------------------
    {
        return tok(text, strlen(text) + 1); // Include trailing 0 of C strings
    }

    tok_t tok(const text &t)
    // ------------------------------------------------------------------------
    //   Return the token for a given text
    // ------------------------------------------------------------------------
    {
        return tok(t.data(), t.size()); // Include trailing 0 of C strings
    }


    bool resolve(tok_t id, tok_t called, byte_p &pc);
    // ------------------------------------------------------------------------
    //   Check if we have resolved an external symbol
    // ------------------------------------------------------------------------



    // ========================================================================
    //
    //   Native and I/O interface
    //
    // ========================================================================

    typedef bool (*native_fn)(runtime &rt);
    void native(tok_t tok, native_fn call)
    // ------------------------------------------------------------------------
    //   Enter a native interface in the runtime
    // ------------------------------------------------------------------------
    {
        natives[tok] = call;
    }


    bool native(tok_t tok)
    // ------------------------------------------------------------------------
    //   Call a native function
    // ------------------------------------------------------------------------
    {
        native_fn callee = natives[tok];
        if (!callee)
        {
            error("Nonexistent native call");
            return false;
        }
        return callee(*this);
    }


    native_fn user_opcode(byte op, native_fn fn = nullptr)
    // ------------------------------------------------------------------------
    //   Set / replace a user-defined opcode
    // ------------------------------------------------------------------------
    {
        op -= 128;
        native_fn old = nullptr;
        if (op < user_ops.size())
          old = user_ops[op];
        else if (fn)
            user_ops.resize(op + 1);
        if (fn)
            user_ops[op] = fn;
        return old;
    }


    bool user_operation(byte op)
    // ------------------------------------------------------------------------
    //   If a user-defined opcode exists, execute it, otherwise invalid
    // ------------------------------------------------------------------------
    {
        if (byte(op - 128) < user_ops.size())
        {
            native_fn user = user_ops[byte(op - 128)];
            return user(*this);
        }
        error("Invalid opcode");
        return false;
    }


    void skip_user_args(byte op, byte_p &pc)
    // ------------------------------------------------------------------------
    //   Advance pc past inline payload of a registered user opcode
    // ------------------------------------------------------------------------
    {
        if (byte(op - 128) < user_ops.size() && user_ops[byte(op - 128)])
        {
            while (*pc)
                pc++;
            if (*pc == 0)
                pc++;
        }
    }


    std::ostream *iosetup(size_t index, std::ostream *output);
    std::istream *iosetup(size_t index, std::istream *input);
    // ------------------------------------------------------------------------
    //    Setup an I/O stream for this runtime
    // ------------------------------------------------------------------------


    std::istream *istream(tok_t id)
    // ------------------------------------------------------------------------
    //   Return an input stream
    // ------------------------------------------------------------------------
    {
        if (id >= inputs.size())
        {
            error("Invalid input stream");
            return nullptr;
        }
        return inputs[id];
    }


    std::ostream *ostream(tok_t id)
    // ------------------------------------------------------------------------
    //   Return an output stream
    // ------------------------------------------------------------------------
    {
        if (id >= outputs.size())
        {
            error("Invalid output stream");
            return nullptr;
        }
        return outputs[id];
    }


    tok_t argv(size_t index)
    // ------------------------------------------------------------------------
    //   Return a text token for the given argument
    // ------------------------------------------------------------------------
    {
        if (index < cmdline.size())
            return tok(cmdline[index]);
        return op_null;
    }


    tok_t env(tok_t t)
    // ------------------------------------------------------------------------
    //   Return a text token for the given argument
    // ------------------------------------------------------------------------
    {
        text name = as_text(t);
        if (cstring env = getenv(name.c_str()))
            return tok(text(env));
        return op_null;
    }


    scanner *scanning(scanner *s)
    // ------------------------------------------------------------------------
    //   Switch to the given scanner, return previous one
    // ------------------------------------------------------------------------
    {
        std::swap(input, s);
        return s;
    }



    // ========================================================================
    //
    //    Scanner / parser configuration
    //
    // ========================================================================

    // Switch between bytecode scanning and high-level parsing modes
    bool parsing_mode(bool mode);

    // Select precedence for infix (A+B), prefix (+A), postfix (A%) and block
    bool symbol_precedence(cstring directive,
                           int *prec, text *symbol, text *closing = nullptr);
    void infix();
    void prefix();
    void postfix();
    void block();


public:
    // ========================================================================
    //
    //   Data for the runtime
    //
    // ========================================================================

    uint64_t            flags;          // Conditional flags (predicates)
    uintptr_t           jit_link;       // Saved native return address for -O
    void               *jit_code;       // Base of mmap'd JIT code (-O)
    size_t              jit_map_size;   // Mapped size for safe unmap after JIT
    std::map<uintptr_t, size_t> jit_pc_off; // Bytecode PC -> native offset
    byte_p              pc;             // Current PC
    byte_p              memory;         // Memory allocated for that runtime
    byte_p              heap;           // Top of heap (lo marker for stack)
    stack_p             stack;          // Pointer to current stack
    stack_p             frame;          // Pointer to locals
    stack_p             arg_locals;     // Caller locals for get/set (see enter/leave)
    stack_p             histack;        // High marker for stack
    cstring             err;            // Error message
    byte_p              entry;          // Main code entry point

    using pointers = std::vector<byte_p>;
    pointers            tokens;         // Table of tokens

    using token_index = std::unordered_map<std::string_view, tok_t>;
    token_index         token_by_text;  // Text to token lookup

    using callees= std::map<tok_t, tok_t>;
    callees             defined;        // Table of callees and globals

    using native_map = std::map<tok_t, native_fn>;
    native_map          natives;        // Native functions

    using native_list = std::vector<native_fn>;
    native_list         user_ops;       // User-defined opcodes

    using globals_list = std::vector<value_t>;
    globals_list        globals;        // Global environment

    using istreams = std::vector<std::istream *>;
    istreams            inputs;        // Output streams for the runtime

    using ostreams = std::vector<std::ostream *>;
    ostreams            outputs;        // Output streams for the runtime

    using argument_list = std::vector<text>;
    argument_list       cmdline;

    scanner            *input;
};

#endif // RUNTIME_H
