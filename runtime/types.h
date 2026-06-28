#ifndef TYPES_H
#  define TYPES_H
// ****************************************************************************
//  types.h                                                        XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Basic types for the XLR8 runtime
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

#include <cstddef>
#include <cstdint>
#include <string>

typedef unsigned int         uint;
typedef const char          *cstring;
typedef const char          *utf8;
typedef unsigned char        byte;
typedef const byte          *byte_p;
typedef const uint          *uint_p;
typedef byte_p               object_p;
typedef size_t               tok_t;
typedef intptr_t             value_t;
typedef uintptr_t            uvalue_t;
typedef value_t              broff_t;
typedef value_t             *stack_p;

typedef std::string          text;

#endif // TYPES_H
