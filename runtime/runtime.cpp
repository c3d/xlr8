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

runtime::runtime(byte_p base, size_t memory, size_t opcodes, run_fn *evaluators)
// ----------------------------------------------------------------------------
//    Initialize the runtime
// ----------------------------------------------------------------------------
    : evaluators(evaluators), opcodes(opcodes),
      pc(), ppc(), op(),
      error_message(), error_source(), error_source_length(),
      lomem(base),
      globals(base),
      heap(base),
      himem(base + memory)
{
    object_p *top = (object_p *) himem;
    for (uint a = 0; a < NUM_ROOTS; a++)
        root[a] = top;
}


#define ERROR(name, msg)                        \
runtime &runtime::name##_error()                \
{                                               \
    return error(msg);                          \
}
#include "errors.tbl"
